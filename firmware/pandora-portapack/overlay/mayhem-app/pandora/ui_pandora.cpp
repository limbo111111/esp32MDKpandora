#include "ui_pandora.hpp"
#include "i2cdevmanager.hpp"
#include "event_m0.hpp"
#include "transmitter_model.hpp"
#include "baseband_api.hpp"
#include "portapack_shared_memory.hpp"
#include "ui_font_fixed_5x8.hpp"
#include "message.hpp"

using namespace i2cdev;

namespace ui::external_app::pandora {

static uint32_t decode_u32_le(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

static void encode_u32_le(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
    bytes[2] = static_cast<uint8_t>(value >> 16);
    bytes[3] = static_cast<uint8_t>(value >> 24);
}

PandoraAppView::PandoraAppView(ui::NavigationView& nav) : nav_{nav} {
    set_style(ui::Theme::getInstance()->bg_dark);

    // Find the PPMod device
    I2CDevManager::manual_scan();
    // Found at: firmware/common/i2cdevmanager.cpp:254
    ppmod_dev_ = (I2cDev_PPmod*)I2CDevManager::get_dev_by_model(I2C_DEVMDL::I2CDECMDL_PPMOD);

    EventDispatcher::events_flag_timer = true;

    // Configure RX Baseband Processor for receiving raw edges
    // Found at: firmware/application/baseband_api.cpp:241
        baseband::run_image(portapack::spi_flash::image_tag_subghz_rx);
    // Found at: firmware/application/receiver_model.cpp:157
    receiver_model.set_sampling_rate(3072000);
    // Found at: firmware/application/receiver_model.cpp:164
    receiver_model.set_baseband_bandwidth(1750000);
    // Found at: firmware/application/baseband_api.cpp:404
    baseband::set_subghzd_config(0, receiver_model.sampling_rate());
    // Found at: firmware/application/receiver_model.cpp:106
    receiver_model.enable();
}

PandoraAppView::~PandoraAppView() {
    EventDispatcher::events_flag_timer = false;
    // Found at: firmware/application/receiver_model.cpp:118
    receiver_model.disable();
    // Found at: firmware/application/baseband_api.cpp:258
        baseband::shutdown();
}
void PandoraAppView::focus() {
}

void PandoraAppView::on_show() {
}

void PandoraAppView::on_hide() {
}

void PandoraAppView::on_timer() {
    if (!is_connected()) return;

    // Poll current RF frequency and mode
    poll_rf_config();

    if (tx_active_) {
        systime_t now = chVTGetSystemTime();
        // Wait until elapsed
        if ((systime_t)(now - tx_start_time_) >= tx_duration_) {
            transmitter_model.disable();
            baseband::shutdown();
            baseband::run_image(portapack::spi_flash::image_tag_subghz_rx);
            receiver_model.enable();
            tx_active_ = false;
        }
        return; // Skip framebuffer updates while transmitting to save CPU
    }

    if (!transfer_active_) {
        if (++poll_frames_ < 6) {
            return;
        }
        poll_frames_ = 0;

        uint32_t generation = 0;
        if (!read_generation(generation) || generation == 0 ||
            generation == rendered_generation_) {
            return;
        }
        transfer_active_ = true;
        transfer_offset_ = 0;
        transfer_generation_ = 0;
    }

    read_next_chunk();

    // Check for TX edges and forward RX edges
    if (!tx_active_) {
        process_tx_edges();
    }
}

bool PandoraAppView::on_key(const ui::KeyEvent event) {
    if (!is_connected()) return false;

    uint8_t btn_mask = 0;

    // We only process KeyEvents from Mayhem and convert to bits
    if (event == ui::KeyEvent::Up) btn_mask |= (1 << 0);
    if (event == ui::KeyEvent::Down) btn_mask |= (1 << 1);
    if (event == ui::KeyEvent::Select) btn_mask |= (1 << 2);
    if (event == ui::KeyEvent::Left) btn_mask |= (1 << 3);
    if (event == ui::KeyEvent::Right) btn_mask |= (1 << 4);
    // User requested B6. We map it to KeyEvent::DFU if available.
    if (event == ui::KeyEvent::DFU) btn_mask |= (1 << 5);

    send_button_state(btn_mask);
    return true;
}
void PandoraAppView::send_button_state(uint8_t mask) {
    uint8_t cmd[3];
    cmd[0] = static_cast<uint8_t>(PANDORA_DISPLAY_BUTTON_COMMAND & 0xFF);
    cmd[1] = static_cast<uint8_t>(PANDORA_DISPLAY_BUTTON_COMMAND >> 8);
    cmd[2] = mask;

    uint8_t dummy;
    ppmod_dev_->i2c_read(cmd, 3, &dummy, 0); // Write only
}

bool PandoraAppView::read_generation(uint32_t& generation) {
    uint8_t command[2] = {
        static_cast<uint8_t>(PANDORA_DISPLAY_META_COMMAND & 0xFF),
        static_cast<uint8_t>(PANDORA_DISPLAY_META_COMMAND >> 8)};
    uint8_t response[PANDORA_DISPLAY_RESPONSE_HEADER_BYTES]{};

    if (!ppmod_dev_->i2c_read(command, sizeof(command), response, sizeof(response))) {
        return false;
    }
    generation = decode_u32_le(response);
    return true;
}

void PandoraAppView::read_next_chunk() {
    uint8_t command[4] = {
        static_cast<uint8_t>(PANDORA_DISPLAY_FRAME_COMMAND & 0xFF),
        static_cast<uint8_t>(PANDORA_DISPLAY_FRAME_COMMAND >> 8),
        static_cast<uint8_t>(transfer_offset_ & 0xFF),
        static_cast<uint8_t>(transfer_offset_ >> 8)};
    uint8_t response[PANDORA_DISPLAY_RESPONSE_BYTES]{};

    if (!ppmod_dev_->i2c_read(command, sizeof(command), response, sizeof(response))) {
        transfer_active_ = false;
        return;
    }

    const uint32_t generation = decode_u32_le(response);
    if (generation == 0 ||
        (transfer_generation_ != 0 &&
         generation != transfer_generation_)) {
        transfer_active_ = false;
        return;
    }
    transfer_generation_ = generation;

    const size_t count = std::min<size_t>(
        PANDORA_DISPLAY_CHUNK_BYTES,
        PANDORA_DISPLAY_FRAME_BYTES - transfer_offset_);
    std::memcpy(candidate_.data() + transfer_offset_,
                response + PANDORA_DISPLAY_RESPONSE_HEADER_BYTES, count);
    transfer_offset_ += static_cast<uint16_t>(count);

    if (transfer_offset_ == PANDORA_DISPLAY_FRAME_BYTES) {
        frame_ = candidate_;
        rendered_generation_ = transfer_generation_;
        frame_ready_ = true;
        transfer_active_ = false;
        set_dirty();
    }
}


void PandoraAppView::poll_rf_config() {
    uint8_t cmd[2] = {
        static_cast<uint8_t>(PANDORA_DISPLAY_RF_CONFIG_COMMAND & 0xFF),
        static_cast<uint8_t>(PANDORA_DISPLAY_RF_CONFIG_COMMAND >> 8)};
    uint8_t response[PANDORA_DISPLAY_RESPONSE_BYTES]{};

    if (ppmod_dev_->i2c_read(cmd, sizeof(cmd), response, sizeof(response))) {
        uint32_t freq = decode_u32_le(response);
        uint8_t mode = response[4];
        if (freq != 0 && freq != current_rf_freq_) {
            current_rf_freq_ = freq;
            receiver_model.set_tuning_frequency(current_rf_freq_);
            transmitter_model.set_tuning_frequency(current_rf_freq_);
        }
        current_rf_mode_ = mode;
    }
}

void PandoraAppView::process_tx_edges() {
    uint8_t command[2] = {
        static_cast<uint8_t>(PANDORA_DISPLAY_TX_EDGE_COMMAND & 0xFF),
        static_cast<uint8_t>(PANDORA_DISPLAY_TX_EDGE_COMMAND >> 8)};
    uint8_t response[PANDORA_DISPLAY_RESPONSE_BYTES]{};

    // We poll until there's no more edge
    std::string bitstream;
    uint32_t symbol_duration_us = 500; // Assume 500us per bit for OOK bitstream as a default (2kHz symbol rate)

    while (ppmod_dev_->i2c_read(command, sizeof(command), response, sizeof(response))) {
        uint32_t duration = decode_u32_le(response + 1);
        if (duration == 0) {
            break; // No more edge
        }

        bool level = response[0] != 0;

        // Very basic conversion of variable-duration edge to fixed bitstream
        uint32_t num_bits = (duration + symbol_duration_us / 2) / symbol_duration_us;
        for (uint32_t i = 0; i < num_bits; i++) {
            bitstream.push_back(level ? '1' : '0');
        }
    }


    if (bitstream.length() > 0) {
        // Stop current baseband
        receiver_model.disable();
        baseband::shutdown();

        // Start the OOK baseband for TX
        baseband::run_image(portapack::spi_flash::image_tag_ook);

        transmitter_model.set_tuning_frequency(current_rf_freq_);
        transmitter_model.set_sampling_rate(2280000);
        transmitter_model.set_baseband_bandwidth(1750000);
        transmitter_model.enable();

        size_t bitstream_length = 0;
        uint8_t byte = 0;
        uint8_t* shared_bitstream = shared_memory.bb_data.data;

        for (auto c : bitstream) {
            byte <<= 1;
            if (c != '0')
                byte |= 1;

            // Bounds check on shared memory (max 256 bytes usually)
        if (bitstream_length >> 3 >= 250) {
            break; // Stop adding if we approach buffer limit
        }
        if ((bitstream_length & 7) == 7)
                shared_bitstream[bitstream_length >> 3] = byte;

            bitstream_length++;
        }

        size_t padding = 8 - (bitstream_length & 7);
        if (padding != 8) {
            byte <<= padding;
            shared_bitstream[(bitstream_length + padding - 1) >> 3] = byte;
        }

        baseband::set_ook_data(
            bitstream_length,
            2280000 / (1000000 / symbol_duration_us),
            1,
            0
        );

        // Async transmission wait
        tx_active_ = true;
        uint32_t wait_ms = (bitstream_length * symbol_duration_us) / 1000 + 10;
        tx_start_time_ = chVTGetSystemTime();
        tx_duration_ = chTimeMS2I(wait_ms);
    }
}

void PandoraAppView::paint(ui::Painter& painter) {
    const ui::Rect rect = screen_rect();
    const ui::Color background = ui::Color::black();
    painter.fill_rectangle(rect, background);
    painter.draw_string(
        {static_cast<ui::Coord>(rect.left() + 8),
         static_cast<ui::Coord>(rect.top() + 6)},
        ui::font::fixed_5x8(), ui::Color::light_grey(), background,
        "PANDORA VIEW  /  RF CAPABLE");

    if (!is_connected()) {
        painter.draw_string(
            {static_cast<ui::Coord>(rect.left() + 8),
             static_cast<ui::Coord>(rect.top() + 30)},
            ui::font::fixed_5x8(), ui::Color::red(), background,
            "PPMOD I2C DISCONNECTED");
        return;
    }

    if (!frame_ready_) {
        painter.draw_string(
            {static_cast<ui::Coord>(rect.left() + 8),
             static_cast<ui::Coord>(rect.top() + 30)},
            ui::font::fixed_5x8(), ui::Color::white(), background,
            "WAITING FOR EMULATOR FRAME");
        return;
    }

    constexpr int maxScale = 2;
    const int scale = std::max(
        1, std::min({maxScale,
                     static_cast<int>(rect.width()) /
                         PANDORA_DISPLAY_VISIBLE_COLUMNS,
                     (static_cast<int>(rect.height()) - 28) /
                         PANDORA_DISPLAY_ROWS}));
    const int imageWidth = PANDORA_DISPLAY_VISIBLE_COLUMNS * scale;
    const int imageHeight = PANDORA_DISPLAY_ROWS * scale;
    const int imageX = rect.left() + (rect.width() - imageWidth) / 2;
    const int imageY =
        rect.top() + 24 + (rect.height() - 24 - imageHeight) / 2;

    const std::array<ui::Color, 4> levels = {
        ui::Color::black(), ui::Color::dark_grey(),
        ui::Color::light_grey(), ui::Color::white()};
    std::array<ui::Color,
               PANDORA_DISPLAY_VISIBLE_COLUMNS * maxScale>
        line{};

    for (int y = 0; y < PANDORA_DISPLAY_ROWS; ++y) {
        for (int x = 0; x < PANDORA_DISPLAY_VISIBLE_COLUMNS; ++x) {
            const uint8_t packed =
                frame_[(y / 4) * PANDORA_DISPLAY_COLUMNS + x];
            const uint8_t level =
                static_cast<uint8_t>((packed >> (2 * (y % 4))) & 0x03);
            for (int xScale = 0; xScale < scale; ++xScale) {
                line[x * scale + xScale] = levels[level];
            }
        }

        for (int yScale = 0; yScale < scale; ++yScale) {
            painter.draw_pixels(
                {static_cast<ui::Coord>(imageX),
                 static_cast<ui::Coord>(imageY + y * scale + yScale),
                 static_cast<ui::Dim>(imageWidth), 1},
                line.data(), static_cast<size_t>(imageWidth));
        }
    }
}

} // namespace ui::external_app::pandora

void PandoraAppView::on_rx_edge(const SubGhzRawEdgeMessage& message) {
    if (!is_connected()) return;

    // Simple noise filter: don't send edges shorter than 50us (likely noise) to avoid flooding I2C
    if (message.duration < 50) return;

    // We should ideally queue these up and send them in chunks in on_timer,
    // but for this patch we'll just filter out the highest frequency noise.
    uint8_t cmd[7];
    cmd[0] = static_cast<uint8_t>(PANDORA_DISPLAY_RX_EDGE_COMMAND & 0xFF);
    cmd[1] = static_cast<uint8_t>(PANDORA_DISPLAY_RX_EDGE_COMMAND >> 8);
    cmd[2] = message.level ? 1 : 0;
    encode_u32_le(cmd + 3, message.duration);

    uint8_t dummy;
    ppmod_dev_->i2c_read(cmd, sizeof(cmd), &dummy, 0); // Write only
}

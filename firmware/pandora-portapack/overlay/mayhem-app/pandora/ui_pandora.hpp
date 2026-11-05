#pragma once

#include "ui.hpp"
#include "ui_navigation.hpp"
#include "ui_painter.hpp"
#include "ui_widget.hpp"
#include "i2cdev_ppmod.hpp"
#include "pandora_display_protocol.h"
#include <array>
#include "message.hpp"

namespace ui::external_app::pandora {

class PandoraAppView : public ui::View {
public:
    PandoraAppView(ui::NavigationView& nav);
    ~PandoraAppView();

    std::string title() const override { return "Pandora View"; }

    void focus() override;
    void on_show() override;
    void on_hide() override;

    void paint(ui::Painter& painter) override;

    bool on_key(const ui::KeyEvent event) override;

    // Timer callback
    void on_timer();

    void on_rx_edge(const SubGhzRawEdgeMessage& message);

private:
    ui::NavigationView& nav_;
    i2cdev::I2cDev_PPmod* ppmod_dev_ = nullptr;

    // Framebuffer State
    std::array<uint8_t, PANDORA_DISPLAY_FRAME_BYTES> frame_{};
    std::array<uint8_t, PANDORA_DISPLAY_FRAME_BYTES> candidate_{};
    uint32_t rendered_generation_ = 0;
    uint32_t transfer_generation_ = 0;
    uint16_t transfer_offset_ = 0;
    uint8_t poll_frames_ = 0;
    bool frame_ready_ = false;
    bool transfer_active_ = false;

    bool tx_active_ = false;
    systime_t tx_start_time_ = 0;
    systime_t tx_duration_ = 0;
    uint32_t current_rf_freq_ = 433920000;
    uint8_t current_rf_mode_ = 0; // 0 = idle, 1 = ready, 2 = rx, 3 = tx

    MessageHandlerRegistration message_handler_rx_edge {
        Message::ID::SubGhzRawEdge,
        [this](const Message* const p) {
            this->on_rx_edge(*static_cast<const SubGhzRawEdgeMessage*>(p));
        }
    };

    bool is_connected() const { return ppmod_dev_ != nullptr; }

    bool read_generation(uint32_t& generation);
    void read_next_chunk();
    void process_tx_edges();
    void send_button_state(uint8_t mask);
    void poll_rf_config();
};

} // namespace ui::external_app::pandora

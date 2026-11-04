#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pandora_display_protocol.h"
#include "ppi2c/pp_handler.hpp"
#include "uart_app.h"

extern "C" {
#include "emulator.h"
}

extern "C" {
extern const uint8_t pandora_firmware_start[]
    asm("_binary_firmware_bin_start");
extern const uint8_t pandora_firmware_end[]
    asm("_binary_firmware_bin_end");
}

namespace {

// Keep the same pin order as the current Mayhem MDK example.
constexpr gpio_num_t kMdkI2cFirstPin = GPIO_NUM_6;
constexpr gpio_num_t kMdkI2cSecondPin = GPIO_NUM_5;
constexpr uint8_t kI2cSlaveAddress = 0x51;
constexpr uint32_t kInstructionsPerSlice = 120000;
constexpr uint32_t kEmulatorTaskStackBytes = 8192;
constexpr uint8_t kAppCore = 1;

static_assert(sizeof(uart_app) % 32 == 0,
              "standalone app size must be a multiple of 32 bytes");
static_assert(OLED_FB_SIZE == PANDORA_DISPLAY_FRAME_BYTES,
              "display protocol must match the emulator framebuffer");
static_assert(OLED_COLS == PANDORA_DISPLAY_COLUMNS,
              "display protocol must match the emulator column stride");

static const char* kTag = "pandora-display";
static EmuState g_emulator{};
static portMUX_TYPE g_framebufferMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t g_liveFramebuffer[OLED_FB_SIZE]{};
static uint8_t g_transferSnapshot[OLED_FB_SIZE]{};
static uint32_t g_frameGeneration = 0;
static uint32_t g_snapshotGeneration = 0;
static uint16_t g_requestedOffset = 0;
static bool g_framebufferReady = false;
static bool g_snapshotReady = false;

void write_u32_le(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t read_u16_le(const std::vector<uint8_t>& data) {
    if (data.size() < 2) {
        return 0;
    }
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

void on_frame_offset(pp_command_data_t command) {
    const uint16_t offset =
        command.data == nullptr ? 0 : read_u16_le(*command.data);

    portENTER_CRITICAL_ISR(&g_framebufferMux);
    g_requestedOffset = offset;
    portEXIT_CRITICAL_ISR(&g_framebufferMux);
}

void send_frame_meta(pp_command_data_t response) {
    response.data->resize(4);
    portENTER_CRITICAL_ISR(&g_framebufferMux);
    write_u32_le(response.data->data(),
                 g_framebufferReady ? g_frameGeneration : 0);
    portEXIT_CRITICAL_ISR(&g_framebufferMux);
}

void send_frame_chunk(pp_command_data_t response) {
    response.data->assign(PANDORA_DISPLAY_RESPONSE_BYTES, 0);

    portENTER_CRITICAL_ISR(&g_framebufferMux);
    const uint16_t offset = g_requestedOffset;
    if (offset == 0) {
        g_snapshotReady = g_framebufferReady;
        g_snapshotGeneration = g_frameGeneration;
        if (g_snapshotReady) {
            std::memcpy(g_transferSnapshot, g_liveFramebuffer,
                        sizeof(g_transferSnapshot));
        }
    }

    uint8_t* output = response.data->data();
    write_u32_le(output, g_snapshotReady ? g_snapshotGeneration : 0);

    if (g_snapshotReady && offset < PANDORA_DISPLAY_FRAME_BYTES) {
        const size_t count = std::min<size_t>(
            PANDORA_DISPLAY_CHUNK_BYTES,
            PANDORA_DISPLAY_FRAME_BYTES - offset);
        std::memcpy(output + PANDORA_DISPLAY_RESPONSE_HEADER_BYTES,
                    g_transferSnapshot + offset, count);
    }
    portEXIT_CRITICAL_ISR(&g_framebufferMux);
}

void publish_framebuffer() {
    uint8_t nextFrame[OLED_FB_SIZE];
    emu_get_fb(&g_emulator, nextFrame);

    portENTER_CRITICAL(&g_framebufferMux);
    if (!g_framebufferReady ||
        std::memcmp(nextFrame, g_liveFramebuffer, sizeof(nextFrame)) != 0) {
        std::memcpy(g_liveFramebuffer, nextFrame, sizeof(g_liveFramebuffer));
        ++g_frameGeneration;
        g_framebufferReady = true;
    }
    portEXIT_CRITICAL(&g_framebufferMux);
}

void emulator_task(void*) {
    const uint32_t firmwareSize =
        static_cast<uint32_t>(pandora_firmware_end - pandora_firmware_start);
    const int initResult = emu_init(
        &g_emulator, &PROFILE_PANDORA_HW, pandora_firmware_start,
        firmwareSize);

    if (initResult != 0) {
        ESP_LOGE(kTag, "emulator initialization failed: %d", initResult);
        vTaskDelete(nullptr);
        return;
    }

    /*
     * Boot without installing a key sequence or injecting button input. The
     * framebuffer is the only emulator data exposed to the PortaPack app.
     */
    emu_set_knock(&g_emulator, nullptr, 0, -1);
    emu_reset_and_unlock(&g_emulator);
    publish_framebuffer();

    uint32_t lastOledFrame = g_emulator.oled_frames;
    ESP_LOGI(kTag, "display-only emulator ready; radio bridge is not included");

    for (;;) {
        emu_run_burst(&g_emulator, kInstructionsPerSlice);
        if (g_emulator.oled_frames != lastOledFrame) {
            lastOledFrame = g_emulator.oled_frames;
            publish_framebuffer();
        }
        vTaskDelay(1);
    }
}

}  // namespace

extern "C" void app_main(void) {
    PPHandler::set_module_name("PANDORA-DISPLAY");
    PPHandler::set_module_version(1);
    if (!PPHandler::add_app(uart_app, sizeof(uart_app))) {
        ESP_LOGE(kTag, "could not register the PortaPack display app");
        return;
    }

    PPHandler::add_custom_command(
        PANDORA_DISPLAY_META_COMMAND, nullptr, send_frame_meta);
    PPHandler::add_custom_command(
        PANDORA_DISPLAY_FRAME_COMMAND, on_frame_offset, send_frame_chunk);
    PPHandler::init(kMdkI2cFirstPin, kMdkI2cSecondPin, kI2cSlaveAddress);

    const BaseType_t taskResult = xTaskCreatePinnedToCore(
        emulator_task, "pandora_display", kEmulatorTaskStackBytes, nullptr,
        5, nullptr, kAppCore);
    if (taskResult != pdPASS) {
        ESP_LOGE(kTag, "could not start the emulator task");
    }
}

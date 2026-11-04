#include <cstdint>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pandora_display_protocol.h"
#include "pandora_display_link.hpp"
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

void publish_framebuffer() {
    uint8_t nextFrame[OLED_FB_SIZE];
    emu_get_fb(&g_emulator, nextFrame);
    pandora_display_link_publish_frame(nextFrame);
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

    pandora_display_link_init();
    PPHandler::init(kMdkI2cFirstPin, kMdkI2cSecondPin, kI2cSlaveAddress);

    const BaseType_t taskResult = xTaskCreatePinnedToCore(
        emulator_task, "pandora_display", kEmulatorTaskStackBytes, nullptr,
        5, nullptr, kAppCore);
    if (taskResult != pdPASS) {
        ESP_LOGE(kTag, "could not start the emulator task");
    }
}

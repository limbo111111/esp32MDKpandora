#include <array>
#include <cstdint>
#include <cstring>

#include "display_test_pattern.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pandora_display_link.hpp"
#include "pandora_display_protocol.h"

namespace {

constexpr int kVisibleWidth = PANDORA_DISPLAY_VISIBLE_COLUMNS;
constexpr int kHeight = PANDORA_DISPLAY_ROWS;
constexpr int kTaskStackBytes = 3072;
constexpr int kTaskCore = 1;
constexpr char kTag[] = "display-test";

void set_pixel(uint8_t* framebuffer, int x, int y, uint8_t level) {
    if (x < 0 || x >= kVisibleWidth || y < 0 || y >= kHeight) {
        return;
    }

    const size_t index =
        static_cast<size_t>(y / 4) * PANDORA_DISPLAY_COLUMNS + x;
    const uint8_t shift = static_cast<uint8_t>((y % 4) * 2);
    const uint8_t mask = static_cast<uint8_t>(0x03u << shift);
    framebuffer[index] = static_cast<uint8_t>(
        (framebuffer[index] & static_cast<uint8_t>(~mask)) |
        static_cast<uint8_t>((level & 0x03u) << shift));
}

void draw_test_frame(uint8_t* framebuffer, uint32_t phase) {
    std::memset(framebuffer, 0, PANDORA_DISPLAY_FRAME_BYTES);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kVisibleWidth; ++x) {
            set_pixel(framebuffer, x, y, 1);
        }
    }

    for (int x = 0; x < kVisibleWidth; ++x) {
        set_pixel(framebuffer, x, 0, 3);
        set_pixel(framebuffer, x, kHeight - 1, 3);
    }
    for (int y = 0; y < kHeight; ++y) {
        set_pixel(framebuffer, 0, y, 3);
        set_pixel(framebuffer, kVisibleWidth - 1, y, 3);
    }

    for (int level = 0; level < 4; ++level) {
        const int left = 7 + level * 19;
        for (int y = 13; y < 42; ++y) {
            for (int x = left; x < left + 14; ++x) {
                set_pixel(framebuffer, x, y, static_cast<uint8_t>(level));
            }
        }
    }

    for (int x = 4; x < kVisibleWidth - 4; ++x) {
        set_pixel(framebuffer, x, 47, static_cast<uint8_t>((x / 4) % 4));
    }

    const int markerX = 4 + static_cast<int>(phase % (kVisibleWidth - 8));
    for (int y = 53; y < 59; ++y) {
        for (int x = markerX; x < markerX + 3; ++x) {
            set_pixel(framebuffer, x, y, 3);
        }
    }
}

void display_test_task(void*) {
    std::array<uint8_t, PANDORA_DISPLAY_FRAME_BYTES> framebuffer{};
    uint32_t phase = 0;

    for (;;) {
        draw_test_frame(framebuffer.data(), phase);
        pandora_display_link_publish_frame(framebuffer.data());
        phase += 3;
        vTaskDelay(pdMS_TO_TICKS(180));
    }
}

}  // namespace

void display_test_pattern_start() {
    const BaseType_t result = xTaskCreatePinnedToCore(
        display_test_task, "display_test", kTaskStackBytes, nullptr, 5,
        nullptr, kTaskCore);
    if (result != pdPASS) {
        ESP_LOGE(kTag, "could not start the display test pattern");
    }
}

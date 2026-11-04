#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "pandora_display_protocol.h"
#include "ppi2c/pp_handler.hpp"

namespace {

static portMUX_TYPE g_framebufferMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t g_liveFramebuffer[PANDORA_DISPLAY_FRAME_BYTES]{};
static uint8_t g_transferSnapshot[PANDORA_DISPLAY_FRAME_BYTES]{};
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

}  // namespace

void pandora_display_link_init() {
    PPHandler::add_custom_command(
        PANDORA_DISPLAY_META_COMMAND, nullptr, send_frame_meta);
    PPHandler::add_custom_command(
        PANDORA_DISPLAY_FRAME_COMMAND, on_frame_offset, send_frame_chunk);
}

void pandora_display_link_publish_frame(const uint8_t* framebuffer) {
    if (framebuffer == nullptr) {
        return;
    }

    portENTER_CRITICAL(&g_framebufferMux);
    if (!g_framebufferReady ||
        std::memcmp(framebuffer, g_liveFramebuffer,
                    sizeof(g_liveFramebuffer)) != 0) {
        std::memcpy(g_liveFramebuffer, framebuffer,
                    sizeof(g_liveFramebuffer));
        ++g_frameGeneration;
        g_framebufferReady = true;
    }
    portEXIT_CRITICAL(&g_framebufferMux);
}

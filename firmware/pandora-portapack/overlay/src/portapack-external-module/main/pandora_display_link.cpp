#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "pandora_display_protocol.h"
#include "ppi2c/pp_handler.hpp"
#include "pandora_display_link.hpp"

namespace {

static portMUX_TYPE g_framebufferMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t g_liveFramebuffer[PANDORA_DISPLAY_FRAME_BYTES]{};
static uint8_t g_transferSnapshot[PANDORA_DISPLAY_FRAME_BYTES]{};
static uint32_t g_frameGeneration = 0;
static uint32_t g_snapshotGeneration = 0;
static uint16_t g_requestedOffset = 0;
static bool g_framebufferReady = false;
static bool g_snapshotReady = false;
static EmuState* g_emulator = nullptr;

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

void on_button_command(pp_command_data_t command) {
    if (g_emulator && command.data && command.data->size() >= 1) {
        uint8_t btn_mask = (*command.data)[0];
        // B_UP = 0, B_DOWN = 1, B_OK = 2, B_BACK = 3, B5 = 4, B6 = 5
        emu_set_button(g_emulator, B_UP, (btn_mask & (1 << 0)) != 0);
        emu_set_button(g_emulator, B_DOWN, (btn_mask & (1 << 1)) != 0);
        emu_set_button(g_emulator, B_OK, (btn_mask & (1 << 2)) != 0);
        emu_set_button(g_emulator, B_BACK, (btn_mask & (1 << 3)) != 0);
        emu_set_button(g_emulator, B5, (btn_mask & (1 << 4)) != 0);
        emu_set_button(g_emulator, B6, (btn_mask & (1 << 5)) != 0);
    }
}

void on_rx_edge_command(pp_command_data_t command) {
    if (g_emulator && command.data && command.data->size() >= 5) {
        const uint8_t* d = command.data->data();
        bool level = d[0] != 0;
        uint32_t duration = d[1] | (d[2] << 8) | (d[3] << 16) | (d[4] << 24);
        emu_rx_inject_edge(g_emulator, level, duration);
    }
}

void send_tx_edge_chunk(pp_command_data_t response) {
    response.data->assign(PANDORA_DISPLAY_RESPONSE_BYTES, 0);

    if (g_emulator) {
        RfEdge edge;
        if (emu_tx_pop_edge(g_emulator, &edge)) {
            uint8_t* out = response.data->data();
            out[0] = edge.level ? 1 : 0;
            write_u32_le(out + 1, edge.duration);
        }
    }
}

}  // namespace

void pandora_display_link_init(EmuState* emulator) {
    g_emulator = emulator;

    PPHandler::add_custom_command(
        PANDORA_DISPLAY_META_COMMAND, nullptr, send_frame_meta);
    PPHandler::add_custom_command(
        PANDORA_DISPLAY_FRAME_COMMAND, on_frame_offset, send_frame_chunk);
    PPHandler::add_custom_command(
        PANDORA_DISPLAY_BUTTON_COMMAND, on_button_command, nullptr);
    PPHandler::add_custom_command(
        PANDORA_DISPLAY_TX_EDGE_COMMAND, nullptr, send_tx_edge_chunk);
    PPHandler::add_custom_command(
        PANDORA_DISPLAY_RX_EDGE_COMMAND, on_rx_edge_command, nullptr);
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

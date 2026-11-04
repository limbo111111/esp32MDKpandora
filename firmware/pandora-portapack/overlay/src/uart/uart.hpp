/*
 * Copyright (C) 2024 Bernd Herzog
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "pandora_display_protocol.h"
#include "standalone_application.hpp"
#include "standaloneviewmirror.hpp"
#include "ui/theme.hpp"
#include "ui/ui_font_fixed_5x8.hpp"
#include "ui/ui_navigation.hpp"
#include "ui/ui_painter.hpp"
#include "ui/ui_widget.hpp"

namespace ui {

class PandoraDisplayView : public ui::View {
   public:
    PandoraDisplayView(ui::NavigationView& nav) {
        (void)nav;
        set_style(ui::Theme::getInstance()->bg_dark);
        set_focusable(false);
    }

    std::string title() const override {
        return "Pandora Display";
    }

    void on_framesync() override {
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
    }

    void paint(ui::Painter& painter) override {
        const ui::Rect rect = screen_rect();
        const ui::Color background = ui::Color::black();
        painter.fill_rectangle(rect, background);
        painter.draw_string(
            {static_cast<ui::Coord>(rect.left() + 8),
             static_cast<ui::Coord>(rect.top() + 6)},
            ui::font::fixed_5x8(), ui::Color::light_grey(), background,
            "PANDORA VIEW  /  RF DISABLED");

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

   private:
    static uint32_t decode_u32_le(const uint8_t* bytes) {
        return static_cast<uint32_t>(bytes[0]) |
               (static_cast<uint32_t>(bytes[1]) << 8) |
               (static_cast<uint32_t>(bytes[2]) << 16) |
               (static_cast<uint32_t>(bytes[3]) << 24);
    }

    bool read_generation(uint32_t& generation) {
        uint8_t command[2] = {
            static_cast<uint8_t>(PANDORA_DISPLAY_META_COMMAND & 0xFF),
            static_cast<uint8_t>(PANDORA_DISPLAY_META_COMMAND >> 8)};
        uint8_t response[PANDORA_DISPLAY_RESPONSE_HEADER_BYTES]{};
        if (!_api->i2c_read(command, sizeof(command), response,
                            sizeof(response))) {
            return false;
        }
        generation = decode_u32_le(response);
        return true;
    }

    void read_next_chunk() {
        uint8_t command[4] = {
            static_cast<uint8_t>(PANDORA_DISPLAY_FRAME_COMMAND & 0xFF),
            static_cast<uint8_t>(PANDORA_DISPLAY_FRAME_COMMAND >> 8),
            static_cast<uint8_t>(transfer_offset_ & 0xFF),
            static_cast<uint8_t>(transfer_offset_ >> 8)};
        uint8_t response[PANDORA_DISPLAY_RESPONSE_BYTES]{};
        if (!_api->i2c_read(command, sizeof(command), response,
                            sizeof(response))) {
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

    std::array<uint8_t, PANDORA_DISPLAY_FRAME_BYTES> frame_{};
    std::array<uint8_t, PANDORA_DISPLAY_FRAME_BYTES> candidate_{};
    uint32_t rendered_generation_ = 0;
    uint32_t transfer_generation_ = 0;
    uint16_t transfer_offset_ = 0;
    uint8_t poll_frames_ = 0;
    bool frame_ready_ = false;
    bool transfer_active_ = false;
};

}  // namespace ui

#ifndef PANDORA_DISPLAY_LINK_HPP
#define PANDORA_DISPLAY_LINK_HPP

#include <cstdint>

#if !defined(PANDORA_DISPLAY_TEST_PATTERN)
extern "C" {
#include "emulator.h"
}
#else
struct EmuState;
#endif

void pandora_display_link_init(EmuState* emulator);
void pandora_display_link_publish_frame(const uint8_t* framebuffer);

#endif

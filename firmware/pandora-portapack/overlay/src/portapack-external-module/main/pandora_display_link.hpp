#ifndef PANDORA_DISPLAY_LINK_HPP
#define PANDORA_DISPLAY_LINK_HPP

#include <cstdint>

void pandora_display_link_init();
void pandora_display_link_publish_frame(const uint8_t* framebuffer);

#endif

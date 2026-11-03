#pragma once
#include <cstdint>
// On success the pointer remains valid until the next load or gallery_close.
const uint16_t *gallery_load_image(const char *name);
void gallery_close_image();

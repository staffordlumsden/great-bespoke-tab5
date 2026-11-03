#pragma once
#include <stddef.h>
#include <stdint.h>

/* RGB565 luminance, quantized back to RGB565 for preview and JPEG encoder. */
static inline uint16_t tab5_mono_rgb565(uint16_t pixel) {
    uint32_t r = ((pixel >> 11) & 31U) * 255U / 31U;
    uint32_t g = ((pixel >> 5) & 63U) * 255U / 63U;
    uint32_t b = (pixel & 31U) * 255U / 31U;
    uint32_t y = (77U * r + 150U * g + 29U * b + 128U) >> 8;
    return (uint16_t)(((y >> 3) << 11) | ((y >> 2) << 5) | (y >> 3));
}
static inline void tab5_mono_buffer(uint16_t *pixels, size_t count) {
    for (size_t i = 0; i < count; ++i) pixels[i] = tab5_mono_rgb565(pixels[i]);
}

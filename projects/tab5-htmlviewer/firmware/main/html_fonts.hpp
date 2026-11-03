#pragma once

#include "html_core.hpp"
#include <cstddef>
#include <cstdint>

namespace tab5_html {

struct FontMetrics {
    int ascent_px;
    int descent_px;
    int line_height_px;
    int space_advance_px;
};

// Initialize the embedded OFL TrueType faces and allocate the glyph bitmap
// cache in SPIRAM/heap. HTML family names map to Source Sans 3, Source Serif 4
// and Source Code Pro compatibility faces.
bool fonts_init() noexcept;

// Measure line metrics for a given family, pixel height, and bold/italic flags.
FontMetrics font_get_metrics(FontFamily family, uint8_t size_px, bool bold, bool italic) noexcept;

// Measure horizontal advance in pixels of a UTF-8 string slice.
int font_measure_utf8(FontFamily family,
                      uint8_t size_px,
                      bool bold,
                      bool italic,
                      const char* utf8,
                      size_t byte_len) noexcept;

// Measure a single Unicode codepoint's horizontal advance in pixels.
int font_measure_codepoint(FontFamily family,
                           uint8_t size_px,
                           bool bold,
                           bool italic,
                           uint32_t codepoint) noexcept;

// Decode one UTF-8 codepoint from [s, s + max_len), returning bytes consumed (1..4).
size_t decode_utf8_one(const char* s, size_t max_len, uint32_t& out_cp) noexcept;

// Render a UTF-8 string slice with 8-level anti-aliased blending over bg_rgb565.
// `x` is the left edge, `y` is the top of the line box (baseline is at y + metrics.ascent_px).
// Returns the horizontal advance in pixels.
int font_draw_utf8(FontFamily family,
                   uint8_t size_px,
                   bool bold,
                   bool italic,
                   int x,
                   int y,
                   const char* utf8,
                   size_t byte_len,
                   uint16_t fg_rgb565,
                   uint16_t bg_rgb565,
                   int clip_top = 0,
                   int clip_bottom = 9999) noexcept;

}  // namespace tab5_html

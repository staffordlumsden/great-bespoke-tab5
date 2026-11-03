#pragma once

#include <cstddef>
#include <cstdint>
#include "note_model.h"

namespace quicknote::terminal {

enum class ZoomLevel : uint8_t {
    compact,    // 8pt (12x20)
    readable,   // 10pt (16x27, default)
    large,      // 15pt (24x40)
};

struct Metrics {
    uint16_t columns;
    uint16_t rows;
    uint8_t cell_width;
    uint8_t cell_height;
    uint8_t font_points;
};

Metrics metrics(ZoomLevel zoom);

class View {
public:
    void init();
    void set_zoom(ZoomLevel zoom);
    ZoomLevel zoom() const { return zoom_; }
    uint8_t font_points() const;

    // Draw the full terminal canvas
    void draw(const Editor& editor, bool history_mode, const char* history_text,
              size_t history_size, int history_scroll, bool cursor_blink);

    // Scroll operations for history mode
    int max_scroll(const char* text, size_t size) const;
    void scroll_page_up(int& scroll) const;
    void scroll_page_down(int& scroll, size_t content_size, const char* text) const;

    // Hit test: returns true if touch was inside terminal canvas
    bool handle_touch(int x, int y, Editor& editor, bool history_mode,
                      int& history_scroll, const char* history_text, size_t history_size);

    uint16_t visible_rows() const { return rows_; }
    uint16_t visible_cols() const { return cols_; }
    int scroll_row() const { return scroll_row_; }

private:
    ZoomLevel zoom_{ZoomLevel::readable};
    uint16_t cols_{74};
    uint16_t rows_{21};
    uint8_t cell_w_{16};
    uint8_t cell_h_{27};
    int scroll_row_{0};
};

}  // namespace quicknote::terminal

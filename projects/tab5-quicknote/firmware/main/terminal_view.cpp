#include "terminal_view.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "fonts/terminal_fonts.h"
#include "tab5_hal.h"

namespace quicknote::terminal {

namespace {

constexpr int kTermX = 10;
constexpr int kTermY = 52;
constexpr int kTermW = 1260;
constexpr int kTermH = 580;

constexpr int kBorderWidth = 2;
constexpr int kInnerX = kTermX + kBorderWidth;
constexpr int kInnerY = kTermY + kBorderWidth;
constexpr int kInnerW = kTermW - 2 * kBorderWidth;
constexpr int kInnerH = kTermH - 2 * kBorderWidth;

// Authentic terminal palette
constexpr uint32_t kTerminalBg = 0x020406;        // Deep terminal black
constexpr uint32_t kBorderColor = 0x334155;       // Frame border
constexpr uint32_t kGutterBg = 0x080c12;          // Subtle gutter background
constexpr uint32_t kGutterText = 0x475569;        // Gutter line numbers
constexpr uint32_t kGutterDivider = 0x1e293b;     // Gutter vertical rule
constexpr uint32_t kTextColor = 0xe8edf2;         // Crisp monospace text
constexpr uint32_t kHeadingColor = 0x56a8ff;      // Markdown heading (# ...)
constexpr uint32_t kTimestampColor = 0x55d187;    // Markdown timestamp (- HH:MM)
constexpr uint32_t kCursorColor = 0xf5b84b;       // Terminal amber block cursor
constexpr uint32_t kMutedColor = 0x93a4b7;        // Muted text

struct VisualLine {
    size_t start;
    size_t end;
    size_t logical_line;
};

bool rect(int x, int y, int width, int height, uint32_t colour) {
    if (width <= 0 || height <= 0) return true;
    return tab5_display_rect(x, y, width, height, colour) == ESP_OK;
}

const tab5_terminal_fonts::Font& terminal_font(ZoomLevel zoom, bool bold) {
    using namespace tab5_terminal_fonts;
    switch (zoom) {
        case ZoomLevel::compact:
            return bold ? tab5_terminal_mono_8pt_bold : tab5_terminal_mono_8pt_regular;
        case ZoomLevel::readable:
            return bold ? tab5_terminal_mono_10pt_bold : tab5_terminal_mono_10pt_regular;
        case ZoomLevel::large:
            return bold ? tab5_terminal_mono_15pt_bold : tab5_terminal_mono_15pt_regular;
    }
    return bold ? tab5_terminal_mono_10pt_bold : tab5_terminal_mono_10pt_regular;
}

size_t glyph_index(char32_t character, const tab5_terminal_fonts::Font& font) {
    size_t low = 0;
    size_t high = font.glyph_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (font.codepoints[middle] < character)
            low = middle + 1;
        else
            high = middle;
    }
    if (low < font.glyph_count && font.codepoints[low] == character) return low;

    // Default to '?' if glyph not found
    low = 0;
    high = font.glyph_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (font.codepoints[middle] < U'?')
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

bool draw_glyph(char32_t character, int x, int y,
                const tab5_terminal_fonts::Font& font,
                uint32_t colour) {
    const uint8_t* bitmap = font.bitmap +
        glyph_index(character, font) * font.glyph_stride;
    const int row_bytes = (font.cell_width + 7) / 8;
    bool ok = true;
    for (int row = 0; row < font.cell_height; ++row) {
        int column = 0;
        while (column < font.cell_width) {
            const auto set = [&](int candidate) {
                const uint8_t byte = bitmap[row * row_bytes + candidate / 8];
                return (byte & (0x80 >> (candidate % 8))) != 0;
            };
            while (column < font.cell_width && !set(column)) ++column;
            const int start = column;
            while (column < font.cell_width && set(column)) ++column;
            if (start < column)
                ok = rect(x + start, y + row, column - start, 1, colour) && ok;
        }
    }
    return ok;
}

// Slice buffer into visual lines based on max columns and newlines
size_t compute_visual_lines(const char* content, size_t size, uint16_t max_cols,
                            VisualLine* lines, size_t max_lines) {
    if (!content || !size || !lines || !max_lines) {
        if (lines && max_lines) {
            lines[0] = {0, 0, 1};
            return 1;
        }
        return 0;
    }

    size_t count = 0;
    size_t pos = 0;
    size_t logical_line = 1;

    while (pos < size && count < max_lines) {
        size_t line_start = pos;
        size_t col = 0;

        while (pos < size && content[pos] != '\n' && col < max_cols) {
            ++pos;
            ++col;
        }

        lines[count++] = {line_start, pos, logical_line};

        if (pos < size && content[pos] == '\n') {
            ++pos;
            ++logical_line;
        }
    }

    // Trailing newline creates an empty line at the end
    if (size > 0 && content[size - 1] == '\n' && count < max_lines) {
        lines[count++] = {size, size, logical_line};
    }

    return count ? count : 1;
}

}  // namespace

Metrics metrics(ZoomLevel zoom) {
    switch (zoom) {
        case ZoomLevel::compact:  // 8pt: 12x20
            return {96, 28, 12, 20, 8};
        case ZoomLevel::readable: // 10pt: 16x27
            return {74, 21, 16, 27, 10};
        case ZoomLevel::large:    // 15pt: 24x40
            return {49, 14, 24, 40, 15};
    }
    return {74, 21, 16, 27, 10};
}

void View::init() {
    set_zoom(ZoomLevel::readable);
}

void View::set_zoom(ZoomLevel zoom) {
    zoom_ = zoom;
    const auto m = metrics(zoom);
    cols_ = m.columns;
    rows_ = m.rows;
    cell_w_ = m.cell_width;
    cell_h_ = m.cell_height;
}

uint8_t View::font_points() const {
    return metrics(zoom_).font_points;
}

int View::max_scroll(const char* text, size_t size) const {
    static VisualLine lines[1024];
    size_t total = compute_visual_lines(text, size, cols_, lines, 1024);
    if (total <= rows_) return 0;
    return static_cast<int>(total - rows_);
}

void View::scroll_page_up(int& scroll) const {
    scroll = std::max(0, scroll - static_cast<int>(rows_));
}

void View::scroll_page_down(int& scroll, size_t content_size, const char* text) const {
    const int max_s = max_scroll(text, content_size);
    scroll = std::min(max_s, scroll + static_cast<int>(rows_));
}

bool View::handle_touch(int x, int y, Editor& editor, bool history_mode,
                        int& history_scroll, const char* history_text, size_t history_size) {
    if (x < kInnerX || x >= kInnerX + kInnerW || y < kInnerY || y >= kInnerY + kInnerH) {
        return false;
    }

    if (history_mode) {
        // Tapping upper half scrolls up, lower half scrolls down
        if (y < kInnerY + kInnerH / 2) {
            scroll_page_up(history_scroll);
        } else {
            scroll_page_down(history_scroll, history_size, history_text);
        }
        return true;
    }

    // In Edit mode: calculate clicked row and column to set cursor
    const int gutter_width = 4 * cell_w_ + 8;
    const int text_x = kInnerX + gutter_width;
    if (x < text_x) return true;

    static VisualLine lines[1024];
    const size_t total_lines = compute_visual_lines(editor.data(), editor.size(), cols_, lines, 1024);

    const int clicked_row = scroll_row_ + (y - kInnerY) / cell_h_;
    if (clicked_row < 0 || static_cast<size_t>(clicked_row) >= total_lines) {
        editor.set_cursor(editor.size());
        return true;
    }

    const auto& line = lines[clicked_row];
    const int clicked_col = (x - text_x) / cell_w_;
    const size_t line_len = line.end - line.start;
    const size_t col_offset = std::min<size_t>(std::max(0, clicked_col), line_len);

    editor.set_cursor(line.start + col_offset);
    return true;
}

void View::draw(const Editor& editor, bool history_mode, const char* history_text,
                size_t history_size, int history_scroll, bool cursor_blink) {
    const auto& regular_font = terminal_font(zoom_, false);
    const auto& bold_font = terminal_font(zoom_, true);

    // 1. Draw outer border
    rect(kTermX, kTermY, kTermW, kBorderWidth, kBorderColor);
    rect(kTermX, kTermY + kTermH - kBorderWidth, kTermW, kBorderWidth, kBorderColor);
    rect(kTermX, kTermY, kBorderWidth, kTermH, kBorderColor);
    rect(kTermX + kTermW - kBorderWidth, kTermY, kBorderWidth, kTermH, kBorderColor);

    // 2. Gutter layout
    const int gutter_cells = 4;
    const int gutter_width = gutter_cells * cell_w_ + 8;
    const int text_x = kInnerX + gutter_width;
    const int text_w = kInnerW - gutter_width;

    // Clear background
    rect(kInnerX, kInnerY, gutter_width, kInnerH, kGutterBg);
    rect(kInnerX + gutter_width - 2, kInnerY, 2, kInnerH, kGutterDivider);
    rect(text_x, kInnerY, text_w, kInnerH, kTerminalBg);

    // 3. Prepare visual lines
    static VisualLine lines[1024];
    const char* content = history_mode ? history_text : editor.data();
    const size_t content_size = history_mode ? history_size : editor.size();

    const size_t total_lines = compute_visual_lines(content, content_size, cols_, lines, 1024);

    // Find cursor visual line and position
    size_t cursor_v_row = 0;
    size_t cursor_v_col = 0;
    if (!history_mode) {
        for (size_t i = 0; i < total_lines; ++i) {
            if (editor.cursor() >= lines[i].start &&
                (editor.cursor() <= lines[i].end || (i + 1 == total_lines))) {
                cursor_v_row = i;
                cursor_v_col = editor.cursor() - lines[i].start;
                break;
            }
        }
        // Keep cursor in view with 1 row margin
        if (cursor_v_row < static_cast<size_t>(scroll_row_)) {
            scroll_row_ = static_cast<int>(cursor_v_row);
        } else if (cursor_v_row >= static_cast<size_t>(scroll_row_ + rows_)) {
            scroll_row_ = static_cast<int>(cursor_v_row - rows_ + 1);
        }
    } else {
        scroll_row_ = std::clamp(history_scroll, 0, max_scroll(content, content_size));
    }

    // 4. Render visible rows
    for (uint16_t row = 0; row < rows_; ++row) {
        const int current_v_row = scroll_row_ + row;
        const int line_y = kInnerY + row * cell_h_;

        if (current_v_row >= static_cast<int>(total_lines)) {
            // Empty row past end of document: draw subtle '~' in gutter like vi
            draw_glyph(U'~', kInnerX + (gutter_cells - 1) * cell_w_, line_y,
                       regular_font, kGutterDivider);
            continue;
        }

        const auto& line = lines[current_v_row];

        // Gutter: print logical line number if this is the start of a logical line
        bool is_continuation = current_v_row > 0 &&
                               lines[current_v_row - 1].logical_line == line.logical_line;
        if (!is_continuation) {
            char num_str[8];
            std::snprintf(num_str, sizeof(num_str), "%3u", static_cast<unsigned>(line.logical_line));
            for (int ci = 0; ci < 3; ++ci) {
                if (num_str[ci] && num_str[ci] != ' ') {
                    draw_glyph(static_cast<char32_t>(num_str[ci]),
                               kInnerX + ci * cell_w_ + 4, line_y,
                               regular_font, kGutterText);
                }
            }
        } else {
            // Continuation marker '·'
            draw_glyph(U'\u00b7', kInnerX + 2 * cell_w_ + 4, line_y,
                       regular_font, kGutterDivider);
        }

        // Determine line styling
        const size_t line_len = line.end - line.start;
        bool is_heading = false;
        bool is_item = false;
        if (line_len > 0 && content) {
            if (line.start < content_size && content[line.start] == '#') {
                is_heading = true;
            } else if (line_len >= 2 && content[line.start] == '-' && content[line.start + 1] == ' ') {
                is_item = true;
            }
        }

        // Draw characters across the line
        for (size_t c = 0; c < line_len && c < cols_; ++c) {
            const size_t char_idx = line.start + c;
            const unsigned char ch = static_cast<unsigned char>(content[char_idx]);
            const int char_x = text_x + static_cast<int>(c) * cell_w_;

            uint32_t color = kTextColor;
            const auto* font_ptr = &regular_font;

            if (is_heading) {
                color = kHeadingColor;
                font_ptr = &bold_font;
            } else if (is_item && c >= 2 && c <= 7) {
                // Timestamp '- HH:MM' highlighted in green
                color = kTimestampColor;
                font_ptr = &bold_font;
            }

            // Draw character
            if (ch >= 32 && ch <= 126) {
                draw_glyph(static_cast<char32_t>(ch), char_x, line_y, *font_ptr, color);
            }
        }

        // Terminal block cursor
        if (!history_mode && static_cast<size_t>(current_v_row) == cursor_v_row) {
            if (cursor_blink) {
                const int cursor_x = text_x + static_cast<int>(cursor_v_col) * cell_w_;
                rect(cursor_x, line_y, cell_w_, cell_h_, kCursorColor);
                if (cursor_v_col < line_len) {
                    const unsigned char ch = static_cast<unsigned char>(content[line.start + cursor_v_col]);
                    if (ch >= 32 && ch <= 126) {
                        draw_glyph(static_cast<char32_t>(ch), cursor_x, line_y,
                                   regular_font, kTerminalBg);
                    }
                }
            }
        }
    }

    // In history mode: if document is empty
    if (history_mode && (!content_size || total_lines == 0)) {
        const char* msg = "No notes recorded for this date.";
        const int len = static_cast<int>(std::strlen(msg));
        const int start_x = text_x + (text_w - len * cell_w_) / 2;
        const int msg_y = kInnerY + (rows_ / 2) * cell_h_;
        for (int i = 0; i < len; ++i) {
            draw_glyph(static_cast<char32_t>(msg[i]), start_x + i * cell_w_, msg_y,
                       regular_font, kMutedColor);
        }
    }

    // Flush terminal window to ST7121 display
    tab5_display_present_region(kTermX, kTermY, kTermW, kTermH);
}

}  // namespace quicknote::terminal

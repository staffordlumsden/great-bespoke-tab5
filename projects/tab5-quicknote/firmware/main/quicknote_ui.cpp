#include "quicknote_ui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "fonts/pixel_fonts.h"
#include "tab5_hal.h"

namespace quicknote::ui {

namespace {

constexpr uint32_t kBackground  = 0x080b10;
constexpr uint32_t kPanel       = 0x111823;
constexpr uint32_t kPanelRaised = 0x1a2431;
constexpr uint32_t kBorder      = 0x334155;
constexpr uint32_t kText        = 0xe8edf2;
constexpr uint32_t kMuted       = 0x93a4b7;
constexpr uint32_t kBlue        = 0x56a8ff;
constexpr uint32_t kGreen       = 0x55d187;
constexpr uint32_t kAmber       = 0xf5b84b;
constexpr uint32_t kRed         = 0xff6b6b;

constexpr int kHeaderH = 48;
constexpr int kStatusY = 636;
constexpr int kStatusH = 22;
constexpr int kToolbarY = 660;
constexpr int kToolbarH = 56;

// 6 touch buttons across width
constexpr int kBtnStarts[6] = {10, 222, 434, 646, 848, 1050};
constexpr int kBtnWidths[6] = {190, 190, 190, 180, 180, 220};

bool rect(int x, int y, int width, int height, uint32_t colour) {
    if (width <= 0 || height <= 0) return true;
    return tab5_display_rect(x, y, width, height, colour) == ESP_OK;
}

int text_width(const char* value, const cyd_fonts::Font& font, int scale = 1) {
    if (!value) return 0;
    int width = 0;
    for (const auto* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        unsigned character = *p;
        if (character < font.first || character > font.last) character = '?';
        width += (font.glyphs[character - font.first].advance + 1) * scale;
    }
    return width ? width - scale : 0;
}

bool glyph(unsigned character, int x, int y, const cyd_fonts::Font& font,
           uint32_t colour, int scale = 1) {
    if (character < font.first || character > font.last) character = '?';
    const auto& g = font.glyphs[character - font.first];
    const int baseline = y + font.ascent * scale;
    bool ok = true;
    for (unsigned gy = 0; gy < g.height; ++gy) {
        for (unsigned gx = 0; gx < g.width; ++gx) {
            const unsigned bit = gy * g.width + gx;
            if (!(font.bitmap[g.offset + bit / 8] & (0x80 >> (bit % 8)))) continue;
            ok = rect(x + (g.x + static_cast<int>(gx)) * scale,
                      baseline + (g.y + static_cast<int>(gy)) * scale,
                      scale, scale, colour) && ok;
        }
    }
    return ok;
}

bool text(const char* value, int x, int y, const cyd_fonts::Font& font,
          uint32_t colour, int scale = 1) {
    if (!value) return true;
    bool ok = true;
    int cursor = x;
    for (const auto* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        unsigned character = *p;
        if (character < font.first || character > font.last) character = '?';
        const auto& g = font.glyphs[character - font.first];
        ok = glyph(character, cursor, y, font, colour, scale) && ok;
        cursor += (g.advance + 1) * scale;
    }
    return ok;
}

bool centered(const char* value, int center_x, int y, const cyd_fonts::Font& font,
              uint32_t colour, int scale = 1) {
    return text(value, center_x - text_width(value, font, scale) / 2,
                y, font, colour, scale);
}

bool button(int x, int y, int width, int height, const char* label,
            uint32_t border_colour, uint32_t text_colour) {
    bool ok = rect(x, y, width, height, kPanelRaised);
    ok = rect(x, y, width, 2, border_colour) && ok;
    ok = rect(x, y + height - 2, width, 2, border_colour) && ok;
    ok = rect(x, y, 2, height, border_colour) && ok;
    ok = rect(x + width - 2, y, 2, height, border_colour) && ok;
    ok = centered(label, x + width / 2, y + 16,
                  cyd_fonts::pixelfont_11x5_thin, text_colour, 2) && ok;
    return ok;
}

}  // namespace

void Chrome::init() {
    tab5_display_set_rotation(TAB5_ROTATION_0);
}

void Chrome::draw_header(const UiState& state) {
    rect(0, 0, TAB5_DISPLAY_WIDTH, kHeaderH, kPanel);
    rect(0, kHeaderH - 1, TAB5_DISPLAY_WIDTH, 1, kBorder);

    // Title & Version
    text("Tab5 QuickNote", 16, 10, cyd_fonts::pixelfont_13x7_semibold, kText, 2);

    char font_str[32];
    std::snprintf(font_str, sizeof(font_str), "TERMINAL MONO %u PT",
                  static_cast<unsigned>(state.font_points));
    text(font_str, 270, 16, cyd_fonts::pixelfont_7x5_thin, kBlue, 2);

    // Date & Time in header
    char clock_str[64];
    std::snprintf(clock_str, sizeof(clock_str), "%s   %s", state.date_str, state.time_str);
    centered(clock_str, 660, 14, cyd_fonts::pixelfont_11x5_thin,
             state.clock_valid ? kText : kAmber, 2);

    // Battery telemetry
    char bat_str[32];
    std::snprintf(bat_str, sizeof(bat_str), "%.2fV %u%%%s",
                  state.battery_voltage, static_cast<unsigned>(state.battery_percent),
                  state.charging ? " CHG" : "");
    text(bat_str, 960, 14, cyd_fonts::pixelfont_11x5_thin,
         state.charging ? kGreen : (state.battery_percent < 20 ? kRed : kText), 2);

    // Keyboard state
    const char* kbd_label = state.tab5_kbd_connected
                                ? "KBD: TAB5"
                                : (state.usb_kbd_connected ? "KBD: USB" : "KBD: NONE");
    uint32_t kbd_color = (state.tab5_kbd_connected || state.usb_kbd_connected)
                             ? kGreen : kMuted;
    text(kbd_label, 1140, 16, cyd_fonts::pixelfont_7x5_thin, kbd_color, 2);

    tab5_display_present_region(0, 0, TAB5_DISPLAY_WIDTH, kHeaderH);
}

void Chrome::draw_status_bar(const UiState& state) {
    rect(0, kStatusY, TAB5_DISPLAY_WIDTH, kStatusH, kPanel);
    rect(0, kStatusY, TAB5_DISPLAY_WIDTH, 1, kBorder);

    // Status message
    uint32_t col = state.status_color ? state.status_color : kMuted;
    const char* msg = state.status_msg;
    if (!msg || !msg[0]) {
        if (!state.clock_valid) {
            msg = "Clock not set - saving will use fallback date";
            col = kAmber;
        } else if (state.history_mode) {
            msg = "Reading daily log - Tap EDIT to write notes";
            col = kBlue;
        } else if (state.dirty) {
            msg = "Unsaved draft - Ctrl+S saves to Daily log";
            col = kAmber;
        } else {
            msg = "Ready. Type note. Enter adds newline. Ctrl+S saves.";
            col = kMuted;
        }
    }
    text(msg, 16, kStatusY + 4, cyd_fonts::pixelfont_11x5_thin, col, 2);

    // Stats on right
    char stats_str[64];
    if (state.history_mode) {
        if (state.history_lines_total > 0) {
            std::snprintf(stats_str, sizeof(stats_str), "Lines: %u total",
                          static_cast<unsigned>(state.history_lines_total));
        } else {
            std::snprintf(stats_str, sizeof(stats_str), "No entries");
        }
    } else {
        std::snprintf(stats_str, sizeof(stats_str), "Ln %u, Col %u  |  %u chars, %u words",
                      static_cast<unsigned>(state.cursor_line),
                      static_cast<unsigned>(state.cursor_col),
                      static_cast<unsigned>(state.char_count),
                      static_cast<unsigned>(state.word_count));
    }
    text(stats_str, 920, kStatusY + 6, cyd_fonts::pixelfont_7x5_thin, kMuted, 2);

    tab5_display_present_region(0, kStatusY, TAB5_DISPLAY_WIDTH, kStatusH);
}

void Chrome::draw_toolbar(const UiState& state) {
    rect(0, kToolbarY, TAB5_DISPLAY_WIDTH, kToolbarH, kBackground);
    rect(0, kToolbarY, TAB5_DISPLAY_WIDTH, 1, kBorder);

    if (state.history_mode) {
        // [EDIT] [PREV DAY] [NEXT DAY] [A-] [A+] [CONSOLE]
        button(kBtnStarts[0], kToolbarY + 4, kBtnWidths[0], 48, "EDIT", kBlue, kText);
        button(kBtnStarts[1], kToolbarY + 4, kBtnWidths[1], 48, "PREV DAY", kBorder, kText);
        button(kBtnStarts[2], kToolbarY + 4, kBtnWidths[2], 48, "NEXT DAY",
               state.day_offset < 0 ? kBorder : kPanel,
               state.day_offset < 0 ? kText : kMuted);
    } else {
        // [HISTORY] [SAVE] [CLEAR] [A-] [A+] [CONSOLE]
        button(kBtnStarts[0], kToolbarY + 4, kBtnWidths[0], 48, "HISTORY", kBlue, kText);
        button(kBtnStarts[1], kToolbarY + 4, kBtnWidths[1], 48, "SAVE",
               state.char_count > 0 ? kGreen : kBorder,
               state.char_count > 0 ? kGreen : kMuted);
        const char* clear_label = state.clear_armed ? "SURE?" : "CLEAR";
        uint32_t clear_color = state.clear_armed ? kRed : kBorder;
        button(kBtnStarts[2], kToolbarY + 4, kBtnWidths[2], 48, clear_label, clear_color,
               state.clear_armed ? kRed : (state.char_count > 0 ? kText : kMuted));
    }

    button(kBtnStarts[3], kToolbarY + 4, kBtnWidths[3], 48, "A-",
           state.font_points > 8 ? kBorder : kPanel,
           state.font_points > 8 ? kText : kMuted);
    button(kBtnStarts[4], kToolbarY + 4, kBtnWidths[4], 48, "A+",
           state.font_points < 15 ? kBorder : kPanel,
           state.font_points < 15 ? kText : kMuted);
    button(kBtnStarts[5], kToolbarY + 4, kBtnWidths[5], 48, "CONSOLE", kAmber, kAmber);

    tab5_display_present_region(0, kToolbarY, TAB5_DISPLAY_WIDTH, kToolbarH);
}

void Chrome::draw_all(const UiState& state) {
    rect(0, 0, TAB5_DISPLAY_WIDTH, TAB5_DISPLAY_HEIGHT, kBackground);
    draw_header(state);
    draw_status_bar(state);
    draw_toolbar(state);
}

Action Chrome::hit_test(int x, int y) const {
    if (y < kToolbarY || y >= kToolbarY + kToolbarH) return Action::none;

    for (int i = 0; i < 6; ++i) {
        if (x >= kBtnStarts[i] && x < kBtnStarts[i] + kBtnWidths[i]) {
            switch (i) {
                case 0: return Action::history_toggle;
                case 1: return Action::save_or_prev;
                case 2: return Action::clear_or_next;
                case 3: return Action::zoom_out;
                case 4: return Action::zoom_in;
                case 5: return Action::console;
            }
        }
    }
    return Action::none;
}

}  // namespace quicknote::ui

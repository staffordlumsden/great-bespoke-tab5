#include "tab5_terminal_display.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "pixel_fonts.h"
#include "terminal_fonts.h"
#include "tab5_hal.h"

namespace tab5_terminal {
namespace {

constexpr int kTermX = 8;
constexpr int kTermY = 54;
constexpr int kTermW = 960;
constexpr int kCollapsedTermW = 1200;
constexpr int kTermH = 600;
constexpr int kSideX = 976;
constexpr int kSideY = 54;
constexpr int kSideW = 296;
constexpr int kSideH = 600;
constexpr int kCollapsedSideX = 1216;
constexpr int kCollapsedSideW = 56;
constexpr int kFooterY = 662;
constexpr int kFooterH = 58;

constexpr uint32_t kBackground = 0x080b10;
constexpr uint32_t kPanel = 0x111823;
constexpr uint32_t kPanelRaised = 0x1a2431;
constexpr uint32_t kBorder = 0x334155;
constexpr uint32_t kText = 0xe8edf2;
constexpr uint32_t kMuted = 0x93a4b7;
constexpr uint32_t kBlue = 0x56a8ff;
constexpr uint32_t kGreen = 0x55d187;
constexpr uint32_t kAmber = 0xf5b84b;
constexpr uint32_t kRed = 0xff6b6b;
constexpr uint32_t kTerminalBackground = 0x020406;

struct GridMetrics {
    uint16_t columns;
    uint16_t rows;
    uint8_t cell_width;
    uint8_t cell_height;
    uint8_t font_points;
};

constexpr GridMetrics metrics(ZoomLevel level) {
    switch (level) {
        case ZoomLevel::compact: return {80, 30, 12, 20, 8};
        case ZoomLevel::readable: return {60, 22, 16, 27, 10};
        case ZoomLevel::large: return {40, 15, 24, 40, 15};
    }
    return {60, 22, 16, 27, 10};
}

constexpr uint32_t kAnsiPalette[16] = {
    0x000000, 0xc00000, 0x00c000, 0xc0c000,
    0x0000c0, 0xc000c0, 0x00c0c0, 0xc0c0c0,
    0x404040, 0xff4040, 0x40ff40, 0xffff40,
    0x4040ff, 0xff40ff, 0x40ffff, 0xffffff,
};

bool rect(int x, int y, int width, int height, uint32_t colour) {
    if (width <= 0 || height <= 0) return true;
    return tab5_display_rect(x, y, width, height, colour) == ESP_OK;
}

int terminal_width(bool sidebar_collapsed) {
    return sidebar_collapsed ? kCollapsedTermW : kTermW;
}

const tab5_terminal_fonts::Font& terminal_font(ZoomLevel level, bool bold) {
    using namespace tab5_terminal_fonts;
    switch (level) {
        case ZoomLevel::compact:
            return bold ? tab5_terminal_mono_8pt_bold : tab5_terminal_mono_8pt_regular;
        case ZoomLevel::readable:
            return bold ? tab5_terminal_mono_10pt_bold : tab5_terminal_mono_10pt_regular;
        case ZoomLevel::large:
            return bold ? tab5_terminal_mono_15pt_bold : tab5_terminal_mono_15pt_regular;
    }
    return bold ? tab5_terminal_mono_10pt_bold : tab5_terminal_mono_10pt_regular;
}

size_t terminal_glyph_index(char32_t character,
                            const tab5_terminal_fonts::Font& font) {
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

    // Printable ASCII '?' is guaranteed to be in every generated atlas.
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

bool terminal_glyph(char32_t character, int x, int y,
                    const tab5_terminal_fonts::Font& font,
                    uint32_t colour) {
    const uint8_t* bitmap = font.bitmap +
        terminal_glyph_index(character, font) * font.glyph_stride;
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

void fit_text(char* out, size_t capacity, const char* value,
              const cyd_fonts::Font& font, int scale, int maximum_width) {
    if (!out || capacity == 0) return;
    if (!value) value = "--";
    std::snprintf(out, capacity, "%s", value);
    if (text_width(out, font, scale) <= maximum_width) return;

    const size_t available = capacity > 4 ? capacity - 4 : 0;
    const size_t source_length = std::min(std::strlen(value), available);
    for (size_t keep = source_length; keep > 0; --keep) {
        std::memcpy(out, value, keep);
        std::memcpy(out + keep, "...", 4);
        if (text_width(out, font, scale) <= maximum_width) return;
    }
    std::snprintf(out, capacity, "--");
}

uint32_t dim(uint32_t colour) {
    const uint32_t r = ((colour >> 16) & 0xff) / 2;
    const uint32_t g = ((colour >> 8) & 0xff) / 2;
    const uint32_t b = (colour & 0xff) / 2;
    return (r << 16) | (g << 8) | b;
}

uint32_t terminal_colour(term::Color colour, uint32_t fallback) {
    using Kind = term::Color::Kind;
    if (colour.kind == Kind::Default) return fallback;
    if (colour.kind == Kind::Rgb) return colour.value & 0xffffff;
    const uint8_t index = static_cast<uint8_t>(colour.value & 0xff);
    if (index < 16) return kAnsiPalette[index];
    if (index >= 232) {
        const uint8_t value = static_cast<uint8_t>((index - 232) * 10 + 8);
        return (uint32_t(value) << 16) | (uint32_t(value) << 8) | value;
    }
    const uint8_t cube = static_cast<uint8_t>(index - 16);
    const uint8_t red = static_cast<uint8_t>((cube / 36) * 51);
    const uint8_t green = static_cast<uint8_t>(((cube / 6) % 6) * 51);
    const uint8_t blue = static_cast<uint8_t>((cube % 6) * 51);
    return (uint32_t(red) << 16) | (uint32_t(green) << 8) | blue;
}

const char* session_label(SessionState state) {
    switch (state) {
        case SessionState::starting: return "STARTING";
        case SessionState::waiting_wifi: return "WAITING FOR WIFI";
        case SessionState::connecting: return "CONNECTING SSH";
        case SessionState::connected: return "SSH CONNECTED";
        case SessionState::disconnected: return "DISCONNECTED";
        case SessionState::failed: return "CONNECTION FAILED";
    }
    return "UNKNOWN";
}

uint32_t session_colour(SessionState state) {
    switch (state) {
        case SessionState::connected: return kGreen;
        case SessionState::failed: return kRed;
        case SessionState::disconnected: return kAmber;
        default: return kBlue;
    }
}

bool button(int x, int y, int width, int height, const char* label,
            uint32_t colour) {
    bool ok = rect(x, y, width, height, kPanelRaised);
    ok = rect(x, y, width, 2, colour) && ok;
    ok = rect(x, y + height - 2, width, 2, colour) && ok;
    ok = rect(x, y, 2, height, colour) && ok;
    ok = rect(x + width - 2, y, 2, height, colour) && ok;
    ok = centered(label, x + width / 2, y + 14,
                  cyd_fonts::pixelfont_11x5_thin, colour, 2) && ok;
    return ok;
}

bool header_button(int x, const char* label) {
    constexpr int y = 6;
    constexpr int width = 72;
    constexpr int height = 36;
    bool ok = rect(x, y, width, height, kPanelRaised);
    ok = rect(x, y, width, 2, kBlue) && ok;
    ok = rect(x, y + height - 2, width, 2, kBlue) && ok;
    ok = rect(x, y, 2, height, kBlue) && ok;
    ok = rect(x + width - 2, y, 2, height, kBlue) && ok;
    ok = centered(label, x + width / 2, y + 6,
                  cyd_fonts::pixelfont_11x5_thin, kText, 2) && ok;
    return ok;
}

bool panel_contents(const UiStatus& status) {
    constexpr int kTextX = kSideX + 18;
    constexpr int kTextWidth = kSideW - 36;
    const auto& side_font = cyd_fonts::pixelfont_11x5_thin;
    char fitted[64];
    bool ok = rect(kSideX, kSideY, kSideW, kSideH, kPanel);
    ok = text("SESSION", kTextX, 66, side_font, kMuted, 2) && ok;
    const char* session = status.config_ready ? session_label(status.session)
                                              : "PROFILE REQUIRED";
    ok = text(session, kTextX, 94, side_font,
              status.config_ready ? session_colour(status.session) : kAmber, 2) && ok;

    ok = text("WIFI", kTextX, 138, side_font, kMuted, 2) && ok;
    fit_text(fitted, sizeof(fitted),
             status.ssid && status.ssid[0] ? status.ssid : "--",
             side_font, 2, kTextWidth);
    ok = text(fitted, kTextX, 166, side_font, kText, 2) && ok;
    char network[96];
    std::snprintf(network, sizeof(network), "%s / %d dBm",
                  status.ip && status.ip[0] ? status.ip : "--", status.rssi);
    fit_text(fitted, sizeof(fitted), network, side_font, 2, kTextWidth);
    ok = text(fitted, kTextX, 194, side_font, kMuted, 2) && ok;

    ok = text("C6 FIRMWARE", kTextX, 236, side_font, kMuted, 2) && ok;
    char version[64];
    if (status.c6_known) {
        std::snprintf(version, sizeof(version), "%u.%u.%u  %s",
                      static_cast<unsigned>(status.c6_major),
                      static_cast<unsigned>(status.c6_minor),
                      static_cast<unsigned>(status.c6_patch),
                      status.c6_compatible ? "OK" : "MISMATCH");
    } else {
        std::snprintf(version, sizeof(version), "CHECKING...");
    }
    ok = text(version, kTextX, 264, side_font,
              status.c6_known && !status.c6_compatible ? kRed : kText, 2) && ok;
    ok = text("FACTORY / NO UPDATE", kTextX, 292, side_font, kGreen, 2) && ok;

    ok = text("SSH TARGET", kTextX, 334, side_font, kMuted, 2) && ok;
    fit_text(fitted, sizeof(fitted),
             status.host_label && status.host_label[0] ? status.host_label : "--",
             side_font, 2, kTextWidth);
    ok = text(fitted, kTextX, 362, side_font,
              status.config_ready ? kText : kMuted, 2) && ok;
    if (status.session == SessionState::failed && status.failure) {
        fit_text(fitted, sizeof(fitted), status.failure, side_font, 2, kTextWidth);
        ok = text(fitted, kTextX, 390, side_font, kRed, 2) && ok;
    }

    char input_title[32];
    std::snprintf(input_title, sizeof(input_title), "INPUT / FONT %u PT",
                  static_cast<unsigned>(status.font_points));
    ok = text(input_title, kTextX, 424, side_font, kMuted, 2) && ok;
    const char* tab5_input = status.tab5_keyboard_input_verified
                                 ? "TAB5: KEY VERIFIED"
                                 : (status.tab5_keyboard_connected
                                        ? "TAB5: CONNECTED"
                                        : "TAB5: NOT FOUND");
    ok = text(tab5_input, kTextX, 452, side_font,
              status.tab5_keyboard_input_verified ? kGreen
                  : (status.tab5_keyboard_connected ? kBlue : kAmber), 2) && ok;
    const char* usb_input = status.usb_keyboard_input_verified
                                ? "USB-A: KEY VERIFIED"
                                : (status.usb_keyboard_connected
                                       ? "USB-A: CONNECTED"
                                       : "USB-A: READY");
    ok = text(usb_input, kTextX, 480, side_font,
              status.usb_keyboard_input_verified ? kGreen
                  : (status.usb_keyboard_connected ? kBlue : kMuted), 2) && ok;

    ok = button(kSideX + 16, 514, kSideW - 32, 52, "RECONNECT", kBlue) && ok;
    ok = button(kSideX + 16, 584, kSideW - 32, 52, "CONSOLE", kAmber) && ok;
    return ok;
}

bool collapsed_panel_contents(const UiStatus& status) {
    bool ok = rect(kCollapsedSideX, kSideY, kCollapsedSideW, kSideH, kPanel);
    ok = rect(kCollapsedSideX, kSideY, 3, kSideH,
              session_colour(status.session)) && ok;
    ok = centered("SSH", kCollapsedSideX + kCollapsedSideW / 2, 76,
                  cyd_fonts::pixelfont_7x5_thin,
                  session_colour(status.session), 1) && ok;
    ok = centered(">", kCollapsedSideX + kCollapsedSideW / 2, 116,
                  cyd_fonts::pixelfont_11x5_thin, kText, 2) && ok;
    ok = centered("INFO", kCollapsedSideX + kCollapsedSideW / 2, 154,
                  cyd_fonts::pixelfont_7x5_thin, kMuted, 1) && ok;
    return ok;
}

bool side_contents(const UiStatus& status, bool collapsed) {
    return collapsed ? collapsed_panel_contents(status) : panel_contents(status);
}

bool footer_contents(const UiStatus& status) {
    bool ok = rect(0, kFooterY, TAB5_DISPLAY_WIDTH, kFooterH, kBackground);
    ok = rect(0, kFooterY, TAB5_DISPLAY_WIDTH, 2, kBorder) && ok;
    ok = text(status.profile_message ? status.profile_message : "--", 16, 666,
              cyd_fonts::pixelfont_11x5_thin,
              status.config_ready ? kGreen : kAmber, 2) && ok;
    char controls[160];
    if (status.scroll_offset > 0) {
        std::snprintf(controls, sizeof(controls),
                      "%u x %u  FONT %u PT  |  SCROLL %u / %u  |  SWIPE OR SHIFT+PGUP/PGDN",
                      static_cast<unsigned>(status.terminal_columns),
                      static_cast<unsigned>(status.terminal_rows),
                      static_cast<unsigned>(status.font_points),
                      static_cast<unsigned>(status.scroll_offset),
                      static_cast<unsigned>(status.scrollback_rows));
    } else {
        std::snprintf(controls, sizeof(controls),
                      "%u x %u  FONT %u PT  |  CTRL +/- ZOOM  |  SWIPE OR SHIFT+PGUP/PGDN",
                      static_cast<unsigned>(status.terminal_columns),
                      static_cast<unsigned>(status.terminal_rows),
                      static_cast<unsigned>(status.font_points));
    }
    ok = text(controls, 16, 696, cyd_fonts::pixelfont_7x5_thin, kMuted, 1) && ok;
    ok = text("(c) Great Bespoke 2026", 1068, 696,
              cyd_fonts::pixelfont_7x5_thin, kMuted, 1) && ok;
    return ok;
}

}  // namespace

term::Result<void> Tab5TerminalDisplay::init() {
    if (tab5_display_set_rotation(TAB5_ROTATION_0) != ESP_OK)
        return std::unexpected(term::Error::BackendError);
    return {};
}

term::Result<void> Tab5TerminalDisplay::draw_cells(
    uint16_t row, uint16_t col, std::span<const term::Cell> cells) {
    if (row >= rows_ || col >= columns_ || cells.size() > columns_ - col)
        return std::unexpected(term::Error::OutOfRange);

    const int grid_x = kTermX +
        (terminal_width(sidebar_collapsed_) - static_cast<int>(columns_) * cell_width_) / 2;
    const int grid_y = kTermY + (kTermH - static_cast<int>(rows_) * cell_height_) / 2;
    bool ok = true;
    for (size_t index = 0; index < cells.size(); ++index) {
        const auto& cell = cells[index];
        if (cell.attrs.wide_cont) continue;

        term::Color foreground_source = cell.fg;
        if (cell.attrs.bold && foreground_source.kind == term::Color::Kind::Index &&
            foreground_source.value < 8) {
            foreground_source = term::Color::indexed(
                static_cast<uint8_t>(foreground_source.value + 8));
        }
        uint32_t foreground = terminal_colour(foreground_source, kText);
        uint32_t background = terminal_colour(cell.bg, kTerminalBackground);
        if (cell.attrs.faint) foreground = dim(foreground);
        if (cell.attrs.hidden) foreground = background;
        if (cell.attrs.reverse) std::swap(foreground, background);

        const int x = grid_x + (static_cast<int>(col) + static_cast<int>(index)) * cell_width_;
        const int y = grid_y + static_cast<int>(row) * cell_height_;
        const int width = cell.attrs.wide ? cell_width_ * 2 : cell_width_;
        ok = rect(x, y, width, cell_height_, background) && ok;
        if (cell.ch != U' ' && cell.ch != 0) {
            const auto& font = terminal_font(zoom_level_, cell.attrs.bold);
            const int glyph_x = x + std::max(0, (width - font.cell_width) / 2);
            const int glyph_y = y +
                std::max(0, (static_cast<int>(cell_height_) - font.cell_height) / 2);
            ok = terminal_glyph(cell.ch, glyph_x, glyph_y, font, foreground) && ok;
        }
        const int decoration = zoom_level_ == ZoomLevel::large ? 2 : 1;
        const int inset = std::max(1, decoration);
        if (cell.attrs.underline)
            ok = rect(x + inset, y + cell_height_ - decoration - 1,
                      width - 2 * inset, decoration, foreground) && ok;
        if (cell.attrs.strike)
            ok = rect(x + inset, y + cell_height_ / 2,
                      width - 2 * inset, decoration, foreground) && ok;
    }
    return ok ? term::Result<void>{}
              : term::Result<void>{std::unexpected(term::Error::BackendError)};
}

term::Result<void> Tab5TerminalDisplay::flush(term::DamageRect damage) {
    if (damage.empty()) return {};
    if (!present_enabled_) return {};
    const int grid_x = kTermX +
        (terminal_width(sidebar_collapsed_) - static_cast<int>(columns_) * cell_width_) / 2;
    const int grid_y = kTermY + (kTermH - static_cast<int>(rows_) * cell_height_) / 2;
    const int x = grid_x + damage.col0 * cell_width_;
    const int y = grid_y + damage.row0 * cell_height_;
    const int width = (damage.col1 - damage.col0) * cell_width_;
    const int height = (damage.row1 - damage.row0) * cell_height_;
    return tab5_display_present_region(x, y, width, height) == ESP_OK
               ? term::Result<void>{}
               : term::Result<void>{std::unexpected(term::Error::BackendError)};
}

bool Tab5TerminalDisplay::draw_chrome(const UiStatus& status) {
    session_connected_ = status.session == SessionState::connected;
    const int active_term_width = terminal_width(sidebar_collapsed_);
    bool ok = rect(0, 0, TAB5_DISPLAY_WIDTH, TAB5_DISPLAY_HEIGHT, kBackground);
    ok = rect(0, 0, TAB5_DISPLAY_WIDTH, 48, kPanel) && ok;
    ok = rect(0, 47, TAB5_DISPLAY_WIDTH, 1, kBorder) && ok;
    ok = text("TAB5 Terminal", 16, 9, cyd_fonts::pixelfont_13x7_semibold,
              kText, 2) && ok;
    ok = text("v0.1.4  -  TERMINAL MONO 10 PT", 248, 16,
              cyd_fonts::pixelfont_7x5_thin, kBlue, 2) && ok;
    if (session_connected_) ok = header_button(1016, "INFO") && ok;
    ok = header_button(1100, "A-") && ok;
    ok = header_button(1184, "A+") && ok;
    ok = rect(kTermX - 2, kTermY - 2, active_term_width + 4,
              kTermH + 4, kBorder) && ok;
    ok = rect(kTermX, kTermY, active_term_width, kTermH,
              kTerminalBackground) && ok;
    ok = side_contents(status, sidebar_collapsed_) && ok;
    ok = footer_contents(status) && ok;
    return ok;
}

bool Tab5TerminalDisplay::draw_status(const UiStatus& status) {
    session_connected_ = status.session == SessionState::connected;
    const int side_x = sidebar_collapsed_ ? kCollapsedSideX : kSideX;
    const int side_width = sidebar_collapsed_ ? kCollapsedSideW : kSideW;
    const bool ok = side_contents(status, sidebar_collapsed_) && footer_contents(status);
    const bool side = tab5_display_present_region(side_x, kSideY,
                                                   side_width, kSideH) == ESP_OK;
    const bool footer = tab5_display_present_region(0, kFooterY,
                                                     TAB5_DISPLAY_WIDTH, kFooterH) == ESP_OK;
    return ok && side && footer;
}

bool Tab5TerminalDisplay::present_all() {
    present_enabled_ = true;
    return tab5_display_present() == ESP_OK;
}

bool Tab5TerminalDisplay::draw_view(std::span<const term::Cell> cells) {
    if (cells.size() != static_cast<size_t>(columns_) * rows_) return false;
    const int active_term_width = terminal_width(sidebar_collapsed_);
    bool ok = rect(kTermX, kTermY, active_term_width, kTermH,
                   kTerminalBackground);
    for (uint16_t row = 0; row < rows_; ++row) {
        auto line = cells.subspan(static_cast<size_t>(row) * columns_, columns_);
        ok = draw_cells(row, 0, line).has_value() && ok;
    }
    if (present_enabled_)
        ok = tab5_display_present_region(kTermX, kTermY,
                                         active_term_width, kTermH) == ESP_OK && ok;
    return ok;
}

bool Tab5TerminalDisplay::set_zoom(ZoomLevel level) {
    const auto next = metrics(level);
    zoom_level_ = level;
    columns_ = next.columns;
    rows_ = next.rows;
    cell_width_ = next.cell_width;
    cell_height_ = next.cell_height;
    font_points_ = next.font_points;
    return rect(kTermX, kTermY, terminal_width(sidebar_collapsed_),
                kTermH, kTerminalBackground);
}

bool Tab5TerminalDisplay::set_sidebar_collapsed(bool collapsed) {
    if (sidebar_collapsed_ == collapsed) return false;
    sidebar_collapsed_ = collapsed;
    return true;
}

bool Tab5TerminalDisplay::terminal_contains(int x, int y) const {
    return x >= kTermX &&
           x < kTermX + terminal_width(sidebar_collapsed_) &&
           y >= kTermY && y < kTermY + kTermH;
}

TouchAction Tab5TerminalDisplay::hit_test(int x, int y) const {
    if (y >= 6 && y < 42) {
        if (session_connected_ && x >= 1016 && x < 1088)
            return TouchAction::toggle_sidebar;
        if (x >= 1100 && x < 1172) return TouchAction::zoom_out;
        if (x >= 1184 && x < 1256) return TouchAction::zoom_in;
    }
    if (sidebar_collapsed_ && x >= kCollapsedSideX &&
        x < kCollapsedSideX + kCollapsedSideW &&
        y >= kSideY && y < kSideY + kSideH)
        return TouchAction::toggle_sidebar;
    if (!sidebar_collapsed_ && x >= kSideX + 16 && x < kSideX + kSideW - 16) {
        if (y >= 514 && y < 566) return TouchAction::reconnect;
        if (y >= 584 && y < 636) return TouchAction::console;
    }
    return TouchAction::none;
}

}  // namespace tab5_terminal

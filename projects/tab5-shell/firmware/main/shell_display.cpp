#include "shell_display.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "pixel_fonts.h"
#include "tab5_hal.h"
#include "terminal_fonts.h"

namespace tab5_shell {
namespace {

constexpr int kHeaderHeight = 36;
constexpr int kFooterHeight = 36;
constexpr int kFooterY = TAB5_DISPLAY_HEIGHT - kFooterHeight;
constexpr int kCellWidth = 16;
constexpr int kCellHeight = 27;
constexpr int kConsoleX = 1104;
constexpr int kConsoleY = 688;
constexpr int kConsoleWidth = 160;
constexpr int kConsoleHeight = 28;

constexpr std::uint32_t kBackground = 0x080b10;
constexpr std::uint32_t kTerminalBackground = 0x020406;
constexpr std::uint32_t kPanel = 0x111823;
constexpr std::uint32_t kBorder = 0x334155;
constexpr std::uint32_t kText = 0xe8edf2;
constexpr std::uint32_t kMuted = 0x93a4b7;
constexpr std::uint32_t kBlue = 0x56a8ff;
constexpr std::uint32_t kGreen = 0x55d187;
constexpr std::uint32_t kAmber = 0xf5b84b;
constexpr std::uint32_t kRed = 0xff6b6b;

constexpr std::uint32_t kAnsiPalette[16] = {
    0x000000, 0xc00000, 0x00c000, 0xc0c000,
    0x0000c0, 0xc000c0, 0x00c0c0, 0xc0c0c0,
    0x404040, 0xff4040, 0x40ff40, 0xffff40,
    0x4040ff, 0xff40ff, 0x40ffff, 0xffffff,
};

bool rect(int x, int y, int width, int height, std::uint32_t colour) {
    if (width <= 0 || height <= 0) return true;
    return tab5_display_rect(x, y, width, height, colour) == ESP_OK;
}

std::uint32_t terminal_colour(term::Color colour, std::uint32_t fallback) {
    using Kind = term::Color::Kind;
    if (colour.kind == Kind::Default) return fallback;
    if (colour.kind == Kind::Rgb) return colour.value & 0xffffff;
    const std::uint8_t index = static_cast<std::uint8_t>(colour.value & 0xff);
    if (index < 16) return kAnsiPalette[index];
    if (index >= 232) {
        const std::uint8_t value = static_cast<std::uint8_t>((index - 232) * 10 + 8);
        return (std::uint32_t(value) << 16) | (std::uint32_t(value) << 8) | value;
    }
    const std::uint8_t cube = static_cast<std::uint8_t>(index - 16);
    const std::uint8_t red = static_cast<std::uint8_t>((cube / 36) * 51);
    const std::uint8_t green = static_cast<std::uint8_t>(((cube / 6) % 6) * 51);
    const std::uint8_t blue = static_cast<std::uint8_t>((cube % 6) * 51);
    return (std::uint32_t(red) << 16) | (std::uint32_t(green) << 8) | blue;
}

std::uint32_t dim(std::uint32_t colour) {
    const std::uint32_t red = ((colour >> 16) & 0xff) / 2;
    const std::uint32_t green = ((colour >> 8) & 0xff) / 2;
    const std::uint32_t blue = (colour & 0xff) / 2;
    return (red << 16) | (green << 8) | blue;
}

const tab5_terminal_fonts::Font& terminal_font(bool bold) {
    return bold ? tab5_terminal_fonts::tab5_terminal_mono_10pt_bold
                : tab5_terminal_fonts::tab5_terminal_mono_10pt_regular;
}

std::size_t glyph_index(char32_t character, const tab5_terminal_fonts::Font& font) {
    std::size_t low = 0;
    std::size_t high = font.glyph_count;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (font.codepoints[middle] < character)
            low = middle + 1;
        else
            high = middle;
    }
    if (low < font.glyph_count && font.codepoints[low] == character) return low;

    low = 0;
    high = font.glyph_count;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (font.codepoints[middle] < U'?')
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

bool terminal_glyph(char32_t character, int x, int y,
                    const tab5_terminal_fonts::Font& font,
                    std::uint32_t colour) {
    const std::uint8_t* bitmap = font.bitmap +
        glyph_index(character, font) * font.glyph_stride;
    const int row_bytes = (font.cell_width + 7) / 8;
    bool ok = true;
    for (int row = 0; row < font.cell_height; ++row) {
        int column = 0;
        while (column < font.cell_width) {
            const auto set = [&](int candidate) {
                const std::uint8_t byte = bitmap[row * row_bytes + candidate / 8];
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

int pixel_text_width(const char* value, const cyd_fonts::Font& font, int scale) {
    if (!value) return 0;
    int width = 0;
    for (const auto* cursor = reinterpret_cast<const unsigned char*>(value); *cursor; ++cursor) {
        unsigned character = *cursor;
        if (character < font.first || character > font.last) character = '?';
        width += (font.glyphs[character - font.first].advance + 1) * scale;
    }
    return width ? width - scale : 0;
}

bool pixel_glyph(unsigned character, int x, int y, const cyd_fonts::Font& font,
                 std::uint32_t colour, int scale) {
    if (character < font.first || character > font.last) character = '?';
    const auto& glyph = font.glyphs[character - font.first];
    const int baseline = y + font.ascent * scale;
    bool ok = true;
    for (unsigned gy = 0; gy < glyph.height; ++gy) {
        for (unsigned gx = 0; gx < glyph.width; ++gx) {
            const unsigned bit = gy * glyph.width + gx;
            if (!(font.bitmap[glyph.offset + bit / 8] & (0x80 >> (bit % 8)))) continue;
            ok = rect(x + (glyph.x + static_cast<int>(gx)) * scale,
                      baseline + (glyph.y + static_cast<int>(gy)) * scale,
                      scale, scale, colour) && ok;
        }
    }
    return ok;
}

bool pixel_text(const char* value, int x, int y, const cyd_fonts::Font& font,
                std::uint32_t colour, int scale) {
    if (!value) return true;
    bool ok = true;
    int cursor = x;
    for (const auto* item = reinterpret_cast<const unsigned char*>(value); *item; ++item) {
        unsigned character = *item;
        if (character < font.first || character > font.last) character = '?';
        const auto& glyph = font.glyphs[character - font.first];
        ok = pixel_glyph(character, cursor, y, font, colour, scale) && ok;
        cursor += (glyph.advance + 1) * scale;
    }
    return ok;
}

bool button(int x, int y, int width, int height, const char* label) {
    bool ok = rect(x, y, width, height, kPanel);
    ok = rect(x, y, width, 2, kAmber) && ok;
    ok = rect(x, y + height - 2, width, 2, kAmber) && ok;
    ok = rect(x, y, 2, height, kAmber) && ok;
    ok = rect(x + width - 2, y, 2, height, kAmber) && ok;
    const int label_width = pixel_text_width(label, cyd_fonts::pixelfont_11x5_thin, 2);
    ok = pixel_text(label, x + (width - label_width) / 2, y + 6,
                    cyd_fonts::pixelfont_11x5_thin, kAmber, 2) && ok;
    return ok;
}

}  // namespace

term::Result<void> ShellDisplay::init() {
    if (tab5_display_set_rotation(TAB5_ROTATION_0) != ESP_OK)
        return std::unexpected(term::Error::BackendError);
    return {};
}

term::Result<void> ShellDisplay::draw_cells(std::uint16_t row, std::uint16_t col,
                                             std::span<const term::Cell> cells) {
    if (row >= rows() || col >= cols() || cells.size() > cols() - col)
        return std::unexpected(term::Error::OutOfRange);
    if (keyboard_visible_ && row>=14) return {};

    bool ok = true;
    for (std::size_t index = 0; index < cells.size(); ++index) {
        const auto& cell = cells[index];
        if (cell.attrs.wide_cont) continue;
        term::Color foreground_source = cell.fg;
        if (cell.attrs.bold && foreground_source.kind == term::Color::Kind::Index &&
            foreground_source.value < 8) {
            foreground_source = term::Color::indexed(
                static_cast<std::uint8_t>(foreground_source.value + 8));
        }
        std::uint32_t foreground = terminal_colour(foreground_source, kText);
        std::uint32_t background = terminal_colour(cell.bg, kTerminalBackground);
        if (cell.attrs.faint) foreground = dim(foreground);
        if (cell.attrs.hidden) foreground = background;
        if (cell.attrs.reverse) std::swap(foreground, background);

        const int x = static_cast<int>(col + index) * kCellWidth;
        const int y = kHeaderHeight + static_cast<int>(row) * kCellHeight;
        const int width = cell.attrs.wide ? kCellWidth * 2 : kCellWidth;
        ok = rect(x, y, width, kCellHeight, background) && ok;
        if (cell.ch != U' ' && cell.ch != 0) {
            const auto& font = terminal_font(cell.attrs.bold);
            const int glyph_x = x + std::max(0, (width - font.cell_width) / 2);
            const int glyph_y = y + std::max(0, (kCellHeight - font.cell_height) / 2);
            ok = terminal_glyph(cell.ch, glyph_x, glyph_y, font, foreground) && ok;
        }
        if (cell.attrs.underline)
            ok = rect(x + 1, y + kCellHeight - 2, width - 2, 1, foreground) && ok;
        if (cell.attrs.strike)
            ok = rect(x + 1, y + kCellHeight / 2, width - 2, 1, foreground) && ok;
    }
    return ok ? term::Result<void>{}
              : term::Result<void>{std::unexpected(term::Error::BackendError)};
}

term::Result<void> ShellDisplay::flush(term::DamageRect damage) {
    if (damage.empty() || !present_enabled_) return {};
    if (keyboard_visible_) {
        if (damage.row0>=14) return {};
        damage.row1=std::min<std::uint16_t>(damage.row1,14);
    }
    const int x = damage.col0 * kCellWidth;
    const int y = kHeaderHeight + damage.row0 * kCellHeight;
    const int width = (damage.col1 - damage.col0) * kCellWidth;
    const int height = (damage.row1 - damage.row0) * kCellHeight;
    return tab5_display_present_region(x, y, width, height) == ESP_OK
               ? term::Result<void>{}
               : term::Result<void>{std::unexpected(term::Error::BackendError)};
}

bool ShellDisplay::draw_chrome(const ShellStatus& status, bool present) {
    bool ok = rect(0, 0, TAB5_DISPLAY_WIDTH, TAB5_DISPLAY_HEIGHT, kBackground);
    ok = rect(0, 0, TAB5_DISPLAY_WIDTH, kHeaderHeight, kPanel) && ok;
    ok = rect(0, kHeaderHeight - 1, TAB5_DISPLAY_WIDTH, 1, kBorder) && ok;
    ok = pixel_text("TAB5 SHELL", 14, 8, cyd_fonts::pixelfont_13x7_semibold,
                    kText, 2) && ok;
    ok = pixel_text("v0.1.3  LOCAL FIRST", 210, 12,
                    cyd_fonts::pixelfont_7x5_thin, kBlue, 2) && ok;
    const char* sd = !status.sd_ready ? "SD UNAVAILABLE"
                     : status.home_ready ? "SD READY / HOME WRITABLE"
                                         : "SD READ / HOME UNAVAILABLE";
    const int sd_x = TAB5_DISPLAY_WIDTH - 14 -
        pixel_text_width(sd, cyd_fonts::pixelfont_7x5_thin, 2);
    ok = pixel_text(sd, sd_x, 12, cyd_fonts::pixelfont_7x5_thin,
                    status.home_ready ? kGreen : kAmber, 2) && ok;
    ok = rect(0, kFooterY, TAB5_DISPLAY_WIDTH, kFooterHeight, kPanel) && ok;
    ok = rect(0, kFooterY, TAB5_DISPLAY_WIDTH, 1, kBorder) && ok;
    ok = draw_status(status) && ok;
    if (present && present_enabled_) {
        ok = tab5_display_present_region(0, 0, TAB5_DISPLAY_WIDTH,
                                         kHeaderHeight) == ESP_OK && ok;
        ok = tab5_display_present_region(0, kFooterY, TAB5_DISPLAY_WIDTH,
                                         kFooterHeight) == ESP_OK && ok;
    }
    return ok;
}

bool ShellDisplay::draw_status(const ShellStatus& status) {
    bool ok = rect(0, kFooterY, TAB5_DISPLAY_WIDTH, kFooterHeight, kPanel);
    ok = rect(0, kFooterY, TAB5_DISPLAY_WIDTH, 1, kBorder) && ok;
    char line[160];
    char history[64];
    std::snprintf(history, sizeof(history), "SCROLLBACK %u/%u  LIVE resumes",
                  static_cast<unsigned>(status.scroll_offset),
                  static_cast<unsigned>(status.scrollback_rows));
    std::snprintf(line, sizeof(line), "status %d  |  %s  |  TAB5 %s  USB %s",
                  status.last_status, status.message ? status.message : "READY",
                  status.tab5_keyboard_ready ? "READY" : "--",
                  status.usb_keyboard_ready ? "READY" : "--");
    if (status.scroll_offset) std::snprintf(line, sizeof(line), "%s", history);
    // Reserve the right side for navigation; long diagnostics must not paint
    // over controls or fail the strict HAL bounds check.
    for (std::size_t n = std::strlen(line);
         n && pixel_text_width(line, cyd_fonts::pixelfont_7x5_thin, 1) > 596; --n)
        line[n - 1] = '\0';
    ok = pixel_text(line, 14, kFooterY + 12,
                    cyd_fonts::pixelfont_7x5_thin,
                    status.scroll_offset ? kAmber : status.last_status == 0 ? kMuted : kRed, 1) && ok;
    ok = button(624, kConsoleY, 80, kConsoleHeight, "UP") && ok;
    ok = button(712, kConsoleY, 88, kConsoleHeight, "DOWN") && ok;
    ok = button(808, kConsoleY, 104, kConsoleHeight, "LIVE") && ok;
    ok = button(kConsoleX, kConsoleY, kConsoleWidth, kConsoleHeight, "CONSOLE") && ok;
    ok = button(928, kConsoleY, 160, kConsoleHeight, "KEYS") && ok;
    if (present_enabled_)
        ok = tab5_display_present_region(0, kFooterY, TAB5_DISPLAY_WIDTH,
                                         kFooterHeight) == ESP_OK && ok;
    return ok;
}

bool ShellDisplay::draw_touch_keyboard(const TouchKeyboard& keyboard) {
    bool ok=rect(0,TouchKeyboard::top,1280,270,kPanel);
    for (int row=0;row<5;++row) {
        int columns=10;
        for (int col=0;col<columns;++col) {
            int x=col*1280/columns;
            int width=(col+1)*1280/columns-x;
            int y=TouchKeyboard::top+row*TouchKeyboard::row_height;
            ok=rect(x+1,y+1,width-2,52,kBorder)&&ok;
            std::string label=row==4?TouchKeyboard::actions[col]:std::string(1,keyboard.label(row,col));
            if (row<4 && keyboard.shift && label[0]>='a'&&label[0]<='z') label[0]-=32;
            auto colour=(row==4 && ((col==0&&keyboard.shift)||(col==1&&keyboard.control)||(col==2&&keyboard.symbols)))?kAmber:kText;
            ok=pixel_text(label.c_str(),x+12,y+17,cyd_fonts::pixelfont_11x5_thin,colour,2)&&ok;
        }
    }
    if (present_enabled_) ok=tab5_display_present_region(0,TouchKeyboard::top,1280,270)==ESP_OK&&ok;
    return ok;
}

bool ShellDisplay::present_all() {
    present_enabled_ = true;
    return tab5_display_present() == ESP_OK;
}

bool ShellDisplay::console_hit_test(int x, int y) const {
    return x >= kConsoleX && x < kConsoleX + kConsoleWidth &&
           y >= kConsoleY && y < kConsoleY + kConsoleHeight;
}

}  // namespace tab5_shell

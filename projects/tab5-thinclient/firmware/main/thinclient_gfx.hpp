#pragma once

#include <cstdint>
#include <cstddef>
#include "app_config.hpp"
#include "arduino_compat.hpp"
#include "pixel_fonts.h"
#include "thinclient_fonts.h"

struct ConsolePalette {
    uint16_t bg;
    uint16_t surface;
    uint16_t surfacePressed;
    uint16_t border;
    uint16_t text;
    uint16_t muted;
    uint16_t disabled;
    uint16_t blue;
    uint16_t orange;
    uint16_t red;
    uint16_t green;
    uint16_t yellow;
    uint16_t purple;
    uint16_t selectBg;
    uint16_t selectBorder;
    uint16_t termBg;
    uint16_t termFg;
};

struct TerminalFontSpec {
    TerminalFontId id;
    const char* label;
    const uint8_t* japaneseFont;
    int cellW;
    int cellH;
    int defaultLineStep;
    int settingsLineHeight;
};

class ThinClientGfx {
public:
    bool init(uint8_t rotation);
    void deinit();

    bool setRotation(uint8_t rotation);
    uint8_t rotation() const { return rotation_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool isPortrait() const { return height_ > width_; }

    void setClipRect(int x, int y, int w, int h);
    void clearClipRect();

    void fillScreen(uint16_t color);
    void fillRect(int x, int y, int w, int h, uint16_t color);
    void drawRect(int x, int y, int w, int h, uint16_t color);
    void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color);
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color);
    void drawPixel(int x, int y, uint16_t color);
    void drawLine(int x0, int y0, int x1, int y1, uint16_t color);
    void drawCircle(int cx, int cy, int r, uint16_t color);
    void fillCircle(int cx, int cy, int r, uint16_t color);
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
    void blitRgb565(int dstX, int dstY, int w, int h, const uint16_t* pixels);

    void markDirty(int x, int y, int w, int h);
    bool hasDirty() const { return dirtyX1_ > dirtyX0_ && dirtyY1_ > dirtyY0_; }
    void flushDirty();

    // Console PixelFonts chrome helpers
    static int pixelTextWidth(const cyd_fonts::Font& font, const char* text, int scale = 1);
    void drawPixelText(const cyd_fonts::Font& font, const char* text, int x, int y,
                       uint16_t fg, int scale = 1, bool topAligned = true);

    // Main-area bitmap & Japanese gothic fonts
    void drawAscii8x16Char(uint8_t ch, int x, int y, uint16_t fg, int scaleX = 1, int scaleY = 1);
    int drawU8g2Text(const uint8_t* fontData, const char* utf8, int x, int topY,
                     uint16_t fg, bool measureOnly = false);
    int u8g2TextWidth(const uint8_t* fontData, const char* utf8);
    int u8g2FontHeight(const uint8_t* fontData) const;

    static const TerminalFontSpec& fontSpec(uint8_t fontId);
    static size_t fontCount();
    static const TerminalFontSpec& fontByIndex(size_t idx);

    bool drawTerminusGlyph(const TerminusBitmap::Font& font, uint32_t cp, int x, int lineTop,
                           int lineStep, uint16_t fg);
    int drawTerminalText(const TerminalFontSpec& spec, int lineStep, const String& text,
                         int x, int lineTop, uint16_t fg, uint16_t bg);
    void drawTightJapaneseRun(const TerminalFontSpec& spec, int lineStep, const String& text,
                              int x, int y, int w, uint16_t fg, uint16_t bg);
    int drawSettingsString(const uint8_t* jpFont, const String& text, int x, int y, uint16_t fg);
    int settingsTextWidth(const uint8_t* jpFont, const String& text);

    bool drawBlockGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg, uint16_t bg);
    bool drawBoxGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg);
    bool drawBrailleGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg);
    bool drawPowerlineGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg, uint16_t bg);
    bool drawFallbackUnicodeGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg, uint16_t bg);

    static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
        return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    static uint16_t rgb565From888(uint32_t rgb) {
        return rgb565(static_cast<uint8_t>((rgb >> 16) & 0xFF),
                      static_cast<uint8_t>((rgb >> 8) & 0xFF),
                      static_cast<uint8_t>(rgb & 0xFF));
    }
    static uint32_t rgb888From565(uint16_t c) {
        return ((static_cast<uint32_t>(c & 0xF800) << 8) |
                (static_cast<uint32_t>(c & 0x07E0) << 5) |
                (static_cast<uint32_t>(c & 0x001F) << 3));
    }
    static const ConsolePalette& palette(bool darkTheme = true);

    static uint8_t utf8CharLength(uint8_t c);
    static uint32_t utf8CodepointAt(const char* s, size_t len, size_t index);
    static bool isJapaneseCodepoint(uint32_t cp);
    static bool isUnicodeBlank(uint32_t cp);
    static int terminalCodepointWidth(const TerminalFontSpec& spec, uint32_t cp);
    static int terminalTextWidth(const TerminalFontSpec& spec, const String& text);

private:
    uint16_t* fb_{nullptr};
    size_t fbCapacityPixels_{0};
    uint8_t rotation_{0};
    int width_{1280};
    int height_{720};

    bool hasClip_{false};
    int clipX0_{0};
    int clipY0_{0};
    int clipX1_{0};
    int clipY1_{0};

    int dirtyX0_{0};
    int dirtyY0_{0};
    int dirtyX1_{0};
    int dirtyY1_{0};

    void plotRaw(int x, int y, uint16_t color);
    int drawU8g2Glyph(const uint8_t* fontData, uint32_t cp, int x, int baseY, uint16_t fg, bool measureOnly);
    const uint8_t* findU8g2Glyph(const uint8_t* fontData, uint32_t cp);
};

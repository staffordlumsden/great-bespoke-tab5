#include "thinclient_gfx.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"
#include "tab5_hal.h"

namespace {

constexpr TerminalFontSpec kTerminalFonts[] = {
    {TerminalFontId::Small, "Terminus 8x16", lgfx_font_japan_gothic_16, 8, 16, 18, 16},
    {TerminalFontId::Medium, "Terminus 10x20", lgfx_font_japan_gothic_20, 10, 20, 22, 20},
    {TerminalFontId::Large, "Terminus 14x28", lgfx_font_japan_gothic_28, 14, 28, 30, 28},
    {TerminalFontId::ExtraLarge, "Terminus 18x36", lgfx_font_japan_gothic_36, 18, 36, 38, 36},
};

const ConsolePalette kDarkPalette = {
    ThinClientGfx::rgb565From888(0x101215),  // bg
    ThinClientGfx::rgb565From888(0x1d2228),  // surface
    ThinClientGfx::rgb565From888(0x2a3139),  // surfacePressed
    ThinClientGfx::rgb565From888(0x353d46),  // border
    ThinClientGfx::rgb565From888(0xf7f8fa),  // text
    ThinClientGfx::rgb565From888(0x9ba6b2),  // muted
    ThinClientGfx::rgb565From888(0x59636f),  // disabled
    ThinClientGfx::rgb565From888(0x3b82f6),  // blue
    ThinClientGfx::rgb565From888(0xf59e0b),  // orange
    ThinClientGfx::rgb565From888(0xef4444),  // red
    ThinClientGfx::rgb565From888(0x22c55e),  // green
    ThinClientGfx::rgb565From888(0xeab308),  // yellow
    ThinClientGfx::rgb565From888(0xa855f7),  // purple
    ThinClientGfx::rgb565From888(0x1b2b44),  // selectBg
    ThinClientGfx::rgb565From888(0x3b82f6),  // selectBorder
    ThinClientGfx::rgb565From888(0x0b0e11),  // termBg
    ThinClientGfx::rgb565From888(0xe6edf3),  // termFg
};

const ConsolePalette kClassicPalette = {
    ThinClientGfx::rgb565From888(0x000000),  // bg
    ThinClientGfx::rgb565From888(0x14181d),  // surface
    ThinClientGfx::rgb565From888(0x222933),  // surfacePressed
    ThinClientGfx::rgb565From888(0x3a434e),  // border
    ThinClientGfx::rgb565From888(0xf7f8fa),  // text
    ThinClientGfx::rgb565From888(0x9ba6b2),  // muted
    ThinClientGfx::rgb565From888(0x59636f),  // disabled
    ThinClientGfx::rgb565From888(0x3b82f6),  // blue
    ThinClientGfx::rgb565From888(0xf59e0b),  // orange
    ThinClientGfx::rgb565From888(0xef4444),  // red
    ThinClientGfx::rgb565From888(0x22c55e),  // green
    ThinClientGfx::rgb565From888(0xeab308),  // yellow
    ThinClientGfx::rgb565From888(0xa855f7),  // purple
    ThinClientGfx::rgb565From888(0x001f4d),  // selectBg
    ThinClientGfx::rgb565From888(0x22d3ee),  // selectBorder
    ThinClientGfx::rgb565From888(0x000000),  // termBg
    ThinClientGfx::rgb565From888(0x22c55e),  // termFg
};

struct U8g2BitReader {
    const uint8_t* ptr{nullptr};
    uint32_t bitPos{0};

    uint32_t getUnsigned(uint8_t bitCnt) {
        if (bitCnt == 0) return 0;
        uint32_t val = 0;
        for (uint8_t i = 0; i < bitCnt; ++i) {
            uint8_t byteVal = ptr[bitPos >> 3];
            uint32_t bit = (byteVal >> (bitPos & 7)) & 1u;
            val |= (bit << i);
            ++bitPos;
        }
        return val;
    }

    int32_t getSigned(uint8_t bitCnt) {
        if (bitCnt == 0) return 0;
        uint32_t u = getUnsigned(bitCnt);
        uint32_t bias = 1u << (bitCnt - 1);
        return static_cast<int32_t>(u) - static_cast<int32_t>(bias);
    }
};

void terminusFillRectCallback(void* ctx, int x, int y, int w, int h, uint16_t fg) {
    auto* gfx = static_cast<ThinClientGfx*>(ctx);
    gfx->fillRect(x, y, w, h, fg);
}

bool isFallbackSymbolRange(uint32_t cp) {
    return (cp >= 0x2190 && cp <= 0x21FF) ||
           (cp >= 0x2300 && cp <= 0x23FF) ||
           (cp >= 0x2460 && cp <= 0x24FF) ||
           (cp >= 0x25A0 && cp <= 0x25FF) ||
           (cp >= 0x2600 && cp <= 0x27BF) ||
           (cp >= 0x27C0 && cp <= 0x27FF) ||
           (cp >= 0x2800 && cp <= 0x28FF) ||
           (cp >= 0xE000 && cp <= 0xF8FF) ||
           (cp >= 0x1F000 && cp <= 0x1FAFF);
}

}  // namespace

const ConsolePalette& ThinClientGfx::palette(bool darkTheme) {
    return darkTheme ? kDarkPalette : kClassicPalette;
}

const TerminalFontSpec& ThinClientGfx::fontSpec(uint8_t fontId) {
    for (const auto& f : kTerminalFonts) {
        if (static_cast<uint8_t>(f.id) == fontId) {
            return f;
        }
    }
    return kTerminalFonts[1];  // Terminus 10x20 default
}

size_t ThinClientGfx::fontCount() {
    return sizeof(kTerminalFonts) / sizeof(kTerminalFonts[0]);
}

const TerminalFontSpec& ThinClientGfx::fontByIndex(size_t idx) {
    if (idx >= fontCount()) {
        return kTerminalFonts[1];
    }
    return kTerminalFonts[idx];
}

bool ThinClientGfx::init(uint8_t rotation) {
    constexpr size_t maxPixels = 1280u * 720u;
    if (!fb_) {
        fb_ = static_cast<uint16_t*>(heap_caps_malloc(maxPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!fb_) {
            fb_ = static_cast<uint16_t*>(malloc(maxPixels * sizeof(uint16_t)));
        }
        if (!fb_) {
            return false;
        }
        fbCapacityPixels_ = maxPixels;
    }
    return setRotation(rotation);
}

void ThinClientGfx::deinit() {
    if (fb_) {
        free(fb_);
        fb_ = nullptr;
        fbCapacityPixels_ = 0;
    }
}

bool ThinClientGfx::setRotation(uint8_t rotation) {
    rotation_ = rotation & 3u;
    esp_err_t err = tab5_display_set_rotation(static_cast<tab5_display_rotation_t>(rotation_));
    width_ = tab5_display_width();
    height_ = tab5_display_height();
    clearClipRect();
    if (fb_ && static_cast<size_t>(width_) * static_cast<size_t>(height_) <= fbCapacityPixels_) {
        memset(fb_, 0, static_cast<size_t>(width_) * static_cast<size_t>(height_) * sizeof(uint16_t));
    }
    dirtyX0_ = 0;
    dirtyY0_ = 0;
    dirtyX1_ = width_;
    dirtyY1_ = height_;
    return err == ESP_OK;
}

void ThinClientGfx::setClipRect(int x, int y, int w, int h) {
    clipX0_ = std::max(0, x);
    clipY0_ = std::max(0, y);
    clipX1_ = std::min(width_, x + w);
    clipY1_ = std::min(height_, y + h);
    hasClip_ = true;
}

void ThinClientGfx::clearClipRect() {
    hasClip_ = false;
    clipX0_ = 0;
    clipY0_ = 0;
    clipX1_ = width_;
    clipY1_ = height_;
}

void ThinClientGfx::markDirty(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(width_, x + w);
    int y1 = std::min(height_, y + h);
    if (x0 >= x1 || y0 >= y1) return;
    if (dirtyX1_ <= dirtyX0_ || dirtyY1_ <= dirtyY0_) {
        dirtyX0_ = x0;
        dirtyY0_ = y0;
        dirtyX1_ = x1;
        dirtyY1_ = y1;
    } else {
        dirtyX0_ = std::min(dirtyX0_, x0);
        dirtyY0_ = std::min(dirtyY0_, y0);
        dirtyX1_ = std::max(dirtyX1_, x1);
        dirtyY1_ = std::max(dirtyY1_, y1);
    }
}

void ThinClientGfx::plotRaw(int x, int y, uint16_t color) {
    int cx0 = hasClip_ ? clipX0_ : 0;
    int cy0 = hasClip_ ? clipY0_ : 0;
    int cx1 = hasClip_ ? clipX1_ : width_;
    int cy1 = hasClip_ ? clipY1_ : height_;
    if (!fb_ || x < cx0 || x >= cx1 || y < cy0 || y >= cy1) {
        return;
    }
    fb_[static_cast<size_t>(y) * static_cast<size_t>(width_) + static_cast<size_t>(x)] = color;
}

void ThinClientGfx::drawPixel(int x, int y, uint16_t color) {
    plotRaw(x, y, color);
    markDirty(x, y, 1, 1);
}

void ThinClientGfx::fillScreen(uint16_t color) {
    fillRect(0, 0, width_, height_, color);
}

void ThinClientGfx::fillRect(int x, int y, int w, int h, uint16_t color) {
    if (!fb_ || w <= 0 || h <= 0) return;
    int cx0 = hasClip_ ? clipX0_ : 0;
    int cy0 = hasClip_ ? clipY0_ : 0;
    int cx1 = hasClip_ ? clipX1_ : width_;
    int cy1 = hasClip_ ? clipY1_ : height_;
    int x0 = std::max(cx0, x);
    int y0 = std::max(cy0, y);
    int x1 = std::min(cx1, x + w);
    int y1 = std::min(cy1, y + h);
    if (x0 >= x1 || y0 >= y1) return;
    for (int yy = y0; yy < y1; ++yy) {
        uint16_t* row = fb_ + static_cast<size_t>(yy) * static_cast<size_t>(width_);
        std::fill(row + x0, row + x1, color);
    }
    markDirty(x0, y0, x1 - x0, y1 - y0);
}

void ThinClientGfx::drawRect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    fillRect(x, y, w, 1, color);
    if (h > 1) fillRect(x, y + h - 1, w, 1, color);
    if (h > 2) {
        fillRect(x, y + 1, 1, h - 2, color);
        if (w > 1) fillRect(x + w - 1, y + 1, 1, h - 2, color);
    }
}

void ThinClientGfx::drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    if (x0 == x1) {
        int top = std::min(y0, y1);
        fillRect(x0, top, 1, std::abs(y1 - y0) + 1, color);
        return;
    }
    if (y0 == y1) {
        int left = std::min(x0, x1);
        fillRect(left, y0, std::abs(x1 - x0) + 1, 1, color);
        return;
    }
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int minX = std::min(x0, x1);
    int minY = std::min(y0, y1);
    int maxX = std::max(x0, x1);
    int maxY = std::max(y0, y1);
    for (;;) {
        plotRaw(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    markDirty(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

void ThinClientGfx::drawCircle(int cx, int cy, int r, uint16_t color) {
    if (r <= 0) {
        drawPixel(cx, cy, color);
        return;
    }
    int x = r;
    int y = 0;
    int err = 1 - x;
    while (x >= y) {
        plotRaw(cx + x, cy + y, color);
        plotRaw(cx + y, cy + x, color);
        plotRaw(cx - y, cy + x, color);
        plotRaw(cx - x, cy + y, color);
        plotRaw(cx - x, cy - y, color);
        plotRaw(cx - y, cy - x, color);
        plotRaw(cx + y, cy - x, color);
        plotRaw(cx + x, cy - y, color);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x + 1);
        }
    }
    markDirty(cx - r, cy - r, r * 2 + 1, r * 2 + 1);
}

void ThinClientGfx::fillCircle(int cx, int cy, int r, uint16_t color) {
    if (r <= 0) {
        drawPixel(cx, cy, color);
        return;
    }
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r) {
                plotRaw(cx + dx, cy + dy, color);
            }
        }
    }
    markDirty(cx - r, cy - r, r * 2 + 1, r * 2 + 1);
}

void ThinClientGfx::drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    r = std::max(0, std::min(r, std::min(w, h) / 2));
    if (r <= 1) {
        drawRect(x, y, w, h, color);
        return;
    }
    fillRect(x + r, y, w - 2 * r, 1, color);
    fillRect(x + r, y + h - 1, w - 2 * r, 1, color);
    fillRect(x, y + r, 1, h - 2 * r, color);
    fillRect(x + w - 1, y + r, 1, h - 2 * r, color);
    int xx = r;
    int yy = 0;
    int err = 1 - xx;
    while (xx >= yy) {
        plotRaw(x + w - r - 1 + xx, y + r - yy, color);
        plotRaw(x + w - r - 1 + yy, y + r - xx, color);
        plotRaw(x + r - xx, y + r - yy, color);
        plotRaw(x + r - yy, y + r - xx, color);
        plotRaw(x + r - xx, y + h - r - 1 + yy, color);
        plotRaw(x + r - yy, y + h - r - 1 + xx, color);
        plotRaw(x + w - r - 1 + xx, y + h - r - 1 + yy, color);
        plotRaw(x + w - r - 1 + yy, y + h - r - 1 + xx, color);
        ++yy;
        if (err < 0) {
            err += 2 * yy + 1;
        } else {
            --xx;
            err += 2 * (yy - xx + 1);
        }
    }
    markDirty(x, y, w, h);
}

void ThinClientGfx::fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    r = std::max(0, std::min(r, std::min(w, h) / 2));
    if (r <= 1) {
        fillRect(x, y, w, h, color);
        return;
    }
    fillRect(x, y + r, w, h - 2 * r, color);
    for (int dy = 0; dy < r; ++dy) {
        int inset = r;
        while (inset > 0 && (inset * inset + (r - dy) * (r - dy) > r * r)) {
            --inset;
        }
        int offset = r - inset;
        int span = w - 2 * offset;
        if (span > 0) {
            fillRect(x + offset, y + dy, span, 1, color);
            fillRect(x + offset, y + h - 1 - dy, span, 1, color);
        }
    }
}

void ThinClientGfx::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); }
    if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }
    if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); }
    if (y0 == y2) {
        int a = std::min({x0, x1, x2});
        int b = std::max({x0, x1, x2});
        fillRect(a, y0, b - a + 1, 1, color);
        return;
    }
    int totalHeight = y2 - y0;
    for (int y = y0; y <= y2; ++y) {
        bool secondHalf = y > y1 || y1 == y0;
        int segHeight = secondHalf ? (y2 - y1) : (y1 - y0);
        if (segHeight == 0) segHeight = 1;
        int alpha = ((y - y0) * 1024) / totalHeight;
        int beta = secondHalf ? (((y - y1) * 1024) / segHeight) : (((y - y0) * 1024) / segHeight);
        int ax = x0 + ((x2 - x0) * alpha) / 1024;
        int bx = secondHalf ? (x1 + ((x2 - x1) * beta) / 1024) : (x0 + ((x1 - x0) * beta) / 1024);
        if (ax > bx) std::swap(ax, bx);
        fillRect(ax, y, bx - ax + 1, 1, color);
    }
}

void ThinClientGfx::blitRgb565(int dstX, int dstY, int w, int h, const uint16_t* pixels) {
    if (!fb_ || !pixels || w <= 0 || h <= 0) return;
    int cx0 = hasClip_ ? clipX0_ : 0;
    int cy0 = hasClip_ ? clipY0_ : 0;
    int cx1 = hasClip_ ? clipX1_ : width_;
    int cy1 = hasClip_ ? clipY1_ : height_;
    int x0 = std::max(cx0, dstX);
    int y0 = std::max(cy0, dstY);
    int x1 = std::min(cx1, dstX + w);
    int y1 = std::min(cy1, dstY + h);
    if (x0 >= x1 || y0 >= y1) return;
    for (int yy = y0; yy < y1; ++yy) {
        const uint16_t* srcRow = pixels + static_cast<size_t>(yy - dstY) * static_cast<size_t>(w) + (x0 - dstX);
        uint16_t* dstRow = fb_ + static_cast<size_t>(yy) * static_cast<size_t>(width_) + x0;
        memcpy(dstRow, srcRow, static_cast<size_t>(x1 - x0) * sizeof(uint16_t));
    }
    markDirty(x0, y0, x1 - x0, y1 - y0);
}

void ThinClientGfx::flushDirty() {
    if (!fb_ || !hasDirty()) return;
    const int x0 = std::max(0, dirtyX0_);
    const int y0 = std::max(0, dirtyY0_);
    const int x1 = std::min(width_, dirtyX1_);
    const int y1 = std::min(height_, dirtyY1_);
    if (x0 >= x1 || y0 >= y1) {
        dirtyX0_ = dirtyY0_ = dirtyX1_ = dirtyY1_ = 0;
        return;
    }

    int y = y0;
    while (y < y1) {
        const uint16_t* row = fb_ + static_cast<size_t>(y) * static_cast<size_t>(width_);
        uint16_t firstColor = row[x0];
        bool solidRow = true;
        for (int x = x0 + 1; x < x1; ++x) {
            if (row[x] != firstColor) {
                solidRow = false;
                break;
            }
        }
        if (solidRow) {
            int runRows = 1;
            while (y + runRows < y1) {
                const uint16_t* nextRow = fb_ + static_cast<size_t>(y + runRows) * static_cast<size_t>(width_);
                bool same = true;
                for (int x = x0; x < x1; ++x) {
                    if (nextRow[x] != firstColor) {
                        same = false;
                        break;
                    }
                }
                if (!same) break;
                ++runRows;
            }
            tab5_display_rect(x0, y, x1 - x0, runRows, rgb888From565(firstColor));
            y += runRows;
            continue;
        }

        int x = x0;
        while (x < x1) {
            uint16_t color = row[x];
            int runEnd = x + 1;
            while (runEnd < x1 && row[runEnd] == color) {
                ++runEnd;
            }
            tab5_display_rect(x, y, runEnd - x, 1, rgb888From565(color));
            x = runEnd;
        }
        ++y;
    }

    tab5_display_present_region(x0, y0, x1 - x0, y1 - y0);
    dirtyX0_ = dirtyY0_ = dirtyX1_ = dirtyY1_ = 0;
}

int ThinClientGfx::pixelTextWidth(const cyd_fonts::Font& font, const char* text, int scale) {
    if (!text) return 0;
    int width = 0;
    for (const char* p = text; *p; ++p) {
        uint8_t ch = static_cast<uint8_t>(*p);
        if (ch < font.first || ch > font.last) ch = ' ';
        const auto& g = font.glyphs[ch - font.first];
        width += g.advance * scale;
    }
    return width;
}

void ThinClientGfx::drawPixelText(const cyd_fonts::Font& font, const char* text, int x, int y,
                                  uint16_t fg, int scale, bool topAligned) {
    if (!text || scale <= 0) return;
    int baseY = topAligned ? (y + font.ascent * scale) : y;
    int cursorX = x;
    int minX = x;
    int minY = baseY - font.ascent * scale;
    int maxX = x;
    int maxY = baseY + 2 * scale;
    for (const char* p = text; *p; ++p) {
        uint8_t ch = static_cast<uint8_t>(*p);
        if (ch < font.first || ch > font.last) {
            cursorX += font.glyphs[0].advance * scale;
            continue;
        }
        const auto& g = font.glyphs[ch - font.first];
        const uint8_t* bits = font.bitmap + g.offset;
        int gx = cursorX + g.x * scale;
        int gy = baseY + g.y * scale;
        for (uint8_t row = 0; row < g.height; ++row) {
            for (uint8_t col = 0; col < g.width; ++col) {
                size_t bitIdx = static_cast<size_t>(row) * g.width + col;
                if (bits[bitIdx >> 3] & (0x80u >> (bitIdx & 7u))) {
                    if (scale == 1) {
                        plotRaw(gx + col, gy + row, fg);
                    } else {
                        for (int sy = 0; sy < scale; ++sy) {
                            for (int sx = 0; sx < scale; ++sx) {
                                plotRaw(gx + col * scale + sx, gy + row * scale + sy, fg);
                            }
                        }
                    }
                }
            }
        }
        maxX = std::max(maxX, gx + g.width * scale);
        maxY = std::max(maxY, gy + g.height * scale);
        cursorX += g.advance * scale;
    }
    maxX = std::max(maxX, cursorX);
    markDirty(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

void ThinClientGfx::drawAscii8x16Char(uint8_t ch, int x, int y, uint16_t fg, int scaleX, int scaleY) {
    const uint8_t* glyph = FontLib8x16 + static_cast<size_t>(ch) * 16u;
    for (int row = 0; row < 16; ++row) {
        uint8_t bits = glyph[row];
        if (!bits) continue;
        for (int col = 0; col < 8; ++col) {
            if (bits & (0x80u >> col)) {
                if (scaleX == 1 && scaleY == 1) {
                    plotRaw(x + col, y + row, fg);
                } else {
                    fillRect(x + col * scaleX, y + row * scaleY, scaleX, scaleY, fg);
                }
            }
        }
    }
    markDirty(x, y, 8 * scaleX, 16 * scaleY);
}

int ThinClientGfx::u8g2FontHeight(const uint8_t* fontData) const {
    if (!fontData) return 16;
    return fontData[10];
}

const uint8_t* ThinClientGfx::findU8g2Glyph(const uint8_t* fontData, uint32_t cp) {
    if (!fontData) return nullptr;
    if (cp <= 0xFFu) {
        uint16_t startOff = (static_cast<uint16_t>(fontData[17]) << 8) | fontData[18];
        if (cp >= 'a') {
            uint16_t lowOff = (static_cast<uint16_t>(fontData[19]) << 8) | fontData[20];
            if (lowOff) startOff = lowOff;
        }
        const uint8_t* p = fontData + 23 + (cp >= 'A' ? startOff : 0);
        for (;;) {
            if (p[0] == 0) return nullptr;
            if (p[0] == cp) return p + 2;
            if (p[1] == 0) return nullptr;
            p += p[1];
        }
    }

    uint16_t utf8Start = (static_cast<uint16_t>(fontData[21]) << 8) | fontData[22];
    const uint8_t* fontBase = fontData + 23 + utf8Start;
    const uint8_t* lut = fontBase;
    const uint8_t* p = fontBase;
    uint16_t e = 0;
    for (;;) {
        uint16_t jump = (static_cast<uint16_t>(lut[0]) << 8) | lut[1];
        e = (static_cast<uint16_t>(lut[2]) << 8) | lut[3];
        if (cp <= e) break;
        p += jump;
        lut += 4;
        if (e == 0xFFFFu || jump == 0) return nullptr;
    }
    if (p == fontBase) {
        // First block begins immediately after the lookup table.
        const uint8_t* scan = lut;
        for (;;) {
            uint16_t j = (static_cast<uint16_t>(scan[0]) << 8) | scan[1];
            uint16_t endCode = (static_cast<uint16_t>(scan[2]) << 8) | scan[3];
            scan += 4;
            if (endCode == 0xFFFFu || j == 0) break;
        }
        p = scan;
    }
    for (;;) {
        uint16_t code = (static_cast<uint16_t>(p[0]) << 8) | p[1];
        if (code == 0) return nullptr;
        if (code == cp) return p + 3;
        if (p[2] == 0) return nullptr;
        p += p[2];
    }
}

int ThinClientGfx::drawU8g2Glyph(const uint8_t* fontData, uint32_t cp, int x, int baseY, uint16_t fg, bool measureOnly) {
    if (!fontData) return 0;
    const uint8_t* glyph = findU8g2Glyph(fontData, cp);
    if (!glyph) {
        return cp < 0x80 ? fontData[9] / 2 : fontData[9];
    }

    U8g2BitReader br{glyph, 0};
    const uint8_t bw = fontData[4];
    const uint8_t bh = fontData[5];
    const uint8_t bx = fontData[6];
    const uint8_t by = fontData[7];
    const uint8_t bd = fontData[8];

    int32_t w = static_cast<int32_t>(br.getUnsigned(bw));
    int32_t h = static_cast<int32_t>(br.getUnsigned(bh));
    int32_t xOff = br.getSigned(bx);
    int32_t yOff = br.getSigned(by);
    int32_t dX = br.getSigned(bd);
    if (measureOnly || w <= 0 || h <= 0) {
        return static_cast<int>(dX);
    }

    const uint8_t bitsPer0 = fontData[2];
    const uint8_t bitsPer1 = fontData[3];
    int px = 0;
    int py = 0;
    int startX = x + xOff;
    int startY = baseY - yOff - h;
    auto advance = [&](uint32_t count, bool setPixel) {
        while (count--) {
            if (setPixel) {
                plotRaw(startX + px, startY + py, fg);
            }
            ++px;
            if (px >= w) {
                px = 0;
                ++py;
                if (py >= h) break;
            }
        }
    };

    while (py < h) {
        uint32_t a = br.getUnsigned(bitsPer0);
        uint32_t b = br.getUnsigned(bitsPer1);
        for (;;) {
            advance(a, false);
            if (py >= h) break;
            advance(b, true);
            if (py >= h) break;
            if (!br.getUnsigned(1)) break;
        }
    }
    markDirty(startX, startY, w, h);
    return static_cast<int>(dX);
}

int ThinClientGfx::drawU8g2Text(const uint8_t* fontData, const char* utf8, int x, int topY,
                                uint16_t fg, bool measureOnly) {
    if (!fontData || !utf8) return 0;
    const int ascent = static_cast<int8_t>(fontData[13]);
    const int baseY = topY + ascent;
    const size_t len = strlen(utf8);
    int cursorX = x;
    for (size_t i = 0; i < len;) {
        uint8_t c = static_cast<uint8_t>(utf8[i]);
        uint32_t cp = utf8CodepointAt(utf8, len, i);
        cursorX += drawU8g2Glyph(fontData, cp, cursorX, baseY, fg, measureOnly);
        i += utf8CharLength(c);
    }
    return cursorX - x;
}

int ThinClientGfx::u8g2TextWidth(const uint8_t* fontData, const char* utf8) {
    return drawU8g2Text(fontData, utf8, 0, 0, 0, true);
}

uint8_t ThinClientGfx::utf8CharLength(uint8_t c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

uint32_t ThinClientGfx::utf8CodepointAt(const char* s, size_t len, size_t index) {
    if (!s || index >= len) return 0;
    const uint8_t c0 = static_cast<uint8_t>(s[index]);
    if (c0 < 0x80) return c0;
    if ((c0 & 0xE0) == 0xC0 && index + 1 < len) {
        return ((c0 & 0x1F) << 6) | (static_cast<uint8_t>(s[index + 1]) & 0x3F);
    }
    if ((c0 & 0xF0) == 0xE0 && index + 2 < len) {
        return ((c0 & 0x0F) << 12) |
               ((static_cast<uint8_t>(s[index + 1]) & 0x3F) << 6) |
               (static_cast<uint8_t>(s[index + 2]) & 0x3F);
    }
    if ((c0 & 0xF8) == 0xF0 && index + 3 < len) {
        return ((c0 & 0x07) << 18) |
               ((static_cast<uint8_t>(s[index + 1]) & 0x3F) << 12) |
               ((static_cast<uint8_t>(s[index + 2]) & 0x3F) << 6) |
               (static_cast<uint8_t>(s[index + 3]) & 0x3F);
    }
    return c0;
}

bool ThinClientGfx::isJapaneseCodepoint(uint32_t cp) {
    return (cp >= 0x3000 && cp <= 0x30FF) ||
           (cp >= 0x31F0 && cp <= 0x31FF) ||
           (cp >= 0x3400 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF);
}

bool ThinClientGfx::isUnicodeBlank(uint32_t cp) {
    return cp == 0x00A0 || cp == 0x1680 || cp == 0x180E ||
           (cp >= 0x2000 && cp <= 0x200F) ||
           cp == 0x202F || cp == 0x205F || cp == 0x3000 ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF;
}

int ThinClientGfx::terminalCodepointWidth(const TerminalFontSpec& spec, uint32_t cp) {
    if (cp == 0) return 0;
    const auto& bmFont = TerminusBitmap::fontForHeight(static_cast<uint8_t>(spec.cellH));
    if (cp < 0x80 || TerminusBitmap::hasGlyph(bmFont, cp)) {
        return spec.cellW;
    }
    if (isJapaneseCodepoint(cp)) {
        return spec.cellW * 2;
    }
    return spec.cellW;
}

int ThinClientGfx::terminalTextWidth(const TerminalFontSpec& spec, const String& text) {
    int width = 0;
    const char* s = text.c_str();
    const size_t len = text.length();
    for (size_t i = 0; i < len;) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        uint32_t cp = utf8CodepointAt(s, len, i);
        width += terminalCodepointWidth(spec, cp);
        i += utf8CharLength(c);
    }
    return width;
}

bool ThinClientGfx::drawTerminusGlyph(const TerminusBitmap::Font& font, uint32_t cp, int x, int lineTop,
                                      int lineStep, uint16_t fg) {
    if (!TerminusBitmap::hasGlyph(font, cp)) {
        return false;
    }
    int y = lineTop + std::max(0, (lineStep - static_cast<int>(font.targetH)) / 2);
    return TerminusBitmap::drawGlyph(terminusFillRectCallback, this, font, cp, x, y, fg);
}

bool ThinClientGfx::drawBlockGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg, uint16_t bg) {
    if (cp >= 0x2588 && cp <= 0x258F) {
        int fillW = w;
        if (cp >= 0x2589) {
            fillW = std::max(1, (w * static_cast<int>(0x2590 - cp)) / 8);
        }
        fillRect(x, y, fillW, h, fg);
        return true;
    }
    if (cp == 0x2580) {
        fillRect(x, y, w, std::max(1, h / 2), fg);
        return true;
    }
    if (cp == 0x2584) {
        int half = std::max(1, h / 2);
        fillRect(x, y + h - half, w, half, fg);
        return true;
    }
    if (cp == 0x2590) {
        fillRect(x + w / 2, y, std::max(1, w - w / 2), h, fg);
        return true;
    }
    if (cp >= 0x2581 && cp <= 0x2587) {
        int fillH = std::max(1, (h * static_cast<int>(cp - 0x2580)) / 8);
        fillRect(x, y + h - fillH, w, fillH, fg);
        return true;
    }
    if (cp >= 0x2596 && cp <= 0x259F) {
        const int leftW = std::max(1, w / 2);
        const int rightX = x + leftW;
        const int rightW = std::max(1, w - leftW);
        const int topH = std::max(1, h / 2);
        const int bottomY = y + topH;
        const int bottomH = std::max(1, h - topH);
        auto quad = [&](bool tl, bool tr, bool bl, bool br) {
            if (tl) fillRect(x, y, leftW, topH, fg);
            if (tr) fillRect(rightX, y, rightW, topH, fg);
            if (bl) fillRect(x, bottomY, leftW, bottomH, fg);
            if (br) fillRect(rightX, bottomY, rightW, bottomH, fg);
        };
        switch (cp) {
            case 0x2596: quad(false, false, true, false); break;
            case 0x2597: quad(false, false, false, true); break;
            case 0x2598: quad(true, false, false, false); break;
            case 0x2599: quad(true, false, true, true); break;
            case 0x259A: quad(true, false, false, true); break;
            case 0x259B: quad(true, true, true, false); break;
            case 0x259C: quad(true, true, false, true); break;
            case 0x259D: quad(false, true, false, false); break;
            case 0x259E: quad(false, true, true, false); break;
            case 0x259F: quad(false, true, true, true); break;
        }
        return true;
    }
    if (cp == 0x2591 || cp == 0x2592 || cp == 0x2593) {
        int step = cp == 0x2591 ? 4 : (cp == 0x2592 ? 3 : 2);
        for (int yy = 0; yy < h; ++yy) {
            for (int xx = (yy % step); xx < w; xx += step) {
                plotRaw(x + xx, y + yy, fg);
            }
        }
        markDirty(x, y, w, h);
        return true;
    }
    (void)bg;
    return false;
}

bool ThinClientGfx::drawBoxGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg) {
    const int cx = x + w / 2;
    const int cy = y + h / 2;
    const int thick = std::max(1, std::min(w, h) / 8);
    auto hline = [&](int x1, int x2) {
        fillRect(std::min(x1, x2), cy - thick / 2, std::abs(x2 - x1) + 1, thick, fg);
    };
    auto vline = [&](int y1, int y2) {
        fillRect(cx - thick / 2, std::min(y1, y2), thick, std::abs(y2 - y1) + 1, fg);
    };
    auto junction = [&](bool left, bool right, bool up, bool down) {
        if (left) hline(x, cx);
        if (right) hline(cx, x + w - 1);
        if (up) vline(y, cy);
        if (down) vline(cy, y + h - 1);
    };

    switch (cp) {
        case 0x2500: hline(x, x + w - 1); return true;
        case 0x2502: vline(y, y + h - 1); return true;
        case 0x250C:
        case 0x256D: junction(false, true, false, true); return true;
        case 0x2510:
        case 0x256E: junction(true, false, false, true); return true;
        case 0x2514:
        case 0x2570: junction(false, true, true, false); return true;
        case 0x2518:
        case 0x256F: junction(true, false, true, false); return true;
        case 0x251C: case 0x251D: case 0x251E: case 0x251F:
        case 0x2520: case 0x2521: case 0x2522: case 0x2523:
            junction(false, true, true, true); return true;
        case 0x2524: case 0x2525: case 0x2526: case 0x2527:
        case 0x2528: case 0x2529: case 0x252A: case 0x252B:
            junction(true, false, true, true); return true;
        case 0x252C: case 0x252D: case 0x252E: case 0x252F:
        case 0x2530: case 0x2531: case 0x2532: case 0x2533:
            junction(true, true, false, true); return true;
        case 0x2534: case 0x2535: case 0x2536: case 0x2537:
        case 0x2538: case 0x2539: case 0x253A: case 0x253B:
            junction(true, true, true, false); return true;
        case 0x253C: case 0x253D: case 0x253E: case 0x253F:
        case 0x2540: case 0x2541: case 0x2542: case 0x2543:
        case 0x2544: case 0x2545: case 0x2546: case 0x2547:
        case 0x2548: case 0x2549: case 0x254A: case 0x254B:
            junction(true, true, true, true); return true;
        case 0x2550:
            fillRect(x, cy - thick, w, thick, fg);
            fillRect(x, cy + thick, w, thick, fg);
            return true;
        case 0x2551:
            fillRect(cx - thick, y, thick, h, fg);
            fillRect(cx + thick, y, thick, h, fg);
            return true;
        case 0x2552: case 0x2553: case 0x2554: junction(false, true, false, true); return true;
        case 0x2555: case 0x2556: case 0x2557: junction(true, false, false, true); return true;
        case 0x2558: case 0x2559: case 0x255A: junction(false, true, true, false); return true;
        case 0x255B: case 0x255C: case 0x255D: junction(true, false, true, false); return true;
        case 0x255E: case 0x255F: case 0x2560: junction(false, true, true, true); return true;
        case 0x2561: case 0x2562: case 0x2563: junction(true, false, true, true); return true;
        case 0x2564: case 0x2565: case 0x2566: junction(true, true, false, true); return true;
        case 0x2567: case 0x2568: case 0x2569: junction(true, true, true, false); return true;
        case 0x256A: case 0x256B: case 0x256C: junction(true, true, true, true); return true;
        case 0x2571: drawLine(x, y + h - 1, x + w - 1, y, fg); return true;
        case 0x2572: drawLine(x, y, x + w - 1, y + h - 1, fg); return true;
        case 0x2573:
            drawLine(x, y + h - 1, x + w - 1, y, fg);
            drawLine(x, y, x + w - 1, y + h - 1, fg);
            return true;
        case 0x2574: hline(x, cx); return true;
        case 0x2575: vline(y, cy); return true;
        case 0x2576: hline(cx, x + w - 1); return true;
        case 0x2577: vline(cy, y + h - 1); return true;
        default: return false;
    }
}

bool ThinClientGfx::drawBrailleGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg) {
    if (cp < 0x2800 || cp > 0x28FF) return false;
    const uint8_t bits = static_cast<uint8_t>(cp - 0x2800);
    const int dot = std::max(1, std::min(w, h) / 5);
    const int left = x + std::max(1, w / 4 - dot / 2);
    const int right = x + std::max(1, (w * 3) / 4 - dot / 2);
    const int top = y + std::max(1, h / 8);
    const int gap = std::max(dot + 1, (h - dot - 2) / 4);
    auto drawDot = [&](uint8_t bit, int dx, int dy) {
        if (bits & bit) {
            fillCircle(dx + dot / 2, dy + dot / 2, std::max(1, dot / 2), fg);
        }
    };
    drawDot(0x01, left, top);
    drawDot(0x02, left, top + gap);
    drawDot(0x04, left, top + gap * 2);
    drawDot(0x40, left, top + gap * 3);
    drawDot(0x08, right, top);
    drawDot(0x10, right, top + gap);
    drawDot(0x20, right, top + gap * 2);
    drawDot(0x80, right, top + gap * 3);
    return true;
}

bool ThinClientGfx::drawPowerlineGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg, uint16_t bg) {
    switch (cp) {
        case 0xE0B0:
        case 0xE0B1:
            fillTriangle(x, y, x, y + h - 1, x + w - 1, y + h / 2, fg);
            if (cp == 0xE0B1) {
                fillTriangle(x + 2, y + 3, x + 2, y + h - 4, x + w - 4, y + h / 2, bg);
            }
            return true;
        case 0xE0B2:
        case 0xE0B3:
            fillTriangle(x + w - 1, y, x + w - 1, y + h - 1, x, y + h / 2, fg);
            if (cp == 0xE0B3) {
                fillTriangle(x + w - 3, y + 3, x + w - 3, y + h - 4, x + 3, y + h / 2, bg);
            }
            return true;
        default:
            return false;
    }
}

bool ThinClientGfx::drawFallbackUnicodeGlyph(uint32_t cp, int x, int y, int w, int h, uint16_t fg, uint16_t bg) {
    if (isUnicodeBlank(cp)) return true;
    if (drawBrailleGlyph(cp, x, y, w, h, fg) || drawPowerlineGlyph(cp, x, y, w, h, fg, bg)) return true;
    if (!isFallbackSymbolRange(cp)) return false;

    const int pad = std::max(1, std::min(w, h) / 8);
    const int cx = x + w / 2;
    const int cy = y + h / 2;
    const int r = std::max(2, std::min(w, h) / 3);

    if (cp >= 0x2190 && cp <= 0x21FF) {
        bool drawn = false;
        if (cp == 0x2190 || cp == 0x2194) {
            drawLine(x + pad, cy, x + w - pad - 1, cy, fg);
            fillTriangle(x + pad, cy, x + pad + r / 2, cy - r / 2, x + pad + r / 2, cy + r / 2, fg);
            drawn = true;
        }
        if (cp == 0x2192 || cp == 0x2194 || cp == 0x21D2) {
            drawLine(x + pad, cy, x + w - pad - 1, cy, fg);
            fillTriangle(x + w - pad - 1, cy, x + w - pad - r / 2, cy - r / 2, x + w - pad - r / 2, cy + r / 2, fg);
            drawn = true;
        }
        if (cp == 0x2191 || cp == 0x2195) {
            drawLine(cx, y + pad, cx, y + h - pad - 1, fg);
            fillTriangle(cx, y + pad, cx - r / 2, y + pad + r / 2, cx + r / 2, y + pad + r / 2, fg);
            drawn = true;
        }
        if (cp == 0x2193 || cp == 0x2195) {
            drawLine(cx, y + pad, cx, y + h - pad - 1, fg);
            fillTriangle(cx, y + h - pad - 1, cx - r / 2, y + h - pad - r / 2, cx + r / 2, y + h - pad - r / 2, fg);
            drawn = true;
        }
        if (!drawn) {
            drawLine(x + pad, cy, x + w - pad - 1, cy, fg);
            fillTriangle(x + w - pad - 1, cy, x + w - pad - r / 2, cy - r / 2, x + w - pad - r / 2, cy + r / 2, fg);
        }
        return true;
    }
    if (cp == 0x25CF || cp == 0x26AB || cp == 0x26AA || cp == 0x1F534 || cp == 0x1F535) {
        fillCircle(cx, cy, r, fg);
        return true;
    }
    if (cp == 0x25CB || cp == 0x25EF) {
        drawCircle(cx, cy, r, fg);
        return true;
    }
    if (cp == 0x25A0 || cp == 0x25A1 || cp == 0x25AA || cp == 0x25AB) {
        if (cp == 0x25A0 || cp == 0x25AA) fillRect(x + pad, y + pad, w - pad * 2, h - pad * 2, fg);
        else drawRect(x + pad, y + pad, w - pad * 2, h - pad * 2, fg);
        return true;
    }
    if (cp == 0x25B6 || cp == 0x25B8 || cp == 0x25BA || cp == 0x25C0 || cp == 0x25C2 || cp == 0x25C4) {
        if (cp == 0x25C0 || cp == 0x25C2 || cp == 0x25C4) {
            fillTriangle(x + pad, cy, x + w - pad - 1, y + pad, x + w - pad - 1, y + h - pad - 1, fg);
        } else {
            fillTriangle(x + w - pad - 1, cy, x + pad, y + pad, x + pad, y + h - pad - 1, fg);
        }
        return true;
    }
    drawRect(x + pad, y + pad, w - pad * 2, h - pad * 2, fg);
    drawLine(x + pad, y + pad, x + w - pad - 1, y + h - pad - 1, fg);
    drawLine(x + w - pad - 1, y + pad, x + pad, y + h - pad - 1, fg);
    return true;
}

int ThinClientGfx::drawTerminalText(const TerminalFontSpec& spec, int lineStep, const String& text,
                                    int x, int lineTop, uint16_t fg, uint16_t bg) {
    const auto& bmFont = TerminusBitmap::fontForHeight(static_cast<uint8_t>(spec.cellH));
    int cursorX = x;
    const char* s = text.c_str();
    const size_t len = text.length();
    for (size_t i = 0; i < len;) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        size_t clen = utf8CharLength(c);
        size_t next = std::min(len, i + clen);
        uint32_t cp = utf8CodepointAt(s, len, i);
        if (drawTerminusGlyph(bmFont, cp, cursorX, lineTop, lineStep, fg)) {
            cursorX += spec.cellW;
        } else if (isJapaneseCodepoint(cp)) {
            int jh = u8g2FontHeight(spec.japaneseFont);
            int textY = lineTop + std::max(0, (lineStep - jh) / 2);
            String glyph = text.substring(i, next);
            drawU8g2Text(spec.japaneseFont, glyph.c_str(), cursorX, textY, fg, false);
            cursorX += spec.cellW * 2;
        } else if (drawFallbackUnicodeGlyph(cp, cursorX, lineTop, spec.cellW, lineStep, fg, bg)) {
            cursorX += spec.cellW;
        } else {
            int jh = u8g2FontHeight(spec.japaneseFont);
            int textY = lineTop + std::max(0, (lineStep - jh) / 2);
            String glyph = text.substring(i, next);
            drawU8g2Text(spec.japaneseFont, glyph.c_str(), cursorX, textY, fg, false);
            cursorX += spec.cellW;
        }
        i = next;
    }
    return cursorX - x;
}

void ThinClientGfx::drawTightJapaneseRun(const TerminalFontSpec& spec, int lineStep, const String& text,
                                         int x, int y, int w, uint16_t fg, uint16_t bg) {
    (void)bg;
    setClipRect(x, y, w, lineStep);
    const int jh = u8g2FontHeight(spec.japaneseFont);
    const int textY = y + std::max(0, (lineStep - jh) / 2);
    const int tighten = std::max(1, spec.cellW / 8);
    int cursorX = x;
    const char* s = text.c_str();
    const size_t len = text.length();
    for (size_t i = 0; i < len;) {
        size_t next = std::min(len, i + utf8CharLength(static_cast<uint8_t>(s[i])));
        String glyph = text.substring(i, next);
        int adv = drawU8g2Text(spec.japaneseFont, glyph.c_str(), cursorX, textY, fg, false);
        cursorX += std::max(1, adv - tighten);
        i = next;
    }
    clearClipRect();
}

int ThinClientGfx::drawSettingsString(const uint8_t* jpFont, const String& text, int x, int y, uint16_t fg) {
    int cursorX = x;
    const char* s = text.c_str();
    const size_t len = text.length();
    const int jh = u8g2FontHeight(jpFont);
    for (size_t i = 0; i < len;) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        size_t next = std::min(len, i + utf8CharLength(c));
        if (c >= 0x20 && c < 0x7F && jh <= 18) {
            drawAscii8x16Char(c, cursorX, y + std::max(0, (jh - 16) / 2), fg, 1, 1);
            cursorX += 8;
        } else {
            String glyph = text.substring(i, next);
            cursorX += drawU8g2Text(jpFont, glyph.c_str(), cursorX, y, fg, false);
        }
        i = next;
    }
    return cursorX - x;
}

int ThinClientGfx::settingsTextWidth(const uint8_t* jpFont, const String& text) {
    int width = 0;
    const char* s = text.c_str();
    const size_t len = text.length();
    const int jh = u8g2FontHeight(jpFont);
    for (size_t i = 0; i < len;) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        size_t next = std::min(len, i + utf8CharLength(c));
        if (c >= 0x20 && c < 0x7F && jh <= 18) {
            width += 8;
        } else {
            String glyph = text.substring(i, next);
            width += u8g2TextWidth(jpFont, glyph.c_str());
        }
        i = next;
    }
    return width;
}

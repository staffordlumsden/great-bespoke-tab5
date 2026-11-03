#include "html_fonts.hpp"
#include "fonts/embedded_html_fonts.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "tab5_hal.h"
#endif

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "fonts/stb_truetype_htcw.h"

namespace tab5_html {

namespace {

enum class FaceId : uint8_t {
    sans_reg = 0,
    sans_bold = 1,
    sans_italic = 2,
    sans_bold_italic = 3,
    serif_reg = 4,
    serif_bold = 5,
    serif_italic = 6,
    serif_bold_italic = 7,
    mono_reg = 8,
    mono_bold = 9,
    count = 10
};

struct FaceRecord {
    const uint8_t* data;
    size_t size;
    stbtt_fontinfo info;
    bool valid;
};

FaceRecord s_faces[static_cast<size_t>(FaceId::count)]{};
bool s_initialised = false;

struct CachedGlyph {
    uint32_t key;  // (face_id << 24) | (synth_italic << 23) | (size_px << 16) | (cp & 0xFFFF)
    int16_t advance_px;
    int16_t xoff;
    int16_t yoff;
    uint8_t width;
    uint8_t height;
    uint8_t* bitmap;  // width * height bytes in s_arena
};

constexpr size_t kGlyphCacheSlots = 1024;  // power of 2
constexpr size_t kArenaSize = 384 * 1024;

CachedGlyph* s_cache = nullptr;
uint8_t* s_arena = nullptr;
size_t s_arena_used = 0;

void* alloc_font_mem(size_t bytes) noexcept {
#ifdef ESP_PLATFORM
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
#endif
    return std::malloc(bytes);
}

FaceId select_face(FontFamily family, bool bold, bool italic, bool& out_synth_italic) noexcept {
    out_synth_italic = false;
    switch (family) {
        case FontFamily::times_new_roman:
        case FontFamily::georgia:
            if (bold && italic) return FaceId::serif_bold_italic;
            if (bold) return FaceId::serif_bold;
            if (italic) return FaceId::serif_italic;
            return FaceId::serif_reg;
        case FontFamily::courier_new:
            out_synth_italic = italic;
            return bold ? FaceId::mono_bold : FaceId::mono_reg;
        case FontFamily::montserrat:
        case FontFamily::arial:
        case FontFamily::verdana:
        default:
            if (bold && italic) return FaceId::sans_bold_italic;
            if (bold) return FaceId::sans_bold;
            if (italic) return FaceId::sans_italic;
            return FaceId::sans_reg;
    }
}
uint32_t sanitize_cp(uint32_t cp) noexcept {
    if (cp >= 0x20 && cp <= 0x7E) return cp;
    if (cp >= 0xA0 && cp <= 0xFF) return cp;
    if (cp >= 0x2010 && cp <= 0x203A) return cp;
    if (cp == 0x20AC || cp == 0x2122 || (cp >= 0x2190 && cp <= 0x2194) ||
        cp == 0x2212 || cp == 0x221A || cp == 0x221E || cp == 0x2248 ||
        cp == 0x2260 || cp == 0x2264 || cp == 0x2265 ||
        (cp >= 0x25A0 && cp <= 0x25CF)) {
        return cp;
    }
    return '?';
}

const CachedGlyph* get_cached_glyph(FaceId fid, uint8_t size_px, bool synth_italic, uint32_t raw_cp) noexcept {
    if (!s_initialised && !fonts_init()) return nullptr;
    size_px = std::clamp<uint8_t>(size_px, 10, 56);
    const uint32_t cp = sanitize_cp(raw_cp);
    const uint32_t key = (static_cast<uint32_t>(fid) << 24) |
                         (synth_italic ? (1U << 23) : 0U) |
                         (static_cast<uint32_t>(size_px) << 16) |
                         (cp & 0xFFFFU);

    // Hash lookup (2-way associative probe)
    const size_t h = ((key * 2654435761U) >> 16) & (kGlyphCacheSlots - 1);
    if (s_cache[h].key == key) return &s_cache[h];
    const size_t h2 = (h + 1) & (kGlyphCacheSlots - 1);
    if (s_cache[h2].key == key) return &s_cache[h2];

    FaceRecord& fr = s_faces[static_cast<size_t>(fid)];
    if (!fr.valid) return nullptr;

    const float scale = stbtt_ScaleForPixelHeight(&fr.info, static_cast<float>(size_px));
    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&fr.info, static_cast<int>(cp), &adv, &lsb);
    const int advance_px = std::max(1, static_cast<int>(std::lround(adv * scale)));

    int w = 0, h_bm = 0, xo = 0, yo = 0;
    unsigned char* raw_bm = stbtt_GetCodepointBitmap(&fr.info, 0.0f, scale, static_cast<int>(cp),
                                                     &w, &h_bm, &xo, &yo);

    int final_w = w;
    int final_xo = xo;
    if (synth_italic && w > 0 && h_bm > 0) {
        const int extra_w = (h_bm + 4) / 5 + 1;
        final_w = w + extra_w;
    }

    const size_t bm_bytes = (final_w > 0 && h_bm > 0) ? static_cast<size_t>(final_w * h_bm) : 0;
    if (s_arena_used + bm_bytes > kArenaSize) {
        // Reset glyph cache arena cleanly
        std::memset(s_cache, 0, sizeof(CachedGlyph) * kGlyphCacheSlots);
        s_arena_used = 0;
    }

    uint8_t* dst_bm = nullptr;
    if (bm_bytes > 0 && raw_bm) {
        dst_bm = s_arena + s_arena_used;
        s_arena_used += bm_bytes;
        if (!synth_italic) {
            std::memcpy(dst_bm, raw_bm, bm_bytes);
        } else {
            std::memset(dst_bm, 0, bm_bytes);
            for (int r = 0; r < h_bm; ++r) {
                const int shift = (h_bm - 1 - r) / 5;
                for (int c = 0; c < w; ++c) {
                    dst_bm[r * final_w + c + shift] = raw_bm[r * w + c];
                }
            }
        }
    }
    if (raw_bm) {
        stbtt_FreeBitmap(raw_bm, nullptr);
    }

    const size_t slot = (s_cache[h].key == 0) ? h : h2;
    CachedGlyph& cg = s_cache[slot];
    cg.key = key;
    cg.advance_px = static_cast<int16_t>(advance_px);
    cg.xoff = static_cast<int16_t>(final_xo);
    cg.yoff = static_cast<int16_t>(yo);
    cg.width = static_cast<uint8_t>(std::clamp(final_w, 0, 255));
    cg.height = static_cast<uint8_t>(std::clamp(h_bm, 0, 255));
    cg.bitmap = dst_bm;
    return &cg;
}

uint32_t blend_rgb565_to_rgb24(uint16_t fg565, uint16_t bg565, uint8_t level_1_to_7) noexcept {
    const int fr = ((fg565 >> 11) & 0x1F) * 255 / 31;
    const int fg = ((fg565 >> 5) & 0x3F) * 255 / 63;
    const int fb = (fg565 & 0x1F) * 255 / 31;

    if (level_1_to_7 >= 7) {
        return (static_cast<uint32_t>(fr) << 16) | (static_cast<uint32_t>(fg) << 8) | static_cast<uint32_t>(fb);
    }

    const int br = ((bg565 >> 11) & 0x1F) * 255 / 31;
    const int bg = ((bg565 >> 5) & 0x3F) * 255 / 63;
    const int bb = (bg565 & 0x1F) * 255 / 31;

    const int a = level_1_to_7;
    const int inv = 7 - a;
    const uint32_t r = static_cast<uint32_t>((fr * a + br * inv) / 7);
    const uint32_t g = static_cast<uint32_t>((fg * a + bg * inv) / 7);
    const uint32_t b = static_cast<uint32_t>((fb * a + bb * inv) / 7);
    return (r << 16) | (g << 8) | b;
}

}  // namespace

bool fonts_init() noexcept {
    if (s_initialised) return true;

    s_cache = static_cast<CachedGlyph*>(alloc_font_mem(sizeof(CachedGlyph) * kGlyphCacheSlots));
    s_arena = static_cast<uint8_t*>(alloc_font_mem(kArenaSize));
    if (!s_cache || !s_arena) return false;
    std::memset(s_cache, 0, sizeof(CachedGlyph) * kGlyphCacheSlots);
    s_arena_used = 0;

    using namespace tab5_html_fonts;
    const struct {
        FaceId id;
        const uint8_t* data;
        size_t size;
    } defs[] = {
        {FaceId::sans_reg, kSourceSansRegularTtf, kSourceSansRegularTtfSize},
        {FaceId::sans_bold, kSourceSansBoldTtf, kSourceSansBoldTtfSize},
        {FaceId::sans_italic, kSourceSansItalicTtf, kSourceSansItalicTtfSize},
        {FaceId::sans_bold_italic, kSourceSansBoldItalicTtf, kSourceSansBoldItalicTtfSize},
        {FaceId::serif_reg, kSourceSerifRegularTtf, kSourceSerifRegularTtfSize},
        {FaceId::serif_bold, kSourceSerifBoldTtf, kSourceSerifBoldTtfSize},
        {FaceId::serif_italic, kSourceSerifItalicTtf, kSourceSerifItalicTtfSize},
        {FaceId::serif_bold_italic, kSourceSerifBoldItalicTtf, kSourceSerifBoldItalicTtfSize},
        {FaceId::mono_reg, kSourceCodeRegularTtf, kSourceCodeRegularTtfSize},
        {FaceId::mono_bold, kSourceCodeBoldTtf, kSourceCodeBoldTtfSize},
    };;

    for (const auto& d : defs) {
        FaceRecord& fr = s_faces[static_cast<size_t>(d.id)];
        fr.data = d.data;
        fr.size = d.size;
        fr.valid = (stbtt_InitFont(&fr.info, const_cast<unsigned char*>(d.data), 0) != 0);
    }

    s_initialised = true;
    return true;
}

FontMetrics font_get_metrics(FontFamily family, uint8_t size_px, bool bold, bool italic) noexcept {
    if (!s_initialised) fonts_init();
    size_px = std::clamp<uint8_t>(size_px, 10, 56);
    bool synth_italic = false;
    const FaceId fid = select_face(family, bold, italic, synth_italic);
    const FaceRecord& fr = s_faces[static_cast<size_t>(fid)];

    FontMetrics m{};
    if (!fr.valid) {
        m.ascent_px = (size_px * 4) / 5;
        m.descent_px = size_px / 5;
        m.line_height_px = (size_px * 13) / 10;
        m.space_advance_px = std::max(4, size_px / 3);
        return m;
    }

    const float scale = stbtt_ScaleForPixelHeight(&fr.info, static_cast<float>(size_px));
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&fr.info, &asc, &desc, &gap);
    m.ascent_px = std::max(1, static_cast<int>(std::lround(asc * scale)));
    m.descent_px = std::max(1, static_cast<int>(std::lround(-desc * scale)));
    m.line_height_px = std::max(static_cast<int>(size_px) + 4,
                                m.ascent_px + m.descent_px + static_cast<int>(std::lround(gap * scale)) + 3);
    int sp_adv = 0, sp_lsb = 0;
    stbtt_GetCodepointHMetrics(&fr.info, ' ', &sp_adv, &sp_lsb);
    m.space_advance_px = std::max(3, static_cast<int>(std::lround(sp_adv * scale)));
    return m;
}

size_t decode_utf8_one(const char* s, size_t max_len, uint32_t& out_cp) noexcept {
    if (!s || max_len == 0) {
        out_cp = 0;
        return 0;
    }
    const uint8_t b0 = static_cast<uint8_t>(s[0]);
    if (b0 < 0x80) {
        out_cp = b0;
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0 && max_len >= 2) {
        out_cp = ((b0 & 0x1FU) << 6) | (static_cast<uint8_t>(s[1]) & 0x3FU);
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0 && max_len >= 3) {
        out_cp = ((b0 & 0x0FU) << 12) |
                 ((static_cast<uint8_t>(s[1]) & 0x3FU) << 6) |
                 (static_cast<uint8_t>(s[2]) & 0x3FU);
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0 && max_len >= 4) {
        out_cp = '?';
        return 4;
    }
    out_cp = '?';
    return 1;
}

int font_measure_codepoint(FontFamily family,
                           uint8_t size_px,
                           bool bold,
                           bool italic,
                           uint32_t codepoint) noexcept {
    bool synth_italic = false;
    const FaceId fid = select_face(family, bold, italic, synth_italic);
    const CachedGlyph* cg = get_cached_glyph(fid, size_px, synth_italic, codepoint);
    return cg ? cg->advance_px : (size_px / 2);
}

int font_measure_utf8(FontFamily family,
                      uint8_t size_px,
                      bool bold,
                      bool italic,
                      const char* utf8,
                      size_t byte_len) noexcept {
    if (!utf8 || byte_len == 0) return 0;
    bool synth_italic = false;
    const FaceId fid = select_face(family, bold, italic, synth_italic);
    int total = 0;
    size_t pos = 0;
    while (pos < byte_len) {
        uint32_t cp = 0;
        const size_t step = decode_utf8_one(utf8 + pos, byte_len - pos, cp);
        if (step == 0) break;
        pos += step;
        const CachedGlyph* cg = get_cached_glyph(fid, size_px, synth_italic, cp);
        if (cg) total += cg->advance_px;
    }
    return total;
}

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
                   int clip_top,
                   int clip_bottom) noexcept {
    if (!utf8 || byte_len == 0) return 0;
    const FontMetrics m = font_get_metrics(family, size_px, bold, italic);
    const int baseline_y = y + m.ascent_px;

    bool synth_italic = false;
    const FaceId fid = select_face(family, bold, italic, synth_italic);

    uint32_t palette[8];
    palette[0] = 0;
    for (uint8_t lv = 1; lv <= 7; ++lv) {
        palette[lv] = blend_rgb565_to_rgb24(fg_rgb565, bg_rgb565, lv);
    }

#ifdef ESP_PLATFORM
    const int sw = tab5_display_width();
    const int sh = tab5_display_height();
#else
    const int sw = 1280;
    const int sh = 720;
    (void)palette;
    (void)baseline_y;
    (void)clip_top;
    (void)clip_bottom;
    (void)sw;
    (void)sh;
#endif

    int pen_x = x;
    size_t pos = 0;
    while (pos < byte_len) {
        uint32_t cp = 0;
        const size_t step = decode_utf8_one(utf8 + pos, byte_len - pos, cp);
        if (step == 0) break;
        pos += step;

        const CachedGlyph* cg = get_cached_glyph(fid, size_px, synth_italic, cp);
        if (!cg) continue;

#ifdef ESP_PLATFORM
        if (cg->bitmap && cg->width > 0 && cg->height > 0) {
            const int gx = pen_x + cg->xoff;
            const int gy = baseline_y + cg->yoff;
            for (int r = 0; r < cg->height; ++r) {
                const int py = gy + r;
                if (py < clip_top || py >= clip_bottom || py < 0 || py >= sh) continue;
                const uint8_t* row_ptr = cg->bitmap + r * cg->width;
                int c = 0;
                while (c < cg->width) {
                    const uint8_t lv = static_cast<uint8_t>((row_ptr[c] + 18) >> 5);
                    if (lv == 0) {
                        ++c;
                        continue;
                    }
                    int run_end = c + 1;
                    while (run_end < cg->width &&
                           static_cast<uint8_t>((row_ptr[run_end] + 18) >> 5) == lv) {
                        ++run_end;
                    }
                    int rx = gx + c;
                    int rw = run_end - c;
                    if (rx < 0) {
                        rw += rx;
                        rx = 0;
                    }
                    if (rx + rw > sw) {
                        rw = sw - rx;
                    }
                    if (rw > 0) {
                        tab5_display_rect(rx, py, rw, 1, palette[std::min<uint8_t>(lv, 7)]);
                    }
                    c = run_end;
                }
            }
        }
#endif
        pen_x += cg->advance_px;
    }
    return pen_x - x;
}

}  // namespace tab5_html

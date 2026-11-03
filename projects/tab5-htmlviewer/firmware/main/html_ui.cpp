#include "html_ui.hpp"
#include "fonts/pixel_fonts.h"
#include "html_fonts.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"

namespace tab5_html {

namespace {

constexpr uint32_t kBlue  = 0x2F6FED;
constexpr uint32_t kGreen = 0x179B62;
constexpr uint32_t kAmber = 0xD9822B;
constexpr uint32_t kRed   = 0xD64545;

constexpr int kReaderTopBarH = 46;

struct ChromeTheme {
    uint32_t bg;
    uint32_t surface;
    uint32_t card;
    uint32_t border;
    uint32_t text;
    uint32_t muted;
};

constexpr ChromeTheme kDarkChrome{
    0x101215, 0x181C21, 0x1D2228, 0x343D48, 0xF2F5F8, 0x9BA6B2
};

constexpr ChromeTheme kLightChrome{
    0xEEF1F4, 0xE2E7EC, 0xFFFFFF, 0xB8C2CC, 0x181C20, 0x5B6672
};

const ChromeTheme& get_chrome(bool dark) noexcept {
    return dark ? kDarkChrome : kLightChrome;
}

constexpr uint16_t rgb24_to_565(uint32_t rgb) noexcept {
    const uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    const uint8_t g = static_cast<uint8_t>((rgb >> 8) & 0xFF);
    const uint8_t b = static_cast<uint8_t>(rgb & 0xFF);
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint32_t rgb565_to_24(uint16_t c) noexcept {
    const uint32_t r = ((c >> 11) & 0x1F) * 255 / 31;
    const uint32_t g = ((c >> 5) & 0x3F) * 255 / 63;
    const uint32_t b = (c & 0x1F) * 255 / 31;
    return (r << 16) | (g << 8) | b;
}

void* alloc_ui_mem(size_t bytes) noexcept {
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
    return std::malloc(bytes);
}

void rect(int x, int y, int w, int h, uint32_t rgb) noexcept {
    const int sw = tab5_display_width();
    const int sh = tab5_display_height();
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w > 0 && h > 0) {
        tab5_display_rect(x, y, w, h, rgb);
    }
}

void box(int x, int y, int w, int h, uint32_t fill, uint32_t border, int bw = 2) noexcept {
    rect(x, y, w, h, fill);
    rect(x, y, w, bw, border);
    rect(x, y + h - bw, w, bw, border);
    rect(x, y, bw, h, border);
    rect(x + w - bw, y, bw, h, border);
}

int pf_text_width(const cyd_fonts::Font& font, const char* s, int scale = 2) noexcept {
    if (!s || !*s) return 0;
    int w = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
        unsigned char c = *p;
        if (c < font.first || c > font.last) c = '?';
        w += font.glyphs[c - font.first].advance * scale;
    }
    return w;
}

int draw_pf_char(const cyd_fonts::Font& font, int x, int y, char ch, uint32_t rgb, int scale = 2) noexcept {
    unsigned char code = static_cast<unsigned char>(ch);
    if (code < font.first || code > font.last) code = '?';
    const cyd_fonts::Glyph& g = font.glyphs[code - font.first];
    const int base_x = x + g.x * scale;
    const int base_y = y + (static_cast<int>(font.ascent) + g.y) * scale;
    for (int row = 0; row < g.height; ++row) {
        int run_start = -1;
        for (int col = 0; col <= g.width; ++col) {
            bool on = false;
            if (col < g.width) {
                const int bit_index = row * g.width + col;
                const uint8_t byte = font.bitmap[g.offset + (bit_index >> 3)];
                on = (byte & (0x80U >> (bit_index & 7))) != 0;
            }
            if (on && run_start < 0) {
                run_start = col;
            } else if (!on && run_start >= 0) {
                rect(base_x + run_start * scale, base_y + row * scale,
                     (col - run_start) * scale, scale, rgb);
                run_start = -1;
            }
        }
    }
    return g.advance * scale;
}

void draw_pf_text(const cyd_fonts::Font& font, int x, int y, const char* s, uint32_t rgb, int scale = 2, int max_w = 0) noexcept {
    if (!s) return;
    int cx = x;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        unsigned char code = static_cast<unsigned char>(s[i]);
        if (code < font.first || code > font.last) code = '?';
        const int step = font.glyphs[code - font.first].advance * scale;
        if (max_w > 0 && (cx - x + step) > max_w) break;
        cx += draw_pf_char(font, cx, y, s[i], rgb, scale);
    }
}

void draw_pf_button(int x, int y, int w, int h, const char* label, uint32_t fill, uint32_t border, uint32_t text_rgb, int scale = 2) noexcept {
    box(x, y, w, h, fill, border, 2);
    const auto& font = cyd_fonts::pixelfont_11x5_thin;
    const int tw = pf_text_width(font, label, scale);
    const int tx = x + std::max(4, (w - tw) / 2);
    const int ty = y + (h - static_cast<int>(font.ascent) * scale) / 2;
    draw_pf_text(font, tx, ty, label, text_rgb, scale, w - 8);
}

void draw_power_glyph(int x, int y, bool charging) noexcept {
    if (charging) {
        const uint32_t gold = 0xF5B623;
        rect(x + 8, y + 0, 5, 3, gold);
        rect(x + 6, y + 3, 5, 3, gold);
        rect(x + 3, y + 6, 10, 3, gold);
        rect(x + 6, y + 9, 5, 3, gold);
        rect(x + 4, y + 12, 4, 4, gold);
    } else {
        rect(x + 2, y + 3, 10, 11, kRed);
        rect(x + 4, y + 1, 6, 2, kRed);
        rect(x + 4, y + 5, 6, 4, 0x181C20);
        rect(x + 4, y + 10, 6, 2, kRed);
    }
}

bool within(int px, int py, int x, int y, int w, int h) noexcept {
    return px >= x && px < (x + w) && py >= y && py < (y + h);
}

// ============================================================================
// Pre-computed Document Layout Structures (allocated in SPIRAM)
// ============================================================================
struct LayoutBox {
    int y;
    int h;
    int x;
    int w;
    bool has_fill;
    uint16_t fill_rgb565;
    bool has_border_left;
    uint16_t border_left_rgb565;
    bool has_border_box;
    uint16_t border_box_rgb565;
    bool is_hr;
};

struct LayoutRun {
    int x;
    int y;
    int w;
    int h;
    uint32_t text_offset;
    uint16_t text_len;
    FontFamily family;
    uint8_t size_px;
    bool bold : 1;
    bool italic : 1;
    bool underline : 1;
    bool strikethrough : 1;
    bool has_bg : 1;
    uint16_t fg_rgb565;
    uint16_t bg_rgb565;
    int16_t link_index;
};

constexpr size_t kMaxLayoutBoxes = 1536;
constexpr size_t kMaxLayoutRuns = 6144;

LayoutBox* s_boxes = nullptr;
size_t s_box_count = 0;

LayoutRun* s_runs = nullptr;
size_t s_run_count = 0;

int s_block_y[kMaxBlocks]{};
int s_last_layout_sw = 0;

bool ensure_layout_buffers() noexcept {
    if (!s_boxes) s_boxes = static_cast<LayoutBox*>(alloc_ui_mem(sizeof(LayoutBox) * kMaxLayoutBoxes));
    if (!s_runs) s_runs = static_cast<LayoutRun*>(alloc_ui_mem(sizeof(LayoutRun) * kMaxLayoutRuns));
    return s_boxes && s_runs;
}

FontFamily apply_font_override(FontFamily doc_fam, FontOverride ov, bool is_code) noexcept {
    if (is_code && ov == FontOverride::doc_css) return FontFamily::courier_new;
    switch (ov) {
        case FontOverride::doc_css: return doc_fam;
        case FontOverride::force_montserrat: return FontFamily::montserrat;
        case FontOverride::force_times: return FontFamily::times_new_roman;
        case FontOverride::force_courier: return FontFamily::courier_new;
    }
    return doc_fam;
}

uint8_t scale_font_px(uint8_t base_px, uint16_t zoom_pct) noexcept {
    const int scaled = (static_cast<int>(base_px) * static_cast<int>(zoom_pct) + 50) / 100;
    return static_cast<uint8_t>(std::clamp(scaled, 10, 54));
}

int flow_spans_into_runs(const HtmlDocument& doc,
                         uint16_t span_start,
                         uint16_t span_count,
                         int content_x,
                         int content_w,
                         int start_y,
                         TextAlign align,
                         uint16_t zoom_pct,
                         FontOverride font_ov,
                         bool preserve_newlines,
                         uint16_t default_fg_565,
                         bool override_fg) noexcept {
    if (span_count == 0 || content_w <= 20) return start_y;

    size_t line_first_run = s_run_count;
    int cur_x = 0;
    int line_max_h = 0;
    int cur_y = start_y;

    auto finish_line = [&]() {
        if (s_run_count > line_first_run) {
            int dx = content_x;
            if (align == TextAlign::center && cur_x < content_w) {
                dx = content_x + (content_w - cur_x) / 2;
            } else if (align == TextAlign::right && cur_x < content_w) {
                dx = content_x + (content_w - cur_x);
            }
            for (size_t r = line_first_run; r < s_run_count; ++r) {
                s_runs[r].x += dx;
                s_runs[r].y = cur_y + (line_max_h - s_runs[r].h);
            }
            cur_y += line_max_h;
        } else if (preserve_newlines && line_max_h > 0) {
            cur_y += line_max_h;
        }
        cur_x = 0;
        line_max_h = 0;
        line_first_run = s_run_count;
    };

    for (uint16_t si = 0; si < span_count; ++si) {
        const HtmlSpan& sp = doc.spans[span_start + si];
        const FontFamily fam = apply_font_override(sp.family, font_ov, sp.inline_code);
        const uint8_t fpx = scale_font_px(sp.size_px, zoom_pct);
        const FontMetrics fm = font_get_metrics(fam, fpx, sp.bold, sp.italic);
        const uint16_t fg565 = override_fg ? default_fg_565 : sp.fg_rgb565;

        const char* txt = doc.text_pool + sp.text_offset;
        const size_t tlen = sp.text_len;
        size_t pos = 0;

        while (pos < tlen) {
            if (txt[pos] == '\n') {
                if (line_max_h < fm.line_height_px) line_max_h = fm.line_height_px;
                finish_line();
                ++pos;
                continue;
            }

            if (preserve_newlines) {
                size_t line_end = pos;
                while (line_end < tlen && txt[line_end] != '\n') ++line_end;
                const size_t seg_len = line_end - pos;
                if (seg_len > 0 && s_run_count < kMaxLayoutRuns) {
                    const int w = font_measure_utf8(fam, fpx, sp.bold, sp.italic, txt + pos, seg_len);
                    LayoutRun& lr = s_runs[s_run_count++];
                    lr.x = cur_x;
                    lr.y = cur_y;
                    lr.w = w;
                    lr.h = fm.line_height_px;
                    lr.text_offset = sp.text_offset + static_cast<uint32_t>(pos);
                    lr.text_len = static_cast<uint16_t>(seg_len);
                    lr.family = fam;
                    lr.size_px = fpx;
                    lr.bold = sp.bold;
                    lr.italic = sp.italic;
                    lr.underline = sp.underline;
                    lr.strikethrough = sp.strikethrough;
                    lr.has_bg = sp.has_bg;
                    lr.fg_rgb565 = fg565;
                    lr.bg_rgb565 = sp.bg_rgb565;
                    lr.link_index = sp.link_index;
                    cur_x += w;
                    line_max_h = std::max(line_max_h, fm.line_height_px);
                }
                pos = line_end;
                continue;
            }

            size_t tok_end = pos;
            if (txt[pos] == ' ') {
                while (tok_end < tlen && txt[tok_end] == ' ') ++tok_end;
            } else {
                while (tok_end < tlen && txt[tok_end] != ' ' && txt[tok_end] != '\n') ++tok_end;
                if (tok_end < tlen && txt[tok_end] == ' ') ++tok_end;
            }

            const size_t wlen = tok_end - pos;
            if (wlen == 1 && txt[pos] == ' ' && cur_x == 0) {
                pos = tok_end;
                continue;
            }

            const int ww = font_measure_utf8(fam, fpx, sp.bold, sp.italic, txt + pos, wlen);
            if (cur_x > 0 && cur_x + ww > content_w) {
                finish_line();
                if (wlen == 1 && txt[pos] == ' ') {
                    pos = tok_end;
                    continue;
                }
            }

            if (s_run_count > line_first_run) {
                LayoutRun& prev = s_runs[s_run_count - 1];
                if (prev.text_offset + prev.text_len == sp.text_offset + pos &&
                    prev.family == fam && prev.size_px == fpx &&
                    prev.bold == sp.bold && prev.italic == sp.italic &&
                    prev.underline == sp.underline && prev.strikethrough == sp.strikethrough &&
                    prev.has_bg == sp.has_bg && prev.fg_rgb565 == fg565 &&
                    prev.bg_rgb565 == sp.bg_rgb565 && prev.link_index == sp.link_index) {
                    prev.text_len = static_cast<uint16_t>(prev.text_len + wlen);
                    prev.w += ww;
                    cur_x += ww;
                    line_max_h = std::max(line_max_h, fm.line_height_px);
                    pos = tok_end;
                    continue;
                }
            }

            if (s_run_count < kMaxLayoutRuns) {
                LayoutRun& lr = s_runs[s_run_count++];
                lr.x = cur_x;
                lr.y = cur_y;
                lr.w = ww;
                lr.h = fm.line_height_px;
                lr.text_offset = sp.text_offset + static_cast<uint32_t>(pos);
                lr.text_len = static_cast<uint16_t>(wlen);
                lr.family = fam;
                lr.size_px = fpx;
                lr.bold = sp.bold;
                lr.italic = sp.italic;
                lr.underline = sp.underline;
                lr.strikethrough = sp.strikethrough;
                lr.has_bg = sp.has_bg;
                lr.fg_rgb565 = fg565;
                lr.bg_rgb565 = sp.bg_rgb565;
                lr.link_index = sp.link_index;
                cur_x += ww;
                line_max_h = std::max(line_max_h, fm.line_height_px);
            }
            pos = tok_end;
        }
    }

    if (s_run_count > line_first_run) {
        finish_line();
    }
    return cur_y;
}

}  // namespace

bool viewer_init(ViewerState& state) noexcept {
    fonts_init();
    if (!ensure_layout_buffers()) return false;
    if (!doc_alloc(&state.doc)) return false;

    state.raw_html_cap = 131072;
    state.raw_html = static_cast<char*>(alloc_ui_mem(state.raw_html_cap));
    state.picker_entries = static_cast<PickerEntry*>(alloc_ui_mem(sizeof(PickerEntry) * kMaxPickerEntries));
    if (!state.raw_html || !state.picker_entries) return false;
    state.raw_html[0] = '\0';
    state.raw_html_len = 0;
    state.screen = ViewScreen::file_picker;
    state.layout_dirty = true;
    return true;
}

void viewer_refresh_picker(ViewerState& state) noexcept {
    state.picker_count = 0;
    state.picker_selected = 0;
    state.picker_scroll_offset = 0;
    if (!state.sd_mounted || !state.picker_entries) return;

    if (state.picker_tab == PickerTab::all_html) {
        scan_all_html_files(state.sd_root, state.picker_entries, kMaxPickerEntries, state.picker_count, 4);
        std::snprintf(state.status_msg, sizeof(state.status_msg),
                      "FOUND %u HTML FILES ON SD CARD", static_cast<unsigned>(state.picker_count));
    } else {
        list_picker_directory(state.sd_root, state.picker_dir, state.picker_html_only,
                              state.picker_entries, kMaxPickerEntries, state.picker_count);
        std::snprintf(state.status_msg, sizeof(state.status_msg),
                      "DIR %.64s (%u ITEMS)", state.picker_dir, static_cast<unsigned>(state.picker_count));
    }
}

bool viewer_load_file(ViewerState& state, const char* rel_path, bool push_history) noexcept {
    if (!rel_path || !rel_path[0] || !state.sd_mounted) return false;

    char vfs_path[256];
    char clean_rel[192];
    if (!resolve_sd_vfs_path(state.sd_root, rel_path, vfs_path, sizeof(vfs_path),
                             clean_rel, sizeof(clean_rel))) {
        std::snprintf(state.status_msg, sizeof(state.status_msg), "INVALID SD PATH");
        return false;
    }

    FILE* fp = std::fopen(vfs_path, "rb");
    if (!fp) {
        std::snprintf(state.status_msg, sizeof(state.status_msg), "FILE NOT FOUND: %.64s", clean_rel);
        return false;
    }

    std::fseek(fp, 0, SEEK_END);
    const long fsize = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (fsize < 0) {
        std::fclose(fp);
        return false;
    }

    if (push_history && state.current_file_rel[0] &&
        std::strcmp(state.current_file_rel, clean_rel) != 0) {
        if (state.history_count < kMaxHistory) {
            HistoryEntry& he = state.history[state.history_count++];
            std::snprintf(he.rel_path, sizeof(he.rel_path), "%s", state.current_file_rel);
            he.scroll_y = state.scroll_y;
        }
    }

    const size_t read_max = std::min<size_t>(static_cast<size_t>(fsize), state.raw_html_cap - 1);
    const size_t nread = std::fread(state.raw_html, 1, read_max, fp);
    std::fclose(fp);

    state.raw_html[nread] = '\0';
    state.raw_html_len = nread;
    state.current_file_size = static_cast<uint64_t>(fsize);
    std::snprintf(state.current_file_rel, sizeof(state.current_file_rel), "%s", clean_rel);

    parse_html_document(state.raw_html, state.raw_html_len, &state.doc);
    state.scroll_y = 0;
    state.layout_dirty = true;
    state.screen = ViewScreen::html_reader;
    return true;
}

void viewer_relayout(ViewerState& state) noexcept {
    if (!ensure_layout_buffers()) return;
    s_box_count = 0;
    s_run_count = 0;

    const int sw = tab5_display_width();
    const int sh = tab5_display_height();
    s_last_layout_sw = sw;

    const int vp_h = std::max(100, sh - kReaderTopBarH);

    if (state.source_view) {
        int lines = 1;
        for (size_t i = 0; i < state.raw_html_len; ++i) {
            if (state.raw_html[i] == '\n') ++lines;
        }
        const uint8_t src_px = scale_font_px(15, state.zoom_pct);
        const FontMetrics fm = font_get_metrics(FontFamily::courier_new, src_px, false, false);
        state.total_doc_height = lines * fm.line_height_px + 32;
        state.max_scroll_y = std::max(0, state.total_doc_height - vp_h);
        state.scroll_y = std::clamp(state.scroll_y, 0, state.max_scroll_y);
        state.layout_dirty = false;
        return;
    }

    // Full-width HTML layout without any sidebar or overlay
    const int margin_x = (sw >= 1000) ? 40 : 24;
    const int base_w = sw - margin_x * 2;
    int cur_y = 20;

    const bool force_dark_text = (state.dark_ui && !state.doc.has_doc_bg);
    const uint16_t default_fg = force_dark_text ? 0xF7BE : state.doc.doc_fg_rgb565;

    for (size_t bi = 0; bi < state.doc.block_count; ++bi) {
        const HtmlBlock& blk = state.doc.blocks[bi];
        const int mt = (blk.margin_top_px * state.zoom_pct) / 100;
        const int mb = (blk.margin_bottom_px * state.zoom_pct) / 100;
        const int pad = (blk.padding_px * state.zoom_pct) / 100;
        cur_y += mt;
        s_block_y[bi] = cur_y;

        if (blk.type == BlockType::hr) {
            if (s_box_count < kMaxLayoutBoxes) {
                LayoutBox& lb = s_boxes[s_box_count++];
                lb.x = margin_x;
                lb.y = cur_y + 6;
                lb.w = base_w;
                lb.h = 2;
                lb.has_fill = true;
                lb.fill_rgb565 = force_dark_text ? rgb24_to_565(0x343D48) : rgb24_to_565(0xCBD5E1);
                lb.has_border_left = false;
                lb.has_border_box = false;
                lb.is_hr = true;
            }
            cur_y += 16 + mb;
            continue;
        }

        if (blk.type == BlockType::table_row && blk.cell_count > 0) {
            const int cols = blk.cell_count;
            const int col_w = base_w / cols;
            const size_t first_box_idx = s_box_count;
            for (int c = 0; c < cols && s_box_count < kMaxLayoutBoxes; ++c) {
                LayoutBox& cb = s_boxes[s_box_count++];
                cb.x = margin_x + c * col_w;
                cb.y = cur_y;
                cb.w = col_w;
                cb.h = 28;
                cb.has_fill = blk.cells[c].has_bg || blk.cells[c].is_header;
                cb.fill_rgb565 = blk.cells[c].has_bg
                                     ? blk.cells[c].bg_rgb565
                                     : (force_dark_text ? rgb24_to_565(0x222933) : rgb24_to_565(0xE2E8F0));
                cb.has_border_left = false;
                cb.has_border_box = true;
                cb.border_box_rgb565 = force_dark_text ? rgb24_to_565(0x3B4554) : rgb24_to_565(0xCBD5E1);
                cb.is_hr = false;
            }

            int max_row_end_y = cur_y + 24;
            for (int c = 0; c < cols; ++c) {
                const HtmlTableCell& cell = blk.cells[c];
                const int cx = margin_x + c * col_w + 8;
                const int cw = std::max(24, col_w - 16);
                const int end_y = flow_spans_into_runs(
                    state.doc, cell.span_start, cell.span_count, cx, cw, cur_y + 6,
                    cell.align, state.zoom_pct, state.font_override, false, default_fg, force_dark_text);
                max_row_end_y = std::max(max_row_end_y, end_y + 6);
            }

            const int row_h = max_row_end_y - cur_y;
            for (int c = 0; c < cols && (first_box_idx + c) < s_box_count; ++c) {
                s_boxes[first_box_idx + c].h = row_h;
            }
            cur_y = max_row_end_y;
            continue;
        }

        int indent = 0;
        if (blk.type == BlockType::list_item) {
            indent = (blk.list_depth + 1) * 28;
        } else if (blk.type == BlockType::blockquote) {
            indent = 16;
        }

        const size_t box_idx = s_box_count;
        const bool needs_box = blk.has_bg || blk.has_border_left || blk.has_border_box ||
                               blk.type == BlockType::blockquote || blk.type == BlockType::code_block;
        if (needs_box && s_box_count < kMaxLayoutBoxes) {
            LayoutBox& lb = s_boxes[s_box_count++];
            lb.x = margin_x + indent;
            lb.y = cur_y;
            lb.w = base_w - indent;
            lb.h = 20;
            lb.has_fill = blk.has_bg || blk.type == BlockType::blockquote || blk.type == BlockType::code_block;
            if (force_dark_text) {
                lb.fill_rgb565 = (blk.type == BlockType::code_block) ? rgb24_to_565(0x161A20) : rgb24_to_565(0x1E2530);
            } else {
                lb.fill_rgb565 = blk.has_bg ? blk.bg_rgb565 : rgb24_to_565(0xF1F5F9);
            }
            lb.has_border_left = blk.has_border_left || blk.type == BlockType::blockquote;
            lb.border_left_rgb565 = blk.has_border_left ? blk.border_left_rgb565 : rgb24_to_565(kBlue);
            lb.has_border_box = blk.has_border_box || blk.type == BlockType::code_block;
            lb.border_box_rgb565 = blk.has_border_box ? blk.border_box_rgb565 : rgb24_to_565(0xCBD5E1);
            lb.is_hr = false;
        }

        const int inner_x = margin_x + indent + pad + (blk.has_border_left ? 8 : 0);
        const int inner_w = std::max(40, base_w - indent - pad * 2 - (blk.has_border_left ? 8 : 0));
        const int text_start_y = cur_y + pad;

        if (blk.type == BlockType::list_item && blk.span_count > 0 && s_run_count < kMaxLayoutRuns) {
            const HtmlSpan& sp0 = state.doc.spans[blk.span_start];
            const FontFamily fam = apply_font_override(sp0.family, state.font_override, false);
            const uint8_t fpx = scale_font_px(sp0.size_px, state.zoom_pct);
            const FontMetrics fm = font_get_metrics(fam, fpx, true, false);

            char marker[16];
            if (blk.ordered_index > 0) {
                std::snprintf(marker, sizeof(marker), "%u.", static_cast<unsigned>(blk.ordered_index));
            } else {
                std::snprintf(marker, sizeof(marker), "\xE2\x80\xA2");
            }
            const size_t mlen = std::strlen(marker);
            if (state.doc.text_len + mlen + 2 < state.doc.text_cap) {
                const uint32_t m_off = static_cast<uint32_t>(state.doc.text_len);
                std::memcpy(state.doc.text_pool + state.doc.text_len, marker, mlen);
                state.doc.text_len += mlen;
                state.doc.text_pool[state.doc.text_len] = '\0';

                LayoutRun& mr = s_runs[s_run_count++];
                mr.x = inner_x - 22;
                mr.y = text_start_y;
                mr.w = 18;
                mr.h = fm.line_height_px;
                mr.text_offset = m_off;
                mr.text_len = static_cast<uint16_t>(mlen);
                mr.family = fam;
                mr.size_px = fpx;
                mr.bold = true;
                mr.italic = false;
                mr.underline = false;
                mr.strikethrough = false;
                mr.has_bg = false;
                mr.fg_rgb565 = rgb24_to_565(kBlue);
                mr.bg_rgb565 = 0;
                mr.link_index = -1;
            }
        }

        const int text_end_y = flow_spans_into_runs(
            state.doc, blk.span_start, blk.span_count, inner_x, inner_w, text_start_y,
            blk.align, state.zoom_pct, state.font_override,
            blk.type == BlockType::code_block, default_fg, force_dark_text && !blk.has_bg);

        cur_y = text_end_y + pad;
        if (needs_box && box_idx < s_box_count) {
            s_boxes[box_idx].h = std::max(16, cur_y - s_boxes[box_idx].y);
        }

        if (blk.type == BlockType::heading && blk.heading_level <= 2 && s_box_count < kMaxLayoutBoxes) {
            cur_y += 4;
            LayoutBox& hr = s_boxes[s_box_count++];
            hr.x = margin_x;
            hr.y = cur_y;
            hr.w = base_w;
            hr.h = (blk.heading_level == 1) ? 2 : 1;
            hr.has_fill = true;
            hr.fill_rgb565 = force_dark_text ? rgb24_to_565(0x2D3643) : rgb24_to_565(0xE2E8F0);
            hr.has_border_left = false;
            hr.has_border_box = false;
            hr.is_hr = true;
            cur_y += 4;
        }

        cur_y += mb;
    }

    state.total_doc_height = std::max(vp_h, cur_y + 28);
    state.max_scroll_y = std::max(0, state.total_doc_height - vp_h);
    state.scroll_y = std::clamp(state.scroll_y, 0, state.max_scroll_y);
    state.layout_dirty = false;
}

namespace {

// ============================================================================
// Screen 1: Full-Screen SD Card File Picker
// ============================================================================
void draw_file_picker_screen(const ViewerState& state) noexcept {
    const ChromeTheme& ct = get_chrome(state.dark_ui);
    const int sw = tab5_display_width();
    const int sh = tab5_display_height();
    const bool portrait = (sw < 900);

    rect(0, 0, sw, sh, ct.bg);

    // Top Header Bar (y = 0..56)
    rect(0, 0, sw, 56, ct.surface);
    rect(0, 54, sw, 2, ct.border);
    draw_pf_text(cyd_fonts::pixelfont_13x7_semibold, 18, 16, "TAB5 HTML VIEWER", ct.text, 2);

    const int console_btn_x = sw - 130;
    draw_pf_button(console_btn_x, 10, 114, 36, "CONSOLE", ct.card, kRed, kRed, 2);

    const int rot_btn_x = console_btn_x - 126;
    draw_pf_button(rot_btn_x, 10, 116, 36, "ROTATE", ct.card, kBlue, ct.text, 2);

    if (!portrait) {
        const int theme_btn_x = rot_btn_x - 116;
        draw_pf_button(theme_btn_x, 10, 106, 36, state.dark_ui ? "DARK" : "LIGHT", ct.card, ct.border, ct.text, 2);

        if (state.battery_valid) {
            const int bat_x = theme_btn_x - 102;
            draw_power_glyph(bat_x, 20, state.battery_charging);
            char bat_s[16];
            std::snprintf(bat_s, sizeof(bat_s), "%u%%", static_cast<unsigned>(state.battery_percent));
            draw_pf_text(cyd_fonts::pixelfont_11x5_thin, bat_x + 20, 17, bat_s, ct.text, 2);
        }
        if (state.clock.valid) {
            char clk_s[16];
            std::snprintf(clk_s, sizeof(clk_s), "%02d:%02d", state.clock.hour, state.clock.minute);
            draw_pf_text(cyd_fonts::pixelfont_13x7_semibold, rot_btn_x - 310, 16, clk_s, ct.text, 2);
        }
    }

    // Location & Filter Row (y = 66..104)
    const int tab_y = 66;
    draw_pf_button(16, tab_y, 196, 38, "ALL HTML ON SD",
                   state.picker_tab == PickerTab::all_html ? kBlue : ct.card,
                   kBlue,
                   state.picker_tab == PickerTab::all_html ? 0xFFFFFF : ct.text, 2);
    draw_pf_button(220, tab_y, 148, 38, "SD ROOT (/)",
                   (state.picker_tab == PickerTab::browse_dir && std::strcmp(state.picker_dir, "/") == 0) ? kBlue : ct.card,
                   ct.border,
                   (state.picker_tab == PickerTab::browse_dir && std::strcmp(state.picker_dir, "/") == 0) ? 0xFFFFFF : ct.text, 2);
    draw_pf_button(376, tab_y, 114, 38, "/files",
                   (state.picker_tab == PickerTab::browse_dir && std::strcmp(state.picker_dir, "/files") == 0) ? kBlue : ct.card,
                   ct.border,
                   (state.picker_tab == PickerTab::browse_dir && std::strcmp(state.picker_dir, "/files") == 0) ? 0xFFFFFF : ct.text, 2);
    draw_pf_button(498, tab_y, 112, 38, "UP DIR", ct.card, ct.border, ct.text, 2);

    if (!portrait) {
        draw_pf_button(618, tab_y, 214, 38,
                       state.picker_html_only ? "FILTER: HTML ONLY" : "FILTER: ALL FILES",
                       ct.card, kGreen, ct.text, 2);
        draw_pf_button(840, tab_y, 120, 38, "REFRESH", ct.card, ct.border, ct.text, 2);
    }

    // Path Banner (y = 112..146)
    box(16, 112, sw - 32, 34, ct.surface, ct.border, 2);
    char banner[240];
    if (state.picker_tab == PickerTab::all_html) {
        std::snprintf(banner, sizeof(banner), "CHOOSE AN HTML FILE FROM SD CARD (%u FOUND)",
                      static_cast<unsigned>(state.picker_count));
    } else {
        std::snprintf(banner, sizeof(banner), "SD DIRECTORY: %s (%u ITEMS)",
                      state.picker_dir, static_cast<unsigned>(state.picker_count));
    }
    draw_pf_text(cyd_fonts::pixelfont_11x5_thin, 28, 118, banner, kBlue, 2, sw - 56);

    // File Rows (y = 154 .. sh - 16)
    const int list_top = 154;
    const int list_bot = sh - 16;
    const int row_h = 56;
    const int vis_rows = std::max(1, (list_bot - list_top) / row_h);

    if (state.picker_count == 0) {
        draw_pf_text(cyd_fonts::pixelfont_13x7_semibold, 28, list_top + 36,
                     state.sd_mounted ? "NO HTML FILES FOUND IN THIS LOCATION" : "SD CARD NOT MOUNTED",
                     ct.muted, 2);
    } else {
        const size_t start_idx = static_cast<size_t>(std::clamp(state.picker_scroll_offset, 0,
                                                                std::max(0, static_cast<int>(state.picker_count) - 1)));
        for (int r = 0; r < vis_rows; ++r) {
            const size_t idx = start_idx + static_cast<size_t>(r);
            if (idx >= state.picker_count) break;
            const PickerEntry& pe = state.picker_entries[idx];
            const int ry = list_top + r * row_h;
            const bool sel = (idx == state.picker_selected);

            box(16, ry, sw - 32, row_h - 4,
                sel ? (state.dark_ui ? 0x1E2F4D : 0xDBEAFE) : ct.card,
                sel ? kBlue : ct.border, 2);

            const uint32_t badge_col = pe.is_dir ? kAmber : (pe.is_html ? kGreen : ct.muted);
            const char* badge_txt = pe.is_dir ? "DIR" : (pe.is_html ? "HTML" : "FILE");
            box(26, ry + 12, 70, 28, badge_col, badge_col, 1);
            draw_pf_text(cyd_fonts::pixelfont_11x5_thin, 34, ry + 15, badge_txt, 0xFFFFFF, 2);

            draw_pf_text(cyd_fonts::pixelfont_7x5_thin, 108, ry + 10,
                         pe.name, ct.text, 4, sw - 280);

            if (!pe.is_dir) {
                char sz_s[24];
                format_file_size(pe.size, sz_s, sizeof(sz_s));
                const int sw_txt = pf_text_width(cyd_fonts::pixelfont_11x5_thin, sz_s, 2);
                draw_pf_text(cyd_fonts::pixelfont_11x5_thin, sw - 32 - sw_txt, ry + 15,
                             sz_s, ct.muted, 2);
            }
        }
    }
}

// ============================================================================
// Screen 2: Clean HTML Reader Screen (ONLY Back & Rotate buttons in top bar,
// zero UI overlaid on the HTML, no sidebar, no bottom bar)
// ============================================================================
void draw_reader_top_bar(const ViewerState& state) noexcept {
    const ChromeTheme& ct = get_chrome(state.dark_ui);
    const int sw = tab5_display_width();

    rect(0, 0, sw, kReaderTopBarH, ct.surface);
    rect(0, kReaderTopBarH - 2, sw, 2, ct.border);

    // Left: BACK button
    draw_pf_button(12, 6, 104, 34, "BACK", ct.card, kBlue, ct.text, 2);

    // Center: Document title or path + zoom indicator
    char center_lbl[160];
    const char* title = state.doc.title[0] ? state.doc.title : state.current_file_rel;
    if (state.zoom_pct != 100) {
        std::snprintf(center_lbl, sizeof(center_lbl), "%.96s (%u%%)",
                      title, static_cast<unsigned>(state.zoom_pct));
    } else {
        std::snprintf(center_lbl, sizeof(center_lbl), "%.120s", title);
    }
    const int avail_w = std::max(80, sw - 270);
    const int tw = std::min(avail_w, pf_text_width(cyd_fonts::pixelfont_11x5_thin, center_lbl, 2));
    const int tx = 128 + std::max(0, (avail_w - tw) / 2);
    draw_pf_text(cyd_fonts::pixelfont_11x5_thin, tx, 12, center_lbl, ct.muted, 2, avail_w);

    // Right: ROTATE button
    draw_pf_button(sw - 124, 6, 112, 34, "ROTATE", ct.card, kBlue, ct.text, 2);
}

void draw_source_viewport(const ViewerState& state, int vp_top, int vp_bot) noexcept {
    const ChromeTheme& ct = get_chrome(state.dark_ui);
    const int sw = tab5_display_width();
    const int vp_h = vp_bot - vp_top;

    const uint32_t bg24 = state.dark_ui ? 0x0D1117 : 0xF8FAFC;
    const uint32_t gutter24 = state.dark_ui ? 0x161B22 : 0xE2E8F0;
    const uint16_t bg565 = rgb24_to_565(bg24);
    const uint16_t gutter565 = rgb24_to_565(gutter24);

    rect(0, vp_top, sw, vp_h, bg24);
    rect(0, vp_top, 64, vp_h, gutter24);
    rect(64, vp_top, 2, vp_h, ct.border);

    const uint8_t src_px = scale_font_px(15, state.zoom_pct);
    const FontMetrics fm = font_get_metrics(FontFamily::courier_new, src_px, false, false);

    const int first_line = std::max(0, state.scroll_y / fm.line_height_px);
    const int max_vis_lines = vp_h / fm.line_height_px + 2;

    int cur_line = 0;
    size_t pos = 0;
    while (pos < state.raw_html_len && cur_line <= first_line + max_vis_lines) {
        size_t eol = pos;
        while (eol < state.raw_html_len && state.raw_html[eol] != '\n') ++eol;

        if (cur_line >= first_line) {
            const int py = vp_top + cur_line * fm.line_height_px - state.scroll_y;
            if (py + fm.line_height_px > vp_top && py < vp_bot) {
                char lnum[16];
                std::snprintf(lnum, sizeof(lnum), "%4d", cur_line + 1);
                font_draw_utf8(FontFamily::courier_new, src_px, false, false,
                               8, py, lnum, std::strlen(lnum),
                               rgb24_to_565(ct.muted), gutter565, vp_top, vp_bot);

                size_t lp = pos;
                int lx = 76;
                while (lp < eol && lx < sw - 12) {
                    if (state.raw_html[lp] == '<') {
                        size_t gt = lp + 1;
                        while (gt < eol && state.raw_html[gt] != '>') ++gt;
                        if (gt < eol) ++gt;
                        const bool is_comment = (lp + 3 < eol && state.raw_html[lp + 1] == '!' &&
                                                 state.raw_html[lp + 2] == '-');
                        const uint16_t col = is_comment
                                                 ? rgb24_to_565(0x6B7280)
                                                 : (state.dark_ui ? rgb24_to_565(0x60A5FA) : rgb24_to_565(0x1D4ED8));
                        lx += font_draw_utf8(FontFamily::courier_new, src_px, !is_comment, is_comment,
                                             lx, py, state.raw_html + lp, gt - lp,
                                             col, bg565, vp_top, vp_bot);
                        lp = gt;
                    } else {
                        size_t next_lt = lp;
                        while (next_lt < eol && state.raw_html[next_lt] != '<') ++next_lt;
                        const uint16_t col = state.dark_ui ? rgb24_to_565(0xE5E7EB) : rgb24_to_565(0x1F2937);
                        lx += font_draw_utf8(FontFamily::courier_new, src_px, false, false,
                                             lx, py, state.raw_html + lp, next_lt - lp,
                                             col, bg565, vp_top, vp_bot);
                        lp = next_lt;
                    }
                }
            }
        }

        pos = (eol < state.raw_html_len) ? (eol + 1) : state.raw_html_len;
        ++cur_line;
    }
}

}  // namespace

void ui_render_viewport(ViewerState& state) noexcept {
    if (state.screen == ViewScreen::file_picker) {
        draw_file_picker_screen(state);
        return;
    }

    const int sw = tab5_display_width();
    const int sh = tab5_display_height();
    if (state.layout_dirty || s_last_layout_sw != sw) {
        viewer_relayout(state);
    }

    const int vp_top = kReaderTopBarH;
    const int vp_bot = sh;
    const int vp_h = vp_bot - vp_top;

    if (state.source_view) {
        draw_source_viewport(state, vp_top, vp_bot);
        return;
    }

    const bool force_dark_bg = (state.dark_ui && !state.doc.has_doc_bg);
    const uint16_t page_bg_565 = force_dark_bg ? rgb24_to_565(0x12161B) : state.doc.doc_bg_rgb565;
    const uint32_t page_bg_24 = rgb565_to_24(page_bg_565);

    rect(0, vp_top, sw, vp_h, page_bg_24);

    // Draw visible LayoutBoxes
    for (size_t i = 0; i < s_box_count; ++i) {
        const LayoutBox& b = s_boxes[i];
        if (b.y + b.h < state.scroll_y || b.y > state.scroll_y + vp_h) continue;
        int sy = vp_top + b.y - state.scroll_y;
        int sh_box = b.h;
        if (sy < vp_top) {
            sh_box -= (vp_top - sy);
            sy = vp_top;
        }
        if (sy + sh_box > vp_bot) {
            sh_box = vp_bot - sy;
        }
        if (sh_box <= 0) continue;

        if (b.has_fill) {
            rect(b.x, sy, b.w, sh_box, rgb565_to_24(b.fill_rgb565));
        }
        if (b.has_border_box) {
            const uint32_t bc = rgb565_to_24(b.border_box_rgb565);
            rect(b.x, sy, b.w, 1, bc);
            rect(b.x, sy + sh_box - 1, b.w, 1, bc);
            rect(b.x, sy, 1, sh_box, bc);
            rect(b.x + b.w - 1, sy, 1, sh_box, bc);
        }
        if (b.has_border_left) {
            rect(b.x, sy, 5, sh_box, rgb565_to_24(b.border_left_rgb565));
        }
    }

    // Draw visible LayoutRuns with anti-aliased TrueType fonts
    for (size_t i = 0; i < s_run_count; ++i) {
        const LayoutRun& r = s_runs[i];
        if (r.y + r.h < state.scroll_y || r.y > state.scroll_y + vp_h) continue;
        const int sy = vp_top + r.y - state.scroll_y;

        uint16_t run_bg = page_bg_565;
        if (r.has_bg) {
            run_bg = r.bg_rgb565;
            int by = std::max(vp_top, sy);
            int bh = std::min(vp_bot, sy + r.h) - by;
            if (bh > 0) {
                rect(r.x - 2, by, r.w + 4, bh, rgb565_to_24(run_bg));
            }
        }

        font_draw_utf8(r.family, r.size_px, r.bold, r.italic,
                       r.x, sy,
                       state.doc.text_pool + r.text_offset, r.text_len,
                       r.fg_rgb565, run_bg, vp_top, vp_bot);

        if (r.underline) {
            const int uy = sy + r.h - 3;
            if (uy >= vp_top && uy < vp_bot) {
                rect(r.x, uy, r.w, 1, rgb565_to_24(r.fg_rgb565));
            }
        }
        if (r.strikethrough) {
            const int sty = sy + r.h / 2;
            if (sty >= vp_top && sty < vp_bot) {
                rect(r.x, sty, r.w, 1, rgb565_to_24(r.fg_rgb565));
            }
        }
    }
}

void ui_render_header_and_footer(const ViewerState& state) noexcept {
    if (state.screen == ViewScreen::file_picker) {
        draw_file_picker_screen(state);
    } else {
        draw_reader_top_bar(state);
    }
}

void ui_render_full(ViewerState& state) noexcept {
    if (state.screen == ViewScreen::file_picker) {
        draw_file_picker_screen(state);
    } else {
        draw_reader_top_bar(state);
        ui_render_viewport(state);
    }
}

namespace {

void navigate_up_directory(ViewerState& state) noexcept {
    state.picker_tab = PickerTab::browse_dir;
    if (std::strcmp(state.picker_dir, "/") != 0) {
        char* last_slash = std::strrchr(state.picker_dir, '/');
        if (last_slash == state.picker_dir || !last_slash) {
            std::snprintf(state.picker_dir, sizeof(state.picker_dir), "/");
        } else {
            *last_slash = '\0';
        }
    }
    viewer_refresh_picker(state);
}

void activate_picker_entry(ViewerState& state, size_t idx) noexcept {
    if (idx >= state.picker_count) return;
    const PickerEntry pe = state.picker_entries[idx];
    if (pe.is_dir) {
        state.picker_tab = PickerTab::browse_dir;
        std::snprintf(state.picker_dir, sizeof(state.picker_dir), "%s", pe.rel_path);
        viewer_refresh_picker(state);
    } else {
        state.history_count = 0;
        viewer_load_file(state, pe.rel_path, false);
    }
}

void activate_link(ViewerState& state, int16_t link_index) noexcept {
    if (link_index < 0 || static_cast<size_t>(link_index) >= state.doc.link_count) return;
    const char* href = state.doc.links[link_index].href;

    char target_rel[192] = {0};
    char anchor[48] = {0};
    if (!resolve_relative_href(state.current_file_rel, href,
                               target_rel, sizeof(target_rel),
                               anchor, sizeof(anchor))) {
        return;
    }

    if (target_rel[0] && std::strcmp(target_rel, state.current_file_rel) != 0) {
        if (!viewer_load_file(state, target_rel, true)) {
            return;
        }
        viewer_relayout(state);
    }

    if (anchor[0]) {
        for (size_t bi = 0; bi < state.doc.block_count; ++bi) {
            if (std::strcmp(state.doc.blocks[bi].anchor_id, anchor) == 0) {
                state.scroll_y = std::clamp(s_block_y[bi] - 12, 0, state.max_scroll_y);
                return;
            }
        }
    }
}

}  // namespace

UiCommand ui_handle_tap(ViewerState& state, int x, int y, bool& out_needs_full_redraw, bool& out_needs_viewport_redraw) noexcept {
    out_needs_full_redraw = false;
    out_needs_viewport_redraw = false;

    const int sw = tab5_display_width();
    const int sh = tab5_display_height();
    const bool portrait = (sw < 900);

    // 1. Full-Screen File Picker Mode
    if (state.screen == ViewScreen::file_picker) {
        const int console_btn_x = sw - 130;
        const int rot_btn_x = console_btn_x - 126;

        if (within(x, y, console_btn_x, 10, 114, 36)) {
            return UiCommand::return_to_console;
        }
        if (within(x, y, rot_btn_x, 10, 116, 36)) {
            return UiCommand::rotate_step;
        }
        if (!portrait && within(x, y, rot_btn_x - 116, 10, 106, 36)) {
            state.dark_ui = !state.dark_ui;
            state.layout_dirty = true;
            out_needs_full_redraw = true;
            return UiCommand::none;
        }

        const int tab_y = 66;
        if (within(x, y, 16, tab_y, 196, 38)) {
            state.picker_tab = PickerTab::all_html;
            viewer_refresh_picker(state);
            out_needs_full_redraw = true;
            return UiCommand::none;
        }
        if (within(x, y, 220, tab_y, 148, 38)) {
            state.picker_tab = PickerTab::browse_dir;
            std::snprintf(state.picker_dir, sizeof(state.picker_dir), "/");
            viewer_refresh_picker(state);
            out_needs_full_redraw = true;
            return UiCommand::none;
        }
        if (within(x, y, 376, tab_y, 114, 38)) {
            state.picker_tab = PickerTab::browse_dir;
            std::snprintf(state.picker_dir, sizeof(state.picker_dir), "/files");
            viewer_refresh_picker(state);
            out_needs_full_redraw = true;
            return UiCommand::none;
        }
        if (within(x, y, 498, tab_y, 112, 38)) {
            navigate_up_directory(state);
            out_needs_full_redraw = true;
            return UiCommand::none;
        }
        if (!portrait) {
            if (within(x, y, 618, tab_y, 214, 38)) {
                state.picker_html_only = !state.picker_html_only;
                state.picker_tab = PickerTab::browse_dir;
                viewer_refresh_picker(state);
                out_needs_full_redraw = true;
                return UiCommand::none;
            }
            if (within(x, y, 840, tab_y, 120, 38)) {
                viewer_refresh_picker(state);
                out_needs_full_redraw = true;
                return UiCommand::none;
            }
        }

        // File list rows
        const int list_top = 154;
        const int list_bot = sh - 16;
        const int row_h = 56;
        if (within(x, y, 16, list_top, sw - 32, list_bot - list_top)) {
            const int r = (y - list_top) / row_h;
            const size_t idx = static_cast<size_t>(std::max(0, state.picker_scroll_offset)) + static_cast<size_t>(r);
            if (idx < state.picker_count) {
                state.picker_selected = idx;
                activate_picker_entry(state, idx);
                out_needs_full_redraw = true;
            }
        }
        return UiCommand::none;
    }

    // 2. HTML Reader Mode (Only BACK and ROTATE buttons in top bar)
    if (y < kReaderTopBarH) {
        if (within(x, y, 12, 6, 104, 34)) {
            if (state.history_count > 0) {
                const HistoryEntry prev = state.history[--state.history_count];
                if (viewer_load_file(state, prev.rel_path, false)) {
                    viewer_relayout(state);
                    state.scroll_y = std::clamp(prev.scroll_y, 0, state.max_scroll_y);
                }
            } else {
                state.screen = ViewScreen::file_picker;
            }
            out_needs_full_redraw = true;
            return UiCommand::none;
        }
        if (within(x, y, sw - 124, 6, 112, 34)) {
            return UiCommand::rotate_step;
        }
        return UiCommand::none;
    }

    // 3. Tapping a link inside the HTML document
    if (!state.source_view && y >= kReaderTopBarH) {
        const int doc_y = y - kReaderTopBarH + state.scroll_y;
        for (size_t i = 0; i < s_run_count; ++i) {
            const LayoutRun& r = s_runs[i];
            if (r.link_index >= 0 &&
                x >= r.x - 4 && x <= r.x + r.w + 4 &&
                doc_y >= r.y - 4 && doc_y <= r.y + r.h + 4) {
                activate_link(state, r.link_index);
                out_needs_full_redraw = true;
                return UiCommand::none;
            }
        }
    }

    return UiCommand::none;
}

void ui_handle_scroll_delta(ViewerState& state, int dy, bool& out_needs_viewport_redraw) noexcept {
    if (dy == 0) return;

    if (state.screen == ViewScreen::file_picker) {
        // Scroll file list by rows (every ~28 px of finger drag = 1 row)
        const int row_step = dy / 28;
        if (row_step != 0 && state.picker_count > 0) {
            const int max_off = std::max(0, static_cast<int>(state.picker_count) - 4);
            const int next_off = std::clamp(state.picker_scroll_offset + row_step, 0, max_off);
            if (next_off != state.picker_scroll_offset) {
                state.picker_scroll_offset = next_off;
                out_needs_viewport_redraw = true;
            }
        }
        return;
    }

    const int next_scroll = std::clamp(state.scroll_y + dy, 0, state.max_scroll_y);
    if (next_scroll != state.scroll_y) {
        state.scroll_y = next_scroll;
        out_needs_viewport_redraw = true;
    }
}

void ui_handle_pinch_zoom(ViewerState& state, uint16_t new_zoom_pct, int focal_screen_y, int dy_pan, bool& out_needs_viewport_redraw) noexcept {
    if (state.screen != ViewScreen::html_reader) return;
    new_zoom_pct = static_cast<uint16_t>(std::clamp<int>(new_zoom_pct, 50, 250));

    bool changed = false;
    if (std::abs(static_cast<int>(new_zoom_pct) - static_cast<int>(state.zoom_pct)) >= 5) {
        const int old_total = std::max(1, state.total_doc_height);
        const int focal_rel_y = std::max(0, focal_screen_y - kReaderTopBarH);
        const int doc_focal_y = state.scroll_y + focal_rel_y;

        state.zoom_pct = new_zoom_pct;
        viewer_relayout(state);

        // Keep the focal point under the two fingers anchored after zoom relayout
        const int new_doc_focal_y = static_cast<int>(
            (static_cast<int64_t>(doc_focal_y) * state.total_doc_height) / old_total);
        state.scroll_y = std::clamp(new_doc_focal_y - focal_rel_y, 0, state.max_scroll_y);
        changed = true;
    }

    if (dy_pan != 0) {
        const int next_scroll = std::clamp(state.scroll_y + dy_pan, 0, state.max_scroll_y);
        if (next_scroll != state.scroll_y) {
            state.scroll_y = next_scroll;
            changed = true;
        }
    }

    if (changed) {
        out_needs_viewport_redraw = true;
    }
}

UiCommand ui_handle_key(ViewerState& state, const uint8_t* bytes, size_t len, bool& out_needs_full_redraw, bool& out_needs_viewport_redraw) noexcept {
    out_needs_full_redraw = false;
    out_needs_viewport_redraw = false;
    if (!bytes || len == 0) return UiCommand::none;

    const int sh = tab5_display_height();
    const int vp_h = sh - kReaderTopBarH;

    if (len >= 3 && bytes[0] == 0x1B && bytes[1] == '[') {
        const char code = static_cast<char>(bytes[2]);
        if (state.screen == ViewScreen::file_picker) {
            const int vis_rows = std::max(1, (sh - 16 - 154) / 56);
            if ((code == 'A' || code == 'D') && state.picker_selected > 0) {
                --state.picker_selected;
                if (static_cast<int>(state.picker_selected) < state.picker_scroll_offset) {
                    state.picker_scroll_offset = static_cast<int>(state.picker_selected);
                }
                out_needs_full_redraw = true;
            } else if ((code == 'B' || code == 'C') && state.picker_selected + 1 < state.picker_count) {
                ++state.picker_selected;
                if (static_cast<int>(state.picker_selected) >= state.picker_scroll_offset + vis_rows) {
                    state.picker_scroll_offset = static_cast<int>(state.picker_selected) - vis_rows + 1;
                }
                out_needs_full_redraw = true;
            }
            return UiCommand::none;
        }
        if (code == 'A') {
            state.scroll_y = std::clamp(state.scroll_y - 48, 0, state.max_scroll_y);
            out_needs_viewport_redraw = true;
        } else if (code == 'B') {
            state.scroll_y = std::clamp(state.scroll_y + 48, 0, state.max_scroll_y);
            out_needs_viewport_redraw = true;
        } else if (code == '5') {
            state.scroll_y = std::clamp(state.scroll_y - (vp_h - 60), 0, state.max_scroll_y);
            out_needs_viewport_redraw = true;
        } else if (code == '6') {
            state.scroll_y = std::clamp(state.scroll_y + (vp_h - 60), 0, state.max_scroll_y);
            out_needs_viewport_redraw = true;
        } else if (code == 'H') {
            state.scroll_y = 0;
            out_needs_viewport_redraw = true;
        } else if (code == 'F') {
            state.scroll_y = state.max_scroll_y;
            out_needs_viewport_redraw = true;
        }
        return UiCommand::none;
    }

    const uint8_t b = bytes[0];
    if (b == 0x11) {  // Ctrl+Q -> Console
        return UiCommand::return_to_console;
    }
    if (b == 0x12 || b == 'r' || b == 'R') {  // Ctrl+R or R -> Rotate
        return UiCommand::rotate_step;
    }
    if (b == 0x15) {  // Ctrl+U -> Toggle Raw Source view
        if (state.screen == ViewScreen::html_reader) {
            state.source_view = !state.source_view;
            state.scroll_y = 0;
            state.layout_dirty = true;
            out_needs_full_redraw = true;
        }
        return UiCommand::none;
    }
    if (b == 0x1B || b == 0x08 || b == 0x7F) {  // Esc / Backspace -> Back
        if (state.screen == ViewScreen::html_reader) {
            if (state.history_count > 0) {
                const HistoryEntry prev = state.history[--state.history_count];
                if (viewer_load_file(state, prev.rel_path, false)) {
                    viewer_relayout(state);
                    state.scroll_y = std::clamp(prev.scroll_y, 0, state.max_scroll_y);
                }
            } else {
                state.screen = ViewScreen::file_picker;
            }
            out_needs_full_redraw = true;
        } else if (b != 0x1B) {
            navigate_up_directory(state);
            out_needs_full_redraw = true;
        }
        return UiCommand::none;
    }

    if (state.screen == ViewScreen::file_picker) {
        if (b == '\r' || b == '\n') {
            activate_picker_entry(state, state.picker_selected);
            out_needs_full_redraw = true;
        }
        return UiCommand::none;
    }

    if (b == ' ' || b == 'j') {
        state.scroll_y = std::clamp(state.scroll_y + (vp_h - 60), 0, state.max_scroll_y);
        out_needs_viewport_redraw = true;
    } else if (b == 'k') {
        state.scroll_y = std::clamp(state.scroll_y - (vp_h - 60), 0, state.max_scroll_y);
        out_needs_viewport_redraw = true;
    } else if (b == '+' || b == '=') {
        ui_handle_pinch_zoom(state, static_cast<uint16_t>(state.zoom_pct + 15), sh / 2, 0, out_needs_viewport_redraw);
        out_needs_full_redraw = out_needs_viewport_redraw;
    } else if (b == '-') {
        ui_handle_pinch_zoom(state, static_cast<uint16_t>(state.zoom_pct > 65 ? state.zoom_pct - 15 : 50), sh / 2, 0, out_needs_viewport_redraw);
        out_needs_full_redraw = out_needs_viewport_redraw;
    } else if (b == '0') {
        ui_handle_pinch_zoom(state, 100, sh / 2, 0, out_needs_viewport_redraw);
        out_needs_full_redraw = out_needs_viewport_redraw;
    }
    return UiCommand::none;
}

}  // namespace tab5_html

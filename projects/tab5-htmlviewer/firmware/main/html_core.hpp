#pragma once

#include <cstddef>
#include <cstdint>

namespace tab5_html {

enum class FontFamily : uint8_t {
    montserrat = 0,       // Default sans-serif & unrecognised sans-serif
    times_new_roman = 1,  // Default serif & unrecognised serif
    georgia = 2,          // Document-specified Georgia
    arial = 3,            // Document-specified Arial / Helvetica
    verdana = 4,          // Document-specified Verdana / Tahoma / Trebuchet MS
    courier_new = 5,      // Document-specified Courier New / Consolas / monospace / code / pre
};

enum class FontOverride : uint8_t {
    doc_css = 0,          // Honour fonts specified in the HTML document
    force_montserrat = 1,
    force_times = 2,
    force_courier = 3,
};

enum class TextAlign : uint8_t {
    left = 0,
    center = 1,
    right = 2,
};

enum class BlockType : uint8_t {
    paragraph = 0,
    heading = 1,
    list_item = 2,
    blockquote = 3,
    code_block = 4,
    table_row = 5,
    hr = 6,
};

struct PickerEntry {
    char name[128];
    char rel_path[192];  // Relative to SD root, e.g. "/files/report.html"
    bool is_dir;
    bool is_html;
    uint64_t size;
};

struct HtmlLink {
    char href[192];
};

struct HtmlOutlineItem {
    uint8_t level;       // 1..6
    uint16_t block_idx;
    char title[80];
};

struct HtmlSpan {
    uint32_t text_offset;
    uint16_t text_len;
    FontFamily family;
    uint8_t size_px;
    bool bold : 1;
    bool italic : 1;
    bool underline : 1;
    bool strikethrough : 1;
    bool uppercase : 1;
    bool inline_code : 1;
    bool has_bg : 1;
    uint16_t fg_rgb565;
    uint16_t bg_rgb565;
    int16_t link_index;  // -1 if none
};

constexpr size_t kMaxTableCols = 6;

struct HtmlTableCell {
    uint16_t span_start;
    uint16_t span_count;
    bool is_header;
    TextAlign align;
    bool has_bg;
    uint16_t bg_rgb565;
};

struct HtmlBlock {
    BlockType type;
    uint8_t heading_level;     // 1..6 for heading
    uint8_t list_depth;        // 0..4 for list_item
    uint16_t ordered_index;    // 0 for unordered bullet, >=1 for ordered list
    TextAlign align;
    bool has_bg;
    bool has_border_left;
    bool has_border_box;
    uint16_t bg_rgb565;
    uint16_t border_left_rgb565;
    uint16_t border_box_rgb565;
    uint8_t margin_top_px;
    uint8_t margin_bottom_px;
    uint8_t padding_px;
    uint16_t span_start;
    uint16_t span_count;
    uint8_t cell_count;
    HtmlTableCell cells[kMaxTableCols];
    char anchor_id[48];
};

constexpr size_t kMaxTextPool = 131072;
constexpr size_t kMaxSpans = 2048;
constexpr size_t kMaxBlocks = 768;
constexpr size_t kMaxLinks = 256;
constexpr size_t kMaxOutline = 96;

struct HtmlDocument {
    char title[128];
    FontFamily base_family;
    uint8_t base_size_px;
    bool has_doc_bg;
    bool has_doc_fg;
    uint16_t doc_bg_rgb565;
    uint16_t doc_fg_rgb565;

    char* text_pool;
    size_t text_len;
    size_t text_cap;

    HtmlSpan* spans;
    size_t span_count;
    size_t span_cap;

    HtmlBlock* blocks;
    size_t block_count;
    size_t block_cap;

    HtmlLink* links;
    size_t link_count;
    size_t link_cap;

    HtmlOutlineItem* outline;
    size_t outline_count;
    size_t outline_cap;
};

// Allocate / free / reset an HtmlDocument (uses SPIRAM on ESP32-P4 when available)
bool doc_alloc(HtmlDocument* doc) noexcept;
void doc_free(HtmlDocument* doc) noexcept;
void doc_clear(HtmlDocument* doc) noexcept;

// Font family resolution according to HTML/CSS specification:
// - Specific installed fonts (Montserrat, Times New Roman, Georgia, Arial/Helvetica, Verdana/Tahoma, Courier New/Consolas)
// - Generic or unrecognised sans-serif -> FontFamily::montserrat
// - Generic or unrecognised serif -> FontFamily::times_new_roman
// - Generic or unrecognised monospace -> FontFamily::courier_new
FontFamily resolve_font_family(const char* css_font_family, FontFamily fallback = FontFamily::montserrat) noexcept;
const char* font_family_name(FontFamily family) noexcept;

// Parse CSS color (#rgb, #rrggbb, rgb(), rgba(), or named color) into RGB565
bool parse_css_color(const char* value, uint16_t& out_rgb565) noexcept;

// Decode HTML entities (&amp;, &mdash;, &#160;, &#x2014;, etc.) into UTF-8
size_t decode_html_entities(const char* src, size_t src_len, char* dst, size_t dst_cap) noexcept;

// Resolve a relative path or href (e.g. "../sub/page.html#sec1") against current_file_rel ("/docs/index.html")
// Outputs normalized SD-relative path ("/sub/page.html") and optional anchor ("sec1")
bool resolve_relative_href(const char* current_file_rel,
                           const char* href,
                           char* out_rel_path,
                           size_t out_rel_cap,
                           char* out_anchor,
                           size_t out_anchor_cap) noexcept;

// Sanitize a relative SD path ("/files/page.html") into VFS path ("/sdcard/files/page.html")
bool resolve_sd_vfs_path(const char* sd_root,
                         const char* rel_path,
                         char* out_vfs,
                         size_t out_vfs_cap,
                         char* out_clean_rel,
                         size_t out_clean_cap) noexcept;

// Check if filename ends with .html or .htm (case-insensitive)
bool is_html_filename(const char* name) noexcept;

// List directory entries for the SD card file picker (folders + HTML files, or all files)
bool list_picker_directory(const char* sd_root,
                           const char* rel_dir,
                           bool html_only,
                           PickerEntry* out_entries,
                           size_t max_entries,
                           size_t& out_count) noexcept;

// Recursively scan SD card (up to max_depth) for all .html/.htm files
bool scan_all_html_files(const char* sd_root,
                         PickerEntry* out_entries,
                         size_t max_entries,
                         size_t& out_count,
                         int max_depth = 4) noexcept;

// Format byte count for UI chrome
void format_file_size(uint64_t bytes, char* out, size_t out_cap) noexcept;

// Parse raw HTML string into structured HtmlDocument
bool parse_html_document(const char* html, size_t html_len, HtmlDocument* doc) noexcept;

}  // namespace tab5_html

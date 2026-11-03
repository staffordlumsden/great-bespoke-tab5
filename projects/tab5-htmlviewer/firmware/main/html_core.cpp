#include "html_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace tab5_html {

namespace {

void* alloc_mem(size_t bytes) noexcept {
#ifdef ESP_PLATFORM
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
#endif
    return std::malloc(bytes);
}

constexpr uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) noexcept {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

char to_lower_ascii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

int ascii_casecmp(const char* a, const char* b) noexcept {
    while (*a && *b) {
        const char ca = to_lower_ascii(*a);
        const char cb = to_lower_ascii(*b);
        if (ca != cb) return static_cast<unsigned char>(ca) - static_cast<unsigned char>(cb);
        ++a;
        ++b;
    }
    return static_cast<unsigned char>(to_lower_ascii(*a)) -
           static_cast<unsigned char>(to_lower_ascii(*b));
}

bool ascii_contains_ci(const char* haystack, const char* needle) noexcept {
    if (!haystack || !needle || !needle[0]) return false;
    const size_t nlen = std::strlen(needle);
    for (size_t i = 0; haystack[i]; ++i) {
        bool match = true;
        for (size_t j = 0; j < nlen; ++j) {
            if (!haystack[i + j] || to_lower_ascii(haystack[i + j]) != to_lower_ascii(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

void trim_inplace(char* s) noexcept {
    if (!s) return;
    size_t len = std::strlen(s);
    size_t start = 0;
    while (start < len && (std::isspace(static_cast<unsigned char>(s[start])) ||
                           s[start] == '\'' || s[start] == '"')) {
        ++start;
    }
    while (len > start && (std::isspace(static_cast<unsigned char>(s[len - 1])) ||
                           s[len - 1] == '\'' || s[len - 1] == '"')) {
        --len;
    }
    if (start > 0) {
        std::memmove(s, s + start, len - start);
    }
    s[len - start] = '\0';
}

int hex_nibble(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t encode_utf8(uint32_t cp, char* out) noexcept {
    if (cp < 0x80) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = '?';
    return 1;
}

struct NamedEntity {
    const char* name;
    uint32_t cp;
};

constexpr NamedEntity kNamedEntities[] = {
    {"nbsp", 0x0020},   {"amp", '&'},       {"lt", '<'},        {"gt", '>'},
    {"quot", '"'},      {"apos", '\''},     {"ndash", 0x2013},  {"mdash", 0x2014},
    {"lsquo", 0x2018},  {"rsquo", 0x2019},  {"ldquo", 0x201C},  {"rdquo", 0x201D},
    {"bull", 0x2022},   {"hellip", 0x2026}, {"copy", 0x00A9},   {"reg", 0x00AE},
    {"trade", 0x2122},  {" deg", 0x00B0},   {"deg", 0x00B0},    {"plusmn", 0x00B1},
    {"times", 0x00D7},  {"divide", 0x00F7}, {"euro", 0x20AC},   {"pound", 0x00A3},
    {"yen", 0x00A5},    {"cent", 0x00A2},   {"sect", 0x00A7},   {"para", 0x00B6},
    {"middot", 0x00B7}, {"laquo", 0x00AB},  {"raquo", 0x00BB},  {"larr", 0x2190},
    {"uarr", 0x2191},   {"rarr", 0x2192},   {"darr", 0x2193},   {"harr", 0x2194},
    {"minus", 0x2212},  {"le", 0x2264},     {"ge", 0x2265},     {"ne", 0x2260},
    {"asymp", 0x2248},  {"infin", 0x221E},  {"check", 0x2713},  {"prime", 0x2032},
    {"Prime", 0x2033},  {"dagger", 0x2020}, {"Dagger", 0x2021}, {"frac12", 0x00BD},
    {"frac14", 0x00BC}, {"frac34", 0x00BE}, {"sup2", 0x00B2},   {"sup3", 0x00B3},
};

struct NamedColor {
    const char* name;
    uint8_t r, g, b;
};

constexpr NamedColor kNamedColors[] = {
    {"black", 0, 0, 0},             {"white", 255, 255, 255},
    {"red", 220, 38, 38},           {"darkred", 139, 0, 0},
    {"crimson", 220, 20, 60},       {"firebrick", 178, 34, 34},
    {"maroon", 128, 0, 0},          {"coral", 255, 127, 80},
    {"tomato", 255, 99, 71},        {"orangered", 255, 69, 0},
    {"orange", 245, 158, 11},       {"amber", 217, 119, 6},
    {"gold", 234, 179, 8},          {"goldenrod", 218, 165, 32},
    {"yellow", 250, 204, 21},       {"cornsilk", 255, 248, 220},
    {"ivory", 255, 255, 240},       {"floralwhite", 255, 250, 240},
    {"linen", 250, 240, 230},       {"beige", 245, 245, 220},
    {"wheat", 245, 222, 179},       {"tan", 210, 180, 140},
    {"brown", 165, 42, 42},         {"saddlebrown", 139, 69, 19},
    {"sienna", 160, 82, 45},        {"chocolate", 210, 105, 30},
    {"green", 22, 163, 74},         {"darkgreen", 0, 100, 0},
    {"forestgreen", 34, 139, 34},   {"seagreen", 46, 139, 87},
    {"limegreen", 50, 205, 50},     {"olive", 128, 128, 0},
    {"teal", 13, 148, 136},         {"darkcyan", 0, 139, 139},
    {"cyan", 6, 182, 212},          {"aqua", 0, 255, 255},
    {"lightcyan", 224, 255, 255},   {"turquoise", 64, 224, 208},
    {"blue", 37, 99, 235},          {"darkblue", 0, 0, 139},
    {"navy", 0, 0, 128},            {"midnightblue", 25, 25, 112},
    {"royalblue", 65, 105, 225},    {"steelblue", 70, 130, 180},
    {"dodgerblue", 30, 144, 255},   {"deepskyblue", 0, 191, 255},
    {"cornflowerblue", 100, 149, 237}, {"skyblue", 135, 206, 235},
    {"lightblue", 173, 216, 230},   {"aliceblue", 240, 248, 255},
    {"indigo", 75, 0, 130},         {"purple", 128, 0, 128},
    {"rebeccapurple", 102, 51, 153},{"slateblue", 106, 90, 205},
    {"darkslateblue", 72, 61, 139}, {"magenta", 255, 0, 255},
    {"fuchsia", 255, 0, 255},       {"deeppink", 255, 20, 147},
    {"pink", 255, 192, 203},        {"lavender", 230, 230, 250},
    {"gray", 128, 128, 128},        {"grey", 128, 128, 128},
    {"darkgray", 169, 169, 169},    {"darkgrey", 169, 169, 169},
    {"dimgray", 105, 105, 105},     {"dimgrey", 105, 105, 105},
    {"lightgray", 211, 211, 211},   {"lightgrey", 211, 211, 211},
    {"silver", 192, 192, 192},      {"gainsboro", 220, 220, 220},
    {"whitesmoke", 245, 245, 245},  {"snow", 255, 250, 250},
    {"ghostwhite", 248, 248, 255},  {"mintcream", 245, 255, 250},
    {"slategray", 112, 128, 144},   {"slategrey", 112, 128, 144},
    {"darkslategray", 47, 79, 79},  {"darkslategrey", 47, 79, 79},
};

}  // namespace

bool doc_alloc(HtmlDocument* doc) noexcept {
    if (!doc) return false;
    std::memset(doc, 0, sizeof(*doc));
    doc->base_family = FontFamily::montserrat;
    doc->base_size_px = 18;
    doc->doc_bg_rgb565 = 0xFFFF;
    doc->doc_fg_rgb565 = pack_rgb565(24, 28, 34);

    doc->text_pool = static_cast<char*>(alloc_mem(kMaxTextPool));
    doc->spans = static_cast<HtmlSpan*>(alloc_mem(sizeof(HtmlSpan) * kMaxSpans));
    doc->blocks = static_cast<HtmlBlock*>(alloc_mem(sizeof(HtmlBlock) * kMaxBlocks));
    doc->links = static_cast<HtmlLink*>(alloc_mem(sizeof(HtmlLink) * kMaxLinks));
    doc->outline = static_cast<HtmlOutlineItem*>(alloc_mem(sizeof(HtmlOutlineItem) * kMaxOutline));

    if (!doc->text_pool || !doc->spans || !doc->blocks || !doc->links || !doc->outline) {
        doc_free(doc);
        return false;
    }
    doc->text_cap = kMaxTextPool;
    doc->span_cap = kMaxSpans;
    doc->block_cap = kMaxBlocks;
    doc->link_cap = kMaxLinks;
    doc->outline_cap = kMaxOutline;
    doc_clear(doc);
    return true;
}

void doc_free(HtmlDocument* doc) noexcept {
    if (!doc) return;
    std::free(doc->text_pool);
    std::free(doc->spans);
    std::free(doc->blocks);
    std::free(doc->links);
    std::free(doc->outline);
    std::memset(doc, 0, sizeof(*doc));
}

void doc_clear(HtmlDocument* doc) noexcept {
    if (!doc) return;
    std::snprintf(doc->title, sizeof(doc->title), "Untitled Document");
    doc->base_family = FontFamily::montserrat;
    doc->base_size_px = 18;
    doc->has_doc_bg = false;
    doc->has_doc_fg = false;
    doc->doc_bg_rgb565 = 0xFFFF;
    doc->doc_fg_rgb565 = pack_rgb565(24, 28, 34);
    doc->text_len = 0;
    if (doc->text_pool && doc->text_cap > 0) doc->text_pool[0] = '\0';
    doc->span_count = 0;
    doc->block_count = 0;
    doc->link_count = 0;
    doc->outline_count = 0;
}

const char* font_family_name(FontFamily family) noexcept {
    switch (family) {
        case FontFamily::montserrat: return "Montserrat";
        case FontFamily::times_new_roman: return "Times New Roman";
        case FontFamily::georgia: return "Georgia";
        case FontFamily::arial: return "Arial";
        case FontFamily::verdana: return "Verdana";
        case FontFamily::courier_new: return "Courier New";
    }
    return "Montserrat";
}

FontFamily resolve_font_family(const char* css_font_family, FontFamily fallback) noexcept {
    if (!css_font_family || !css_font_family[0]) return fallback;

    // Parse comma-separated font stack in priority order
    const char* p = css_font_family;
    while (*p) {
        const char* comma = std::strchr(p, ',');
        const size_t len = comma ? static_cast<size_t>(comma - p) : std::strlen(p);
        char tok[96];
        const size_t copy_n = std::min(len, sizeof(tok) - 1);
        std::memcpy(tok, p, copy_n);
        tok[copy_n] = '\0';
        trim_inplace(tok);
        for (size_t i = 0; tok[i]; ++i) tok[i] = to_lower_ascii(tok[i]);

        if (tok[0]) {
            // Exact or primary supported families
            if (std::strcmp(tok, "montserrat") == 0) {
                return FontFamily::montserrat;
            }
            if (std::strcmp(tok, "times new roman") == 0 || std::strcmp(tok, "times") == 0) {
                return FontFamily::times_new_roman;
            }
            if (std::strcmp(tok, "georgia") == 0) {
                return FontFamily::georgia;
            }
            if (std::strcmp(tok, "arial") == 0 || std::strcmp(tok, "helvetica") == 0 ||
                std::strcmp(tok, "helvetica neue") == 0 || std::strcmp(tok, "arial narrow") == 0) {
                return FontFamily::arial;
            }
            if (std::strcmp(tok, "verdana") == 0 || std::strcmp(tok, "tahoma") == 0 ||
                std::strcmp(tok, "trebuchet ms") == 0 || std::strcmp(tok, "trebuchet") == 0 ||
                std::strcmp(tok, "geneva") == 0) {
                return FontFamily::verdana;
            }
            if (std::strcmp(tok, "courier new") == 0 || std::strcmp(tok, "courier") == 0 ||
                std::strcmp(tok, "consolas") == 0 || std::strcmp(tok, "monaco") == 0 ||
                std::strcmp(tok, "menlo") == 0 || std::strcmp(tok, "jetbrains mono") == 0 ||
                std::strcmp(tok, "fira code") == 0 || std::strcmp(tok, "sfmono-regular") == 0 ||
                std::strcmp(tok, "ui-monospace") == 0 || std::strcmp(tok, "monospace") == 0 ||
                std::strstr(tok, "mono") != nullptr) {
                return FontFamily::courier_new;
            }
            // Generic or document-specified serif -> Times New Roman
            if (std::strcmp(tok, "serif") == 0 || std::strcmp(tok, "ui-serif") == 0 ||
                std::strstr(tok, "serif") != nullptr ||
                std::strstr(tok, "garamond") != nullptr ||
                std::strstr(tok, "crimson") != nullptr ||
                std::strstr(tok, "cormorant") != nullptr ||
                std::strstr(tok, "baskerville") != nullptr ||
                std::strstr(tok, "palatino") != nullptr ||
                std::strstr(tok, "book antiqua") != nullptr ||
                std::strstr(tok, "cambria") != nullptr ||
                std::strstr(tok, "merriweather") != nullptr ||
                std::strstr(tok, "playfair") != nullptr ||
                std::strstr(tok, "lora") != nullptr ||
                std::strstr(tok, "charter") != nullptr ||
                std::strstr(tok, "minion") != nullptr ||
                std::strstr(tok, "caslon") != nullptr ||
                std::strstr(tok, "bodoni") != nullptr ||
                std::strstr(tok, "didot") != nullptr) {
                if (std::strstr(tok, "sans") == nullptr) {
                    return FontFamily::times_new_roman;
                }
            }
            // Generic or unspecified sans-serif -> Montserrat
            if (std::strcmp(tok, "sans-serif") == 0 || std::strcmp(tok, "sans") == 0 ||
                std::strcmp(tok, "system-ui") == 0 || std::strcmp(tok, "-apple-system") == 0 ||
                std::strcmp(tok, "blinkmacsystemfont") == 0 || std::strcmp(tok, "ui-sans-serif") == 0 ||
                std::strstr(tok, "sans") != nullptr ||
                std::strstr(tok, "segoe") != nullptr ||
                std::strstr(tok, "roboto") != nullptr ||
                std::strcmp(tok, "inter") == 0 ||
                std::strstr(tok, "lato") != nullptr ||
                std::strstr(tok, "nunito") != nullptr ||
                std::strstr(tok, "poppins") != nullptr ||
                std::strstr(tok, "ubuntu") != nullptr ||
                std::strstr(tok, "calibri") != nullptr ||
                std::strstr(tok, "futura") != nullptr ||
                std::strstr(tok, "optima") != nullptr ||
                std::strstr(tok, "avenir") != nullptr ||
                std::strstr(tok, "proxima") != nullptr) {
                return FontFamily::montserrat;
            }
        }
        if (!comma) break;
        p = comma + 1;
    }
    return fallback;
}

bool parse_css_color(const char* value, uint16_t& out_rgb565) noexcept {
    if (!value) return false;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.63s", value);
    // Strip "!important"
    char* imp = std::strstr(buf, "!important");
    if (imp) *imp = '\0';
    trim_inplace(buf);
    if (!buf[0]) return false;

    if (buf[0] == '#') {
        const size_t len = std::strlen(buf + 1);
        if (len == 3 || len == 4) {
            const int r = hex_nibble(buf[1]);
            const int g = hex_nibble(buf[2]);
            const int b = hex_nibble(buf[3]);
            if (r < 0 || g < 0 || b < 0) return false;
            out_rgb565 = pack_rgb565(static_cast<uint8_t>(r * 17),
                                     static_cast<uint8_t>(g * 17),
                                     static_cast<uint8_t>(b * 17));
            return true;
        }
        if (len == 6 || len == 8) {
            const int r = (hex_nibble(buf[1]) << 4) | hex_nibble(buf[2]);
            const int g = (hex_nibble(buf[3]) << 4) | hex_nibble(buf[4]);
            const int b = (hex_nibble(buf[5]) << 4) | hex_nibble(buf[6]);
            if (r < 0 || g < 0 || b < 0) return false;
            out_rgb565 = pack_rgb565(static_cast<uint8_t>(r),
                                     static_cast<uint8_t>(g),
                                     static_cast<uint8_t>(b));
            return true;
        }
        return false;
    }

    for (size_t i = 0; buf[i]; ++i) buf[i] = to_lower_ascii(buf[i]);
    if (std::strncmp(buf, "rgb(", 4) == 0 || std::strncmp(buf, "rgba(", 5) == 0) {
        const char* lp = std::strchr(buf, '(');
        if (!lp) return false;
        int r = 0, g = 0, b = 0;
        if (std::sscanf(lp + 1, "%d , %d , %d", &r, &g, &b) == 3) {
            r = std::clamp(r, 0, 255);
            g = std::clamp(g, 0, 255);
            b = std::clamp(b, 0, 255);
            out_rgb565 = pack_rgb565(static_cast<uint8_t>(r),
                                     static_cast<uint8_t>(g),
                                     static_cast<uint8_t>(b));
            return true;
        }
        return false;
    }

    // Check if value is a multi-token CSS shorthand like "4px solid #b84a39"
    if (std::strchr(buf, ' ') != nullptr) {
        const char* hash = std::strchr(buf, '#');
        if (hash) {
            return parse_css_color(hash, out_rgb565);
        }
        const char* rgb = std::strstr(buf, "rgb");
        if (rgb) {
            return parse_css_color(rgb, out_rgb565);
        }
    }

    for (const auto& nc : kNamedColors) {
        if (std::strcmp(buf, nc.name) == 0) {
            out_rgb565 = pack_rgb565(nc.r, nc.g, nc.b);
            return true;
        }
    }
    return false;
}

size_t decode_html_entities(const char* src, size_t src_len, char* dst, size_t dst_cap) noexcept {
    if (!dst || dst_cap == 0) return 0;
    size_t w = 0;
    size_t i = 0;
    while (i < src_len && w + 4 < dst_cap) {
        if (src[i] == '&') {
            size_t semi = i + 1;
            while (semi < src_len && semi - i <= 12 && src[semi] != ';' &&
                   src[semi] != ' ' && src[semi] != '<' && src[semi] != '&') {
                ++semi;
            }
            if (semi < src_len && src[semi] == ';' && semi > i + 1) {
                char ent[16];
                const size_t elen = semi - (i + 1);
                std::memcpy(ent, src + i + 1, elen);
                ent[elen] = '\0';

                uint32_t cp = 0;
                bool decoded = false;
                if (ent[0] == '#') {
                    if (ent[1] == 'x' || ent[1] == 'X') {
                        cp = static_cast<uint32_t>(std::strtoul(ent + 2, nullptr, 16));
                        decoded = (cp > 0);
                    } else {
                        cp = static_cast<uint32_t>(std::strtoul(ent + 1, nullptr, 10));
                        decoded = (cp > 0);
                    }
                    if (cp == 0xA0) cp = 0x20;
                } else {
                    for (const auto& ne : kNamedEntities) {
                        if (std::strcmp(ent, ne.name) == 0) {
                            cp = ne.cp;
                            decoded = true;
                            break;
                        }
                    }
                }
                if (decoded) {
                    w += encode_utf8(cp, dst + w);
                    i = semi + 1;
                    continue;
                }
            }
        }
        dst[w++] = src[i++];
    }
    dst[w] = '\0';
    return w;
}

bool resolve_sd_vfs_path(const char* sd_root,
                         const char* rel_path,
                         char* out_vfs,
                         size_t out_vfs_cap,
                         char* out_clean_rel,
                         size_t out_clean_cap) noexcept {
    if (!sd_root || !rel_path || !out_vfs || out_vfs_cap == 0) return false;

    char segs[24][64];
    size_t seg_count = 0;

    const char* p = rel_path;
    while (*p) {
        while (*p == '/' || *p == '\\') ++p;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != '/' && *p != '\\') ++p;
        const size_t slen = static_cast<size_t>(p - start);
        if (slen == 1 && start[0] == '.') {
            continue;
        }
        if (slen == 2 && start[0] == '.' && start[1] == '.') {
            if (seg_count == 0) return false;
            --seg_count;
            continue;
        }
        if (slen >= sizeof(segs[0]) || seg_count >= 24) return false;
        std::memcpy(segs[seg_count], start, slen);
        segs[seg_count][slen] = '\0';
        ++seg_count;
    }

    char clean[256];
    if (seg_count == 0) {
        std::snprintf(clean, sizeof(clean), "/");
    } else {
        size_t pos = 0;
        for (size_t i = 0; i < seg_count; ++i) {
            const int n = std::snprintf(clean + pos, sizeof(clean) - pos, "/%s", segs[i]);
            if (n <= 0 || pos + static_cast<size_t>(n) >= sizeof(clean)) return false;
            pos += static_cast<size_t>(n);
        }
    }

    if (out_clean_rel && out_clean_cap > 0) {
        if (std::strlen(clean) + 1 > out_clean_cap) return false;
        std::snprintf(out_clean_rel, out_clean_cap, "%s", clean);
    }

    size_t root_len = std::strlen(sd_root);
    while (root_len > 1 && sd_root[root_len - 1] == '/') --root_len;

    const int n = (std::strcmp(clean, "/") == 0)
                      ? std::snprintf(out_vfs, out_vfs_cap, "%.*s", static_cast<int>(root_len), sd_root)
                      : std::snprintf(out_vfs, out_vfs_cap, "%.*s%s", static_cast<int>(root_len), sd_root, clean);
    return (n > 0 && static_cast<size_t>(n) < out_vfs_cap);
}

bool resolve_relative_href(const char* current_file_rel,
                           const char* href,
                           char* out_rel_path,
                           size_t out_rel_cap,
                           char* out_anchor,
                           size_t out_anchor_cap) noexcept {
    if (out_rel_path && out_rel_cap > 0) out_rel_path[0] = '\0';
    if (out_anchor && out_anchor_cap > 0) out_anchor[0] = '\0';
    if (!href || !href[0]) return false;

    // Reject external schemes (http:, https:, mailto:, javascript:, data:)
    if (ascii_contains_ci(href, "http://") || ascii_contains_ci(href, "https://") ||
        ascii_contains_ci(href, "mailto:") || ascii_contains_ci(href, "javascript:") ||
        ascii_contains_ci(href, "data:")) {
        return false;
    }

    const char* hash = std::strchr(href, '#');
    if (hash && out_anchor && out_anchor_cap > 0) {
        std::snprintf(out_anchor, out_anchor_cap, "%.*s",
                      static_cast<int>(out_anchor_cap - 1), hash + 1);
    }

    const size_t path_part_len = hash ? static_cast<size_t>(hash - href) : std::strlen(href);
    if (path_part_len == 0) {
        // Pure in-page anchor (#section)
        if (current_file_rel && out_rel_path && out_rel_cap > 0) {
            std::snprintf(out_rel_path, out_rel_cap, "%.*s",
                          static_cast<int>(out_rel_cap - 1), current_file_rel);
        }
        return (hash != nullptr);
    }

    char path_part[192];
    if (path_part_len >= sizeof(path_part)) return false;
    std::memcpy(path_part, href, path_part_len);
    path_part[path_part_len] = '\0';
    // Strip query string if present
    char* qmark = std::strchr(path_part, '?');
    if (qmark) *qmark = '\0';

    char combined[400];
    if (path_part[0] == '/') {
        std::snprintf(combined, sizeof(combined), "%s", path_part);
    } else {
        char base_dir[192] = "/";
        if (current_file_rel && current_file_rel[0]) {
            std::snprintf(base_dir, sizeof(base_dir), "%.190s", current_file_rel);
            char* last_slash = std::strrchr(base_dir, '/');
            if (last_slash) {
                *(last_slash + 1) = '\0';
            }
        }
        std::snprintf(combined, sizeof(combined), "%.190s/%.190s", base_dir, path_part);
    }

    char dummy_vfs[256];
    return resolve_sd_vfs_path("/sdcard", combined, dummy_vfs, sizeof(dummy_vfs),
                               out_rel_path, out_rel_cap);
}

bool is_html_filename(const char* name) noexcept {
    if (!name) return false;
    const char* dot = std::strrchr(name, '.');
    if (!dot) return false;
    return ascii_casecmp(dot, ".html") == 0 || ascii_casecmp(dot, ".htm") == 0 ||
           ascii_casecmp(dot, ".xhtml") == 0;
}

bool list_picker_directory(const char* sd_root,
                           const char* rel_dir,
                           bool html_only,
                           PickerEntry* out_entries,
                           size_t max_entries,
                           size_t& out_count) noexcept {
    out_count = 0;
    if (!sd_root || !rel_dir || !out_entries || max_entries == 0) return false;

    char vfs_dir[256];
    char clean_rel[192];
    if (!resolve_sd_vfs_path(sd_root, rel_dir, vfs_dir, sizeof(vfs_dir), clean_rel, sizeof(clean_rel))) {
        return false;
    }

    DIR* dir = opendir(vfs_dir);
    if (!dir) return false;

    struct dirent* de = nullptr;
    while ((de = readdir(dir)) != nullptr) {
        const char* name = de->d_name;
        if (!name || name[0] == '\0' || name[0] == '.') continue;
        if (ascii_casecmp(name, "System Volume Information") == 0 ||
            ascii_casecmp(name, "Spotlight-V100") == 0 ||
            ascii_casecmp(name, "fseventsd") == 0 ||
            ascii_casecmp(name, "Trashes") == 0) {
            continue;
        }

        char full_path[320];
        const int n = std::snprintf(full_path, sizeof(full_path), "%s/%s", vfs_dir, name);
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(full_path)) continue;

        struct stat st{};
        bool is_dir = (de->d_type == DT_DIR);
        uint64_t fsize = 0;
        if (stat(full_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            fsize = is_dir ? 0 : static_cast<uint64_t>(st.st_size);
        }

        const bool is_html = !is_dir && is_html_filename(name);
        if (html_only && !is_dir && !is_html) continue;

        if (out_count < max_entries) {
            PickerEntry& e = out_entries[out_count++];
            std::snprintf(e.name, sizeof(e.name), "%.127s", name);
            if (std::strcmp(clean_rel, "/") == 0) {
                std::snprintf(e.rel_path, sizeof(e.rel_path), "/%.189s", name);
            } else {
                std::snprintf(e.rel_path, sizeof(e.rel_path), "%.90s/%.95s", clean_rel, name);
            }
            e.is_dir = is_dir;
            e.is_html = is_html;
            e.size = fsize;
        }
    }
    closedir(dir);

    if (out_count > 1) {
        std::sort(out_entries, out_entries + out_count, [](const PickerEntry& a, const PickerEntry& b) {
            if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
            if (a.is_html != b.is_html) return a.is_html > b.is_html;
            return ascii_casecmp(a.name, b.name) < 0;
        });
    }
    return true;
}

namespace {

void scan_html_recursive(const char* vfs_root,
                         const char* rel_dir,
                         PickerEntry* out_entries,
                         size_t max_entries,
                         size_t& out_count,
                         int depth_left) noexcept {
    if (depth_left < 0 || out_count >= max_entries) return;
    char vfs_dir[256];
    char clean_rel[192];
    if (!resolve_sd_vfs_path(vfs_root, rel_dir, vfs_dir, sizeof(vfs_dir), clean_rel, sizeof(clean_rel))) {
        return;
    }

    DIR* dir = opendir(vfs_dir);
    if (!dir) return;

    struct dirent* de = nullptr;
    while ((de = readdir(dir)) != nullptr && out_count < max_entries) {
        const char* name = de->d_name;
        if (!name || name[0] == '\0' || name[0] == '.') continue;
        if (ascii_casecmp(name, "System Volume Information") == 0 ||
            ascii_casecmp(name, "firmware") == 0) {
            continue;
        }

        char full_path[320];
        const int n = std::snprintf(full_path, sizeof(full_path), "%s/%s", vfs_dir, name);
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(full_path)) continue;

        struct stat st{};
        bool is_dir = (de->d_type == DT_DIR);
        uint64_t fsize = 0;
        if (stat(full_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            fsize = is_dir ? 0 : static_cast<uint64_t>(st.st_size);
        }

        char child_rel[192];
        if (std::strcmp(clean_rel, "/") == 0) {
            std::snprintf(child_rel, sizeof(child_rel), "/%.189s", name);
        } else {
            std::snprintf(child_rel, sizeof(child_rel), "%.90s/%.95s", clean_rel, name);
        }

        if (is_dir) {
            scan_html_recursive(vfs_root, child_rel, out_entries, max_entries, out_count, depth_left - 1);
        } else if (is_html_filename(name)) {
            PickerEntry& e = out_entries[out_count++];
            std::snprintf(e.name, sizeof(e.name), "%.127s", child_rel + 1);
            std::snprintf(e.rel_path, sizeof(e.rel_path), "%.191s", child_rel);
            e.is_dir = false;
            e.is_html = true;
            e.size = fsize;
        }
    }
    closedir(dir);
}

}  // namespace

bool scan_all_html_files(const char* sd_root,
                         PickerEntry* out_entries,
                         size_t max_entries,
                         size_t& out_count,
                         int max_depth) noexcept {
    out_count = 0;
    if (!sd_root || !out_entries || max_entries == 0) return false;
    scan_html_recursive(sd_root, "/", out_entries, max_entries, out_count, max_depth);
    if (out_count > 1) {
        std::sort(out_entries, out_entries + out_count, [](const PickerEntry& a, const PickerEntry& b) {
            return ascii_casecmp(a.rel_path, b.rel_path) < 0;
        });
    }
    return true;
}

void format_file_size(uint64_t bytes, char* out, size_t out_cap) noexcept {
    if (!out || out_cap == 0) return;
    if (bytes < 1024ULL) {
        std::snprintf(out, out_cap, "%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ULL * 1024ULL) {
        std::snprintf(out, out_cap, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(out, out_cap, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
}

// ============================================================================
// CSS & HTML Parser
// ============================================================================
namespace {

struct CssVar {
    char name[40];
    char value[96];
};

struct CssPropSet {
    bool has_family = false;
    FontFamily family = FontFamily::montserrat;
    bool has_size = false;
    uint8_t size_px = 18;
    bool is_em_size = false;
    float em_scale = 1.0f;
    bool has_bold = false;
    bool bold = false;
    bool has_italic = false;
    bool italic = false;
    bool has_underline = false;
    bool underline = false;
    bool has_uppercase = false;
    bool uppercase = false;
    bool has_fg = false;
    uint16_t fg_rgb565 = 0;
    bool has_bg = false;
    uint16_t bg_rgb565 = 0;
    bool has_border_left = false;
    uint16_t border_left_rgb565 = 0;
    bool has_border_box = false;
    uint16_t border_box_rgb565 = 0;
    bool has_align = false;
    TextAlign align = TextAlign::left;
};

struct CssRule {
    char selector[48];  // e.g. "body", "h1", ".callout", "#hero"
    CssPropSet props;
};

constexpr size_t kMaxCssVars = 48;
constexpr size_t kMaxCssRules = 96;

struct Stylesheet {
    CssVar vars[kMaxCssVars];
    size_t var_count = 0;
    CssRule rules[kMaxCssRules];
    size_t rule_count = 0;
};

void resolve_css_var_value(const Stylesheet& sheet, const char* raw_val, char* out, size_t out_cap) noexcept {
    if (!raw_val || !out || out_cap == 0) return;
    const char* vp = std::strstr(raw_val, "var(");
    if (!vp) {
        std::snprintf(out, out_cap, "%.*s", static_cast<int>(out_cap - 1), raw_val);
        trim_inplace(out);
        return;
    }
    const char* start = vp + 4;
    const char* rp = std::strchr(start, ')');
    if (!rp) {
        std::snprintf(out, out_cap, "%.*s", static_cast<int>(out_cap - 1), raw_val);
        trim_inplace(out);
        return;
    }
    char inside[96];
    const size_t ilen = std::min(static_cast<size_t>(rp - start), sizeof(inside) - 1);
    std::memcpy(inside, start, ilen);
    inside[ilen] = '\0';

    char var_name[48];
    char fallback_val[64] = {0};
    char* comma = std::strchr(inside, ',');
    if (comma) {
        *comma = '\0';
        std::snprintf(fallback_val, sizeof(fallback_val), "%.63s", comma + 1);
        trim_inplace(fallback_val);
    }
    std::snprintf(var_name, sizeof(var_name), "%.47s", inside);
    trim_inplace(var_name);

    for (size_t i = 0; i < sheet.var_count; ++i) {
        if (ascii_casecmp(sheet.vars[i].name, var_name) == 0) {
            std::snprintf(out, out_cap, "%.*s", static_cast<int>(out_cap - 1), sheet.vars[i].value);
            return;
        }
    }
    std::snprintf(out, out_cap, "%.*s", static_cast<int>(out_cap - 1), fallback_val);
}

void parse_css_declarations(const Stylesheet& sheet, const char* decls, size_t len, CssPropSet& props, Stylesheet* mut_sheet = nullptr) noexcept {
    size_t pos = 0;
    while (pos < len) {
        size_t semi = pos;
        int paren_depth = 0;
        while (semi < len) {
            if (decls[semi] == '(') ++paren_depth;
            else if (decls[semi] == ')' && paren_depth > 0) --paren_depth;
            else if (decls[semi] == ';' && paren_depth == 0) break;
            ++semi;
        }
        size_t colon = pos;
        while (colon < semi && decls[colon] != ':') ++colon;
        if (colon < semi) {
            char key[48];
            char raw_val[128];
            const size_t klen = std::min(colon - pos, sizeof(key) - 1);
            const size_t vlen = std::min(semi - (colon + 1), sizeof(raw_val) - 1);
            std::memcpy(key, decls + pos, klen);
            key[klen] = '\0';
            std::memcpy(raw_val, decls + colon + 1, vlen);
            raw_val[vlen] = '\0';
            trim_inplace(key);
            trim_inplace(raw_val);

            if (key[0] == '-' && key[1] == '-' && mut_sheet && mut_sheet->var_count < kMaxCssVars) {
                CssVar& cv = mut_sheet->vars[mut_sheet->var_count++];
                std::snprintf(cv.name, sizeof(cv.name), "%.39s", key);
                std::snprintf(cv.value, sizeof(cv.value), "%.95s", raw_val);
            } else {
                for (size_t i = 0; key[i]; ++i) key[i] = to_lower_ascii(key[i]);
                char val[128];
                resolve_css_var_value(sheet, raw_val, val, sizeof(val));

                if (std::strcmp(key, "font-family") == 0) {
                    props.has_family = true;
                    props.family = resolve_font_family(val, FontFamily::montserrat);
                } else if (std::strcmp(key, "font") == 0) {
                    props.has_family = true;
                    props.family = resolve_font_family(val, FontFamily::montserrat);
                    if (ascii_contains_ci(val, "bold")) {
                        props.has_bold = true;
                        props.bold = true;
                    }
                    if (ascii_contains_ci(val, "italic")) {
                        props.has_italic = true;
                        props.italic = true;
                    }
                } else if (std::strcmp(key, "font-size") == 0) {
                    char vlow[48];
                    std::snprintf(vlow, sizeof(vlow), "%.47s", val);
                    for (size_t i = 0; vlow[i]; ++i) vlow[i] = to_lower_ascii(vlow[i]);
                    const float num = std::strtof(vlow, nullptr);
                    if (std::strstr(vlow, "rem") || std::strstr(vlow, "em")) {
                        if (num > 0.2f && num < 5.0f) {
                            props.has_size = true;
                            props.is_em_size = true;
                            props.em_scale = num;
                            props.size_px = static_cast<uint8_t>(std::clamp(static_cast<int>(18.0f * num + 0.5f), 11, 46));
                        }
                    } else if (std::strstr(vlow, "%")) {
                        if (num >= 40.0f && num <= 400.0f) {
                            props.has_size = true;
                            props.is_em_size = true;
                            props.em_scale = num / 100.0f;
                            props.size_px = static_cast<uint8_t>(std::clamp(static_cast<int>(18.0f * (num / 100.0f) + 0.5f), 11, 46));
                        }
                    } else if (std::strstr(vlow, "pt")) {
                        if (num >= 6.0f && num <= 48.0f) {
                            props.has_size = true;
                            props.size_px = static_cast<uint8_t>(std::clamp(static_cast<int>(num * 1.33f + 0.5f), 11, 46));
                        }
                    } else if (num >= 9.0f && num <= 64.0f) {
                        props.has_size = true;
                        props.size_px = static_cast<uint8_t>(std::clamp(static_cast<int>(num + 0.5f), 11, 46));
                    } else if (std::strcmp(vlow, "small") == 0 || std::strcmp(vlow, "x-small") == 0) {
                        props.has_size = true;
                        props.size_px = 14;
                    } else if (std::strcmp(vlow, "large") == 0) {
                        props.has_size = true;
                        props.size_px = 22;
                    } else if (std::strcmp(vlow, "x-large") == 0) {
                        props.has_size = true;
                        props.size_px = 26;
                    } else if (std::strcmp(vlow, "xx-large") == 0) {
                        props.has_size = true;
                        props.size_px = 32;
                    }
                } else if (std::strcmp(key, "font-weight") == 0) {
                    const int w = std::atoi(val);
                    props.has_bold = true;
                    props.bold = (ascii_contains_ci(val, "bold") || ascii_contains_ci(val, "bolder") || w >= 600);
                } else if (std::strcmp(key, "font-style") == 0) {
                    props.has_italic = true;
                    props.italic = (ascii_contains_ci(val, "italic") || ascii_contains_ci(val, "oblique"));
                } else if (std::strcmp(key, "text-decoration") == 0) {
                    if (ascii_contains_ci(val, "underline")) {
                        props.has_underline = true;
                        props.underline = true;
                    } else if (ascii_contains_ci(val, "none")) {
                        props.has_underline = true;
                        props.underline = false;
                    }
                } else if (std::strcmp(key, "text-transform") == 0) {
                    props.has_uppercase = true;
                    props.uppercase = ascii_contains_ci(val, "uppercase");
                } else if (std::strcmp(key, "text-align") == 0) {
                    if (ascii_contains_ci(val, "center")) {
                        props.has_align = true;
                        props.align = TextAlign::center;
                    } else if (ascii_contains_ci(val, "right")) {
                        props.has_align = true;
                        props.align = TextAlign::right;
                    } else if (ascii_contains_ci(val, "left")) {
                        props.has_align = true;
                        props.align = TextAlign::left;
                    }
                } else if (std::strcmp(key, "color") == 0) {
                    uint16_t c = 0;
                    if (parse_css_color(val, c)) {
                        props.has_fg = true;
                        props.fg_rgb565 = c;
                    }
                } else if (std::strcmp(key, "background-color") == 0 || std::strcmp(key, "background") == 0) {
                    uint16_t c = 0;
                    if (parse_css_color(val, c)) {
                        props.has_bg = true;
                        props.bg_rgb565 = c;
                    }
                } else if (std::strcmp(key, "border-left") == 0 || std::strcmp(key, "border-left-color") == 0) {
                    uint16_t c = 0;
                    if (parse_css_color(val, c)) {
                        props.has_border_left = true;
                        props.border_left_rgb565 = c;
                    }
                } else if (std::strcmp(key, "border") == 0 || std::strcmp(key, "border-color") == 0) {
                    uint16_t c = 0;
                    if (parse_css_color(val, c)) {
                        props.has_border_box = true;
                        props.border_box_rgb565 = c;
                    }
                }
            }
        }
        pos = semi + 1;
    }
}

void parse_stylesheet_block(const char* css, size_t len, Stylesheet& sheet) noexcept {
    // Strip /* ... */ comments while scanning selector { declarations }
    size_t i = 0;
    while (i < len) {
        while (i < len && std::isspace(static_cast<unsigned char>(css[i]))) ++i;
        if (i + 1 < len && css[i] == '/' && css[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(css[i] == '*' && css[i + 1] == '/')) ++i;
            i += 2;
            continue;
        }
        size_t sel_start = i;
        while (i < len && css[i] != '{' && css[i] != '}') ++i;
        if (i >= len) break;
        if (css[i] == '}') {
            ++i;
            continue;
        }
        const size_t sel_len = i - sel_start;
        ++i;  // skip '{'
        const size_t body_start = i;
        int brace_depth = 1;
        while (i < len && brace_depth > 0) {
            if (css[i] == '{') ++brace_depth;
            else if (css[i] == '}') --brace_depth;
            ++i;
        }
        const size_t body_len = (i > body_start) ? (i - 1 - body_start) : 0;

        char sel_group[160];
        const size_t copy_n = std::min(sel_len, sizeof(sel_group) - 1);
        std::memcpy(sel_group, css + sel_start, copy_n);
        sel_group[copy_n] = '\0';

        if ( sel_group[0] == '@' ) {
            continue;
        }

        CssPropSet props{};
        parse_css_declarations(sheet, css + body_start, body_len, props, &sheet);

        // Register each comma-separated selector
        const char* sp = sel_group;
        while (*sp && sheet.rule_count < kMaxCssRules) {
            const char* comma = std::strchr(sp, ',');
            const size_t sl = comma ? static_cast<size_t>(comma - sp) : std::strlen(sp);
            char one_sel[48];
            const size_t cn = std::min(sl, sizeof(one_sel) - 1);
            std::memcpy(one_sel, sp, cn);
            one_sel[cn] = '\0';
            trim_inplace(one_sel);
            for (size_t k = 0; one_sel[k]; ++k) one_sel[k] = to_lower_ascii(one_sel[k]);

            // Keep simple tag, .class, or #id (if compound like "article h2", take last token "h2")
            char* last_space = std::strrchr(one_sel, ' ');
            const char* simple_sel = last_space ? (last_space + 1) : one_sel;

            if (simple_sel[0] && std::strcmp(simple_sel, ":root") != 0) {
                CssRule& rule = sheet.rules[sheet.rule_count++];
                std::snprintf(rule.selector, sizeof(rule.selector), "%.47s", simple_sel);
                rule.props = props;
            }
            if (!comma) break;
            sp = comma + 1;
        }
    }
}

struct StyleState {
    FontFamily family = FontFamily::montserrat;
    uint8_t size_px = 18;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool uppercase = false;
    bool inline_code = false;
    bool has_bg = false;
    uint16_t fg_rgb565 = pack_rgb565(24, 28, 34);
    uint16_t bg_rgb565 = 0xFFFF;
    int16_t link_index = -1;
    TextAlign align = TextAlign::left;
    bool block_has_bg = false;
    uint16_t block_bg_rgb565 = 0;
    bool block_has_border_left = false;
    uint16_t block_border_left_rgb565 = 0;
    bool block_has_border_box = false;
    uint16_t block_border_box_rgb565 = 0;
};

void apply_prop_set(const CssPropSet& p, StyleState& st, bool is_block_elem) noexcept {
    if (p.has_family) st.family = p.family;
    if (p.has_size) st.size_px = p.size_px;
    if (p.has_bold) st.bold = p.bold;
    if (p.has_italic) st.italic = p.italic;
    if (p.has_underline) st.underline = p.underline;
    if (p.has_uppercase) st.uppercase = p.uppercase;
    if (p.has_fg) st.fg_rgb565 = p.fg_rgb565;
    if (p.has_align) st.align = p.align;
    if (p.has_bg) {
        if (is_block_elem) {
            st.block_has_bg = true;
            st.block_bg_rgb565 = p.bg_rgb565;
        } else {
            st.has_bg = true;
            st.bg_rgb565 = p.bg_rgb565;
        }
    }
    if (p.has_border_left) {
        st.block_has_border_left = true;
        st.block_border_left_rgb565 = p.border_left_rgb565;
    }
    if (p.has_border_box) {
        st.block_has_border_box = true;
        st.block_border_box_rgb565 = p.border_box_rgb565;
    }
}

void apply_matching_rules(const Stylesheet& sheet,
                          const char* tag_lower,
                          const char* class_attr,
                          const char* id_attr,
                          StyleState& st,
                          bool is_block_elem) noexcept {
    for (size_t i = 0; i < sheet.rule_count; ++i) {
        const CssRule& r = sheet.rules[i];
        if (r.selector[0] == '.') {
            if (class_attr && class_attr[0]) {
                // Check space-separated class tokens
                const char* cp = class_attr;
                while (*cp) {
                    while (*cp && std::isspace(static_cast<unsigned char>(*cp))) ++cp;
                    if (!*cp) break;
                    const char* start = cp;
                    while (*cp && !std::isspace(static_cast<unsigned char>(*cp))) ++cp;
                    char ctok[48];
                    const size_t clen = std::min(static_cast<size_t>(cp - start), sizeof(ctok) - 1);
                    std::memcpy(ctok, start, clen);
                    ctok[clen] = '\0';
                    if (ascii_casecmp(r.selector + 1, ctok) == 0) {
                        apply_prop_set(r.props, st, is_block_elem);
                        break;
                    }
                }
            }
        } else if (r.selector[0] == '#') {
            if (id_attr && id_attr[0] && ascii_casecmp(r.selector + 1, id_attr) == 0) {
                apply_prop_set(r.props, st, is_block_elem);
            }
        } else if (ascii_casecmp(r.selector, tag_lower) == 0) {
            apply_prop_set(r.props, st, is_block_elem);
        }
    }
}

bool extract_attr(const char* tag_body, size_t tag_len, const char* attr_name, char* out, size_t out_cap) noexcept {
    if (!out || out_cap == 0) return false;
    out[0] = '\0';
    const size_t alen = std::strlen(attr_name);
    size_t i = 0;
    while (i + alen < tag_len) {
        if ((i == 0 || std::isspace(static_cast<unsigned char>(tag_body[i - 1]))) &&
             [&]() {
                 for (size_t k = 0; k < alen; ++k) {
                     if (to_lower_ascii(tag_body[i + k]) != to_lower_ascii(attr_name[k])) return false;
                 }
                 return true;
             }()) {
            size_t p = i + alen;
            while (p < tag_len && std::isspace(static_cast<unsigned char>(tag_body[p]))) ++p;
            if (p < tag_len && tag_body[p] == '=') {
                ++p;
                while (p < tag_len && std::isspace(static_cast<unsigned char>(tag_body[p]))) ++p;
                if (p < tag_len) {
                    char quote = 0;
                    if (tag_body[p] == '"' || tag_body[p] == '\'') {
                        quote = tag_body[p++];
                    }
                    const size_t val_start = p;
                    while (p < tag_len) {
                        if (quote) {
                            if (tag_body[p] == quote) break;
                        } else if (std::isspace(static_cast<unsigned char>(tag_body[p])) || tag_body[p] == '>') {
                            break;
                        }
                        ++p;
                    }
                    decode_html_entities(tag_body + val_start, p - val_start, out, out_cap);
                    return true;
                }
            }
        }
        ++i;
    }
    return false;
}

bool is_block_tag(const char* t) noexcept {
    return std::strcmp(t, "p") == 0 || std::strcmp(t, "div") == 0 ||
           std::strcmp(t, "section") == 0 || std::strcmp(t, "article") == 0 ||
           std::strcmp(t, "header") == 0 || std::strcmp(t, "footer") == 0 ||
           std::strcmp(t, "main") == 0 || std::strcmp(t, "nav") == 0 ||
           std::strcmp(t, "aside") == 0 || std::strcmp(t, "blockquote") == 0 ||
           std::strcmp(t, "pre") == 0 || std::strcmp(t, "li") == 0 ||
           std::strcmp(t, "dt") == 0 || std::strcmp(t, "dd") == 0 ||
           std::strcmp(t, "tr") == 0 || std::strcmp(t, "hr") == 0 ||
           std::strcmp(t, "details") == 0 || std::strcmp(t, "summary") == 0 ||
           std::strcmp(t, "figure") == 0 || std::strcmp(t, "figcaption") == 0 ||
           (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && t[2] == '\0');
}

}  // namespace

bool parse_html_document(const char* html, size_t html_len, HtmlDocument* doc) noexcept {
    if (!html || !doc || !doc->text_pool || !doc->spans || !doc->blocks) return false;
    doc_clear(doc);

    auto* sheet_ptr = static_cast<Stylesheet*>(alloc_mem(sizeof(Stylesheet)));
    if (!sheet_ptr) return false;
    *sheet_ptr = Stylesheet{};
    Stylesheet& sheet = *sheet_ptr;

    // First pass: extract <title> and <style> blocks
    {
        size_t i = 0;
        while (i < html_len) {
            if (html[i] == '<') {
                if (i + 7 < html_len &&
                    to_lower_ascii(html[i + 1]) == 't' &&
                    to_lower_ascii(html[i + 2]) == 'i' &&
                    to_lower_ascii(html[i + 3]) == 't' &&
                    to_lower_ascii(html[i + 4]) == 'l' &&
                    to_lower_ascii(html[i + 5]) == 'e' &&
                    (html[i + 6] == '>' || std::isspace(static_cast<unsigned char>(html[i + 6])))) {
                    size_t gt = i + 6;
                    while (gt < html_len && html[gt] != '>') ++gt;
                    if (gt < html_len) {
                        const size_t t_start = gt + 1;
                        size_t t_end = t_start;
                        while (t_end + 7 < html_len &&
                               !(html[t_end] == '<' && html[t_end + 1] == '/' &&
                                 to_lower_ascii(html[t_end + 2]) == 't')) {
                            ++t_end;
                        }
                        decode_html_entities(html + t_start, t_end - t_start, doc->title, sizeof(doc->title));
                        trim_inplace(doc->title);
                        i = t_end;
                        continue;
                    }
                } else if (i + 7 < html_len &&
                           to_lower_ascii(html[i + 1]) == 's' &&
                           to_lower_ascii(html[i + 2]) == 't' &&
                           to_lower_ascii(html[i + 3]) == 'y' &&
                           to_lower_ascii(html[i + 4]) == 'l' &&
                           to_lower_ascii(html[i + 5]) == 'e' &&
                           (html[i + 6] == '>' || std::isspace(static_cast<unsigned char>(html[i + 6])))) {
                    size_t gt = i + 6;
                    while (gt < html_len && html[gt] != '>') ++gt;
                    if (gt < html_len) {
                        const size_t s_start = gt + 1;
                        size_t s_end = s_start;
                        while (s_end + 7 < html_len &&
                               !(html[s_end] == '<' && html[s_end + 1] == '/' &&
                                 to_lower_ascii(html[s_end + 2]) == 's' &&
                                 to_lower_ascii(html[s_end + 3]) == 't')) {
                            ++s_end;
                        }
                        parse_stylesheet_block(html + s_start, s_end - s_start, sheet);
                        i = s_end;
                        continue;
                    }
                }
            }
            ++i;
        }
    }

    // Resolve base document styles from html / body rules
    StyleState root_style{};
    root_style.family = FontFamily::montserrat;
    root_style.size_px = 18;
    root_style.fg_rgb565 = pack_rgb565(24, 28, 34);
    root_style.bg_rgb565 = 0xFFFF;

    for (size_t i = 0; i < sheet.rule_count; ++i) {
        const CssRule& r = sheet.rules[i];
        if (std::strcmp(r.selector, "html") == 0 || std::strcmp(r.selector, "body") == 0) {
            if (r.props.has_family) {
                root_style.family = r.props.family;
                doc->base_family = r.props.family;
            }
            if (r.props.has_size) {
                root_style.size_px = r.props.size_px;
                doc->base_size_px = r.props.size_px;
            }
            if (r.props.has_fg) {
                root_style.fg_rgb565 = r.props.fg_rgb565;
                doc->doc_fg_rgb565 = r.props.fg_rgb565;
                doc->has_doc_fg = true;
            }
            if (r.props.has_bg) {
                root_style.bg_rgb565 = r.props.bg_rgb565;
                doc->doc_bg_rgb565 = r.props.bg_rgb565;
                doc->has_doc_bg = true;
            }
        }
    }

    // Style stack
    constexpr size_t kMaxStack = 32;
    struct StackEntry {
        char tag[16];
        StyleState style;
    };
    StackEntry stack[kMaxStack];
    size_t stack_depth = 1;
    std::snprintf(stack[0].tag, sizeof(stack[0].tag), "body");
    stack[0].style = root_style;

    // Active block state
    bool in_block = false;
    HtmlBlock cur_block{};
    auto init_block = [&](BlockType bt, const StyleState& st) {
        std::memset(&cur_block, 0, sizeof(cur_block));
        cur_block.type = bt;
        cur_block.align = st.align;
        cur_block.has_bg = st.block_has_bg;
        cur_block.bg_rgb565 = st.block_bg_rgb565;
        cur_block.has_border_left = st.block_has_border_left;
        cur_block.border_left_rgb565 = st.block_border_left_rgb565;
        cur_block.has_border_box = st.block_has_border_box;
        cur_block.border_box_rgb565 = st.block_border_box_rgb565;
        cur_block.margin_top_px = 6;
        cur_block.margin_bottom_px = 8;
        cur_block.padding_px = (st.block_has_bg || st.block_has_border_left || st.block_has_border_box) ? 10 : 0;
        cur_block.span_start = static_cast<uint16_t>(doc->span_count);
        cur_block.span_count = 0;
        in_block = true;
    };

    auto flush_block = [&]() {
        if (!in_block) return;
        if (cur_block.type == BlockType::hr || cur_block.type == BlockType::table_row ||
            cur_block.span_count > 0) {
            if (doc->block_count < doc->block_cap) {
                const uint16_t blk_idx = static_cast<uint16_t>(doc->block_count);
                doc->blocks[doc->block_count++] = cur_block;

                // Record headings in document outline
                if (cur_block.type == BlockType::heading && doc->outline_count < doc->outline_cap) {
                    HtmlOutlineItem& item = doc->outline[doc->outline_count++];
                    item.level = cur_block.heading_level;
                    item.block_idx = blk_idx;
                    item.title[0] = '\0';
                    size_t w = 0;
                    for (uint16_t s = 0; s < cur_block.span_count && w + 1 < sizeof(item.title); ++s) {
                        const HtmlSpan& sp = doc->spans[cur_block.span_start + s];
                        const size_t copy_n = std::min<size_t>(sp.text_len, sizeof(item.title) - 1 - w);
                        std::memcpy(item.title + w, doc->text_pool + sp.text_offset, copy_n);
                        w += copy_n;
                    }
                    item.title[w] = '\0';
                    trim_inplace(item.title);
                }
            }
        }
        in_block = false;
    };

    auto append_span = [&](const char* text, size_t len, const StyleState& st, bool preserve_ws) {
        if (len == 0 || doc->span_count >= doc->span_cap || doc->text_len + 4 >= doc->text_cap) return;
        if (!in_block) {
            init_block(BlockType::paragraph, st);
        }

        // Decode entities into temporary buffer, then normalize whitespace unless preserve_ws
        char* decoded = static_cast<char*>(alloc_mem(len + 16));
        if (!decoded) return;
        const size_t dec_len = decode_html_entities(text, len, decoded, len + 15);

        const uint32_t start_off = static_cast<uint32_t>(doc->text_len);
        size_t written = 0;
        if (preserve_ws) {
            for (size_t k = 0; k < dec_len && doc->text_len + 2 < doc->text_cap; ++k) {
                if (decoded[k] == '\r') continue;
                char c = decoded[k];
                if (st.uppercase && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                doc->text_pool[doc->text_len++] = c;
                ++written;
            }
        } else {
            bool prev_space = (cur_block.span_count == 0);
            if (!prev_space && doc->text_len > start_off) {
                prev_space = (doc->text_pool[doc->text_len - 1] == ' ');
            } else if (!prev_space && cur_block.span_count > 0) {
                const HtmlSpan& prev_sp = doc->spans[doc->span_count - 1];
                if (prev_sp.text_len > 0) {
                    const char last_c = doc->text_pool[prev_sp.text_offset + prev_sp.text_len - 1];
                    prev_space = (last_c == ' ' || last_c == '\n');
                }
            }
            for (size_t k = 0; k < dec_len && doc->text_len + 2 < doc->text_cap; ++k) {
                const unsigned char uc = static_cast<unsigned char>(decoded[k]);
                if (uc == ' ' || uc == '\t' || uc == '\n' || uc == '\r') {
                    if (!prev_space) {
                        doc->text_pool[doc->text_len++] = ' ';
                        ++written;
                        prev_space = true;
                    }
                } else {
                    char c = decoded[k];
                    if (st.uppercase && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                    doc->text_pool[doc->text_len++] = c;
                    ++written;
                    prev_space = false;
                }
            }
        }
        std::free(decoded);

        if (written == 0) return;
        doc->text_pool[doc->text_len] = '\0';

        HtmlSpan& sp = doc->spans[doc->span_count++];
        sp.text_offset = start_off;
        sp.text_len = static_cast<uint16_t>(written);
        sp.family = st.family;
        sp.size_px = st.size_px;
        sp.bold = st.bold;
        sp.italic = st.italic;
        sp.underline = st.underline;
        sp.strikethrough = st.strikethrough;
        sp.uppercase = st.uppercase;
        sp.inline_code = st.inline_code;
        sp.has_bg = st.has_bg;
        sp.fg_rgb565 = st.fg_rgb565;
        sp.bg_rgb565 = st.bg_rgb565;
        sp.link_index = st.link_index;
        ++cur_block.span_count;
    };

    // List and table state
    uint8_t list_depth = 0;
    bool list_ordered[8] = {false};
    uint16_t list_counter[8] = {0};
    bool in_pre = false;
    bool in_table_row = false;
    bool in_table_cell = false;
    int active_cell_idx = -1;

    size_t pos = 0;
    while (pos < html_len) {
        if (html[pos] == '<') {
            // Comment <!-- ... -->
            if (pos + 3 < html_len && html[pos + 1] == '!' && html[pos + 2] == '-' && html[pos + 3] == '-') {
                pos += 4;
                while (pos + 2 < html_len && !(html[pos] == '-' && html[pos + 1] == '-' && html[pos + 2] == '>')) {
                    ++pos;
                }
                pos = (pos + 2 < html_len) ? (pos + 3) : html_len;
                continue;
            }
            // <!DOCTYPE ...>
            if (pos + 1 < html_len && html[pos + 1] == '!') {
                while (pos < html_len && html[pos] != '>') ++pos;
                if (pos < html_len) ++pos;
                continue;
            }

            size_t gt = pos + 1;
            while (gt < html_len && html[gt] != '>') ++gt;
            if (gt >= html_len) break;

            const char* tag_inner = html + pos + 1;
            size_t inner_len = gt - (pos + 1);
            bool is_closing = false;
            if (inner_len > 0 && tag_inner[0] == '/') {
                is_closing = true;
                ++tag_inner;
                --inner_len;
            }
            while (inner_len > 0 && (tag_inner[inner_len - 1] == '/' ||
                                     std::isspace(static_cast<unsigned char>(tag_inner[inner_len - 1])))) {
                --inner_len;
            }

            char tag_name[24];
            size_t tlen = 0;
            while (tlen < inner_len && tlen + 1 < sizeof(tag_name) &&
                   !std::isspace(static_cast<unsigned char>(tag_inner[tlen]))) {
                tag_name[tlen] = to_lower_ascii(tag_inner[tlen]);
                ++tlen;
            }
            tag_name[tlen] = '\0';

            // Skip <head>, <script>, <style>, <svg> subtrees completely
            if (!is_closing && (std::strcmp(tag_name, "script") == 0 ||
                                std::strcmp(tag_name, "style") == 0 ||
                                std::strcmp(tag_name, "svg") == 0 ||
                                std::strcmp(tag_name, "title") == 0)) {
                pos = gt + 1;
                while (pos + 2 + tlen < html_len) {
                    if (html[pos] == '<' && html[pos + 1] == '/') {
                        bool match = true;
                        for (size_t k = 0; k < tlen; ++k) {
                            if (to_lower_ascii(html[pos + 2 + k]) != tag_name[k]) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            while (pos < html_len && html[pos] != '>') ++pos;
                            if (pos < html_len) ++pos;
                            break;
                        }
                    }
                    ++pos;
                }
                continue;
            }

            if (is_closing) {
                if (std::strcmp(tag_name, "ul") == 0 || std::strcmp(tag_name, "ol") == 0) {
                    flush_block();
                    if (list_depth > 0) --list_depth;
                } else if (std::strcmp(tag_name, "pre") == 0) {
                    flush_block();
                    in_pre = false;
                } else if (std::strcmp(tag_name, "tr") == 0) {
                    in_table_cell = false;
                    active_cell_idx = -1;
                    flush_block();
                    in_table_row = false;
                } else if (std::strcmp(tag_name, "td") == 0 || std::strcmp(tag_name, "th") == 0) {
                    in_table_cell = false;
                } else if (is_block_tag(tag_name)) {
                    flush_block();
                }

                // Pop style stack back to matching tag
                for (size_t s = stack_depth; s > 1; --s) {
                    if (std::strcmp(stack[s - 1].tag, tag_name) == 0) {
                        stack_depth = s - 1;
                        break;
                    }
                }
            } else {
                // Opening tag
                StyleState next_st = stack[stack_depth - 1].style;
                const bool blk_elem = is_block_tag(tag_name) || std::strcmp(tag_name, "body") == 0 ||
                                      std::strcmp(tag_name, "td") == 0 || std::strcmp(tag_name, "th") == 0;
                if (!blk_elem) {
                    // Inline elements shouldn't inherit single-block box borders
                }

                char class_attr[96] = {0};
                char id_attr[48] = {0};
                char style_attr[256] = {0};
                extract_attr(tag_inner, inner_len, "class", class_attr, sizeof(class_attr));
                extract_attr(tag_inner, inner_len, "id", id_attr, sizeof(id_attr));
                extract_attr(tag_inner, inner_len, "style", style_attr, sizeof(style_attr));

                // Semantic defaults before CSS rules & inline styles
                if (tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= '6' && tag_name[2] == '\0') {
                    const int lvl = tag_name[1] - '0';
                    constexpr uint8_t kHeadingSizes[6] = {32, 26, 22, 19, 17, 15};
                    next_st.size_px = kHeadingSizes[lvl - 1];
                    next_st.bold = true;
                } else if (std::strcmp(tag_name, "b") == 0 || std::strcmp(tag_name, "strong") == 0 ||
                           std::strcmp(tag_name, "th") == 0 || std::strcmp(tag_name, "dt") == 0 ||
                           std::strcmp(tag_name, "summary") == 0) {
                    next_st.bold = true;
                } else if (std::strcmp(tag_name, "i") == 0 || std::strcmp(tag_name, "em") == 0 ||
                           std::strcmp(tag_name, "cite") == 0 || std::strcmp(tag_name, "dfn") == 0 ||
                           std::strcmp(tag_name, "figcaption") == 0) {
                    next_st.italic = true;
                } else if (std::strcmp(tag_name, "u") == 0 || std::strcmp(tag_name, "ins") == 0) {
                    next_st.underline = true;
                } else if (std::strcmp(tag_name, "s") == 0 || std::strcmp(tag_name, "del") == 0 ||
                           std::strcmp(tag_name, "strike") == 0) {
                    next_st.strikethrough = true;
                } else if (std::strcmp(tag_name, "small") == 0 || std::strcmp(tag_name, "sub") == 0 ||
                           std::strcmp(tag_name, "sup") == 0) {
                    next_st.size_px = static_cast<uint8_t>(std::max(12, static_cast<int>(next_st.size_px) - 4));
                } else if (std::strcmp(tag_name, "code") == 0 || std::strcmp(tag_name, "kbd") == 0 ||
                           std::strcmp(tag_name, "samp") == 0 || std::strcmp(tag_name, "tt") == 0) {
                    next_st.family = FontFamily::courier_new;
                    if (!in_pre) {
                        next_st.inline_code = true;
                        next_st.has_bg = true;
                        next_st.bg_rgb565 = pack_rgb565(232, 236, 242);
                        next_st.size_px = static_cast<uint8_t>(std::max(13, static_cast<int>(next_st.size_px) - 2));
                    }
                } else if (std::strcmp(tag_name, "pre") == 0) {
                    next_st.family = FontFamily::courier_new;
                    next_st.size_px = 15;
                    next_st.block_has_bg = true;
                    next_st.block_bg_rgb565 = pack_rgb565(240, 243, 247);
                    next_st.block_has_border_box = true;
                    next_st.block_border_box_rgb565 = pack_rgb565(198, 206, 216);
                } else if (std::strcmp(tag_name, "mark") == 0) {
                    next_st.has_bg = true;
                    next_st.bg_rgb565 = pack_rgb565(254, 240, 138);
                    next_st.fg_rgb565 = pack_rgb565(20, 20, 20);
                } else if (std::strcmp(tag_name, "blockquote") == 0 || std::strcmp(tag_name, "aside") == 0) {
                    next_st.block_has_border_left = true;
                    next_st.block_border_left_rgb565 = pack_rgb565(59, 130, 246);
                    next_st.block_has_bg = true;
                    next_st.block_bg_rgb565 = pack_rgb565(243, 246, 250);
                    if (std::strcmp(tag_name, "blockquote") == 0) next_st.italic = true;
                } else if (std::strcmp(tag_name, "a") == 0) {
                    char href[192] = {0};
                    if (extract_attr(tag_inner, inner_len, "href", href, sizeof(href)) && href[0]) {
                        next_st.underline = true;
                        next_st.fg_rgb565 = pack_rgb565(37, 99, 235);
                        if (doc->link_count < doc->link_cap) {
                            next_st.link_index = static_cast<int16_t>(doc->link_count);
                            std::snprintf(doc->links[doc->link_count++].href, 192, "%.191s", href);
                        }
                    }
                    if (!id_attr[0]) {
                        extract_attr(tag_inner, inner_len, "name", id_attr, sizeof(id_attr));
                    }
                } else if (std::strcmp(tag_name, "font") == 0) {
                    char face[96] = {0};
                    char col[48] = {0};
                    if (extract_attr(tag_inner, inner_len, "face", face, sizeof(face)) && face[0]) {
                        next_st.family = resolve_font_family(face, next_st.family);
                    }
                    if (extract_attr(tag_inner, inner_len, "color", col, sizeof(col)) && col[0]) {
                        uint16_t c = 0;
                        if (parse_css_color(col, c)) next_st.fg_rgb565 = c;
                    }
                }

                // Apply stylesheet rules matching tag, .class, #id
                apply_matching_rules(sheet, tag_name, class_attr, id_attr, next_st, blk_elem);

                // Apply inline style="..."
                if (style_attr[0]) {
                    CssPropSet inline_props{};
                    parse_css_declarations(sheet, style_attr, std::strlen(style_attr), inline_props, nullptr);
                    apply_prop_set(inline_props, next_st, blk_elem);
                }

                if (std::strcmp(tag_name, "body") == 0 || std::strcmp(tag_name, "html") == 0) {
                    if (next_st.block_has_bg) {
                        doc->doc_bg_rgb565 = next_st.block_bg_rgb565;
                        doc->has_doc_bg = true;
                        next_st.block_has_bg = false;
                    }
                    doc->doc_fg_rgb565 = next_st.fg_rgb565;
                    doc->base_family = next_st.family;
                    doc->base_size_px = next_st.size_px;
                    stack[0].style = next_st;
                }

                // Handle structural tags
                if (std::strcmp(tag_name, "br") == 0) {
                    append_span("\n", 1, next_st, true);
                } else if (std::strcmp(tag_name, "hr") == 0) {
                    flush_block();
                    init_block(BlockType::hr, next_st);
                    flush_block();
                } else if (std::strcmp(tag_name, "ul") == 0 || std::strcmp(tag_name, "ol") == 0) {
                    flush_block();
                    if (list_depth < 7) {
                        list_ordered[list_depth] = (std::strcmp(tag_name, "ol") == 0);
                        list_counter[list_depth] = 0;
                        ++list_depth;
                    }
                } else if (std::strcmp(tag_name, "li") == 0) {
                    flush_block();
                    init_block(BlockType::list_item, next_st);
                    const uint8_t d = (list_depth > 0) ? static_cast<uint8_t>(list_depth - 1) : 0;
                    cur_block.list_depth = d;
                    if (list_depth > 0 && list_ordered[d]) {
                        cur_block.ordered_index = ++list_counter[d];
                    } else {
                        cur_block.ordered_index = 0;
                    }
                    if (id_attr[0]) std::snprintf(cur_block.anchor_id, sizeof(cur_block.anchor_id), "%.47s", id_attr);
                } else if (tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= '6' && tag_name[2] == '\0') {
                    flush_block();
                    init_block(BlockType::heading, next_st);
                    cur_block.heading_level = static_cast<uint8_t>(tag_name[1] - '0');
                    cur_block.margin_top_px = (cur_block.heading_level <= 2) ? 14 : 10;
                    cur_block.margin_bottom_px = 8;
                    if (id_attr[0]) std::snprintf(cur_block.anchor_id, sizeof(cur_block.anchor_id), "%.47s", id_attr);
                } else if (std::strcmp(tag_name, "pre") == 0) {
                    flush_block();
                    init_block(BlockType::code_block, next_st);
                    cur_block.padding_px = 10;
                    in_pre = true;
                    if (id_attr[0]) std::snprintf(cur_block.anchor_id, sizeof(cur_block.anchor_id), "%.47s", id_attr);
                } else if (std::strcmp(tag_name, "blockquote") == 0 || std::strcmp(tag_name, "aside") == 0) {
                    flush_block();
                    init_block(BlockType::blockquote, next_st);
                    cur_block.padding_px = 12;
                    if (id_attr[0]) std::snprintf(cur_block.anchor_id, sizeof(cur_block.anchor_id), "%.47s", id_attr);
                } else if (std::strcmp(tag_name, "tr") == 0) {
                    flush_block();
                    init_block(BlockType::table_row, next_st);
                    in_table_row = true;
                    in_table_cell = false;
                    active_cell_idx = -1;
                } else if (in_table_row && (std::strcmp(tag_name, "td") == 0 || std::strcmp(tag_name, "th") == 0)) {
                    if (cur_block.cell_count < kMaxTableCols) {
                        active_cell_idx = static_cast<int>(cur_block.cell_count++);
                        HtmlTableCell& cell = cur_block.cells[active_cell_idx];
                        cell.span_start = static_cast<uint16_t>(doc->span_count);
                        cell.span_count = 0;
                        cell.is_header = (std::strcmp(tag_name, "th") == 0);
                        cell.align = next_st.align;
                        cell.has_bg = next_st.block_has_bg;
                        cell.bg_rgb565 = next_st.block_bg_rgb565;
                        in_table_cell = true;
                    }
                } else if (is_block_tag(tag_name)) {
                    flush_block();
                    const BlockType bt = (next_st.block_has_border_left ||
                                          ascii_contains_ci(class_attr, "callout") ||
                                          ascii_contains_ci(class_attr, "alert") ||
                                          ascii_contains_ci(class_attr, "note"))
                                             ? BlockType::blockquote
                                             : BlockType::paragraph;
                    init_block(bt, next_st);
                    if (id_attr[0]) std::snprintf(cur_block.anchor_id, sizeof(cur_block.anchor_id), "%.47s", id_attr);
                } else if (id_attr[0] && in_block && !cur_block.anchor_id[0]) {
                    std::snprintf(cur_block.anchor_id, sizeof(cur_block.anchor_id), "%.47s", id_attr);
                }

                // Push onto style stack if not void element
                if (std::strcmp(tag_name, "br") != 0 && std::strcmp(tag_name, "hr") != 0 &&
                    std::strcmp(tag_name, "img") != 0 && std::strcmp(tag_name, "meta") != 0 &&
                    std::strcmp(tag_name, "link") != 0 && std::strcmp(tag_name, "input") != 0 &&
                    stack_depth < kMaxStack) {
                    std::snprintf(stack[stack_depth].tag, sizeof(stack[stack_depth].tag), "%.15s", tag_name);
                    stack[stack_depth].style = next_st;
                    ++stack_depth;
                }
            }
            pos = gt + 1;
        } else {
            // Text node up to next '<'
            size_t next_lt = pos;
            while (next_lt < html_len && html[next_lt] != '<') ++next_lt;
            const size_t tlen = next_lt - pos;

            // Check if text node is purely whitespace when not in a block yet
            bool all_ws = true;
            for (size_t k = 0; k < tlen; ++k) {
                if (!std::isspace(static_cast<unsigned char>(html[pos + k]))) {
                    all_ws = false;
                    break;
                }
            }

            if (!all_ws || (in_block && (in_pre || cur_block.span_count > 0))) {
                const uint16_t prev_spans = static_cast<uint16_t>(doc->span_count);
                append_span(html + pos, tlen, stack[stack_depth - 1].style, in_pre);
                if (in_table_row && in_table_cell && active_cell_idx >= 0 &&
                    doc->span_count > prev_spans) {
                    cur_block.cells[active_cell_idx].span_count +=
                        static_cast<uint16_t>(doc->span_count - prev_spans);
                }
            }
            pos = next_lt;
        }
    }

    flush_block();
    std::free(sheet_ptr);
    return true;
}

}  // namespace tab5_html

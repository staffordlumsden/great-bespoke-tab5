#include "fileserver_ui.hpp"
#include "fonts/pixel_fonts.h"
#include "fs_core.hpp"
#include "tab5_hal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tab5_fs {

namespace {

using cyd_fonts::Font;
using cyd_fonts::Glyph;

struct Theme {
    uint32_t bg, surface, surface_pressed, border;
    uint32_t text, muted, disabled;
};

constexpr Theme kDark{
    0x101215, 0x1d2228, 0x2a3139, 0x353d46,
    0xf7f8fa, 0x9ba6b2, 0x59636f,
};
constexpr Theme kLight{
    0xeef1f4, 0xffffff, 0xdde2e8, 0xc8cfd7,
    0x15181c, 0x59636f, 0xa4acb5,
};

constexpr uint32_t kBlue = 0x3b82f6;
constexpr uint32_t kOrange = 0xf59e0b;
constexpr uint32_t kRed = 0xef4444;
constexpr uint32_t kGreen = 0x22c55e;
constexpr uint32_t kYellow = 0xeab308;

bool within(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    tab5_display_rect(x, y, w, h, color);
}

void box(int x, int y, int w, int h, uint32_t fill, uint32_t edge, int bw = 2) {
    rect(x, y, w, h, fill);
    rect(x, y, w, bw, edge);
    rect(x, y + h - bw, w, bw, edge);
    rect(x, y, bw, h, edge);
    rect(x + w - bw, y, bw, h, edge);
}

int text_width(const Font& font, const char* s, int scale = 1) {
    if (!s) return 0;
    int w = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
        unsigned char c = *p;
        if (c < font.first || c > font.last) c = '?';
        w += font.glyphs[c - font.first].advance * scale;
    }
    return w;
}

void draw_char(const Font& font, int x, int y, char ch, uint32_t color, int scale) {
    unsigned char code = static_cast<unsigned char>(ch);
    if (code < font.first || code > font.last) code = '?';
    const Glyph& g = font.glyphs[code - font.first];
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
                     (col - run_start) * scale, scale, color);
                run_start = -1;
            }
        }
    }
}

void draw_text(const Font& font, int x, int y, const char* s, uint32_t color, int scale = 1, int max_w = 0) {
    if (!s) return;
    int cx = x;
    const int limit = max_w > 0 ? x + max_w : 1280;
    for (const char* p = s; *p; ++p) {
        unsigned char code = static_cast<unsigned char>(*p);
        if (code < font.first || code > font.last) code = '?';
        const int adv = font.glyphs[code - font.first].advance * scale;
        if (cx + adv > limit) break;
        draw_char(font, cx, y, *p, color, scale);
        cx += adv;
    }
}

void draw_centered(const Font& font, int rx, int ry, int rw, int rh,
                   const char* s, uint32_t color, int scale = 1) {
    const int tw = text_width(font, s, scale);
    const int th = font.ascent * scale;
    const int tx = rx + std::max(4, (rw - tw) / 2);
    const int ty = ry + std::max(2, (rh - th) / 2);
    draw_text(font, tx, ty, s, color, scale, rw - 8);
}

void draw_theme_icon(int cx, int cy, bool dark_mode, uint32_t color) {
    if (dark_mode) {
        // Sun icon
        rect(cx - 5, cy - 5, 10, 10, color);
        rect(cx - 1, cy - 11, 2, 4, color);
        rect(cx - 1, cy + 7, 2, 4, color);
        rect(cx - 11, cy - 1, 4, 2, color);
        rect(cx + 7, cy - 1, 4, 2, color);
    } else {
        // Crescent icon
        rect(cx - 6, cy - 8, 12, 16, color);
        rect(cx - 1, cy - 6, 8, 12, kLight.surface);
    }
}

void draw_top_tabs(const Theme& t) {
    static const char* const kLabels[] = {
        "Console", "Terminal", "Ollama", "Files", "Web Server", "Network"
    };
    constexpr int kCount = 6;
    for (int i = 0; i < kCount; ++i) {
        const int x = i * 1280 / kCount;
        const int right = (i + 1) * 1280 / kCount;
        const int w = right - x;
        const bool active = (i == 3);
        const bool is_web = (i == 4);
        const bool is_term = (i == 1);
        const bool is_ollama = (i == 2);
        const bool is_console = (i == 0);

        uint32_t fill = active ? t.surface_pressed : t.surface;
        uint32_t edge = t.border;
        uint32_t fg = t.disabled;

        if (active) {
            edge = kBlue;
            fg = t.text;
        } else if (is_web) {
            edge = kGreen;
            fg = kGreen;
        } else if (is_term) {
            edge = kRed;
            fg = kRed;
        } else if (is_ollama) {
            edge = kOrange;
            fg = kOrange;
        } else if (is_console) {
            fg = t.muted;
        }

        box(x, 0, w, 46, fill, edge, 2);
        draw_centered(active ? cyd_fonts::pixelfont_13x7_semibold : cyd_fonts::pixelfont_11x5_thin,
                      x, 0, w, 46, kLabels[i], fg, 2);
    }
}

void draw_header(const UiState& state, const Theme& t) {
    rect(0, 46, 1280, 82, t.bg);
    draw_top_tabs(t);

    draw_text(cyd_fonts::pixelfont_13x7_semibold, 24, 62, "TAB5 File Server", t.text, 2);
    draw_text(cyd_fonts::pixelfont_11x5_thin, 24, 96, "0.1.0 - SD CARD WEB EXPLORER & HOTSPOT", t.muted, 2);

    // Battery status
    char batt_buf[32];
    const int pct = std::clamp<int>(state.battery_percent, 0, 100);
    std::snprintf(batt_buf, sizeof(batt_buf), "%s %d%% (%.2fV)",
                  state.charging ? "CHG" : "BAT", pct, state.battery_voltage);
    const uint32_t batt_color = state.charging ? kGreen : (pct <= 20 ? kRed : t.text);
    draw_text(cyd_fonts::pixelfont_11x5_thin, 680, 66, batt_buf, batt_color, 2);

    // Date & Time
    draw_text(cyd_fonts::pixelfont_13x7_semibold, 960, 62, state.time_str, t.text, 2);
    draw_text(cyd_fonts::pixelfont_11x5_thin, 960, 96, state.date_str, t.muted, 2);

    // Theme button (top-right: x=1196, y=60, w=60, h=54)
    box(1196, 60, 60, 54, t.surface, t.border, 2);
    draw_theme_icon(1226, 87, state.dark_mode, t.text);

    rect(0, 126, 1280, 2, t.border);
}

void draw_cards(const UiState& state, const Theme& t) {
    rect(0, 128, 1280, 514, t.bg);

    // =========================================================================
    // CARD 1: NETWORK MODE & ENDPOINT (Left: x=24, y=136, w=604, h=264)
    // =========================================================================
    const bool is_hotspot = (state.net.mode == NetMode::hotspot);
    box(24, 136, 604, 264, t.surface, is_hotspot ? kGreen : kBlue, 2);
    draw_text(cyd_fonts::pixelfont_13x7_semibold, 44, 150, "1. NETWORK MODE & ACCESS URL", t.text, 2);

    // Mode switch buttons
    box(44, 184, 272, 46, !is_hotspot ? t.surface_pressed : t.bg, !is_hotspot ? kBlue : t.border, 2);
    draw_centered(cyd_fonts::pixelfont_13x7_semibold, 44, 184, 272, 46,
                  "LAN WIFI MODE", !is_hotspot ? kBlue : t.muted, 2);

    box(332, 184, 276, 46, is_hotspot ? t.surface_pressed : t.bg, is_hotspot ? kGreen : t.border, 2);
    draw_centered(cyd_fonts::pixelfont_13x7_semibold, 332, 184, 276, 46,
                  "WIFI HOTSPOT", is_hotspot ? kGreen : t.muted, 2);

    if (is_hotspot) {
        char ssid_line[64];
        std::snprintf(ssid_line, sizeof(ssid_line), "HOTSPOT SSID: %s", state.net.ap_ssid);
        draw_text(cyd_fonts::pixelfont_11x5_thin, 44, 244, ssid_line, t.text, 2);

        char clients_line[64];
        std::snprintf(clients_line, sizeof(clients_line), "CONNECTED CLIENTS: %u",
                      static_cast<unsigned>(state.net.ap_clients));
        draw_text(cyd_fonts::pixelfont_11x5_thin, 390, 244, clients_line,
                  state.net.ap_clients > 0 ? kGreen : t.muted, 2);

        box(44, 274, 564, 52, t.bg, kGreen, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 44, 274, 564, 52,
                      "OPEN HTTP://192.168.4.1/ OR HTTP://TAB5.LOCAL/", kGreen, 2);

        draw_text(cyd_fonts::pixelfont_11x5_thin, 44, 344,
                  "JOIN HOTSPOT USING THE 8-DIGIT PASSCODE ON THE RIGHT ->", t.muted, 2);
        draw_text(cyd_fonts::pixelfont_11x5_thin, 44, 370,
                  "ENTER THAT SAME 8-DIGIT CODE IN THE BROWSER EXPLORER.", t.muted, 2);
    } else {
        char ssid_line[80];
        if (state.net.sta_connected && state.net.sta_has_ip) {
            std::snprintf(ssid_line, sizeof(ssid_line), "CONNECTED SSID: %s (%d dBm)",
                          state.net.sta_ssid, static_cast<int>(state.net.sta_rssi));
            draw_text(cyd_fonts::pixelfont_11x5_thin, 44, 244, ssid_line, kGreen, 2);
        } else if (state.net.sta_ssid[0]) {
            std::snprintf(ssid_line, sizeof(ssid_line), "TARGET SSID: %s (CONNECTING...)", state.net.sta_ssid);
            draw_text(cyd_fonts::pixelfont_11x5_thin, 44, 244, ssid_line, kOrange, 2);
        } else {
            draw_text(cyd_fonts::pixelfont_11x5_thin, 44, 244, "NO LAN WIFI CONNECTED - TAP SCAN WIFI", kRed, 2);
        }

        char url_line[80];
        if (state.net.sta_has_ip && state.net.ip_address[0]) {
            std::snprintf(url_line, sizeof(url_line), "HTTP://%s/  OR  HTTP://TAB5.LOCAL/", state.net.ip_address);
        } else {
            std::snprintf(url_line, sizeof(url_line), "WAITING FOR LAN IP ADDRESS...");
        }
        box(44, 274, 564, 50, t.bg, state.net.sta_has_ip ? kBlue : t.border, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 44, 274, 564, 50,
                      url_line, state.net.sta_has_ip ? kBlue : t.muted, 2);

        box(44, 336, 272, 48, t.surface_pressed, kBlue, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 44, 336, 272, 48, "SCAN / PICK WIFI", t.text, 2);

        box(332, 336, 276, 48, t.surface_pressed, t.border, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 332, 336, 276, 48, "RECONNECT SAVED", t.text, 2);
    }

    // =========================================================================
    // CARD 2: 8-DIGIT PASSCODE (Right: x=652, y=136, w=604, h=264)
    // =========================================================================
    box(652, 136, 604, 264, t.surface, kOrange, 2);
    draw_text(cyd_fonts::pixelfont_13x7_semibold, 672, 150, "2. 8-DIGIT SECURITY PASSCODE", t.text, 2);
    draw_text(cyd_fonts::pixelfont_11x5_thin, 672, 182,
              "HOTSPOT WPA2 PASSWORD & WEB FILE EXPLORER LOGIN", t.muted, 2);

    char formatted_pin[12];
    format_passcode_display(state.passcode, formatted_pin);
    box(672, 212, 564, 104, t.bg, kOrange, 2);
    draw_centered(cyd_fonts::pixelfont_13x7_semibold, 672, 212, 564, 104,
                  formatted_pin, kOrange, 4);

    box(672, 332, 564, 52, t.surface_pressed, t.border, 2);
    draw_centered(cyd_fonts::pixelfont_13x7_semibold, 672, 332, 564, 52,
                  "GENERATE NEW 8-DIGIT PASSCODE", t.text, 2);

    // =========================================================================
    // CARD 3: SD CARD ROOT & HTTP TELEMETRY (Bottom: x=24, y=416, w=1232, h=212)
    // =========================================================================
    box(24, 416, 1232, 212, t.surface, t.border, 2);

    // Left column: SD Root
    draw_text(cyd_fonts::pixelfont_13x7_semibold, 44, 432, "SD CARD ROOT (/sdcard)", t.text, 2);
    if (state.sd_ready && state.sd_total > 0) {
        const uint64_t used = (state.sd_total >= state.sd_free) ? (state.sd_total - state.sd_free) : 0;
        char total_s[24], free_s[24], used_s[24];
        format_bytes(state.sd_total, total_s, sizeof(total_s));
        format_bytes(state.sd_free, free_s, sizeof(free_s));
        format_bytes(used, used_s, sizeof(used_s));

        char cap_line[96];
        std::snprintf(cap_line, sizeof(cap_line), "USED %s / %s  (FREE: %s)", used_s, total_s, free_s);
        draw_text(cyd_fonts::pixelfont_11x5_thin, 44, 468, cap_line, t.muted, 2);

        // Progress bar
        box(44, 498, 564, 24, t.bg, t.border, 2);
        const int fill_w = static_cast<int>((used * 560ULL) / state.sd_total);
        if (fill_w > 0) {
            rect(46, 500, std::min(560, fill_w), 20, kBlue);
        }

        char items_line[80];
        std::snprintf(items_line, sizeof(items_line), "SD ROOT DIRECTORY: %u FOLDERS, %u FILES",
                      static_cast<unsigned>(state.root_dirs), static_cast<unsigned>(state.root_files));
        draw_text(cyd_fonts::pixelfont_11x5_thin, 44, 536, items_line, t.text, 2);
    } else {
        draw_text(cyd_fonts::pixelfont_13x7_semibold, 44, 480, "SD CARD NOT MOUNTED", kRed, 2);
    }
    draw_text(cyd_fonts::pixelfont_11x5_thin, 44, 574,
              "DEFAULT: DOWNLOAD TO CLIENT  |  HTML (.HTML/.HTM): STREAM INLINE", kGreen, 2);

    // Vertical divider
    rect(636, 432, 2, 180, t.border);

    // Right column: Live HTTP telemetry
    draw_text(cyd_fonts::pixelfont_13x7_semibold, 656, 432, "HTTP FILE EXPLORER TELEMETRY", t.text, 2);

    char counts_line1[80];
    std::snprintf(counts_line1, sizeof(counts_line1), "REQUESTS: %u    DOWNLOADS: %u",
                  static_cast<unsigned>(state.http.requests), static_cast<unsigned>(state.http.downloads));
    draw_text(cyd_fonts::pixelfont_11x5_thin, 656, 468, counts_line1, t.text, 2);

    char counts_line2[80];
    std::snprintf(counts_line2, sizeof(counts_line2), "HTML STREAMS: %u    UPLOADS: %u",
                  static_cast<unsigned>(state.http.html_streams), static_cast<unsigned>(state.http.uploads));
    draw_text(cyd_fonts::pixelfont_11x5_thin, 656, 498, counts_line2, t.text, 2);

    char xfer_s[24];
    format_bytes(state.http.bytes_transferred, xfer_s, sizeof(xfer_s));
    char xfer_line[64];
    std::snprintf(xfer_line, sizeof(xfer_line), "DATA TRANSFERRED: %s", xfer_s);
    draw_text(cyd_fonts::pixelfont_11x5_thin, 656, 528, xfer_line, kBlue, 2);

    box(656, 560, 580, 48, t.bg, t.border, 2);
    draw_text(cyd_fonts::pixelfont_11x5_thin, 668, 573,
              state.http.last_event[0] ? state.http.last_event : "WAITING FOR CLIENT CONNECTION...",
              t.muted, 2, 556);
}

void draw_footer(const UiState& state, const Theme& t) {
    rect(0, 642, 1280, 78, t.surface);
    rect(0, 642, 1280, 2, t.border);

    const char* msg = state.footer_notice[0] ? state.footer_notice : state.net.status_msg;
    draw_text(cyd_fonts::pixelfont_13x7_semibold, 24, 668, msg, t.text, 2, 840);

    box(880, 656, 176, 50, t.surface_pressed, kBlue, 2);
    draw_centered(cyd_fonts::pixelfont_13x7_semibold, 880, 656, 176, 50, "WIFI SCAN", kBlue, 2);

    box(1072, 656, 184, 50, t.surface_pressed, t.border, 2);
    draw_centered(cyd_fonts::pixelfont_13x7_semibold, 1072, 656, 184, 50, "CONSOLE", t.text, 2);
}

const char* const kRowsLower[4] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._/"};
const char* const kRowsUpper[4] = {"!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL_", "ZXCVBNM:?+"};
const char* const kRowsSym[4]   = {"1234567890", "!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/?"};

const char* active_kb_row(const UiState& state, int r) {
    if (state.pass_symbols) return kRowsSym[r];
    return state.pass_shift ? kRowsUpper[r] : kRowsLower[r];
}

void draw_wifi_modal(const UiState& state, const Theme& t) {
    box(24, 136, 1232, 500, t.surface, kBlue, 2);

    if (!state.wifi_pass_mode) {
        draw_text(cyd_fonts::pixelfont_13x7_semibold, 44, 152, "SELECT 2.4 GHZ LAN WIFI NETWORK", t.text, 2);
        draw_text(cyd_fonts::pixelfont_11x5_thin, 44, 184, state.net.status_msg, t.muted, 2, 820);

        box(920, 148, 148, 48, t.surface_pressed, kBlue, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 920, 148, 148, 48, "RESCAN", kBlue, 2);

        box(1084, 148, 148, 48, t.surface_pressed, t.border, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 1084, 148, 148, 48, "CLOSE", t.text, 2);

        const size_t total = state.net.scan_count;
        const size_t shown = (state.wifi_scroll < total) ? std::min<size_t>(8, total - state.wifi_scroll) : 0;
        for (size_t i = 0; i < shown; ++i) {
            ScannedNetwork net{};
            if (!net_get_scanned_network(state.wifi_scroll + i, &net)) continue;
            const int ry = 212 + static_cast<int>(i) * 48;
            box(44, ry, 1060, 42, t.bg, t.border, 2);
            draw_text(cyd_fonts::pixelfont_13x7_semibold, 60, ry + 8, net.ssid, t.text, 2, 740);
            char info[48];
            std::snprintf(info, sizeof(info), "%s  %d dBm", net.secure ? "WPA2" : "OPEN", static_cast<int>(net.rssi));
            draw_text(cyd_fonts::pixelfont_11x5_thin, 860, ry + 10, info, t.muted, 2);
        }
        if (shown == 0) {
            draw_centered(cyd_fonts::pixelfont_13x7_semibold, 44, 300, 1060, 60,
                          state.net.scanning ? "SCANNING FOR 2.4 GHZ NETWORKS..." : "NO NETWORKS LISTED - TAP RESCAN",
                          t.muted, 2);
        }

        box(1124, 212, 108, 180, t.surface_pressed, t.border, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 1124, 212, 108, 180, "UP", t.text, 2);
        box(1124, 412, 108, 180, t.surface_pressed, t.border, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 1124, 412, 108, 180, "DOWN", t.text, 2);
    } else {
        char title[80];
        std::snprintf(title, sizeof(title), "PASSWORD FOR %s", state.selected_ssid);
        draw_text(cyd_fonts::pixelfont_13x7_semibold, 44, 152, title, t.text, 2, 860);

        box(920, 148, 148, 48, t.surface_pressed, t.border, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 920, 148, 148, 48, "BACK", t.text, 2);

        box(1084, 148, 148, 48, t.surface_pressed, kGreen, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 1084, 148, 148, 48, "CONNECT", kGreen, 2);

        box(44, 210, 1188, 54, t.bg, kBlue, 2);
        draw_text(cyd_fonts::pixelfont_13x7_semibold, 60, 224,
                  state.entered_pass[0] ? state.entered_pass : "TYPE WIFI PASSWORD...",
                  state.entered_pass[0] ? t.text : t.muted, 2, 1150);

        for (int r = 0; r < 4; ++r) {
            const char* row = active_kb_row(state, r);
            for (int c = 0; c < 10; ++c) {
                const int kx = 44 + c * 118;
                const int ky = 280 + r * 64;
                char label[2] = {row[c], '\0'};
                box(kx, ky, 110, 56, t.surface_pressed, t.border, 2);
                draw_centered(cyd_fonts::pixelfont_13x7_semibold, kx, ky, 110, 56, label, t.text, 2);
            }
        }

        // Bottom keyboard control row (y = 540)
        box(44, 544, 180, 56, state.pass_shift ? t.surface_pressed : t.bg, state.pass_shift ? kBlue : t.border, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 44, 544, 180, 56, "SHIFT", t.text, 2);

        box(238, 544, 180, 56, state.pass_symbols ? t.surface_pressed : t.bg, state.pass_symbols ? kBlue : t.border, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 238, 544, 180, 56, "SYM", t.text, 2);

        box(432, 544, 400, 56, t.surface_pressed, t.border, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 432, 544, 400, 56, "SPACE", t.text, 2);

        box(846, 544, 186, 56, t.surface_pressed, kOrange, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 846, 544, 186, 56, "DEL", kOrange, 2);

        box(1046, 544, 186, 56, t.surface_pressed, kRed, 2);
        draw_centered(cyd_fonts::pixelfont_13x7_semibold, 1046, 544, 186, 56, "CLEAR", kRed, 2);
    }
}

}  // namespace

void ui_render(const UiState& state, unsigned dirty) {
    if (!dirty) return;
    const Theme& t = state.dark_mode ? kDark : kLight;

    if (dirty & DIRTY_HEADER) {
        draw_header(state, t);
    }
    if (dirty & (DIRTY_CARDS | DIRTY_MODAL)) {
        if (state.wifi_modal_open) {
            rect(0, 128, 1280, 514, t.bg);
            draw_wifi_modal(state, t);
        } else {
            draw_cards(state, t);
        }
    }
    if (dirty & DIRTY_FOOTER) {
        draw_footer(state, t);
    }

    if (dirty == DIRTY_ALL || (dirty & (DIRTY_CARDS | DIRTY_MODAL))) {
        tab5_display_present();
    } else {
        if (dirty & DIRTY_HEADER) tab5_display_present_region(0, 0, 1280, 128);
        if (dirty & DIRTY_FOOTER) tab5_display_present_region(0, 642, 1280, 78);
    }
}

UiAction ui_handle_tap(int x, int y, UiState& state, unsigned& out_dirty) {
    out_dirty = DIRTY_NONE;

    // Top tab 0 ("Console") returns to Console
    if (within(x, y, 0, 0, 1280 / 6, 46)) {
        return UiAction::return_console;
    }

    // Theme toggle button in header
    if (within(x, y, 1196, 60, 60, 54)) {
        return UiAction::toggle_theme;
    }

    // Footer buttons
    if (within(x, y, 880, 656, 176, 50)) {
        return UiAction::open_wifi_modal;
    }
    if (within(x, y, 1072, 656, 184, 50)) {
        return UiAction::return_console;
    }

    // If Wi-Fi modal is open, handle modal touches
    if (state.wifi_modal_open) {
        if (!state.wifi_pass_mode) {
            if (within(x, y, 920, 148, 148, 48)) {
                net_start_scan();
                out_dirty = DIRTY_MODAL | DIRTY_FOOTER;
                return UiAction::none;
            }
            if (within(x, y, 1084, 148, 148, 48)) {
                state.wifi_modal_open = false;
                out_dirty = DIRTY_CARDS | DIRTY_FOOTER;
                return UiAction::none;
            }
            if (within(x, y, 1124, 212, 108, 180)) {
                state.wifi_scroll = (state.wifi_scroll >= 8) ? (state.wifi_scroll - 8) : 0;
                out_dirty = DIRTY_MODAL;
                return UiAction::none;
            }
            if (within(x, y, 1124, 412, 108, 180)) {
                if (state.wifi_scroll + 8 < state.net.scan_count) state.wifi_scroll += 8;
                out_dirty = DIRTY_MODAL;
                return UiAction::none;
            }
            const size_t total = state.net.scan_count;
            const size_t shown = (state.wifi_scroll < total) ? std::min<size_t>(8, total - state.wifi_scroll) : 0;
            for (size_t i = 0; i < shown; ++i) {
                const int ry = 212 + static_cast<int>(i) * 48;
                if (within(x, y, 44, ry, 1060, 42)) {
                    ScannedNetwork net{};
                    if (!net_get_scanned_network(state.wifi_scroll + i, &net)) break;
                    std::snprintf(state.selected_ssid, sizeof(state.selected_ssid), "%s", net.ssid);
                    if (net.secure) {
                        state.wifi_pass_mode = true;
                        state.entered_pass[0] = '\0';
                        state.pass_shift = false;
                        state.pass_symbols = false;
                        out_dirty = DIRTY_MODAL;
                    } else {
                        state.wifi_modal_open = false;
                        net_connect_lan(net.ssid, "");
                        out_dirty = DIRTY_CARDS | DIRTY_FOOTER;
                    }
                    return UiAction::none;
                }
            }
        } else {
            // Back
            if (within(x, y, 920, 148, 148, 48)) {
                state.wifi_pass_mode = false;
                out_dirty = DIRTY_MODAL;
                return UiAction::none;
            }
            // Connect
            if (within(x, y, 1084, 148, 148, 48)) {
                if (std::strlen(state.entered_pass) >= 8) {
                    state.wifi_pass_mode = false;
                    state.wifi_modal_open = false;
                    net_connect_lan(state.selected_ssid, state.entered_pass);
                    std::memset(state.entered_pass, 0, sizeof(state.entered_pass));
                    out_dirty = DIRTY_CARDS | DIRTY_FOOTER;
                } else {
                    std::snprintf(state.footer_notice, sizeof(state.footer_notice),
                                  "WIFI PASSWORD MUST BE AT LEAST 8 CHARACTERS");
                    out_dirty = DIRTY_FOOTER;
                }
                return UiAction::none;
            }
            // Character keys
            for (int r = 0; r < 4; ++r) {
                const char* row = active_kb_row(state, r);
                for (int c = 0; c < 10; ++c) {
                    const int kx = 44 + c * 118;
                    const int ky = 280 + r * 64;
                    if (within(x, y, kx, ky, 110, 56)) {
                        const size_t len = std::strlen(state.entered_pass);
                        if (len + 1 < sizeof(state.entered_pass)) {
                            state.entered_pass[len] = row[c];
                            state.entered_pass[len + 1] = '\0';
                            out_dirty = DIRTY_MODAL;
                        }
                        return UiAction::none;
                    }
                }
            }
            // Control keys
            if (within(x, y, 44, 544, 180, 56)) {
                state.pass_shift = !state.pass_shift;
                out_dirty = DIRTY_MODAL;
            } else if (within(x, y, 238, 544, 180, 56)) {
                state.pass_symbols = !state.pass_symbols;
                out_dirty = DIRTY_MODAL;
            } else if (within(x, y, 432, 544, 400, 56)) {
                const size_t len = std::strlen(state.entered_pass);
                if (len + 1 < sizeof(state.entered_pass)) {
                    state.entered_pass[len] = ' ';
                    state.entered_pass[len + 1] = '\0';
                    out_dirty = DIRTY_MODAL;
                }
            } else if (within(x, y, 846, 544, 186, 56)) {
                const size_t len = std::strlen(state.entered_pass);
                if (len > 0) {
                    state.entered_pass[len - 1] = '\0';
                    out_dirty = DIRTY_MODAL;
                }
            } else if (within(x, y, 1046, 544, 186, 56)) {
                state.entered_pass[0] = '\0';
                out_dirty = DIRTY_MODAL;
            }
        }
        return UiAction::none;
    }

    // Card 1: LAN vs Hotspot buttons
    if (within(x, y, 44, 184, 272, 46)) {
        return UiAction::mode_lan;
    }
    if (within(x, y, 332, 184, 276, 46)) {
        return UiAction::mode_hotspot;
    }

    if (state.net.mode == NetMode::lan) {
        if (within(x, y, 44, 336, 272, 48)) {
            return UiAction::open_wifi_modal;
        }
        if (within(x, y, 332, 336, 276, 48)) {
            return UiAction::reconnect_lan;
        }
    }

    // Card 2: Generate new 8-digit passcode button
    if (within(x, y, 672, 332, 564, 52)) {
        return UiAction::new_passcode;
    }

    return UiAction::none;
}

bool ui_handle_key(uint8_t key, UiState& state, unsigned& out_dirty) {
    out_dirty = DIRTY_NONE;
    if (!state.wifi_modal_open || !state.wifi_pass_mode) return false;

    if (key == '\r' || key == '\n') {
        if (std::strlen(state.entered_pass) >= 8) {
            state.wifi_pass_mode = false;
            state.wifi_modal_open = false;
            net_connect_lan(state.selected_ssid, state.entered_pass);
            std::memset(state.entered_pass, 0, sizeof(state.entered_pass));
            out_dirty = DIRTY_CARDS | DIRTY_FOOTER;
        }
        return true;
    }
    if (key == 27) {
        state.wifi_pass_mode = false;
        out_dirty = DIRTY_MODAL;
        return true;
    }
    if (key == 8 || key == 127) {
        const size_t len = std::strlen(state.entered_pass);
        if (len > 0) {
            state.entered_pass[len - 1] = '\0';
            out_dirty = DIRTY_MODAL;
        }
        return true;
    }
    if (key >= 32 && key <= 126) {
        const size_t len = std::strlen(state.entered_pass);
        if (len + 1 < sizeof(state.entered_pass)) {
            state.entered_pass[len] = static_cast<char>(key);
            state.entered_pass[len + 1] = '\0';
            out_dirty = DIRTY_MODAL;
        }
        return true;
    }
    return false;
}

}  // namespace tab5_fs

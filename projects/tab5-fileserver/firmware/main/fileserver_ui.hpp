#pragma once

#include "fileserver_http.hpp"
#include "fileserver_net.hpp"
#include <cstddef>
#include <cstdint>

namespace tab5_fs {

enum class UiAction {
    none,
    return_console,
    toggle_theme,
    mode_lan,
    mode_hotspot,
    new_passcode,
    open_wifi_modal,
    reconnect_lan,
};

enum DirtyRegion : unsigned {
    DIRTY_NONE = 0,
    DIRTY_HEADER = 1 << 0,
    DIRTY_CARDS = 1 << 1,
    DIRTY_FOOTER = 1 << 2,
    DIRTY_MODAL = 1 << 3,
    DIRTY_ALL = DIRTY_HEADER | DIRTY_CARDS | DIRTY_FOOTER,
};

struct UiState {
    bool dark_mode = true;
    bool clock_valid = false;
    char date_str[32] = "Date unavailable";
    char time_str[16] = "--:--";
    float battery_voltage = 0.0f;
    uint8_t battery_percent = 0;
    bool charging = false;

    bool sd_ready = false;
    uint64_t sd_total = 0;
    uint64_t sd_free = 0;
    size_t root_dirs = 0;
    size_t root_files = 0;

    char passcode[9] = "12345678";
    NetStatus net{};
    HttpStats http{};
    char footer_notice[72] = {0};

    // Wi-Fi picker modal state
    bool wifi_modal_open = false;
    bool wifi_pass_mode = false;
    bool pass_shift = false;
    bool pass_symbols = false;
    size_t wifi_scroll = 0;
    char selected_ssid[kSsidMaxLen] = {0};
    char entered_pass[65] = {0};
};

void ui_render(const UiState& state, unsigned dirty);

// Handles a touch tap at (x, y). May mutate modal state inside UiState and return a top-level UiAction.
UiAction ui_handle_tap(int x, int y, UiState& state, unsigned& out_dirty);

// Handles a physical keyboard ASCII/control key when the Wi-Fi password modal is open.
bool ui_handle_key(uint8_t key, UiState& state, unsigned& out_dirty);

}  // namespace tab5_fs

#pragma once

#include "html_core.hpp"
#include "tab5_hal.h"
#include <cstddef>
#include <cstdint>

namespace tab5_html {

enum class ViewScreen : uint8_t {
    file_picker = 0,  // Separate full-screen SD card HTML file picker
    html_reader = 1,  // Clean full-screen HTML reader with only BACK and ROTATE buttons
};

enum class PickerTab : uint8_t {
    all_html = 0,
    browse_dir = 1,
};

struct HistoryEntry {
    char rel_path[192];
    int scroll_y;
};

constexpr size_t kMaxPickerEntries = 192;
constexpr size_t kMaxHistory = 12;

struct ViewerState {
    ViewScreen screen = ViewScreen::file_picker;
    bool dark_ui = true;
    bool source_view = false;
    uint8_t rotation = 0;           // 0..3 (TAB5_ROTATION_0 .. TAB5_ROTATION_270)
    uint16_t zoom_pct = 100;        // 50 .. 250 (controlled via pinch-to-zoom)
    FontOverride font_override = FontOverride::doc_css;

    bool sd_mounted = false;
    char sd_root[32] = "/sdcard";
    char current_file_rel[192] = "";
    uint64_t current_file_size = 0;

    char* raw_html = nullptr;
    size_t raw_html_len = 0;
    size_t raw_html_cap = 0;
    HtmlDocument doc{};

    int scroll_y = 0;
    int max_scroll_y = 0;
    int total_doc_height = 0;
    bool layout_dirty = true;

    // Navigation history stack for clickable local HTML links
    HistoryEntry history[kMaxHistory]{};
    size_t history_count = 0;

    // File Picker state
    PickerTab picker_tab = PickerTab::all_html;
    bool picker_html_only = true;
    char picker_dir[192] = "/";
    PickerEntry* picker_entries = nullptr;
    size_t picker_count = 0;
    size_t picker_selected = 0;
    int picker_scroll_offset = 0;  // Entry index at top of visible list

    // Telemetry (shown on the File Picker screen)
    uint8_t battery_percent = 0;
    float battery_voltage = 0.0f;
    bool battery_charging = false;
    bool battery_valid = false;
    tab5_clock_time clock{};
    bool kbd_connected = false;

    char status_msg[96] = "SELECT AN HTML FILE FROM SD CARD";
};

enum class UiCommand : uint8_t {
    none = 0,
    return_to_console,
    rotate_step,
};

bool viewer_init(ViewerState& state) noexcept;
void viewer_refresh_picker(ViewerState& state) noexcept;
bool viewer_load_file(ViewerState& state, const char* rel_path, bool push_history = true) noexcept;
void viewer_relayout(ViewerState& state) noexcept;

void ui_render_full(ViewerState& state) noexcept;
void ui_render_header_and_footer(const ViewerState& state) noexcept;
void ui_render_viewport(ViewerState& state) noexcept;

UiCommand ui_handle_tap(ViewerState& state, int x, int y, bool& out_needs_full_redraw, bool& out_needs_viewport_redraw) noexcept;
void ui_handle_scroll_delta(ViewerState& state, int dy, bool& out_needs_viewport_redraw) noexcept;
void ui_handle_pinch_zoom(ViewerState& state, uint16_t new_zoom_pct, int focal_screen_y, int dy_pan, bool& out_needs_viewport_redraw) noexcept;
UiCommand ui_handle_key(ViewerState& state, const uint8_t* bytes, size_t len, bool& out_needs_full_redraw, bool& out_needs_viewport_redraw) noexcept;

}  // namespace tab5_html

#pragma once

#include <cstddef>
#include <cstdint>
#include "note_model.h"

namespace quicknote::ui {

enum class Action : uint8_t {
    none,
    history_toggle,  // HISTORY or EDIT
    save_or_prev,    // SAVE or PREV DAY
    clear_or_next,   // CLEAR / SURE? or NEXT DAY
    zoom_out,        // A-
    zoom_in,         // A+
    console,         // CONSOLE
};

struct UiState {
    bool history_mode{false};
    bool clear_armed{false};
    bool clock_valid{false};
    bool dirty{false};
    bool tab5_kbd_connected{false};
    bool usb_kbd_connected{false};
    bool charging{false};
    uint8_t battery_percent{100};
    float battery_voltage{8.0f};
    uint8_t font_points{10};
    int day_offset{0};

    const char* date_str{"Date unavailable"};
    const char* time_str{"--:--"};
    const char* status_msg{""};
    uint32_t status_color{0};

    size_t cursor_line{1};
    size_t cursor_col{1};
    size_t word_count{0};
    size_t char_count{0};

    size_t history_lines_viewed{0};
    size_t history_lines_total{0};
};

class Chrome {
public:
    void init();
    void draw_header(const UiState& state);
    void draw_status_bar(const UiState& state);
    void draw_toolbar(const UiState& state);
    void draw_all(const UiState& state);

    Action hit_test(int x, int y) const;
};

}  // namespace quicknote::ui

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "input_tab5_kbd.hpp"
#include "note_model.h"
#include "quicknote_ui.h"
#include "tab5_app_boot.h"
#include "tab5_hal.h"
#include "terminal_view.h"

namespace {

constexpr const char* kNotesRoot = "/sdcard/files/QuickNote";

quicknote::Editor editor{};
quicknote::terminal::View terminal_view{};
quicknote::ui::Chrome chrome{};
quicknote::ui::UiState ui_state{};

QueueHandle_t key_queue = nullptr;

bool history_mode = false;
bool clear_armed = false;
bool terminal_dirty = true;
bool chrome_dirty = true;
bool cursor_blink = true;

int day_offset = 0;
int history_scroll = 0;
char history_buffer[quicknote::editor_capacity + 1]{};
size_t history_size = 0;
size_t history_total_lines = 0;

char status_text[80]{};
uint32_t status_color = 0;
uint64_t status_until = 0;
uint64_t draft_due = 0;
uint64_t clear_until = 0;
uint64_t next_blink = 0;
uint64_t last_second = 0;

const char* const kDayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kFullDayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kFullMonthNames[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

uint64_t uptime_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

int day_of_week(int y, int m, int d) {
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

int days_from_civil(int y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

void civil_from_days(int z, int& y, unsigned& m, unsigned& d) noexcept {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
}

bool resolve_day(const tab5_clock_time& now, int offset,
                 char iso[16], char heading[64], char date_short[48]) {
    if (!now.valid || now.year < 2024 || now.year > 2099 ||
        now.month < 1 || now.month > 12 || now.day < 1 || now.day > 31) {
        return false;
    }
    int today_days = days_from_civil(now.year, now.month, now.day);
    int target_days = today_days + offset;
    int y = 0;
    unsigned m = 0, d = 0;
    civil_from_days(target_days, y, m, d);
    if (y < 2020 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31) {
        return false;
    }

    int dow = day_of_week(y, m, d);
    std::snprintf(iso, 16, "%04d-%02u-%02u", y, m, d);
    std::snprintf(heading, 64, "%s, %u %s %04d", kFullDayNames[dow], d, kFullMonthNames[m - 1], y);
    std::snprintf(date_short, 48, "%s %u %s %04d", kDayNames[dow], d, kMonthNames[m - 1], y);
    return true;
}

void set_status(const char* msg, uint32_t color = 0, unsigned duration_ms = 4000) {
    std::snprintf(status_text, sizeof(status_text), "%s", msg ? msg : "");
    status_color = color;
    status_until = duration_ms ? uptime_ms() + duration_ms : 0;
    chrome_dirty = true;
}

void save_nvs_settings() {
    nvs_handle_t handle = 0;
    if (nvs_open("qn_ui", NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u8(handle, "zoom", static_cast<uint8_t>(terminal_view.zoom()));
    nvs_commit(handle);
    nvs_close(handle);
}

void load_nvs_settings() {
    nvs_handle_t handle = 0;
    if (nvs_open("qn_ui", NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t z = 1;
    if (nvs_get_u8(handle, "zoom", &z) == ESP_OK && z <= 2) {
        terminal_view.set_zoom(static_cast<quicknote::terminal::ZoomLevel>(z));
    }
    nvs_close(handle);
}

void load_history_for_offset() {
    tab5_clock_time now{};
    tab5_rtc_read(&now);
    char iso[16], heading[64], date_short[48], err[80]{};
    history_scroll = 0;
    history_buffer[0] = 0;
    history_size = 0;
    history_total_lines = 0;

    if (!resolve_day(now, day_offset, iso, heading, date_short)) {
        set_status("Clock not set / Cannot resolve date", 0xff6b6b, 0);
        return;
    }

    bool truncated = false;
    history_size = quicknote::load_day_tail(kNotesRoot, iso, history_buffer,
                                            sizeof(history_buffer), truncated,
                                            err, sizeof(err));
    if (err[0]) {
        set_status(err, 0x93a4b7, 3000);
    } else {
        // Count total lines in history
        size_t lines = 0;
        for (size_t i = 0; i < history_size; ++i) {
            if (history_buffer[i] == '\n') ++lines;
        }
        if (history_size > 0 && history_buffer[history_size - 1] != '\n') ++lines;
        history_total_lines = lines;

        char msg[64];
        std::snprintf(msg, sizeof(msg), "Viewing %s", date_short);
        set_status(msg, 0x56a8ff, 3000);
    }
    terminal_dirty = true;
    chrome_dirty = true;
}

bool checkpoint(bool announce) {
    char err[80]{};
    if (quicknote::checkpoint_draft(kNotesRoot, editor, err, sizeof(err))) {
        draft_due = 0;
        if (announce) set_status("Draft checkpoint saved", 0x55d187, 2500);
        return true;
    }
    draft_due = uptime_ms() + 5000;
    set_status(err, 0xff6b6b, 0);
    return false;
}

void save_current_note() {
    tab5_clock_time now{};
    tab5_rtc_read(&now);
    if (!now.valid) {
        set_status("Set clock before saving notes", 0xff6b6b, 0);
        return;
    }
    if (!editor.size()) {
        set_status("Write something before saving", 0xf5b84b, 2500);
        return;
    }
    if (editor.dirty() && !checkpoint(false)) return;

    char iso[16], heading[64], date_short[48], time_buf[16], err[80]{};
    if (!resolve_day(now, 0, iso, heading, date_short)) {
        set_status("Could not resolve current date", 0xff6b6b, 0);
        return;
    }
    std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", now.hour, now.minute);

    if (!quicknote::append_note(kNotesRoot, iso, heading, time_buf, editor, err, sizeof(err))) {
        set_status(err, 0xff6b6b, 0);
        return;
    }

    const bool cleaned = quicknote::discard_draft(kNotesRoot, err, sizeof(err));
    editor.clear();
    editor.mark_saved();
    draft_due = 0;

    if (!cleaned) {
        set_status("Saved note; draft cleanup failed", 0xf5b84b, 0);
    } else {
        char saved_msg[64];
        std::snprintf(saved_msg, sizeof(saved_msg), "Saved to %s.md", iso);
        set_status(saved_msg, 0x55d187, 4000);
    }
    terminal_dirty = true;
    chrome_dirty = true;
}

void on_tab5_kbd_rx(const uint8_t* data, size_t len) {
    if (!key_queue || !data || !len) return;
    for (size_t i = 0; i < len; ++i) {
        xQueueSend(key_queue, &data[i], 0);
    }
}

void return_to_console() {
    if (editor.dirty()) checkpoint(false);
    tab5_sd_unmount();
    const esp_err_t err = tab5_app_boot_return_to_console();
    if (err != ESP_OK) {
        set_status("Console return failed / Press reset", 0xff6b6b, 0);
    }
}

void change_zoom(int delta) {
    int current = static_cast<int>(terminal_view.zoom());
    int next = std::clamp(current + delta, 0, 2);
    if (next != current) {
        terminal_view.set_zoom(static_cast<quicknote::terminal::ZoomLevel>(next));
        save_nvs_settings();
        terminal_dirty = true;
        chrome_dirty = true;
    }
}

void handle_action(quicknote::ui::Action act) {
    using quicknote::ui::Action;
    switch (act) {
        case Action::history_toggle:
            history_mode = !history_mode;
            day_offset = 0;
            if (history_mode) {
                load_history_for_offset();
            } else {
                set_status("Editor mode", 0x56a8ff, 2000);
            }
            terminal_dirty = true;
            chrome_dirty = true;
            break;

        case Action::save_or_prev:
            if (history_mode) {
                --day_offset;
                load_history_for_offset();
            } else {
                save_current_note();
            }
            break;

        case Action::clear_or_next:
            if (history_mode) {
                if (day_offset < 0) {
                    ++day_offset;
                    load_history_for_offset();
                }
            } else if (editor.size()) {
                if (clear_armed) {
                    char err[80]{};
                    if (quicknote::discard_draft(kNotesRoot, err, sizeof(err))) {
                        editor.clear();
                        editor.mark_saved();
                        draft_due = 0;
                        set_status("Draft cleared", 0x55d187, 2500);
                    } else {
                        set_status(err, 0xff6b6b, 0);
                    }
                    clear_armed = false;
                } else {
                    clear_armed = true;
                    clear_until = uptime_ms() + 3000;
                    set_status("Tap CLEAR again to discard draft", 0xf5b84b, 3000);
                }
                terminal_dirty = true;
                chrome_dirty = true;
            }
            break;

        case Action::zoom_out:
            change_zoom(-1);
            break;

        case Action::zoom_in:
            change_zoom(1);
            break;

        case Action::console:
            return_to_console();
            break;

        default:
            break;
    }
}

void sync_state() {
    tab5_clock_time now{};
    ui_state.clock_valid = tab5_rtc_read(&now);
    static char date_buf[48] = "Date unavailable";
    static char time_buf[16] = "--:--";

    if (ui_state.clock_valid) {
        char iso[16], heading[64];
        resolve_day(now, history_mode ? day_offset : 0, iso, heading, date_buf);
        std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", now.hour, now.minute);
    }
    ui_state.date_str = date_buf;
    ui_state.time_str = time_buf;

    tab5_battery_read_status(&ui_state.battery_voltage,
                             &ui_state.battery_percent,
                             &ui_state.charging);

    ui_state.tab5_kbd_connected = tab5::tab5_kbd_connected();
    ui_state.usb_kbd_connected = tab5_keyboard_connected();
    ui_state.history_mode = history_mode;
    ui_state.clear_armed = clear_armed;
    ui_state.dirty = editor.dirty();
    ui_state.font_points = terminal_view.font_points();
    ui_state.day_offset = day_offset;

    ui_state.status_msg = status_text;
    ui_state.status_color = status_color;

    editor.cursor_position(ui_state.cursor_line, ui_state.cursor_col);
    editor.stats(ui_state.word_count, ui_state.char_count);

    ui_state.history_lines_total = history_total_lines;
}

}  // namespace

extern "C" void app_main() {
    esp_log_level_set("*", ESP_LOG_INFO);
    std::printf("TAB5_QN_BOOT version=%s target=ESP32-P4\n",
                esp_app_get_description()->version);

    const bool nvs_ok = nvs_flash_init() == ESP_OK;
    const auto board = tab5_board_init();

    chrome.init();
    terminal_view.init();
    if (nvs_ok) load_nvs_settings();

    key_queue = xQueueCreate(64, sizeof(uint8_t));

    // Tab5 ExtPort1 clip-on keyboard
    const bool tab5_kbd_started = tab5::start_tab5_kbd_input(on_tab5_kbd_rx);
    std::printf("TAB5_QN_INPUT tab5_i2c=%d usb_hid=%d\n",
                tab5_kbd_started, board.usb_hid_ready);

    // Prepare note folders on SD and restore draft
    char err[80]{};
    if (board.sd_ready && quicknote::ensure_note_directories(kNotesRoot, err, sizeof(err))) {
        if (!quicknote::load_draft(kNotesRoot, editor, err, sizeof(err))) {
            set_status(err, 0xff6b6b, 0);
        } else if (editor.size()) {
            set_status("Recovered saved draft", 0x55d187, 4000);
        }
    } else if (err[0]) {
        set_status(err, 0xff6b6b, 0);
    } else {
        set_status("SD card not available", 0xff6b6b, 0);
    }

    // First frame render
    sync_state();
    chrome.draw_all(ui_state);
    terminal_view.draw(editor, history_mode, history_buffer, history_size, history_scroll, cursor_blink);
    tab5_display_present();

    const bool healthy = nvs_ok && board.display_ready && board.touch_ready &&
                         board.sd_ready && tab5_app_boot_console_available();
    const bool boot_confirmed = healthy && tab5_app_boot_ready(true) == ESP_OK;
    const uint64_t boot_deadline = uptime_ms() + 15000;

    std::printf("TAB5_QN_STATUS healthy=%d confirmed=%d display=%d touch=%d sd=%d\n",
                healthy, boot_confirmed, board.display_ready, board.touch_ready, board.sd_ready);

    bool touching = false;
    unsigned touch_releases = 0;
    int touch_x = 0, touch_y = 0;

    for (;;) {
        const uint64_t now = uptime_ms();

        // Rollback safety deadline
        if (!boot_confirmed && now >= boot_deadline) {
            std::printf("TAB5_QN_WATCHDOG startup_timeout returning_to_console\n");
            return_to_console();
        }

        // Poll USB-A HID keyboard
        uint8_t usb_keys[32];
        const size_t usb_count = tab5_keyboard_poll(usb_keys, sizeof(usb_keys));
        for (size_t i = 0; i < usb_count; ++i) {
            xQueueSend(key_queue, &usb_keys[i], 0);
        }

        // Process queued keyboard inputs
        uint8_t batch[16];
        size_t batch_len = 0;
        uint8_t b = 0;
        if (xQueueReceive(key_queue, &b, 0) == pdTRUE) {
            batch[batch_len++] = b;
            if (b == 27) {
                // Peek/drain escape sequence bytes
                while (batch_len < sizeof(batch) &&
                       xQueueReceive(key_queue, &b, pdMS_TO_TICKS(10)) == pdTRUE) {
                    batch[batch_len++] = b;
                    if ((batch_len == 3 && batch[1] != '[') ||
                        (batch_len >= 3 && batch[batch_len - 1] >= 'A' && batch[batch_len - 1] <= '~')) {
                        break;
                    }
                }
            }

            const auto res = quicknote::process_key_bytes(editor, batch, batch_len, history_mode);
            switch (res) {
                case quicknote::Input::save:
                    save_current_note();
                    break;
                case quicknote::Input::zoom_in:
                    change_zoom(1);
                    break;
                case quicknote::Input::zoom_out:
                    change_zoom(-1);
                    break;
                case quicknote::Input::cancel:
                    if (history_mode) {
                        history_mode = false;
                        set_status("Editor mode", 0x56a8ff, 2000);
                        terminal_dirty = true;
                        chrome_dirty = true;
                    }
                    break;
                case quicknote::Input::page_up:
                    if (history_mode) {
                        terminal_view.scroll_page_up(history_scroll);
                        terminal_dirty = true;
                    } else {
                        for (int i = 0; i < 5; ++i) editor.up();
                        terminal_dirty = true;
                        chrome_dirty = true;
                    }
                    break;
                case quicknote::Input::page_down:
                    if (history_mode) {
                        terminal_view.scroll_page_down(history_scroll, history_size, history_buffer);
                        terminal_dirty = true;
                    } else {
                        for (int i = 0; i < 5; ++i) editor.down();
                        terminal_dirty = true;
                        chrome_dirty = true;
                    }
                    break;
                case quicknote::Input::changed:
                    if (editor.dirty()) draft_due = now + 1500;
                    status_text[0] = 0;
                    terminal_dirty = true;
                    chrome_dirty = true;
                    break;
                default:
                    break;
            }
        }

        // Auto-checkpoint draft
        if (draft_due && now >= draft_due) {
            checkpoint(false);
        }

        // Auto-clear disarm
        if (clear_armed && now >= clear_until) {
            clear_armed = false;
            chrome_dirty = true;
        }

        // Clear expired status messages
        if (status_until && now >= status_until) {
            status_until = 0;
            status_text[0] = 0;
            chrome_dirty = true;
        }

        // Cursor blink timer (every 500 ms)
        if (now >= next_blink) {
            next_blink = now + 500;
            cursor_blink = !cursor_blink;
            if (!history_mode) {
                terminal_dirty = true;
            }
        }

        // Second tick for clock & battery updates
        if (now / 1000 != last_second) {
            last_second = now / 1000;
            chrome_dirty = true;
        }

        // Touch handling
        int tx = 0, ty = 0;
        const bool pressed = tab5_touch_poll(&tx, &ty);
        if (pressed) {
            if (!touching) {
                touching = true;
                touch_x = tx;
                touch_y = ty;
            }
            touch_releases = 0;
        } else if (touching && ++touch_releases >= 2) {
            touching = false;
            // First check toolbar
            const auto act = chrome.hit_test(touch_x, touch_y);
            if (act != quicknote::ui::Action::none) {
                handle_action(act);
            } else {
                // Check terminal canvas touch
                if (terminal_view.handle_touch(touch_x, touch_y, editor, history_mode,
                                              history_scroll, history_buffer, history_size)) {
                    terminal_dirty = true;
                    chrome_dirty = true;
                }
            }
        }

        // Render updates
        if (chrome_dirty) {
            sync_state();
            chrome.draw_header(ui_state);
            chrome.draw_status_bar(ui_state);
            chrome.draw_toolbar(ui_state);
            chrome_dirty = false;
        }

        if (terminal_dirty) {
            terminal_view.draw(editor, history_mode, history_buffer, history_size,
                               history_scroll, cursor_blink);
            terminal_dirty = false;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

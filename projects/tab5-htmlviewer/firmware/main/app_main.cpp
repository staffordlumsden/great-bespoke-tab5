#include "html_ui.hpp"
#include "input_tab5_kbd.hpp"
#include "tab5_app_boot.h"
#include "tab5_hal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {

constexpr const char* kTag = "tab5_htmlviewer";

SemaphoreHandle_t s_key_lock = nullptr;
uint8_t s_key_ring[256];
size_t s_key_len = 0;

void push_key_bytes(const uint8_t* data, size_t len) {
    if (!s_key_lock || !data || len == 0) return;
    xSemaphoreTake(s_key_lock, portMAX_DELAY);
    for (size_t i = 0; i < len && s_key_len < sizeof(s_key_ring); ++i) {
        s_key_ring[s_key_len++] = data[i];
    }
    xSemaphoreGive(s_key_lock);
}

size_t pop_key_bytes(uint8_t* out, size_t cap) {
    if (!s_key_lock || !out || cap == 0) return 0;
    xSemaphoreTake(s_key_lock, portMAX_DELAY);
    const size_t n = (s_key_len < cap) ? s_key_len : cap;
    if (n > 0) {
        std::memcpy(out, s_key_ring, n);
        if (n < s_key_len) {
            std::memmove(s_key_ring, s_key_ring + n, s_key_len - n);
        }
        s_key_len -= n;
    }
    xSemaphoreGive(s_key_lock);
    return n;
}

uint8_t load_saved_rotation() {
    nvs_handle_t h = 0;
    uint8_t rot = 0;
    if (nvs_open("tab5console", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "rotation", &rot);
        nvs_close(h);
    }
    return rot & 3U;
}

void save_rotation(uint8_t rot) {
    nvs_handle_t h = 0;
    if (nvs_open("tab5console", NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_u8(h, "rotation", rot & 3U) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

void refresh_telemetry(tab5_html::ViewerState& state) {
    state.battery_valid = tab5_battery_read_status(
        &state.battery_voltage, &state.battery_percent, &state.battery_charging);
    tab5_rtc_read(&state.clock);
    state.kbd_connected = tab5::tab5_kbd_connected() || tab5_keyboard_connected();
}

void handle_command(tab5_html::ViewerState& state,
                    tab5_html::UiCommand cmd,
                    bool& needs_full,
                    bool& needs_vp) {
    using namespace tab5_html;
    if (cmd == UiCommand::return_to_console) {
        ESP_LOGI(kTag, "Returning to resident Tab5Console...");
        tab5_sd_unmount();
        tab5_app_boot_return_to_console();
        return;
    }
    if (cmd == UiCommand::rotate_step) {
        state.rotation = (state.rotation + 1U) & 3U;
        if (tab5_display_set_rotation(static_cast<tab5_display_rotation_t>(state.rotation)) == ESP_OK) {
            save_rotation(state.rotation);
            state.layout_dirty = true;
            needs_full = true;
            needs_vp = false;
        }
    }
}

int point_distance(int x0, int y0, int x1, int y1) {
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    return static_cast<int>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "Starting Tab5HtmlViewer 0.1.1");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kTag, "NVS init returned %s (preserving NVS)", esp_err_to_name(nvs_err));
    }

    s_key_lock = xSemaphoreCreateMutex();

    const tab5_board_status board = tab5_board_init();
    ESP_LOGI(kTag, "Board init: display=%d touch=%d sd=%d usb=%d",
             board.display_ready, board.touch_ready, board.sd_ready, board.usb_hid_ready);

    static tab5_html::ViewerState state{};
    state.rotation = load_saved_rotation();
    tab5_display_set_rotation(static_cast<tab5_display_rotation_t>(state.rotation));

    state.sd_mounted = board.sd_ready;
    tab5_html::viewer_init(state);
    if (state.sd_mounted) {
        tab5_html::viewer_refresh_picker(state);
    }
    refresh_telemetry(state);

    // Present first frame before marking boot ready
    tab5_html::ui_render_full(state);
    tab5_display_present();
    tab5_app_boot_ready(board.display_ready && board.touch_ready);

    // Start clip-on I2C keyboard listener
    tab5::start_tab5_kbd_input([](const uint8_t* data, size_t len) {
        push_key_bytes(data, len);
    });

    // Multi-touch gesture tracking state
    int prev_touch_count = 0;
    int tap_start_x = 0;
    int tap_start_y = 0;
    int last_x0 = 0;
    int last_y0 = 0;
    int total_drag_px = 0;
    bool gesture_was_multi = false;

    int pinch_start_dist = 0;
    uint16_t pinch_start_zoom = 100;
    int last_mid_y = 0;

    int64_t last_telemetry_us = esp_timer_get_time();

    while (true) {
        bool needs_full = false;
        bool needs_vp = false;

        // 1. Poll multi-touch (1-finger drag scroll, 2-finger pinch-to-zoom & pan, or single tap)
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        const int tcount = tab5_touch_poll_multi(&x0, &y0, &x1, &y1);

        if (tcount >= 2) {
            gesture_was_multi = true;
            const int dist = std::max(20, point_distance(x0, y0, x1, y1));
            const int mid_y = (y0 + y1) / 2;

            if (prev_touch_count < 2) {
                pinch_start_dist = dist;
                pinch_start_zoom = state.zoom_pct;
                last_mid_y = mid_y;
            } else {
                const int dy_pan = last_mid_y - mid_y;
                last_mid_y = mid_y;
                const uint16_t target_zoom = static_cast<uint16_t>(
                    (static_cast<int>(pinch_start_zoom) * dist) / std::max(20, pinch_start_dist));
                tab5_html::ui_handle_pinch_zoom(state, target_zoom, mid_y, dy_pan, needs_vp);
                if (needs_vp) {
                    needs_full = true;  // Update zoom indicator in top bar too
                }
            }
            last_x0 = x0;
            last_y0 = y0;
        } else if (tcount == 1) {
            if (prev_touch_count == 0) {
                tap_start_x = x0;
                tap_start_y = y0;
                last_x0 = x0;
                last_y0 = y0;
                total_drag_px = 0;
                gesture_was_multi = false;
            } else if (prev_touch_count == 1 && !gesture_was_multi) {
                const int dx = x0 - last_x0;
                const int dy = last_y0 - y0;  // Dragging finger up scrolls content down
                total_drag_px += std::abs(dx) + std::abs(dy);
                if (total_drag_px >= 10 && std::abs(dy) >= 2) {
                    tab5_html::ui_handle_scroll_delta(state, dy, needs_vp);
                    last_x0 = x0;
                    last_y0 = y0;
                }
            } else {
                // Transitioned from 2 fingers down to 1 finger: re-anchor single finger
                last_x0 = x0;
                last_y0 = y0;
            }
        } else if (tcount == 0 && prev_touch_count > 0) {
            if (!gesture_was_multi && total_drag_px < 16) {
                const auto cmd = tab5_html::ui_handle_tap(state, tap_start_x, tap_start_y, needs_full, needs_vp);
                handle_command(state, cmd, needs_full, needs_vp);
            }
            gesture_was_multi = false;
            total_drag_px = 0;
        }
        prev_touch_count = tcount;

        // 2. Poll USB HID keyboard + I2C clip-on keyboard
        uint8_t usb_buf[32];
        const size_t usb_n = tab5_keyboard_poll(usb_buf, sizeof(usb_buf));
        if (usb_n > 0) {
            push_key_bytes(usb_buf, usb_n);
        }

        uint8_t kbd_buf[32];
        const size_t kbd_n = pop_key_bytes(kbd_buf, sizeof(kbd_buf));
        if (kbd_n > 0) {
            bool kf = false, kv = false;
            const auto cmd = tab5_html::ui_handle_key(state, kbd_buf, kbd_n, kf, kv);
            if (kf) needs_full = true;
            if (kv) needs_vp = true;
            handle_command(state, cmd, needs_full, needs_vp);
        }

        // 3. Periodic telemetry refresh (only redraws if on File Picker screen)
        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_telemetry_us >= 2000000LL) {
            last_telemetry_us = now_us;
            const uint8_t prev_bat = state.battery_percent;
            const bool prev_chg = state.battery_charging;
            const int prev_min = state.clock.minute;
            refresh_telemetry(state);
            if (state.screen == tab5_html::ViewScreen::file_picker &&
                (state.battery_percent != prev_bat ||
                 state.battery_charging != prev_chg ||
                 state.clock.minute != prev_min)) {
                needs_full = true;
            }
        }

        if (needs_full) {
            tab5_html::ui_render_full(state);
            tab5_display_present();
        } else if (needs_vp) {
            tab5_html::ui_render_viewport(state);
            tab5_display_present();
        }

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

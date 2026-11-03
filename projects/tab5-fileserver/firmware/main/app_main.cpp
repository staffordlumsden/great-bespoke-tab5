#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "fileserver_http.hpp"
#include "fileserver_net.hpp"
#include "fileserver_ui.hpp"
#include "fs_core.hpp"
#include "input_tab5_kbd.hpp"
#include "tab5_app_boot.h"
#include "tab5_hal.h"

namespace {

using namespace tab5_fs;

constexpr const char* kSdRoot = "/sdcard";

UiState g_ui{};
QueueHandle_t g_key_queue = nullptr;
bool g_nvs_ok = false;

const char* const kDayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kMonthNames[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

uint64_t uptime_ms() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

int day_of_week(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

void on_tab5_kbd_rx(const uint8_t* data, size_t len) {
    if (!g_key_queue || !data) return;
    for (size_t i = 0; i < len; ++i) {
        xQueueSend(g_key_queue, &data[i], 0);
    }
}

void load_persistent_settings(NetMode& out_mode) {
    out_mode = NetMode::lan;
    format_passcode(esp_random(), g_ui.passcode);

    if (!g_nvs_ok) return;

    nvs_handle_t console_h = 0;
    if (nvs_open("tab5console", NVS_READONLY, &console_h) == ESP_OK) {
        uint8_t dark = 1;
        if (nvs_get_u8(console_h, "dark", &dark) == ESP_OK) {
            g_ui.dark_mode = (dark != 0);
        }
        nvs_close(console_h);
    }

    nvs_handle_t fs_h = 0;
    if (nvs_open("tab5_fs", NVS_READWRITE, &fs_h) == ESP_OK) {
        uint8_t mode_val = 0;
        if (nvs_get_u8(fs_h, "mode", &mode_val) == ESP_OK) {
            out_mode = (mode_val == 1) ? NetMode::hotspot : NetMode::lan;
        }
        char saved_pin[12] = {0};
        size_t pin_len = sizeof(saved_pin);
        if (nvs_get_str(fs_h, "passcode", saved_pin, &pin_len) == ESP_OK &&
            normalize_passcode_input(saved_pin, g_ui.passcode)) {
            // Valid persisted 8-digit passcode loaded
        } else {
            nvs_set_str(fs_h, "passcode", g_ui.passcode);
            nvs_commit(fs_h);
        }
        nvs_close(fs_h);
    }
}

void save_passcode_nvs(const char code[9]) {
    if (!g_nvs_ok) return;
    nvs_handle_t fs_h = 0;
    if (nvs_open("tab5_fs", NVS_READWRITE, &fs_h) == ESP_OK) {
        nvs_set_str(fs_h, "passcode", code);
        nvs_commit(fs_h);
        nvs_close(fs_h);
    }
}

void save_mode_nvs(NetMode mode) {
    if (!g_nvs_ok) return;
    nvs_handle_t fs_h = 0;
    if (nvs_open("tab5_fs", NVS_READWRITE, &fs_h) == ESP_OK) {
        nvs_set_u8(fs_h, "mode", static_cast<uint8_t>(mode));
        nvs_commit(fs_h);
        nvs_close(fs_h);
    }
}

void save_theme_nvs(bool dark) {
    if (!g_nvs_ok) return;
    nvs_handle_t console_h = 0;
    if (nvs_open("tab5console", NVS_READWRITE, &console_h) == ESP_OK) {
        nvs_set_u8(console_h, "dark", dark ? 1 : 0);
        nvs_commit(console_h);
        nvs_close(console_h);
    }
}

void refresh_clock_and_battery() {
    tab5_clock_time now{};
    g_ui.clock_valid = tab5_rtc_read(&now);
    if (g_ui.clock_valid && now.month >= 1 && now.month <= 12 && now.day >= 1 && now.day <= 31) {
        const int dow = day_of_week(now.year, now.month, now.day);
        std::snprintf(g_ui.date_str, sizeof(g_ui.date_str), "%s %02d %s %04d",
                      kDayNames[dow], now.day, kMonthNames[now.month - 1], now.year);
        std::snprintf(g_ui.time_str, sizeof(g_ui.time_str), "%02d:%02d:%02d",
                      now.hour, now.minute, now.second);
    }
    tab5_battery_read_status(&g_ui.battery_voltage, &g_ui.battery_percent, &g_ui.charging);
}

void refresh_sd_summary() {
    g_ui.sd_ready = tab5_sd_usage(&g_ui.sd_total, &g_ui.sd_free);
    if (g_ui.sd_ready) {
        size_t count = 0;
        list_directory(kSdRoot, nullptr, 0, count, g_ui.root_dirs, g_ui.root_files);
    } else {
        g_ui.root_dirs = 0;
        g_ui.root_files = 0;
    }
}

void return_to_console() {
    tab5_sd_unmount();
    if (tab5_app_boot_return_to_console() != ESP_OK) {
        std::snprintf(g_ui.footer_notice, sizeof(g_ui.footer_notice),
                      "CONSOLE RETURN FAILED — PRESS RESET");
        ui_render(g_ui, DIRTY_FOOTER);
    }
}

}  // namespace

extern "C" void app_main() {
    esp_log_level_set("*", ESP_LOG_INFO);
    std::printf("TAB5_FILESERVER_BOOT version=%s target=ESP32-P4\n",
                esp_app_get_description()->version);

    g_nvs_ok = (nvs_flash_init() == ESP_OK);
    const auto board = tab5_board_init();

    NetMode initial_mode = NetMode::lan;
    load_persistent_settings(initial_mode);

    g_key_queue = xQueueCreate(64, sizeof(uint8_t));
    tab5::start_tab5_kbd_input(on_tab5_kbd_rx);

    refresh_clock_and_battery();
    refresh_sd_summary();
    std::snprintf(g_ui.net.status_msg, sizeof(g_ui.net.status_msg),
                  "STARTING C6 RADIO & HTTP FILE SERVER...");

    // Render and present first frame before confirming OTA boot
    ui_render(g_ui, DIRTY_ALL);

    const bool healthy = g_nvs_ok && board.display_ready && board.touch_ready &&
                         board.sd_ready && tab5_app_boot_console_available();
    const bool boot_confirmed = healthy && (tab5_app_boot_ready(true) == ESP_OK);
    const uint64_t boot_deadline = uptime_ms() + 15000;

    std::printf("TAB5_FILESERVER_STATUS healthy=%d confirmed=%d sd=%d\n",
                healthy, boot_confirmed, board.sd_ready);

    // Power C6 companion BEFORE any hosted Wi-Fi RPC
    if (tab5_radio_power_set(true) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(250));
        net_init(g_ui.passcode, initial_mode);
        http_server_start(kSdRoot, g_ui.passcode);
    } else {
        std::snprintf(g_ui.footer_notice, sizeof(g_ui.footer_notice),
                      "C6 RADIO POWER INIT FAILED");
    }

    net_get_status(&g_ui.net);
    http_server_get_stats(&g_ui.http);
    ui_render(g_ui, DIRTY_ALL);

    uint32_t last_net_rev = g_ui.net.revision;
    uint32_t last_http_rev = g_ui.http.revision;
    uint64_t last_sec = 0;
    uint64_t notice_until = 0;

    bool touching = false;
    unsigned touch_releases = 0;
    int touch_x = 0, touch_y = 0;

    for (;;) {
        const uint64_t now = uptime_ms();
        unsigned dirty = DIRTY_NONE;

        if (!boot_confirmed && now >= boot_deadline) {
            return_to_console();
            esp_restart();
        }

        // Poll USB-A HID keyboard
        uint8_t usb_keys[16];
        const size_t usb_n = tab5_keyboard_poll(usb_keys, sizeof(usb_keys));
        for (size_t i = 0; i < usb_n; ++i) {
            xQueueSend(g_key_queue, &usb_keys[i], 0);
        }

        uint8_t key = 0;
        while (xQueueReceive(g_key_queue, &key, 0) == pdTRUE) {
            unsigned kd = DIRTY_NONE;
            if (ui_handle_key(key, g_ui, kd)) {
                dirty |= kd;
            }
        }

        // Check network status updates
        NetStatus latest_net{};
        net_get_status(&latest_net);
        if (latest_net.revision != last_net_rev) {
            last_net_rev = latest_net.revision;
            g_ui.net = latest_net;
            dirty |= (g_ui.wifi_modal_open ? DIRTY_MODAL : DIRTY_CARDS) | DIRTY_FOOTER;
        }

        // Check HTTP server telemetry updates
        HttpStats latest_http{};
        http_server_get_stats(&latest_http);
        if (latest_http.revision != last_http_rev) {
            const uint32_t prev_uploads = g_ui.http.uploads;
            last_http_rev = latest_http.revision;
            g_ui.http = latest_http;
            if (latest_http.uploads != prev_uploads) {
                refresh_sd_summary();
            }
            if (!g_ui.wifi_modal_open) {
                dirty |= DIRTY_CARDS;
            }
        }

        // Clear temporary footer notice
        if (notice_until && now >= notice_until) {
            notice_until = 0;
            g_ui.footer_notice[0] = '\0';
            dirty |= DIRTY_FOOTER;
        }

        // 1-second header clock & battery refresh (only repaints header!)
        if (now / 1000 != last_sec) {
            last_sec = now / 1000;
            refresh_clock_and_battery();
            dirty |= DIRTY_HEADER;
        }

        // Touch sampling with release debounce
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
            unsigned tap_dirty = DIRTY_NONE;
            const UiAction act = ui_handle_tap(touch_x, touch_y, g_ui, tap_dirty);
            dirty |= tap_dirty;

            switch (act) {
                case UiAction::return_console:
                    return_to_console();
                    break;

                case UiAction::toggle_theme:
                    g_ui.dark_mode = !g_ui.dark_mode;
                    save_theme_nvs(g_ui.dark_mode);
                    dirty |= DIRTY_ALL;
                    break;

                case UiAction::mode_lan:
                    if (g_ui.net.mode != NetMode::lan) {
                        save_mode_nvs(NetMode::lan);
                        net_set_mode(NetMode::lan, g_ui.passcode);
                        net_get_status(&g_ui.net);
                        last_net_rev = g_ui.net.revision;
                        dirty |= DIRTY_CARDS | DIRTY_FOOTER;
                    }
                    break;

                case UiAction::mode_hotspot:
                    if (g_ui.net.mode != NetMode::hotspot) {
                        save_mode_nvs(NetMode::hotspot);
                        net_set_mode(NetMode::hotspot, g_ui.passcode);
                        net_get_status(&g_ui.net);
                        last_net_rev = g_ui.net.revision;
                        dirty |= DIRTY_CARDS | DIRTY_FOOTER;
                    }
                    break;

                case UiAction::new_passcode:
                    format_passcode(esp_random(), g_ui.passcode);
                    save_passcode_nvs(g_ui.passcode);
                    http_server_set_passcode(g_ui.passcode);
                    net_update_passcode(g_ui.passcode);
                    net_get_status(&g_ui.net);
                    http_server_get_stats(&g_ui.http);
                    last_net_rev = g_ui.net.revision;
                    last_http_rev = g_ui.http.revision;
                    std::snprintf(g_ui.footer_notice, sizeof(g_ui.footer_notice),
                                  "NEW 8-DIGIT PASSCODE ACTIVE: %s", g_ui.passcode);
                    notice_until = now + 4000;
                    dirty |= DIRTY_CARDS | DIRTY_FOOTER;
                    break;

                case UiAction::open_wifi_modal:
                    g_ui.wifi_modal_open = true;
                    g_ui.wifi_pass_mode = false;
                    g_ui.wifi_scroll = 0;
                    net_start_scan();
                    net_get_status(&g_ui.net);
                    last_net_rev = g_ui.net.revision;
                    dirty |= DIRTY_MODAL | DIRTY_FOOTER;
                    break;

                case UiAction::reconnect_lan:
                    net_reconnect_lan();
                    net_get_status(&g_ui.net);
                    last_net_rev = g_ui.net.revision;
                    dirty |= DIRTY_CARDS | DIRTY_FOOTER;
                    break;

                case UiAction::none:
                    break;
            }
        }

        if (dirty != DIRTY_NONE) {
            ui_render(g_ui, dirty);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

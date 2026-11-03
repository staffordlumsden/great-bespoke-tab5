#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "connection.hpp"
#include "cursor_renderer.hpp"
#include "esp_hosted_api.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "input_tab5_kbd.hpp"
#include "nvs_flash.h"
#include "ssh_connection.hpp"
#include "tab5_app_boot.h"
#include "tab5_hal.h"
#include "tab5_terminal_config.hpp"
#include "tab5_terminal_display.hpp"
#include "tab5_weather_wifi.h"
#include "term_core/terminal.hpp"

namespace {

using tab5_terminal::SessionState;

tab5_terminal::Config config;
tab5_terminal::Tab5TerminalDisplay display;
std::unique_ptr<term::Terminal> terminal;
std::unique_ptr<tab5::CursorRenderer> cursor;

StreamBufferHandle_t rx_stream;
StreamBufferHandle_t key_stream;
QueueHandle_t connect_requests;
QueueHandle_t input_actions;
std::atomic<tab5::SshConnection*> connection{nullptr};
std::atomic<SessionState> session_state{SessionState::starting};
std::atomic<tab5::SshFailure> session_failure{tab5::SshFailure::none};
std::atomic<uint32_t> session_revision{0};
std::atomic<bool> rx_overflow{false};
std::atomic<bool> key_overflow{false};
std::atomic<bool> tab5_keyboard_input_verified{false};
std::atomic<bool> usb_keyboard_input_verified{false};
std::atomic<uint16_t> pty_columns{tab5_terminal::kDefaultTerminalColumns};
std::atomic<uint16_t> pty_rows{tab5_terminal::kDefaultTerminalRows};

enum class InputAction : uint8_t {
    zoom_in,
    zoom_out,
    zoom_reset,
    scroll_page_older,
    scroll_page_newer,
    scroll_oldest,
    scroll_live,
};

size_t scroll_offset = 0;
std::vector<term::Cell> scroll_view;

char profile_message[96] = "Loading private SD profile";
char host_label[40] = "--";

bool queue_keyboard_action(uint8_t hid_usage, uint8_t modifiers) {
    if (!input_actions) return false;
    constexpr uint8_t kCtrl = 0x01 | 0x10;
    constexpr uint8_t kShift = 0x02 | 0x20;
    const bool ctrl = modifiers & kCtrl;
    const bool shift = modifiers & kShift;
    InputAction action;
    if (ctrl && hid_usage == 0x2e) {          // Ctrl+= or Ctrl++
        action = InputAction::zoom_in;
    } else if (ctrl && hid_usage == 0x2d) {   // Ctrl+-
        action = InputAction::zoom_out;
    } else if (ctrl && hid_usage == 0x27) {   // Ctrl+0
        action = InputAction::zoom_reset;
    } else if (shift && hid_usage == 0x4b) {  // Shift+PageUp
        action = InputAction::scroll_page_older;
    } else if (shift && hid_usage == 0x4e) {  // Shift+PageDown
        action = InputAction::scroll_page_newer;
    } else if (shift && hid_usage == 0x4a) {  // Shift+Home
        action = InputAction::scroll_oldest;
    } else if (shift && hid_usage == 0x4d) {  // Shift+End
        action = InputAction::scroll_live;
    } else {
        return false;
    }
    return xQueueSend(input_actions, &action, 0) == pdTRUE;
}

bool usb_keyboard_filter(uint8_t hid_usage, uint8_t modifiers, void*) {
    return queue_keyboard_action(hid_usage, modifiers);
}

uint64_t milliseconds() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

void set_session(SessionState state,
                 tab5::SshFailure failure = tab5::SshFailure::none) {
    session_failure.store(failure);
    session_state.store(state);
    session_revision.fetch_add(1);
}

void receive_bytes(std::span<const uint8_t> bytes) {
    if (!rx_stream || bytes.empty()) return;
    const size_t sent = xStreamBufferSend(rx_stream, bytes.data(), bytes.size(),
                                          pdMS_TO_TICKS(20));
    if (sent != bytes.size()) rx_overflow.store(true);
}

void receive_keys(std::span<const uint8_t> bytes) {
    if (!key_stream || bytes.empty()) return;
    const size_t sent = xStreamBufferSend(key_stream, bytes.data(), bytes.size(), 0);
    if (sent != bytes.size()) key_overflow.store(true);
}

void connect_worker(void*) {
    uint8_t request = 0;
    for (;;) {
        if (xQueueReceive(connect_requests, &request, portMAX_DELAY) != pdTRUE) continue;

        tab5::SshConnection* old = connection.exchange(nullptr);
        tab5::set_active_connection(nullptr);
        if (old) old->stop();
        const uint64_t stop_deadline = milliseconds() + 3000;
        while (!tab5::ssh_transport_idle() && milliseconds() < stop_deadline)
            vTaskDelay(pdMS_TO_TICKS(20));
        delete old;

        if (!tab5::ssh_transport_idle()) {
            set_session(SessionState::failed, tab5::SshFailure::resources);
            continue;
        }

        set_session(SessionState::connecting);
        const tab5::SshConfig ssh_config{
            .host = config.host,
            .port = config.port,
            .user = config.username,
            .password = config.password,
            .private_key_file = config.private_key_file,
            .private_key_passphrase = config.private_key_passphrase,
            .cols = pty_columns.load(),
            .rows = pty_rows.load(),
            .task_stack_bytes = 12288,
        };
        auto* candidate = new (std::nothrow) tab5::SshConnection(ssh_config);
        if (!candidate) {
            set_session(SessionState::failed, tab5::SshFailure::resources);
            continue;
        }
        if (!candidate->start(receive_bytes)) {
            const auto failure = tab5::ssh_last_failure();
            delete candidate;
            set_session(SessionState::failed, failure);
            continue;
        }
        connection.store(candidate);
        tab5::set_active_connection(candidate);
        set_session(SessionState::connected);
    }
}

bool request_connection() {
    if (!connect_requests || !config.ready) return false;
    const uint8_t request = 1;
    return xQueueSend(connect_requests, &request, 0) == pdTRUE;
}

void return_to_console() {
    if (auto* active = connection.load()) active->stop();
    tab5_sd_unmount();
    const esp_err_t result = tab5_app_boot_return_to_console();
    if (result != ESP_OK) {
        std::snprintf(profile_message, sizeof(profile_message),
                      "Console return failed: %s / press reset", esp_err_to_name(result));
        session_revision.fetch_add(1);
    }
}

struct Touch {
    bool armed{false};
    bool down{false};
    bool cancelled{false};
    unsigned releases{0};
    tab5_terminal::TouchAction start{tab5_terminal::TouchAction::none};
    int start_x{0};
    int start_y{0};
    int last_x{0};
    int last_y{0};
    bool terminal_gesture{false};

    tab5_terminal::TouchAction sample(bool pressed, int x, int y) {
        if (!armed) {
            releases = pressed ? 0 : releases + 1;
            if (releases >= 3) {
                armed = true;
                releases = 0;
            }
            return tab5_terminal::TouchAction::none;
        }
        if (pressed) {
            releases = 0;
            const auto action = display.hit_test(x, y);
            if (!down) {
                down = true;
                cancelled = false;
                start_x = last_x = x;
                start_y = last_y = y;
                start = action;
                terminal_gesture = display.terminal_contains(x, y);
            } else if (action != start) {
                if (!terminal_gesture) cancelled = true;
            }
            last_x = x;
            last_y = y;
        } else if (down && ++releases >= 3) {
            down = false;
            releases = 0;
            if (terminal_gesture) {
                terminal_gesture = false;
                const int dx = last_x - start_x;
                const int dy = last_y - start_y;
                if (std::abs(dy) >= 48 && std::abs(dy) > std::abs(dx))
                    return dy > 0 ? tab5_terminal::TouchAction::scroll_older
                                  : tab5_terminal::TouchAction::scroll_newer;
                return tab5_terminal::TouchAction::none;
            }
            return cancelled ? tab5_terminal::TouchAction::none : start;
        }
        return tab5_terminal::TouchAction::none;
    }
};

tab5_terminal::UiStatus make_status(const tab5_weather_wifi_status_t& wifi,
                                    bool tab5_keyboard, bool usb_keyboard,
                                    bool c6_known,
                                    bool c6_compatible,
                                    const esp_hosted_coprocessor_fwver_t& c6) {
    tab5_terminal::UiStatus result;
    result.session = session_state.load();
    result.profile_message = profile_message;
    result.ssid = wifi.ssid[0] ? wifi.ssid : "--";
    result.ip = wifi.ip_address[0] ? wifi.ip_address : "--";
    result.host_label = host_label;
    result.failure = tab5::ssh_failure_label(session_failure.load());
    result.rssi = wifi.rssi;
    result.config_ready = config.ready;
    result.tab5_keyboard_connected = tab5_keyboard;
    result.usb_keyboard_connected = usb_keyboard;
    result.tab5_keyboard_input_verified = tab5_keyboard_input_verified.load();
    result.usb_keyboard_input_verified = usb_keyboard_input_verified.load();
    result.c6_known = c6_known;
    result.c6_compatible = c6_compatible;
    result.c6_major = c6.major1;
    result.c6_minor = c6.minor1;
    result.c6_patch = c6.patch1;
    result.terminal_columns = display.cols();
    result.terminal_rows = display.rows();
    result.font_points = display.font_points();
    result.scroll_offset = scroll_offset;
    result.scrollback_rows = terminal ? terminal->screen().scrollback_rows() : 0;
    return result;
}

}  // namespace

extern "C" void app_main() {
    const bool nvs_ready = nvs_flash_init() == ESP_OK;  // Never erase shared NVS.
    const auto board = tab5_board_init();
    const bool display_ready = board.display_ready && display.init().has_value();

    if (board.sd_ready)
        tab5_terminal::load_config(config, profile_message, sizeof(profile_message));
    else
        std::snprintf(profile_message, sizeof(profile_message), "SD card is unavailable");
    if (config.ready)
        std::snprintf(host_label, sizeof(host_label), "%.12s@%.20s",
                      config.username, config.host);

    rx_stream = xStreamBufferCreate(16384, 1);
    key_stream = xStreamBufferCreate(512, 1);
    connect_requests = xQueueCreate(1, sizeof(uint8_t));
    input_actions = xQueueCreate(16, sizeof(InputAction));
    const bool worker_ready = rx_stream && key_stream && connect_requests && input_actions &&
        xTaskCreate(connect_worker, "terminal_connect", 16384, nullptr, 4, nullptr) == pdPASS;
    if (input_actions) tab5_keyboard_set_key_filter(usb_keyboard_filter, nullptr);

    // Primary physical input: M5Stack's clip-on Tab5 Keyboard on ExtPort1.
    // Its absence is non-fatal because the board HAL has already brought up
    // the independent USB-A HID keyboard fallback.
    const auto tab5_keyboard_start = key_stream
        ? tab5::start_tab5_kbd_input(receive_keys, queue_keyboard_action)
        : std::unexpected(term::Error::NotInitialized);
    const bool tab5_keyboard_ready = tab5_keyboard_start.has_value();
    std::printf("TAB5_TERMINAL_INPUT tab5_i2c=%d usb_hid_ready=%d primary=TAB5_I2C fallback=USB_A\n",
                tab5_keyboard_ready, board.usb_hid_ready);

    pty_columns.store(display.cols());
    pty_rows.store(display.rows());
    terminal = std::make_unique<term::Terminal>(display.cols(), display.rows(),
                                                display);
    cursor = std::make_unique<tab5::CursorRenderer>(terminal->screen(), display);
    terminal->screen().set_response_sink([](std::span<const uint8_t> bytes) {
        if (auto* active = connection.load(); active && active->is_connected())
            active->send(bytes);
    });

    tab5_weather_wifi_status_t wifi{};
    esp_hosted_coprocessor_fwver_t c6{};
    const auto initial_status = make_status(wifi, tab5::tab5_kbd_connected(),
                                            tab5_keyboard_connected(), false,
                                            false, c6);
    bool first_frame = display_ready && display.draw_chrome(initial_status);
    if (first_frame) {
        terminal->feed("\x1b[2J\x1b[H"
                       "TAB5 Terminal 0.1.4\r\n"
                       "portable_terminal VT100 core / Great Bespoke Tab5 HAL\r\n");
        if (config.ready) {
            terminal->feed("Checking the factory C6 and saved Console Wi-Fi...\r\n\r\n");
        } else {
            terminal->feed("SSH profile required: /private/tab5-terminal.json\r\n"
                           "Wi-Fi and C6 status are shown in the sidebar.\r\n"
                           "Ctrl +/- changes text size; swipe vertically for scrollback.\r\n"
                           "Type any key to verify keyboard input. Keys are sent only after SSH connects.\r\n\r\n");
        }
        first_frame = terminal->render_dirty().has_value();
        if (first_frame) cursor->draw();
        first_frame = first_frame && display.present_all();
    }

    bool radio_ready = false;
    bool c6_known = false;
    bool c6_compatible = false;
    if (tab5_radio_power_set(true) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(250));
        const esp_err_t init = tab5_weather_wifi_init();
        if (init == ESP_OK) {
            const esp_err_t version = esp_hosted_get_coprocessor_fwversion(&c6);
            c6_known = version == ESP_OK;
            c6_compatible = c6_known && c6.major1 == 1 && c6.minor1 == 4;
            const esp_err_t start = c6_compatible ? tab5_weather_wifi_start()
                                                  : ESP_ERR_INVALID_VERSION;
            radio_ready = c6_compatible && start == ESP_OK;
        }
    }
    std::printf("TAB5_TERMINAL_C6 known=%d version=%u.%u.%u compatible=%d action=NONE\n",
                c6_known, static_cast<unsigned>(c6.major1),
                static_cast<unsigned>(c6.minor1), static_cast<unsigned>(c6.patch1),
                c6_compatible);

    if (!radio_ready) {
        set_session(SessionState::failed, tab5::SshFailure::network);
        std::snprintf(profile_message, sizeof(profile_message),
                      c6_known ? "Factory C6 version mismatch / no update attempted"
                               : "Factory C6 check failed / no update attempted");
    } else {
        set_session(SessionState::waiting_wifi);
    }
    tab5_weather_wifi_get_status(&wifi);
    display.draw_status(make_status(wifi, tab5::tab5_kbd_connected(),
                                    tab5_keyboard_connected(), c6_known,
                                    c6_compatible, c6));

    const bool input_ready = tab5_keyboard_ready || board.usb_hid_ready;
    const bool healthy = nvs_ready && display_ready && board.touch_ready &&
        board.sd_ready && input_ready && worker_ready && radio_ready &&
        tab5_app_boot_console_available();
    const bool boot_confirmed = healthy && first_frame &&
        tab5_app_boot_ready(true) == ESP_OK;
    const uint64_t boot_deadline = milliseconds() + 15000;
    std::printf("TAB5_TERMINAL_BOOT healthy=%d first_frame=%d confirmed=%d "
                "credentials=WITHHELD c6_update=NONE\n",
                healthy, first_frame, boot_confirmed);

    uint32_t wifi_revision = wifi.revision;
    uint32_t observed_session_revision = session_revision.load();
    bool tab5_keyboard = tab5::tab5_kbd_connected();
    bool usb_keyboard = tab5_keyboard_connected();
    bool connection_requested = false;
    bool observed_connected = false;
    SessionState displayed_session = session_state.load();
    uint64_t next_cursor_blink = milliseconds() + 600;
    Touch touch;

    auto redraw_view = [&]() -> bool {
        cursor->erase();
        const size_t cells = static_cast<size_t>(terminal->screen().cols()) *
                             terminal->screen().rows();
        scroll_view.resize(cells);
        if (!terminal->screen().copy_view(scroll_offset, scroll_view)) return false;
        const bool ok = display.draw_view(scroll_view);
        (void)terminal->screen().take_damage();
        if (scroll_offset == 0) cursor->draw();
        return ok;
    };

    auto set_scroll = [&](size_t wanted) -> bool {
        const size_t maximum = terminal->screen().scrollback_rows();
        wanted = std::min(wanted, maximum);
        if (wanted == scroll_offset) return false;
        scroll_offset = wanted;
        const bool ok = redraw_view();
        session_revision.fetch_add(1);
        next_cursor_blink = milliseconds() + 600;
        return ok;
    };

    auto send_remote_page = [&](bool older) {
        static constexpr uint8_t kPageUp[] = {0x1b, '[', '5', '~'};
        static constexpr uint8_t kPageDown[] = {0x1b, '[', '6', '~'};
        if (auto* active = connection.load(); active && active->is_connected()) {
            active->send(older ? std::span<const uint8_t>(kPageUp)
                               : std::span<const uint8_t>(kPageDown));
        }
    };

    auto page_view = [&](bool older, bool whole_history = false) -> bool {
        if (terminal->screen().alt_active()) {
            send_remote_page(older);
            return false;
        }
        const size_t page = std::max<size_t>(1, terminal->screen().rows() / 2);
        const size_t wanted = whole_history
            ? (older ? terminal->screen().scrollback_rows() : 0)
            : (older ? scroll_offset + page
                     : (scroll_offset > page ? scroll_offset - page : 0));
        return set_scroll(wanted);
    };

    auto apply_zoom = [&](tab5_terminal::ZoomLevel wanted) -> bool {
        if (wanted == display.zoom_level()) return false;
        scroll_offset = 0;
        cursor->erase();
        if (!display.set_zoom(wanted)) return false;
        terminal->resize(display.cols(), display.rows());
        pty_columns.store(display.cols());
        pty_rows.store(display.rows());
        if (auto* active = connection.load(); active && active->is_connected())
            active->resize(display.cols(), display.rows());
        const bool ok = redraw_view();
        session_revision.fetch_add(1);
        next_cursor_blink = milliseconds() + 600;
        return ok;
    };

    auto change_zoom = [&](int direction) -> bool {
        using tab5_terminal::ZoomLevel;
        ZoomLevel wanted = display.zoom_level();
        if (direction > 0) {
            if (wanted == ZoomLevel::compact) wanted = ZoomLevel::readable;
            else if (wanted == ZoomLevel::readable) wanted = ZoomLevel::large;
        } else if (direction < 0) {
            if (wanted == ZoomLevel::large) wanted = ZoomLevel::readable;
            else if (wanted == ZoomLevel::readable) wanted = ZoomLevel::compact;
        } else {
            wanted = ZoomLevel::readable;
        }
        return apply_zoom(wanted);
    };

    for (;;) {
        const uint64_t now = milliseconds();
        if (!boot_confirmed && now >= boot_deadline) {
            return_to_console();
            esp_restart();
        }

        tab5_weather_wifi_status_t current_wifi{};
        if (radio_ready) tab5_weather_wifi_get_status(&current_wifi);

        if (boot_confirmed && !connection_requested && config.ready &&
            current_wifi.has_ip && c6_compatible && worker_ready) {
            connection_requested = request_connection();
        }

        if (auto* active = connection.load()) {
            const bool connected = active->is_connected();
            if (observed_connected && !connected &&
                session_state.load() == SessionState::connected)
                set_session(SessionState::disconnected);
            observed_connected = connected;
        } else {
            observed_connected = false;
        }

        bool rendered = false;
        uint8_t incoming[2048];
        size_t count = 0;
        bool received = false;
        if (rx_stream) {
            while ((count = xStreamBufferReceive(rx_stream, incoming,
                                                  sizeof(incoming), 0)) > 0) {
                const size_t old_history = terminal->screen().scrollback_rows();
                if (!received && scroll_offset == 0) cursor->erase();
                terminal->feed(std::span<const uint8_t>(incoming, count));
                const size_t new_history = terminal->screen().scrollback_rows();
                if (scroll_offset > 0 && new_history > old_history) {
                    scroll_offset = std::min(new_history,
                                             scroll_offset + new_history - old_history);
                    session_revision.fetch_add(1);
                }
                received = true;
            }
            if (received) {
                if (scroll_offset == 0) {
                    rendered = terminal->render_dirty().has_value();
                    cursor->draw();
                } else {
                    rendered = true;
                }
                next_cursor_blink = now + 600;
            }
        }
        if (rx_overflow.exchange(false)) {
            std::snprintf(profile_message, sizeof(profile_message),
                          "Terminal receive queue overflowed; output was dropped");
            session_revision.fetch_add(1);
        }

        InputAction input_action;
        while (input_actions && xQueueReceive(input_actions, &input_action, 0) == pdTRUE) {
            switch (input_action) {
                case InputAction::zoom_in:
                    rendered = change_zoom(1) || rendered;
                    break;
                case InputAction::zoom_out:
                    rendered = change_zoom(-1) || rendered;
                    break;
                case InputAction::zoom_reset:
                    rendered = change_zoom(0) || rendered;
                    break;
                case InputAction::scroll_page_older:
                    rendered = page_view(true) || rendered;
                    break;
                case InputAction::scroll_page_newer:
                    rendered = page_view(false) || rendered;
                    break;
                case InputAction::scroll_oldest:
                    rendered = page_view(true, true) || rendered;
                    break;
                case InputAction::scroll_live:
                    rendered = page_view(false, true) || rendered;
                    break;
            }
        }

        uint8_t accessory_keys[64];
        if (key_stream) {
            while ((count = xStreamBufferReceive(key_stream, accessory_keys,
                                                  sizeof(accessory_keys), 0)) > 0) {
                const bool first_key = !tab5_keyboard_input_verified.exchange(true);
                if (first_key) session_revision.fetch_add(1);
                if (scroll_offset > 0) rendered = set_scroll(0) || rendered;
                if (auto* active = connection.load(); active && active->is_connected()) {
                    active->send(std::span<const uint8_t>(accessory_keys, count));
                } else if (first_key) {
                    cursor->erase();
                    terminal->feed("\r\nTab5 Keyboard input verified. "
                                   "Keys will be sent after SSH connects.\r\n");
                    rendered = terminal->render_dirty().has_value() || rendered;
                    cursor->draw();
                    next_cursor_blink = now + 600;
                }
            }
        }
        if (key_overflow.exchange(false)) {
            std::snprintf(profile_message, sizeof(profile_message),
                          "Keyboard input queue overflowed; keystrokes were dropped");
            session_revision.fetch_add(1);
        }

        uint8_t keys[64];
        size_t key_count = 0;
        while ((key_count = tab5_keyboard_poll(keys, sizeof(keys))) > 0) {
            const bool first_key = !usb_keyboard_input_verified.exchange(true);
            if (first_key) session_revision.fetch_add(1);
            if (scroll_offset > 0) rendered = set_scroll(0) || rendered;
            if (auto* active = connection.load(); active && active->is_connected()) {
                active->send(std::span<const uint8_t>(keys, key_count));
            } else if (first_key) {
                cursor->erase();
                terminal->feed("\r\nUSB-A keyboard input verified. "
                               "Keys will be sent after SSH connects.\r\n");
                rendered = terminal->render_dirty().has_value() || rendered;
                cursor->draw();
                next_cursor_blink = now + 600;
            }
        }

        int touch_x = 0, touch_y = 0;
        const bool touch_pressed = tab5_touch_poll(&touch_x, &touch_y);
        const auto action = touch.sample(touch_pressed, touch_x, touch_y);
        if (action == tab5_terminal::TouchAction::console) {
            return_to_console();
        } else if (action == tab5_terminal::TouchAction::reconnect) {
            if (boot_confirmed && config.ready && current_wifi.has_ip &&
                c6_compatible) {
                connection_requested = request_connection();
                if (connection_requested) set_session(SessionState::connecting);
            }
        } else if (action == tab5_terminal::TouchAction::zoom_in) {
            rendered = change_zoom(1) || rendered;
        } else if (action == tab5_terminal::TouchAction::zoom_out) {
            rendered = change_zoom(-1) || rendered;
        } else if (action == tab5_terminal::TouchAction::toggle_sidebar) {
            if (session_state.load() == SessionState::connected &&
                display.set_sidebar_collapsed(!display.sidebar_collapsed())) {
                const auto status = make_status(current_wifi, tab5_keyboard,
                                                usb_keyboard, c6_known,
                                                c6_compatible, c6);
                const bool chrome = display.draw_chrome(status);
                rendered = redraw_view() || rendered;
                rendered = display.present_all() && chrome && rendered;
            }
        } else if (action == tab5_terminal::TouchAction::scroll_older) {
            rendered = page_view(true) || rendered;
        } else if (action == tab5_terminal::TouchAction::scroll_newer) {
            rendered = page_view(false) || rendered;
        }

        const bool current_tab5_keyboard = tab5::tab5_kbd_connected();
        const bool current_usb_keyboard = tab5_keyboard_connected();
        const uint32_t current_session_revision = session_revision.load();
        if (current_wifi.revision != wifi_revision ||
            current_tab5_keyboard != tab5_keyboard ||
            current_usb_keyboard != usb_keyboard ||
            current_session_revision != observed_session_revision) {
            wifi_revision = current_wifi.revision;
            tab5_keyboard = current_tab5_keyboard;
            usb_keyboard = current_usb_keyboard;
            observed_session_revision = current_session_revision;
            const auto status = make_status(current_wifi, tab5_keyboard,
                                            usb_keyboard, c6_known,
                                            c6_compatible, c6);
            bool layout_changed = false;
            if (status.session != displayed_session) {
                if (status.session == SessionState::connected) {
                    display.set_sidebar_collapsed(true);
                    layout_changed = true;
                } else if (displayed_session == SessionState::connected) {
                    display.set_sidebar_collapsed(false);
                    layout_changed = true;
                }
                displayed_session = status.session;
            }
            if (layout_changed) {
                const bool chrome = display.draw_chrome(status);
                rendered = redraw_view() || rendered;
                rendered = display.present_all() && chrome && rendered;
            } else {
                display.draw_status(status);
            }
        }

        if (scroll_offset == 0 && !rendered && now >= next_cursor_blink) {
            cursor->toggle_blink();
            next_cursor_blink = now + 600;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

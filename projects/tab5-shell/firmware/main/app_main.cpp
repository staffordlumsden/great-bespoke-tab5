#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "boot_policy.h"
#include "document_mode.h"
#include "shell_network.h"
#include "package_runtime.h"
#include "download.h"
#include "cursor_renderer.h"
#include "scrollback.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "input_tab5_kbd.hpp"
#include "line_editor.h"
#include "nvs_flash.h"
#include "shell_core.h"
#include "shell_display.h"
#include "tab5_app_boot.h"
#include "tab5_hal.h"
#include "term_core/terminal.hpp"

namespace {

constexpr const char* kHomeRoot = "/sdcard/files/Tab5Shell";
constexpr const char* kSdRoot = "/sdcard";

QueueHandle_t key_queue = nullptr;
QueueHandle_t scroll_actions = nullptr;
tab5_shell::Scrollback scrollback;
tab5_shell::ShellDisplay display;
tab5_shell::Shell shell({kHomeRoot, kSdRoot, false});
tab5_shell::LineEditor editor;
tab5_shell::DocumentMode document(shell);
tab5_shell::Packages packages(shell);
tab5_shell::ShellNetwork network;
tab5_shell::SessionFlow session;
std::vector<tab5_shell::ViewRow> previous_rows;
tab5_shell::SshRequest pending_ssh;
bool password_prompt=false;
std::string password;
std::uint64_t draft_due=0;
tab5_shell::TouchKeyboard touch_keyboard;
tab5_shell::ShellStatus ui_status;
std::string status_message = "LOCAL SHELL";
std::unique_ptr<term::Terminal> terminal;
std::unique_ptr<tab5_shell::CursorRenderer> cursor;

std::uint64_t milliseconds() {
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

void on_tab5_keyboard(const std::uint8_t* data, std::size_t length) {
    if (!key_queue || !data) return;
    for (std::size_t index = 0; index < length; ++index)
        xQueueSend(key_queue, &data[index], 0);
}

bool queue_scroll_action(std::uint8_t usage, std::uint8_t modifiers) {
    const auto action = tab5_shell::scroll_key(usage, modifiers);
    return scroll_actions && action != tab5_shell::ScrollAction::none &&
           xQueueSend(scroll_actions, &action, 0) == pdTRUE;
}

bool usb_scroll_filter(std::uint8_t usage, std::uint8_t modifiers, void*) {
    return queue_scroll_action(usage, modifiers);
}

std::uint16_t visible_rows() { return touch_keyboard.visible ? 14 : display.rows(); }

void update_scroll_status() {
    const auto rows = terminal->screen().scrollback_rows();
    if (ui_status.scroll_offset == scrollback.offset() && ui_status.scrollback_rows == rows) return;
    ui_status.scroll_offset = scrollback.offset(); ui_status.scrollback_rows = rows;
    (void)display.draw_status(ui_status);
}

std::string prompt() {
    auto cwd=shell.cwd();
    if (cwd.size()>32) cwd="..."+cwd.substr(cwd.size()-29);
    return cwd + " $ ";
}

void feed_output(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size() + 16);
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\n' && (index == 0 || text[index - 1] != '\r'))
            normalized.push_back('\r');
        normalized.push_back(text[index]);
    }
    (void)terminal->feed(normalized);
}

void feed_prompt() {
    feed_output(prompt());
}

void configure_local_window() {
    terminal->screen().set_scrollback_capture_rows(visible_rows());
    (void)terminal->feed(touch_keyboard.visible ? "\x1b[1;14r\x1b[14;1H" : "\x1b[r");
}

void redraw_window() {
    cursor->erase();
    (void)scrollback.draw(terminal->screen(), display, visible_rows());
    (void)terminal->screen().take_damage();
    if (!scrollback.active()) cursor->sync_after_render();
    update_scroll_status();
}

void render_terminal() {
    const bool redraw = scrollback.observe(terminal->screen());
    if (scrollback.active()) {
        if (redraw) redraw_window();
        update_scroll_status();
        return;
    }
    cursor->erase();
    if (redraw) terminal->screen().mark_region(0, 0, display.rows(), display.cols());
    (void)terminal->render_dirty();
    cursor->sync_after_render();
    update_scroll_status();
}

void reset_after_remote() {
    // A remote can disconnect in the middle of UTF-8/OSC/CSI or leave alternate
    // screen/margins active. Recreate parser + screen before local input resumes.
    std::uint8_t discarded[1024];
    while (network.receive(discarded,sizeof(discarded))) {}
    cursor->erase();
    scrollback.reset();
    *terminal=term::Terminal(display.cols(),display.rows(),display);
    terminal->screen().set_response_sink([](std::span<const std::uint8_t> bytes) {
        if (network.connected()) network.send(bytes);
    });
    cursor=std::make_unique<tab5_shell::CursorRenderer>(terminal->screen(),display);
    configure_local_window();
}

void redraw_input_line() {
    cursor->erase();
    (void)terminal->feed("\r\x1b[2K");
    feed_output(prompt());
    const auto prefix=prompt().size();
    const auto available=79-prefix;
    const auto left=editor.cursor()>=available ? editor.cursor()-available+1 : 0;
    feed_output(std::string_view(editor.line()).substr(left,available));
    (void)terminal->feed("\r\x1b[" + std::to_string(prefix+editor.cursor()-left) + "C");
    render_terminal();
}

void set_status(int status, std::string message) {
    ui_status.last_status = status;
    status_message = std::move(message);
    ui_status.message = status_message.c_str();
    (void)display.draw_status(ui_status);
}

void render_document() {
    cursor->erase();
    const auto rows=document.screen(display.cols(),touch_keyboard.visible?14:display.rows());
    for (std::size_t i=0;i<rows.size();++i) {
        if (i<previous_rows.size() && rows[i].text==previous_rows[i].text && rows[i].style==previous_rows[i].style) continue;
        (void)terminal->feed("\x1b["+std::to_string(i+1)+";1H\x1b[0m\x1b[2K");
        if (rows[i].style) (void)terminal->feed("\x1b["+std::to_string(rows[i].style)+"m");
        (void)terminal->feed(rows[i].text);
    }
    previous_rows=rows;
    (void)terminal->feed("\x1b[0m\x1b["+std::to_string(document.cursor_row()+1)+";"+
                         std::to_string(document.cursor_col()+1)+"H");
    render_terminal();
}

void scroll_window(tab5_shell::ScrollAction action, std::size_t amount = 0) {
    using tab5_shell::ScrollAction;
    if (action == ScrollAction::none) return;
    if (document.active()) {
        (void)document.feed(tab5_shell::scroll_navigation(action)); render_document();
        return;
    }
    if (terminal->screen().alt_active()) {
        if (network.connected()) {
            const auto key = tab5_shell::scroll_navigation(action);
            network.send(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(key.data()), key.size()));
        }
        return;
    }
    if (!amount) amount = std::max<std::size_t>(1, visible_rows() - 2);
    if (scrollback.move(action, terminal->screen(), amount)) redraw_window();
}

int dispatch(std::string_view command) {
    auto parsed=tab5_shell::parse_command(command);
    if (!parsed.ok || parsed.arguments.empty()) return shell.execute(command,feed_output);
    const auto& args=parsed.arguments;
    const auto& name=args.front();
    if (name=="nano" || name=="glow") {
        bool recover=args.size()==3 && args[1]=="--recover" && name=="nano";
        if ((!recover && args.size()!=2) || (name=="glow" && recover)) {
            feed_output("usage: nano [--recover] FILE | glow FILE\n"); return 2;
        }
        std::string error;
        if (!document.open(args.back(),name=="glow",recover,error)) { feed_output(error+"\n"); return 1; }
        (void)terminal->feed("\x1b[?1049h\x1b[2J\x1b[H"); previous_rows.clear(); render_document();
        return 0;
    }
    if (name=="ssh") {
        std::vector<std::string> options(args.begin()+1,args.end()); std::string error;
        if (!tab5_shell::parse_ssh(options,pending_ssh,error)) { feed_output(error+"\n"); return 2; }
        // No credential enters history. A named profile may supply a key; an
        // empty password lets the existing private-key authenticator run.
        password_prompt=true; password.clear();
        feed_output("Password (hidden; Enter for profile/key; ^C cancels): ");
        return 0;
    }
    if (name=="pkg") return packages.command(std::vector<std::string>(args.begin()+1,args.end()),feed_output);
    if (name=="download") {
        if (args.size()!=3) { feed_output("usage: download https://HOST/PATH /home/FILE\n"); return 2; }
        std::string error;
        if (!tab5_shell::https_download(shell,args[1],args[2],error)) { feed_output(error+"\n"); return 1; }
        feed_output("Downloaded (TLS verified, at most 64 KiB)\n"); return 0;
    }
    if (name=="wifi" && args.size()==1) { feed_output(network.wifi_status()+"\n"); return 0; }
    if (name=="help") {
        int status=shell.execute(command,feed_output);
        feed_output("nano [--recover] FILE; glow FILE; ssh [-p PORT] [-i /private/KEY] user@host\n"
                    "ssh --profile NAME; wifi (status only); ^] disconnects SSH\n"
                    "pkg install MANIFEST.pkg | list | remove NAME; installed NAME [args]\n"
                    "download https://HOST/PATH /home/FILE; KEYS toggles touch keyboard\n"
                    "Scroll: Shift+PgUp/PgDn/Home/End, swipe down/up, UP/DOWN/LIVE\n"); return status;
    }
    int installed=packages.run(name,std::vector<std::string>(args.begin()+1,args.end()),feed_output);
    if (installed!=127) return installed;
    return shell.execute(command,feed_output);
}

void return_to_console() {
    if (document.active() && !document.checkpoint()) { set_status(1,"Draft failed; save before Console"); return; }
    if (network.busy()) { session.stop(); network.stop(); set_status(1,"Stopping SSH; tap Console again when local"); return; }
    tab5_sd_unmount();
    const esp_err_t result = tab5_app_boot_return_to_console();
    if (result != ESP_OK) {
        set_status(1, std::string("Console return failed: ") + esp_err_to_name(result));
    }
}

}  // namespace

extern "C" void app_main() {
    esp_log_level_set("*", ESP_LOG_INFO);
    std::printf("TAB5_SHELL_BOOT version=%s target=ESP32-P4 local_only=1\n",
                esp_app_get_description()->version);

    const bool nvs_ready = nvs_flash_init() == ESP_OK;  // Shared NVS is never erased.
    const auto board = tab5_board_init();
    const bool display_ready = board.display_ready && display.init().has_value();

    shell = tab5_shell::Shell(
        {kHomeRoot, kSdRoot, board.sd_ready},
        []() {
            return tab5_shell::MemoryInfo{
                heap_caps_get_total_size(MALLOC_CAP_DEFAULT),
                esp_get_free_heap_size(),
                esp_get_minimum_free_heap_size(),
            };
        },
        []() {
            tab5_shell::DiskInfo info;
            info.available = tab5_sd_usage(&info.total_bytes, &info.free_bytes);
            return info;
        });
    std::string storage_error;
    const bool home_ready = shell.prepare(storage_error);
    if (!board.sd_ready)
        status_message = "SD unavailable; diagnostics only";
    else if (!home_ready)
        status_message = "HOME unavailable; /sd read-only";
    else
        status_message = "READY / LOCAL";

    key_queue = xQueueCreate(256, sizeof(std::uint8_t));
    scroll_actions = xQueueCreate(16, sizeof(tab5_shell::ScrollAction));
    tab5_keyboard_set_key_filter(usb_scroll_filter, nullptr);
    const bool tab5_keyboard_ready = key_queue &&
        tab5::start_tab5_kbd_input(on_tab5_keyboard, {}, queue_scroll_action);

    terminal = std::make_unique<term::Terminal>(display.cols(), display.rows(), display);
    terminal->screen().set_response_sink([](std::span<const std::uint8_t> bytes) {
        if (network.connected()) network.send(bytes);
    });
    cursor = std::make_unique<tab5_shell::CursorRenderer>(terminal->screen(), display);

    ui_status.sd_ready = board.sd_ready;
    ui_status.home_ready = home_ready;
    ui_status.tab5_keyboard_ready = tab5_keyboard_ready;
    ui_status.usb_keyboard_ready = board.usb_hid_ready;
    ui_status.last_status = 0;
    ui_status.message = status_message.c_str();

    const bool chrome_ready = display_ready && display.draw_chrome(ui_status, false);
    bool terminal_ready = false;
    bool presented = false;
    bool first_frame = chrome_ready;
    if (first_frame) {
        feed_output("TAB5 Shell 0.1.3 - local P4 shell\n"
                    "No SSH profile or radio connection is required.\n"
                    "Writes are confined to /home. /sd is read-only.\n");
        if (!board.sd_ready)
            feed_output("SD unavailable: help, clear, pwd, cd, echo and free remain available.\n");
        else if (!home_ready)
            feed_output(std::string("Storage warning: ") + storage_error + "\n");
        feed_output("\nType 'help' for commands. Ctrl+C cancels the current input line.\n\n");
        feed_prompt();
        terminal_ready = terminal->render_dirty().has_value();
        first_frame = terminal_ready;
        if (first_frame) cursor->sync_after_render();
        presented = first_frame && display.present_all();
        first_frame = presented;
    }
    std::printf("TAB5_SHELL_FIRST_FRAME display=%d chrome=%d terminal=%d presented=%d\n",
                display_ready, chrome_ready, terminal_ready, presented);

    const bool console_available = tab5_app_boot_console_available();
    const bool healthy = tab5_shell::can_confirm_local_shell(
        {nvs_ready, display_ready, console_available, false, board.sd_ready});
    const bool boot_confirmed = healthy && first_frame &&
                                tab5_app_boot_ready(true) == ESP_OK;
    const std::uint64_t boot_deadline = milliseconds() + 15000;
    std::printf("TAB5_SHELL_STATUS healthy=%d first_frame=%d confirmed=%d "
                "radio=NONE sd=%d home=%d\n",
                healthy, first_frame, boot_confirmed, board.sd_ready, home_ready);

    bool touching = false;
    unsigned touch_releases = 0;
    int touch_x = 0;
    int touch_y = 0;
    int touch_last_x = 0, touch_last_y = 0;
    std::uint64_t next_cursor_blink = milliseconds() + 600;
    bool previous_tab5_keyboard = tab5_keyboard_ready;
    bool previous_usb_keyboard = board.usb_hid_ready;

    for (;;) {
        const std::uint64_t now = milliseconds();
        if (!boot_confirmed && now >= boot_deadline) {
            return_to_console();
            esp_restart();
        }

        tab5_shell::ScrollAction scroll_action{};
        while (scroll_actions && xQueueReceive(scroll_actions, &scroll_action, 0) == pdTRUE)
            scroll_window(scroll_action);

        std::uint8_t usb_bytes[32];
        std::size_t usb_count = 0;
        while ((usb_count = tab5_keyboard_poll(usb_bytes, sizeof(usb_bytes))) > 0) {
            for (std::size_t index = 0; index < usb_count; ++index)
                if (key_queue) xQueueSend(key_queue, &usb_bytes[index], 0);
        }

        std::uint8_t byte = 0;
        if (key_queue && xQueueReceive(key_queue, &byte, 0) == pdTRUE) {
            std::uint8_t sequence[8]{byte};
            std::size_t length = 1;
            if (byte == 0x1b) {
                while (length < sizeof(sequence) &&
                       xQueueReceive(key_queue, &byte, pdMS_TO_TICKS(10)) == pdTRUE) {
                    sequence[length++] = byte;
                    if (length >= 3 &&
                        (sequence[length - 1] >= 'A' && sequence[length - 1] <= 'Z'))
                        break;
                    if (length >= 4 && sequence[length - 1] == '~') break;
                }
            }

            const std::string_view key(reinterpret_cast<const char*>(sequence), length);
            if (session.state()==tab5_shell::SessionState::local && !password_prompt &&
                !document.active() && (key=="\x1b[5~" || key=="\x1b[6~")) {
                scroll_window(key=="\x1b[5~" ? tab5_shell::ScrollAction::older : tab5_shell::ScrollAction::newer);
            } else {
            if (scrollback.active()) scroll_window(tab5_shell::ScrollAction::live);
            if (session.state()!=tab5_shell::SessionState::local) {
                if (key=="\x1d" || (key=="\x03" && !network.connected())) { session.stop(); network.stop(); }
                else if (network.connected()) network.send(std::span<const std::uint8_t>(sequence,length));
            } else if (password_prompt) {
                if (key=="\x03") { password_prompt=false; password.clear(); feed_output("^C\n"); feed_prompt(); }
                else if (key=="\r" || key=="\n") {
                    password_prompt=false; feed_output("\n");
                    std::string error;
                    terminal->screen().set_scrollback_capture_rows(display.rows());
                    (void)terminal->feed("\x1b[r");
                    if (session.begin() && network.start(pending_ssh,std::move(password),error)) {
                        feed_output("Connecting with strict SD host-key pins; ^] cancels.\n");
                    } else {
                        session.finish_connect(false); configure_local_window();
                        feed_output(error+"\n"); feed_prompt();
                    }
                } else if (key=="\x7f" || key=="\x08") { if (!password.empty()) password.pop_back(); }
                else if (length==1 && sequence[0]>=32 && sequence[0]<=126 && password.size()<255) password+=key;
                render_terminal();
            } else if (document.active()) {
                if (!document.feed(key)) {
                    (void)terminal->feed("\x1b[?1049l");
                    configure_local_window();
                    feed_prompt(); render_terminal();
                } else {
                    if (document.dirty() && !draft_due) draft_due=now+2000;
                    render_document();
                }
            } else {
            const auto edit = editor.feed(key);
            if (edit.interrupt) {
                cursor->erase();
                (void)terminal->feed("^C\r\n");
                editor.reset();
                feed_prompt();
                render_terminal();
                set_status(130, "input cancelled");
            } else if (edit.execute) {
                const std::string command = editor.line();
                editor.remember(command);
                cursor->erase();
                (void)terminal->feed("\r\n");
                const int status = dispatch(command);
                editor.reset();
                if (status == tab5_shell::kStatusExit) {
                    render_terminal();
                    return_to_console();
                    esp_restart();
                }
                if (!document.active() && !password_prompt) feed_prompt();
                render_terminal();
                if (!command.empty()) set_status(status, status == 0 ? "OK" : "COMMAND FAILED");
            } else if (edit.changed) {
                redraw_input_line();
            }
            }
            }
        }

        if (document.active() && draft_due && now>=draft_due) {
            draft_due=0;
            if (!document.checkpoint()) draft_due=now+2000;
            render_document();
        }
        std::uint8_t remote[1024];
        auto remote_count=network.receive(remote,sizeof(remote));
        if (remote_count) { cursor->erase(); (void)terminal->feed(std::span<const std::uint8_t>(remote,remote_count)); render_terminal(); }
        if (session.state()==tab5_shell::SessionState::connecting && network.connected()) {
            session.finish_connect(true); set_status(0,"SSH: ^] disconnects");
        }
        if (session.state()!=tab5_shell::SessionState::local && !network.busy()) {
            reset_after_remote();
            session.transport_idle();
            feed_output(std::string(network.result())+"\n"); editor.reset(); feed_prompt(); render_terminal();
            set_status(0,"LOCAL");
        }

        int x = 0;
        int y = 0;
        const bool pressed = tab5_touch_poll(&x, &y);
        if (pressed) {
            if (!touching) {
                touching = true;
                touch_x = x;
                touch_y = y;
            }
            touch_last_x = x; touch_last_y = y;
            touch_releases = 0;
        } else if (touching && ++touch_releases >= 2) {
            touching = false;
            const auto gesture = tab5_shell::scroll_swipe(touch_x, touch_y, touch_last_x,
                touch_last_y, touch_keyboard.visible ? tab5_shell::TouchKeyboard::top : 684);
            const auto button = tab5_shell::scroll_button(touch_x, touch_y);
            if (gesture != tab5_shell::ScrollAction::none) {
                scroll_window(gesture, static_cast<std::size_t>(std::abs(touch_last_y-touch_y)/27));
            } else if (button != tab5_shell::ScrollAction::none) scroll_window(button);
            else if (display.console_hit_test(touch_x, touch_y)) return_to_console();
            else if (tab5_shell::TouchKeyboard::toggle_hit(touch_x,touch_y)) {
                if (scrollback.active()) scroll_window(tab5_shell::ScrollAction::live);
                touch_keyboard.visible=!touch_keyboard.visible;
                display.keyboard_visible(touch_keyboard.visible);
                if (touch_keyboard.visible) {
                    if (session.state()==tab5_shell::SessionState::local && !document.active()) {
                        configure_local_window();
                        if (password_prompt) { feed_output("Password (hidden): "); render_terminal(); }
                        else redraw_input_line();
                    }
                    display.draw_touch_keyboard(touch_keyboard);
                } else {
                    if (session.state()==tab5_shell::SessionState::local && !document.active())
                        configure_local_window();
                    terminal->screen().mark_region(14,0,24,80); render_terminal();
                }
                if (document.active()) { previous_rows.clear(); render_document(); }
            } else if (touch_keyboard.visible) {
                auto key=touch_keyboard.tap(touch_x,touch_y);
                for (unsigned char byte:key) if (key_queue) xQueueSend(key_queue,&byte,0);
                display.draw_touch_keyboard(touch_keyboard);
            }
        }

        const bool tab5_keyboard = tab5::tab5_kbd_connected();
        const bool usb_keyboard = tab5_keyboard_connected();
        if (tab5_keyboard != previous_tab5_keyboard || usb_keyboard != previous_usb_keyboard) {
            previous_tab5_keyboard = tab5_keyboard;
            previous_usb_keyboard = usb_keyboard;
            ui_status.tab5_keyboard_ready = tab5_keyboard;
            ui_status.usb_keyboard_ready = usb_keyboard;
            (void)display.draw_status(ui_status);
        }

        if (now >= next_cursor_blink) {
            next_cursor_blink = now + 600;
            if (!scrollback.active()) cursor->toggle_blink();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

#include "cache_model.h"
#include "console_bridge.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tab5_hal.h"
#include "tab5_weather_wifi.h"
#include "ui.h"
#include "weather_http.h"
#include "search_flow.h"
#include "tab5_weather_snapshot.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <string>
namespace {
using namespace wm;
View view;
CacheBook caches;
bool has_nvs = false, net_started = false, worker = false, job_busy = false, sntp = false,
     redraw = true;
uint32_t search_generation = 0;
uint64_t search_after = 0, message_until = 0;
char message[45]{};
SearchFlow search_flow;
bool searching_job = false;
uint64_t uptime_ms() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

void clear_wifi_password() {
    volatile char *p = view.wifi_password;
    for (size_t i = 0; i < sizeof(view.wifi_password); ++i) p[i] = 0;
}

bool safe_manifest_value(const char *raw, const char *key, char *out, size_t capacity) {
    if (!raw || !key || !out || capacity < 2) return false;
    char marker[24];
    const int n = std::snprintf(marker, sizeof(marker), "\"%s\"", key);
    if (n <= 0 || size_t(n) >= sizeof(marker)) return false;
    const char *p = std::strstr(raw, marker);
    if (!p) return false;
    p += n;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p++ != ':') return false;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p++ != '"') return false;
    size_t used = 0;
    while (*p && *p != '"' && used + 1 < capacity) {
        const unsigned char c = static_cast<unsigned char>(*p++);
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == ' '))
            return false;
        out[used++] = static_cast<char>(c);
    }
    if (!used || *p != '"') return false;
    out[used] = 0;
    return true;
}

void scan_sd_library() {
    view.sd_count = 0;
    view.sd_page = 0;
    DIR *dir = opendir("/sdcard/firmware");
    if (!dir) return;
    while (view.sd_count < 8) {
        auto *entry = readdir(dir);
        if (!entry) break;
        const size_t len = std::strlen(entry->d_name);
        if (len < 6 || std::strcmp(entry->d_name + len - 5, ".json") != 0) continue;
        char path[256]{};
        const int path_length = std::snprintf(path, sizeof(path), "/sdcard/firmware/%s",
                                              entry->d_name);
        if (path_length <= 0 || size_t(path_length) >= sizeof(path)) continue;
        FILE *file = fopen(path, "rb");
        if (!file) continue;
        char raw[1024]{};
        const size_t size = fread(raw, 1, sizeof(raw) - 1, file);
        const bool truncated = size == sizeof(raw) - 1 && fgetc(file) != EOF;
        raw[size] = '\0';
        fclose(file);
        if (truncated || size == 0) continue;
        SdPackage package{};
        if (!safe_manifest_value(raw, "project", package.project, sizeof(package.project)) ||
            !safe_manifest_value(raw, "version", package.version, sizeof(package.version)) ||
            !safe_manifest_value(raw, "image", package.image, sizeof(package.image)) ||
            !safe_manifest_value(raw, "license", package.license, sizeof(package.license)))
            continue;
        view.sd_packages[view.sd_count++] = package;
    }
    closedir(dir);
    view.sd_selected = 0;
}
void status(const char *text, uint64_t now);
void resolve_sd_action(Action &a, uint64_t now) {
    if (a.kind != Kind::sd_select) return;
    const bool ok = view.sd_ready && a.value < view.sd_count;
    if (ok) {
        view.sd_selected = a.value;
        status("Selected; return to Console to validate and launch", now);
    } else {
        status("SD package unavailable", now);
    }
}
void sync_wifi_view(const tab5_weather_wifi_status_t &wifi) {
    view.wifi_scanning = wifi.scanning;
    view.wifi_has_saved = wifi.has_saved_network;
    view.wifi_count = std::min<unsigned>(wifi.network_count, max_wifi_networks);
    std::snprintf(view.wifi_ssid, sizeof(view.wifi_ssid), "%s", wifi.ssid);
    for (unsigned i = 0; i < view.wifi_count; ++i) {
        tab5_weather_wifi_network_t network{};
        if (!tab5_weather_wifi_get_network(i, &network)) {
            view.wifi_count = i;
            break;
        }
        std::snprintf(view.wifi_networks[i].ssid, sizeof(view.wifi_networks[i].ssid), "%s",
                      network.ssid);
        view.wifi_networks[i].rssi = network.rssi;
        view.wifi_networks[i].secure = network.secure;
    }
    view.wifi_page = std::min(view.wifi_page, view.wifi_count ? (view.wifi_count - 1) / 3 : 0);
}

uint32_t rgb888(uint16_t colour) {
    const uint32_t red = (colour >> 11) & 0x1f;
    const uint32_t green = (colour >> 5) & 0x3f;
    const uint32_t blue = colour & 0x1f;
    return (((red << 3) | (red >> 2)) << 16) |
           (((green << 2) | (green >> 4)) << 8) |
           ((blue << 3) | (blue >> 2));
}

bool render_weather() {
    // First milestone: preserve Weather Micro's audited 320x240 scene and use a
    // uniform 3x scale. Native 1280x720 redesign can then change layout without
    // coupling network/parser validation to a simultaneous UI rewrite.
    constexpr int scale = 3, left = (TAB5_DISPLAY_WIDTH - 320 * scale) / 2;
    prepare(view);
    if (!scene_valid()) return false;
    const uint32_t background = rgb888(pixel(0, 0));
    if (tab5_display_rect(0, 0, TAB5_DISPLAY_WIDTH, TAB5_DISPLAY_HEIGHT, background) != ESP_OK)
        return false;
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320;) {
            const uint16_t colour = pixel(x, y);
            int end = x + 1;
            while (end < 320 && pixel(end, y) == colour) ++end;
            if (tab5_display_rect(left + x * scale, y * scale, (end - x) * scale, scale,
                                  rgb888(colour)) != ESP_OK)
                return false;
            x = end;
        }
    }
    return tab5_display_present() == ESP_OK;
}
void status(const char *text, uint64_t now) {
    std::snprintf(message, sizeof(message), "%s", text);
    message_until = now + 5000;
    redraw = true;
}
Cache *cache(int64_t id, bool create = true) { return caches.find(id, create); }
void publish_console_weather(const Location &place, const Forecast &forecast) {
    if (!has_nvs) return;
    tab5_weather_snapshot_t snapshot{};
    snapshot.has_temperature = forecast.temp_ok;
    snapshot.temperature_tenths_c = forecast.temp_ok ? int32_t(std::lround(forecast.temperature * 10)) : 0;
    snapshot.fahrenheit = view.settings.fahrenheit;
    std::snprintf(snapshot.city, sizeof(snapshot.city), "%s", place.name);
    std::snprintf(snapshot.conditions, sizeof(snapshot.conditions), "%s", condition(forecast.code));
    const auto result = tab5_weather_snapshot_store(&snapshot);
    if (result != ESP_OK)
        std::printf("TAB5_WX_SUMMARY store=%s values=WITHHELD\n", esp_err_to_name(result));
}
void prune() { caches.prune(view.settings); }
bool persist_settings(const Settings &settings, void *) {
    if (!has_nvs)
        return false;
    auto *data = static_cast<char *>(std::malloc(max_settings));
    if (!data)
        return false;
    bool ok = encode_settings(settings, data, max_settings);
    nvs_handle_t h{};
    if (ok && nvs_open("wx_ui", NVS_READWRITE, &h) == ESP_OK) {
        auto e = nvs_set_blob(h, "settings", data, std::strlen(data));
        if (e == ESP_OK)
            e = nvs_commit(h);
        ok = e == ESP_OK;
        nvs_close(h);
    } else
        ok = false;
    std::free(data);
    return ok;
}
bool persist() { return persist_settings(view.settings, nullptr); }
void load() {
    auto *data = static_cast<char *>(std::malloc(max_settings));
    if (!data)
        return;
    nvs_handle_t h{};
    if (nvs_open("wx_ui", NVS_READONLY, &h) == ESP_OK) {
        size_t n = max_settings;
        if (nvs_get_blob(h, "settings", data, &n) == ESP_OK && n <= max_settings)
            decode_settings(data, n, view.settings);
        nvs_close(h);
    }
    std::free(data);
}
void refresh(uint64_t now) {
    if (view.settings.count) {
        auto *c = cache(view.settings.places[view.settings.selected].id);
        if (c && now >= c->manual_after)
            c->due = now;
        else
            status("Please wait before refreshing", now);
    }
}
void action(Action a, uint64_t now) {
    resolve_sd_action(a, now);
    bool save = false;
    if (a.kind != Kind::none) {
        message_until = 0;
        redraw = true;
    }
    switch (a.kind) {
    case Kind::page:
        if (a.value <= unsigned(Page::sd_library)) {
            if (a.value == unsigned(Page::current) && view.page == Page::current)
                refresh(now);
            view.page = static_cast<Page>(a.value);
            if (view.page == Page::network && net_started && !view.wifi_scanning &&
                !view.wifi_count)
                (void)tab5_weather_wifi_scan();
            if (view.page == Page::sd_library) scan_sd_library();
        }
        break;
    case Kind::console:
        clear_wifi_password();
        if (console_available()) {
            tab5_display_set_brightness(0);
            tab5_sd_unmount();
            if (!return_to_console()) {
                view.console_ok = false;
                status("Console return unavailable", now);
            }
        }
        break;
    case Kind::theme:
        view.settings.light = !view.settings.light;
        save = true;
        break;
    case Kind::unit:
        view.settings.fahrenheit = !view.settings.fahrenheit;
        if (view.settings.count) {
            const auto &place = view.settings.places[view.settings.selected];
            const auto *c = cache(place.id, false);
            if (c && c->has) publish_console_weather(place, c->forecast);
        }
        save = true;
        break;
    case Kind::places:
        view.page = Page::locations;
        view.list_page = 0;
        break;
    case Kind::add:
        view.page = Page::search;
        search_flow.cancel();
        view.keyboard_page = 0;
        ++search_generation;
        break;
    case Kind::place:
        if (a.value < view.settings.count) {
            view.manage = a.value;
            view.page = Page::place;
        }
        break;
    case Kind::use:
        if (view.manage < view.settings.count) {
            view.settings.selected = view.manage;
            view.page = Page::current;
            save = true;
        }
        break;
    case Kind::remove:
        remove_place(view.settings, view.manage);
        prune();
        view.list_page =
            std::min(view.list_page, view.settings.count ? (view.settings.count - 1) / 3 : 0);
        view.page = Page::locations;
        save = true;
        break;
    case Kind::previous:
        if (view.page == Page::locations && view.list_page)
            --view.list_page;
        if (view.page == Page::results && view.result_page)
            --view.result_page;
        if (view.page == Page::network && view.wifi_page)
            --view.wifi_page;
        if (view.page == Page::sd_library && view.sd_page)
            --view.sd_page;
        break;
    case Kind::next:
        if (view.page == Page::locations && (view.list_page + 1) * 3 < view.settings.count)
            ++view.list_page;
        if (view.page == Page::results && (view.result_page + 1) * 3 < view.results.count)
            ++view.result_page;
        if (view.page == Page::network && (view.wifi_page + 1) * 3 < view.wifi_count)
            ++view.wifi_page;
        if (view.page == Page::sd_library && (view.sd_page + 1) * 3 < view.sd_count)
            ++view.sd_page;
        break;
    case Kind::key: {
        if (view.page == Page::wifi_password) {
            size_t n = std::strlen(view.wifi_password);
            if (n < 64 && a.value >= 32 && a.value <= 126) {
                view.wifi_password[n] = char(a.value);
                view.wifi_password[n + 1] = 0;
            }
        } else {
            search_flow.cancel();
            size_t n = std::strlen(view.query);
            if (n < 40 && a.value >= 32 && a.value <= 126) {
                view.query[n] = char(a.value);
                view.query[n + 1] = 0;
                ++search_generation;
            }
        }
        break;
    }
    case Kind::erase: {
        if (view.page == Page::wifi_password) {
            size_t n = std::strlen(view.wifi_password);
            if (n) view.wifi_password[n - 1] = 0;
        } else {
            search_flow.cancel();
            size_t n = std::strlen(view.query);
            if (n) {
                view.query[n - 1] = 0;
                ++search_generation;
            }
        }
        break;
    }
    case Kind::keyboard:
        if (view.page == Page::wifi_password) view.wifi_keyboard_page ^= 1;
        else view.keyboard_page ^= 1;
        break;
    case Kind::symbols:
        if (view.page == Page::wifi_password)
            view.wifi_keyboard_page = view.wifi_keyboard_page >= 2 ? 0 : 2;
        else view.keyboard_page = view.keyboard_page >= 2 ? 0 : 2;
        break;
    case Kind::search:
        if (!worker) status("Search service unavailable / restart", now);
        else if (now < search_after) status("Please wait before searching again", now);
        else if (search_flow.begin(view.query, ++search_generation, now))
            status("Preparing city search...", now);
        else status("Enter at least two letters", now);
        break;
    case Kind::result:
        if (a.value < view.results.count) {
            if (save_search_result(view.settings, view.results.places[a.value],
                                    persist_settings, nullptr)) {
                view.page = Page::current;
                status("City saved", now);
            } else
                status(view.settings.count >= max_places ? "List full / remove a saved place"
                                                         : "City not saved / storage unavailable", now);
        }
        break;
    case Kind::retry:
        if (!job_busy) {
            const esp_err_t result = tab5_weather_wifi_retry();
            status(result == ESP_OK ? "Retrying saved Wi-Fi" : "No saved Wi-Fi to retry", now);
        }
        break;
    case Kind::wifi_scan:
        view.wifi_page = 0;
        if (tab5_weather_wifi_scan() != ESP_OK) status("Wi-Fi scan could not start", now);
        break;
    case Kind::wifi_network:
        if (a.value < view.wifi_count) {
            const auto &network = view.wifi_networks[a.value];
            std::snprintf(view.wifi_selected_ssid, sizeof(view.wifi_selected_ssid), "%s",
                          network.ssid);
            view.wifi_selected_secure = network.secure;
            clear_wifi_password();
            view.wifi_keyboard_page = 0;
            if (network.secure) {
                view.page = Page::wifi_password;
            } else {
                const esp_err_t result = tab5_weather_wifi_connect(network.ssid, "");
                status(result == ESP_OK ? "Joining open Wi-Fi" : "Wi-Fi join could not start",
                       now);
            }
        }
        break;
    case Kind::wifi_join: {
        const size_t password_length = std::strlen(view.wifi_password);
        bool valid = view.wifi_selected_ssid[0] &&
                     (!view.wifi_selected_secure ||
                      (password_length >= 8 && password_length <= 64));
        if (valid && view.wifi_selected_secure && password_length == 64) {
            for (size_t i = 0; i < password_length; ++i) {
                const char c = view.wifi_password[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F'))) {
                    valid = false;
                    break;
                }
            }
        }
        if (!valid) {
            status(password_length == 64 ? "64 char key must be hexadecimal"
                                         : "Password must be 8 to 63 chars",
                   now);
            break;
        }
        const esp_err_t result = tab5_weather_wifi_connect(view.wifi_selected_ssid,
                                                           view.wifi_password);
        clear_wifi_password();
        view.page = Page::network;
        status(result == ESP_OK ? "Joining selected Wi-Fi" : "Wi-Fi join could not start", now);
        break;
    }
    case Kind::wifi_disconnect:
        if (tab5_weather_wifi_disconnect() != ESP_OK) status("Wi-Fi disconnect failed", now);
        break;
    case Kind::refresh:
        refresh(now);
        break;
    case Kind::back:
        search_flow.cancel();
        if (view.page == Page::wifi_password) {
            clear_wifi_password();
            view.page = Page::network;
        } else if (view.page == Page::place)
            view.page = Page::locations;
        else if (view.page == Page::results)
            view.page = Page::search;
        else {
            view.page = Page::settings;
            ++search_generation;
        }
        break;
    default:
        break;
    }
    if (save && !persist())
        status("Settings not saved", now);
}
void keyboard_input(const uint8_t *bytes, size_t count, uint64_t now) {
    if (!bytes || !count || (view.page != Page::search && view.page != Page::wifi_password))
        return;
    if (bytes[0] == 0x1b && count > 1) return; // Ignore ANSI navigation sequences.
    for (size_t i = 0; i < count; ++i) {
        const uint8_t key = bytes[i];
        if (key == 0x1b)
            action({Kind::back, 0}, now);
        else if (key == 8 || key == 127)
            action({Kind::erase, 0}, now);
        else if (key == '\r' || key == '\n')
            action({view.page == Page::wifi_password ? Kind::wifi_join : Kind::search, 0}, now);
        else if (key >= 32 && key <= 126)
            action({Kind::key, key}, now);
    }
}
class Touch {
    bool armed = false, pressed = false, cancelled = false;
    unsigned releases = 0;
    Action start{};

  public:
    Action sample(bool down, Action current) {
        if (!armed) {
            releases = down ? 0 : releases + 1;
            if (releases >= 3) {
                armed = true;
                releases = 0;
            }
            return {};
        }
        if (down) {
            releases = 0;
            if (!pressed) {
                pressed = true;
                cancelled = false;
                start = current;
            } else if (current != start)
                cancelled = true;
        } else if (pressed && ++releases >= 3) {
            pressed = false;
            releases = 0;
            return cancelled ? Action{} : start;
        }
        return {};
    }
};
} // namespace
extern "C" void app_main() {
    using namespace wm;
    has_nvs = nvs_flash_init() == ESP_OK;
    const auto board = tab5_board_init();
    view.sd_ready = board.sd_ready;
    if (has_nvs) load();
    if (view.sd_ready) scan_sd_library();
    view.console_ok = console_available();
    worker = fetch_init();
    tab5_weather_wifi_status_t wifi{};
    if (tab5_radio_power_set(true) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(250));
        net_started = tab5_weather_wifi_init() == ESP_OK && tab5_weather_wifi_start() == ESP_OK;
    }
    tab5_weather_wifi_get_status(&wifi);
    sync_wifi_view(wifi);
    const bool healthy = has_nvs && board.display_ready && board.touch_ready &&
                         board.sd_ready && worker && view.console_ok;
    bool boot_confirmed = false;
    const uint64_t boot_deadline = uptime_ms() + 15000;
    std::printf("TAB5_WX_BOOT healthy=%d worker=%d configured_wifi=%d values=WITHHELD\n", healthy,
                worker, wifi.has_saved_network);
    if (!healthy) status("Startup issue / return to Console", uptime_ms());
    Touch touch;
    bool connected = false;
    uint32_t wifi_revision = wifi.revision;
    int64_t minute = -1;
    uint64_t second = 0;
    static Result result;
    for (;;) {
        uint64_t now = uptime_ms();
        if (!boot_confirmed && now >= boot_deadline) {
            std::printf("TAB5_WX_BOOT startup_deadline=FAILED returning_to_console\n");
            if (!return_to_console()) esp_restart();
        }
        tab5_weather_wifi_get_status(&wifi);
        if (wifi.revision != wifi_revision) {
            wifi_revision = wifi.revision;
            sync_wifi_view(wifi);
            redraw = true;
        }
        view.connected = net_started && wifi.has_ip;
        const std::time_t epoch = std::time(nullptr);
        view.clock_valid = epoch >= 1704067200;
        view.now = view.clock_valid ? epoch : 0;
        if (view.connected != connected) {
            connected = view.connected;
            redraw = true;
        }
        if (view.connected && !sntp) {
            esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
                2, ESP_SNTP_SERVER_LIST("time.google.com", "pool.ntp.org"));
            sntp = esp_netif_sntp_init(&cfg) == ESP_OK;
        }
        if (fetch_result(result)) {
            job_busy = false;
            searching_job = false;
            redraw = true;
            if (result.kind == JobKind::search) {
                search_after = std::max(search_after, now + uint64_t(result.retry_after) * 1000);
                if (result.generation == search_generation && view.page == Page::search) {
                    if (result.ok) {
                        view.results = result.search;
                        view.result_page = 0;
                        view.page = Page::results;
                        status(view.results.count ? "Tap a city below to save it"
                                                  : "No matching city / try another name", now);
                    } else
                        status(fetch_error(result.error), now);
                }
            } else {
                auto *c = caches.response(view.settings, result.place_id);
                if (c) {
                    if (result.ok && (result.forecast.current_time > view.now + 3600 ||
                                      view.now - result.forecast.current_time > 7200)) {
                        result.ok = false;
                        result.error = FetchError::json;
                    }
                    if (result.ok) {
                        c->forecast = result.forecast;
                        c->has = true;
                        c->updated = now;
                        c->due = now + 900000;
                        c->failures = 0;
                        c->error[0] = 0;
                        for (unsigned i = 0; i < view.settings.count; ++i)
                            if (view.settings.places[i].id == result.place_id) {
                                publish_console_weather(view.settings.places[i], c->forecast);
                                break;
                            }
                    } else {
                        c->manual_after =
                            std::max(c->manual_after, now + uint64_t(result.retry_after) * 1000);
                        c->failures = std::min(c->failures + 1, 5u);
                        c->due = now +
                                 std::max(uint64_t(std::min(60u << (c->failures - 1), 900u)) * 1000,
                                          uint64_t(result.retry_after) * 1000);
                        std::snprintf(c->error, sizeof(c->error), "%s", fetch_error(result.error));
                    }
                }
            }
        }
        int x = 0, y = 0;
        bool down = board.touch_ready && tab5_touch_poll(&x, &y);
        constexpr int scale = 3, left = (TAB5_DISPLAY_WIDTH - 320 * scale) / 2;
        const bool inside = down && x >= left && x < left + 320 * scale;
        action(touch.sample(down, inside ? hit(view, (x - left) / scale, y / scale) : Action{}),
               now);
        uint8_t keys[32];
        size_t key_count = 0;
        while ((key_count = tab5_keyboard_poll(keys, sizeof(keys))) > 0)
            keyboard_input(keys, key_count, now);
        if (search_flow.pending()) {
            const char *waiting = nullptr;
            switch (search_flow.poll(view.connected, view.clock_valid, job_busy, now)) {
            case SearchFlow::Step::offline: waiting = "Waiting for Wi-Fi / Settings > Network"; break;
            case SearchFlow::Step::clock: waiting = "Synchronising time for secure search"; break;
            case SearchFlow::Step::busy: waiting = "Waiting for current request..."; break;
            case SearchFlow::Step::expired: waiting = "Search timed out / check Network and retry"; break;
            case SearchFlow::Step::ready:
                if (fetch_start(search_flow.job())) {
                    search_flow.submitted();
                    job_busy = searching_job = true;
                    search_after = now + 3000;
                    status("Searching cities...", now);
                }
                break;
            default: break;
            }
            if (waiting && (std::strcmp(message, waiting) || message_until <= now))
                status(waiting, now);
        }
        view.searching = search_flow.pending() || searching_job;
        view.busy = job_busy;
        if (view.settings.count) {
            auto *c = cache(view.settings.places[view.settings.selected].id);
            view.has = c && c->has;
            if (c) {
                view.forecast = c->forecast;
                view.stale =
                    c->has && (!view.connected || c->error[0] || now - c->updated > 1800000);
                if (worker && view.connected && view.clock_valid && !job_busy && !search_flow.pending() && now >= c->due &&
                    view.page != Page::search && view.page != Page::results) {
                    Job job;
                    job.location = view.settings.places[view.settings.selected];
                    if (fetch_start(job)) {
                        job_busy = true;
                        view.busy = true;
                        c->manual_after = now + 30000;
                        redraw = true;
                    }
                }
            }
        } else {
            view.has = false;
            view.forecast = Forecast{};
            view.stale = false;
        }
        view.status[0] = 0;
        if (message_until > now)
            std::snprintf(view.status, sizeof(view.status), "%s", message);
        else if (view.page == Page::network)
            std::snprintf(view.status, sizeof(view.status), "%s", wifi.message);
        else if (!worker)
            std::snprintf(view.status, sizeof(view.status), "Network worker unavailable");
        else if (view.connected && !view.clock_valid)
            std::snprintf(view.status, sizeof(view.status), "Synchronising time for HTTPS");
        else if (view.settings.count && !job_busy) {
            auto *c = cache(view.settings.places[view.settings.selected].id);
            if (c && c->error[0])
                std::snprintf(view.status, sizeof(view.status), "%s%.36s",
                              view.has ? "STALE / " : "", c->error);
        }
        if (view.now / 60 != minute || now / 1000 != second) {
            if (view.now / 60 != minute) {
                minute = view.now / 60;
                redraw = true;
            }
            if (message_until && now >= message_until) {
                message_until = 0;
                redraw = true;
            }
            second = now / 1000;
        }
        if (redraw && board.display_ready) {
            const bool drawn = render_weather();
            if (!boot_confirmed && healthy && drawn) {
                boot_confirmed = confirm_first_boot(true);
                if (!boot_confirmed) status("Console recovery setup failed", now);
                std::printf("TAB5_WX_UI_READY main_stack_free=%u values=WITHHELD\n",
                            unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            }
            std::printf("TAB5_WX_UI page=%u stack_min_bytes=%u values=WITHHELD\n",
                        unsigned(view.page), unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            redraw = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

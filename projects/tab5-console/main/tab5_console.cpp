/* TAB5 Console 0.3.5 — Files & HTML View auto-launch, SD Library 2x font & keyboard nav. */
#include "sdkconfig.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_partition.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pixel_fonts.h"
#include "tab5_hal.h"
#include "tab5_radio.h"
#include "tab5_installer.h"
#include "tab5_app_boot.h"
#include "tab5_weather_snapshot.h"

#include "input_tab5_kbd.hpp"
#include "matrix_clock.hpp"
#include "power_layout.hpp"

namespace {
constexpr const char *TAG = "TAB5_CONSOLE";
constexpr const char *UPDATE_DESCRIPTION = "PURPLE CAMERA TAB & 32-PKG SD LIBRARY";
constexpr const char *OLLAMA_PROJECT = "Tab5OllamaAnalytics";
constexpr const char *SHELL_PROJECT = "Tab5Shell";
constexpr const char *FILESERVER_PROJECT = "Tab5FileServer";
constexpr const char *HTMLVIEWER_PROJECT = "Tab5HtmlViewer";
constexpr const char *CAMERA_PROJECT = "Tab5Camera";

struct Theme {
    uint32_t bg, surface, surface_pressed, border;
    uint32_t text, muted, disabled;
};

constexpr Theme DARK{
    0x101215, 0x1d2228, 0x2a3139, 0x353d46,
    0xf7f8fa, 0x9ba6b2, 0x59636f,
};
constexpr Theme LIGHT{
    0xeef1f4, 0xffffff, 0xdde2e8, 0xc8cfd7,
    0x15181c, 0x59636f, 0xa4acb5,
};
constexpr uint32_t BLUE = 0x3b82f6;
constexpr uint32_t ORANGE = 0xf59e0b;
constexpr uint32_t RED = 0xef4444;
constexpr uint32_t GREEN = 0x22c55e;
constexpr uint32_t YELLOW = 0xeab308;
constexpr uint32_t PURPLE = 0xa855f7;

using cyd_fonts::Font;
using cyd_fonts::Glyph;

Theme theme = DARK;
bool dark_mode = true;
bool wifi_on = false;
bool bluetooth_on = false;
uint8_t brightness = 70;
uint8_t volume = 50;
uint8_t display_rotation = 0;
nvs_handle_t settings = 0;
bool settings_ready = false;
tab5_weather_snapshot_t weather_snapshot{};
bool weather_saved = false;

enum class Overlay { none, wifi, bluetooth, installer, clock };
Overlay overlay = Overlay::none;
enum class AutoLaunch { none, ollama, shell, fileserver, htmlviewer, camera };
AutoLaunch installer_auto_launch = AutoLaunch::none;

const char *auto_launch_project(AutoLaunch target)
{
    switch (target) {
    case AutoLaunch::shell:      return SHELL_PROJECT;
    case AutoLaunch::fileserver: return FILESERVER_PROJECT;
    case AutoLaunch::htmlviewer: return HTMLVIEWER_PROJECT;
    case AutoLaunch::camera:     return CAMERA_PROJECT;
    case AutoLaunch::ollama:
    default:                     return OLLAMA_PROJECT;
    }
}

const char *auto_launch_finding_message(AutoLaunch target)
{
    switch (target) {
    case AutoLaunch::shell:      return "FINDING SHELL PACKAGE";
    case AutoLaunch::fileserver: return "FINDING FILE SERVER PACKAGE";
    case AutoLaunch::htmlviewer: return "FINDING HTML VIEWER PACKAGE";
    case AutoLaunch::camera:     return "FINDING CAMERA PACKAGE";
    case AutoLaunch::ollama:
    default:                     return "FINDING OLLAMA PACKAGE";
    }
}

const char *auto_launch_checking_message(AutoLaunch target)
{
    switch (target) {
    case AutoLaunch::shell:      return "CHECKING SHELL FIRMWARE";
    case AutoLaunch::fileserver: return "CHECKING FILE SERVER FIRMWARE";
    case AutoLaunch::htmlviewer: return "CHECKING HTML VIEWER FIRMWARE";
    case AutoLaunch::camera:     return "CHECKING CAMERA FIRMWARE";
    case AutoLaunch::ollama:
    default:                     return "CHECKING OLLAMA FIRMWARE";
    }
}

const char *auto_launch_missing_message(AutoLaunch target)
{
    switch (target) {
    case AutoLaunch::shell:      return "TAB5 SHELL PACKAGE NOT FOUND";
    case AutoLaunch::fileserver: return "TAB5 FILE SERVER PACKAGE NOT FOUND";
    case AutoLaunch::htmlviewer: return "TAB5 HTML VIEWER PACKAGE NOT FOUND";
    case AutoLaunch::camera:     return "TAB5 CAMERA PACKAGE NOT FOUND";
    case AutoLaunch::ollama:
    default:                     return "OLLAMA ANALYTICS PACKAGE NOT FOUND";
    }
}

enum class ClockField { year, month, day, hour, minute, second };
bool clock_edit_mode = false;
ClockField editing_field = ClockField::day;
tab5_clock_time editing_time {};
char clock_status_message[64] = {0};
bool clock_sync_in_progress = false;

struct ClockZone {
    const char *name;
    const char *abbr;
    const char *tz;
};
constexpr ClockZone CLOCK_ZONES[] = {
    {"Sydney", "AEDT", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Melbourne", "AEDT", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Brisbane", "AEST", "AEST-10"},
    {"Adelaide", "ACDT", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Perth", "AWST", "AWST-8"},
    {"UTC", "UTC", "UTC0"},
};
constexpr size_t CLOCK_ZONE_COUNT = sizeof(CLOCK_ZONES) / sizeof(CLOCK_ZONES[0]);
size_t current_zone = 0;

static const char *MONTH_NAMES[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
static const char *WEEKDAY_NAMES[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

bool wifi_password_mode = false;
bool password_visible = false;
bool password_shift = false;
bool password_symbols = false;
char selected_ssid[TAB5_RADIO_SSID_LEN] = {0};
char wifi_password[65] = {0};
char saved_wifi_ssid[TAB5_RADIO_SSID_LEN] = {0};
char saved_wifi_pass[65] = {0};
char pairing_pin[7] = {0};
char ble_test[73] = {0};
const char *ble_last_key = "";
unsigned ble_escape = 0;
size_t wifi_scroll = 0;
size_t ble_scroll = 0;
tab5_installer_catalog_t installer_catalog {};
size_t installer_scroll = 0;
bool pending_installer_scan = false;
int installer_selected = -1;
bool installer_busy = false;
bool installer_ready = false;
bool installer_worker_started = false;
bool installer_auto_ollama = false;
unsigned installer_escape = 0;
int64_t installer_escape_at = 0;
QueueHandle_t installer_events = nullptr;
QueueHandle_t tab5_kbd_queue = nullptr;
tab5_installer_package_t installer_job{};
tab5_installer_status_t installer_progress{};
int64_t installer_last_paint = 0;
tab5_radio_status_t radio_status {};
uint32_t last_radio_revision = 0;
bool radio_ready = false;
int64_t pending_scan_at = 0;

constexpr char PASSWORD_SYMBOLS_0[] = "!@#$%^&*()";
constexpr char PASSWORD_SYMBOLS_1[] = {'-', '_', '=', '+', '[', ']', '{', '}', '\\', '/'};
constexpr char PASSWORD_SYMBOLS_2[] = {';', ':', '\'', '"', ',', '.', '<', '>', '?', '|'};
constexpr char PASSWORD_SYMBOLS_3[] = {'~', '`', '\\', '|', '^', '%', '@'};

struct Metrics {
    bool sd_ready = false;
    uint64_t sd_total = 0;
    uint64_t sd_free = 0;
    uint64_t flash_total = 0;
    uint64_t flash_allocated = 0;
    size_t internal_total = 0;
    size_t internal_free = 0;
    size_t psram_total = 0;
    size_t psram_free = 0;
    bool battery_ready = false;
    float battery_voltage = 0;
    uint8_t battery_percent = 0;
    bool battery_charging = false;
} metrics;

char notice[96] = "CONSOLE READY";
int64_t notice_until = 0;
bool power_confirm = false;
int64_t power_confirm_until = 0;

enum UiDirty : uint32_t {
    DIRTY_NONE     = 0,
    DIRTY_CLOCK    = 1U << 0,
    DIRTY_CONTROLS = 1U << 1,
    DIRTY_STORAGE  = 1U << 2,
    DIRTY_BATTERY  = 1U << 3,
    DIRTY_POWER    = 1U << 4,
    DIRTY_FOOTER   = 1U << 5,
    DIRTY_OVERLAY  = 1U << 6,
    DIRTY_RESTORE  = 1U << 7,
    DIRTY_BLE_TEST = 1U << 8,
    DIRTY_INSTALLER = 1U << 9,
    DIRTY_ALL      = 1U << 31,
};

bool portrait() { return (display_rotation & 1U) != 0; }
int screen_width() { return tab5_display_width(); }
int screen_height() { return tab5_display_height(); }
int footer_top() { return screen_height() - 70; }

void rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(screen_width(), x + w);
    int y1 = std::min(screen_height(), y + h);
    if (x0 < x1 && y0 < y1) tab5_display_rect(x0, y0, x1 - x0, y1 - y0, color);
}

void panel(int x, int y, int w, int h, uint32_t fill, uint32_t edge, int edge_width = 2)
{
    rect(x, y, w, h, fill);
    rect(x, y, w, edge_width, edge);
    rect(x, y + h - edge_width, w, edge_width, edge);
    rect(x, y, edge_width, h, edge);
    rect(x + w - edge_width, y, edge_width, h, edge);
}

const Glyph &glyph(const Font &font, char c)
{
    unsigned value = static_cast<unsigned char>(c);
    if (value < font.first || value > font.last) value = '?';
    return font.glyphs[value - font.first];
}

int text_width(const char *text, const Font &font, int scale = 1)
{
    int width = 0;
    for (; *text; ++text) width += (glyph(font, *text).advance + 1) * scale;
    return width ? width - scale : 0;
}

void text(const char *value, int x, int y, const Font &font, uint32_t color, int scale = 1)
{
    int baseline = y + font.ascent * scale;
    for (; *value; ++value) {
        const Glyph &g = glyph(font, *value);
        for (unsigned gy = 0; gy < g.height; ++gy) {
            for (unsigned gx = 0; gx < g.width; ++gx) {
                unsigned bit = gy * g.width + gx;
                if (font.bitmap[g.offset + bit / 8] & (0x80 >> (bit % 8))) {
                    rect(x + (g.x + static_cast<int>(gx)) * scale,
                         baseline + (g.y + static_cast<int>(gy)) * scale,
                         scale, scale, color);
                }
            }
        }
        x += (g.advance + 1) * scale;
    }
}

void centered(const char *value, int cx, int y, const Font &font, uint32_t color, int scale = 1)
{
    text(value, cx - text_width(value, font, scale) / 2, y, font, color, scale);
}

void line(int x0, int y0, int x1, int y1, uint32_t color, int thickness = 2)
{
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        rect(x0 - thickness / 2, y0 - thickness / 2, thickness, thickness, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

void circle(int cx, int cy, int radius, uint32_t color, bool filled = false)
{
    int outer = radius * radius;
    int inner = (radius - 2) * (radius - 2);
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            int d = x * x + y * y;
            if (d <= outer && (filled || d >= inner)) rect(cx + x, cy + y, 1, 1, color);
        }
    }
}

enum class Icon { sun, moon, weather, wifi, bulb, speaker, bluetooth, sleep, lock, power, sd, rotate };

void icon(Icon which, int x, int y, uint32_t color, uint32_t background)
{
    if (which == Icon::sun) {
        circle(x + 18, y + 18, 7, color, false);
        for (int i = 0; i < 8; ++i) {
            double angle = i * 3.141592653589793 / 4.0;
            line(x + 18 + static_cast<int>(11 * std::cos(angle)),
                 y + 18 + static_cast<int>(11 * std::sin(angle)),
                 x + 18 + static_cast<int>(16 * std::cos(angle)),
                 y + 18 + static_cast<int>(16 * std::sin(angle)), color, 2);
        }
    } else if (which == Icon::moon || which == Icon::sleep) {
        circle(x + 17, y + 18, 13, color, true);
        circle(x + 23, y + 13, 12, background, true);
    } else if (which == Icon::weather) {
        circle(x + 25, y + 11, 7, YELLOW, false);
        circle(x + 13, y + 23, 8, color, true);
        circle(x + 23, y + 20, 10, color, true);
        rect(x + 7, y + 23, 29, 9, color);
    } else if (which == Icon::wifi) {
        for (int y0 = 0; y0 < 25; ++y0) for (int x0 = 0; x0 < 37; ++x0) {
            int dx = x0 - 18, dy = y0 - 30, d = dx * dx + dy * dy;
            if ((d >= 625 && d <= 729) || (d >= 256 && d <= 361) ||
                (d >= 64 && d <= 121)) rect(x + x0, y + y0, 1, 1, color);
        }
        circle(x + 18, y + 29, 3, color, true);
    } else if (which == Icon::bulb) {
        circle(x + 18, y + 13, 11, color, false);
        line(x + 11, y + 21, x + 14, y + 27, color, 2);
        line(x + 25, y + 21, x + 22, y + 27, color, 2);
        rect(x + 13, y + 27, 10, 3, color);
        rect(x + 15, y + 32, 6, 2, color);
    } else if (which == Icon::speaker) {
        rect(x + 4, y + 13, 8, 12, color);
        line(x + 12, y + 13, x + 21, y + 6, color, 3);
        line(x + 21, y + 6, x + 21, y + 32, color, 3);
        line(x + 21, y + 32, x + 12, y + 25, color, 3);
        line(x + 27, y + 12, x + 31, y + 18, color, 2);
        line(x + 31, y + 18, x + 27, y + 25, color, 2);
    } else if (which == Icon::bluetooth) {
        line(x + 18, y + 2, x + 18, y + 34, color, 3);
        line(x + 18, y + 2, x + 29, y + 11, color, 3);
        line(x + 29, y + 11, x + 8, y + 28, color, 3);
        line(x + 8, y + 8, x + 29, y + 25, color, 3);
        line(x + 29, y + 25, x + 18, y + 34, color, 3);
    } else if (which == Icon::lock) {
        // Closed shackle, solid body and a contrasting keyhole.
        line(x + 9, y + 17, x + 9, y + 8, color, 3);
        line(x + 9, y + 8, x + 14, y + 3, color, 3);
        line(x + 14, y + 3, x + 22, y + 3, color, 3);
        line(x + 22, y + 3, x + 27, y + 8, color, 3);
        line(x + 27, y + 8, x + 27, y + 17, color, 3);
        rect(x + 5, y + 16, 27, 20, color);
        circle(x + 18, y + 23, 3, background, true);
        rect(x + 17, y + 24, 3, 7, background);
    } else if (which == Icon::power) {
        circle(x + 18, y + 19, 14, color, false);
        rect(x + 14, y, 8, 18, background);
        line(x + 18, y + 1, x + 18, y + 18, color, 4);
    } else if (which == Icon::sd) {
        // Exact CYD Console SD-card silhouette, enlarged 2x for Tab5.
        line(x + 12, y + 2, x + 34, y + 2, color, 2);
        line(x + 34, y + 2, x + 34, y + 36, color, 2);
        line(x + 34, y + 36, x + 6, y + 36, color, 2);
        line(x + 6, y + 36, x + 6, y + 10, color, 2);
        line(x + 6, y + 10, x + 12, y + 2, color, 2);
        for (int pin = 14; pin < 32; pin += 6) rect(x + pin, y + 8, 4, 10, color);
    } else if (which == Icon::rotate) {
        for (int degrees = 30; degrees <= 165; degrees += 5) {
            double a = degrees * 3.141592653589793 / 180.0;
            rect(x + 18 + static_cast<int>(14 * std::cos(a)),
                 y + 18 + static_cast<int>(14 * std::sin(a)), 2, 2, color);
        }
        for (int degrees = 210; degrees <= 345; degrees += 5) {
            double a = degrees * 3.141592653589793 / 180.0;
            rect(x + 18 + static_cast<int>(14 * std::cos(a)),
                 y + 18 + static_cast<int>(14 * std::sin(a)), 2, 2, color);
        }
        line(x + 5, y + 23, x + 3, y + 13, color, 2);
        line(x + 5, y + 23, x + 14, y + 20, color, 2);
        line(x + 31, y + 13, x + 33, y + 23, color, 2);
        line(x + 31, y + 13, x + 22, y + 16, color, 2);
    }
}

void set_notice(const char *message, int seconds = 4)
{
    std::snprintf(notice, sizeof(notice), "%s", message);
    notice_until = esp_timer_get_time() + static_cast<int64_t>(seconds) * 1000000;
}

void save_setting(const char *key, uint8_t value)
{
    if (settings_ready && nvs_set_u8(settings, key, value) == ESP_OK) nvs_commit(settings);
}

void load_settings()
{
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable (%s); preserving it and using defaults", esp_err_to_name(result));
        return;
    }
    if (nvs_open("tab5console", NVS_READWRITE, &settings) != ESP_OK) return;
    settings_ready = true;
    uint8_t value;
    if (nvs_get_u8(settings, "dark", &value) == ESP_OK) dark_mode = value != 0;
    if (nvs_get_u8(settings, "bright", &value) == ESP_OK) brightness = std::min<uint8_t>(100, value);
    if (nvs_get_u8(settings, "volume", &value) == ESP_OK) volume = std::min<uint8_t>(100, value);
    if (nvs_get_u8(settings, "rotation", &value) == ESP_OK) display_rotation = value & 3U;
    if (nvs_get_u8(settings, "wifi", &value) == ESP_OK) wifi_on = value != 0;
    if (nvs_get_u8(settings, "bt", &value) == ESP_OK) bluetooth_on = value != 0;
    if (nvs_get_u8(settings, "zone", &value) == ESP_OK && value < CLOCK_ZONE_COUNT) current_zone = value;
    size_t ssid_len = sizeof(saved_wifi_ssid);
    if (nvs_get_str(settings, "wifi_ssid", saved_wifi_ssid, &ssid_len) != ESP_OK) {
        saved_wifi_ssid[0] = '\0';
    }
    size_t pass_len = sizeof(saved_wifi_pass);
    if (nvs_get_str(settings, "wifi_pass", saved_wifi_pass, &pass_len) != ESP_OK) {
        saved_wifi_pass[0] = '\0';
    }
    theme = dark_mode ? DARK : LIGHT;
}

void refresh_storage()
{
    metrics.sd_ready = tab5_sd_usage(&metrics.sd_total, &metrics.sd_free);
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    metrics.flash_total = flash_size;
    metrics.flash_allocated = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY,
                                                      ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (it) {
        const esp_partition_t *part = esp_partition_get(it);
        metrics.flash_allocated += part->size;
        it = esp_partition_next(it);
    }
}

void refresh_ram()
{
    metrics.internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    metrics.internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    metrics.psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    metrics.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    set_notice("RAM USAGE REFRESHED", 2);
}

void refresh_battery()
{
    metrics.battery_ready = tab5_battery_read_status(&metrics.battery_voltage,
                                                    &metrics.battery_percent,
                                                    &metrics.battery_charging);
}

void progress_bar(int x, int y, int w, int h, unsigned percent, uint32_t color)
{
    percent = std::min(100U, percent);
    rect(x, y, w, h, theme.surface_pressed);
    rect(x, y, static_cast<int>((static_cast<uint64_t>(w) * percent) / 100), h, color);
}

void draw_tabs()
{
    static const char *labels[] = {"Console", "Shell", "Ollama",
                                   "Files", "HTML View", "Camera"};
    constexpr int count = sizeof(labels) / sizeof(labels[0]);
    const int width = screen_width();
    for (int i = 0; i < count; ++i) {
        int x = i * width / count;
        int right = (i + 1) * width / count;
        uint32_t fill = i == 0 ? theme.surface : theme.bg;
        uint32_t edge = i == 0 ? BLUE :
                        i == 1 ? RED :
                        i == 2 ? ORANGE :
                        i == 3 ? GREEN :
                        i == 4 ? BLUE :
                        i == 5 ? PURPLE : theme.border;
        panel(x, 0, right - x, 54, fill, edge, i == 0 ? 3 : 1);
        centered(labels[i], (x + right) / 2, 17, cyd_fonts::pixelfont_11x5_thin,
                 i == 0 ? theme.text :
                 i == 1 ? RED :
                 i == 2 ? ORANGE :
                 i == 3 ? GREEN :
                 i == 4 ? BLUE :
                 i == 5 ? PURPLE : theme.disabled,
                 portrait() ? 1 : 2);
    }
}

void draw_clock()
{
    const int x = portrait() ? 438 : 1004;
    const int center = portrait() ? 567 : 1130;
    panel(x, 64, portrait() ? 258 : 252, 70, theme.surface, BLUE);
    tab5_clock_time now {};
    if (tab5_rtc_read(&now)) {
        char time[16], date[24];
        std::snprintf(time, sizeof(time), "%02d:%02d:%02d", now.hour, now.minute, now.second);
        std::snprintf(date, sizeof(date), "%02d/%02d/%04d", now.day, now.month, now.year);
        centered(time, center, 72, cyd_fonts::pixelfont_13x7_semibold, theme.text, 2);
        centered(date, center, 104, cyd_fonts::pixelfont_7x5_thin, theme.muted, 1);
        text("RTC", x + (portrait() ? 221 : 213), 118,
             cyd_fonts::pixelfont_7x5_thin, BLUE, 1);
    } else {
        centered("RTC ERROR", center, 86, cyd_fonts::pixelfont_11x5_thin, RED, 2);
    }
}

void draw_header()
{
    text("TAB5 Console", 24, 66, cyd_fonts::pixelfont_13x7_semibold,
         theme.text, portrait() ? 2 : 3);
    char detail[80];
    std::snprintf(detail, sizeof(detail), "v%s  -  %s", esp_app_get_description()->version,
                  UPDATE_DESCRIPTION);
    text(detail, 27, portrait() ? 104 : 112, cyd_fonts::pixelfont_7x5_thin,
         theme.muted, portrait() ? 1 : 2);

    draw_clock();
}

void draw_card_base(int x, int y, bool active = true, int width = 402)
{
    panel(x, y, width, 100, theme.surface, active ? theme.border : theme.disabled);
}

void draw_card_label(int x, int y, Icon which, const char *label, const char *value,
                     uint32_t value_color = 0, bool active = true, int width = 402)
{
    draw_card_base(x, y, active, width);
    icon(which, x + 23, y + 31, active ? BLUE : theme.disabled, theme.surface);
    text(label, x + 80, y + 21, cyd_fonts::pixelfont_11x5_thin,
         active ? theme.text : theme.disabled, 2);
    text(value, x + 80, y + 58, cyd_fonts::pixelfont_7x5_thin,
         value_color ? value_color : (active ? theme.muted : theme.disabled), 2);
}

void draw_adjust_card(int x, int y, Icon which, const char *label, unsigned value,
                      int width = 402)
{
    draw_card_base(x, y, true, width);
    icon(which, x + 23, y + 31, BLUE, theme.surface);
    text(label, x + 80, y + 18, cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
    char amount[12]; std::snprintf(amount, sizeof(amount), "%u%%", value);
    text(amount, x + 80, y + 57, cyd_fonts::pixelfont_7x5_thin, theme.muted, 2);
    panel(x + width - 124, y + 18, 50, 64, theme.surface_pressed, theme.border);
    panel(x + width - 73, y + 18, 50, 64, theme.surface_pressed, theme.border);
    centered("+", x + width - 99, y + 38, cyd_fonts::pixelfont_13x7_semibold, theme.text, 2);
    centered("-", x + width - 48, y + 38, cyd_fonts::pixelfont_13x7_semibold, theme.text, 2);
}

const char *wifi_card_value()
{
    if (!radio_status.wifi_enabled) return "OFF";
    if (radio_status.wifi_scanning) return "SCANNING";
    if (radio_status.wifi_connected && radio_status.wifi_ssid[0]) return radio_status.wifi_ssid;
    return "TAP TO SCAN";
}

void weather_card_text(const char *value, int x, int y, const Font &font,
                        uint32_t color, int width)
{
    char fitted[48];
    std::snprintf(fitted, sizeof(fitted), "%s", value);
    size_t length = std::strlen(fitted);
    while (length && text_width(fitted, font, 2) > width) fitted[--length] = 0;
    if (length < std::strlen(value) && length > 3) {
        fitted[length - 1] = fitted[length - 2] = fitted[length - 3] = '.';
    }
    text(fitted, x, y, font, color, 2);
}

void draw_weather_card(int x, int y, int width = 402)
{
    draw_card_base(x, y, true, width);
    icon(Icon::weather, x + 23, y + 31, BLUE, theme.surface);
    text("Weather", x + 80, y + 15, cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
    if (!weather_saved) {
        text("NO WEATHER SAVED", x + 80, y + 55, cyd_fonts::pixelfont_7x5_thin, theme.muted, 2);
        return;
    }
    char temperature[24];
    tab5_weather_snapshot_temperature(&weather_snapshot, temperature, sizeof(temperature));
    text(temperature, x + width - 22 - text_width(temperature, cyd_fonts::pixelfont_11x5_thin, 2),
         y + 15, cyd_fonts::pixelfont_11x5_thin, BLUE, 2);
    weather_card_text(weather_snapshot.city, x + 80, y + 45,
                       cyd_fonts::pixelfont_11x5_thin, theme.text, width - 102);
    weather_card_text(weather_snapshot.conditions, x + 80, y + 75,
                       cyd_fonts::pixelfont_7x5_thin, theme.muted, width - 102);
}

const char *bluetooth_card_value()
{
    if (!radio_status.bluetooth_enabled) return "OFF";
    if (radio_status.ble_keyboard_ready) return "KEYBOARD READY";
    if (radio_status.ble_connecting) return "CONNECTING";
    if (radio_status.ble_bonded) return "TRUSTED";
    if (radio_status.ble_connected) return "PAIRING";
    if (radio_status.bt_scanning) return "SCANNING";
    return "TAP TO SCAN";
}

const char *bluetooth_footer_value()
{
    if (!radio_status.bluetooth_enabled) return "BT: OFF";
    if (radio_status.ble_keyboard_ready) return "BT: KEYBOARD";
    if (radio_status.ble_connecting) return "BT: CONNECTING";
    if (radio_status.ble_bonded) return "BT: TRUSTED";
    if (radio_status.ble_pairing_mode != TAB5_BLE_PAIRING_NONE) return "BT: PAIRING";
    if (radio_status.ble_connected) return "BT: CONNECTED";
    if (radio_status.bt_scanning) return "BT: SCANNING";
    return "BT: READY";
}

uint32_t bluetooth_status_color()
{
    if (!radio_status.bluetooth_enabled || !radio_ready) return theme.muted;
    if (radio_status.ble_bonded) return GREEN;
    if (radio_status.ble_pairing_mode != TAB5_BLE_PAIRING_NONE || radio_status.bt_scanning) return ORANGE;
    if (radio_status.ble_connected) return BLUE;
    return theme.text;
}

bool radio_visible_state_changed(const tab5_radio_status_t &before, const tab5_radio_status_t &after)
{
    return before.wifi_enabled != after.wifi_enabled ||
           before.bluetooth_enabled != after.bluetooth_enabled ||
           before.wifi_scanning != after.wifi_scanning ||
           before.bt_scanning != after.bt_scanning ||
           before.wifi_connected != after.wifi_connected ||
           before.wifi_has_ip != after.wifi_has_ip ||
           before.ble_connected != after.ble_connected ||
           before.ble_connecting != after.ble_connecting ||
           before.ble_keyboard_ready != after.ble_keyboard_ready ||
           before.ble_bonded != after.ble_bonded ||
           before.ble_encrypted != after.ble_encrypted ||
           before.ble_pairing_mode != after.ble_pairing_mode ||
           before.ble_passkey != after.ble_passkey ||
           before.ble_conn_handle != after.ble_conn_handle ||
           std::strcmp(before.wifi_ssid, after.wifi_ssid) != 0 ||
           std::strcmp(before.ip_address, after.ip_address) != 0 ||
           std::strcmp(before.wifi_message, after.wifi_message) != 0 ||
           std::strcmp(before.bt_message, after.bt_message) != 0 ||
           std::strcmp(before.ble_peer_name, after.ble_peer_name) != 0;
}

void draw_controls()
{
    if (portrait()) {
        constexpr int left = 24, right = 364, width = 332;
        draw_card_label(left, 150, dark_mode ? Icon::sun : Icon::moon, "Appearance",
                        dark_mode ? "DARK" : "LIGHT", dark_mode ? YELLOW : BLUE,
                        true, width);
        draw_weather_card(right, 150, width);
        draw_card_label(left, 262, Icon::wifi, "WiFi", wifi_card_value(),
                        radio_status.wifi_connected ? GREEN :
                        (radio_status.wifi_enabled ? ORANGE : theme.muted), true, width);
        draw_adjust_card(right, 262, Icon::bulb, "Brightness", brightness, width);
        draw_adjust_card(left, 374, Icon::speaker, "Volume", volume, width);
        draw_card_label(right, 374, Icon::bluetooth, "Bluetooth", bluetooth_card_value(),
                        bluetooth_status_color(), true, width);
        return;
    }
    constexpr int x0 = 24, x1 = 439, x2 = 854;
    constexpr int y0 = 150, y1 = 262;
    draw_card_label(x0, y0, dark_mode ? Icon::sun : Icon::moon, "Appearance",
                    dark_mode ? "DARK" : "LIGHT", dark_mode ? YELLOW : BLUE);
    draw_weather_card(x1, y0);
    draw_card_label(x2, y0, Icon::wifi, "WiFi", wifi_card_value(),
                    radio_status.wifi_connected ? GREEN :
                    (radio_status.wifi_enabled ? ORANGE : theme.muted));

    draw_adjust_card(x0, y1, Icon::bulb, "Brightness", brightness);
    draw_adjust_card(x1, y1, Icon::speaker, "Volume", volume);
    draw_card_label(x2, y1, Icon::bluetooth, "Bluetooth", bluetooth_card_value(),
                    bluetooth_status_color());
}

void draw_launch_button()
{
    const int x = portrait() ? 24 : 854;
    const int y = portrait() ? 598 : 374;
    const int width = portrait() ? 672 : 402;
    panel(x, y, width, 100, metrics.sd_ready ? ORANGE : theme.surface_pressed,
          metrics.sd_ready ? ORANGE : theme.border);
    icon(Icon::sd, x + 28, y + 29, metrics.sd_ready ? 0xffffff : theme.muted,
         metrics.sd_ready ? ORANGE : theme.surface_pressed);
    centered("Launch from SD Card", x + width / 2 + 16, y + 20,
             cyd_fonts::pixelfont_11x5_thin,
             metrics.sd_ready ? 0xffffff : theme.text, 2);
    centered("CHOOSE APP PACKAGE", x + width / 2 + 16, y + 60,
             cyd_fonts::pixelfont_7x5_thin,
             metrics.sd_ready ? 0xfff8e5 : theme.muted, 2);
}

void draw_storage()
{
    if (portrait()) {
        panel(24, 486, 332, 100, theme.surface, theme.border);
        text("Storage", 44, 498, cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
        if (metrics.sd_ready) {
            uint64_t used = metrics.sd_total - metrics.sd_free;
            unsigned percent = metrics.sd_total
                ? static_cast<unsigned>(used * 100 / metrics.sd_total) : 0;
            char value[64];
            std::snprintf(value, sizeof(value), "SD %.1f / %.1f GB", used / 1073741824.0,
                          metrics.sd_total / 1073741824.0);
            text(value, 44, 526, cyd_fonts::pixelfont_7x5_thin, theme.muted, 2);
            progress_bar(44, 558, 292, 10, percent, BLUE);
        } else {
            text("SD NOT MOUNTED", 44, 526, cyd_fonts::pixelfont_7x5_thin, RED, 2);
            progress_bar(44, 558, 292, 10, 0, RED);
        }
        panel(364, 486, 332, 100, theme.surface, theme.border);
        text("RAM", 384, 498, cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
        size_t total = metrics.internal_total + metrics.psram_total;
        size_t used = total - metrics.internal_free - metrics.psram_free;
        unsigned percent = total ? static_cast<unsigned>(used * 100 / total) : 0;
        char value[64];
        std::snprintf(value, sizeof(value), "INT %.0fk  PS %.1fM",
                      static_cast<double>(metrics.internal_free) / 1024.0,
                      static_cast<double>(metrics.psram_free) / 1048576.0);
        text(value, 384, 526, cyd_fonts::pixelfont_7x5_thin, theme.muted, 2);
        progress_bar(384, 558, 292, 10, percent, GREEN);
        draw_launch_button();
        return;
    }
    panel(24, 374, 402, 100, theme.surface, theme.border);
    text("Storage", 44, 386, cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
    if (metrics.sd_ready) {
        uint64_t used = metrics.sd_total - metrics.sd_free;
        unsigned percent = metrics.sd_total
            ? static_cast<unsigned>(used * 100 / metrics.sd_total) : 0;
        char value[64];
        std::snprintf(value, sizeof(value), "SD %.1f / %.1f GB", used / 1073741824.0,
                      metrics.sd_total / 1073741824.0);
        text(value, 44, 414, cyd_fonts::pixelfont_11x5_thin, theme.muted, 2);
        progress_bar(44, 446, 362, 10, percent, BLUE);
    } else {
        text("SD CARD NOT MOUNTED", 44, 414, cyd_fonts::pixelfont_11x5_thin, RED, 2);
        progress_bar(44, 446, 362, 10, 0, RED);
    }

    panel(439, 374, 402, 100, theme.surface, theme.border);
    text("RAM", 461, 386, cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
    size_t total = metrics.internal_total + metrics.psram_total;
    size_t used = total - metrics.internal_free - metrics.psram_free;
    unsigned percent = total ? static_cast<unsigned>(used * 100 / total) : 0;
    char value[64];
    std::snprintf(value, sizeof(value), "INT %.1fk PS %.1fM",
                  static_cast<double>(metrics.internal_free) / 1024.0,
                  static_cast<double>(metrics.psram_free) / 1048576.0);
    text(value, 461, 414, cyd_fonts::pixelfont_11x5_thin, theme.muted, 2);
    progress_bar(461, 446, 362, 10, percent, GREEN);

    draw_launch_button();
}

void draw_bolt(int x, int y)
{
    // A crisp, filled yellow lightning bolt (⚡) indicating charging
    uint32_t c = YELLOW;
    static const int8_t rows[][2] = {
        {8, 3}, {7, 4}, {6, 4}, {5, 4}, {4, 5},
        {3, 5}, {2, 5}, {1, 6}, {0, 6}, {0, 13},
        {2, 12}, {4, 10}, {5, 7}, {5, 6}, {4, 6},
        {4, 5}, {3, 5}, {3, 4}, {2, 4}, {2, 3},
        {1, 3}, {1, 2},
    };
    for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]); ++r) {
        rect(x + rows[r][0], y + static_cast<int>(r), rows[r][1], 1, c);
    }
}

void draw_discharging(int x, int y)
{
    // Battery casing with red draining indicator (🪫)
    rect(x + 5, y, 4, 2, theme.muted);
    rect(x, y + 2, 14, 2, theme.muted);
    rect(x, y + 18, 14, 2, theme.muted);
    rect(x, y + 2, 2, 18, theme.muted);
    rect(x + 12, y + 2, 2, 18, theme.muted);
    rect(x + 3, y + 12, 8, 4, RED);
}

void draw_battery()
{
    const int x = 24;
    const int y = portrait() ? 710 : 512;
    const int width = portrait() ? 672 : 464;
    const int height = portrait() ? 130 : 126;
    panel(x, y, width, height, theme.surface, theme.border);
    text("Battery", x + 20, y + 14, cyd_fonts::pixelfont_13x7_semibold, theme.text, 2);
    char value[48];
    if (metrics.battery_ready) {
        std::snprintf(value, sizeof(value), "%u%%  -  %.2f V", metrics.battery_percent,
                      metrics.battery_voltage);
        int val_w = text_width(value, cyd_fonts::pixelfont_11x5_thin, 2);
        int text_x = x + width - 24 - val_w;
        text(value, text_x, y + 14, cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
        int icon_x = text_x - 22;
        if (metrics.battery_charging) {
            draw_bolt(icon_x, y + 14);
        } else {
            draw_discharging(icon_x, y + 15);
        }
    } else {
        text("MONITOR UNAVAILABLE", x + 190, y + 14,
             cyd_fonts::pixelfont_11x5_thin, RED, 2);
    }

    int filled = metrics.battery_ready ? std::max(1, (metrics.battery_percent + 19) / 20) : 0;
    const int segment_gap = 10;
    const int segment_width = (width - 70 - segment_gap * 4) / 5;
    for (int i = 0; i < 5; ++i) {
        uint32_t color = i < 3 ? GREEN : (i == 3 ? YELLOW : RED);
        panel(x + 21 + i * (segment_width + segment_gap), y + 54, segment_width, 36,
              i < filled ? color : theme.surface_pressed,
              i < filled ? color : theme.border);
    }
    rect(x + width - 28, y + 64, 13, 16, theme.muted);
}

void draw_power_button(int x, int y, int width, int height, Icon which,
                       const char *label, uint32_t accent, bool danger = false)
{
    panel(x, y, width, height, danger ? RED : theme.surface_pressed,
          danger ? RED : theme.border);
    if (width < 220 && !portrait()) {
        icon(which, x + (width - 36) / 2, y + 20,
             danger ? 0xffffff : accent, danger ? RED : theme.surface_pressed);
        centered(label, x + width / 2, y + 80,
                 cyd_fonts::pixelfont_11x5_thin, danger ? 0xffffff : theme.text, 2);
        return;
    }
    icon(which, x + 20, y + (height - 36) / 2, danger ? 0xffffff : accent,
         danger ? RED : theme.surface_pressed);
    const int scale = portrait() && width < 220 ? 1 : 2;
    centered(label, x + width / 2 + 18, y + (height - 22 * scale) / 2,
             cyd_fonts::pixelfont_11x5_thin, danger ? 0xffffff : theme.text, scale);
}

void draw_lock_button(const tab5::PowerButton &b)
{
    constexpr uint32_t background = 0x020a05;
    panel(b.x, b.y, b.width, b.height, background, GREEN);
    constexpr char characters[] = "01ZXCVBNMASDFGHJKLQWERTYUIOP789#@$%";
    // Fixed column offsets and glyph selection: a still image on every redraw.
    for (int column = 0, dx = 8; dx + 6 < b.width - 4; ++column, dx += 12) {
        const int head = (column * 7 + 3) % 8;
        for (int row = 0, dy = 6; dy + 9 < b.height - 4; ++row, dy += 12) {
            const int distance = (row - head + 16) % 8;
            if (distance > 5) continue;
            const char value[] = {characters[(column * 13 + row * 7) %
                                             (sizeof(characters) - 1)], 0};
            const uint32_t ink = distance == 0 ? 0x4c9967 :
                                 distance < 3 ? 0x17552c : 0x0c2917;
            text(value, b.x + dx, b.y + dy, cyd_fonts::pixelfont_7x5_thin, ink);
        }
    }
    if (portrait()) {
        rect(b.x + 78, b.y + 22, 176, 56, background);
        icon(Icon::lock, b.x + 88, b.y + 32, 0xcfffe0, background);
        text("Lock", b.x + 146, b.y + 38,
             cyd_fonts::pixelfont_11x5_thin, 0xcfffe0, 2);
    } else {
        rect(b.x + 38, b.y + 14, b.width - 76, b.height - 28, background);
        icon(Icon::lock, b.x + (b.width - 36) / 2, b.y + 20, 0xcfffe0, background);
        centered("Lock", b.x + b.width / 2, b.y + 80,
                 cyd_fonts::pixelfont_11x5_thin, 0xcfffe0, 2);
    }
}

void draw_power_controls()
{
    for (auto action : tab5::power_actions) {
        const auto b = tab5::power_button(portrait(), action);
        if (action == tab5::PowerAction::lock) {
            draw_lock_button(b);
            continue;
        }
        const bool sleeping = action == tab5::PowerAction::sleep;
        const bool rotating = action == tab5::PowerAction::rotate;
        draw_power_button(b.x, b.y, b.width, b.height,
                          sleeping ? Icon::sleep : rotating ? Icon::rotate : Icon::power,
                          sleeping ? "Sleep" : rotating ? "Rotate" :
                          power_confirm ? "Confirm" : "Power Off",
                          sleeping ? BLUE : rotating ? ORANGE : RED,
                          action == tab5::PowerAction::power && power_confirm);
    }
}

void draw_battery_and_power()
{
    draw_battery();
    draw_power_controls();
}

void draw_footer()
{
    const int top = footer_top();
    const int width = screen_width();
    panel(0, top, width, 70, theme.bg, theme.border, 1);
    char ip[32];
    std::snprintf(ip, sizeof(ip), "IP: %s", radio_status.wifi_has_ip ? radio_status.ip_address : "--");
    text(ip, portrait() ? 16 : 24, top + 14, cyd_fonts::pixelfont_7x5_thin,
         theme.muted, portrait() ? 1 : 2);
    centered(bluetooth_footer_value(), portrait() ? 520 : 905, top + 14,
             cyd_fonts::pixelfont_7x5_thin, bluetooth_status_color(), portrait() ? 1 : 2);
    text(radio_status.ble_keyboard_ready ? "BLE KEYBOARD CONNECTED" :
         (tab5::tab5_kbd_connected() ? "TAB5 KEYBOARD CONNECTED" :
          (tab5_keyboard_connected() ? "USB KEYBOARD CONNECTED" : "USB KEYBOARD READY")),
         portrait() ? 16 : 24, top + 42, cyd_fonts::pixelfont_7x5_thin,
         theme.disabled, portrait() ? 1 : 2);
    if (notice_until > esp_timer_get_time())
        centered(notice, width / 2, top + 47, cyd_fonts::pixelfont_7x5_thin, BLUE, 1);
    const char *copyright = "(c) Great Bespoke 2026";
    const int copyright_scale = portrait() ? 1 : 2;
    text(copyright, width - (portrait() ? 16 : 24) -
         text_width(copyright, cyd_fonts::pixelfont_7x5_thin, copyright_scale),
         top + 42, cyd_fonts::pixelfont_7x5_thin, theme.muted, copyright_scale);
}

void draw_ui()
{
    tab5_display_rect(0, 0, screen_width(), screen_height(), theme.bg);
    draw_tabs();
    draw_header();
    draw_controls();
    draw_storage();
    draw_battery_and_power();
    draw_footer();
    tab5_display_present();
}


void overlay_frame(const char *title, const char *status, const char *close_label = "CLOSE");
void draw_button(int x, int y, int w, int h, const char *label, bool active = true);

void post_installer_progress(const tab5_installer_status_t *snapshot, void *)
{
    if (installer_events && snapshot->state != TAB5_INSTALLER_DONE &&
        snapshot->state != TAB5_INSTALLER_FAILED)
        xQueueOverwrite(installer_events, snapshot);
}

void installer_worker(void *)
{
    tab5_installer_status_t result{};
    int slot = -1;
    tab5_installer_install(&installer_job, &result, &slot);
    xQueueOverwrite(installer_events, &result);
    vTaskDelete(nullptr);
}

void draw_install_progress(bool frame)
{
    const int width = screen_width();
    const int bottom = footer_top();
    const int center = width / 2;
    if (frame) {
        rect(0, 54, width, bottom - 54, theme.surface);
        const int icon_y = portrait() ? 244 : 120;
        icon(Icon::sd, center - 20, icon_y, ORANGE, theme.surface);
        centered("Loading app", center, portrait() ? 316 : 180,
                 cyd_fonts::pixelfont_13x7_semibold, theme.text, 3);
        centered(installer_job.project, center, portrait() ? 382 : 244,
                 cyd_fonts::pixelfont_11x5_thin, theme.muted, 2);
        centered("KEEP POWER CONNECTED", center, portrait() ? 1050 : 560,
                 cyd_fonts::pixelfont_7x5_thin, theme.muted, 2);
    }
    const int progress_x = portrait() ? 32 : 76;
    const int progress_y = portrait() ? 500 : 306;
    const int progress_width = portrait() ? 656 : 1128;
    rect(progress_x, progress_y, progress_width, portrait() ? 220 : 210, theme.surface);
    centered(installer_progress.message, center, progress_y + 10,
             cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
    progress_bar(progress_x + 14, progress_y + 78, progress_width - 28, 26,
                 installer_progress.progress, BLUE);
    char percent[12];
    std::snprintf(percent, sizeof(percent), "%u%%", installer_progress.progress);
    centered(percent, center, progress_y + 128,
             cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
}

void draw_installer_overlay()
{
    if (installer_busy) { draw_install_progress(true); return; }
    if ((installer_auto_launch != AutoLaunch::none || installer_auto_ollama) && pending_installer_scan) {
        const AutoLaunch target = (installer_auto_launch != AutoLaunch::none) ? installer_auto_launch : AutoLaunch::ollama;
        const char *proj = auto_launch_project(target);
        const char *finding = auto_launch_finding_message(target);
        std::snprintf(installer_job.project, sizeof(installer_job.project), "%s", proj);
        installer_progress.state = TAB5_INSTALLER_VALIDATING;
        installer_progress.progress = 0;
        std::snprintf(installer_progress.message, sizeof(installer_progress.message), "%s", finding);
        draw_install_progress(true);
        return;
    }
    overlay_frame("SD App Library", !installer_ready ? "Launcher migration required" :
                  metrics.sd_ready ? "Select a package to validate" : "SD not mounted");
    if (!metrics.sd_ready) {
        centered("NO SD CARD", screen_width() / 2, portrait() ? 500 : 322,
                 cyd_fonts::pixelfont_11x5_thin, theme.muted, 2);
    } else if (installer_catalog.count == 0) {
        centered("NO PACKAGES - ADD /firmware/*.json", screen_width() / 2,
                 portrait() ? 500 : 322,
                 cyd_fonts::pixelfont_11x5_thin, theme.muted, 2);
    } else {
        size_t last_page = installer_catalog.count > 5
            ? installer_catalog.count - 5 : 0;
        if (installer_scroll > last_page) installer_scroll = last_page;
        draw_button(portrait() ? 318 : 862, portrait() ? 150 : 132,
                    portrait() ? 110 : 116, 50, "UP", installer_scroll > 0);
        draw_button(portrait() ? 438 : 986, portrait() ? 150 : 132,
                    portrait() ? 110 : 116, 50, "DOWN", installer_scroll < last_page);
        size_t shown = installer_catalog.count - installer_scroll;
        if (shown > 5) shown = 5;
        for (size_t i = 0; i < shown; ++i) {
            const auto &pkg = installer_catalog.packages[installer_scroll + i];
            int y = (portrait() ? 250 : 206) +
                    static_cast<int>(i) * (portrait() ? 130 : 58);
            bool sel = static_cast<int>(installer_scroll + i) == installer_selected;
            panel(portrait() ? 32 : 44, y, portrait() ? 656 : 1132,
                  portrait() ? 100 : 50, sel ? theme.surface : theme.surface_pressed,
                  sel ? BLUE : theme.border);
            char line[96];
            std::snprintf(line, sizeof(line), "%s  %s", pkg.project, pkg.version);
            text(line, portrait() ? 48 : 60, y + (portrait() ? 34 : 9),
                 cyd_fonts::pixelfont_7x5_thin,
                 sel ? theme.text : theme.muted, 4);
        }
    }

    tab5_installer_status_t istatus{};
    tab5_installer_get_status(&istatus);
    if (istatus.state == TAB5_INSTALLER_FAILED) {
        text(istatus.message, portrait() ? 48 : 60, portrait() ? 980 : 524,
             cyd_fonts::pixelfont_7x5_thin,
             istatus.state == TAB5_INSTALLER_FAILED ? RED : theme.muted, 2);
    }

    bool can_install = installer_ready && installer_selected >= 0 && !installer_busy;
    draw_button(portrait() ? 24 : 44, portrait() ? 1100 : 578,
                portrait() ? 210 : 350, portrait() ? 64 : 44,
                "REFRESH", metrics.sd_ready && !installer_busy);
    draw_button(portrait() ? 255 : 406, portrait() ? 1100 : 578,
                portrait() ? 441 : 712, portrait() ? 64 : 44,
                "INSTALL + RUN", can_install);
}

void draw_overlay();
void restore_base_ui();
void draw_ble_test();

void redraw(uint32_t dirty)
{
    if (dirty == DIRTY_NONE) return;
    if (installer_busy) {
        dirty &= (DIRTY_OVERLAY | DIRTY_INSTALLER);
        if (installer_worker_started && (dirty & DIRTY_OVERLAY))
            dirty = (dirty & ~DIRTY_OVERLAY) | DIRTY_INSTALLER;
    }
    if (dirty & DIRTY_ALL) {
        draw_ui();
        return;
    }
    if (overlay != Overlay::none) {
        // The overlay owns the region from y=120 to y=640. Do not let periodic
        // battery/telemetry updates paint hidden base panels over it.
        dirty &= ~(DIRTY_CONTROLS | DIRTY_STORAGE | DIRTY_BATTERY |
                   DIRTY_POWER | DIRTY_RESTORE);
    }
    if (dirty & DIRTY_CLOCK) {
        draw_clock();
        tab5_display_present_region(portrait() ? 438 : 1004, 64,
                                    portrait() ? 258 : 252, 70);
    }
    if (dirty & DIRTY_CONTROLS) {
        draw_controls();
        tab5_display_present_region(24, 150, portrait() ? 672 : 1232,
                                    portrait() ? 324 : 212);
    }
    if (dirty & DIRTY_STORAGE) {
        draw_storage();
        tab5_display_present_region(24, portrait() ? 486 : 374,
                                    portrait() ? 672 : 1232,
                                    portrait() ? 212 : 126);
    }
    if (dirty & DIRTY_BATTERY) {
        draw_battery();
        tab5_display_present_region(24, portrait() ? 710 : 512,
                                    portrait() ? 672 : 464,
                                    portrait() ? 130 : 126);
    }
    if (dirty & DIRTY_POWER) {
        draw_power_controls();
        tab5_display_present_region(portrait() ? 24 : 500,
                                    portrait() ? 852 : 512,
                                    portrait() ? 672 : 756,
                                    portrait() ? 212 : 126);
    }
    if (dirty & DIRTY_FOOTER) {
        draw_footer();
        tab5_display_present_region(0, footer_top(), screen_width(), 70);
    }
    if (dirty & DIRTY_RESTORE) restore_base_ui();
    if (dirty & DIRTY_OVERLAY) {
        draw_overlay();
        const bool loading_page = overlay == Overlay::installer &&
                                  (installer_busy ||
                                   ((installer_auto_launch != AutoLaunch::none || installer_auto_ollama) && pending_installer_scan));
        tab5_display_present_region(loading_page ? 0 : (portrait() ? 16 : 20),
                                    loading_page ? 54 : (portrait() ? 70 : 120),
                                    loading_page ? screen_width() : (portrait() ? 688 : 1240),
                                    loading_page ? footer_top() - 54 :
                                                   (portrait() ? 1124 : 520));
    } else if ((dirty & DIRTY_INSTALLER) && installer_busy) {
        draw_install_progress(false);
        tab5_display_present_region(portrait() ? 32 : 76,
                                    portrait() ? 500 : 306,
                                    portrait() ? 656 : 1128,
                                    portrait() ? 220 : 210);
    } else if ((dirty & DIRTY_BLE_TEST) && overlay == Overlay::bluetooth &&
               radio_status.ble_keyboard_ready) {
        draw_ble_test();
        tab5_display_present_region(portrait() ? 32 : 76,
                                    portrait() ? 420 : 334,
                                    portrait() ? 656 : 1128,
                                    portrait() ? 240 : 148);
    }
}

bool within(int x, int y, int left, int top, int width, int height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

void draw_button(int x, int y, int w, int h, const char *label, bool active)
{
    panel(x, y, w, h, active ? theme.surface_pressed : theme.surface,
          active ? theme.border : theme.disabled);
    centered(label, x + w / 2, y + std::max(7, (h - 22) / 2),
             cyd_fonts::pixelfont_11x5_thin,
             active ? theme.text : theme.disabled, 2);
}

void overlay_frame(const char *title, const char *status, const char *close_label)
{
    if (portrait()) {
        panel(16, 70, 688, 1124, theme.surface, BLUE, 3);
        text(title, 32, 88, cyd_fonts::pixelfont_13x7_semibold, theme.text, 2);
        text(status, 32, 128, cyd_fonts::pixelfont_7x5_thin, theme.muted, 1);
        draw_button(568, 82, 112, 52, close_label);
        return;
    }
    panel(20, 120, 1240, 520, theme.surface, BLUE, 3);
    text(title, 44, 136, cyd_fonts::pixelfont_13x7_semibold, theme.text, 3);
    text(status, 44, 178, cyd_fonts::pixelfont_7x5_thin, theme.muted, 2);
    draw_button(1112, 132, 120, 50, close_label);
}

const char *safe_wifi_ssid(const tab5_wifi_network_t &network)
{
    return network.ssid[0] ? network.ssid : "(HIDDEN)";
}

void draw_wifi_list()
{
    overlay_frame("Wi-Fi Networks", radio_status.wifi_message);
    size_t last_page = radio_status.wifi_count > 8
        ? ((radio_status.wifi_count - 1) / 8) * 8 : 0;
    if (wifi_scroll > last_page) wifi_scroll = last_page;
    draw_button(portrait() ? 318 : 862, portrait() ? 150 : 132,
                portrait() ? 110 : 116, 50, "UP", wifi_scroll > 0);
    draw_button(portrait() ? 438 : 986, portrait() ? 150 : 132,
                portrait() ? 110 : 116, 50, "DOWN", wifi_scroll < last_page);
    if (!radio_status.wifi_enabled) {
        centered("WI-FI IS OFF - TAP TURN ON", screen_width() / 2,
                 portrait() ? 500 : 322, cyd_fonts::pixelfont_11x5_thin,
                 theme.muted, 2);
    } else if (radio_status.wifi_count == 0) {
        centered(radio_status.wifi_scanning ? "SCANNING FOR NETWORKS..." : "NO NETWORKS YET - TAP SCAN",
                 screen_width() / 2, portrait() ? 500 : 322,
                 cyd_fonts::pixelfont_11x5_thin, theme.muted, 2);
    } else {
        size_t shown = wifi_scroll < radio_status.wifi_count
            ? std::min<size_t>(8, radio_status.wifi_count - wifi_scroll) : 0;
        for (size_t i = 0; i < shown; ++i) {
            tab5_wifi_network_t network {};
            if (!tab5_radio_get_wifi_network(wifi_scroll + i, &network)) continue;
            int y = (portrait() ? 220 : 206) + static_cast<int>(i) * (portrait() ? 88 : 43);
            panel(portrait() ? 32 : 44, y, portrait() ? 656 : 1132,
                  portrait() ? 72 : 38, theme.surface_pressed, theme.border);
            text(safe_wifi_ssid(network), portrait() ? 48 : 60, y + 10,
                 cyd_fonts::pixelfont_7x5_thin, theme.text, 2);
            char detail[40];
            std::snprintf(detail, sizeof(detail), "%d dBm  %s", network.rssi,
                          network.secure ? "SECURE" : "OPEN");
            if (portrait()) {
                text(detail, 48, y + 42, cyd_fonts::pixelfont_7x5_thin,
                     network.secure ? theme.muted : GREEN, 1);
            } else {
                text(detail, 1160 - text_width(detail, cyd_fonts::pixelfont_7x5_thin, 2),
                     y + 10, cyd_fonts::pixelfont_7x5_thin,
                     network.secure ? theme.muted : GREEN, 2);
            }
        }
    }
    const int bottom = portrait() ? 1110 : 578;
    draw_button(portrait() ? 24 : 44, bottom, portrait() ? 210 : 350,
                portrait() ? 60 : 44, "SCAN", radio_status.wifi_enabled);
    draw_button(portrait() ? 255 : 406, bottom, portrait() ? 210 : 350,
                portrait() ? 60 : 44, "DISCONNECT", radio_status.wifi_connected);
    draw_button(portrait() ? 486 : 768, bottom, portrait() ? 210 : 350,
                portrait() ? 60 : 44, "TURN OFF", radio_status.wifi_enabled);
}

void draw_password_key(int x, int y, int w, int h, const char *label, bool active = true)
{
    draw_button(x, y, w, h, label, active);
}

void draw_wifi_password()
{
    overlay_frame("Wi-Fi Password", selected_ssid, "BACK");
    if (portrait()) {
        panel(32, 190, 656, 70, theme.bg, BLUE);
        char shown[66] = {0};
        size_t length = std::strlen(wifi_password);
        size_t start = length > 44 ? length - 44 : 0;
        for (size_t i = start; i < length; ++i)
            shown[i - start] = password_visible ? wifi_password[i] : '*';
        if (!shown[0]) std::snprintf(shown, sizeof(shown), "TYPE PASSWORD");
        text(shown, 48, 214, cyd_fonts::pixelfont_7x5_thin,
             wifi_password[0] ? theme.text : theme.disabled, 2);
        const int rows[] = {300, 374, 448, 522};
        const char *letters[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
        for (int row = 0; row < 3; ++row) {
            int count = row == 2 ? 9 : 10;
            int start_x = row == 2 ? 50 : 16;
            for (int i = 0; i < count; ++i) {
                char label[2];
                if (password_symbols) {
                    label[0] = row == 0 ? PASSWORD_SYMBOLS_0[i] :
                               row == 1 ? PASSWORD_SYMBOLS_1[i] : PASSWORD_SYMBOLS_2[i];
                } else {
                    label[0] = letters[row][i];
                    if (!password_shift && row > 0) label[0] += 32;
                }
                label[1] = 0;
                draw_password_key(start_x + i * 69, rows[row], 63, 60, label);
            }
        }
        draw_password_key(16, rows[3], 100, 60,
                          password_symbols ? "ABC" : (password_shift ? "SHIFT+" : "SHIFT"),
                          password_symbols || password_shift);
        for (int i = 0; i < 7; ++i) {
            char label[2] = {password_symbols ? PASSWORD_SYMBOLS_3[i] : letters[3][i], 0};
            if (!password_symbols && !password_shift) label[0] += 32;
            draw_password_key(120 + i * 69, rows[3], 63, 60, label);
        }
        draw_password_key(606, rows[3], 90, 60, "DEL");
        draw_button(24, 620, 300, 60, "SPACE");
        draw_button(336, 620, 170, 60, password_symbols ? "ABC" : "SYM", password_symbols);
        draw_button(518, 620, 178, 60, password_visible ? "HIDE" : "SHOW");
        draw_button(24, 700, 672, 64, "CONNECT", true);
        return;
    }
    panel(60, 205, 1160, 54, theme.bg, BLUE);
    char shown[66] = {0};
    size_t length = std::strlen(wifi_password);
    size_t start = length > 48 ? length - 48 : 0;
    for (size_t i = start; i < length; ++i) shown[i - start] = password_visible ? wifi_password[i] : '*';
    if (!shown[0]) std::snprintf(shown, sizeof(shown), "TYPE PASSWORD");
    text(shown, 76, 226, cyd_fonts::pixelfont_7x5_thin,
         wifi_password[0] ? theme.text : theme.disabled, 2);
    const int y_rows[] = {278, 330, 382, 434};
    if (password_symbols) {
        for (int i = 0; i < 10; ++i) {
            char label[2] = {PASSWORD_SYMBOLS_0[i], 0};
            draw_password_key(64 + i * 116, y_rows[0], 108, 44, label);
            label[0] = PASSWORD_SYMBOLS_1[i];
            draw_password_key(64 + i * 116, y_rows[1], 108, 44, label);
            label[0] = PASSWORD_SYMBOLS_2[i];
            draw_password_key(64 + i * 116, y_rows[2], 108, 44, label);
        }
        draw_password_key(122, y_rows[3], 108, 44, "ABC");
        for (int i = 0; i < 7; ++i) {
            char label[2] = {PASSWORD_SYMBOLS_3[i], 0};
            draw_password_key(238 + i * 116, y_rows[3], 108, 44, label);
        }
    } else {
        const char *rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
        for (int row = 0; row < 2; ++row) {
            for (int i = 0; i < 10; ++i) {
                char label[2] = {rows[row][i], 0};
                if (!password_shift && row > 0 && label[0] >= 'A' && label[0] <= 'Z') label[0] += 32;
                draw_password_key(64 + i * 116, y_rows[row], 108, 44, label);
            }
        }
        for (int i = 0; i < 9; ++i) {
            char label[2] = {rows[2][i], 0};
            if (!password_shift) label[0] += 32;
            draw_password_key(122 + i * 116, y_rows[2], 108, 44, label);
        }
        draw_password_key(122, y_rows[3], 108, 44, password_shift ? "SHIFT+" : "SHIFT", password_shift);
        for (int i = 0; i < 7; ++i) {
            char label[2] = {rows[3][i], 0};
            if (!password_shift) label[0] += 32;
            draw_password_key(238 + i * 116, y_rows[3], 108, 44, label);
        }
    }
    draw_password_key(1050, y_rows[3], 108, 44, "DEL");
    draw_button(76, 492, 424, 44, "SPACE");
    draw_button(508, 492, 192, 44, password_symbols ? "ABC" : "SYM", password_symbols);
    draw_button(708, 492, 224, 44, password_visible ? "HIDE" : "SHOW");
    draw_button(940, 492, 324, 44, "CONNECT", true);
}

void draw_ble_row(size_t index, int y)
{
    tab5_ble_device_t device {};
    if (!tab5_radio_get_ble_device(index, &device)) return;
    panel(portrait() ? 32 : 44, y, portrait() ? 656 : 1132,
          portrait() ? 72 : 38, theme.surface_pressed, theme.border);
    char name[TAB5_RADIO_NAME_LEN];
    if (device.name[0]) std::snprintf(name, sizeof(name), "%s", device.name);
    else std::snprintf(name, sizeof(name), "%02X:%02X:%02X:%02X:%02X:%02X",
                       device.addr[0], device.addr[1], device.addr[2],
                       device.addr[3], device.addr[4], device.addr[5]);
    text(name, portrait() ? 48 : 60, y + 10,
         cyd_fonts::pixelfont_7x5_thin, theme.text, 2);
    char detail[40];
    std::snprintf(detail, sizeof(detail), "%d dBm  %s", device.rssi,
                  device.connectable ? "CONNECTABLE" : "ADVERT");
    if (portrait()) {
        text(detail, 48, y + 42, cyd_fonts::pixelfont_7x5_thin,
             device.connectable ? BLUE : theme.muted, 1);
    } else {
        text(detail, 1160 - text_width(detail, cyd_fonts::pixelfont_7x5_thin, 2),
             y + 10, cyd_fonts::pixelfont_7x5_thin,
             device.connectable ? BLUE : theme.muted, 2);
    }
}

void draw_ble_pair_confirm()
{
    const int center = screen_width() / 2;
    centered("CONFIRM PASSKEY", center, portrait() ? 300 : 230,
             cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
    char value[16];
    std::snprintf(value, sizeof(value), "%06lu", (unsigned long)radio_status.ble_passkey);
    centered(value, center, portrait() ? 380 : 282,
             cyd_fonts::pixelfont_13x7_semibold, YELLOW, 5);
    centered("SAME NUMBER ON YOUR DEVICE?", center, portrait() ? 510 : 386,
             cyd_fonts::pixelfont_7x5_thin,
             theme.muted, 2);
    draw_button(portrait() ? 70 : 260, portrait() ? 610 : 466,
                portrait() ? 275 : 340, 58, "PAIR + TRUST");
    draw_button(portrait() ? 375 : 680, portrait() ? 610 : 466,
                portrait() ? 275 : 340, 58, "CANCEL");
}

void draw_ble_pair_input()
{
    const int center = screen_width() / 2;
    centered("ENTER PAIRING PASSKEY", center, portrait() ? 250 : 218,
             cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
    char shown[8] = "------";
    for (int i = 0; i < 6 && pairing_pin[i]; ++i) shown[i] = pairing_pin[i];
    centered(shown, center, portrait() ? 320 : 264,
             cyd_fonts::pixelfont_13x7_semibold, YELLOW, 4);
    const char *keys[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "DEL", "0", "PAIR"};
    for (int i = 0; i < 12; ++i) {
        int col = i % 3, row = i / 3;
        draw_button((portrait() ? 250 : 470) + col * (portrait() ? 110 : 120),
                    (portrait() ? 430 : 342) + row * (portrait() ? 74 : 58),
                    portrait() ? 100 : 108, portrait() ? 62 : 48, keys[i]);
    }
    draw_button(portrait() ? 32 : 230, portrait() ? 430 : 342,
                portrait() ? 190 : 200, portrait() ? 62 : 48, "CANCEL");
}

void draw_ble_test()
{
    const int x = portrait() ? 32 : 76;
    const int y = portrait() ? 420 : 334;
    const int width = portrait() ? 656 : 1128;
    const int height = portrait() ? 240 : 148;
    panel(x, y, width, height, theme.surface_pressed, theme.border);
    text(ble_test[0] ? ble_test : "Type here to test your keyboard", x + 24, y + 24,
         cyd_fonts::pixelfont_7x5_thin, ble_test[0] ? theme.text : theme.muted, 2);
    text(ble_last_key, x + 24, y + (portrait() ? 150 : 96),
         cyd_fonts::pixelfont_11x5_thin, BLUE, 2);
}

void draw_bluetooth_overlay()
{
    overlay_frame("Bluetooth Devices", radio_status.bt_message);
    if (radio_status.ble_pairing_mode == TAB5_BLE_PAIRING_CONFIRM) {
        draw_ble_pair_confirm();
        return;
    }
    if (radio_status.ble_pairing_mode == TAB5_BLE_PAIRING_INPUT) {
        draw_ble_pair_input();
        return;
    }
    if (radio_status.ble_pairing_mode == TAB5_BLE_PAIRING_DISPLAY) {
        centered("ENTER THIS PASSKEY ON THE DEVICE", screen_width() / 2,
                 portrait() ? 300 : 260,
                 cyd_fonts::pixelfont_11x5_thin, theme.text, 2);
        char value[16];
        std::snprintf(value, sizeof(value), "%06lu", (unsigned long)radio_status.ble_passkey);
        centered(value, screen_width() / 2, portrait() ? 400 : 320,
                 cyd_fonts::pixelfont_13x7_semibold, YELLOW, 5);
        draw_button(portrait() ? 190 : 470, portrait() ? 600 : 470,
                    portrait() ? 340 : 340, 58, "CANCEL");
        return;
    }
    if (radio_status.ble_keyboard_ready) {
        centered(radio_status.ble_peer_name, screen_width() / 2,
                 portrait() ? 250 : 230,
                 cyd_fonts::pixelfont_13x7_semibold, GREEN, 3);
        centered("KEYBOARD READY - TYPE OR TRY ARROW KEYS", screen_width() / 2,
                 portrait() ? 330 : 290,
                 cyd_fonts::pixelfont_7x5_thin, theme.text, 2);
        draw_ble_test();
        const int bottom = portrait() ? 1110 : 578;
        draw_button(portrait() ? 24 : 44, bottom, portrait() ? 210 : 350,
                    portrait() ? 60 : 44, "SCAN");
        draw_button(portrait() ? 255 : 406, bottom, portrait() ? 210 : 350,
                    portrait() ? 60 : 44, "DISCONNECT");
        draw_button(portrait() ? 486 : 768, bottom, portrait() ? 210 : 350,
                    portrait() ? 60 : 44, "TURN OFF");
        return;
    }
    size_t last_page = radio_status.ble_count > 8
        ? ((radio_status.ble_count - 1) / 8) * 8 : 0;
    if (ble_scroll > last_page) ble_scroll = last_page;
    draw_button(portrait() ? 318 : 862, portrait() ? 150 : 132,
                portrait() ? 110 : 116, 50, "UP", ble_scroll > 0);
    draw_button(portrait() ? 438 : 986, portrait() ? 150 : 132,
                portrait() ? 110 : 116, 50, "DOWN", ble_scroll < last_page);
    if (!radio_status.bluetooth_enabled) {
        centered("BLUETOOTH IS OFF - TAP TURN ON", screen_width() / 2,
                 portrait() ? 500 : 322,
                 cyd_fonts::pixelfont_11x5_thin, theme.muted, 2);
    } else if (radio_status.ble_count == 0) {
        centered(radio_status.bt_scanning ? "SCANNING FOR BLE DEVICES..." : "NO DEVICES YET - TAP SCAN",
                 screen_width() / 2, portrait() ? 500 : 322,
                 cyd_fonts::pixelfont_11x5_thin, theme.muted, 2);
    } else {
        size_t shown = ble_scroll < radio_status.ble_count
            ? std::min<size_t>(8, radio_status.ble_count - ble_scroll) : 0;
        for (size_t i = 0; i < shown; ++i)
            draw_ble_row(ble_scroll + i, (portrait() ? 220 : 206) +
                         static_cast<int>(i) * (portrait() ? 88 : 43));
    }
    const int bottom = portrait() ? 1110 : 578;
    draw_button(portrait() ? 24 : 44, bottom, portrait() ? 210 : 350,
                portrait() ? 60 : 44, "SCAN",
                radio_status.bluetooth_enabled && !radio_status.ble_connecting);
    draw_button(portrait() ? 255 : 406, bottom, portrait() ? 210 : 350,
                portrait() ? 60 : 44, "DISCONNECT",
                radio_status.ble_connected || radio_status.ble_connecting);
    draw_button(portrait() ? 486 : 768, bottom, portrait() ? 210 : 350,
                portrait() ? 60 : 44, "TURN OFF", radio_status.bluetooth_enabled);
}

static void clock_sync_worker(void *)
{
    clock_sync_in_progress = true;
    std::snprintf(clock_status_message, sizeof(clock_status_message), "CONTACTING NTP POOL...");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.start = true;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err == ESP_OK) {
        err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(8000));
        if (err == ESP_OK) {
            time_t now = 0;
            time(&now);
            setenv("TZ", CLOCK_ZONES[current_zone].tz, 1);
            tzset();
            struct tm local {};
            localtime_r(&now, &local);
            tab5_clock_time new_time {
                .year = local.tm_year + 1900,
                .month = local.tm_mon + 1,
                .day = local.tm_mday,
                .weekday = local.tm_wday,
                .hour = local.tm_hour,
                .minute = local.tm_min,
                .second = local.tm_sec,
                .valid = true,
            };
            if (tab5_rtc_set(&new_time)) {
                std::snprintf(clock_status_message, sizeof(clock_status_message),
                              "SYNCED: %02d/%02d/%04d %02d:%02d:%02d",
                              new_time.day, new_time.month, new_time.year,
                              new_time.hour, new_time.minute, new_time.second);
                set_notice("RTC SYNCHRONIZED VIA NTP", 4);
            } else {
                std::snprintf(clock_status_message, sizeof(clock_status_message), "RTC WRITE FAILED");
            }
        } else {
            std::snprintf(clock_status_message, sizeof(clock_status_message), "NTP TIMEOUT - RETRY LATER");
        }
        esp_netif_sntp_deinit();
    } else {
        std::snprintf(clock_status_message, sizeof(clock_status_message), "SNTP INIT FAILED");
    }
    clock_sync_in_progress = false;
    vTaskDelete(nullptr);
}

static int days_in_month(int year, int month)
{
    if (month < 1 || month > 12) return 31;
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int d = days[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
        d = 29;
    }
    return d;
}

static void adjust_editing_time(ClockField field, int direction)
{
    direction = direction < 0 ? -1 : 1;
    switch (field) {
    case ClockField::year:
        editing_time.year += direction;
        if (editing_time.year < 2024) editing_time.year = 2099;
        if (editing_time.year > 2099) editing_time.year = 2024;
        break;
    case ClockField::month:
        editing_time.month += direction;
        if (editing_time.month < 1) editing_time.month = 12;
        if (editing_time.month > 12) editing_time.month = 1;
        break;
    case ClockField::day: {
        int max_d = days_in_month(editing_time.year, editing_time.month);
        editing_time.day += direction;
        if (editing_time.day < 1) editing_time.day = max_d;
        if (editing_time.day > max_d) editing_time.day = 1;
        break;
    }
    case ClockField::hour:
        editing_time.hour += direction;
        if (editing_time.hour < 0) editing_time.hour = 23;
        if (editing_time.hour > 23) editing_time.hour = 0;
        break;
    case ClockField::minute:
        editing_time.minute += direction;
        if (editing_time.minute < 0) editing_time.minute = 59;
        if (editing_time.minute > 59) editing_time.minute = 0;
        break;
    case ClockField::second:
        editing_time.second += direction;
        if (editing_time.second < 0) editing_time.second = 59;
        if (editing_time.second > 59) editing_time.second = 0;
        break;
    }
    int max_d = days_in_month(editing_time.year, editing_time.month);
    if (editing_time.day > max_d) editing_time.day = max_d;
}

void draw_clock_main()
{
    tab5_clock_time now {};
    bool ok = tab5_rtc_read(&now);

    char status[64];
    if (clock_status_message[0]) {
        std::snprintf(status, sizeof(status), "%s", clock_status_message);
    } else if (radio_status.wifi_has_ip) {
        std::snprintf(status, sizeof(status), "Wi-Fi Connected (%s) - Ready to Sync", radio_status.ip_address);
    } else {
        std::snprintf(status, sizeof(status), "Set manually or connect Wi-Fi for network sync");
    }

    overlay_frame("Clock & Date Settings", status);

    char time_str[16] = "--:--:--", date_str[48] = "CLOCK NOT SET";
    if (ok) {
        std::snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", now.hour, now.minute, now.second);
        int wday = (now.weekday >= 0 && now.weekday < 7) ? now.weekday : 0;
        int mon = (now.month >= 1 && now.month <= 12) ? now.month : 1;
        std::snprintf(date_str, sizeof(date_str), "%s, %02d %s %04d",
                      WEEKDAY_NAMES[wday], now.day, MONTH_NAMES[mon - 1], now.year);
    }

    if (portrait()) {
        panel(32, 170, 656, 170, theme.surface_pressed, BLUE, 2);
        centered(time_str, 360, 195, cyd_fonts::pixelfont_13x7_semibold, theme.text, 3);
        centered(date_str, 360, 275, cyd_fonts::pixelfont_11x5_thin, theme.muted, 2);

        char zone_label[40];
        std::snprintf(zone_label, sizeof(zone_label), "ZONE: %s (%s)",
                      CLOCK_ZONES[current_zone].name, CLOCK_ZONES[current_zone].abbr);
        draw_button(32, 360, 656, 70, zone_label);

        char wifi_label[48];
        std::snprintf(wifi_label, sizeof(wifi_label), "WIFI: %s",
                      radio_status.wifi_has_ip ? radio_status.ip_address :
                      radio_status.wifi_enabled ? "NO IP" : "OFF");
        draw_button(32, 450, 656, 70, wifi_label, false);

        draw_button(32, 540, 656, 80,
                    clock_sync_in_progress ? "CONTACTING NTP..." : "WI-FI TIME SYNC",
                    !clock_sync_in_progress && radio_status.wifi_has_ip);

        draw_button(32, 640, 656, 80, "SET MANUALLY", !clock_sync_in_progress);
    } else {
        panel(80, 200, 1120, 140, theme.surface_pressed, BLUE, 2);
        centered(time_str, 640, 218, cyd_fonts::pixelfont_13x7_semibold, theme.text, 3);
        centered(date_str, 640, 280, cyd_fonts::pixelfont_11x5_thin, theme.muted, 2);

        char zone_label[40];
        std::snprintf(zone_label, sizeof(zone_label), "ZONE: %s (%s)",
                      CLOCK_ZONES[current_zone].name, CLOCK_ZONES[current_zone].abbr);
        draw_button(80, 360, 545, 64, zone_label);

        char wifi_label[48];
        std::snprintf(wifi_label, sizeof(wifi_label), "WIFI: %s",
                      radio_status.wifi_has_ip ? radio_status.ip_address :
                      radio_status.wifi_enabled ? "ENABLED (NO IP)" : "DISCONNECTED");
        draw_button(655, 360, 545, 64, wifi_label, false);

        draw_button(80, 450, 545, 74,
                    clock_sync_in_progress ? "CONTACTING NTP..." : "WI-FI TIME SYNC",
                    !clock_sync_in_progress && radio_status.wifi_has_ip);

        draw_button(655, 450, 545, 74, "SET MANUALLY", !clock_sync_in_progress);
    }
}

void draw_clock_edit()
{
    overlay_frame("Set Date & Time", "Select a field, then tap - or + to adjust");

    static const char *field_names[] = {"YEAR", "MONTH", "DAY", "HOUR", "MINUTE", "SECOND"};
    int field_values[] = {
        editing_time.year, editing_time.month, editing_time.day,
        editing_time.hour, editing_time.minute, editing_time.second
    };

    if (portrait()) {
        for (int i = 0; i < 6; ++i) {
            int col = i / 3;
            int row = i % 3;
            int x = col == 0 ? 32 : 375;
            int y = 170 + row * 110;
            int w = 313, h = 95;
            bool sel = (static_cast<int>(editing_field) == i);
            panel(x, y, w, h, sel ? theme.surface_pressed : theme.surface,
                  sel ? BLUE : theme.border, sel ? 3 : 1);
            text(field_names[i], x + 16, y + 14, cyd_fonts::pixelfont_7x5_thin,
                 sel ? BLUE : theme.muted, 2);
            char val_str[16];
            std::snprintf(val_str, sizeof(val_str), i == 0 ? "%04d" : "%02d", field_values[i]);
            centered(val_str, x + w / 2, y + 44, cyd_fonts::pixelfont_13x7_semibold,
                     sel ? theme.text : theme.muted, 2);
        }

        draw_button(32, 530, 313, 74, "- LOWER");
        draw_button(375, 530, 313, 74, "+ HIGHER");

        char preview[64];
        std::snprintf(preview, sizeof(preview), "PREVIEW: %02d/%02d/%04d %02d:%02d:%02d",
                      editing_time.day, editing_time.month, editing_time.year,
                      editing_time.hour, editing_time.minute, editing_time.second);
        centered(preview, 360, 630, cyd_fonts::pixelfont_11x5_thin, theme.text, 2);

        draw_button(32, 700, 313, 74, "CANCEL");
        draw_button(375, 700, 313, 74, "SAVE TO RTC");
    } else {
        for (int i = 0; i < 6; ++i) {
            int x = 80 + i * 190;
            int y = 200;
            int w = 170, h = 100;
            bool sel = (static_cast<int>(editing_field) == i);
            panel(x, y, w, h, sel ? theme.surface_pressed : theme.surface,
                  sel ? BLUE : theme.border, sel ? 3 : 1);
            text(field_names[i], x + 14, y + 14, cyd_fonts::pixelfont_7x5_thin,
                 sel ? BLUE : theme.muted, 1);
            char val_str[16];
            std::snprintf(val_str, sizeof(val_str), i == 0 ? "%04d" : "%02d", field_values[i]);
            centered(val_str, x + w / 2, y + 42, cyd_fonts::pixelfont_13x7_semibold,
                     sel ? theme.text : theme.muted, 2);
        }

        draw_button(80, 330, 545, 74, "- LOWER");
        draw_button(655, 330, 545, 74, "+ HIGHER");

        char preview[64];
        std::snprintf(preview, sizeof(preview), "PREVIEW: %02d/%02d/%04d  %02d:%02d:%02d",
                      editing_time.day, editing_time.month, editing_time.year,
                      editing_time.hour, editing_time.minute, editing_time.second);
        centered(preview, 640, 425, cyd_fonts::pixelfont_11x5_thin, theme.text, 2);

        draw_button(80, 470, 545, 74, "CANCEL");
        draw_button(655, 470, 545, 74, "SAVE TO RTC");
    }
}

void draw_clock_overlay()
{
    if (clock_edit_mode) draw_clock_edit();
    else draw_clock_main();
}

void draw_overlay()
{
    if (overlay == Overlay::wifi) {
        if (wifi_password_mode) draw_wifi_password();
        else draw_wifi_list();
    } else if (overlay == Overlay::bluetooth) {
        draw_bluetooth_overlay();
    } else if (overlay == Overlay::installer) {
        draw_installer_overlay();
    } else if (overlay == Overlay::clock) {
        draw_clock_overlay();
    }
}

void restore_base_ui()
{
    tab5_display_rect(0, 54, screen_width(), footer_top() - 54, theme.bg);
    draw_header();
    draw_controls();
    draw_storage();
    draw_battery_and_power();
    draw_footer();
    tab5_display_present_region(0, 54, screen_width(), footer_top() - 54);
}

void screen_sleep()
{
    tab5_display_set_brightness(0);
    int x, y;
    while (tab5_touch_poll(&x, &y)) vTaskDelay(pdMS_TO_TICKS(30));
    while (!tab5_touch_poll(&x, &y)) vTaskDelay(pdMS_TO_TICKS(30));
    while (tab5_touch_poll(&x, &y)) vTaskDelay(pdMS_TO_TICKS(30));
    tab5_display_set_brightness(brightness);
    set_notice("AWAKE", 2);
}

void adjust_brightness(int delta)
{
    int next = std::max(10, std::min(100, static_cast<int>(brightness) + delta));
    brightness = static_cast<uint8_t>(next);
    tab5_display_set_brightness(brightness);
    save_setting("bright", brightness);
}

void adjust_volume(int delta)
{
    int next = std::max(0, std::min(100, static_cast<int>(volume) + delta));
    volume = static_cast<uint8_t>(next);
    save_setting("volume", volume);
    set_notice("VOLUME SAVED - AUDIO ENGINE LATER", 3);
}

void close_overlay()
{
    if (installer_busy) return;
    overlay = Overlay::none;
    installer_auto_launch = AutoLaunch::none;
    installer_auto_ollama = false;
    installer_escape = 0;
    installer_escape_at = 0;
    clock_edit_mode = false;
    wifi_password_mode = false;
    password_visible = false;
    password_symbols = false;
    pairing_pin[0] = '\0';
    wifi_scroll = 0;
    ble_scroll = 0;
    memset(wifi_password, 0, sizeof(wifi_password));
    memset(selected_ssid, 0, sizeof(selected_ssid));
}

uint32_t open_installer(AutoLaunch auto_target = AutoLaunch::none)
{
    if (!metrics.sd_ready) {
        if (auto_target == AutoLaunch::shell) {
            set_notice("SHELL PACKAGE NEEDS THE SD CARD", 4);
        } else if (auto_target == AutoLaunch::ollama) {
            set_notice("OLLAMA PACKAGE NEEDS THE SD CARD", 4);
        } else if (auto_target == AutoLaunch::fileserver) {
            set_notice("FILE SERVER NEEDS THE SD CARD", 4);
        } else if (auto_target == AutoLaunch::htmlviewer) {
            set_notice("HTML VIEWER NEEDS THE SD CARD", 4);
        } else if (auto_target == AutoLaunch::camera) {
            set_notice("CAMERA PACKAGE NEEDS THE SD CARD", 4);
        } else {
            set_notice("INSERT SD CARD TO LAUNCH", 4);
        }
        return DIRTY_FOOTER;
    }
    if (!installer_ready) {
        set_notice("LAUNCHER MIGRATION REQUIRED", 4);
        return DIRTY_FOOTER;
    }
    overlay = Overlay::installer;
    installer_auto_launch = auto_target;
    installer_auto_ollama = (auto_target == AutoLaunch::ollama);
    installer_escape = 0;
    installer_escape_at = 0;
    installer_scroll = 0;
    installer_selected = -1;
    installer_catalog.count = 0;
    pending_installer_scan = true;
    return DIRTY_OVERLAY | DIRTY_FOOTER;
}

uint32_t open_installer(bool ollama)
{
    return open_installer(ollama ? AutoLaunch::ollama : AutoLaunch::none);
}

void password_append(char c)
{
    size_t length = std::strlen(wifi_password);
    if (length + 1 < sizeof(wifi_password)) {
        wifi_password[length] = c;
        wifi_password[length + 1] = '\0';
    }
}

void password_backspace()
{
    size_t length = std::strlen(wifi_password);
    if (length) wifi_password[length - 1] = '\0';
}

void wifi_password_connect()
{
    if (!selected_ssid[0]) return;
    if (std::strlen(wifi_password) < 8) {
        set_notice("PASSWORD MUST BE 8+ CHARACTERS", 3);
        return;
    }
    std::snprintf(saved_wifi_ssid, sizeof(saved_wifi_ssid), "%s", selected_ssid);
    std::snprintf(saved_wifi_pass, sizeof(saved_wifi_pass), "%s", wifi_password);
    if (settings_ready) {
        nvs_set_str(settings, "wifi_ssid", saved_wifi_ssid);
        nvs_set_str(settings, "wifi_pass", saved_wifi_pass);
        nvs_commit(settings);
    }
    tab5_radio_wifi_connect(selected_ssid, wifi_password);
    set_notice("WIFI CONNECTING", 4);
    wifi_password_mode = false;
    password_visible = false;
    password_symbols = false;
    memset(wifi_password, 0, sizeof(wifi_password));
}

uint32_t handle_wifi_password_tap(int x, int y)
{
    if (portrait()) {
        if (within(x, y, 568, 82, 112, 52)) {
            wifi_password_mode = false;
            password_visible = false;
            password_symbols = false;
            memset(wifi_password, 0, sizeof(wifi_password));
            return DIRTY_OVERLAY | DIRTY_FOOTER;
        }
        if (within(x, y, 24, 620, 300, 60)) password_append(' ');
        else if (within(x, y, 336, 620, 170, 60)) password_symbols = !password_symbols;
        else if (within(x, y, 518, 620, 178, 60)) password_visible = !password_visible;
        else if (within(x, y, 24, 700, 672, 64)) {
            wifi_password_connect();
            return DIRTY_OVERLAY | DIRTY_CONTROLS | DIRTY_FOOTER;
        } else {
            const char *letters[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
            const int rows[] = {300, 374, 448, 522};
            for (int row = 0; row < 3; ++row) {
                int count = row == 2 ? 9 : 10;
                int start_x = row == 2 ? 50 : 16;
                for (int i = 0; i < count; ++i) {
                    if (!within(x, y, start_x + i * 69, rows[row], 63, 60)) continue;
                    char c = password_symbols ?
                        (row == 0 ? PASSWORD_SYMBOLS_0[i] :
                         row == 1 ? PASSWORD_SYMBOLS_1[i] : PASSWORD_SYMBOLS_2[i]) :
                        letters[row][i];
                    if (!password_symbols && !password_shift && row > 0) c += 32;
                    password_append(c);
                    return DIRTY_OVERLAY;
                }
            }
            if (within(x, y, 16, rows[3], 100, 60)) {
                if (password_symbols) password_symbols = false;
                else password_shift = !password_shift;
            } else {
                for (int i = 0; i < 7; ++i) {
                    if (!within(x, y, 120 + i * 69, rows[3], 63, 60)) continue;
                    char c = password_symbols ? PASSWORD_SYMBOLS_3[i] : letters[3][i];
                    if (!password_symbols && !password_shift) c += 32;
                    password_append(c);
                    return DIRTY_OVERLAY;
                }
                if (within(x, y, 606, rows[3], 90, 60)) password_backspace();
                else return DIRTY_NONE;
            }
        }
        return DIRTY_OVERLAY;
    }
    if (within(x, y, 1112, 132, 120, 50)) {
        wifi_password_mode = false;
        password_visible = false;
        password_symbols = false;
        memset(wifi_password, 0, sizeof(wifi_password));
        return DIRTY_OVERLAY | DIRTY_FOOTER;
    }
    if (within(x, y, 76, 492, 424, 44)) {
        password_append(' ');
        return DIRTY_OVERLAY;
    }
    if (within(x, y, 508, 492, 192, 44)) {
        password_symbols = !password_symbols;
        return DIRTY_OVERLAY;
    }
    if (within(x, y, 708, 492, 224, 44)) {
        password_visible = !password_visible;
        return DIRTY_OVERLAY;
    }
    if (within(x, y, 940, 492, 324, 44)) {
        wifi_password_connect();
        return DIRTY_OVERLAY | DIRTY_CONTROLS | DIRTY_FOOTER;
    }
    if (password_symbols) {
        for (int row = 0; row < 3; ++row) {
            for (int i = 0; i < 10; ++i) {
                if (within(x, y, 64 + i * 116, 278 + row * 52, 108, 44)) {
                    char c = row == 0 ? PASSWORD_SYMBOLS_0[i] :
                             (row == 1 ? PASSWORD_SYMBOLS_1[i] : PASSWORD_SYMBOLS_2[i]);
                    password_append(c);
                    return DIRTY_OVERLAY;
                }
            }
        }
        if (within(x, y, 122, 434, 108, 44)) {
            password_symbols = false;
            return DIRTY_OVERLAY;
        }
        for (int i = 0; i < 7; ++i) {
            if (within(x, y, 238 + i * 116, 434, 108, 44)) {
                password_append(PASSWORD_SYMBOLS_3[i]);
                return DIRTY_OVERLAY;
            }
        }
    } else {
        const char *rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
        for (int row = 0; row < 2; ++row) {
            for (int i = 0; i < 10; ++i) {
                if (within(x, y, 64 + i * 116, 278 + row * 52, 108, 44)) {
                    char c = rows[row][i];
                    if (!password_shift && c >= 'A' && c <= 'Z') c += 32;
                    password_append(c);
                    return DIRTY_OVERLAY;
                }
            }
        }
        for (int i = 0; i < 9; ++i) {
            if (within(x, y, 122 + i * 116, 382, 108, 44)) {
                char c = rows[2][i];
                if (!password_shift) c += 32;
                password_append(c);
                return DIRTY_OVERLAY;
            }
        }
        if (within(x, y, 122, 434, 108, 44)) {
            password_shift = !password_shift;
            return DIRTY_OVERLAY;
        }
        for (int i = 0; i < 7; ++i) {
            if (within(x, y, 238 + i * 116, 434, 108, 44)) {
                char c = rows[3][i];
                if (!password_shift) c += 32;
                password_append(c);
                return DIRTY_OVERLAY;
            }
        }
    }
    if (within(x, y, 1050, 434, 108, 44)) {
        password_backspace();
        return DIRTY_OVERLAY;
    }
    return DIRTY_NONE;
}

uint32_t handle_wifi_tap(int x, int y)
{
    if (wifi_password_mode) return handle_wifi_password_tap(x, y);
    if (within(x, y, portrait() ? 568 : 1112, portrait() ? 82 : 132,
               portrait() ? 112 : 120, portrait() ? 52 : 50)) {
        close_overlay();
        return DIRTY_RESTORE;
    }
    if (within(x, y, portrait() ? 318 : 862, portrait() ? 150 : 132,
               portrait() ? 110 : 116, 50)) {
        wifi_scroll = wifi_scroll >= 8 ? wifi_scroll - 8 : 0;
        return DIRTY_OVERLAY;
    }
    if (within(x, y, portrait() ? 438 : 986, portrait() ? 150 : 132,
               portrait() ? 110 : 116, 50)) {
        size_t last_page = radio_status.wifi_count > 8
            ? ((radio_status.wifi_count - 1) / 8) * 8 : 0;
        if (wifi_scroll + 8 <= last_page) wifi_scroll += 8;
        return DIRTY_OVERLAY;
    }
    if (within(x, y, portrait() ? 24 : 44, portrait() ? 1110 : 578,
               portrait() ? 210 : 350, portrait() ? 60 : 44)) {
        if (!radio_status.wifi_enabled) tab5_radio_set_wifi_enabled(true);
        wifi_scroll = 0;
        pending_scan_at = esp_timer_get_time() + 350000;
        return DIRTY_OVERLAY;
    }
    if (within(x, y, portrait() ? 255 : 406, portrait() ? 1110 : 578,
               portrait() ? 210 : 350, portrait() ? 60 : 44)) {
        tab5_radio_wifi_disconnect();
        return DIRTY_OVERLAY | DIRTY_CONTROLS | DIRTY_FOOTER;
    }
    if (within(x, y, portrait() ? 486 : 768, portrait() ? 1110 : 578,
               portrait() ? 210 : 350, portrait() ? 60 : 44)) {
        tab5_radio_set_wifi_enabled(false);
        wifi_on = false;
        save_setting("wifi", 0);
        close_overlay();
        return DIRTY_RESTORE | DIRTY_CONTROLS | DIRTY_FOOTER;
    }
    if (radio_status.wifi_enabled) {
        size_t shown = wifi_scroll < radio_status.wifi_count
            ? std::min<size_t>(8, radio_status.wifi_count - wifi_scroll) : 0;
        for (size_t i = 0; i < shown; ++i) {
            if (within(x, y, portrait() ? 32 : 44,
                       (portrait() ? 220 : 206) + static_cast<int>(i) *
                       (portrait() ? 88 : 43), portrait() ? 656 : 1132,
                       portrait() ? 72 : 38)) {
                tab5_wifi_network_t network {};
                if (!tab5_radio_get_wifi_network(wifi_scroll + i, &network)) break;
                std::snprintf(selected_ssid, sizeof(selected_ssid), "%s", network.ssid);
                if (network.secure) {
                    wifi_password_mode = true;
                    password_visible = false;
                    password_shift = false;
                    password_symbols = false;
                    if (saved_wifi_ssid[0] && std::strcmp(network.ssid, saved_wifi_ssid) == 0 && saved_wifi_pass[0]) {
                        std::snprintf(wifi_password, sizeof(wifi_password), "%s", saved_wifi_pass);
                    } else {
                        char r_pass[65] = {0};
                        char r_ssid[TAB5_RADIO_SSID_LEN] = {0};
                        if (tab5_radio_get_saved_wifi(r_ssid, sizeof(r_ssid), r_pass, sizeof(r_pass)) &&
                            std::strcmp(network.ssid, r_ssid) == 0 && r_pass[0]) {
                            std::snprintf(wifi_password, sizeof(wifi_password), "%s", r_pass);
                        } else {
                            memset(wifi_password, 0, sizeof(wifi_password));
                        }
                    }
                } else {
                    tab5_radio_wifi_connect(network.ssid, "");
                    set_notice("WIFI CONNECTING", 4);
                }
                return DIRTY_OVERLAY | DIRTY_CONTROLS | DIRTY_FOOTER;
            }
        }
    }
    return DIRTY_NONE;
}

uint32_t handle_ble_pair_input_tap(int x, int y)
{
    if (within(x, y, portrait() ? 32 : 230, portrait() ? 430 : 342,
               portrait() ? 190 : 200, portrait() ? 62 : 48)) {
        tab5_radio_ble_pair_confirm(false);
        pairing_pin[0] = '\0';
        return DIRTY_OVERLAY | DIRTY_FOOTER;
    }
    for (int i = 0; i < 12; ++i) {
        int col = i % 3, row = i / 3;
        if (!within(x, y, (portrait() ? 250 : 470) + col * (portrait() ? 110 : 120),
                    (portrait() ? 430 : 342) + row * (portrait() ? 74 : 58),
                    portrait() ? 100 : 108, portrait() ? 62 : 48)) continue;
        size_t length = std::strlen(pairing_pin);
        if (i <= 8 && length < 6) {
            pairing_pin[length] = static_cast<char>('1' + i);
            pairing_pin[length + 1] = '\0';
        } else if (i == 9 && length) {
            pairing_pin[length - 1] = '\0';
        } else if (i == 10 && length < 6) {
            pairing_pin[length] = '0';
            pairing_pin[length + 1] = '\0';
        } else if (i == 11 && length == 6) {
            uint32_t value = static_cast<uint32_t>(std::strtoul(pairing_pin, nullptr, 10));
            tab5_radio_ble_submit_passkey(value);
            pairing_pin[0] = '\0';
        }
        return DIRTY_OVERLAY | DIRTY_FOOTER;
    }
    return DIRTY_NONE;
}

uint32_t handle_bluetooth_tap(int x, int y)
{
    if (within(x, y, portrait() ? 568 : 1112, portrait() ? 82 : 132,
               portrait() ? 112 : 120, portrait() ? 52 : 50)) {
        close_overlay();
        return DIRTY_RESTORE;
    }
    if (radio_status.ble_pairing_mode == TAB5_BLE_PAIRING_CONFIRM) {
        if (within(x, y, portrait() ? 70 : 260, portrait() ? 610 : 466,
                   portrait() ? 275 : 340, 58)) tab5_radio_ble_pair_confirm(true);
        else if (within(x, y, portrait() ? 375 : 680, portrait() ? 610 : 466,
                        portrait() ? 275 : 340, 58)) tab5_radio_ble_pair_confirm(false);
        else return DIRTY_NONE;
        return DIRTY_OVERLAY | DIRTY_FOOTER;
    }
    if (radio_status.ble_pairing_mode == TAB5_BLE_PAIRING_INPUT)
        return handle_ble_pair_input_tap(x, y);
    if (radio_status.ble_pairing_mode == TAB5_BLE_PAIRING_DISPLAY) {
        if (within(x, y, portrait() ? 190 : 470, portrait() ? 600 : 470, 340, 58)) {
            tab5_radio_ble_pair_confirm(false);
            return DIRTY_OVERLAY | DIRTY_FOOTER;
        }
        return DIRTY_NONE;
    }
    if (within(x, y, portrait() ? 318 : 862, portrait() ? 150 : 132,
               portrait() ? 110 : 116, 50)) {
        ble_scroll = ble_scroll >= 8 ? ble_scroll - 8 : 0;
        return DIRTY_OVERLAY;
    }
    if (within(x, y, portrait() ? 438 : 986, portrait() ? 150 : 132,
               portrait() ? 110 : 116, 50)) {
        size_t last_page = radio_status.ble_count > 8
            ? ((radio_status.ble_count - 1) / 8) * 8 : 0;
        if (ble_scroll + 8 <= last_page) ble_scroll += 8;
        return DIRTY_OVERLAY;
    }
    if (within(x, y, portrait() ? 24 : 44, portrait() ? 1110 : 578,
               portrait() ? 210 : 350, portrait() ? 60 : 44)) {
        if (!radio_status.bluetooth_enabled) tab5_radio_set_bluetooth_enabled(true);
        ble_scroll = 0;
        pending_scan_at = esp_timer_get_time() + 350000;
        return DIRTY_OVERLAY;
    }
    if (within(x, y, portrait() ? 255 : 406, portrait() ? 1110 : 578,
               portrait() ? 210 : 350, portrait() ? 60 : 44)) {
        tab5_radio_ble_disconnect();
        return DIRTY_OVERLAY | DIRTY_CONTROLS | DIRTY_FOOTER;
    }
    if (within(x, y, portrait() ? 486 : 768, portrait() ? 1110 : 578,
               portrait() ? 210 : 350, portrait() ? 60 : 44)) {
        tab5_radio_set_bluetooth_enabled(false);
        bluetooth_on = false;
        save_setting("bt", 0);
        close_overlay();
        return DIRTY_RESTORE | DIRTY_CONTROLS | DIRTY_FOOTER;
    }
    if (radio_status.bluetooth_enabled) {
        size_t shown = ble_scroll < radio_status.ble_count
            ? std::min<size_t>(8, radio_status.ble_count - ble_scroll) : 0;
        for (size_t i = 0; i < shown; ++i) {
            if (within(x, y, portrait() ? 32 : 44,
                       (portrait() ? 220 : 206) + static_cast<int>(i) *
                       (portrait() ? 88 : 43), portrait() ? 656 : 1132,
                       portrait() ? 72 : 38)) {
                tab5_ble_device_t device {};
                if (tab5_radio_get_ble_device(ble_scroll + i, &device)) {
                    esp_err_t result = tab5_radio_ble_connect(&device);
                    set_notice(result == ESP_OK ? "BLUETOOTH CONNECTING" : "CHECK BLUETOOTH STATUS", 4);
                }
                return DIRTY_OVERLAY | DIRTY_CONTROLS | DIRTY_FOOTER;
            }
        }
    }
    return DIRTY_NONE;
}


uint32_t handle_installer_tap(int x, int y)
{
    if (installer_busy) return DIRTY_NONE;
    if (within(x, y, portrait() ? 568 : 1112, portrait() ? 82 : 132,
               portrait() ? 112 : 120, portrait() ? 52 : 50)) {
        installer_auto_launch = AutoLaunch::none;
        installer_auto_ollama = false;
        close_overlay();
        return DIRTY_RESTORE;
    }
    if (within(x, y, portrait() ? 318 : 862, portrait() ? 150 : 132,
               portrait() ? 110 : 116, 50)) {
        installer_scroll = installer_scroll > 5 ? installer_scroll - 5 : 0;
        return DIRTY_OVERLAY;
    }
    if (within(x, y, portrait() ? 438 : 986, portrait() ? 150 : 132,
               portrait() ? 110 : 116, 50)) {
        size_t last_page = installer_catalog.count > 5 ? installer_catalog.count - 5 : 0;
        installer_scroll = std::min(installer_scroll + 5, last_page);
        return DIRTY_OVERLAY;
    }
    if (within(x, y, portrait() ? 24 : 44, portrait() ? 1100 : 578,
               portrait() ? 210 : 350, portrait() ? 64 : 44)) {
        pending_installer_scan = true;
        installer_selected = -1;
        return DIRTY_OVERLAY;
    }
    if (within(x, y, portrait() ? 255 : 406, portrait() ? 1100 : 578,
               portrait() ? 441 : 712, portrait() ? 64 : 44)) {
        if (installer_ready && installer_selected >= 0) {
            installer_job = installer_catalog.packages[installer_selected];
            installer_progress = {TAB5_INSTALLER_VALIDATING, 0, "CHECKING FIRMWARE"};
            installer_busy = true;
            installer_worker_started = false;
            return DIRTY_OVERLAY;
        }
    }
    if (metrics.sd_ready && installer_catalog.count) {
        size_t shown = installer_catalog.count - installer_scroll;
        if (shown > 5) shown = 5;
        for (size_t i = 0; i < shown; ++i) {
            if (within(x, y, portrait() ? 32 : 44,
                       (portrait() ? 250 : 206) + static_cast<int>(i) *
                       (portrait() ? 130 : 58), portrait() ? 656 : 1132,
                       portrait() ? 100 : 50)) {
                installer_selected = static_cast<int>(installer_scroll + i);
                return DIRTY_OVERLAY | DIRTY_CONTROLS;
            }
        }
    }
    return DIRTY_NONE;
}

uint32_t handle_clock_tap(int x, int y)
{
    if (within(x, y, portrait() ? 568 : 1112, portrait() ? 82 : 132,
               portrait() ? 112 : 120, portrait() ? 52 : 50)) {
        if (clock_edit_mode) {
            clock_edit_mode = false;
            return DIRTY_OVERLAY;
        }
        close_overlay();
        return DIRTY_RESTORE;
    }

    if (clock_edit_mode) {
        if (portrait()) {
            for (int i = 0; i < 6; ++i) {
                int col = i / 3;
                int row = i % 3;
                int bx = col == 0 ? 32 : 375;
                int by = 170 + row * 110;
                if (within(x, y, bx, by, 313, 95)) {
                    editing_field = static_cast<ClockField>(i);
                    return DIRTY_OVERLAY;
                }
            }
            if (within(x, y, 32, 530, 313, 74)) {
                adjust_editing_time(editing_field, -1);
                return DIRTY_OVERLAY;
            }
            if (within(x, y, 375, 530, 313, 74)) {
                adjust_editing_time(editing_field, +1);
                return DIRTY_OVERLAY;
            }
            if (within(x, y, 32, 700, 313, 74)) {
                clock_edit_mode = false;
                return DIRTY_OVERLAY;
            }
            if (within(x, y, 375, 700, 313, 74)) {
                if (tab5_rtc_set(&editing_time)) {
                    std::snprintf(clock_status_message, sizeof(clock_status_message),
                                  "SAVED: %02d/%02d/%04d %02d:%02d:%02d",
                                  editing_time.day, editing_time.month, editing_time.year,
                                  editing_time.hour, editing_time.minute, editing_time.second);
                    set_notice("RTC DATE & TIME SAVED", 4);
                } else {
                    std::snprintf(clock_status_message, sizeof(clock_status_message), "RTC WRITE FAILED");
                    set_notice("RTC WRITE FAILED", 4);
                }
                clock_edit_mode = false;
                return DIRTY_OVERLAY | DIRTY_CLOCK | DIRTY_FOOTER;
            }
        } else {
            for (int i = 0; i < 6; ++i) {
                int bx = 80 + i * 190;
                int by = 200;
                if (within(x, y, bx, by, 170, 100)) {
                    editing_field = static_cast<ClockField>(i);
                    return DIRTY_OVERLAY;
                }
            }
            if (within(x, y, 80, 330, 545, 74)) {
                adjust_editing_time(editing_field, -1);
                return DIRTY_OVERLAY;
            }
            if (within(x, y, 655, 330, 545, 74)) {
                adjust_editing_time(editing_field, +1);
                return DIRTY_OVERLAY;
            }
            if (within(x, y, 80, 470, 545, 74)) {
                clock_edit_mode = false;
                return DIRTY_OVERLAY;
            }
            if (within(x, y, 655, 470, 545, 74)) {
                if (tab5_rtc_set(&editing_time)) {
                    std::snprintf(clock_status_message, sizeof(clock_status_message),
                                  "SAVED: %02d/%02d/%04d %02d:%02d:%02d",
                                  editing_time.day, editing_time.month, editing_time.year,
                                  editing_time.hour, editing_time.minute, editing_time.second);
                    set_notice("RTC DATE & TIME SAVED", 4);
                } else {
                    std::snprintf(clock_status_message, sizeof(clock_status_message), "RTC WRITE FAILED");
                    set_notice("RTC WRITE FAILED", 4);
                }
                clock_edit_mode = false;
                return DIRTY_OVERLAY | DIRTY_CLOCK | DIRTY_FOOTER;
            }
        }
    } else {
        if (portrait()) {
            if (within(x, y, 32, 360, 656, 70)) {
                current_zone = (current_zone + 1) % CLOCK_ZONE_COUNT;
                save_setting("zone", static_cast<uint8_t>(current_zone));
                return DIRTY_OVERLAY;
            }
            if (within(x, y, 32, 540, 656, 80)) {
                if (clock_sync_in_progress) return DIRTY_NONE;
                if (!radio_status.wifi_has_ip) {
                    std::snprintf(clock_status_message, sizeof(clock_status_message),
                                  "CONNECT TO WI-FI FIRST");
                    return DIRTY_OVERLAY;
                }
                xTaskCreate(clock_sync_worker, "ntp_sync", 4096, nullptr, 2, nullptr);
                return DIRTY_OVERLAY;
            }
            if (within(x, y, 32, 640, 656, 80)) {
                clock_edit_mode = true;
                editing_field = ClockField::day;
                if (!tab5_rtc_read(&editing_time)) {
                    editing_time = {2026, 10, 31, 6, 12, 0, 0, true};
                }
                return DIRTY_OVERLAY;
            }
        } else {
            if (within(x, y, 80, 360, 545, 64)) {
                current_zone = (current_zone + 1) % CLOCK_ZONE_COUNT;
                save_setting("zone", static_cast<uint8_t>(current_zone));
                return DIRTY_OVERLAY;
            }
            if (within(x, y, 80, 450, 545, 74)) {
                if (clock_sync_in_progress) return DIRTY_NONE;
                if (!radio_status.wifi_has_ip) {
                    std::snprintf(clock_status_message, sizeof(clock_status_message),
                                  "CONNECT TO WI-FI FIRST");
                    return DIRTY_OVERLAY;
                }
                xTaskCreate(clock_sync_worker, "ntp_sync", 4096, nullptr, 2, nullptr);
                return DIRTY_OVERLAY;
            }
            if (within(x, y, 655, 450, 545, 74)) {
                clock_edit_mode = true;
                editing_field = ClockField::day;
                if (!tab5_rtc_read(&editing_time)) {
                    editing_time = {2026, 10, 31, 6, 12, 0, 0, true};
                }
                return DIRTY_OVERLAY;
            }
        }
    }

    return DIRTY_NONE;
}

uint32_t handle_overlay_tap(int x, int y)
{
    if (overlay == Overlay::wifi) return handle_wifi_tap(x, y);
    if (overlay == Overlay::bluetooth) return handle_bluetooth_tap(x, y);
    if (overlay == Overlay::installer) return handle_installer_tap(x, y);
    if (overlay == Overlay::clock) return handle_clock_tap(x, y);
    return DIRTY_NONE;
}

uint32_t handle_overlay_key(uint8_t key)
{
    if (overlay == Overlay::none) return DIRTY_NONE;
    if (overlay == Overlay::wifi && wifi_password_mode) {
        if (key == 0x1b) {
            wifi_password_mode = false;
            memset(wifi_password, 0, sizeof(wifi_password));
            return DIRTY_OVERLAY;
        }
        if (key == '\r') {
            wifi_password_connect();
            return DIRTY_OVERLAY | DIRTY_CONTROLS | DIRTY_FOOTER;
        }
        if (key == 0x7f || key == '\b') {
            password_backspace();
            return DIRTY_OVERLAY;
        }
        if (key >= 32 && key <= 126) {
            password_append(static_cast<char>(key));
            return DIRTY_OVERLAY;
        }
        return DIRTY_NONE;
    }
    if (overlay == Overlay::bluetooth && radio_status.ble_pairing_mode == TAB5_BLE_PAIRING_CONFIRM) {
        if (key == 'y' || key == 'Y' || key == '\r') {
            tab5_radio_ble_pair_confirm(true);
            return DIRTY_OVERLAY | DIRTY_FOOTER;
        }
        if (key == 'n' || key == 'N' || key == 0x1b) {
            tab5_radio_ble_pair_confirm(false);
            return DIRTY_OVERLAY | DIRTY_FOOTER;
        }
        return DIRTY_NONE;
    }
    if (overlay == Overlay::bluetooth && radio_status.ble_pairing_mode == TAB5_BLE_PAIRING_INPUT) {
        if (key == 0x1b) {
            tab5_radio_ble_pair_confirm(false);
            pairing_pin[0] = '\0';
            return DIRTY_OVERLAY | DIRTY_FOOTER;
        }
        if (key == '\r') {
            if (std::strlen(pairing_pin) == 6)
                tab5_radio_ble_submit_passkey(static_cast<uint32_t>(std::strtoul(pairing_pin, nullptr, 10)));
            pairing_pin[0] = '\0';
            return DIRTY_OVERLAY | DIRTY_FOOTER;
        }
        if (key == 0x7f || key == '\b') {
            size_t length = std::strlen(pairing_pin);
            if (length) pairing_pin[length - 1] = '\0';
            return DIRTY_OVERLAY;
        }
        if (key >= '0' && key <= '9' && std::strlen(pairing_pin) < 6) {
            size_t length = std::strlen(pairing_pin);
            pairing_pin[length] = static_cast<char>(key);
            pairing_pin[length + 1] = '\0';
            return DIRTY_OVERLAY;
        }
        return DIRTY_NONE;
    }
    if (overlay == Overlay::bluetooth && radio_status.ble_keyboard_ready) {
        if (key == 0x1b) { ble_escape = 1; return DIRTY_NONE; }
        if (ble_escape == 1 && key == '[') { ble_escape = 2; return DIRTY_NONE; }
        if (ble_escape == 2) {
            ble_escape = 0;
            ble_last_key = key == 'A' ? "UP" : key == 'B' ? "DOWN" :
                           key == 'C' ? "RIGHT" : key == 'D' ? "LEFT" : "NAVIGATION";
            return DIRTY_BLE_TEST;
        }
        ble_escape = 0;
        size_t length = std::strlen(ble_test);
        if (key == 0x7f || key == '\b') {
            if (length) ble_test[length - 1] = 0;
            ble_last_key = "BACKSPACE";
        } else if (key == '\r') { ble_test[0] = 0; ble_last_key = "ENTER"; }
        else if (key >= 32 && key <= 126) {
            if (length == sizeof(ble_test) - 1) {
                std::memmove(ble_test, ble_test + 1, length); --length;
            }
            ble_test[length] = static_cast<char>(key); ble_test[length + 1] = 0;
            ble_last_key = "";
        } else ble_last_key = "CONTROL KEY";
        return DIRTY_BLE_TEST;
    }
    if (overlay == Overlay::installer) {
        if (installer_busy) return DIRTY_NONE;
        if (key == 0x1b) {
            if (installer_escape == 1) {
                installer_escape = 0;
                close_overlay();
                return DIRTY_RESTORE;
            }
            installer_escape = 1;
            installer_escape_at = esp_timer_get_time();
            return DIRTY_NONE;
        }
        if (installer_escape == 1 && (key == '[' || key == 'O')) {
            installer_escape = 2;
            return DIRTY_NONE;
        }
        if (installer_escape == 2) {
            installer_escape = 0;
            if (installer_catalog.count == 0) return DIRTY_NONE;
            if (key == 'A' || key == 'D') {
                if (installer_selected < 0) installer_selected = 0;
                else if (installer_selected > 0) --installer_selected;
                if (static_cast<size_t>(installer_selected) < installer_scroll) {
                    installer_scroll = static_cast<size_t>(installer_selected);
                }
                return DIRTY_OVERLAY;
            }
            if (key == 'B' || key == 'C') {
                if (installer_selected < 0) installer_selected = 0;
                else if (static_cast<size_t>(installer_selected + 1) < installer_catalog.count) {
                    ++installer_selected;
                }
                if (static_cast<size_t>(installer_selected) >= installer_scroll + 5) {
                    installer_scroll = static_cast<size_t>(installer_selected) - 4;
                }
                return DIRTY_OVERLAY;
            }
            return DIRTY_NONE;
        }
        installer_escape = 0;
        if (key == '\r' || key == '\n') {
            if (installer_ready && installer_catalog.count > 0) {
                if (installer_selected < 0) installer_selected = 0;
                if (static_cast<size_t>(installer_selected) < installer_catalog.count) {
                    installer_job = installer_catalog.packages[installer_selected];
                    installer_progress = {TAB5_INSTALLER_VALIDATING, 0, "CHECKING FIRMWARE"};
                    installer_busy = true;
                    installer_worker_started = false;
                    return DIRTY_OVERLAY;
                }
            }
        }
        return DIRTY_NONE;
    }
    if (key == 0x1b) {
        close_overlay();
        return DIRTY_RESTORE;
    }
    return DIRTY_NONE;
}

uint32_t handle_tap(int x, int y)
{
    if (overlay != Overlay::none) return handle_overlay_tap(x, y);
    if (y < 54) {
        int tab = std::min(5, x * 6 / screen_width());
        if (tab == 1) return open_installer(AutoLaunch::shell);
        if (tab == 2) return open_installer(true);
        if (tab == 3) return open_installer(AutoLaunch::fileserver);
        if (tab == 4) return open_installer(AutoLaunch::htmlviewer);
        if (tab == 5) return open_installer(AutoLaunch::camera);
        return DIRTY_NONE;
    }

    const int clock_x = portrait() ? 438 : 1004;
    const int clock_w = portrait() ? 258 : 252;
    if (within(x, y, clock_x, 64, clock_w, 70)) {
        overlay = Overlay::clock;
        clock_edit_mode = false;
        clock_status_message[0] = '\0';
        return DIRTY_OVERLAY;
    }

    const int card_width = portrait() ? 332 : 402;
    const int appearance_x = 24, appearance_y = 150;
    const int weather_x = portrait() ? 364 : 439, weather_y = 150;
    const int wifi_x = portrait() ? 24 : 854, wifi_y = portrait() ? 262 : 150;
    const int brightness_x = portrait() ? 364 : 24, brightness_y = 262;
    const int volume_x = portrait() ? 24 : 439, volume_y = portrait() ? 374 : 262;
    const int bluetooth_x = portrait() ? 364 : 854, bluetooth_y = portrait() ? 374 : 262;
    const int launch_x = portrait() ? 24 : 854, launch_y = portrait() ? 598 : 374;
    const int ram_x = portrait() ? 364 : 439, ram_y = portrait() ? 486 : 374;

    if (within(x, y, appearance_x, appearance_y, card_width, 100)) {
        dark_mode = !dark_mode;
        theme = dark_mode ? DARK : LIGHT;
        save_setting("dark", dark_mode);
        set_notice(dark_mode ? "DARK APPEARANCE" : "LIGHT APPEARANCE", 2);
        return DIRTY_ALL;
    } else if (within(x, y, weather_x, weather_y, card_width, 100)) {
        return open_installer(false);
    } else if (within(x, y, wifi_x, wifi_y, card_width, 100)) {
        if (!radio_ready) {
            set_notice("HOSTED WIFI NOT READY", 3);
            return DIRTY_FOOTER;
        }
        if (!radio_status.wifi_enabled) {
            tab5_radio_set_wifi_enabled(true);
            wifi_on = true;
            save_setting("wifi", 1);
            pending_scan_at = esp_timer_get_time() + 400000;
        } else {
            pending_scan_at = esp_timer_get_time();
        }
        overlay = Overlay::wifi;
        wifi_password_mode = false;
        return DIRTY_OVERLAY | DIRTY_CONTROLS | DIRTY_FOOTER;
    } else if (within(x, y, brightness_x + card_width - 124, brightness_y + 18, 50, 64)) {
        adjust_brightness(+10);
        return DIRTY_CONTROLS;
    } else if (within(x, y, brightness_x + card_width - 73, brightness_y + 18, 50, 64)) {
        adjust_brightness(-10);
        return DIRTY_CONTROLS;
    } else if (within(x, y, volume_x + card_width - 124, volume_y + 18, 50, 64)) {
        adjust_volume(+10);
        return DIRTY_CONTROLS | DIRTY_FOOTER;
    } else if (within(x, y, volume_x + card_width - 73, volume_y + 18, 50, 64)) {
        adjust_volume(-10);
        return DIRTY_CONTROLS | DIRTY_FOOTER;
    } else if (within(x, y, bluetooth_x, bluetooth_y, card_width, 100)) {
        if (!radio_ready) {
            set_notice("HOSTED BLUETOOTH NOT READY", 3);
            return DIRTY_FOOTER;
        }
        if (!radio_status.bluetooth_enabled) {
            tab5_radio_set_bluetooth_enabled(true);
            bluetooth_on = true;
            save_setting("bt", 1);
            pending_scan_at = esp_timer_get_time() + 2000000;
        } else {
            pending_scan_at = esp_timer_get_time();
        }
        overlay = Overlay::bluetooth;
        pairing_pin[0] = '\0';
        return DIRTY_OVERLAY | DIRTY_CONTROLS | DIRTY_FOOTER;
    } else if (within(x, y, launch_x, launch_y, portrait() ? 672 : 402, 100)) {
        return open_installer(false);
    } else if (within(x, y, ram_x, ram_y, card_width, 100)) {
        refresh_ram();
        return DIRTY_STORAGE | DIRTY_FOOTER;
    }

    const auto power_action = tab5::power_hit(portrait(), x, y);
    if (power_action == tab5::PowerAction::sleep) {
        screen_sleep();
        return DIRTY_ALL;
    } else if (power_action == tab5::PowerAction::lock) {
        power_confirm = false;
        tab5::matrix_clock_run(brightness);
        return DIRTY_ALL;
    } else if (power_action == tab5::PowerAction::rotate) {
        display_rotation = (display_rotation + 1U) & 3U;
        if (tab5_display_set_rotation(static_cast<tab5_display_rotation_t>(display_rotation)) != ESP_OK) {
            display_rotation = (display_rotation + 3U) & 3U;
            set_notice("ROTATION FAILED", 3);
            return DIRTY_FOOTER;
        }
        save_setting("rotation", display_rotation);
        power_confirm = false;
        set_notice(portrait() ? "PORTRAIT ORIENTATION" : "LANDSCAPE ORIENTATION", 3);
        return DIRTY_ALL;
    } else if (power_action == tab5::PowerAction::power) {
        int64_t now = esp_timer_get_time();
        if (power_confirm && now < power_confirm_until) {
            tab5_display_set_brightness(0);
            tab5_power_off();
        } else {
            power_confirm = true;
            power_confirm_until = now + 5000000;
            set_notice("TAP POWER OFF AGAIN TO CONFIRM", 5);
            return DIRTY_POWER | DIRTY_FOOTER;
        }
    }
    return DIRTY_NONE;
}
} // namespace

extern "C" void app_main(void)
{
    load_settings();
    weather_saved = tab5_weather_snapshot_load(&weather_snapshot) == ESP_OK;
    ESP_LOGI(TAG, "Last Weather snapshot=%s (read-only; no weather fetch)",
             weather_saved ? "AVAILABLE" : "EMPTY");
    installer_events = xQueueCreate(1, sizeof(tab5_installer_status_t));
    tab5_installer_set_progress_callback(post_installer_progress, nullptr);
    installer_ready = tab5_app_boot_check_layout() == ESP_OK;
    installer_ready = installer_ready && installer_events;
    ESP_LOGI(TAG, "SD launcher layout=%s factory=0x10000 slots=0x420000,0x920000",
             installer_ready ? "READY" : "MIGRATION_REQUIRED");
    tab5_board_status board = tab5_board_init();
    tab5_kbd_queue = xQueueCreate(64, sizeof(uint8_t));
    const bool tab5_kbd_ready = tab5::start_tab5_kbd_input([](const uint8_t *data, size_t len) {
        if (!tab5_kbd_queue || !data) return;
        for (size_t i = 0; i < len; ++i) xQueueSend(tab5_kbd_queue, &data[i], 0);
    });
    ESP_LOGI(TAG, "TAB5 Console %s: display=%d touch=%d sd=%d USB HID=%d Tab5KBD=%d ST7121 firmware=%u",
             esp_app_get_description()->version, board.display_ready, board.touch_ready,
             board.sd_ready, board.usb_hid_ready, tab5_kbd_ready, tab5_touch_firmware_version());
    if (!board.display_ready) {
        ESP_LOGE(TAG, "Display unavailable; Console cannot continue");
        return;
    }

    ESP_ERROR_CHECK(tab5_display_set_rotation(
        static_cast<tab5_display_rotation_t>(display_rotation)));
    tab5_display_set_brightness(brightness);
    tab5_radio_power_set(true);
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_err_t radio_error = tab5_radio_init();
    if (radio_error == ESP_OK) {
        radio_ready = true;
        tab5_radio_get_status(&radio_status);
        last_radio_revision = radio_status.revision;
        tab5_radio_set_wifi_enabled(wifi_on);
        tab5_radio_set_bluetooth_enabled(bluetooth_on);
        tab5_radio_get_status(&radio_status);
        last_radio_revision = radio_status.revision;
    } else {
        ESP_LOGE(TAG, "Hosted radio init failed: %s", esp_err_to_name(radio_error));
    }
    refresh_storage();
    refresh_ram();
    refresh_battery();
    if (tab5_rtc_was_initialized_from_build())
        set_notice("RTC SET FROM BUILD TIME - WIFI SYNC LATER", 8);
    if (!radio_ready) set_notice("HOSTED RADIO INIT FAILED", 8);
    draw_ui();

    bool was_down = false;
    int press_x = 0, press_y = 0, last_x = 0, last_y = 0;
    int64_t last_clock = esp_timer_get_time();
    int64_t last_battery = last_clock;
    int64_t last_radio_poll = last_clock;
    bool old_keyboard = tab5_keyboard_connected();
    for (;;) {
        uint32_t dirty = DIRTY_NONE;
        int x, y;
        bool down = board.touch_ready && tab5_touch_poll(&x, &y);
        if (down) {
            last_x = x; last_y = y;
            if (!was_down) { press_x = x; press_y = y; }
        }
        if (!down && was_down) {
            if (std::abs(last_x - press_x) < 40 && std::abs(last_y - press_y) < 40)
                dirty |= handle_tap(press_x, press_y);
        }
        was_down = down;

        uint8_t keys[32];
        size_t key_count;
        uint8_t kbd_byte = 0;
        while (tab5_kbd_queue && xQueueReceive(tab5_kbd_queue, &kbd_byte, 0) == pdTRUE) {
            dirty |= handle_overlay_key(kbd_byte);
        }
        while ((key_count = tab5_keyboard_poll(keys, sizeof(keys))) > 0) {
            for (size_t i = 0; i < key_count; ++i) dirty |= handle_overlay_key(keys[i]);
        }
        while ((key_count = tab5_radio_ble_keyboard_poll(keys, sizeof(keys))) > 0) {
            for (size_t i = 0; i < key_count; ++i) dirty |= handle_overlay_key(keys[i]);
        }
        bool keyboard = tab5_keyboard_connected();
        if (keyboard != old_keyboard) {
            old_keyboard = keyboard;
            set_notice(keyboard ? "USB KEYBOARD CONNECTED" : "USB KEYBOARD DISCONNECTED", 3);
            dirty |= DIRTY_FOOTER;
        }

        int64_t now = esp_timer_get_time();
        if (overlay == Overlay::installer && installer_escape == 1 &&
            now - installer_escape_at >= 40000) {
            installer_escape = 0;
            close_overlay();
            dirty |= DIRTY_RESTORE;
        }
        if (radio_ready && now - last_radio_poll >= 200000) {
            tab5_radio_status_t next {};
            tab5_radio_get_status(&next);
            if (next.revision != last_radio_revision) {
                tab5_ble_pairing_mode_t old_pairing = radio_status.ble_pairing_mode;
                bool visible_change = radio_visible_state_changed(radio_status, next);
                radio_status = next;
                last_radio_revision = next.revision;
                wifi_on = next.wifi_enabled;
                bluetooth_on = next.bluetooth_enabled;
                if (old_pairing != TAB5_BLE_PAIRING_INPUT &&
                    next.ble_pairing_mode == TAB5_BLE_PAIRING_INPUT) pairing_pin[0] = '\0';
                if (visible_change) {
                    dirty |= DIRTY_CONTROLS | DIRTY_FOOTER;
                    if (overlay != Overlay::none) dirty |= DIRTY_OVERLAY;
                }
            }
            last_radio_poll = now;
        }
        if (pending_scan_at && now >= pending_scan_at) {
            esp_err_t scan_result = ESP_ERR_INVALID_STATE;
            if (overlay == Overlay::wifi && radio_status.wifi_enabled)
                scan_result = tab5_radio_wifi_scan();
            else if (overlay == Overlay::bluetooth && radio_status.bluetooth_enabled)
                scan_result = tab5_radio_ble_scan();
            if (scan_result != ESP_OK && overlay != Overlay::none)
                set_notice("SCAN NOT READY - TAP SCAN", 3);
            pending_scan_at = 0;
        }
        if (pending_installer_scan && overlay == Overlay::installer && !installer_busy) {
            pending_installer_scan = false;
            installer_catalog.count = 0;
            installer_selected = -1;
            tab5_installer_status_t scan_status{};
            esp_err_t r = tab5_installer_scan(&installer_catalog);
            tab5_installer_get_status(&scan_status);
            if ((installer_auto_launch != AutoLaunch::none || installer_auto_ollama) && r == ESP_OK) {
                const AutoLaunch target_mode = (installer_auto_launch != AutoLaunch::none) ? installer_auto_launch : AutoLaunch::ollama;
                const char *target = auto_launch_project(target_mode);
                for (size_t i = 0; i < installer_catalog.count; ++i) {
                    if (std::strcmp(installer_catalog.packages[i].project, target) == 0) {
                        installer_selected = static_cast<int>(i);
                        installer_job = installer_catalog.packages[i];
                        installer_progress.state = TAB5_INSTALLER_VALIDATING;
                        installer_progress.progress = 0;
                        std::snprintf(installer_progress.message, sizeof(installer_progress.message), "%s",
                                      auto_launch_checking_message(target_mode));
                        installer_busy = true;
                        installer_worker_started = false;
                        break;
                    }
                }
                if (!installer_busy) {
                    const char *missing_msg = auto_launch_missing_message(target_mode);
                    installer_auto_launch = AutoLaunch::none;
                    installer_auto_ollama = false;
                    set_notice(missing_msg, 6);
                }
            } else {
                if (r == ESP_OK && installer_catalog.count > 0) {
                    installer_selected = 0;
                }
                set_notice(r == ESP_OK && installer_catalog.count
                           ? "SD APPS FOUND" : scan_status.message, 3);
            }
            dirty |= DIRTY_OVERLAY;
        }
        if (installer_busy && !installer_worker_started) {
            xQueueReset(installer_events);
            redraw(DIRTY_OVERLAY); // Present the stable loading page before any work.
            dirty = DIRTY_NONE;
            installer_last_paint = now;
            installer_worker_started = true;
            if (xTaskCreate(installer_worker, "sd_install", 12288, nullptr, 2, nullptr) != pdPASS) {
                installer_busy = false;
                installer_worker_started = false;
                set_notice("CANNOT START INSTALLER", 4);
                dirty |= DIRTY_OVERLAY | DIRTY_FOOTER;
            }
        }
        if (installer_busy) {
            tab5_installer_status_t next{};
            if (xQueuePeek(installer_events, &next, 0) == pdTRUE &&
                (now - installer_last_paint >= 200000 || next.state == TAB5_INSTALLER_DONE ||
                 next.state == TAB5_INSTALLER_FAILED)) {
                xQueueReceive(installer_events, &installer_progress, 0);
                installer_last_paint = now;
                dirty |= DIRTY_INSTALLER;
                if (installer_progress.state == TAB5_INSTALLER_DONE) {
                    redraw(DIRTY_INSTALLER);
                    vTaskDelay(pdMS_TO_TICKS(350));
                    tab5_display_set_brightness(0);
                    tab5_sd_unmount();
                    esp_restart();
                } else if (installer_progress.state == TAB5_INSTALLER_FAILED) {
                    installer_busy = false;
                    installer_worker_started = false;
                    installer_auto_launch = AutoLaunch::none;
                    installer_auto_ollama = false;
                    set_notice(installer_progress.message, 6);
                    dirty |= DIRTY_OVERLAY | DIRTY_FOOTER;
                }
            }
        }
        if (power_confirm && now >= power_confirm_until) {
            power_confirm = false;
            dirty |= DIRTY_POWER;
        }
        if (notice_until != 0 && now >= notice_until) {
            notice_until = 0;
            dirty |= DIRTY_FOOTER;
        }
        if (now - last_battery >= 3000000) {
            bool old_ready = metrics.battery_ready;
            uint8_t old_percent = metrics.battery_percent;
            bool old_charging = metrics.battery_charging;
            refresh_battery();
            last_battery = now;
            if (old_ready != metrics.battery_ready ||
                old_percent != metrics.battery_percent ||
                old_charging != metrics.battery_charging) {
                dirty |= DIRTY_BATTERY;
            }
        }
        if (now - last_clock >= 1000000) {
            dirty |= DIRTY_CLOCK;
            if (overlay == Overlay::clock && !clock_edit_mode) dirty |= DIRTY_OVERLAY;
            last_clock = now;
        }
        redraw(dirty);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

#pragma once
#include "weather_model.h"
namespace wm {
enum class Page {
    current,
    hourly,
    details,
    settings,
    locations,
    place,
    search,
    results,
    network,
    wifi_password,
    about,
    sd_library
};
enum class Kind {
    none,
    page,
    theme,
    unit,
    console,
    places,
    add,
    place,
    use,
    remove,
    previous,
    next,
    key,
    keyboard,
    symbols,
    erase,
    search,
    result,
    retry,
    wifi_scan,
    wifi_network,
    wifi_join,
    wifi_disconnect,
    refresh,
    back,
    sd_select
};
struct Action {
    Kind kind = Kind::none;
    unsigned value = 0;
    bool operator==(const Action &a) const { return kind == a.kind && value == a.value; }
    bool operator!=(const Action &a) const { return !(*this == a); }
};
struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
struct Button {
    Rect rect;
    Action action;
    char label[42]{}, subtitle[38]{};
    bool selected = false, enabled = true;
};
constexpr unsigned max_wifi_networks = 16;
struct WifiNetworkView {
    char ssid[33]{};
    int8_t rssi = 0;
    bool secure = false;
};
struct SdPackage {
    char project[33]{}, version[33]{}, image[101]{}, license[101]{};
    bool valid = false;
};
struct View {
    Settings settings{};
    Forecast forecast{};
    SearchResults results{};
    Page page = Page::current;
    unsigned list_page = 0, result_page = 0, manage = 0, keyboard_page = 0,
             wifi_page = 0, wifi_keyboard_page = 0, wifi_count = 0;
    bool has = false, busy = false, connected = false, clock_valid = false, stale = false,
         console_ok = true, wifi_scanning = false, wifi_has_saved = false,
         wifi_selected_secure = false, searching = false;
    WifiNetworkView wifi_networks[max_wifi_networks]{};
    char query[41]{}, status[64]{}, wifi_ssid[33]{}, wifi_selected_ssid[33]{},
         wifi_password[65]{};
    int64_t now = 0;
    bool sd_ready = false;
    unsigned sd_count = 0, sd_page = 0, sd_selected = 0;
    SdPackage sd_packages[8]{};
};
unsigned buttons(const View &, Button *, unsigned);
Action hit(const View &, int, int);
void prepare(const View &);
uint16_t pixel(int, int, void *user = nullptr);
bool scene_valid();
int text_width(const char *, unsigned face = 1, unsigned scale = 1);
} // namespace wm

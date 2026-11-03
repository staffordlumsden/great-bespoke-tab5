#include "console_bridge.h"
#include "tab5_app_boot.h"
#include <cstdio>

namespace wm {
bool console_available() { return tab5_app_boot_console_available(); }
bool confirm_first_boot(bool hardware_ok) {
    const auto result = tab5_app_boot_ready(hardware_ok);
    std::printf("TAB5_WX_BOOT confirm_and_arm_console=%s\n", esp_err_to_name(result));
    return result == ESP_OK;
}
bool return_to_console() {
    const auto result = tab5_app_boot_return_to_console();
    std::printf("TAB5_WX_CONSOLE return=%s nvs=PRESERVED\n", esp_err_to_name(result));
    std::fflush(stdout);
    if (result != ESP_OK)
        return false;
    return true;
}
} // namespace wm

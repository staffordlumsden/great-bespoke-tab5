/* Isolated Tab5 hardware diagnostics. No network or application integration. */
#include "sdkconfig.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "tab5_hal.h"
static const char *TAG = "TAB5_BRINGUP";
static const uint32_t FG = 0xd8f4ed, BG = 0x102028;
// Diagnostic-only 5x7 uppercase font, columns with least-significant bit at top.
static const uint8_t glyphs[][5] = {
 {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
 {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
 {6,0x49,0x49,0x29,0x1e},
 {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
 {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
 {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},{0,0x41,0x7f,0x41,0},
 {0x20,0x40,0x41,0x3f,1},{0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
 {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
 {0x7f,9,9,9,6},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},{0x3f,0x40,0x40,0x40,0x3f},
 {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
 {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
};
static void text(int x, int y, const char *s, int scale, uint32_t color)
{
    for (; *s && x + 6 * scale <= TAB5_DISPLAY_WIDTH; s++, x += 6 * scale) {
        int c = toupper((unsigned char)*s);
        const uint8_t *g = NULL;
        if (c >= '0' && c <= '9') g = glyphs[c - '0'];
        if (c >= 'A' && c <= 'Z') g = glyphs[10 + c - 'A'];
        if (g) {
            for (int col = 0; col < 5; col++) for (int row = 0; row < 7; row++)
                if (g[col] & (1 << row)) tab5_display_rect(x + col * scale, y + row * scale, scale, scale, color);
        } else if (c == '-' || c == '=') tab5_display_rect(x, y + 3 * scale, 5 * scale, scale, color);
        else if (c == '.') tab5_display_rect(x + 2 * scale, y + 6 * scale, scale, scale, color);
        else if (c == ':') {
            tab5_display_rect(x + 2 * scale, y + 2 * scale, scale, scale, color);
            tab5_display_rect(x + 2 * scale, y + 5 * scale, scale, scale, color);
        }
    }
}
static void line(int y, const char *s, uint32_t color)
{
    tab5_display_rect(24, y, 1232, 28, BG);
    text(24, y, s, 3, color);
}
void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "Tab5 isolated HAL: IDF %s target=%s revision=%u flash=%u MB PSRAM=%u bytes",
             esp_get_idf_version(), CONFIG_IDF_TARGET, chip.revision, flash_size / (1024 * 1024),
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    struct tab5_board_status s = tab5_board_init();
    ESP_LOGI(TAG, "display=%s ready=%d; touch=%s ready=%d addr=0x%02x firmware=%u; sd=%s ready=%d; USB HID=%s ready=%d",
             esp_err_to_name(s.display), s.display_ready, esp_err_to_name(s.touch), s.touch_ready,
             tab5_touch_address(), tab5_touch_firmware_version(), esp_err_to_name(s.sd), s.sd_ready,
             esp_err_to_name(s.usb_hid), s.usb_hid_ready);
    esp_err_t sd_test = s.sd_ready ? tab5_sd_self_test() : ESP_ERR_INVALID_STATE;
    if (s.display_ready) {
        tab5_display_fill_rgb(BG);
        uint32_t bars[] = {0xff0000, 0x00ff00, 0x0000ff, 0xffffff};
        for (int i = 0; i < 4; i++) tab5_display_rect(i * 320, 0, 320, 12, bars[i]);
        text(24, 32, "TAB5 HAL - ST7121", 5, FG);
        line(106, "DISPLAY READY - 1280 X 720", FG);
        line(150, s.touch_ready ? "TOUCH READY - TAP THE SCREEN" : "TOUCH FAILED", s.touch_ready ? FG : 0xff8080);
        line(194, sd_test == ESP_OK ? "SD READ WRITE PASS" : "SD TEST FAILED - SEE SERIAL", sd_test == ESP_OK ? FG : 0xff8080);
        line(238, s.usb_hid_ready ? "USB HID READY - CONNECT KEYBOARD" : "USB HID FAILED", FG);
        line(326, "KEYBOARD INPUT", FG);
        line(590, "TOUCH BELOW TO DRAW WHITE MARKS", FG);
        tab5_display_present();
    }
    char typed[65] = {0}; size_t used = 0;
    int old_x = -100, old_y = -100; bool was_touch = false, was_keyboard = false;
    int64_t last_heartbeat = 0;
    for (;;) {
        bool changed = false;
        int x, y;
        bool down = tab5_touch_poll(&x, &y);
        if (down && (!was_touch || x > old_x + 8 || x < old_x - 8 || y > old_y + 8 || y < old_y - 8)) {
            ESP_LOGI(TAG, "TOUCH x=%d y=%d", x, y);
            old_x = x; old_y = y;
            char msg[64]; snprintf(msg, sizeof(msg), "TOUCH X=%d Y=%d", x, y);
            line(282, msg, FG);
            if (y >= 625) tab5_display_rect(x > 1271 ? 1271 : (x > 4 ? x - 4 : 0), y > 711 ? 711 : y - 4, 9, 9, 0xffffff);
            changed = true;
        }
        if (was_touch && !down) ESP_LOGI(TAG, "TOUCH released");
        was_touch = down;
        bool keyboard = tab5_keyboard_connected();
        if (keyboard != was_keyboard) {
            line(238, keyboard ? "USB KEYBOARD CONNECTED" : "USB KEYBOARD DISCONNECTED", FG);
            was_keyboard = keyboard; changed = true;
        }
        uint8_t input[32]; size_t n = tab5_keyboard_poll(input, sizeof(input));
        if (n) {
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, input, n, ESP_LOG_INFO);
            for (size_t i = 0; i < n; i++) {
                if (input[i] == '\r') { used = 0; typed[0] = 0; }
                else if (input[i] == 0x7f && used) typed[--used] = 0;
                else if (input[i] >= 32 && input[i] < 127) {
                    if (used == sizeof(typed) - 1) used = 0;
                    typed[used++] = input[i]; typed[used] = 0;
                }
            }
            line(370, typed, 0xffe090);
            changed = true;
        }
        int64_t now = esp_timer_get_time();
        if (now - last_heartbeat >= 10000000) {
            last_heartbeat = now;
            ESP_LOGI(TAG, "ALIVE uptime=%lld s free_internal=%u free_psram=%u keyboard=%d stack=%u",
                     now / 1000000, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), keyboard,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
            char msg[64]; snprintf(msg, sizeof(msg), "UPTIME %lld SECONDS", now / 1000000);
            line(546, msg, FG); changed = true;
        }
        if (changed && s.display_ready) tab5_display_present();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

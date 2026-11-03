#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_DISPLAY_WIDTH  1280
#define TAB5_DISPLAY_HEIGHT 720

typedef enum {
    TAB5_ROTATION_0 = 0,
    TAB5_ROTATION_90 = 1,
    TAB5_ROTATION_180 = 2,
    TAB5_ROTATION_270 = 3,
} tab5_display_rotation_t;

struct tab5_board_status {
    esp_err_t display;
    esp_err_t touch;
    esp_err_t sd;
    esp_err_t usb_hid;
    bool      display_ready;
    bool      touch_ready;
    bool      sd_ready;
    bool      usb_hid_ready;
};

struct tab5_clock_time {
    int year;
    int month;
    int day;
    int weekday;
    int hour;
    int minute;
    int second;
    bool valid;
};

/**
 * @brief Initialise display, touch, SD and USB host.
 */
struct tab5_board_status tab5_board_init(void);

/**
 * @brief Return the touch controller I2C address detected during init.
 * 0x55 = ST7121 / ST7123 integrated touch.
 * 0x14 = GT911 (older Tab5 units).
 * 0    = not detected.
 */
uint8_t tab5_touch_address(void);
uint8_t tab5_touch_firmware_version(void);

/**
 * @brief Poll for a single touch point.
 * @return true if a touch is currently present, with screen coordinates in x/y.
 */
bool tab5_touch_poll(int *x, int *y);

/**
 * @brief Poll for up to two simultaneous touch points (for pinch-to-zoom and two-finger gestures).
 * @return Number of active touch points written (0, 1, or 2).
 */
int tab5_touch_poll_multi(int *x0, int *y0, int *x1, int *y1);

/**
 * @brief Fill the entire display with a 24-bit RGB colour.
 * This is intended only for early bring-up verification.
 */
esp_err_t tab5_display_fill_rgb(uint32_t rgb);
esp_err_t tab5_display_rect(int x, int y, int width, int height, uint32_t rgb);
esp_err_t tab5_display_present(void);
/** Flush only the native framebuffer span covering a logical landscape region. */
esp_err_t tab5_display_present_region(int x, int y, int width, int height);

/** Rotate display and touch together in 90-degree clockwise steps. */
esp_err_t tab5_display_set_rotation(tab5_display_rotation_t rotation);
tab5_display_rotation_t tab5_display_get_rotation(void);
int tab5_display_width(void);
int tab5_display_height(void);

/** Set the real GPIO22 backlight PWM, 0..100 percent. */
esp_err_t tab5_display_set_brightness(uint8_t percent);
uint8_t tab5_display_get_brightness(void);

/**
 * @brief Mount the SD card at /sdcard.
 */
esp_err_t tab5_sd_mount(void);

/**
 * @brief Unmount the SD card.
 */
esp_err_t tab5_sd_unmount(void);
esp_err_t tab5_sd_self_test(void);
bool tab5_sd_usage(uint64_t *total_bytes, uint64_t *free_bytes);

/** Read the battery-backed RX8130. Invalid RTC state is initialized once
 * from the firmware build date/time during board start-up. */
bool tab5_rtc_read(struct tab5_clock_time *out);
bool tab5_rtc_was_initialized_from_build(void);

/** Set the battery-backed RX8130 RTC date and time.
 * Returns true if the time was validated and written to the RX8130. */
bool tab5_rtc_set(const struct tab5_clock_time *time);

/** Read Tab5 battery-pack bus voltage from INA226 and return a conservative
 * linear estimate between the documented 6.0 V shutdown and 8.23 V full. */
bool tab5_battery_read(float *voltage, uint8_t *percent);
bool tab5_battery_read_status(float *voltage, uint8_t *percent, bool *charging);

/** The P4 has no radio. Wi-Fi and BLE share the C6 companion's power rail. */
esp_err_t tab5_radio_power_set(bool enabled);
bool tab5_radio_power_get(void);

/** Generate the documented PI4IO power-off pulse. */
esp_err_t tab5_power_off(void);

/**
 * @brief Return true if a USB HID keyboard is currently connected.
 */
bool tab5_keyboard_connected(void);

/**
 * Optional raw-key filter invoked before USB HID translation. Return true to
 * consume a press locally (for example terminal zoom or scrollback commands),
 * or false to keep the normal ASCII/ANSI byte path. The callback runs on the
 * HID task and must not block.
 */
typedef bool (*tab5_keyboard_key_filter_t)(uint8_t hid_usage,
                                           uint8_t modifiers,
                                           void *context);
void tab5_keyboard_set_key_filter(tab5_keyboard_key_filter_t filter,
                                  void *context);

/**
 * @brief Poll a USB HID keyboard.  If a key press is available, copy the
 * UTF-8/ASCII and ANSI key sequences into out. US layout, press events only;
 * Shift, Caps Lock, Ctrl, Alt, arrows and navigation supported. No key repeat.
 * Returns 0 if none. Boot-protocol HID keyboards supported.
 */
size_t tab5_keyboard_poll(uint8_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif

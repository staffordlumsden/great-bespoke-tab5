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
 * @brief Fill the entire display with a 24-bit RGB colour.
 * This is intended only for early bring-up verification.
 */
esp_err_t tab5_display_fill_rgb(uint32_t rgb);
esp_err_t tab5_display_rect(int x, int y, int width, int height, uint32_t rgb);
esp_err_t tab5_display_present(void);

/**
 * @brief Mount the SD card at /sdcard.
 */
esp_err_t tab5_sd_mount(void);

/**
 * @brief Unmount the SD card.
 */
esp_err_t tab5_sd_unmount(void);
esp_err_t tab5_sd_self_test(void);

/**
 * @brief Return true if a USB HID keyboard is currently connected.
 */
bool tab5_keyboard_connected(void);

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

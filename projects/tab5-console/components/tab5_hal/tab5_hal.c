#include "tab5_hal.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/sdmmc_host.h"
#include "esp_cache.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7121.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

// Register and reset sequence reference: M5Stack M5Tab5-UserDemo, commit
// b4e356bc491ca070d54004718dad789c07d5fc93. LCD_RST=P4, TP_RST=P5.
#define LCD_RST (1U << 4)
#define TP_RST  (1U << 5)
#define WLAN_EN (1U << 0)
#define USB_EN  (1U << 3)
#define POWEROFF_PULSE (1U << 4)
#define CHARGE_EN (1U << 7)
#define NATIVE_W 720
#define NATIVE_H 1280
static const char *TAG = "TAB5_HAL";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t exp1, exp2, touch, rtc, ina226;
static uint8_t touch_addr, fw_version;
static esp_lcd_panel_handle_t panel;
static esp_lcd_dsi_bus_handle_t dsi;
static esp_lcd_panel_io_handle_t dbi;
static esp_ldo_channel_handle_t dsi_power;
static uint16_t *framebuffer;
static uint16_t *scanout;
static SemaphoreHandle_t frame_done;
static StaticSemaphore_t frame_done_storage;
static bool display_can_reveal, first_frame;
static sd_pwr_ctrl_handle_t sd_power;
static sdmmc_card_t *sd_card;
static bool sd_mounted, initialized;
static bool rtc_ready, battery_ready, rtc_seeded, radio_power;
static uint8_t brightness = 70;
static tab5_display_rotation_t display_rotation = TAB5_ROTATION_0;
static struct tab5_board_status status;
esp_err_t tab5_keyboard_init(void);

static esp_err_t add_device(uint8_t addr, i2c_master_dev_handle_t *dev)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr, .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &cfg, dev);
}

// PI4IOE5V6408: direction=0x03 (1=output), output=0x05,
// high impedance=0x07, pull enable=0x0B, pull select=0x0D.
static esp_err_t update_reg(i2c_master_dev_handle_t dev, uint8_t reg,
                            uint8_t mask, uint8_t value)
{
    uint8_t old;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(dev, &reg, 1, &old, 1, 100), TAG, "read expander");
    uint8_t data[] = {reg, (old & ~mask) | (value & mask)};
    return i2c_master_transmit(dev, data, sizeof(data), 100);
}

static esp_err_t read_touch(uint16_t reg, uint8_t *data, size_t count)
{
    uint8_t addr[] = {reg >> 8, reg & 0xff};
    return i2c_master_transmit_receive(touch, addr, sizeof(addr), data, count, 100);
}

static esp_err_t read_reg8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(dev, &reg, 1, value, 1, 100);
}

static esp_err_t write_reg8(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(dev, data, sizeof(data), 100);
}

static uint8_t from_bcd(uint8_t value) { return (value >> 4) * 10 + (value & 0x0f); }
static uint8_t to_bcd(unsigned value) { return ((value / 10) << 4) | (value % 10); }

static int compile_month(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    for (int i = 0; i < 12; ++i) if (!memcmp(__DATE__, months + i * 3, 3)) return i + 1;
    return 1;
}

static int weekday_for_date(int year, int month, int day)
{
    static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) --year;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
}

static bool rtc_fields_valid(const uint8_t data[7], uint8_t flags)
{
    if (flags & (1U << 1)) return false; // VLF: supply voltage dropped below retention limit.
    int second = from_bcd(data[0] & 0x7f), minute = from_bcd(data[1] & 0x7f);
    int hour = from_bcd(data[2] & 0x3f), day = from_bcd(data[4] & 0x3f);
    int month = from_bcd(data[5] & 0x1f), year = from_bcd(data[6]);
    return second < 60 && minute < 60 && hour < 24 && day >= 1 && day <= 31 &&
           month >= 1 && month <= 12 && year <= 99;
}

static esp_err_t rtc_write_build_time(void)
{
    int year, day, hour, minute, second;
    char month_name[4];
    if (sscanf(__DATE__, "%3s %d %d", month_name, &day, &year) != 3 ||
        sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) return ESP_FAIL;
    int month = compile_month();
    uint8_t ctrl;
    ESP_RETURN_ON_ERROR(read_reg8(rtc, 0x1e, &ctrl), TAG, "RTC control read");
    ESP_RETURN_ON_ERROR(write_reg8(rtc, 0x1e, ctrl | (1U << 6)), TAG, "RTC stop");
    uint8_t data[] = {0x10, to_bcd(second), to_bcd(minute), to_bcd(hour),
                      to_bcd(weekday_for_date(year, month, day)), to_bcd(day),
                      to_bcd(month), to_bcd(year % 100)};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(rtc, data, sizeof(data), 100), TAG, "RTC set build time");
    ESP_RETURN_ON_ERROR(write_reg8(rtc, 0x1e, ctrl & ~(1U << 6)), TAG, "RTC start");
    uint8_t flags;
    if (read_reg8(rtc, 0x1d, &flags) == ESP_OK) write_reg8(rtc, 0x1d, flags & ~(1U << 1));
    rtc_seeded = true;
    ESP_LOGW(TAG, "RTC was invalid; initialized once from firmware build time %s %s", __DATE__, __TIME__);
    return ESP_OK;
}

static esp_err_t init_rtc_and_battery(void)
{
    if (i2c_master_probe(bus, 0x32, 100) == ESP_OK && add_device(0x32, &rtc) == ESP_OK) {
        rtc_ready = true;
        uint8_t ctrl1;
        if (read_reg8(rtc, 0x1f, &ctrl1) == ESP_OK) write_reg8(rtc, 0x1f, ctrl1 | (1U << 4) | (1U << 5));
        uint8_t reg = 0x10, data[7], flags = 0xff;
        if (i2c_master_transmit_receive(rtc, &reg, 1, data, sizeof(data), 100) != ESP_OK ||
            read_reg8(rtc, 0x1d, &flags) != ESP_OK || !rtc_fields_valid(data, flags)) {
            ESP_RETURN_ON_ERROR(rtc_write_build_time(), TAG, "RTC seed");
        }
    } else ESP_LOGW(TAG, "RX8130 RTC not available");

    if (i2c_master_probe(bus, 0x41, 100) == ESP_OK && add_device(0x41, &ina226) == ESP_OK) {
        // 16 averages, 1.1 ms bus/shunt conversions, continuous shunt+bus mode.
        uint8_t config[] = {0x00, 0x45, 0x27};
        battery_ready = i2c_master_transmit(ina226, config, sizeof(config), 100) == ESP_OK;
    }
    if (!battery_ready) ESP_LOGW(TAG, "INA226 battery monitor not available");
    return ESP_OK;
}

static esp_err_t init_touch(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = GPIO_NUM_31,
        .scl_io_num = GPIO_NUM_32, .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &bus), TAG, "I2C init");
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, 0x43, 100), TAG, "expander 0x43 missing");
    ESP_RETURN_ON_ERROR(add_device(0x43, &exp1), TAG, "expander 1");
    // Assert low using outputs, then release LCD_RST as INPUT PULL-UP.
    // Never drive LCD_RST high push-pull: it shares the panel's 1.8V domain.
    ESP_RETURN_ON_ERROR(update_reg(exp1, 0x0d, LCD_RST | TP_RST, LCD_RST | TP_RST), TAG, "reset pull select");
    ESP_RETURN_ON_ERROR(update_reg(exp1, 0x0b, LCD_RST | TP_RST, LCD_RST | TP_RST), TAG, "reset pull enable");
    ESP_RETURN_ON_ERROR(update_reg(exp1, 0x05, LCD_RST | TP_RST, 0), TAG, "reset latch low");
    ESP_RETURN_ON_ERROR(update_reg(exp1, 0x07, LCD_RST | TP_RST, 0), TAG, "reset output enable");
    ESP_RETURN_ON_ERROR(update_reg(exp1, 0x03, LCD_RST | TP_RST, LCD_RST | TP_RST), TAG, "assert reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(update_reg(exp1, 0x05, TP_RST, TP_RST), TAG, "release touch reset");
    ESP_RETURN_ON_ERROR(update_reg(exp1, 0x03, LCD_RST, 0), TAG, "release LCD reset as input");
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, 0x55, 100), TAG, "Sitronix touch missing");
    ESP_RETURN_ON_ERROR(add_device(0x55, &touch), TAG, "touch handle");
    touch_addr = 0x55;
    ESP_RETURN_ON_ERROR(read_touch(0x0000, &fw_version, 1), TAG, "read controller version");
    uint8_t info[5], revision[4];
    ESP_RETURN_ON_ERROR(read_touch(0x0005, info, sizeof(info)), TAG, "touch dimensions");
    ESP_RETURN_ON_ERROR(read_touch(0x000c, revision, sizeof(revision)), TAG, "touch revision");
    ESP_LOGI(TAG, "PHYSICAL CONTROLLER: address=0x55 firmware=%u (%u.%u.%u.%u), max=%ux%u points=%u",
             fw_version, revision[0], revision[1], revision[2], revision[3],
             (info[0] << 8) | info[1], (info[2] << 8) | info[3], info[4]);
    ESP_RETURN_ON_FALSE(fw_version == 1, ESP_ERR_NOT_SUPPORTED, TAG, "Expected ST7121 (firmware=1); refusing mismatched panel driver");
    ESP_RETURN_ON_FALSE(((info[0] << 8) | info[1]) == NATIVE_W &&
                       ((info[2] << 8) | info[3]) == NATIVE_H && info[4] <= 10,
                       ESP_ERR_INVALID_RESPONSE, TAG, "Unexpected touch geometry");
    ESP_LOGI(TAG, "ST7121 verified; selecting M5Stack ST7121 display driver");
    return ESP_OK;
}

static bool IRAM_ATTR frame_complete(esp_lcd_panel_handle_t lcd,
                                     esp_lcd_dpi_panel_event_data_t *event, void *context)
{
    (void)lcd; (void)event; (void)context;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(frame_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t init_display(void)
{
    // Selection follows the physical firmware read above; no address-only guess.
    ESP_RETURN_ON_FALSE(fw_version == 1 && status.touch == ESP_OK, ESP_ERR_INVALID_STATE, TAG, "Controller unverified");
    ledc_timer_config_t timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_12_BIT, .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK, .deconfigure = false};
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight PWM timer");
    ledc_channel_config_t channel = {.gpio_num = GPIO_NUM_22,
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE, .timer_sel = LEDC_TIMER_0,
        .duty = 0, .hpoint = 0};
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight PWM channel");
    esp_ldo_channel_config_t power = {.chan_id = 3, .voltage_mv = 2500};
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&power, &dsi_power), TAG, "DSI LDO");
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0, .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M,
        .lane_bit_rate_mbps = 965,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi), TAG, "DSI bus");
    esp_lcd_dbi_io_config_t io_cfg = {.virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi, &io_cfg, &dbi), TAG, "DBI IO");
    esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0, .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 70, .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,
        .video_timing = {.h_size = NATIVE_W, .v_size = NATIVE_H,
            .hsync_pulse_width = 2, .hsync_back_porch = 40, .hsync_front_porch = 40,
            .vsync_pulse_width = 20, .vsync_back_porch = 24, .vsync_front_porch = 200},
    };
    st7121_vendor_config_t vendor = {.mipi_config = {.dsi_bus = dsi, .dpi_config = &dpi}};
    esp_lcd_panel_dev_config_t panel_cfg = {.reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 24,
        .vendor_config = &vendor};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7121(dbi, &panel_cfg, &panel), TAG, "ST7121 create");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "ST7121 software reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "ST7121 init");
    frame_done = xSemaphoreCreateBinaryStatic(&frame_done_storage);
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_frame_buf_complete = frame_complete};
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(panel, &callbacks, NULL), TAG, "frame callback");
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 2, (void **)&scanout,
                                                        (void **)&framebuffer), TAG, "framebuffers");
    memset(scanout, 0, NATIVE_W * NATIVE_H * sizeof(*scanout));
    ESP_RETURN_ON_ERROR(esp_cache_msync(scanout, NATIVE_W * NATIVE_H * sizeof(*scanout),
                                      ESP_CACHE_MSYNC_FLAG_DIR_C2M), TAG, "initial scanout");
    ESP_RETURN_ON_ERROR(tab5_display_fill_rgb(0x102028), TAG, "initial fill");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "display on");
    display_can_reveal = true; // Reveal only after the application's first complete frame.
    ESP_LOGI(TAG, "Display ready: native 720x1280, HAL landscape 1280x720, RGB565");
    return ESP_OK;
}

static esp_err_t enable_usb_power(void)
{
    if (!bus) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, 0x44, 100), TAG, "USB power expander missing");
    ESP_RETURN_ON_ERROR(add_device(0x44, &exp2), TAG, "expander 2");
    // Initialize only the outputs Console owns. Keep power-off low, C6 off,
    // USB-A on and charging enabled; preserve unrelated expander bits.
    const uint8_t owned = WLAN_EN | USB_EN | POWEROFF_PULSE | CHARGE_EN;
    ESP_RETURN_ON_ERROR(update_reg(exp2, 0x05, owned, USB_EN | CHARGE_EN), TAG, "board power latches");
    ESP_RETURN_ON_ERROR(update_reg(exp2, 0x07, owned, 0), TAG, "board outputs enable");
    ESP_RETURN_ON_ERROR(update_reg(exp2, 0x03, owned, owned), TAG, "board outputs direction");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

struct tab5_board_status tab5_board_init(void)
{
    if (initialized) return status;
    initialized = true;
    gpio_set_direction(GPIO_NUM_22, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_22, 0);
    ESP_LOGI(TAG, "PSRAM available: %u bytes", (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    // Match M5Stack's factory Tab5 configuration: the high-speed C6 SDIO bus
    // uses this board-specific output drive strength.
    const gpio_num_t hosted_pins[] = {
        GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11,
        GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_15,
    };
    for (size_t i = 0; i < sizeof(hosted_pins) / sizeof(hosted_pins[0]); ++i) {
        esp_err_t drive = gpio_set_drive_capability(hosted_pins[i], GPIO_DRIVE_CAP_0);
        if (drive != ESP_OK) ESP_LOGW(TAG, "GPIO %d drive capability: %s",
                                     (int)hosted_pins[i], esp_err_to_name(drive));
    }
    status.touch = init_touch();
    status.touch_ready = status.touch == ESP_OK;
    status.display = init_display();
    status.display_ready = status.display == ESP_OK;
    status.sd = tab5_sd_mount();
    status.sd_ready = status.sd == ESP_OK;
    status.usb_hid = enable_usb_power();
    if (status.usb_hid == ESP_OK) status.usb_hid = tab5_keyboard_init();
    status.usb_hid_ready = status.usb_hid == ESP_OK;
    if (bus) init_rtc_and_battery();
    return status;
}

uint8_t tab5_touch_address(void) { return touch_addr; }
uint8_t tab5_touch_firmware_version(void) { return fw_version; }

int tab5_touch_poll_multi(int *x0, int *y0, int *x1, int *y1)
{
    if (!x0 || !y0 || !status.touch_ready) return 0;
    uint8_t info, count;
    if (read_touch(0x0010, &info, 1) != ESP_OK || !(info & 0x08)) return 0;
    if (read_touch(0x0009, &count, 1) != ESP_OK || count == 0 || count > 10) return 0;
    uint8_t reports[70];
    if (read_touch(0x0014, reports, 7 * count) != ESP_OK) return 0;
    int found = 0;
    for (size_t i = 0; i < count && found < 2; i++) {
        const uint8_t *p = &reports[i * 7];
        if (!(p[0] & 0x80)) continue;
        int nx = ((p[0] & 0x3f) << 8) | p[1];
        int ny = (p[2] << 8) | p[3];
        if (nx >= NATIVE_W || ny >= NATIVE_H) continue;
        int lx = 0, ly = 0;
        switch (display_rotation) {
        case TAB5_ROTATION_0:
            lx = NATIVE_H - 1 - ny;
            ly = nx;
            break;
        case TAB5_ROTATION_90:
            lx = NATIVE_W - 1 - nx;
            ly = NATIVE_H - 1 - ny;
            break;
        case TAB5_ROTATION_180:
            lx = ny;
            ly = NATIVE_W - 1 - nx;
            break;
        case TAB5_ROTATION_270:
            lx = nx;
            ly = ny;
            break;
        default:
            return 0;
        }
        if (found == 0) {
            *x0 = lx;
            *y0 = ly;
        } else if (x1 && y1) {
            *x1 = lx;
            *y1 = ly;
        }
        ++found;
    }
    return found;
}

bool tab5_touch_poll(int *x, int *y)
{
    return tab5_touch_poll_multi(x, y, NULL, NULL) >= 1;
}

static uint16_t rgb565(uint32_t rgb)
{
    return ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
}

int tab5_display_width(void)
{
    return (display_rotation & 1) ? NATIVE_W : NATIVE_H;
}

int tab5_display_height(void)
{
    return (display_rotation & 1) ? NATIVE_H : NATIVE_W;
}

tab5_display_rotation_t tab5_display_get_rotation(void) { return display_rotation; }

esp_err_t tab5_display_set_rotation(tab5_display_rotation_t rotation)
{
    if (rotation < TAB5_ROTATION_0 || rotation > TAB5_ROTATION_270)
        return ESP_ERR_INVALID_ARG;
    display_rotation = rotation;
    ESP_LOGI(TAG, "Display rotation=%u logical=%dx%d", (unsigned)rotation,
             tab5_display_width(), tab5_display_height());
    return ESP_OK;
}

static void logical_to_native_rect(int x, int y, int width, int height,
                                   int *native_x, int *native_y,
                                   int *native_width, int *native_height)
{
    switch (display_rotation) {
    case TAB5_ROTATION_0:
        *native_x = y;
        *native_y = NATIVE_H - x - width;
        *native_width = height;
        *native_height = width;
        break;
    case TAB5_ROTATION_90:
        *native_x = NATIVE_W - x - width;
        *native_y = NATIVE_H - y - height;
        *native_width = width;
        *native_height = height;
        break;
    case TAB5_ROTATION_180:
        *native_x = NATIVE_W - y - height;
        *native_y = x;
        *native_width = height;
        *native_height = width;
        break;
    case TAB5_ROTATION_270:
        *native_x = x;
        *native_y = y;
        *native_width = width;
        *native_height = height;
        break;
    default:
        // Keep corrupted/out-of-range state bounded to the proven landscape
        // transform.  Public setters reject invalid rotations, but this also
        // makes all outputs definite for compiler data-flow analysis.
        *native_x = y;
        *native_y = NATIVE_H - x - width;
        *native_width = height;
        *native_height = width;
        break;
    }
}

// Drawing and touch polling have one owner (the application task).
esp_err_t tab5_display_rect(int x, int y, int width, int height, uint32_t rgb)
{
    if (!framebuffer) return ESP_ERR_INVALID_STATE;
    const int logical_width = tab5_display_width();
    const int logical_height = tab5_display_height();
    if (x < 0 || y < 0 || width < 0 || height < 0 || x > logical_width ||
        y > logical_height || width > logical_width - x ||
        height > logical_height - y) return ESP_ERR_INVALID_ARG;
    int native_x, native_y, native_width, native_height;
    logical_to_native_rect(x, y, width, height, &native_x, &native_y,
                           &native_width, &native_height);
    uint16_t color = rgb565(rgb);
    for (int row = native_y; row < native_y + native_height; ++row) {
        uint16_t *pixels = &framebuffer[row * NATIVE_W + native_x];
        for (int column = 0; column < native_width; ++column) pixels[column] = color;
    }
    return ESP_OK;
}

esp_err_t tab5_display_present(void)
{
    return tab5_display_present_region(0, 0, tab5_display_width(), tab5_display_height());
}

esp_err_t tab5_display_present_region(int x, int y, int width, int height)
{
    if (!framebuffer) return ESP_ERR_INVALID_STATE;
    const int logical_width = tab5_display_width();
    const int logical_height = tab5_display_height();
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        x > logical_width || y > logical_height ||
        width > logical_width - x || height > logical_height - y) {
        return ESP_ERR_INVALID_ARG;
    }

    int native_x, native_y, native_width, native_height;
    logical_to_native_rect(x, y, width, height, &native_x, &native_y,
                           &native_width, &native_height);
    // Flush one contiguous native bounding span. Untouched pixels between row
    // edges are copied too, preserving the two-buffer dirty-region contract.
    const size_t first_pixel = (size_t)native_y * NATIVE_W + native_x;
    const size_t end_pixel = (size_t)(native_y + native_height - 1) * NATIVE_W +
                             native_x + native_width;
    while (xSemaphoreTake(frame_done, 0) == pdTRUE) {}
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel, 0, 0, NATIVE_W, NATIVE_H,
                                                 framebuffer), TAG, "queue completed frame");
    // One event may finish the previous frame. The second proves scanout has
    // switched and the former scanout buffer can safely become our draw buffer.
    for (int i = 0; i < 2; ++i) {
        if (xSemaphoreTake(frame_done, pdMS_TO_TICKS(250)) != pdTRUE) {
            tab5_display_set_brightness(0);
            framebuffer = NULL; // Do not draw into a potentially active buffer.
            return ESP_ERR_TIMEOUT;
        }
    }
    uint16_t *previous = scanout;
    scanout = framebuffer;
    framebuffer = previous;
    // Both buffers retain the same base image, preserving dirty-region drawing.
    memcpy(&framebuffer[first_pixel], &scanout[first_pixel],
           (end_pixel - first_pixel) * sizeof(*framebuffer));
    if (display_can_reveal && !first_frame) {
        first_frame = true;
        ESP_LOGI(TAG, "First application frame presented: buffers=2; revealing backlight");
        return tab5_display_set_brightness(brightness);
    }
    return ESP_OK;
}

esp_err_t tab5_display_fill_rgb(uint32_t rgb)
{
    ESP_RETURN_ON_ERROR(tab5_display_rect(0, 0, tab5_display_width(),
                                         tab5_display_height(), rgb), TAG, "fill");
    return tab5_display_present();
}

esp_err_t tab5_display_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    brightness = percent;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                      first_frame ? (4095U * percent) / 100U : 0), TAG, "brightness duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

uint8_t tab5_display_get_brightness(void) { return brightness; }

bool tab5_rtc_read(struct tab5_clock_time *out)
{
    if (!out || !rtc_ready) return false;
    uint8_t reg = 0x10, data[7], flags;
    if (i2c_master_transmit_receive(rtc, &reg, 1, data, sizeof(data), 100) != ESP_OK ||
        read_reg8(rtc, 0x1d, &flags) != ESP_OK || !rtc_fields_valid(data, flags)) return false;
    *out = (struct tab5_clock_time){
        .year = 2000 + from_bcd(data[6]), .month = from_bcd(data[5] & 0x1f),
        .day = from_bcd(data[4] & 0x3f), .weekday = from_bcd(data[3] & 0x7f),
        .hour = from_bcd(data[2] & 0x3f), .minute = from_bcd(data[1] & 0x7f),
        .second = from_bcd(data[0] & 0x7f), .valid = true};
    return true;
}

bool tab5_rtc_was_initialized_from_build(void) { return rtc_seeded; }

bool tab5_rtc_set(const struct tab5_clock_time *time)
{
    if (!time || !rtc_ready) return false;
    if (time->second >= 60 || time->minute >= 60 || time->hour >= 24 ||
        time->day < 1 || time->day > 31 || time->month < 1 || time->month > 12 ||
        time->year < 2000 || time->year > 2099) {
        return false;
    }
    uint8_t ctrl = 0;
    if (read_reg8(rtc, 0x1e, &ctrl) != ESP_OK) return false;
    if (write_reg8(rtc, 0x1e, ctrl | (1U << 6)) != ESP_OK) return false;

    int weekday = weekday_for_date(time->year, time->month, time->day);
    uint8_t data[] = {
        0x10,
        to_bcd(time->second),
        to_bcd(time->minute),
        to_bcd(time->hour),
        to_bcd(weekday),
        to_bcd(time->day),
        to_bcd(time->month),
        to_bcd(time->year % 100)
    };
    if (i2c_master_transmit(rtc, data, sizeof(data), 100) != ESP_OK) {
        write_reg8(rtc, 0x1e, ctrl & ~(1U << 6));
        return false;
    }
    if (write_reg8(rtc, 0x1e, ctrl & ~(1U << 6)) != ESP_OK) return false;
    uint8_t flags = 0;
    if (read_reg8(rtc, 0x1d, &flags) == ESP_OK) {
        write_reg8(rtc, 0x1d, flags & ~(1U << 1));
    }
    rtc_seeded = false;
    ESP_LOGI(TAG, "RTC updated: %04d-%02d-%02d %02d:%02d:%02d",
             time->year, time->month, time->day, time->hour, time->minute, time->second);
    return true;
}

bool tab5_battery_read_status(float *voltage, uint8_t *percent, bool *charging)
{
    if (!voltage || !percent || !battery_ready) return false;
    uint8_t reg = 0x02, data[2];
    if (i2c_master_transmit_receive(ina226, &reg, 1, data, sizeof(data), 100) != ESP_OK) return false;
    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    *voltage = raw * 0.00125f;
    float estimate = (*voltage - 6.0f) * 100.0f / (8.23f - 6.0f);
    if (estimate < 0) estimate = 0;
    if (estimate > 100) estimate = 100;
    *percent = (uint8_t)(estimate + 0.5f);

    if (charging) {
        uint8_t sreg = 0x01, sdata[2];
        if (i2c_master_transmit_receive(ina226, &sreg, 1, sdata, sizeof(sdata), 100) == ESP_OK) {
            int16_t shunt = (int16_t)(((uint16_t)sdata[0] << 8) | sdata[1]);
            // INA226 on Tab5: negative current (into battery) indicates charging.
            *charging = (shunt < 0);
        } else {
            *charging = false;
        }
    }
    return true;
}

bool tab5_battery_read(float *voltage, uint8_t *percent)
{
    return tab5_battery_read_status(voltage, percent, NULL);
}

esp_err_t tab5_radio_power_set(bool enabled)
{
    if (!exp2) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(update_reg(exp2, 0x05, WLAN_EN, enabled ? WLAN_EN : 0), TAG, "C6 power latch");
    radio_power = enabled;
    ESP_LOGI(TAG, "ESP32-C6 power %s (hosted Wi-Fi/BLE stack not initialized)", enabled ? "ON" : "OFF");
    return ESP_OK;
}

bool tab5_radio_power_get(void) { return radio_power; }

esp_err_t tab5_power_off(void)
{
    if (!exp2) return ESP_ERR_INVALID_STATE;
    ESP_LOGW(TAG, "Generating Tab5 power-off pulse");
    for (int i = 0; i < 3; ++i) {
        ESP_RETURN_ON_ERROR(update_reg(exp2, 0x05, POWEROFF_PULSE, POWEROFF_PULSE), TAG, "poweroff high");
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_RETURN_ON_ERROR(update_reg(exp2, 0x05, POWEROFF_PULSE, 0), TAG, "poweroff low");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}

esp_err_t tab5_sd_mount(void)
{
    if (sd_mounted) return ESP_OK;
    if (!sd_power) {
        sd_pwr_ctrl_ldo_config_t power = {.ldo_chan_id = 4};
        ESP_RETURN_ON_ERROR(sd_pwr_ctrl_new_on_chip_ldo(&power, &sd_power), TAG, "SD LDO");
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.pwr_ctrl_handle = sd_power;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = GPIO_NUM_43; slot.cmd = GPIO_NUM_44;
    slot.d0 = GPIO_NUM_39; slot.d1 = GPIO_NUM_40;
    slot.d2 = GPIO_NUM_41; slot.d3 = GPIO_NUM_42;
    esp_vfs_fat_mount_config_t mount = {.format_if_mount_failed = false, .max_files = 5};
    esp_err_t r = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &sd_card);
    sd_mounted = r == ESP_OK;
    if (sd_mounted) {
        sdmmc_card_print_info(stdout, sd_card);
        FATFS *fs; DWORD free_clusters;
        if (f_getfree("0:", &free_clusters, &fs) == FR_OK)
            ESP_LOGI(TAG, "SD mounted: filesystem=%s capacity=%llu MiB free=%llu MiB",
                     fs->fs_type == FS_EXFAT ? "exFAT" : "FAT",
                     (unsigned long long)sd_card->csd.capacity * sd_card->csd.sector_size / (1024 * 1024),
                     (unsigned long long)free_clusters * fs->csize * sd_card->csd.sector_size / (1024 * 1024));
    } else ESP_LOGE(TAG, "SD mount: %s (formatting disabled)", esp_err_to_name(r));
    return r;
}

esp_err_t tab5_sd_unmount(void)
{
    if (!sd_mounted) return ESP_OK;
    esp_err_t r = esp_vfs_fat_sdcard_unmount("/sdcard", sd_card);
    if (r == ESP_OK) { sd_card = NULL; sd_mounted = false; }
    return r;
}

esp_err_t tab5_sd_self_test(void)
{
    if (!sd_mounted) return ESP_ERR_INVALID_STATE;
    const char *path = "/sdcard/.tab5-hal-check.tmp";
    const char payload[] = "Tab5 isolated HAL SD read/write verification\n";
    char actual[sizeof(payload)] = {0};
    // Exclusive creation: never replace an existing file, even after power loss.
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) return ESP_FAIL;
    bool ok = write(fd, payload, sizeof(payload)) == sizeof(payload);
    if (fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    fd = open(path, O_RDONLY);
    if (fd < 0) ok = false;
    else {
        if (read(fd, actual, sizeof(actual)) != sizeof(actual) || memcmp(payload, actual, sizeof(payload))) ok = false;
        if (close(fd) != 0) ok = false;
    }
    if (unlink(path) != 0) ok = false;
    ESP_LOGI(TAG, "SD exclusive create/write/fsync/reopen/read/compare/delete: %s", ok ? "PASS" : "FAIL");
    return ok ? ESP_OK : ESP_FAIL;
}

bool tab5_sd_usage(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!sd_mounted || !sd_card || !total_bytes || !free_bytes) return false;
    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK || !fs) return false;
    uint64_t cluster_bytes = (uint64_t)fs->csize * sd_card->csd.sector_size;
    *total_bytes = (uint64_t)(fs->n_fatent - 2) * cluster_bytes;
    *free_bytes = (uint64_t)free_clusters * cluster_bytes;
    return true;
}

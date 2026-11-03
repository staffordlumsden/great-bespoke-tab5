#include "tab5_hal.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
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
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

// Register and reset sequence reference: M5Stack M5Tab5-UserDemo, commit
// b4e356bc491ca070d54004718dad789c07d5fc93. LCD_RST=P4, TP_RST=P5.
#define LCD_RST (1U << 4)
#define TP_RST  (1U << 5)
#define USB_EN  (1U << 3)
#define NATIVE_W 720
#define NATIVE_H 1280
static const char *TAG = "TAB5_HAL";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t exp1, exp2, touch;
static uint8_t touch_addr, fw_version;
static esp_lcd_panel_handle_t panel;
static esp_lcd_dsi_bus_handle_t dsi;
static esp_lcd_panel_io_handle_t dbi;
static esp_ldo_channel_handle_t dsi_power;
static uint16_t *framebuffer;
static sd_pwr_ctrl_handle_t sd_power;
static sdmmc_card_t *sd_card;
static bool sd_mounted, initialized;
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

static esp_err_t init_display(void)
{
    // Selection follows the physical firmware read above; no address-only guess.
    ESP_RETURN_ON_FALSE(fw_version == 1 && status.touch == ESP_OK, ESP_ERR_INVALID_STATE, TAG, "Controller unverified");
    gpio_config_t light = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << GPIO_NUM_22};
    ESP_RETURN_ON_ERROR(gpio_config(&light), TAG, "backlight GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_22, 0), TAG, "backlight off");
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
        .num_fbs = 1,
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
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, (void **)&framebuffer), TAG, "framebuffer");
    ESP_RETURN_ON_ERROR(tab5_display_fill_rgb(0x102028), TAG, "initial fill");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "display on");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_22, 1), TAG, "backlight on");
    ESP_LOGI(TAG, "Display ready: native 720x1280, HAL landscape 1280x720, RGB565");
    return ESP_OK;
}

static esp_err_t enable_usb_power(void)
{
    if (!bus) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, 0x44, 100), TAG, "USB power expander missing");
    ESP_RETURN_ON_ERROR(add_device(0x44, &exp2), TAG, "expander 2");
    // Change only USB5V_EN (P3); retain WLAN, charging and shutdown outputs.
    ESP_RETURN_ON_ERROR(update_reg(exp2, 0x05, USB_EN, USB_EN), TAG, "USB5V latch");
    ESP_RETURN_ON_ERROR(update_reg(exp2, 0x07, USB_EN, 0), TAG, "USB5V output enable");
    ESP_RETURN_ON_ERROR(update_reg(exp2, 0x03, USB_EN, USB_EN), TAG, "USB5V direction");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

struct tab5_board_status tab5_board_init(void)
{
    if (initialized) return status;
    initialized = true;
    ESP_LOGI(TAG, "PSRAM available: %u bytes", (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    status.touch = init_touch();
    status.touch_ready = status.touch == ESP_OK;
    status.display = init_display();
    status.display_ready = status.display == ESP_OK;
    status.sd = tab5_sd_mount();
    status.sd_ready = status.sd == ESP_OK;
    status.usb_hid = enable_usb_power();
    if (status.usb_hid == ESP_OK) status.usb_hid = tab5_keyboard_init();
    status.usb_hid_ready = status.usb_hid == ESP_OK;
    return status;
}

uint8_t tab5_touch_address(void) { return touch_addr; }
uint8_t tab5_touch_firmware_version(void) { return fw_version; }

bool tab5_touch_poll(int *x, int *y)
{
    if (!x || !y || !status.touch_ready) return false;
    uint8_t info, count;
    if (read_touch(0x0010, &info, 1) != ESP_OK || !(info & 0x08)) return false;
    if (read_touch(0x0009, &count, 1) != ESP_OK || count == 0 || count > 10) return false;
    uint8_t reports[70];
    if (read_touch(0x0014, reports, 7 * count) != ESP_OK) return false;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *p = &reports[i * 7];
        if (!(p[0] & 0x80)) continue;
        int nx = ((p[0] & 0x3f) << 8) | p[1];
        int ny = (p[2] << 8) | p[3];
        if (nx >= NATIVE_W || ny >= NATIVE_H) continue;
        *x = NATIVE_H - 1 - ny;
        *y = nx;
        return true;
    }
    return false;
}

static uint16_t rgb565(uint32_t rgb)
{
    return ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
}

// Drawing and touch polling have one owner (the application task).
esp_err_t tab5_display_rect(int x, int y, int width, int height, uint32_t rgb)
{
    if (!framebuffer) return ESP_ERR_INVALID_STATE;
    if (x < 0 || y < 0 || width < 0 || height < 0 || x > TAB5_DISPLAY_WIDTH ||
        y > TAB5_DISPLAY_HEIGHT || width > TAB5_DISPLAY_WIDTH - x ||
        height > TAB5_DISPLAY_HEIGHT - y) return ESP_ERR_INVALID_ARG;
    uint16_t color = rgb565(rgb);
    for (int lx = x; lx < x + width; lx++) {
        uint16_t *row = &framebuffer[(NATIVE_H - 1 - lx) * NATIVE_W + y];
        for (int ly = 0; ly < height; ly++) row[ly] = color;
    }
    return ESP_OK;
}

esp_err_t tab5_display_present(void)
{
    if (!framebuffer) return ESP_ERR_INVALID_STATE;
    return esp_cache_msync(framebuffer, NATIVE_W * NATIVE_H * sizeof(*framebuffer),
                           ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

esp_err_t tab5_display_fill_rgb(uint32_t rgb)
{
    ESP_RETURN_ON_ERROR(tab5_display_rect(0, 0, TAB5_DISPLAY_WIDTH, TAB5_DISPLAY_HEIGHT, rgb), TAG, "fill");
    return tab5_display_present();
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

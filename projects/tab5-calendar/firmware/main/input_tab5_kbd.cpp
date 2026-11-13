// Tab5 clip-on QWERTY keyboard (ExtPort1, I2C 0x6D) — standalone driver.
#include "input_tab5_kbd.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "hid_translate.hpp"

namespace tab5 {

namespace {

constexpr const char* kTag = "tab5_kbd";

constexpr uint8_t kRegIntCfg        = 0x00;
constexpr uint8_t kRegIntStat       = 0x01;
constexpr uint8_t kRegEventNum      = 0x02;
constexpr uint8_t kRegModeKeyboard  = 0x10;
constexpr uint8_t kRegHidEvent      = 0x30;
constexpr uint8_t kRegFirmwareVer   = 0xFE;

constexpr uint8_t kModeHid          = 0x01;
constexpr uint8_t kIntEnHid         = 0x02;
constexpr uint8_t kHidEventEmpty    = 0xFF;

ByteSink                 g_sink;
TaskHandle_t             g_task = nullptr;
i2c_master_bus_handle_t  g_bus  = nullptr;
i2c_master_dev_handle_t  g_dev  = nullptr;
SemaphoreHandle_t        g_int_sem = nullptr;
int                      g_int_gpio = -1;
std::atomic<bool>        g_isr_installed{false};
std::atomic<bool>        g_connected{false};

constexpr int64_t kRepeatInitialUs = 400 * 1000;
constexpr int64_t kRepeatRateUs    =  60 * 1000;

struct HeldKey {
    bool    active        = false;
    uint8_t modifier      = 0;
    uint8_t keycode       = 0;
    int64_t press_time_us = 0;
    int64_t next_emit_us  = 0;
};
HeldKey g_held;

void emit_hid(uint8_t modifier, uint8_t keycode) {
    if (!g_sink) return;
    uint8_t buf[hid_translate::kMaxBytes];
    size_t n = hid_translate::emit_key(keycode, modifier, buf);
    if (n) g_sink(buf, n);
}

esp_err_t reg_read(uint8_t reg, uint8_t* buf, size_t len) {
    return i2c_master_transmit_receive(g_dev, &reg, 1, buf, len, 50);
}

esp_err_t reg_write(uint8_t reg, uint8_t value) {
    const uint8_t tx[2] = { reg, value };
    return i2c_master_transmit(g_dev, tx, 2, 50);
}

void IRAM_ATTR isr_handler(void* /*arg*/) {
    BaseType_t hp_woken = pdFALSE;
    xSemaphoreGiveFromISR(g_int_sem, &hp_woken);
    portYIELD_FROM_ISR(hp_woken);
}

[[noreturn]] void reader_task(void* /*arg*/) {
    while (true) {
        TickType_t wait;
        if (g_held.active) {
            int64_t now = esp_timer_get_time();
            int64_t remaining = g_held.next_emit_us - now;
            wait = (remaining <= 0)
                       ? (TickType_t)0
                       : (TickType_t)(pdMS_TO_TICKS(remaining / 1000) + 1);
        } else {
            wait = pdMS_TO_TICKS(g_int_sem ? 1000 : 100);
        }

        if (g_int_sem) {
            xSemaphoreTake(g_int_sem, wait);
        } else {
            vTaskDelay(wait ? wait : 1);
        }

        bool got_event = false;
        for (int safety = 32; safety > 0; --safety) {
            uint8_t count = 0;
            if (reg_read(kRegEventNum, &count, 1) != ESP_OK) break;
            if (count == 0) break;

            uint8_t ev[2] = { 0, 0 };
            if (reg_read(kRegHidEvent, ev, 2) != ESP_OK) break;
            const uint8_t modifier = ev[0];
            const uint8_t keycode  = ev[1];
            if (keycode == kHidEventEmpty && modifier == kHidEventEmpty) break;
            got_event = true;

            if (keycode == 0) {
                g_held.active = false;
                continue;
            }

            const int64_t now = esp_timer_get_time();
            g_held = {
                true,
                modifier,
                keycode,
                now,
                now + kRepeatInitialUs,
            };
            emit_hid(modifier, keycode);
        }

        if (g_held.active) {
            int64_t now = esp_timer_get_time();
            for (int i = 0; i < 4 && now >= g_held.next_emit_us; ++i) {
                emit_hid(g_held.modifier, g_held.keycode);
                g_held.next_emit_us += kRepeatRateUs;
                now = esp_timer_get_time();
            }
        }

        if (got_event) (void)reg_write(kRegIntStat, 0x00);
    }
}

}  // namespace

bool start_tab5_kbd_input(ByteSink sink, const Tab5KbdConfig& cfg) {
    if (g_task) {
        ESP_LOGW(kTag, "already started");
        return true;
    }
    g_sink = std::move(sink);
    g_int_gpio = cfg.int_gpio;

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_1;
    bus_cfg.sda_io_num = static_cast<gpio_num_t>(cfg.sda_gpio);
    bus_cfg.scl_io_num = static_cast<gpio_num_t>(cfg.scl_gpio);
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &g_bus);
    if (err != ESP_OK || !g_bus) {
        ESP_LOGE(kTag, "i2c_new_master_bus -> %s", esp_err_to_name(err));
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address  = 0x6D;
    dev_cfg.scl_speed_hz    = cfg.i2c_hz;
    err = i2c_master_bus_add_device(g_bus, &dev_cfg, &g_dev);
    if (err != ESP_OK || !g_dev) {
        ESP_LOGE(kTag, "bus_add_device -> %s", esp_err_to_name(err));
        i2c_del_master_bus(g_bus);
        g_bus = nullptr;
        return false;
    }

    uint8_t fw = 0;
    err = reg_read(kRegFirmwareVer, &fw, 1);
    if (err != ESP_OK) {
        ESP_LOGI(kTag, "Tab5 Keyboard not detected on I2C (%s)", esp_err_to_name(err));
        i2c_master_bus_rm_device(g_dev);
        i2c_del_master_bus(g_bus);
        g_dev = nullptr; g_bus = nullptr;
        return false;
    }

    (void)reg_write(kRegEventNum, 0);
    (void)reg_write(kRegIntStat, 0);
    err = reg_write(kRegModeKeyboard, kModeHid);
    if (err == ESP_OK) err = reg_write(kRegIntCfg, kIntEnHid);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "configure HID mode -> %s (continuing)", esp_err_to_name(err));
    }

    if (g_int_gpio >= 0) {
        g_int_sem = xSemaphoreCreateBinary();
        gpio_config_t gc = {};
        gc.intr_type    = GPIO_INTR_NEGEDGE;
        gc.mode         = GPIO_MODE_INPUT;
        gc.pin_bit_mask = 1ULL << g_int_gpio;
        gc.pull_up_en   = GPIO_PULLUP_ENABLE;
        esp_err_t gpio_result = gpio_config(&gc);

        esp_err_t r = g_int_sem && gpio_result == ESP_OK
                          ? gpio_install_isr_service(0)
                          : gpio_result;
        if (g_int_sem && (r == ESP_OK || r == ESP_ERR_INVALID_STATE)) {
            r = gpio_isr_handler_add(static_cast<gpio_num_t>(g_int_gpio),
                                     &isr_handler, nullptr);
            if (r == ESP_OK) g_isr_installed = true;
        } else {
            r = r == ESP_OK ? ESP_ERR_NO_MEM : r;
        }
        if (r != ESP_OK) {
            ESP_LOGW(kTag, "keyboard INT setup -> %s; polling fallback", esp_err_to_name(r));
            if (g_int_sem) vSemaphoreDelete(g_int_sem);
            g_int_sem = nullptr;
        }
    }

    ESP_LOGI(kTag, "Tab5 Keyboard ready (fw=0x%02x sda=%d scl=%d int=%d %s)",
             fw, cfg.sda_gpio, cfg.scl_gpio, cfg.int_gpio,
             g_int_sem ? "INT" : "polling");

    BaseType_t ok = xTaskCreate(reader_task, "tab5_kbd",
                                cfg.task_stack, nullptr,
                                tskIDLE_PRIORITY + 3, &g_task);
    if (ok != pdPASS) {
        ESP_LOGE(kTag, "task create failed");
        g_task = nullptr;
        if (g_isr_installed.exchange(false))
            gpio_isr_handler_remove(static_cast<gpio_num_t>(g_int_gpio));
        if (g_int_sem) {
            vSemaphoreDelete(g_int_sem);
            g_int_sem = nullptr;
        }
        i2c_master_bus_rm_device(g_dev);
        i2c_del_master_bus(g_bus);
        g_dev = nullptr;
        g_bus = nullptr;
        return false;
    }
    g_connected.store(true);
    return true;
}

bool tab5_kbd_connected() { return g_connected.load(); }

}  // namespace tab5

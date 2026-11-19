#include "tab5_gpio_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr int8_t kUserPins[] = {
    2, 3, 4, 5,      // M-Bus 21, 19, 20, 11
    6, 7,            // Port C TXD / RXD
    16, 18, 19,      // M-Bus 2, 7, 9
    17, 52,          // Port B p1 / p2
    45, 47, 48, 51,  // M-Bus 8, 23, 22, 26
    53, 54,          // Port A SDA / SCL (Grove)
};

// ESP32-P4 ADC1 covers 16-23, ADC2 covers 49-54.
constexpr int8_t kAdcPins[] = {16, 17, 18, 19, 51, 52, 53, 54};

bool inList(const int8_t* list, size_t count, int pin) {
    for (size_t i = 0; i < count; ++i) {
        if (list[i] == pin) return true;
    }
    return false;
}

bool pinToAdc(int pin, adc_unit_t& unit, adc_channel_t& ch) {
    switch (pin) {
        case 16: unit = ADC_UNIT_1; ch = ADC_CHANNEL_0; return true;
        case 17: unit = ADC_UNIT_1; ch = ADC_CHANNEL_1; return true;
        case 18: unit = ADC_UNIT_1; ch = ADC_CHANNEL_2; return true;
        case 19: unit = ADC_UNIT_1; ch = ADC_CHANNEL_3; return true;
        case 51: unit = ADC_UNIT_2; ch = ADC_CHANNEL_2; return true;
        case 52: unit = ADC_UNIT_2; ch = ADC_CHANNEL_3; return true;
        case 53: unit = ADC_UNIT_2; ch = ADC_CHANNEL_4; return true;
        case 54: unit = ADC_UNIT_2; ch = ADC_CHANNEL_5; return true;
        default: return false;
    }
}

adc_oneshot_unit_handle_t s_adc1 = nullptr;
adc_oneshot_unit_handle_t s_adc2 = nullptr;

adc_oneshot_unit_handle_t ensureAdcUnit(adc_unit_t unit) {
    adc_oneshot_unit_handle_t* slot = (unit == ADC_UNIT_1) ? &s_adc1 : &s_adc2;
    if (!*slot) {
        adc_oneshot_unit_init_cfg_t cfg{};
        cfg.unit_id = unit;
        cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
        if (adc_oneshot_new_unit(&cfg, slot) != ESP_OK) {
            return nullptr;
        }
    }
    return *slot;
}

struct PwmSlot {
    int8_t pin{-1};
    int freq{0};
    bool timerInited{false};
};
constexpr size_t kMaxPwmChannels = 8;
PwmSlot s_pwmSlots[kMaxPwmChannels];

int findPwmChannel(int pin) {
    for (size_t i = 0; i < kMaxPwmChannels; ++i) {
        if (s_pwmSlots[i].pin == pin) return static_cast<int>(i);
    }
    return -1;
}

int allocPwmChannel(int pin) {
    int existing = findPwmChannel(pin);
    if (existing >= 0) return existing;
    for (size_t i = 0; i < kMaxPwmChannels; ++i) {
        if (s_pwmSlots[i].pin < 0) return static_cast<int>(i);
    }
    return -1;
}

struct SoftI2cState {
    int sda{-1};
    int scl{-1};
    uint32_t halfPeriodUs{5};
    bool ready{false};
};
SoftI2cState s_i2c;

void i2cDelay() {
    esp_rom_delay_us(s_i2c.halfPeriodUs);
}

void sdaHigh() {
    gpio_set_direction(static_cast<gpio_num_t>(s_i2c.sda), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(s_i2c.sda), GPIO_PULLUP_ONLY);
}

void sdaLow() {
    gpio_set_level(static_cast<gpio_num_t>(s_i2c.sda), 0);
    gpio_set_direction(static_cast<gpio_num_t>(s_i2c.sda), GPIO_MODE_OUTPUT);
}

void sclHigh() {
    gpio_set_direction(static_cast<gpio_num_t>(s_i2c.scl), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(s_i2c.scl), GPIO_PULLUP_ONLY);
}

void sclLow() {
    gpio_set_level(static_cast<gpio_num_t>(s_i2c.scl), 0);
    gpio_set_direction(static_cast<gpio_num_t>(s_i2c.scl), GPIO_MODE_OUTPUT);
}

void i2cStart() {
    sdaHigh();
    sclHigh();
    i2cDelay();
    sdaLow();
    i2cDelay();
    sclLow();
}

void i2cStop() {
    sdaLow();
    i2cDelay();
    sclHigh();
    i2cDelay();
    sdaHigh();
    i2cDelay();
}

bool i2cWriteByte(uint8_t byte) {
    for (int bit = 7; bit >= 0; --bit) {
        if ((byte >> bit) & 1u) sdaHigh();
        else sdaLow();
        i2cDelay();
        sclHigh();
        i2cDelay();
        sclLow();
    }
    sdaHigh();
    i2cDelay();
    sclHigh();
    i2cDelay();
    bool ack = (gpio_get_level(static_cast<gpio_num_t>(s_i2c.sda)) == 0);
    sclLow();
    return ack;
}

uint8_t i2cReadByte(bool ackMore) {
    uint8_t v = 0;
    sdaHigh();
    for (int bit = 7; bit >= 0; --bit) {
        i2cDelay();
        sclHigh();
        i2cDelay();
        if (gpio_get_level(static_cast<gpio_num_t>(s_i2c.sda))) {
            v |= static_cast<uint8_t>(1u << bit);
        }
        sclLow();
    }
    if (ackMore) sdaLow();
    else sdaHigh();
    i2cDelay();
    sclHigh();
    i2cDelay();
    sclLow();
    sdaHigh();
    return v;
}

spi_device_handle_t s_spiDev = nullptr;
bool s_spiBusInited = false;

bool s_uartStarted[3] = {false, false, false};

bool uartNumFor(int port, uart_port_t& out) {
    if (port == 1) { out = UART_NUM_1; return true; }
    if (port == 2) { out = UART_NUM_2; return true; }
    return false;
}

}  // namespace

extern "C" int tab5_gpio_pin_allowed(int pin) {
    return inList(kUserPins, sizeof(kUserPins) / sizeof(kUserPins[0]), pin) ? 1 : 0;
}

extern "C" int tab5_gpio_pin_list(char* out, int out_len) {
    if (!out || out_len <= 0) return TAB5_GPIO_ERR_RANGE;
    int written = 0;
    for (size_t i = 0; i < sizeof(kUserPins) / sizeof(kUserPins[0]); ++i) {
        int n = snprintf(out + written, static_cast<size_t>(out_len - written), "%s%d",
                         written ? "," : "", static_cast<int>(kUserPins[i]));
        if (n < 0 || written + n >= out_len) break;
        written += n;
    }
    out[written] = '\0';
    return written;
}

extern "C" int tab5_gpio_pin_mode(int pin, int mode) {
    if (!tab5_gpio_pin_allowed(pin)) return TAB5_GPIO_ERR_PIN;
    gpio_config_t cfg{};
    cfg.pin_bit_mask = (1ULL << pin);
    switch (mode) {
        case TAB5_PIN_INPUT:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case TAB5_PIN_OUTPUT:
            cfg.mode = GPIO_MODE_INPUT_OUTPUT;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case TAB5_PIN_INPUT_PULLUP:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_ENABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            break;
        case TAB5_PIN_INPUT_PULLDOWN:
            cfg.mode = GPIO_MODE_INPUT;
            cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
            break;
        default:
            return TAB5_GPIO_ERR_RANGE;
    }
    return gpio_config(&cfg) == ESP_OK ? TAB5_GPIO_OK : TAB5_GPIO_ERR_IO;
}

extern "C" int tab5_gpio_write(int pin, int value) {
    if (!tab5_gpio_pin_allowed(pin)) return TAB5_GPIO_ERR_PIN;
    return gpio_set_level(static_cast<gpio_num_t>(pin), value ? 1 : 0) == ESP_OK ? TAB5_GPIO_OK : TAB5_GPIO_ERR_IO;
}

extern "C" int tab5_gpio_read(int pin) {
    if (!tab5_gpio_pin_allowed(pin)) return TAB5_GPIO_ERR_PIN;
    return gpio_get_level(static_cast<gpio_num_t>(pin)) ? 1 : 0;
}

extern "C" int tab5_gpio_adc_read(int pin) {
    adc_unit_t unit{};
    adc_channel_t ch{};
    if (!tab5_gpio_pin_allowed(pin) || !inList(kAdcPins, sizeof(kAdcPins) / sizeof(kAdcPins[0]), pin) ||
        !pinToAdc(pin, unit, ch)) {
        return TAB5_GPIO_ERR_PIN;
    }
    auto handle = ensureAdcUnit(unit);
    if (!handle) return TAB5_GPIO_ERR_IO;
    adc_oneshot_chan_cfg_t cfg{};
    cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    cfg.atten = ADC_ATTEN_DB_12;
    if (adc_oneshot_config_channel(handle, ch, &cfg) != ESP_OK) return TAB5_GPIO_ERR_IO;
    int raw = 0;
    if (adc_oneshot_read(handle, ch, &raw) != ESP_OK) return TAB5_GPIO_ERR_IO;
    return raw;
}

extern "C" int tab5_gpio_adc_mv(int pin) {
    int raw = tab5_gpio_adc_read(pin);
    if (raw < 0) return raw;
    // Standard 12-bit approximation when no curve-fitting calibration is active (3300 mV full scale).
    return (raw * 3300) / 4095;
}

extern "C" int tab5_gpio_pwm_write(int pin, int duty, int freq) {
    if (!tab5_gpio_pin_allowed(pin)) return TAB5_GPIO_ERR_PIN;
    if (duty < 0 || duty > 255 || freq <= 0 || freq > 40000000) return TAB5_GPIO_ERR_RANGE;
    int slot = allocPwmChannel(pin);
    if (slot < 0) return TAB5_GPIO_ERR_BUSY;

    auto timer = static_cast<ledc_timer_t>(slot % 4);
    auto ch = static_cast<ledc_channel_t>(slot);
    if (s_pwmSlots[slot].pin != pin || s_pwmSlots[slot].freq != freq) {
        ledc_timer_config_t tcfg{};
        tcfg.speed_mode = LEDC_LOW_SPEED_MODE;
        tcfg.timer_num = timer;
        tcfg.duty_resolution = LEDC_TIMER_8_BIT;
        tcfg.freq_hz = static_cast<uint32_t>(freq);
        tcfg.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&tcfg) != ESP_OK) return TAB5_GPIO_ERR_IO;

        ledc_channel_config_t ccfg{};
        ccfg.speed_mode = LEDC_LOW_SPEED_MODE;
        ccfg.channel = ch;
        ccfg.timer_sel = timer;
        ccfg.intr_type = LEDC_INTR_DISABLE;
        ccfg.gpio_num = pin;
        ccfg.duty = static_cast<uint32_t>(duty);
        ccfg.hpoint = 0;
        if (ledc_channel_config(&ccfg) != ESP_OK) return TAB5_GPIO_ERR_IO;
        s_pwmSlots[slot].pin = static_cast<int8_t>(pin);
        s_pwmSlots[slot].freq = freq;
        s_pwmSlots[slot].timerInited = true;
    } else {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, static_cast<uint32_t>(duty));
        ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
    }
    return TAB5_GPIO_OK;
}

extern "C" int tab5_gpio_pwm_release(int pin) {
    if (!tab5_gpio_pin_allowed(pin)) return TAB5_GPIO_ERR_PIN;
    int slot = findPwmChannel(pin);
    if (slot >= 0) {
        ledc_stop(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(slot), 0);
        s_pwmSlots[slot].pin = -1;
        s_pwmSlots[slot].freq = 0;
    }
    gpio_reset_pin(static_cast<gpio_num_t>(pin));
    return TAB5_GPIO_OK;
}

extern "C" int tab5_i2c_init(int sda, int scl, int freq) {
    if (!tab5_gpio_pin_allowed(sda) || !tab5_gpio_pin_allowed(scl)) return TAB5_GPIO_ERR_PIN;
    if (sda == scl || freq < 1000 || freq > 400000) return TAB5_GPIO_ERR_RANGE;
    s_i2c.sda = sda;
    s_i2c.scl = scl;
    s_i2c.halfPeriodUs = std::max<uint32_t>(1, 500000u / static_cast<uint32_t>(freq));
    s_i2c.ready = true;
    sdaHigh();
    sclHigh();
    return TAB5_GPIO_OK;
}

extern "C" int tab5_i2c_deinit(void) {
    if (s_i2c.ready) {
        gpio_reset_pin(static_cast<gpio_num_t>(s_i2c.sda));
        gpio_reset_pin(static_cast<gpio_num_t>(s_i2c.scl));
    }
    s_i2c.ready = false;
    return TAB5_GPIO_OK;
}

extern "C" int tab5_i2c_scan(uint8_t* out, int max_len) {
    if (!s_i2c.ready) return TAB5_GPIO_ERR_STATE;
    if (!out || max_len <= 0) return TAB5_GPIO_ERR_RANGE;
    int count = 0;
    for (int addr = 0x08; addr <= 0x77 && count < max_len; ++addr) {
        i2cStart();
        bool ack = i2cWriteByte(static_cast<uint8_t>(addr << 1));
        i2cStop();
        if (ack) out[count++] = static_cast<uint8_t>(addr);
    }
    return count;
}

extern "C" int tab5_i2c_write(int addr, const uint8_t* data, int len, int stop) {
    if (!s_i2c.ready) return TAB5_GPIO_ERR_STATE;
    if (addr < 0 || addr > 0x7F || len < 0 || (len && !data)) return TAB5_GPIO_ERR_RANGE;
    i2cStart();
    if (!i2cWriteByte(static_cast<uint8_t>(addr << 1))) {
        i2cStop();
        return TAB5_GPIO_ERR_IO;
    }
    for (int i = 0; i < len; ++i) {
        if (!i2cWriteByte(data[i])) {
            i2cStop();
            return TAB5_GPIO_ERR_IO;
        }
    }
    if (stop) i2cStop();
    return len;
}

extern "C" int tab5_i2c_read(int addr, uint8_t* out, int len) {
    if (!s_i2c.ready) return TAB5_GPIO_ERR_STATE;
    if (addr < 0 || addr > 0x7F || len < 0 || (len && !out)) return TAB5_GPIO_ERR_RANGE;
    i2cStart();
    if (!i2cWriteByte(static_cast<uint8_t>((addr << 1) | 1))) {
        i2cStop();
        return TAB5_GPIO_ERR_IO;
    }
    for (int i = 0; i < len; ++i) {
        out[i] = i2cReadByte(i + 1 < len);
    }
    i2cStop();
    return len;
}

extern "C" int tab5_i2c_write_read(int addr, const uint8_t* tx, int tx_len, uint8_t* rx, int rx_len) {
    int rc = tab5_i2c_write(addr, tx, tx_len, 0);
    if (rc < 0) return rc;
    return tab5_i2c_read(addr, rx, rx_len);
}

extern "C" int tab5_spi_init(int sck, int miso, int mosi, int freq, int mode) {
    if (!tab5_gpio_pin_allowed(sck) || !tab5_gpio_pin_allowed(mosi)) return TAB5_GPIO_ERR_PIN;
    if (miso >= 0 && !tab5_gpio_pin_allowed(miso)) return TAB5_GPIO_ERR_PIN;
    if (freq <= 0 || freq > 40000000 || mode < 0 || mode > 3) return TAB5_GPIO_ERR_RANGE;

    tab5_spi_deinit();
    spi_bus_config_t bus{};
    bus.sclk_io_num = sck;
    bus.mosi_io_num = mosi;
    bus.miso_io_num = miso;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return TAB5_GPIO_ERR_BUSY;
    }
    s_spiBusInited = true;

    spi_device_interface_config_t dev{};
    dev.clock_speed_hz = freq;
    dev.mode = static_cast<uint8_t>(mode);
    dev.spics_io_num = -1;
    dev.queue_size = 1;
    if (spi_bus_add_device(SPI3_HOST, &dev, &s_spiDev) != ESP_OK) {
        tab5_spi_deinit();
        return TAB5_GPIO_ERR_BUSY;
    }
    return TAB5_GPIO_OK;
}

extern "C" int tab5_spi_deinit(void) {
    if (s_spiDev) {
        spi_bus_remove_device(s_spiDev);
        s_spiDev = nullptr;
    }
    if (s_spiBusInited) {
        spi_bus_free(SPI3_HOST);
        s_spiBusInited = false;
    }
    return TAB5_GPIO_OK;
}

extern "C" int tab5_spi_transfer(int cs, const uint8_t* tx, uint8_t* rx, int len) {
    if (!s_spiDev) return TAB5_GPIO_ERR_STATE;
    if (len < 0 || (len && !tx)) return TAB5_GPIO_ERR_RANGE;
    if (cs >= 0) {
        if (!tab5_gpio_pin_allowed(cs)) return TAB5_GPIO_ERR_PIN;
        gpio_set_direction(static_cast<gpio_num_t>(cs), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(cs), 0);
    }
    spi_transaction_t t{};
    t.length = static_cast<size_t>(len) * 8u;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    esp_err_t err = spi_device_transmit(s_spiDev, &t);
    if (cs >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(cs), 1);
    }
    return err == ESP_OK ? len : TAB5_GPIO_ERR_IO;
}

extern "C" int tab5_uart_init(int port, int baud, int rx_pin, int tx_pin) {
    uart_port_t u{};
    if (!uartNumFor(port, u)) return TAB5_GPIO_ERR_RANGE;
    if (baud < 300 || baud > 5000000) return TAB5_GPIO_ERR_RANGE;
    if (rx_pin >= 0 && !tab5_gpio_pin_allowed(rx_pin)) return TAB5_GPIO_ERR_PIN;
    if (tx_pin >= 0 && !tab5_gpio_pin_allowed(tx_pin)) return TAB5_GPIO_ERR_PIN;

    tab5_uart_deinit(port);
    uart_config_t cfg{};
    cfg.baud_rate = baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(u, 1024, 0, 0, nullptr, 0) != ESP_OK) return TAB5_GPIO_ERR_BUSY;
    uart_param_config(u, &cfg);
    uart_set_pin(u, tx_pin >= 0 ? tx_pin : UART_PIN_NO_CHANGE,
                 rx_pin >= 0 ? rx_pin : UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    s_uartStarted[port] = true;
    return TAB5_GPIO_OK;
}

extern "C" int tab5_uart_deinit(int port) {
    uart_port_t u{};
    if (!uartNumFor(port, u)) return TAB5_GPIO_ERR_RANGE;
    if (s_uartStarted[port]) {
        uart_driver_delete(u);
        s_uartStarted[port] = false;
    }
    return TAB5_GPIO_OK;
}

extern "C" int tab5_uart_write(int port, const uint8_t* data, int len) {
    uart_port_t u{};
    if (!uartNumFor(port, u)) return TAB5_GPIO_ERR_RANGE;
    if (!s_uartStarted[port]) return TAB5_GPIO_ERR_STATE;
    if (len < 0 || (len && !data)) return TAB5_GPIO_ERR_RANGE;
    return uart_write_bytes(u, data, static_cast<size_t>(len));
}

extern "C" int tab5_uart_read(int port, uint8_t* out, int len, int timeout_ms) {
    uart_port_t u{};
    if (!uartNumFor(port, u)) return TAB5_GPIO_ERR_RANGE;
    if (!s_uartStarted[port]) return TAB5_GPIO_ERR_STATE;
    if (len < 0 || (len && !out) || timeout_ms < 0) return TAB5_GPIO_ERR_RANGE;
    return uart_read_bytes(u, out, static_cast<uint32_t>(len), pdMS_TO_TICKS(timeout_ms));
}

extern "C" int tab5_uart_any(int port) {
    uart_port_t u{};
    if (!uartNumFor(port, u)) return TAB5_GPIO_ERR_RANGE;
    if (!s_uartStarted[port]) return TAB5_GPIO_ERR_STATE;
    size_t buffered = 0;
    uart_get_buffered_data_len(u, &buffered);
    return static_cast<int>(buffered);
}

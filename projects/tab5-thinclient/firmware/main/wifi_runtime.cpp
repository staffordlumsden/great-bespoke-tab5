#include "wifi_runtime.hpp"

#include "esp_err.h"
#include "esp_hosted_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tab5_hal.h"
#include "tab5_weather_wifi.h"

bool WifiRuntime::init() {
    if (initialized_) return true;
    // AGENTS.md hardware rule: power C6 radio and wait before calling Wi-Fi APIs.
    esp_err_t err = tab5_radio_power_set(true);
    if (err != ESP_OK) {
        message_ = String("Radio power failed: ") + static_cast<int>(err);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(250));

    err = tab5_weather_wifi_init();
    if (err != ESP_OK) {
        message_ = String("Wi-Fi init failed: ") + static_cast<int>(err);
        return false;
    }

    esp_hosted_coprocessor_fwver_t c6{};
    if (esp_hosted_get_coprocessor_fwversion(&c6) == ESP_OK) {
        if (!(c6.major1 == 1 && c6.minor1 == 4)) {
            message_ = String("C6 FW ") + static_cast<unsigned int>(c6.major1) + "." +
                       static_cast<unsigned int>(c6.minor1) + " unsupported";
            return false;
        }
    }

    initialized_ = true;
    poll();
    return true;
}

bool WifiRuntime::start() {
    if (!init()) return false;
    if (started_) return true;
    esp_err_t err = tab5_weather_wifi_start();
    if (err != ESP_OK) {
        message_ = String("Wi-Fi start failed: ") + static_cast<int>(err);
        return false;
    }
    started_ = true;
    poll();
    return true;
}

bool WifiRuntime::connect(const String& ssid, const String& password) {
    if (!start()) return false;
    esp_err_t err = tab5_weather_wifi_connect(ssid.c_str(), password.c_str());
    if (err != ESP_OK) {
        message_ = String("Wi-Fi connect failed: ") + static_cast<int>(err);
        return false;
    }
    poll();
    return true;
}

bool WifiRuntime::disconnect() {
    if (!initialized_) return true;
    esp_err_t err = tab5_weather_wifi_disconnect();
    poll();
    return err == ESP_OK;
}

bool WifiRuntime::retry() {
    if (!start()) return false;
    esp_err_t err = tab5_weather_wifi_retry();
    poll();
    return err == ESP_OK;
}

bool WifiRuntime::scan() {
    if (!start()) return false;
    esp_err_t err = tab5_weather_wifi_scan();
    if (err != ESP_OK) {
        message_ = String("Wi-Fi scan failed: ") + static_cast<int>(err);
        return false;
    }
    poll();
    return true;
}

void WifiRuntime::poll() {
    if (!initialized_) return;
    tab5_weather_wifi_status_t st{};
    tab5_weather_wifi_get_status(&st);
    connected_ = st.connected;
    hasIp_ = st.has_ip;
    scanning_ = st.scanning;
    hasSaved_ = st.has_saved_network;
    rssi_ = st.rssi;
    ssid_ = st.ssid;
    ip_ = st.ip_address;
    message_ = st.message[0] ? String(st.message) : (hasIp_ ? String("Connected") : String("Waiting"));

    if (st.revision != lastRevision_) {
        lastRevision_ = st.revision;
        scanResults_.clear();
        for (size_t i = 0; i < st.network_count; ++i) {
            tab5_weather_wifi_network_t net{};
            if (tab5_weather_wifi_get_network(i, &net) && net.ssid[0]) {
                WifiScanEntry e;
                e.ssid = net.ssid;
                e.rssi = net.rssi;
                e.secure = net.secure;
                scanResults_.push_back(std::move(e));
            }
        }
    }
}

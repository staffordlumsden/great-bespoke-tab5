#include "tab5_radio.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "tab5_bluetooth.h"

static const char *TAG = "TAB5_RADIO";

static SemaphoreHandle_t s_lock;
static tab5_radio_status_t s_status;
static tab5_wifi_network_t s_networks[TAB5_RADIO_MAX_WIFI_NETWORKS];
static wifi_ap_record_t s_scan_records[TAB5_RADIO_MAX_WIFI_NETWORKS];

static esp_netif_t *s_sta_netif;
static bool s_initialized;
static bool s_wifi_started;
static bool s_auto_connect_on_start;
static void lock_state(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock_state(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

static void changed_locked(void)
{
    ++s_status.revision;
}

static void copy_ascii(char *dst, size_t capacity, const uint8_t *src, size_t length)
{
    if (!dst || capacity == 0) return;
    size_t used = 0;
    for (size_t i = 0; i < length && used + 1 < capacity; ++i) {
        uint8_t c = src[i];
        if (c == 0) break;
        dst[used++] = (c >= 32 && c <= 126) ? (char)c : '?';
    }
    dst[used] = '\0';
}

static void set_message(char *dst, size_t capacity, const char *message)
{
    snprintf(dst, capacity, "%s", message ? message : "");
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_SCAN_DONE) {
        uint16_t count = TAB5_RADIO_MAX_WIFI_NETWORKS;
        memset(s_scan_records, 0, sizeof(s_scan_records));
        esp_err_t err = esp_wifi_scan_get_ap_records(&count, s_scan_records);
        lock_state();
        s_status.wifi_scanning = false;
        s_status.wifi_count = 0;
        if (err == ESP_OK) {
            for (uint16_t i = 0; i < count; ++i) {
                if (!s_scan_records[i].ssid[0]) continue;
                char ssid[TAB5_RADIO_SSID_LEN] = {0};
                copy_ascii(ssid, sizeof(ssid), s_scan_records[i].ssid,
                           strnlen((char *)s_scan_records[i].ssid, 32));
                size_t existing = s_status.wifi_count;
                for (size_t j = 0; j < existing; ++j) {
                    if (strcmp(s_networks[j].ssid, ssid) == 0) {
                        if (s_scan_records[i].rssi > s_networks[j].rssi) {
                            s_networks[j].rssi = s_scan_records[i].rssi;
                            s_networks[j].authmode = s_scan_records[i].authmode;
                            s_networks[j].secure = s_scan_records[i].authmode != WIFI_AUTH_OPEN;
                        }
                        existing = SIZE_MAX;
                        break;
                    }
                }
                if (existing == SIZE_MAX || s_status.wifi_count >= TAB5_RADIO_MAX_WIFI_NETWORKS) continue;
                tab5_wifi_network_t *entry = &s_networks[s_status.wifi_count++];
                snprintf(entry->ssid, sizeof(entry->ssid), "%s", ssid);
                entry->rssi = s_scan_records[i].rssi;
                entry->authmode = s_scan_records[i].authmode;
                entry->secure = s_scan_records[i].authmode != WIFI_AUTH_OPEN;
            }
            for (size_t i = 0; i < s_status.wifi_count; ++i) {
                for (size_t j = i + 1; j < s_status.wifi_count; ++j) {
                    if (s_networks[j].rssi > s_networks[i].rssi) {
                        tab5_wifi_network_t swap = s_networks[i];
                        s_networks[i] = s_networks[j];
                        s_networks[j] = swap;
                    }
                }
            }
            snprintf(s_status.wifi_message, sizeof(s_status.wifi_message), "SCAN COMPLETE - %u NETWORKS",
                     (unsigned)s_status.wifi_count);
        } else {
            set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "SCAN RESULTS UNAVAILABLE");
        }
        changed_locked();
        unlock_state();
        esp_wifi_clear_ap_list();
    } else if (id == WIFI_EVENT_STA_START) {
        bool auto_connect = s_auto_connect_on_start;
        s_auto_connect_on_start = false;
        wifi_config_t saved = {0};
        bool has_saved = esp_wifi_get_config(WIFI_IF_STA, &saved) == ESP_OK && saved.sta.ssid[0];
        lock_state();
        if (auto_connect && has_saved) {
            copy_ascii(s_status.wifi_ssid, sizeof(s_status.wifi_ssid), saved.sta.ssid,
                       strnlen((char *)saved.sta.ssid, sizeof(saved.sta.ssid)));
            set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "AUTO-CONNECTING SAVED WIFI");
        } else {
            set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "WIFI READY - SCAN FOR NETWORKS");
        }
        changed_locked();
        unlock_state();
        if (auto_connect && has_saved) {
            ESP_LOGI(TAG, "Auto-connecting saved Wi-Fi network");
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)data;
        lock_state();
        s_status.wifi_connected = true;
        s_status.wifi_has_ip = false;
        s_status.ip_address[0] = '\0';
        copy_ascii(s_status.wifi_ssid, sizeof(s_status.wifi_ssid), event->ssid, event->ssid_len);
        set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "CONNECTED - OBTAINING IP");
        changed_locked();
        unlock_state();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)data;
        lock_state();
        s_status.wifi_connected = false;
        s_status.wifi_has_ip = false;
        s_status.wifi_rssi = 0;
        s_status.ip_address[0] = '\0';
        snprintf(s_status.wifi_message, sizeof(s_status.wifi_message), "DISCONNECTED - REASON %u",
                 (unsigned)event->reason);
        changed_locked();
        unlock_state();
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        lock_state();
        s_status.wifi_has_ip = true;
        s_status.wifi_connected = true;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_status.ip_address, sizeof(s_status.ip_address));
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_status.wifi_rssi = ap.rssi;
        set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "WIFI CONNECTED");
        changed_locked();
        unlock_state();
        ESP_LOGI(TAG, "Wi-Fi connected, IP %s", s_status.ip_address);
    }
}

esp_err_t tab5_radio_init(void)
{
    if (s_initialized) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(&s_status, 0, sizeof(s_status));
    set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "WIFI OFF");
    set_message(s_status.bt_message, sizeof(s_status.bt_message), "BLUETOOTH OFF");

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "NVS init: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL));

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;

    err = tab5_bluetooth_init();
    if (err != ESP_OK) return err;

    s_initialized = true;
    lock_state();
    s_status.initialized = true;
    changed_locked();
    unlock_state();
    ESP_LOGI(TAG, "Hosted Wi-Fi and Bluedroid HID host initialised");
    return ESP_OK;
}

esp_err_t tab5_radio_set_wifi_enabled(bool enabled)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    if (enabled) {
        if (!s_wifi_started) {
            s_auto_connect_on_start = true;
            err = esp_wifi_start();
            if (err == ESP_OK) s_wifi_started = true;
            else s_auto_connect_on_start = false;
        }
    } else {
        if (s_status.wifi_scanning) esp_wifi_scan_stop();
        if (s_status.wifi_connected) esp_wifi_disconnect();
        if (s_wifi_started) {
            err = esp_wifi_stop();
            if (err == ESP_OK) s_wifi_started = false;
        }
        s_auto_connect_on_start = false;
    }
    lock_state();
    s_status.wifi_enabled = enabled;
    s_status.wifi_scanning = false;
    if (!enabled) {
        s_status.wifi_connected = false;
        s_status.wifi_has_ip = false;
        s_status.ip_address[0] = '\0';
        set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "WIFI OFF");
    } else {
        set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "WIFI STARTING");
    }
    changed_locked();
    unlock_state();
    return err;
}

void tab5_radio_get_status(tab5_radio_status_t *out)
{
    if (!out) return;
    lock_state();
    *out = s_status;
    unlock_state();
    tab5_bluetooth_get_status(out);
}

esp_err_t tab5_radio_wifi_scan(void)
{
    if (!s_initialized || !s_status.wifi_enabled) return ESP_ERR_INVALID_STATE;
    lock_state();
    s_status.wifi_scanning = true;
    set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "SCANNING WIFI...");
    changed_locked();
    unlock_state();
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        lock_state();
        s_status.wifi_scanning = false;
        set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "SCAN COULD NOT START");
        changed_locked();
        unlock_state();
    }
    return err;
}

bool tab5_radio_get_wifi_network(size_t index, tab5_wifi_network_t *out)
{
    if (!out) return false;
    lock_state();
    bool found = index < s_status.wifi_count;
    if (found) *out = s_networks[index];
    unlock_state();
    return found;
}

esp_err_t tab5_radio_wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    esp_err_t err = tab5_radio_set_wifi_enabled(true);
    if (err != ESP_OK) return err;
    if (s_status.wifi_scanning) esp_wifi_scan_stop();
    wifi_config_t config = {0};
    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s", ssid);
    snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s", password ? password : "");
    config.sta.threshold.authmode = config.sta.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) return err;
    esp_wifi_disconnect();
    err = esp_wifi_connect();
    lock_state();
    s_status.wifi_connected = false;
    s_status.wifi_has_ip = false;
    s_status.ip_address[0] = '\0';
    snprintf(s_status.wifi_ssid, sizeof(s_status.wifi_ssid), "%s", ssid);
    set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "CONNECTING WIFI...");
    changed_locked();
    unlock_state();
    return err;
}

esp_err_t tab5_radio_wifi_disconnect(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    s_auto_connect_on_start = false;
    esp_err_t err = esp_wifi_disconnect();
    lock_state();
    s_status.wifi_connected = false;
    s_status.wifi_has_ip = false;
    s_status.ip_address[0] = '\0';
    set_message(s_status.wifi_message, sizeof(s_status.wifi_message), "WIFI DISCONNECTED");
    changed_locked();
    unlock_state();
    return err;
}

bool tab5_radio_get_saved_wifi(char *ssid, size_t ssid_size, char *password, size_t pass_size)
{
    if (!s_initialized || !s_wifi_started) return false;
    wifi_config_t saved = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &saved) != ESP_OK || !saved.sta.ssid[0]) return false;
    if (ssid && ssid_size) {
        snprintf(ssid, ssid_size, "%s", (char *)saved.sta.ssid);
    }
    if (password && pass_size) {
        snprintf(password, pass_size, "%s", (char *)saved.sta.password);
    }
    return true;
}


#include "tab5_weather_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static tab5_weather_wifi_status_t s_status;
static tab5_weather_wifi_network_t s_networks[TAB5_WEATHER_WIFI_MAX_NETWORKS];
static wifi_ap_record_t s_scan_records[TAB5_WEATHER_WIFI_MAX_NETWORKS];
static bool s_initialised;
static bool s_started;

static void lock_state(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock_state(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

static void changed_locked(const char *message)
{
    if (message) snprintf(s_status.message, sizeof(s_status.message), "%s", message);
    ++s_status.revision;
}

static void copy_ascii(char *dst, size_t capacity, const uint8_t *src, size_t length)
{
    if (!dst || capacity == 0) return;
    size_t used = 0;
    for (size_t i = 0; i < length && used + 1 < capacity; ++i) {
        const uint8_t value = src[i];
        if (value == 0) break;
        dst[used++] = value >= 32 && value <= 126 ? (char)value : '?';
    }
    dst[used] = '\0';
}

static void clear_bytes(void *data, size_t length)
{
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    while (length--) *bytes++ = 0;
}

static bool saved_network_exists(char *ssid, size_t capacity)
{
    wifi_config_t saved = {0};
    const bool found = esp_wifi_get_config(WIFI_IF_STA, &saved) == ESP_OK && saved.sta.ssid[0];
    if (found && ssid)
        copy_ascii(ssid, capacity, saved.sta.ssid,
                   strnlen((const char *)saved.sta.ssid, sizeof(saved.sta.ssid)));
    clear_bytes(&saved, sizeof(saved));
    return found;
}

static void scan_done(void)
{
    uint16_t count = TAB5_WEATHER_WIFI_MAX_NETWORKS;
    memset(s_scan_records, 0, sizeof(s_scan_records));
    const esp_err_t result = esp_wifi_scan_get_ap_records(&count, s_scan_records);

    lock_state();
    s_status.scanning = false;
    s_status.network_count = 0;
    memset(s_networks, 0, sizeof(s_networks));
    if (result == ESP_OK) {
        for (uint16_t i = 0; i < count; ++i) {
            if (!s_scan_records[i].ssid[0]) continue;
            char ssid[TAB5_WEATHER_WIFI_SSID_LEN] = {0};
            copy_ascii(ssid, sizeof(ssid), s_scan_records[i].ssid,
                       strnlen((const char *)s_scan_records[i].ssid, 32));
            size_t existing = s_status.network_count;
            for (size_t j = 0; j < s_status.network_count; ++j) {
                if (strcmp(s_networks[j].ssid, ssid) != 0) continue;
                if (s_scan_records[i].rssi > s_networks[j].rssi) {
                    s_networks[j].rssi = s_scan_records[i].rssi;
                    s_networks[j].authmode = s_scan_records[i].authmode;
                    s_networks[j].secure = s_scan_records[i].authmode != WIFI_AUTH_OPEN;
                }
                existing = SIZE_MAX;
                break;
            }
            if (existing == SIZE_MAX ||
                s_status.network_count >= TAB5_WEATHER_WIFI_MAX_NETWORKS)
                continue;
            tab5_weather_wifi_network_t *entry = &s_networks[s_status.network_count++];
            snprintf(entry->ssid, sizeof(entry->ssid), "%s", ssid);
            entry->rssi = s_scan_records[i].rssi;
            entry->authmode = s_scan_records[i].authmode;
            entry->secure = s_scan_records[i].authmode != WIFI_AUTH_OPEN;
        }
        for (size_t i = 0; i < s_status.network_count; ++i) {
            for (size_t j = i + 1; j < s_status.network_count; ++j) {
                if (s_networks[j].rssi <= s_networks[i].rssi) continue;
                const tab5_weather_wifi_network_t swap = s_networks[i];
                s_networks[i] = s_networks[j];
                s_networks[j] = swap;
            }
        }
        char message[64];
        snprintf(message, sizeof(message), "SCAN COMPLETE - %u NETWORKS",
                 (unsigned)s_status.network_count);
        changed_locked(message);
    } else {
        changed_locked("SCAN RESULTS UNAVAILABLE");
    }
    unlock_state();
    esp_wifi_clear_ap_list();
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_SCAN_DONE) {
        scan_done();
    } else if (id == WIFI_EVENT_STA_START) {
        char ssid[TAB5_WEATHER_WIFI_SSID_LEN] = {0};
        const bool has_saved = saved_network_exists(ssid, sizeof(ssid));
        lock_state();
        s_status.started = true;
        s_status.has_saved_network = has_saved;
        if (has_saved) {
            snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", ssid);
            changed_locked("AUTO-CONNECTING SAVED WIFI");
        } else {
            changed_locked("WIFI READY - TAP SCAN");
        }
        unlock_state();
        if (has_saved) esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *event = (const wifi_event_sta_connected_t *)data;
        lock_state();
        s_status.connected = true;
        s_status.has_ip = false;
        s_status.ip_address[0] = '\0';
        copy_ascii(s_status.ssid, sizeof(s_status.ssid), event->ssid, event->ssid_len);
        changed_locked("CONNECTED - OBTAINING IP");
        unlock_state();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        lock_state();
        s_status.connected = false;
        s_status.has_ip = false;
        s_status.rssi = 0;
        s_status.ip_address[0] = '\0';
        changed_locked("WIFI DISCONNECTED");
        unlock_state();
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_STA_GOT_IP) return;
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
    wifi_ap_record_t access_point = {0};
    lock_state();
    s_status.connected = true;
    s_status.has_ip = true;
    esp_ip4addr_ntoa(&event->ip_info.ip, s_status.ip_address, sizeof(s_status.ip_address));
    if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) s_status.rssi = access_point.rssi;
    changed_locked("WIFI CONNECTED");
    unlock_state();
}

esp_err_t tab5_weather_wifi_init(void)
{
    if (s_initialised) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(&s_status, 0, sizeof(s_status));
    memset(s_networks, 0, sizeof(s_networks));

    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (result != ESP_OK) return result;
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL);
    if (result != ESP_OK) return result;

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&config);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (result != ESP_OK) return result;
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) return result;

    s_initialised = true;
    lock_state();
    s_status.initialised = true;
    s_status.has_saved_network = saved_network_exists(NULL, 0);
    changed_locked(s_status.has_saved_network ? "SAVED WIFI READY" : "WIFI READY - TAP SCAN");
    unlock_state();
    return ESP_OK;
}

esp_err_t tab5_weather_wifi_start(void)
{
    if (!s_initialised) return ESP_ERR_INVALID_STATE;
    if (s_started) return ESP_OK;
    const esp_err_t result = esp_wifi_start();
    if (result == ESP_OK) s_started = true;
    return result;
}

esp_err_t tab5_weather_wifi_scan(void)
{
    if (!s_initialised || !s_started) return ESP_ERR_INVALID_STATE;
    lock_state();
    if (s_status.scanning) {
        unlock_state();
        return ESP_ERR_INVALID_STATE;
    }
    s_status.scanning = true;
    changed_locked("SCANNING WIFI...");
    unlock_state();
    const esp_err_t result = esp_wifi_scan_start(NULL, false);
    if (result != ESP_OK) {
        lock_state();
        s_status.scanning = false;
        changed_locked("SCAN COULD NOT START");
        unlock_state();
    }
    return result;
}

bool tab5_weather_wifi_get_network(size_t index, tab5_weather_wifi_network_t *out)
{
    if (!out) return false;
    lock_state();
    const bool found = index < s_status.network_count;
    if (found) *out = s_networks[index];
    unlock_state();
    return found;
}

esp_err_t tab5_weather_wifi_connect(const char *ssid, const char *password)
{
    if (!s_initialised || !s_started || !ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    const size_t ssid_length = strnlen(ssid, TAB5_WEATHER_WIFI_SSID_LEN);
    const size_t password_length = password ? strnlen(password, 65) : 0;
    if (ssid_length > 32 || password_length > 64) return ESP_ERR_INVALID_ARG;
    if (password_length == 64) {
        for (size_t i = 0; i < password_length; ++i) {
            const char c = password[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F')))
                return ESP_ERR_INVALID_ARG;
        }
    }

    lock_state();
    const bool scanning = s_status.scanning;
    unlock_state();
    if (scanning) esp_wifi_scan_stop();

    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, ssid_length);
    if (password_length) memcpy(config.sta.password, password, password_length);
    config.sta.threshold.authmode = password_length ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &config);
    clear_bytes(&config, sizeof(config));
    if (result != ESP_OK) return result;
    (void)esp_wifi_disconnect();
    result = esp_wifi_connect();

    lock_state();
    s_status.scanning = false;
    s_status.has_saved_network = true;
    s_status.connected = false;
    s_status.has_ip = false;
    s_status.ip_address[0] = '\0';
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", ssid);
    changed_locked(result == ESP_OK ? "CONNECTING WIFI..." : "WIFI CONNECT FAILED");
    unlock_state();
    return result;
}

esp_err_t tab5_weather_wifi_disconnect(void)
{
    if (!s_initialised || !s_started) return ESP_ERR_INVALID_STATE;
    const esp_err_t result = esp_wifi_disconnect();
    lock_state();
    s_status.connected = false;
    s_status.has_ip = false;
    s_status.rssi = 0;
    s_status.ip_address[0] = '\0';
    changed_locked("WIFI DISCONNECTED");
    unlock_state();
    return result;
}

esp_err_t tab5_weather_wifi_retry(void)
{
    if (!s_initialised || !s_started) return ESP_ERR_INVALID_STATE;
    char ssid[TAB5_WEATHER_WIFI_SSID_LEN] = {0};
    if (!saved_network_exists(ssid, sizeof(ssid))) return ESP_ERR_NOT_FOUND;
    lock_state();
    s_status.has_saved_network = true;
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", ssid);
    changed_locked("RETRYING WIFI...");
    unlock_state();
    (void)esp_wifi_disconnect();
    return esp_wifi_connect();
}

void tab5_weather_wifi_get_status(tab5_weather_wifi_status_t *out)
{
    if (!out) return;
    lock_state();
    *out = s_status;
    unlock_state();
}

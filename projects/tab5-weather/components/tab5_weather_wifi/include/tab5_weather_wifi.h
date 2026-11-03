#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_WEATHER_WIFI_MAX_NETWORKS 16
#define TAB5_WEATHER_WIFI_SSID_LEN 33

typedef struct {
    char ssid[TAB5_WEATHER_WIFI_SSID_LEN];
    int8_t rssi;
    uint8_t authmode;
    bool secure;
} tab5_weather_wifi_network_t;

typedef struct {
    uint32_t revision;
    bool initialised;
    bool started;
    bool scanning;
    bool has_saved_network;
    bool connected;
    bool has_ip;
    int8_t rssi;
    size_t network_count;
    char ssid[TAB5_WEATHER_WIFI_SSID_LEN];
    char ip_address[16];
    char message[64];
} tab5_weather_wifi_status_t;

/** Initialise the hosted station interface. Does not erase NVS. */
esp_err_t tab5_weather_wifi_init(void);

/** Start station mode and reconnect the last saved network when one exists. */
esp_err_t tab5_weather_wifi_start(void);

/** Start an asynchronous scan. Results are bounded, deduplicated and RSSI-sorted. */
esp_err_t tab5_weather_wifi_scan(void);

bool tab5_weather_wifi_get_network(size_t index, tab5_weather_wifi_network_t *out);

/** Save and join a station configuration shared with the resident Console. */
esp_err_t tab5_weather_wifi_connect(const char *ssid, const char *password);

/** Disconnect without erasing the saved station configuration. */
esp_err_t tab5_weather_wifi_disconnect(void);

/** Retry the most recently saved station configuration. */
esp_err_t tab5_weather_wifi_retry(void);

void tab5_weather_wifi_get_status(tab5_weather_wifi_status_t *out);

#ifdef __cplusplus
}
#endif

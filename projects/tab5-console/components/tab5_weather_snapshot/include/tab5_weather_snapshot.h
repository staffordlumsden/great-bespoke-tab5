/* Last-known Weather data shared across app reboots. No network or clock code. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_WEATHER_SNAPSHOT_WIRE_SIZE 96
typedef struct {
    int32_t temperature_tenths_c;
    bool has_temperature;
    bool fahrenheit;
    char city[41];
    char conditions[32];
} tab5_weather_snapshot_t;

bool tab5_weather_snapshot_encode(const tab5_weather_snapshot_t *snapshot,
                                  uint8_t out[TAB5_WEATHER_SNAPSHOT_WIRE_SIZE]);
bool tab5_weather_snapshot_decode(const uint8_t *data, size_t size,
                                  tab5_weather_snapshot_t *snapshot);
void tab5_weather_snapshot_temperature(const tab5_weather_snapshot_t *snapshot,
                                      char *out, size_t size);

/* NVS must already be initialised. Console calls load only, once at boot.
 * Weather stores accepted forecast data; failures must leave the old key alone.
 * Identical snapshots do not cause another commit. No NVS erase is performed. */
esp_err_t tab5_weather_snapshot_load(tab5_weather_snapshot_t *snapshot);
esp_err_t tab5_weather_snapshot_store(const tab5_weather_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

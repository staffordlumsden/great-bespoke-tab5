#include "tab5_weather_snapshot.h"
#include "nvs.h"
#include <string.h>

esp_err_t tab5_weather_snapshot_load(tab5_weather_snapshot_t *snapshot)
{
    if (!snapshot) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("wx_summary", NVS_READONLY, &handle);
    if (result != ESP_OK) return result;
    uint8_t data[TAB5_WEATHER_SNAPSHOT_WIRE_SIZE];
    size_t size = sizeof(data);
    result = nvs_get_blob(handle, "last_v1", data, &size);
    nvs_close(handle);
    if (result != ESP_OK) return result;
    return tab5_weather_snapshot_decode(data, size, snapshot) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t tab5_weather_snapshot_store(const tab5_weather_snapshot_t *snapshot)
{
    uint8_t data[TAB5_WEATHER_SNAPSHOT_WIRE_SIZE];
    if (!tab5_weather_snapshot_encode(snapshot, data)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("wx_summary", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    uint8_t previous[TAB5_WEATHER_SNAPSHOT_WIRE_SIZE];
    size_t size = sizeof(previous);
    if (nvs_get_blob(handle, "last_v1", previous, &size) == ESP_OK &&
        size == sizeof(data) && !memcmp(previous, data, sizeof(data))) result = ESP_OK;
    else {
        result = nvs_set_blob(handle, "last_v1", data, sizeof(data));
        if (result == ESP_OK) result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

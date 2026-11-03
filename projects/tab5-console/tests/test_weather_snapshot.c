#include "tab5_weather_snapshot.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t persisted[96], pending[96];
static size_t persisted_size;
static int writes, commits, last_mode;
static bool fail_write, fail_commit;

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(name, "wx_summary"));
    last_mode = mode; *handle = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *size)
{
    assert(handle == 1 && !strcmp(key, "last_v1"));
    if (!persisted_size) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size >= persisted_size);
    *size = persisted_size; memcpy(out, persisted, *size);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t size)
{
    assert(handle == 1 && !strcmp(key, "last_v1") && last_mode == NVS_READWRITE && size == 96);
    ++writes;
    if (fail_write) return ESP_FAIL;
    memcpy(pending, data, size); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1 && last_mode == NVS_READWRITE); ++commits;
    if (fail_commit) return ESP_FAIL;
    memcpy(persisted, pending, 96); persisted_size = 96; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1); }

int main(void)
{
    tab5_weather_snapshot_t value = {0}, out = {0};
    value.temperature_tenths_c = 213; value.has_temperature = true;
    strcpy(value.city, "Fixture City"); strcpy(value.conditions, "Partly cloudy");
    assert(tab5_weather_snapshot_load(&out) == ESP_ERR_NVS_NOT_FOUND && !writes && !commits);
    assert(tab5_weather_snapshot_store(&value) == ESP_OK && writes == 1 && commits == 1);
    assert(tab5_weather_snapshot_load(&out) == ESP_OK && last_mode == NVS_READONLY);
    assert(!strcmp(out.city, value.city) && !strcmp(out.conditions, value.conditions));
    assert(out.temperature_tenths_c == 213 && out.has_temperature && !out.fahrenheit);
    assert(tab5_weather_snapshot_store(&value) == ESP_OK && writes == 1 && commits == 1);
    char temperature[24];
    tab5_weather_snapshot_temperature(&out, temperature, sizeof(temperature));
    assert(!strcmp(temperature, "21.3 C"));
    value.fahrenheit = true;
    assert(tab5_weather_snapshot_store(&value) == ESP_OK && commits == 2);
    assert(tab5_weather_snapshot_load(&out) == ESP_OK);
    tab5_weather_snapshot_temperature(&out, temperature, sizeof(temperature));
    assert(!strcmp(temperature, "70.3 F"));

    uint8_t wire[96];
    assert(tab5_weather_snapshot_encode(&value, wire));
    for (unsigned i = 0; i < sizeof(wire); ++i) {
        wire[i] ^= 1;
        assert(!tab5_weather_snapshot_decode(wire, sizeof(wire), &out));
        wire[i] ^= 1;
    }
    assert(!tab5_weather_snapshot_decode(wire, 95, &out));
    assert(!tab5_weather_snapshot_decode(wire, 97, &out));
    assert(!tab5_weather_snapshot_decode(NULL, 96, &out));
    assert(!tab5_weather_snapshot_decode(wire, 96, NULL));
    assert(!tab5_weather_snapshot_encode(NULL, wire));
    value.temperature_tenths_c = -1000;
    assert(tab5_weather_snapshot_encode(&value, wire));
    assert(tab5_weather_snapshot_decode(wire, 96, &out) && out.temperature_tenths_c == -1000);
    value.temperature_tenths_c = 701;
    assert(!tab5_weather_snapshot_encode(&value, wire));
    const int writes_before = writes;
    assert(tab5_weather_snapshot_store(&value) != ESP_OK && writes == writes_before);
    assert(tab5_weather_snapshot_load(&out) == ESP_OK && out.temperature_tenths_c == 213);
    value.temperature_tenths_c = 220;
    fail_write = true;
    assert(tab5_weather_snapshot_store(&value) != ESP_OK);
    assert(tab5_weather_snapshot_load(&out) == ESP_OK && out.temperature_tenths_c == 213);
    fail_write = false; fail_commit = true;
    assert(tab5_weather_snapshot_store(&value) != ESP_OK);
    assert(tab5_weather_snapshot_load(&out) == ESP_OK && out.temperature_tenths_c == 213);
    fail_commit = false;
    value.has_temperature = false;
    assert(tab5_weather_snapshot_store(&value) == ESP_OK);
    assert(tab5_weather_snapshot_load(&out) == ESP_OK && !out.has_temperature && !out.temperature_tenths_c);
    tab5_weather_snapshot_temperature(&out, temperature, sizeof(temperature));
    assert(!strcmp(temperature, "-- F"));
    memset(value.city, 'A', sizeof(value.city));
    assert(!tab5_weather_snapshot_encode(&value, wire));
    strcpy(value.city, "Bad\nCity"); assert(!tab5_weather_snapshot_encode(&value, wire));
    strcpy(value.city, "Fixture City"); value.conditions[0] = 0;
    assert(!tab5_weather_snapshot_encode(&value, wire));
    persisted[20] ^= 1;
    assert(tab5_weather_snapshot_load(&out) == ESP_ERR_INVALID_STATE);
    puts("PASS: Weather snapshot codec, read-only consumption, retained/deduplicated NVS data, units and corruption/failure gates");
    return 0;
}

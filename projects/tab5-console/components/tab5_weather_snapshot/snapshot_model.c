#include "tab5_weather_snapshot.h"
#include <stdio.h>
#include <string.h>

static bool text_valid(const char *value, size_t size)
{
    const char *end = memchr(value, 0, size);
    if (!end || end == value) return false;
    for (const char *p = value; p < end; ++p)
        if ((unsigned char)*p < 32 || (unsigned char)*p > 126) return false;
    return true;
}

static uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return ~crc;
}

static void put32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (i * 8));
}

static uint32_t get32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

bool tab5_weather_snapshot_encode(const tab5_weather_snapshot_t *s, uint8_t out[96])
{
    if (!s || !out || !text_valid(s->city, sizeof(s->city)) ||
        !text_valid(s->conditions, sizeof(s->conditions)) ||
        (s->has_temperature && (s->temperature_tenths_c < -1000 || s->temperature_tenths_c > 700)))
        return false;
    memset(out, 0, 96);
    memcpy(out, "TWX1", 4);
    out[4] = 1;
    out[5] = (s->fahrenheit ? 1 : 0) | (s->has_temperature ? 2 : 0);
    put32(out + 8, s->has_temperature ? (uint32_t)s->temperature_tenths_c : 0);
    memcpy(out + 12, s->city, strlen(s->city));
    memcpy(out + 53, s->conditions, strlen(s->conditions));
    put32(out + 92, crc32(out, 92));
    return true;
}

bool tab5_weather_snapshot_decode(const uint8_t *data, size_t size, tab5_weather_snapshot_t *out)
{
    if (!data || !out || size != 96 || memcmp(data, "TWX1", 4) || data[4] != 1 ||
        (data[5] & ~3) || data[6] || data[7] || get32(data + 92) != crc32(data, 92))
        return false;
    for (unsigned i = 85; i < 92; ++i) if (data[i]) return false;
    tab5_weather_snapshot_t next = {0};
    next.temperature_tenths_c = (int32_t)get32(data + 8);
    next.fahrenheit = (data[5] & 1) != 0;
    next.has_temperature = (data[5] & 2) != 0;
    memcpy(next.city, data + 12, sizeof(next.city));
    memcpy(next.conditions, data + 53, sizeof(next.conditions));
    uint8_t canonical[96];
    if (!tab5_weather_snapshot_encode(&next, canonical) || memcmp(canonical, data, 96))
        return false;
    *out = next;
    return true;
}

void tab5_weather_snapshot_temperature(const tab5_weather_snapshot_t *s, char *out, size_t size)
{
    if (!out || !size) return;
    if (!s) { out[0] = 0; return; }
    const char unit = s->fahrenheit ? 'F' : 'C';
    if (!s->has_temperature) snprintf(out, size, "-- %c", unit);
    else {
        double value = s->temperature_tenths_c / 10.0;
        if (s->fahrenheit) value = value * 1.8 + 32;
        snprintf(out, size, "%.1f %c", value, unit);
    }
}

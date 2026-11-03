#include "ollama_analytics.h"
#include "cJSON.h"
#include "cyd_usage/json_guard.h"
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace oa {
namespace {
const cJSON *field(const cJSON *p, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(p, key);
}
bool count(const cJSON *p, uint64_t &out) {
    if (!cJSON_IsNumber(p) || !std::isfinite(p->valuedouble) ||
        p->valuedouble < 0 || p->valuedouble > 9007199254740991.0 ||
        std::floor(p->valuedouble) != p->valuedouble) return false;
    out = static_cast<uint64_t>(p->valuedouble);
    return true;
}
struct Json {
    cJSON *value = nullptr;
    ~Json() { cJSON_Delete(value); }
};
} // namespace

bool parse_models(const char *raw, size_t size, Models &out) {
    // Reuse the CYD parser's size/depth, duplicate-key, timestamp, scope and
    // 24-hour bucket checks before interpreting model grouping.
    ou::Usage summary;
    ou::Error error;
    if (!ou::parse_usage(raw, size, summary, error)) return false;
    Json json{cJSON_ParseWithLength(raw, size)};
    if (!json.value) return false;
    const auto *grouping = field(json.value, "group_by");
    if (!cJSON_IsString(grouping) || std::strcmp(grouping->valuestring, "model")) return false;
    const auto *total = field(json.value, "totals");
    const auto *groups = field(total, "groups");
    if (!cJSON_IsArray(groups) || cJSON_GetArraySize(groups) > 10) return false;
    ModelCount models[10]{};
    unsigned used = 0;
    uint64_t sum = 0, remainder = 0;
    for (auto *group = groups->child; group; group = group->next) {
        const auto *name = field(group, "model");
        if (!cJSON_IsObject(group) || !cJSON_IsString(name)) return false;
        const size_t length = std::strlen(name->valuestring);
        if (!length || length >= sizeof(models[0].name)) return false;
        for (size_t i = 0; i < length; ++i)
            if (name->valuestring[i] < 32 || name->valuestring[i] > 126) return false;
        for (unsigned i = 0; i < used; ++i)
            if (!std::strcmp(models[i].name, name->valuestring)) return false;
        if (!count(field(group, "request_count"), models[used].requests) ||
            models[used].requests > summary.requests - sum) return false;
        sum += models[used].requests;
        std::memcpy(models[used].name, name->valuestring, length + 1);
        ++used;
    }
    const auto *hidden = field(total, "remainder");
    if (hidden && (!cJSON_IsObject(hidden) || !count(field(hidden, "request_count"), remainder)))
        return false;
    if (remainder != summary.requests - sum) return false;
    std::sort(models, models + used, [](const ModelCount &a, const ModelCount &b) {
        return a.requests != b.requests ? a.requests > b.requests : std::strcmp(a.name, b.name) < 0;
    });
    Models result;
    result.requests = summary.requests;
    result.from = summary.from;
    result.until = summary.until;
    result.count = std::min(used, top_models);
    uint64_t visible = 0;
    for (unsigned i = 0; i < result.count; ++i) {
        result.top[i] = models[i];
        visible += models[i].requests;
    }
    result.other_requests = result.requests - visible;
    result.global_top_five = !remainder ||
        (result.count == top_models && remainder <= result.top[top_models - 1].requests);
    out = result;
    return true;
}

void wipe(void *data, size_t size) {
    volatile auto *p = static_cast<volatile unsigned char *>(data);
    while (size--) *p++ = 0;
}

bool models_fresh(const Models &models, int64_t now) {
    return now >= 1704067200 && now <= 4102444799 &&
        models.until >= now - 180 && models.until <= now + 120;
}

bool parse_config(const char *raw, size_t size, Config &out) {
    wipe(&out, sizeof(out));
    if (!raw || !size || size > 2048 || !ou::json_guard(raw, size, 4)) return false;
    const char *end = nullptr;
    Json json{cJSON_ParseWithLengthOpts(raw, size, &end, false)};
    if (!json.value || !cJSON_IsObject(json.value) || !end) return false;
    while (end < raw + size && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
    if (end != raw + size || cJSON_GetArraySize(json.value) != 2) return false;
    const auto *schema = field(json.value, "schema");
    const auto *key = field(json.value, "ollama_api_key");
    if (!cJSON_IsNumber(schema) || schema->valuedouble != 1 || !cJSON_IsString(key)) return false;
    const size_t length = std::strlen(key->valuestring);
    if (length >= sizeof(out.ollama_key)) return false;
    if (!length) return true; // A blank template is valid but never sends a request.
    if (length < 16 || !std::strncmp(key->valuestring, "REPLACE_", 8)) return false;
    for (size_t i = 0; i < length; ++i)
        if (key->valuestring[i] < 33 || key->valuestring[i] > 126) return false;
    std::memcpy(out.ollama_key, key->valuestring, length + 1);
    out.ready = true;
    return true;
}

bool load_config(Config &out, const char *path) {
    wipe(&out, sizeof(out));
    FILE *file = std::fopen(path, "rb");
    if (!file && errno == ENOENT && !std::strcmp(path, config_path))
        file = std::fopen(legacy_config_path, "rb");
    if (!file) return false;
    char raw[2049]{};
    const size_t size = std::fread(raw, 1, sizeof(raw), file);
    const bool readable = !std::ferror(file);
    std::fclose(file);
    const bool ok = readable && parse_config(raw, size, out);
    wipe(raw, sizeof(raw));
    return ok;
}
} // namespace oa

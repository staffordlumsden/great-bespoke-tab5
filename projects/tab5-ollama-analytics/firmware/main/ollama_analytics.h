#pragma once
#include "cyd_usage/usage_model.h"
#include <cstddef>
#include <cstdint>

namespace oa {
constexpr unsigned top_models = 5;
constexpr uint32_t model_colours[top_models] = {
    0x3b82f6, 0x22c55e, 0xf59e0b, 0xa78bfa, 0xf472b6};
struct ModelCount {
    char name[96]{};
    uint64_t requests = 0;
};
struct Models {
    uint64_t requests = 0, other_requests = 0;
    int64_t from = 0, until = 0;
    unsigned count = 0;
    ModelCount top[top_models]{};
    // Ollama can omit groups chosen by spend. Remainder is an upper bound on
    // any hidden model's request count, so sometimes global rank is provable.
    bool global_top_five = false;
};
struct Config {
    char ollama_key[513]{};
    bool ready = false;
};
bool parse_models(const char *json, size_t size, Models &out);
bool models_fresh(const Models &models, int64_t now);
bool parse_config(const char *json, size_t size, Config &out);
void wipe(void *data, size_t size);
constexpr const char *config_path = "/sdcard/private/tab5-ollama-analytics.json";
constexpr const char *legacy_config_path = "/sdcard/private/tab5-ai-usage.json";
// Use the legacy profile only when the default new profile is absent.
bool load_config(Config &out, const char *path = config_path);
} // namespace oa

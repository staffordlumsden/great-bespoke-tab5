#pragma once
#include "ollama_analytics.h"
namespace oa {
enum class HttpError { none, config, transport, status, size, content_type, incomplete, json };
struct FetchMeta {
    bool ok = false;
    int status = 0;
    unsigned retry_after = 0;
    HttpError error = HttpError::none;
};
struct FetchResult {
    FetchMeta balance_meta, models_meta;
    ou::Balance balance;
    Models models;
};
// Synchronous, read-only HTTPS. The app invokes this from a dedicated worker.
void fetch_ollama(const Config &config, FetchResult &out);
} // namespace oa

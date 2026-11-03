#include "ollama_http.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace oa {
namespace {
struct Headers { bool json = false; unsigned retry = 0; };
esp_err_t event(esp_http_client_event_t *e) {
    if (e->event_id != HTTP_EVENT_ON_HEADER || !e->header_key || !e->header_value)
        return ESP_OK;
    auto &h = *static_cast<Headers *>(e->user_data);
    if (!strcasecmp(e->header_key, "Content-Type"))
        h.json = !strncasecmp(e->header_value, "application/json", 16) &&
            (!e->header_value[16] || e->header_value[16] == ';' || e->header_value[16] == ' ');
    if (!strcasecmp(e->header_key, "Retry-After")) h.retry = ou::retry_seconds(e->header_value);
    return ESP_OK;
}
void request(const Config &config, bool models, FetchResult &out) {
    auto &meta = models ? out.models_meta : out.balance_meta;
    Headers headers;
    esp_http_client_config_t cfg{};
    cfg.url = models ? "https://ollama.com/api/usage?range=24h&scope=self&group_by=model"
                     : "https://ollama.com/api/balance";
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = true;
    cfg.timeout_ms = 8000;
    cfg.buffer_size = 2048;
    cfg.buffer_size_tx = 1024;
    cfg.event_handler = event;
    cfg.user_data = &headers;
    auto client = esp_http_client_init(&cfg);
    if (!client) { meta.error = HttpError::transport; return; }
    char authorization[520]{};
    std::snprintf(authorization, sizeof(authorization), "Bearer %s", config.ollama_key);
    const bool authorized = esp_http_client_set_header(client, "Authorization", authorization) == ESP_OK;
    wipe(authorization, sizeof(authorization));
    char *body = nullptr;
    size_t used = 0;
    meta.error = HttpError::transport;
    const auto start = esp_timer_get_time();
    if (authorized && esp_http_client_set_header(client, "Accept", "application/json") == ESP_OK &&
        esp_http_client_open(client, 0) == ESP_OK) {
        const auto length = esp_http_client_fetch_headers(client);
        meta.status = esp_http_client_get_status_code(client);
        meta.retry_after = headers.retry;
        if (meta.status != 200) meta.error = HttpError::status;
        else if (length < 0) meta.error = HttpError::transport;
        else if (length > static_cast<int64_t>(ou::response_limit)) meta.error = HttpError::size;
        else if (!headers.json) meta.error = HttpError::content_type;
        else if (!(body = static_cast<char *>(std::malloc(ou::response_limit + 1))))
            meta.error = HttpError::transport;
        else {
            meta.error = HttpError::incomplete;
            while (esp_timer_get_time() - start < 25000000) {
                const size_t left = ou::response_limit + 1 - used;
                if (!left) { meta.error = HttpError::size; break; }
                const int n = esp_http_client_read(client, body + used,
                    static_cast<int>(std::min(left, size_t(2048))));
                if (n < 0) { meta.error = HttpError::transport; break; }
                used += n;
                if (used > ou::response_limit) { meta.error = HttpError::size; break; }
                if (esp_http_client_is_complete_data_received(client)) {
                    body[used] = 0;
                    ou::Error error;
                    meta.ok = models ? parse_models(body, used, out.models)
                                     : ou::parse_balance(body, used, out.balance, error);
                    meta.error = meta.ok ? HttpError::none : HttpError::json;
                    break;
                }
                if (!n) break;
            }
        }
    }
    if (body) { wipe(body, used); std::free(body); }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}
} // namespace
void fetch_ollama(const Config &config, FetchResult &out) {
    out = FetchResult{};
    if (!config.ready) {
        out.balance_meta.error = out.models_meta.error = HttpError::config;
        return;
    }
    request(config, false, out);
    if (out.balance_meta.status == 401 || out.balance_meta.status == 403) {
        out.models_meta = out.balance_meta;
        return;
    }
    request(config, true, out);
}
} // namespace oa

#pragma once

#include "esp_err.h"
#include <cstddef>
#include <cstdint>

namespace tab5_fs {

struct HttpStats {
    uint32_t revision;
    bool running;
    uint32_t requests;
    uint32_t downloads;
    uint32_t html_streams;
    uint32_t uploads;
    uint64_t bytes_transferred;
    char last_event[72];
};

// Start the HTTP File Explorer server on port 80.
esp_err_t http_server_start(const char* sd_root, const char initial_passcode[9]);

// Update the active 8-digit passcode required by the HTTP server.
void http_server_set_passcode(const char new_passcode[9]);

// Retrieve current HTTP server telemetry.
void http_server_get_stats(HttpStats* out);

}  // namespace tab5_fs

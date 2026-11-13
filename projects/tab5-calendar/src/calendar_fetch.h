#pragma once
#include <string>

namespace calendar {
enum class FetchError { none, transport, http, oversized, incomplete, invalid_calendar };
struct FetchOutcome {
    FetchError error = FetchError::transport;
    int http_status = 0;
};
using FetchAttempt = FetchOutcome (*)(const char *url, std::string &body, void *context);
// Retrying only transient network failures preserves the previous successful
// calendar on all failures. Error results never expose a partial response.
FetchOutcome fetch_with_retry(const char *url, std::string &body,
                              FetchAttempt attempt, void *context);
bool retryable(FetchOutcome outcome);
}

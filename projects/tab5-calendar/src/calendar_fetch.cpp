#include "calendar_fetch.h"
namespace calendar {
bool retryable(FetchOutcome outcome) {
    return outcome.error == FetchError::transport || outcome.error == FetchError::incomplete ||
           (outcome.error == FetchError::http && (outcome.http_status == 429 || outcome.http_status >= 500));
}
FetchOutcome fetch_with_retry(const char *url, std::string &body,
                              FetchAttempt attempt, void *context) {
    body.clear();
    if (!url || !attempt) return {};
    FetchOutcome result{};
    for (int i = 0; i < 3; ++i) {
        body.clear();
        result = attempt(url, body, context);
        if (result.error == FetchError::none) return result;
        body.clear();
        if (!retryable(result)) break;
    }
    return result;
}
}

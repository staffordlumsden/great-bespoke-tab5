#include "../src/calendar_fetch.h"
#include <cassert>
using namespace calendar;
struct Fake { int count; FetchOutcome outcomes[3]; };
static FetchOutcome perform(const char *, std::string &body, void *context) {
    auto &fake = *static_cast<Fake *>(context);
    body += fake.count ? "complete" : "partial";
    return fake.outcomes[fake.count++];
}
int main() {
    std::string body;
    Fake retry{0, {{FetchError::transport,0}, {FetchError::http,503}, {FetchError::none,200}}};
    auto result = fetch_with_retry("https://example.invalid", body, perform, &retry);
    assert(result.error == FetchError::none && retry.count == 3 && body == "complete");
    Fake missing{0, {{FetchError::http,404}, {FetchError::none,200}, {}}};
    result = fetch_with_retry("https://example.invalid", body, perform, &missing);
    assert(result.error == FetchError::http && result.http_status == 404 && missing.count == 1 && body.empty());
    Fake incomplete{0, {{FetchError::incomplete,200}, {FetchError::incomplete,200}, {FetchError::incomplete,200}}};
    result = fetch_with_retry("https://example.invalid", body, perform, &incomplete);
    assert(result.error == FetchError::incomplete && incomplete.count == 3 && body.empty());
    assert(!retryable({FetchError::oversized,200}));
}

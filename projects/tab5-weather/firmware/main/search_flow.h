#pragma once
#include "weather_http.h"
#include <cstdio>
#include <cstring>

namespace wm {
// Pending searches wait for networking/time or an existing fetch; they are never
// silently dropped. Query generation still rejects obsolete asynchronous replies.
class SearchFlow {
    bool pending_ = false;
    uint64_t deadline_ = 0;
    Job job_{};
  public:
    enum class Step { idle, offline, clock, busy, ready, expired };
    bool begin(const char *term, uint32_t generation, uint64_t now) {
        const size_t length = term ? std::strlen(term) : 0;
        if (length < 2 || length > 40) return false;
        job_.kind = JobKind::search;
        job_.generation = generation;
        std::snprintf(job_.query, sizeof(job_.query), "%s", term);
        deadline_ = now + 30000;
        pending_ = true;
        return true;
    }
    void cancel() { pending_ = false; }
    bool pending() const { return pending_; }
    Step poll(bool connected, bool clock, bool busy, uint64_t now) {
        if (!pending_) return Step::idle;
        if (now >= deadline_) { pending_ = false; return Step::expired; }
        if (!connected) return Step::offline;
        if (!clock) return Step::clock;
        if (busy) return Step::busy;
        return Step::ready;
    }
    const Job &job() const { return job_; }
    void submitted() { pending_ = false; }
};

using SettingsWriter = bool (*)(const Settings &, void *);
inline bool save_search_result(Settings &settings, const Location &place,
                               SettingsWriter write, void *context) {
    Settings candidate = settings;
    if (!save_place(candidate, place) || !write(candidate, context)) return false;
    settings = candidate; // Never display a saved city before persistence succeeds.
    return true;
}
} // namespace wm

#pragma once
#include "calendar_view.h"
#include <string>
#include <vector>
namespace calendar {
struct Event {
    Date start{}, end{}; // end is exclusive for all-day; timed events use inclusive end date
    int minute = 0, end_minute = 0;
    Date source_start{};
    int source_minute = 0, source_utc_offset = 0;
    bool fixed_source_zone = false;
    bool all_day = false, recurring = false;
    int interval = 1, count = 0, weekdays = 0;
    int month_day = 0, month = 0, weekday_position = 0, week_start_day = 0;
    Date until{2100,12,31};
    int until_minute = 1439;
    char frequency = 0;
    std::string title;
    std::string uid;
    size_t calendar = 0;
    std::vector<Date> excluded;
};
struct ParseResult { std::vector<Event> events; unsigned warnings = 0; bool fatal = false; };
// Input is bounded by the caller. TZ must be configured for the device's local zone.
ParseResult parse_ics(const char *data, size_t length, size_t calendar_index);
bool occurs(const Event &e, Date d);
bool starts_on(const Event &e, Date d);
int occurrence_minute(const Event &e, Date d);
}

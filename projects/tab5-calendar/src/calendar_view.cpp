#include "calendar_view.h"
#include <algorithm>

namespace calendar {
namespace {
bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
int days_in_month(int y, int m) {
    static constexpr int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[m-1] + (m == 2 && leap(y));
}
// Howard Hinnant's civil calendar arithmetic; epoch is 1970-01-01.
int64_t serial(Date d) {
    int y = d.year - (d.month <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned mp = static_cast<unsigned>(d.month + (d.month > 2 ? -3 : 9));
    const unsigned doy = (153 * mp + 2) / 5 + static_cast<unsigned>(d.day) - 1;
    return int64_t(era) * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
Date from_serial(int64_t z) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int y = static_cast<int>(yoe) + static_cast<int>(era * 400);
    const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
    const unsigned mp = (5*doy+2)/153;
    const unsigned day = doy - (153*mp+2)/5 + 1;
    const int month = static_cast<int>(mp) + (mp < 10 ? 3 : -9);
    y += month <= 2;
    return {y, month, static_cast<int>(day)};
}
}
bool valid(Date d) {
    return d.year >= 1900 && d.year <= 2100 && d.month >= 1 && d.month <= 12 &&
           d.day >= 1 && d.day <= days_in_month(d.year, d.month);
}
int compare(Date a, Date b) {
    const auto x = serial(a), y = serial(b);
    return x < y ? -1 : x > y ? 1 : 0;
}
Date add_days(Date d, int days) {
    if (!valid(d)) return d;
    Date out = from_serial(serial(d) + days);
    return valid(out) ? out : d;
}
Date add_months(Date d, int months) {
    if (!valid(d)) return d;
    const int64_t m = int64_t(d.year) * 12 + d.month - 1 + months;
    if (m < 1900 * 12 || m >= 2101 * 12) return d;
    const int y = static_cast<int>(m / 12), month = static_cast<int>(m % 12) + 1;
    return {y, month, std::min(d.day, days_in_month(y, month))};
}
int weekday(Date d) {
    if (!valid(d)) return -1;
    int n = static_cast<int>((serial(d) + 3) % 7);
    return n < 0 ? n + 7 : n;
}
Date week_start(Date d) { return add_days(d, -weekday(d)); }
Date month_grid_start(Date d) { d.day = 1; return week_start(d); }
void toggle(CalendarSetting &s) { s.visible = !s.visible; }
void cycle_colour(CalendarSetting &s, int direction) {
    const int current = static_cast<int>(s.colour);
    if (current < 0 || current >= 5) { s.colour = Colour::red; return; }
    if (direction > 0) s.colour = static_cast<Colour>((current + 1) % 5);
    if (direction < 0) s.colour = static_cast<Colour>((current + 4) % 5);
}
void move(Navigation &s, Direction key, size_t agenda_count, size_t calendar_count) {
    if (s.view == View::month || s.view == View::week) {
        int step = key == Direction::left ? -1 : key == Direction::right ? 1 :
                   key == Direction::up ? -7 : 7;
        s.date = add_days(s.date, step);
    } else if (s.view == View::agenda) {
        if (!agenda_count) { s.agenda_offset = 0; return; }
        s.agenda_offset = std::min(s.agenda_offset, agenda_count - 1);
        if (key == Direction::up && s.agenda_offset) --s.agenda_offset;
        if (key == Direction::down && s.agenda_offset + 1 < agenda_count) ++s.agenda_offset;
        if (key == Direction::left) s.agenda_offset = s.agenda_offset > 7 ? s.agenda_offset - 7 : 0;
        if (key == Direction::right) s.agenda_offset += std::min(size_t(7), agenda_count - 1 - s.agenda_offset);
    } else {
        if (!calendar_count) { s.calendar_selection = 0; return; }
        s.calendar_selection = std::min(s.calendar_selection, calendar_count - 1);
        if (key == Direction::up && s.calendar_selection) --s.calendar_selection;
        if (key == Direction::down && s.calendar_selection + 1 < calendar_count) ++s.calendar_selection;
    }
}
}

#pragma once
#include <cstddef>
#include <cstdint>

namespace calendar {
struct Date { int year, month, day; };
bool valid(Date d);
int compare(Date a, Date b);
Date add_days(Date d, int days);
Date add_months(Date d, int months);
int weekday(Date d); // Monday = 0

enum class View { month, week, agenda, calendars };
enum class Direction { left, right, up, down };
enum class Colour : uint8_t { red, yellow, green, blue, purple };
constexpr uint32_t colours[] = {0xef4444, 0xeab308, 0x22c55e, 0x3b82f6, 0xa855f7};
struct CalendarSetting { bool visible; Colour colour; };
void toggle(CalendarSetting &setting);
void cycle_colour(CalendarSetting &setting, int direction);
struct Navigation {
    Date date;
    View view;
    size_t agenda_offset;
    size_t calendar_selection;
};
// Arrows navigate days/week rows in the grid, and list rows in agenda/settings.
void move(Navigation &state, Direction key, size_t agenda_count, size_t calendar_count);
Date month_grid_start(Date date);
Date week_start(Date date);
}

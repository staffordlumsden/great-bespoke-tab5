#include "../src/calendar_view.h"
#include <cassert>
using namespace calendar;
static bool eq(Date a, Date b) { return compare(a,b)==0; }
int main() {
    assert(valid({2024,2,29}) && !valid({2025,2,29}));
    assert(eq(add_days({2024,2,28},1),{2024,2,29}));
    assert(eq(add_days({2024,12,31},1),{2025,1,1}));
    assert(eq(add_months({2024,1,31},1),{2024,2,29}));
    assert(weekday({2026,11,13}) == 4);
    assert(eq(month_grid_start({2026,11,13}),{2026,10,26}));
    Navigation n{{2026,11,13},View::month,0,0};
    move(n,Direction::up,0,0); assert(eq(n.date,{2026,11,6}));
    move(n,Direction::right,0,0); assert(eq(n.date,{2026,11,7}));
    n.view=View::agenda; move(n,Direction::right,3,0); assert(n.agenda_offset==2);
    move(n,Direction::down,3,0); assert(n.agenda_offset==2);
    move(n,Direction::up,3,0); assert(n.agenda_offset==1);
    n.view=View::calendars; move(n,Direction::down,0,2); assert(n.calendar_selection==1);
    move(n,Direction::down,0,2); assert(n.calendar_selection==1);
    CalendarSetting setting{true, Colour::red};
    toggle(setting); assert(!setting.visible); toggle(setting); assert(setting.visible);
    cycle_colour(setting,-1); assert(setting.colour==Colour::purple);
    cycle_colour(setting,1); assert(setting.colour==Colour::red);
    cycle_colour(setting,1); assert(setting.colour==Colour::yellow);
}

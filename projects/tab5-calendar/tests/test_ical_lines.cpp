#include "../src/ical_lines.h"
#include <cassert>
#include <cstring>

int main() {
    using namespace calendar;
    const char input[] = "BEGIN:VCALENDAR\r\nSUMMARY:One long\r\n line\r\nEND:VCALENDAR\r\n";
    size_t pos = 0;
    char out[64];
    assert(next_line(input, sizeof(input)-1, pos, out, sizeof(out)) == LineResult::line);
    assert(std::strcmp(out, "BEGIN:VCALENDAR") == 0);
    assert(next_line(input, sizeof(input)-1, pos, out, sizeof(out)) == LineResult::line);
    assert(std::strcmp(out, "SUMMARY:One longline") == 0);
    assert(next_line(input, sizeof(input)-1, pos, out, sizeof(out)) == LineResult::line);
    assert(std::strcmp(out, "END:VCALENDAR") == 0);
    assert(next_line(input, sizeof(input)-1, pos, out, sizeof(out)) == LineResult::end);
    pos = 0;
    assert(next_line(" bad\n", 5, pos, out, sizeof(out)) == LineResult::malformed);
    pos = 0;
    assert(next_line("A\rB", 3, pos, out, sizeof(out)) == LineResult::malformed);
    pos = 0;
    assert(next_line("1234\n", 5, pos, out, 4) == LineResult::too_long);
    pos = 0;
    const char nul[] = {'A', '\0', 'B'};
    assert(next_line(nul, sizeof(nul), pos, out, sizeof(out)) == LineResult::malformed);
}

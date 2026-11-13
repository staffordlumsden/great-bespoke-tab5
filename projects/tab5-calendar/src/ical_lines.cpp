#include "ical_lines.h"

namespace calendar {
LineResult next_line(const char *data, size_t length, size_t &offset,
                     char *output, size_t capacity) {
    if (!data || !output || capacity == 0 || offset > length) return LineResult::malformed;
    output[0] = '\0';
    if (offset == length) return LineResult::end;
    if (data[offset] == ' ' || data[offset] == '\t') return LineResult::malformed;
    size_t used = 0;
    while (offset < length) {
        const unsigned char ch = static_cast<unsigned char>(data[offset++]);
        if (ch == '\0') return LineResult::malformed;
        if (ch == '\r' || ch == '\n') {
            if (ch == '\r') {
                if (offset >= length || data[offset] != '\n') return LineResult::malformed;
                ++offset;
            }
            if (offset < length && (data[offset] == ' ' || data[offset] == '\t')) {
                ++offset;
                continue;
            }
            output[used] = '\0';
            return LineResult::line;
        }
        if (used + 1 >= capacity) return LineResult::too_long;
        output[used++] = static_cast<char>(ch);
    }
    output[used] = '\0';
    return used ? LineResult::line : LineResult::malformed;
}
}

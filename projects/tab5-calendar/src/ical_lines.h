#pragma once

#include <cstddef>

// Unfold an iCalendar content line into a caller-owned buffer. The input is
// length-delimited: calendar data need not be NUL-terminated. On an error the
// caller must discard the entire feed rather than use a partial calendar.
namespace calendar {
enum class LineResult { line, end, malformed, too_long };
LineResult next_line(const char *data, size_t length, size_t &offset,
                     char *output, size_t capacity);
}

#pragma once

#include <cstdint>

namespace tab5 {

// Matrix digital-rain lockscreen and RTC clock for Tab5 Console.
// Opened by the dedicated Lock button: a green glyph cascade plus a central
// clock panel. Double-tap opens the PIN keypad; only a correct PIN returns.
void matrix_clock_run(uint8_t brightness_percent);

}  // namespace tab5

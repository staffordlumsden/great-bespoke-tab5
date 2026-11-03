#pragma once
#include "serial_drive.h"
#include <cstdint>

namespace usbdrive {
enum class Action { none, theme, stop, console };
struct View { bool light = false; bool console = false; Snapshot drive{}; };
uint16_t pixel(int x, int y, void *context);
Action hit(int x, int y);
}

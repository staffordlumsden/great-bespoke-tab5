#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace tab5 {

struct Tab5KbdConfig {
    int      sda_gpio   = 0;
    int      scl_gpio   = 1;
    int      int_gpio   = 50;
    uint32_t i2c_hz     = 400000;
    int      task_stack = 4096;
};

using ByteSink = std::function<void(const uint8_t* data, size_t length)>;

bool start_tab5_kbd_input(ByteSink sink, const Tab5KbdConfig& cfg = {});
bool tab5_kbd_connected();

}  // namespace tab5

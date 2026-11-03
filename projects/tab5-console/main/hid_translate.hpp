#pragma once

#include <cstddef>
#include <cstdint>

namespace tab5::hid_translate {

using Mods = uint8_t;
constexpr size_t kMaxBytes = 8;

size_t emit_key(uint8_t hid_code, Mods mods, uint8_t out[kMaxBytes]);

}  // namespace tab5::hid_translate

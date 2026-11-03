#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "hid_translate.hpp"

namespace {

void expect(uint8_t code, uint8_t modifiers,
            std::initializer_list<uint8_t> wanted, const char* label) {
    std::array<uint8_t, tab5::hid_translate::kMaxBytes> output{};
    const size_t count = tab5::hid_translate::emit_key(code, modifiers,
                                                       output.data());
    if (count != wanted.size()) {
        std::fprintf(stderr, "%s: count=%zu wanted=%zu\n", label, count,
                     wanted.size());
        std::exit(1);
    }
    size_t index = 0;
    for (uint8_t value : wanted) {
        if (output[index] != value) {
            std::fprintf(stderr, "%s: byte[%zu]=0x%02x wanted=0x%02x\n",
                         label, index, output[index], value);
            std::exit(1);
        }
        ++index;
    }
}

}  // namespace

int main() {
    expect(0x04, 0x00, {'a'}, "letter");
    expect(0x04, 0x02, {'A'}, "shift-letter");
    expect(0x1E, 0x02, {'!'}, "shift-number");
    expect(0x2D, 0x02, {'_'}, "underscore");
    expect(0x2F, 0x02, {'{'}, "brace");
    expect(0x38, 0x02, {'?'}, "question-mark");
    expect(0x06, 0x01, {0x03}, "ctrl-c");
    expect(0x1B, 0x04, {0x1B, 'x'}, "alt-x");
    expect(0x4F, 0x00, {0x1B, '[', 'C'}, "right-arrow");
    expect(0x3E, 0x00, {0x1B, '[', '1', '5', '~'}, "f5");
    expect(0x2A, 0x00, {0x7F}, "backspace");
    expect(0xFF, 0x00, {}, "unknown");
    std::puts("TAB5_HID_TRANSLATE PASS");
    return 0;
}

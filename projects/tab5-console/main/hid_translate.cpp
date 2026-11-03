#include "hid_translate.hpp"

#include <cstring>

namespace tab5::hid_translate {

namespace {

constexpr uint8_t kModLCtrl  = 0x01;
constexpr uint8_t kModLShift = 0x02;
constexpr uint8_t kModLAlt   = 0x04;
constexpr uint8_t kModRCtrl  = 0x10;
constexpr uint8_t kModRShift = 0x20;
constexpr uint8_t kModRAlt   = 0x40;

constexpr uint8_t kHidEnter    = 0x28;
constexpr uint8_t kHidEsc      = 0x29;
constexpr uint8_t kHidDel      = 0x2A;
constexpr uint8_t kHidTab      = 0x2B;
constexpr uint8_t kHidF1       = 0x3A;
constexpr uint8_t kHidF2       = 0x3B;
constexpr uint8_t kHidF3       = 0x3C;
constexpr uint8_t kHidF4       = 0x3D;
constexpr uint8_t kHidF5       = 0x3E;
constexpr uint8_t kHidF6       = 0x3F;
constexpr uint8_t kHidF7       = 0x40;
constexpr uint8_t kHidF8       = 0x41;
constexpr uint8_t kHidF9       = 0x42;
constexpr uint8_t kHidF10      = 0x43;
constexpr uint8_t kHidF11      = 0x44;
constexpr uint8_t kHidF12      = 0x45;
constexpr uint8_t kHidInsert   = 0x49;
constexpr uint8_t kHidHome     = 0x4A;
constexpr uint8_t kHidPageUp   = 0x4B;
constexpr uint8_t kHidDelete   = 0x4C;
constexpr uint8_t kHidEnd      = 0x4D;
constexpr uint8_t kHidPageDown = 0x4E;
constexpr uint8_t kHidRight    = 0x4F;
constexpr uint8_t kHidLeft     = 0x50;
constexpr uint8_t kHidDown     = 0x51;
constexpr uint8_t kHidUp       = 0x52;
constexpr uint8_t kHidKpEnter  = 0x58;

struct CharMap {
    char base;
    char shifted;
};
constexpr CharMap kMap[0x39] = {
    {0, 0},  {0, 0},  {0, 0},  {0, 0},                       // 0x00..0x03
    {'a','A'}, {'b','B'}, {'c','C'}, {'d','D'}, {'e','E'},   // 0x04..0x08
    {'f','F'}, {'g','G'}, {'h','H'}, {'i','I'}, {'j','J'},   // 0x09..0x0D
    {'k','K'}, {'l','L'}, {'m','M'}, {'n','N'}, {'o','O'},   // 0x0E..0x12
    {'p','P'}, {'q','Q'}, {'r','R'}, {'s','S'}, {'t','T'},   // 0x13..0x17
    {'u','U'}, {'v','V'}, {'w','W'}, {'x','X'}, {'y','Y'},   // 0x18..0x1C
    {'z','Z'},                                               // 0x1D
    {'1','!'}, {'2','@'}, {'3','#'}, {'4','$'}, {'5','%'},   // 0x1E..0x22
    {'6','^'}, {'7','&'}, {'8','*'}, {'9','('}, {'0',')'},   // 0x23..0x27
    {0, 0},   // Enter — handled specially      0x28
    {0, 0},   // Esc                            0x29
    {0, 0},   // Backspace                      0x2A
    {0, 0},   // Tab                            0x2B
    {' ',' '}, // Space                         0x2C
    {'-','_'}, {'=','+'},                                    // 0x2D, 0x2E
    {'[','{'}, {']','}'}, {'\\','|'},                        // 0x2F..0x31
    {0, 0},                                                  // 0x32 non-US hash
    {';',':'}, {'\'','"'}, {'`','~'},                        // 0x33..0x35
    {',','<'}, {'.','>'}, {'/','?'},                         // 0x36..0x38
};

const char* special_seq(uint8_t code, int* len_out) {
    *len_out = 0;
    auto lit = [&](const char* s, int n) { *len_out = n; return s; };
    switch (code) {
        case kHidRight:    return lit("\x1B[C", 3);
        case kHidLeft:     return lit("\x1B[D", 3);
        case kHidDown:     return lit("\x1B[B", 3);
        case kHidUp:       return lit("\x1B[A", 3);
        case kHidHome:     return lit("\x1B[H", 3);
        case kHidEnd:      return lit("\x1B[F", 3);
        case kHidInsert:   return lit("\x1B[2~", 4);
        case kHidDelete:   return lit("\x1B[3~", 4);
        case kHidPageUp:   return lit("\x1B[5~", 4);
        case kHidPageDown: return lit("\x1B[6~", 4);
        case kHidF1:       return lit("\x1BOP", 3);
        case kHidF2:       return lit("\x1BOQ", 3);
        case kHidF3:       return lit("\x1BOR", 3);
        case kHidF4:       return lit("\x1BOS", 3);
        case kHidF5:       return lit("\x1B[15~", 5);
        case kHidF6:       return lit("\x1B[17~", 5);
        case kHidF7:       return lit("\x1B[18~", 5);
        case kHidF8:       return lit("\x1B[19~", 5);
        case kHidF9:       return lit("\x1B[20~", 5);
        case kHidF10:      return lit("\x1B[21~", 5);
        case kHidF11:      return lit("\x1B[23~", 5);
        case kHidF12:      return lit("\x1B[24~", 5);
        default: return nullptr;
    }
}

}  // namespace

size_t emit_key(uint8_t code, Mods mods, uint8_t out[kMaxBytes]) {
    const bool shift = mods & (kModLShift | kModRShift);
    const bool ctrl  = mods & (kModLCtrl  | kModRCtrl);
    const bool alt   = mods & (kModLAlt   | kModRAlt);

    auto write_bytes = [&](const uint8_t* src, size_t n) -> size_t {
        size_t off = 0;
        if (alt && off + 1 <= kMaxBytes) out[off++] = 0x1B;
        for (size_t i = 0; i < n && off < kMaxBytes; ++i) out[off++] = src[i];
        return off;
    };

    switch (code) {
        case kHidEnter:
        case kHidKpEnter: { uint8_t b = '\r'; return write_bytes(&b, 1); }
        case kHidEsc:     { uint8_t b = 0x1B; return write_bytes(&b, 1); }
        case kHidDel:     { uint8_t b = 0x7F; return write_bytes(&b, 1); }
        case kHidTab:     { uint8_t b = '\t'; return write_bytes(&b, 1); }
        default: break;
    }

    {
        int len = 0;
        if (const char* s = special_seq(code, &len)) {
            return write_bytes(reinterpret_cast<const uint8_t*>(s),
                               static_cast<size_t>(len));
        }
    }

    if (code >= 0x04 && code < 0x39) {
        char ch = shift ? kMap[code].shifted : kMap[code].base;
        if (ch == 0) return 0;
        if (ctrl) {
            if (ch >= 'a' && ch <= 'z')      ch = static_cast<char>(ch - 'a' + 1);
            else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 1);
            else if (ch >= '@' && ch <= '_') ch = static_cast<char>(ch - '@');
        }
        uint8_t b = static_cast<uint8_t>(ch);
        return write_bytes(&b, 1);
    }

    return 0;
}

}  // namespace tab5::hid_translate

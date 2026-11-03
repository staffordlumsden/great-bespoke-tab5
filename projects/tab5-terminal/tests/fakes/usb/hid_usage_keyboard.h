#pragma once

// USB HID Keyboard Page usage values needed by hid_translate.cpp. The target
// build uses Espressif's real header; this host fake keeps the mapping test
// independent of ESP-IDF.
#define HID_KEY_ENTER     0x28
#define HID_KEY_ESC       0x29
#define HID_KEY_DEL       0x2A
#define HID_KEY_TAB       0x2B
#define HID_KEY_F1        0x3A
#define HID_KEY_F2        0x3B
#define HID_KEY_F3        0x3C
#define HID_KEY_F4        0x3D
#define HID_KEY_F5        0x3E
#define HID_KEY_F6        0x3F
#define HID_KEY_F7        0x40
#define HID_KEY_F8        0x41
#define HID_KEY_F9        0x42
#define HID_KEY_F10       0x43
#define HID_KEY_F11       0x44
#define HID_KEY_F12       0x45
#define HID_KEY_INSERT    0x49
#define HID_KEY_HOME      0x4A
#define HID_KEY_PAGEUP    0x4B
#define HID_KEY_DELETE    0x4C
#define HID_KEY_END       0x4D
#define HID_KEY_PAGEDOWN  0x4E
#define HID_KEY_RIGHT     0x4F
#define HID_KEY_LEFT      0x50
#define HID_KEY_DOWN      0x51
#define HID_KEY_UP        0x52

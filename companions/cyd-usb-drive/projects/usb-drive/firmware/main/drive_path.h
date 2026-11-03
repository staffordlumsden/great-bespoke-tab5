#pragma once
#include <cstddef>

namespace usbdrive {
bool canonical_utf8(const unsigned char *text);
bool virtual_path(const char *path, char *fat_path, size_t capacity,
                  bool allow_root = false);
bool root_path(const char *path);
const char *display_name(const char *path);
}

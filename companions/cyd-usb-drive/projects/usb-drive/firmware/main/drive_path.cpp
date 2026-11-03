#include "drive_path.h"
#include <cstring>
#include <cstdio>

namespace usbdrive {
bool canonical_utf8(const unsigned char *p) {
    while (*p) {
        unsigned first = *p++;
        if (first < 128) continue;
        unsigned count = 0, value = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; value = first & 31; minimum = 128; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; value = first & 15; minimum = 2048; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; value = first & 7; minimum = 65536; }
        else return false;
        while (count--) {
            if ((*p & 0xc0) != 0x80) return false;
            value = (value << 6) | (*p++ & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

bool root_path(const char *path) {
    return path && (!std::strcmp(path, "/") || !std::strcmp(path, "/Files") ||
                    !std::strcmp(path, "/Firmware"));
}

bool virtual_path(const char *path, char *out, size_t capacity, bool allow_root) {
    if (!path || !out || !capacity || path[0] != '/' || !canonical_utf8(reinterpret_cast<const unsigned char *>(path))) return false;
    if (!std::strcmp(path, "/")) {
        if (!allow_root) return false;
        std::snprintf(out, capacity, "0:");
        return true;
    }
    const char *tail = nullptr;
    const char *base = nullptr;
    if (!std::strncmp(path, "/Files", 6) && (!path[6] || path[6] == '/')) {
        base = "0:/files"; tail = path + 6;
    } else if (!std::strncmp(path, "/Firmware", 9) && (!path[9] || path[9] == '/')) {
        base = "0:/firmware"; tail = path + 9;
    } else return false;
    if (!*tail) {
        if (!allow_root) return false;
        return std::snprintf(out, capacity, "%s", base) > 0;
    }
    if (*tail != '/' || !tail[1] || path[std::strlen(path) - 1] == '/') return false;
    for (const char *segment = tail + 1; *segment;) {
        const char *end = std::strchr(segment, '/');
        const size_t length = end ? size_t(end - segment) : std::strlen(segment);
        if (!length || length > 128 || segment[0] == '.') return false;
        for (size_t i = 0; i < length; ++i) {
            const unsigned char c = static_cast<unsigned char>(segment[i]);
            if (c < 32 || c == 127 || c == ':' || c == '\\') return false;
        }
        if (!end) break;
        segment = end + 1;
        if (!*segment) return false;
    }
    return std::snprintf(out, capacity, "%s%s", base, tail) > 0 &&
           std::strlen(base) + std::strlen(tail) < capacity;
}

const char *display_name(const char *path) {
    if (!path) return "";
    const char *slash = std::strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}
}

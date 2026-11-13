#pragma once

#include <cstddef>

namespace calendar {
constexpr size_t max_files = 16;
constexpr size_t max_filename = 128;
struct FileList {
    char names[max_files][max_filename];
    size_t count;
};
enum class ScanResult { ok, unavailable, too_many, name_too_long, io_error };
// Enumerates regular .ics files in one directory, without traversing folders.
// Results are sorted for a stable on-device order. On failure the list is empty.
ScanResult scan_files(const char *directory, FileList &result);
}

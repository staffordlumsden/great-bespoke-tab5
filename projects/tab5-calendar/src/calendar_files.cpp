#include "calendar_files.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace calendar {
namespace {
bool is_ics(const char *name) {
    const size_t n = std::strlen(name);
    return n > 4 && name[0] != '.' && name[n-4] == '.' &&
        std::tolower(static_cast<unsigned char>(name[n-3])) == 'i' &&
        std::tolower(static_cast<unsigned char>(name[n-2])) == 'c' &&
        std::tolower(static_cast<unsigned char>(name[n-1])) == 's';
}
}
ScanResult scan_files(const char *directory, FileList &result) {
    result.count = 0;
    if (!directory) return ScanResult::unavailable;
    DIR *dir = opendir(directory);
    if (!dir) return ScanResult::unavailable;
    ScanResult status = ScanResult::ok;
    for (;;) {
        errno = 0;
        dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) status = ScanResult::io_error;
            break;
        }
        if (!is_ics(entry->d_name)) continue;
        if (std::strlen(entry->d_name) >= max_filename) {
            status = ScanResult::name_too_long;
            break;
        }
        char path[512];
        int n = std::snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(path)) {
            status = ScanResult::name_too_long;
            break;
        }
        struct stat info{};
#ifdef ESP_PLATFORM
        // FAT has no symbolic links; newlib on the target does not expose lstat.
        if (stat(path, &info) != 0) { status = ScanResult::io_error; break; }
#else
        if (lstat(path, &info) != 0) { status = ScanResult::io_error; break; }
#endif
        if (!S_ISREG(info.st_mode)) continue;
        if (result.count == max_files) { status = ScanResult::too_many; break; }
        std::strcpy(result.names[result.count++], entry->d_name);
    }
    if (closedir(dir) != 0 && status == ScanResult::ok) status = ScanResult::io_error;
    if (status != ScanResult::ok) { result.count = 0; return status; }
    // Arrays cannot be swapped by std::sort on all supported toolchains.
    for (size_t i = 1; i < result.count; ++i) {
        char name[max_filename];
        std::strcpy(name, result.names[i]);
        size_t j = i;
        while (j && std::strcmp(result.names[j - 1], name) > 0) {
            std::strcpy(result.names[j], result.names[j - 1]);
            --j;
        }
        std::strcpy(result.names[j], name);
    }
    return status;
}
}

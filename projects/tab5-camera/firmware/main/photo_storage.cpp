#include "photo_storage.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool make_dir(const char *path) {
    if (mkdir(path, 0775) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat st{};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
bool absent(const char *path) {
    struct stat st{};
    if (stat(path, &st) == 0) return false;
    return errno == ENOENT;
}
bool date_valid(PhotoDate d) {
    return d.valid && d.year >= 2000 && d.year <= 2099 && d.month >= 1 && d.month <= 12 &&
           d.day >= 1 && d.day <= 31 && d.hour >= 0 && d.hour <= 23 &&
           d.minute >= 0 && d.minute <= 59 && d.second >= 0 && d.second <= 59;
}
}
PhotoSaveResult photo_save_jpeg(const char *root, const uint8_t *jpeg, size_t size,
                                PhotoDate date, char *relative, size_t capacity) {
    if (!relative || !capacity) return PhotoSaveResult::create;
    relative[0] = '\0';
    if (!root || !jpeg || size < 4 || jpeg[0] != 0xff || jpeg[1] != 0xd8 ||
        jpeg[size - 2] != 0xff || jpeg[size - 1] != 0xd9) return PhotoSaveResult::invalid_jpeg;
    char dcim[256], folder[256], final_path[256], temp_path[256], name[100];
    if (std::snprintf(dcim, sizeof(dcim), "%s/DCIM", root) >= (int)sizeof(dcim) ||
        std::snprintf(folder, sizeof(folder), "%s/100TAB5", dcim) >= (int)sizeof(folder) ||
        !make_dir(dcim) || !make_dir(folder)) return PhotoSaveResult::directory;
    for (unsigned sequence = 1; sequence <= 9999; ++sequence) {
        if (date_valid(date)) {
            std::snprintf(name, sizeof(name), "IMG_%04d%02d%02d_%02d%02d%02d_%04u.jpg",
                          date.year, date.month, date.day, date.hour, date.minute, date.second, sequence);
        } else std::snprintf(name, sizeof(name), "IMG_UNDATED_%04u.jpg", sequence);
        int f = std::snprintf(final_path, sizeof(final_path), "%s/%s", folder, name);
        int t = std::snprintf(temp_path, sizeof(temp_path), "%s/.%s.part", folder, name);
        int n = std::snprintf(relative, capacity, "/DCIM/100TAB5/%s", name);
        if (f < 0 || t < 0 || n < 0 || f >= (int)sizeof(final_path) ||
            t >= (int)sizeof(temp_path) || n >= (int)capacity) {
            relative[0] = '\0'; return PhotoSaveResult::create;
        }
        if (!absent(final_path) || !absent(temp_path)) continue;
        int fd = open(temp_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            relative[0] = '\0'; return PhotoSaveResult::create;
        }
        bool ok = true;
        size_t done = 0;
        while (done < size) {
            ssize_t nwrite = write(fd, jpeg + done, size - done);
            if (nwrite < 0 && errno == EINTR) continue;
            if (nwrite <= 0) { ok = false; break; }
            done += (size_t)nwrite;
        }
        if (ok && fsync(fd) != 0) ok = false;
        if (close(fd) != 0) ok = false;
        if (!ok) { unlink(temp_path); relative[0] = '\0'; return PhotoSaveResult::write; }
        fd = open(temp_path, O_RDONLY);
        if (fd < 0) { unlink(temp_path); relative[0] = '\0'; return PhotoSaveResult::readback; }
        uint8_t chunk[1024];
        done = 0;
        while (done < size) {
            size_t count = size - done < sizeof(chunk) ? size - done : sizeof(chunk);
            ssize_t got = read(fd, chunk, count);
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0 || std::memcmp(chunk, jpeg + done, (size_t)got)) { ok = false; break; }
            done += (size_t)got;
        }
        if (ok && read(fd, chunk, 1) != 0) ok = false;
        if (close(fd) != 0) ok = false;
        if (!ok || done != size) { unlink(temp_path); relative[0] = '\0'; return PhotoSaveResult::readback; }
        // FatFs f_rename refuses to replace an existing destination. The app's
        // single writer also checks before the rename; never delete that target.
        if (!absent(final_path) || rename(temp_path, final_path) != 0) {
            unlink(temp_path); relative[0] = '\0'; return PhotoSaveResult::finalise;
        }
        struct stat st{};
        if (stat(final_path, &st) != 0 || st.st_size != (off_t)size) {
            relative[0] = '\0'; return PhotoSaveResult::finalise;
        }
        return PhotoSaveResult::ok;
    }
    relative[0] = '\0'; return PhotoSaveResult::no_name;
}

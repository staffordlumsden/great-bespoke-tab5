#include "gallery_model.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
namespace {
bool path_for(char *out, size_t cap, const char *root, const char *name) {
    int n = std::snprintf(out, cap, "%s/DCIM/100TAB5/%s", root, name);
    return n >= 0 && (size_t)n < cap;
}
bool digits(const char *s, size_t n) {
    for (size_t i = 0; i < n; ++i) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}
}
bool gallery_valid_name(const char *s) {
    if (!s || std::strncmp(s, "IMG_", 4)) return false;
    size_t n = std::strlen(s);
    if (n == 28 && digits(s + 4, 8) && s[12] == '_' && digits(s + 13, 6) &&
        s[19] == '_' && digits(s + 20, 4) && !std::strcmp(s + 24, ".jpg")) return true;
    return n == 20 && !std::strncmp(s, "IMG_UNDATED_", 12) && digits(s + 12, 4) && !std::strcmp(s + 16, ".jpg");
}
GalleryResult gallery_select(const char *root, const char *current, GalleryDirection direction,
                             char *selected, size_t capacity, unsigned *total) {
    if (!root || !selected || capacity == 0 || !total ||
        (direction != GalleryDirection::latest && !gallery_valid_name(current))) return GalleryResult::invalid;
    char dir[256];
    int n = std::snprintf(dir, sizeof(dir), "%s/DCIM/100TAB5", root);
    if (n < 0 || (size_t)n >= sizeof(dir)) return GalleryResult::invalid;
    DIR *d = opendir(dir);
    if (!d) return errno == ENOENT ? GalleryResult::empty : GalleryResult::unavailable;
    char best[100]{};
    *total = 0;
    errno = 0;
    for (dirent *e = readdir(d); e; e = readdir(d)) {
        if (!gallery_valid_name(e->d_name)) continue;
        char full[256]; struct stat st{};
        if (!path_for(full, sizeof(full), root, e->d_name) || stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        ++*total;
        int cmp = direction == GalleryDirection::latest ? -1 : std::strcmp(e->d_name, current);
        bool eligible = direction == GalleryDirection::latest ||
                        (direction == GalleryDirection::older && cmp < 0) ||
                        (direction == GalleryDirection::newer && cmp > 0);
        if (!eligible) continue;
        if (!best[0] || (direction == GalleryDirection::newer ?
            std::strcmp(e->d_name, best) < 0 : std::strcmp(e->d_name, best) > 0)) {
            const size_t length = std::strlen(e->d_name);
            if (length >= sizeof(best)) continue;
            std::memcpy(best, e->d_name, length + 1);
        }
    }
    closedir(d);
    if (!*total) { selected[0] = '\0'; return GalleryResult::empty; }
    if (!best[0]) return GalleryResult::boundary;
    if (std::strlen(best) + 1 > capacity) return GalleryResult::invalid;
    std::memcpy(selected, best, std::strlen(best) + 1);
    return GalleryResult::ok;
}
GalleryResult gallery_delete(const char *root, const char *name) {
    if (!root || !gallery_valid_name(name)) return GalleryResult::invalid;
    char full[256]; struct stat st{};
    if (!path_for(full, sizeof(full), root, name)) return GalleryResult::invalid;
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) return GalleryResult::unavailable;
    return unlink(full) == 0 ? GalleryResult::ok : GalleryResult::error;
}

#pragma once
#include <cstddef>
#include <cstdint>

struct PhotoDate {
    bool valid;
    int year, month, day, hour, minute, second;
};

enum class PhotoSaveResult { ok, invalid_jpeg, directory, no_name, create, write, readback, finalise };

// root is the mounted SD root. The output path is relative to it. The caller
// owns the one storage task; existing files and interrupted temp files survive.
PhotoSaveResult photo_save_jpeg(const char *root, const uint8_t *jpeg, size_t size,
                                PhotoDate date, char *relative, size_t capacity);

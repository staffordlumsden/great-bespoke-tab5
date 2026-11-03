#pragma once
#include <cstddef>

enum class GalleryDirection { latest, older, newer };
enum class GalleryResult { ok, empty, boundary, unavailable, invalid, error };
// Scans only camera photo names; returns a bounded, lexically ordered selection.
GalleryResult gallery_select(const char *root, const char *current, GalleryDirection direction,
                             char *selected, size_t capacity, unsigned *total);
GalleryResult gallery_delete(const char *root, const char *name);
bool gallery_valid_name(const char *name);

#pragma once
#include "camera_service.h"
#include "photo_storage.h"
#include <cstddef>

struct PhotoResult {
    bool success;
    char message[48];
    char path[128];
    size_t bytes;
};

bool photo_service_start(bool sd_ready);
bool photo_capture_claim();
void photo_capture_cancel(const char *message);
bool photo_capture_submit(const CameraFrame &frame, PhotoDate date, bool monochrome);
bool photo_capture_busy();
bool photo_take_result(PhotoResult *result);

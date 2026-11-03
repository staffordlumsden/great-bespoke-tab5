#pragma once
#include <cstddef>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

struct CameraFrame {
    const uint16_t *pixels;
    uint32_t width, height, index;
};

enum CameraState { CAMERA_STARTING, CAMERA_STREAMING, CAMERA_FAILED };
void camera_start(QueueHandle_t frames, QueueHandle_t returned);
CameraState camera_state();

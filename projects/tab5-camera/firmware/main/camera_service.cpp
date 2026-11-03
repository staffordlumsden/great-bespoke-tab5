#include "camera_service.h"
#include "tab5_hal.h"
#include "esp_log.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <cinttypes>
#include <cerrno>
#include <cstring>

namespace {
constexpr const char *TAG = "camera_service";
QueueHandle_t frame_queue, return_queue;
// Only the camera task writes this state after startup; the UI reads it.
std::atomic<CameraState> state{CAMERA_STARTING};

void camera_task(void *) {
    int fd = -1;
    bool video_initialized = false, streaming = false;
    void *buffers[2] = {};
    size_t lengths[2] = {};
    uint32_t width = 0, height = 0;
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    esp_video_init_csi_config_t csi{};
    esp_video_init_config_t config{};
    v4l2_format format{};
    v4l2_requestbuffers request{};

    if (tab5_camera_power_set(true) != ESP_OK || !tab5_camera_i2c_bus()) {
        ESP_LOGE(TAG, "Cannot power camera or access board I2C");
        goto failed;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    csi.sccb_config.init_sccb = false;
    csi.sccb_config.i2c_handle = tab5_camera_i2c_bus();
    csi.sccb_config.freq = 400000;
    csi.reset_pin = GPIO_NUM_NC;
    csi.pwdn_pin = GPIO_NUM_NC;
    config.csi = &csi;
    if (esp_video_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Camera/video initialization failed");
        goto failed;
    }
    video_initialized = true;
    fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (fd < 0) { ESP_LOGE(TAG, "Open CSI failed: %d", errno); goto failed; }
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &format) != 0) { ESP_LOGE(TAG, "Get format failed: errno=%d", errno); goto failed; }
    ESP_LOGI(TAG, "Initial format: %" PRIu32 "x%" PRIu32 " fourcc=0x%08" PRIx32 " type=%" PRIu32,
             format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.pixelformat, format.type);
    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
        // esp_video's G_FMT can return a zero type even when the query used
        // VIDEO_CAPTURE. Always specify the stream type on the next ioctl.
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        if (ioctl(fd, VIDIOC_S_FMT, &format) != 0) {
            ESP_LOGE(TAG, "Set RGB565 format failed: errno=%d", errno); goto failed;
        }
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_G_FMT, &format) != 0) {
            ESP_LOGE(TAG, "Read RGB565 format failed: errno=%d", errno); goto failed;
        }
    }
    width = format.fmt.pix.width;
    height = format.fmt.pix.height;
    if (width != 1280 || height != 720 || format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565 ||
        (format.fmt.pix.bytesperline && format.fmt.pix.bytesperline != width * 2)) {
        ESP_LOGE(TAG, "Unexpected camera format %" PRIu32 "x%" PRIu32 " stride=%" PRIu32,
                 width, height, format.fmt.pix.bytesperline);
        goto failed;
    }
    request.count = 2;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &request) != 0 || request.count != 2) {
        ESP_LOGE(TAG, "Could not allocate two camera buffers"); goto failed;
    }
    for (uint32_t i = 0; i < 2; ++i) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &b) != 0 || b.length < width * height * 2) {
            ESP_LOGE(TAG, "Invalid camera buffer %" PRIu32, i); goto failed;
        }
        lengths[i] = b.length;
        buffers[i] = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        if (!buffers[i] || buffers[i] == MAP_FAILED) { buffers[i] = nullptr; ESP_LOGE(TAG, "mmap failed"); goto failed; }
        if (ioctl(fd, VIDIOC_QBUF, &b) != 0) { ESP_LOGE(TAG, "Queue failed"); goto failed; }
    }
    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) { ESP_LOGE(TAG, "Stream start failed"); goto failed; }
    streaming = true;
    ESP_LOGI(TAG, "CSI stream started %" PRIu32 "x%" PRIu32 " RGB565, two mapped buffers", width, height);
    for (;;) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_DQBUF, &b) != 0 || b.index >= 2) {
            ESP_LOGE(TAG, "Frame dequeue failed"); goto failed;
        }
        if (!(b.flags & V4L2_BUF_FLAG_DONE) || b.bytesused < width * height * 2 || b.bytesused > lengths[b.index]) {
            ESP_LOGW(TAG, "Discarding incomplete camera frame (%" PRIu32 " bytes)", b.bytesused);
            if (ioctl(fd, VIDIOC_QBUF, &b) != 0) { ESP_LOGE(TAG, "Frame requeue failed"); goto failed; }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (state != CAMERA_STREAMING) {
            state = CAMERA_STREAMING;
            ESP_LOGI(TAG, "First complete camera frame received");
        }
        CameraFrame frame{static_cast<const uint16_t *>(buffers[b.index]), width, height, b.index};
        if (xQueueSend(frame_queue, &frame, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "Preview queue stalled"); goto failed;
        }
        uint32_t returned = UINT32_MAX;
        if (xQueueReceive(return_queue, &returned, portMAX_DELAY) != pdTRUE || returned != b.index) {
            ESP_LOGE(TAG, "Preview buffer was not returned"); goto failed;
        }
        if (ioctl(fd, VIDIOC_QBUF, &b) != 0) { ESP_LOGE(TAG, "Frame requeue failed"); goto failed; }
    }
failed:
    state = CAMERA_FAILED;
    if (streaming) ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < 2; ++i) if (buffers[i]) munmap(buffers[i], lengths[i]);
    if (fd >= 0) close(fd);
    if (video_initialized) esp_video_deinit();
    tab5_camera_power_set(false);
    vTaskDelete(nullptr);
}
}

void camera_start(QueueHandle_t frames, QueueHandle_t returned) {
    frame_queue = frames;
    return_queue = returned;
    if (!frames || !returned || xTaskCreate(camera_task, "camera", 8192, nullptr, 5, nullptr) != pdPASS) {
        state = CAMERA_FAILED;
        ESP_LOGE(TAG, "Unable to create camera task/queues");
    }
}
CameraState camera_state() { return state.load(); }

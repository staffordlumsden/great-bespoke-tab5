#include "photo_service.h"
#include "tab5_hal.h"
#include "image_effects.h"
#include "driver/jpeg_encode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr const char *TAG = "photo_capture";
constexpr size_t RAW_SIZE = 720U * 1280U * 2U;
constexpr size_t JPEG_CAPACITY = 4U * 1024U * 1024U;
constexpr uint64_t SPACE_MARGIN = 128U * 1024U;
uint8_t *raw = nullptr, *jpeg = nullptr;
size_t raw_size = 0, jpeg_capacity = 0;
jpeg_encoder_handle_t encoder = nullptr;
QueueHandle_t requests = nullptr, results = nullptr;
std::atomic<bool> busy{false};
bool ready = false;
struct PhotoRequest { PhotoDate date; bool monochrome; };

void complete(bool success, const char *message, const char *path = "", size_t bytes = 0) {
    PhotoResult r{};
    r.success = success;
    std::snprintf(r.message, sizeof(r.message), "%s", message);
    std::snprintf(r.path, sizeof(r.path), "%s", path);
    r.bytes = bytes;
    if (results) xQueueOverwrite(results, &r);
    busy.store(false);
}
const char *save_message(PhotoSaveResult result) {
    switch (result) {
    case PhotoSaveResult::ok: return "SAVED";
    case PhotoSaveResult::invalid_jpeg: return "JPEG INVALID";
    case PhotoSaveResult::directory: return "SD FOLDER ERROR";
    case PhotoSaveResult::no_name: return "NO FREE FILENAME";
    case PhotoSaveResult::create: return "SD CREATE ERROR";
    case PhotoSaveResult::write: return "SD WRITE ERROR";
    case PhotoSaveResult::readback: return "SD VERIFY ERROR";
    case PhotoSaveResult::finalise: return "SD FINALISE ERROR";
    }
    return "SAVE FAILED";
}
void capture_task(void *) {
    for (;;) {
        PhotoRequest request{};
        if (xQueueReceive(requests, &request, portMAX_DELAY) != pdTRUE) continue;
        uint64_t total = 0, free_bytes = 0;
        if (!tab5_sd_usage(&total, &free_bytes) || free_bytes < SPACE_MARGIN) {
            complete(false, "SD MISSING OR FULL");
            continue;
        }
        jpeg_encode_cfg_t config{};
        config.height = 1280;
        config.width = 720;
        config.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
        config.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
        config.image_quality = 80;
        config.pixel_reverse = false;
        uint32_t encoded = 0;
        int64_t start = esp_timer_get_time();
        if (request.monochrome) tab5_mono_buffer(reinterpret_cast<uint16_t *>(raw), RAW_SIZE / 2);
        esp_err_t err = jpeg_encoder_process(encoder, &config, raw, RAW_SIZE,
                                               jpeg, jpeg_capacity, &encoded);
        if (err != ESP_OK || encoded < 4 || encoded > jpeg_capacity) {
            ESP_LOGE(TAG, "JPEG encoding failed: %s size=%u", esp_err_to_name(err), (unsigned)encoded);
            complete(false, "JPEG ENCODE FAILED");
            continue;
        }
        if (!tab5_sd_usage(&total, &free_bytes) || free_bytes < (uint64_t)encoded + SPACE_MARGIN) {
            complete(false, "SD MISSING OR FULL");
            continue;
        }
        char path[128]{};
        PhotoSaveResult result = photo_save_jpeg("/sdcard", jpeg, encoded, request.date, path, sizeof(path));
        if (result != PhotoSaveResult::ok) {
            ESP_LOGE(TAG, "Photo save failed: %s", save_message(result));
            complete(false, save_message(result));
            continue;
        }
        ESP_LOGI(TAG, "Saved %s (%u bytes) in %lld ms", path, (unsigned)encoded,
                 (long long)((esp_timer_get_time() - start) / 1000));
        complete(true, "SAVED", path, encoded);
    }
}
}

bool photo_service_start(bool sd_ready) {
    if (!sd_ready) return false;
    jpeg_encode_memory_alloc_cfg_t alloc{};
    // The raw input is written by PPA, so it needs DMA output alignment.
    alloc.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER;
    raw = static_cast<uint8_t *>(jpeg_alloc_encoder_mem(RAW_SIZE, &alloc, &raw_size));
    jpeg = static_cast<uint8_t *>(jpeg_alloc_encoder_mem(JPEG_CAPACITY, &alloc, &jpeg_capacity));
    jpeg_encode_engine_cfg_t config{};
    config.timeout_ms = 3000;
    if (!raw || !jpeg || jpeg_new_encoder_engine(&config, &encoder) != ESP_OK) goto fail;
    requests = xQueueCreate(1, sizeof(PhotoRequest));
    results = xQueueCreate(1, sizeof(PhotoResult));
    if (!requests || !results || xTaskCreate(capture_task, "photo_save", 8192, nullptr, 4, nullptr) != pdPASS) goto fail;
    ready = true;
    ESP_LOGI(TAG, "JPEG capture ready: raw=%u output=%u bytes", (unsigned)raw_size, (unsigned)jpeg_capacity);
    return true;
fail:
    ESP_LOGE(TAG, "Unable to initialize JPEG capture resources");
    if (requests) vQueueDelete(requests);
    if (results) vQueueDelete(results);
    requests = nullptr; results = nullptr;
    if (encoder) jpeg_del_encoder_engine(encoder);
    encoder = nullptr;
    free(raw); free(jpeg); raw = nullptr; jpeg = nullptr;
    return false;
}
bool photo_capture_claim() {
    bool expected = false;
    return ready && busy.compare_exchange_strong(expected, true);
}
void photo_capture_cancel(const char *message) { complete(false, message); }
bool photo_capture_submit(const CameraFrame &frame, PhotoDate date, bool monochrome) {
    if (!ready || !busy.load() || !frame.pixels) return false;
    esp_err_t err = tab5_camera_capture_rotate(frame.pixels, frame.width, frame.height,
                                                reinterpret_cast<uint16_t *>(raw), raw_size);
    if (err != ESP_OK) { complete(false, "FRAME ROTATION FAILED"); return false; }
    PhotoRequest request{date, monochrome};
    if (xQueueSend(requests, &request, 0) != pdTRUE) { complete(false, "CAPTURE QUEUE ERROR"); return false; }
    return true;
}
bool photo_capture_busy() { return busy.load(); }
bool photo_take_result(PhotoResult *result) {
    return result && results && xQueueReceive(results, result, 0) == pdTRUE;
}

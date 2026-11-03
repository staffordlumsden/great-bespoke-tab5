#include "gallery_image.h"
#include "gallery_model.h"
#include "driver/jpeg_decode.h"
#include "esp_log.h"
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

namespace {
constexpr const char *TAG = "gallery";
constexpr size_t RAW_SIZE = 720U * 1280U * 2U;
constexpr size_t MAX_JPEG_SIZE = 4U * 1024U * 1024U;
jpeg_decoder_handle_t decoder = nullptr;
uint8_t *decoded = nullptr;
size_t decoded_capacity = 0;
}
const uint16_t *gallery_load_image(const char *name) {
    if (!gallery_valid_name(name)) return nullptr;
    char path[256];
    int n = std::snprintf(path, sizeof(path), "/sdcard/DCIM/100TAB5/%s", name);
    if (n < 0 || n >= (int)sizeof(path)) return nullptr;
    struct stat st{};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 4 || (size_t)st.st_size > MAX_JPEG_SIZE) return nullptr;
    jpeg_decode_memory_alloc_cfg_t cfg{};
    cfg.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
    size_t input_capacity = 0;
    auto *input = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(st.st_size, &cfg, &input_capacity));
    if (!input || input_capacity < (size_t)st.st_size) { free(input); return nullptr; }
    FILE *fp = std::fopen(path, "rb");
    if (!fp) { free(input); return nullptr; }
    size_t count = std::fread(input, 1, st.st_size, fp);
    bool file_ok = count == (size_t)st.st_size && !std::ferror(fp);
    if (std::fclose(fp) != 0) file_ok = false;
    jpeg_decode_picture_info_t info{};
    if (!file_ok || jpeg_decoder_get_info(input, count, &info) != ESP_OK || info.width != 720 || info.height != 1280) {
        ESP_LOGW(TAG, "Invalid or unsupported camera image"); free(input); return nullptr;
    }
    if (!decoded) {
        cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
        decoded = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(RAW_SIZE, &cfg, &decoded_capacity));
        if (!decoded || decoded_capacity < RAW_SIZE) { free(input); gallery_close_image(); return nullptr; }
    }
    if (!decoder) {
        jpeg_decode_engine_cfg_t config{}; config.timeout_ms = 3000;
        if (jpeg_new_decoder_engine(&config, &decoder) != ESP_OK) { free(input); gallery_close_image(); return nullptr; }
    }
    jpeg_decode_cfg_t config{};
    config.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    // On the P4 JPEG decoder, BGR selects native little-endian RGB565
    // packing for the Tab5 display; RGB scrambles the two bytes per pixel.
    config.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
    config.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
    uint32_t produced = 0;
    esp_err_t result = jpeg_decoder_process(decoder, &config, input, count, decoded, decoded_capacity, &produced);
    free(input);
    if (result != ESP_OK || produced < RAW_SIZE) {
        ESP_LOGW(TAG, "Decode failed: %s, bytes=%u", esp_err_to_name(result), (unsigned)produced);
        return nullptr;
    }
    ESP_LOGI(TAG, "Decoded %s (%u bytes) to 720x1280 RGB565", name, (unsigned)count);
    return reinterpret_cast<const uint16_t *>(decoded);
}
void gallery_close_image() {
    if (decoder) jpeg_del_decoder_engine(decoder);
    decoder = nullptr;
    free(decoded); decoded = nullptr; decoded_capacity = 0;
}

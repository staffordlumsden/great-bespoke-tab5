#include "camera_service.h"
#include "gallery_image.h"
#include "gallery_model.h"
#include "photo_service.h"
#include "tab5_app_boot.h"
#include "tab5_hal.h"
#include "pixel_fonts.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstdio>
#include <cstring>

namespace {
constexpr const char *TAG = "tab5_camera";
constexpr int VX = 90, VY = 76, VW = 540, VH = 960;
constexpr int RX = 20, RY = 1160, RW = 210, RH = 82;
constexpr int SX = 253, SY = 1160, SW = 197, SH = 82;
constexpr int GRID_X = 155, MONO_X = 298, GALLERY_X = 445, HEADER_Y = 12, HEADER_H = 52;
constexpr int HEADER_W = 132, GALLERY_W = 260;
constexpr int BTN_Y = 1160, BTN_H = 82, BTN_W = 165;
enum class Screen { camera, gallery, confirm_delete };
bool grid_on = false, mono_on = false;
Screen screen = Screen::camera;
char gallery_name[100]{};
unsigned gallery_count = 0;
const uint16_t *gallery_pixels = nullptr;

void text(const char *s, int x, int y, uint32_t color, int scale) {
    const auto &f = cyd_fonts::pixelfont_7x5_thin;
    for (; *s; ++s) {
        unsigned c = static_cast<unsigned char>(*s);
        if (c < f.first || c > f.last) c = '?';
        const auto &g = f.glyphs[c - f.first];
        for (unsigned gy = 0; gy < g.height; ++gy) for (unsigned gx = 0; gx < g.width; ++gx) {
            unsigned bit = gy * g.width + gx;
            if (f.bitmap[g.offset + bit / 8] & (0x80 >> (bit % 8)))
                tab5_display_rect(x + (g.x + static_cast<int>(gx)) * scale,
                    y + (f.ascent + g.y + static_cast<int>(gy)) * scale, scale, scale, color);
        }
        x += (g.advance + 1) * scale;
    }
}
void button(int x, int y, int w, int h, const char *label, uint32_t bg, uint32_t fg, int scale = 2) {
    tab5_display_rect(x, y, w, h, bg);
    text(label, x + 12, y + (h - 7 * scale) / 2, fg, scale);
}
bool inside(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}
void load_settings() {
    nvs_handle_t h = 0; uint8_t flags = 0;
    if (nvs_open("tab5camera", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "settings", &flags);
        nvs_close(h);
    }
    grid_on = (flags & 1U) != 0;
    mono_on = (flags & 2U) != 0;
}
bool save_settings() {
    nvs_handle_t h = 0;
    if (nvs_open("tab5camera", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t result = nvs_set_u8(h, "settings", (grid_on ? 1U : 0U) | (mono_on ? 2U : 0U));
    if (result == ESP_OK) result = nvs_commit(h);
    nvs_close(h);
    return result == ESP_OK;
}
void draw_header() {
    tab5_display_rect(0, 0, 720, 76, 0x101217);
    text("CAMERA", 12, 28, 0xffffff, 3);
    button(GRID_X, HEADER_Y, HEADER_W, HEADER_H, grid_on ? "GRID ON" : "GRID OFF", grid_on ? 0x24543d : 0x29313a, 0xffffff);
    button(MONO_X, HEADER_Y, HEADER_W, HEADER_H, mono_on ? "MONO ON" : "MONO OFF", mono_on ? 0x24543d : 0x29313a, 0xffffff);
    button(GALLERY_X, HEADER_Y, GALLERY_W, HEADER_H, "GALLERY", 0x29313a, 0xffffff, 3);
}
void render_chrome(bool sd) {
    tab5_display_fill_rgb(0x101217);
    draw_header();
    tab5_display_rect(VX, VY, VW, VH, 0x20242b);
    text("STARTING CAMERA", 175, 500, 0xcbd5e1, 4);
    button(RX, RY, RW, RH, "CONSOLE", 0x29313a, 0xffffff, 3);
    button(SX, SY, SW, SH, "CAPTURE", 0xe5e7eb, 0x111827, 3);
    text(sd ? "SD READY" : "SD NOT READY", 480, 1170, sd ? 0x86efac : 0xfca5a5, 2);
    text("OFFLINE", 480, 1204, 0xaab3be, 2);
    text("CAMERA 0.2.1-DEV", 22, 1090, 0x9ca3af, 2);
}
void show_status(const char *line1, const char *line2, uint32_t color) {
    tab5_display_rect(0, 1045, 720, 100, 0x101217);
    text(line1, 22, 1060, color, 3);
    if (line2 && *line2) text(line2, 22, 1100, 0xcbd5e1, 2);
    tab5_display_present_region(0, 1045, 720, 100);
}
void draw_grid() {
    constexpr uint32_t line = 0xe5e7eb;
    tab5_display_rect(VX + VW / 3, VY, 2, VH, line);
    tab5_display_rect(VX + 2 * VW / 3, VY, 2, VH, line);
    tab5_display_rect(VX, VY + VH / 3, VW, 2, line);
    tab5_display_rect(VX, VY + 2 * VH / 3, VW, 2, line);
}
void gallery_load_selected() {
    gallery_pixels = gallery_name[0] ? gallery_load_image(gallery_name) : nullptr;
}
void gallery_render() {
    tab5_display_fill_rgb(0x101217);
    text("PHOTO GALLERY", 20, 23, 0xffffff, 4);
    tab5_display_rect(VX, VY, VW, VH, 0x20242b);
    if (gallery_pixels) {
        if (tab5_camera_photo_blit(gallery_pixels, 720, 1280, VX, VY, VW, VH) != ESP_OK)
            text("IMAGE DISPLAY ERROR", 160, 500, 0xfca5a5, 3);
    } else text(gallery_name[0] ? "CANNOT OPEN PHOTO" : "NO CAMERA PHOTOS", 170, 500, 0xcbd5e1, 3);
    char count[48]; std::snprintf(count, sizeof(count), "%u PHOTO%s", gallery_count, gallery_count == 1 ? "" : "S");
    text(count, 22, 1055, 0xe5e7eb, 3);
    if (gallery_name[0]) text(gallery_name, 22, 1100, 0xcbd5e1, 2);
    button(8, BTN_Y, BTN_W, BTN_H, "BACK", 0x29313a, 0xffffff, 3);
    button(187, BTN_Y, BTN_W, BTN_H, "PREV", 0x29313a, 0xffffff, 3);
    button(366, BTN_Y, BTN_W, BTN_H, "NEXT", 0x29313a, 0xffffff, 3);
    button(545, BTN_Y, BTN_W, BTN_H, "DELETE", 0x6f262b, 0xffffff, 3);
    tab5_display_present();
}
void gallery_choose(GalleryDirection direction) {
    char candidate[sizeof(gallery_name)]{};
    unsigned total = 0;
    GalleryResult result = gallery_select("/sdcard", gallery_name, direction, candidate, sizeof(candidate), &total);
    if (result == GalleryResult::ok) {
        std::snprintf(gallery_name, sizeof(gallery_name), "%s", candidate);
        gallery_count = total;
        gallery_load_selected();
    } else if (result == GalleryResult::empty) {
        gallery_name[0] = '\0'; gallery_pixels = nullptr; gallery_count = 0;
    } else if (result != GalleryResult::boundary) {
        ESP_LOGW(TAG, "Gallery scan failed: %d", (int)result);
        gallery_name[0] = '\0'; gallery_pixels = nullptr; gallery_count = 0;
    }
    gallery_render();
}
void confirm_render() {
    tab5_display_fill_rgb(0x101217);
    text("DELETE PHOTO?", 125, 380, 0xffffff, 4);
    text(gallery_name, 50, 450, 0xcbd5e1, 2);
    text("THIS CANNOT BE UNDONE", 120, 505, 0xfca5a5, 3);
    button(70, 650, 250, 100, "CANCEL", 0x29313a, 0xffffff, 4);
    button(400, 650, 250, 100, "DELETE", 0x7f1d1d, 0xffffff, 4);
    tab5_display_present();
}
}
extern "C" void app_main() {
    ESP_LOGI(TAG, "Tab5Camera 0.2.1-dev, reset reason=%d", (int)esp_reset_reason());
    esp_err_t nvs = nvs_flash_init();
    if (nvs != ESP_OK) ESP_LOGW(TAG, "NVS initialization: %s; preserving contents", esp_err_to_name(nvs));
    load_settings();
    ESP_LOGI(TAG, "Settings loaded grid=%d monochrome=%d", grid_on, mono_on);
    auto board = tab5_board_init();
    ESP_LOGI(TAG, "display=%d touch=%d sd=%d PSRAM free=%u", board.display_ready,
             board.touch_ready, board.sd_ready, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (!board.display_ready) { ESP_LOGE(TAG, "No display; leaving OTA unconfirmed"); return; }
    tab5_display_set_rotation(TAB5_ROTATION_270);
    render_chrome(board.sd_ready);
    esp_err_t presented = tab5_display_present();
    esp_err_t boot = tab5_app_boot_ready(board.display_ready && board.touch_ready && presented == ESP_OK);
    if (boot != ESP_OK) ESP_LOGW(TAG, "Boot recovery: %s", esp_err_to_name(boot));
    if (presented != ESP_OK) return;
    QueueHandle_t frames = xQueueCreate(1, sizeof(CameraFrame));
    QueueHandle_t returned = xQueueCreate(1, sizeof(uint32_t));
    camera_start(frames, returned);
    const bool capture_ready = photo_service_start(board.sd_ready);
    if (!capture_ready) show_status(board.sd_ready ? "CAPTURE UNAVAILABLE" : "SD NOT READY", "", 0xfca5a5);
    bool last_touch = false, failure_shown = false, capture_pending = false, capture_mono = false;
    int64_t pending_since = 0, fps_start = esp_timer_get_time();
    PhotoDate photo_date{};
    uint32_t frame_count = 0;
    for (;;) {
        PhotoResult result{};
        if (photo_take_result(&result) && screen == Screen::camera) {
            const char *name = std::strrchr(result.path, '/');
            show_status(result.message, name ? name + 1 : "", result.success ? 0x86efac : 0xfca5a5);
        }
        int x = 0, y = 0;
        bool touched = tab5_touch_poll(&x, &y);
        if (touched && !last_touch) {
            if (screen == Screen::camera) {
                if (inside(x,y,RX,RY,RW,RH)) {
                    if (photo_capture_busy()) show_status("PLEASE WAIT", "SAVING PHOTO", 0xfde68a);
                    else tab5_app_boot_return_to_console();
                } else if (inside(x,y,SX,SY,SW,SH)) {
                    if (!capture_ready) show_status(board.sd_ready ? "CAPTURE UNAVAILABLE" : "SD NOT READY", "", 0xfca5a5);
                    else if (camera_state() != CAMERA_STREAMING) show_status("CAMERA NOT READY", "", 0xfca5a5);
                    else if (photo_capture_claim()) {
                        tab5_clock_time now{}; bool valid = tab5_rtc_read(&now);
                        photo_date = {valid && now.valid, now.year, now.month, now.day, now.hour, now.minute, now.second};
                        capture_mono = mono_on; capture_pending = true; pending_since = esp_timer_get_time();
                        show_status("CAPTURING", "PLEASE HOLD STILL", 0xfde68a);
                    }
                } else if (inside(x,y,GRID_X,HEADER_Y,HEADER_W,HEADER_H) || inside(x,y,MONO_X,HEADER_Y,HEADER_W,HEADER_H)) {
                    if (photo_capture_busy()) show_status("PLEASE WAIT", "SAVING PHOTO", 0xfde68a);
                    else {
                        if (inside(x,y,GRID_X,HEADER_Y,HEADER_W,HEADER_H)) grid_on = !grid_on; else mono_on = !mono_on;
                        bool saved = save_settings(); draw_header(); tab5_display_present_region(0, 0, 720, 76);
                        if (!saved) show_status("SETTINGS NOT SAVED", "", 0xfca5a5);
                        ESP_LOGI(TAG, "Settings grid=%d monochrome=%d saved=%d", grid_on, mono_on, saved);
                    }
                } else if (inside(x,y,GALLERY_X,HEADER_Y,GALLERY_W,HEADER_H)) {
                    if (photo_capture_busy()) show_status("PLEASE WAIT", "SAVING PHOTO", 0xfde68a);
                    else { screen = Screen::gallery; gallery_choose(GalleryDirection::latest); }
                }
            } else if (screen == Screen::gallery) {
                if (inside(x,y,8,BTN_Y,BTN_W,BTN_H)) {
                    gallery_close_image(); gallery_pixels = nullptr; screen = Screen::camera;
                    fps_start = esp_timer_get_time(); frame_count = 0;
                    render_chrome(board.sd_ready); tab5_display_present();
                } else if (inside(x,y,187,BTN_Y,BTN_W,BTN_H) && gallery_name[0]) gallery_choose(GalleryDirection::older);
                else if (inside(x,y,366,BTN_Y,BTN_W,BTN_H) && gallery_name[0]) gallery_choose(GalleryDirection::newer);
                else if (inside(x,y,545,BTN_Y,BTN_W,BTN_H) && gallery_name[0]) { screen = Screen::confirm_delete; confirm_render(); }
            } else {
                if (inside(x,y,70,650,250,100)) { screen = Screen::gallery; gallery_render(); }
                else if (inside(x,y,400,650,250,100)) {
                    GalleryResult deleted = gallery_delete("/sdcard", gallery_name);
                    ESP_LOGI(TAG, "Gallery delete result=%d", (int)deleted);
                    screen = Screen::gallery;
                    if (deleted == GalleryResult::ok) gallery_choose(GalleryDirection::latest);
                    else { gallery_render(); text("DELETE FAILED", 22, 1130, 0xfca5a5, 2); tab5_display_present_region(0, 1110, 720, 40); }
                }
            }
        }
        last_touch = touched;
        CameraFrame frame{};
        if (frames && xQueueReceive(frames, &frame, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (screen == Screen::camera) {
                esp_err_t err = tab5_camera_preview_blit(frame.pixels, frame.width, frame.height, VX, VY, VW, VH, mono_on);
                if (err == ESP_OK && grid_on) draw_grid();
                if (err == ESP_OK) err = tab5_display_present_region(VX, VY, VW, VH);
                if (err != ESP_OK) ESP_LOGE(TAG, "Preview presentation: %s", esp_err_to_name(err));
                if (capture_pending) {
                    capture_pending = false;
                    if (photo_capture_submit(frame, photo_date, capture_mono)) show_status("SAVING PHOTO", "PLEASE WAIT", 0xfde68a);
                }
                if (err == ESP_OK) ++frame_count;
                int64_t now = esp_timer_get_time();
                if (now - fps_start >= 5000000) {
                    unsigned tenths = (unsigned)((uint64_t)frame_count * 10000000ULL / (uint64_t)(now - fps_start));
                    ESP_LOGI(TAG, "Preview throughput %u.%u fps (%u frames), mono=%d", tenths / 10, tenths % 10, (unsigned)frame_count, mono_on);
                    fps_start = now; frame_count = 0;
                }
            }
            xQueueSend(returned, &frame.index, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(1));
        } else if (camera_state() == CAMERA_FAILED && !failure_shown && screen == Screen::camera) {
            tab5_display_rect(VX, VY, VW, VH, 0x20242b);
            text("CAMERA NOT", 200, 495, 0xfca5a5, 4); text("AVAILABLE", 210, 535, 0xfca5a5, 4);
            text("SEE SERIAL LOG FOR DETAILS", 175, 580, 0xcbd5e1, 2);
            tab5_display_present_region(VX, VY, VW, VH); failure_shown = true;
        }
        if (capture_pending && (camera_state() == CAMERA_FAILED || esp_timer_get_time() - pending_since > 5000000)) {
            capture_pending = false; photo_capture_cancel("CAMERA FRAME TIMEOUT");
        }
        if (!frames) vTaskDelay(pdMS_TO_TICKS(20));
    }
}

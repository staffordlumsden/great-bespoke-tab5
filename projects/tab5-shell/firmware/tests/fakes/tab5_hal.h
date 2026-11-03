#pragma once
#include <cstdint>
using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
inline constexpr esp_err_t ESP_ERR_TIMEOUT = 0x107;
inline constexpr int TAB5_DISPLAY_WIDTH = 1280;
inline constexpr int TAB5_DISPLAY_HEIGHT = 720;
enum tab5_display_rotation_t { TAB5_ROTATION_0 };
esp_err_t tab5_display_set_rotation(tab5_display_rotation_t);
esp_err_t tab5_display_rect(int x,int y,int width,int height,std::uint32_t rgb);
esp_err_t tab5_display_present_region(int x,int y,int width,int height);
esp_err_t tab5_display_present();

#pragma once

namespace calendar::layout {
constexpr int screen_width = 1280;
constexpr int screen_height = 720;
constexpr int toolbar_y = 648;
constexpr int button_y = 650;
constexpr int button_height = 66;
constexpr int button_start_x = 8;
constexpr int button_stride = 182;
constexpr int button_width = 174;
constexpr int button_count = 7;
constexpr int large_text_scale = 3; // Seven-pixel PixelFont: 21-pixel glyphs.

inline int toolbar_action(int x, int y) {
    if (x < 0 || x >= screen_width || y < toolbar_y || y >= screen_height) return -1;
    if (x < button_start_x) return 0;
    int index = (x - button_start_x) / button_stride;
    return index < button_count ? index : button_count - 1;
}
}

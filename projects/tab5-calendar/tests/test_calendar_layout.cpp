#include "../src/calendar_layout.h"
#include "../firmware/main/fonts/pixel_fonts.h"
#include <cassert>
static int text_width(const char *s) {
    const auto &font = cyd_fonts::pixelfont_7x5_thin;
    int width = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p)
        width += (font.glyphs[*p - font.first].advance + 1) * calendar::layout::large_text_scale;
    return width;
}
int main() {
    using namespace calendar::layout;
    static_assert(large_text_scale * 7 == 21);
    static_assert(button_height > 48);
    assert(toolbar_action(0, toolbar_y) == 0);
    assert(toolbar_action(1280 - 1, 719) == 6);
    for (int i = 0; i < button_count; ++i) {
        assert(toolbar_action(button_start_x + i * button_stride + 20, button_y) == i);
        assert(toolbar_action(button_start_x + i * button_stride + 20, 715) == i);
    }
    assert(toolbar_action(100, toolbar_y - 1) == -1);
    assert(toolbar_action(-1, toolbar_y) == -1);
    assert(toolbar_action(1280, toolbar_y) == -1);
    assert(text_width("COLOUR >") <= 150);
    assert(text_width("NETWORK/TLS ERROR") <= 320);
    assert(text_width("WED 30 Sep") <= 232);
    assert(text_width("NEXT YEAR - LIST LIMITED TO 3000") <= 570);
}

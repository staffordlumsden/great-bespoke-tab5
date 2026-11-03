#include "matrix_clock.hpp"
#include "matrix_model.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pixel_fonts.h"
#include "tab5_hal.h"

namespace tab5 {
namespace {

constexpr uint32_t BG       = 0x050a05;
constexpr uint32_t DIM      = 0x0f3d0f;
constexpr uint32_t GREEN    = 0x22c55e;
constexpr uint32_t BRIGHT   = 0x86efac;
constexpr uint32_t PANEL    = 0x0a1f0a;
constexpr uint32_t PANEL_EDGE = 0x166534;
constexpr uint32_t CLOCK_TEXT = 0xdcfce7;

constexpr int CELL_W    = matrix::cell_width;
constexpr int CELL_H    = matrix::cell_height;
constexpr int SCALE     = 2;
constexpr int FRAME_US  = 83000;  // CYD's ~12 fps animation cadence
matrix::Rain rain;

int screen_w() { return tab5_display_width(); }
int screen_h() { return tab5_display_height(); }

void fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(screen_w(), x + w);
    int y1 = std::min(screen_h(), y + h);
    if (x0 < x1 && y0 < y1) tab5_display_rect(x0, y0, x1 - x0, y1 - y0, color);
}

const cyd_fonts::Glyph &font_glyph(char c, const cyd_fonts::Font &font = cyd_fonts::pixelfont_7x5_thin)
{
    unsigned value = static_cast<unsigned char>(c);
    if (value < font.first || value > font.last) value = '?';
    return font.glyphs[value - font.first];
}

void draw_glyph(char c, int x, int y, uint32_t color)
{
    const auto &font = cyd_fonts::pixelfont_7x5_thin;
    const auto &g = font_glyph(c);
    int baseline = y + font.ascent * SCALE;
    for (unsigned gy = 0; gy < g.height; ++gy) {
        for (unsigned gx = 0; gx < g.width; ++gx) {
            unsigned bit = gy * g.width + gx;
            if (font.bitmap[g.offset + bit / 8] & (0x80 >> (bit % 8))) {
                fill_rect(x + (g.x + static_cast<int>(gx)) * SCALE,
                          baseline + (g.y + static_cast<int>(gy)) * SCALE,
                          SCALE, SCALE, color);
            }
        }
    }
}

void draw_centered_text(const char *text, int cx, int y, const cyd_fonts::Font &font,
                        uint32_t color, int scale)
{
    int width = 0;
    for (const char *p = text; *p; ++p) {
        unsigned value = static_cast<unsigned char>(*p);
        if (value < font.first || value > font.last) value = '?';
        width += (font.glyphs[value - font.first].advance + 1) * scale;
    }
    if (width) width -= scale;
    int x = cx - width / 2;
    int baseline = y + font.ascent * scale;
    for (const char *p = text; *p; ++p) {
        const auto &g = font_glyph(*p, font);
        for (unsigned gy = 0; gy < g.height; ++gy) {
            for (unsigned gx = 0; gx < g.width; ++gx) {
                unsigned bit = gy * g.width + gx;
                if (font.bitmap[g.offset + bit / 8] & (0x80 >> (bit % 8))) {
                    fill_rect(x + (g.x + static_cast<int>(gx)) * scale,
                              baseline + (g.y + static_cast<int>(gy)) * scale,
                              scale, scale, color);
                }
            }
        }
        x += (g.advance + 1) * scale;
    }
}

uint32_t intensity_color(uint8_t intensity)
{
    switch (intensity) {
        case 1: return DIM;
        case 2: return GREEN;
        case 3: return BRIGHT;
        default: return BG;
    }
}

void draw_rain(int cols, int rows)
{
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            const auto &column = rain.columns[c];
            const int intensity = matrix::Rain::intensity(column, r);
            if (!intensity) continue;
            draw_glyph(column.glyphs[r], c * CELL_W, r * CELL_H, intensity_color(intensity));
        }
    }
}

void draw_clock_panel()
{
    tab5_clock_time now{};
    char time_str[16] = "--:--", date_str[32] = "CLOCK NOT SET";
    if (tab5_rtc_read(&now)) {
        std::snprintf(time_str, sizeof(time_str), "%02d:%02d", now.hour, now.minute);
        std::snprintf(date_str, sizeof(date_str), "%02d/%02d/%04d", now.day, now.month, now.year);
    }

    // Measure large time text.
    const auto &big = cyd_fonts::pixelfont_13x7_semibold;
    int time_w = 0;
    for (const char *p = time_str; *p; ++p) {
        unsigned value = static_cast<unsigned char>(*p);
        if (value < big.first || value > big.last) value = '?';
        time_w += (big.glyphs[value - big.first].advance + 1) * 4;
    }
    if (time_w) time_w -= 4;

    const int pad = 28;
    const int panel_w = std::max(time_w, 420) + pad * 2;
    const int panel_h = 170;
    const int px = (screen_w() - panel_w) / 2;
    const int py = (screen_h() - panel_h) / 2;

    fill_rect(px, py, panel_w, panel_h, PANEL);
    fill_rect(px, py, panel_w, 4, PANEL_EDGE);
    fill_rect(px, py + panel_h - 4, panel_w, 4, PANEL_EDGE);
    fill_rect(px, py, 4, panel_h, PANEL_EDGE);
    fill_rect(px + panel_w - 4, py, 4, panel_h, PANEL_EDGE);

    draw_centered_text(time_str, screen_w() / 2, py + 22, big, CLOCK_TEXT, 4);
    draw_centered_text(date_str, screen_w() / 2, py + 102,
                       cyd_fonts::pixelfont_11x5_thin, GREEN, 2);
    draw_centered_text("DOUBLE TAP TO UNLOCK", screen_w() / 2, py + 140,
                       cyd_fonts::pixelfont_7x5_thin, BRIGHT, 2);
}

void draw_keypad(const matrix::Lock &lock, uint64_t now)
{
    fill_rect(0, 0, screen_w(), screen_h(), BG);
    matrix::Keypad layout(screen_w(), screen_h());
    const bool wrong = now < lock.wrong_until;
    draw_centered_text(wrong ? "WRONG PIN - TRY AGAIN" : "ENTER PIN", screen_w()/2,
                       layout.top, cyd_fonts::pixelfont_11x5_thin,
                       wrong ? 0xef4444 : CLOCK_TEXT, 2);
    char masked[5] = "____";
    for (int i=0; i<lock.count; ++i) masked[i]='*';
    draw_centered_text(masked, screen_w()/2, layout.top+58,
                       cyd_fonts::pixelfont_13x7_semibold, CLOCK_TEXT, 3);
    constexpr const char *labels[] = {"1","2","3","4","5","6","7","8","9","BACK","0","OK"};
    for (int i=0; i<13; ++i) {
        auto box = i==12 ? layout.cancel() : layout.key(i);
        const bool enabled = !wrong && (i!=11 || lock.count==4);
        fill_rect(box.x,box.y,box.w,box.h,enabled ? PANEL_EDGE : PANEL);
        draw_centered_text(i==12 ? "CANCEL" : labels[i], box.x+box.w/2,
                           box.y+(box.h-22)/2, cyd_fonts::pixelfont_11x5_thin,
                           enabled ? CLOCK_TEXT : GREEN, 2);
    }
}

}  // namespace

void matrix_clock_run(uint8_t brightness_percent)
{
    tab5_display_set_brightness(brightness_percent);
    const int cols = std::min(matrix::max_columns, (screen_w()+CELL_W-1)/CELL_W);
    const int rows = std::min(matrix::max_rows, (screen_h()+CELL_H-1)/CELL_H);
    rain.reset(rows, esp_random());
    matrix::Lock lock;
    int64_t next_frame = 0;
    bool keypad_dirty = true;
    while (lock.state != matrix::State::unlocked) {
        int64_t now = esp_timer_get_time();
        const uint64_t ms = static_cast<uint64_t>(now/1000);
        int x=0,y=0;
        bool pressed = tab5_touch_poll(&x,&y);
        auto before = lock.state;
        if (lock.poll(pressed,x,y,ms,screen_w(),screen_h())) keypad_dirty=true;
        if (before != lock.state) next_frame=0;
        if (lock.wrong_until && ms>=lock.wrong_until) {
            lock.wrong_until=0; keypad_dirty=true;
        }
        if (lock.state==matrix::State::rain && now>=next_frame) {
            fill_rect(0,0,screen_w(),screen_h(),BG);
            rain.tick(cols,rows);
            draw_rain(cols,rows);
            draw_clock_panel();
            tab5_display_present();
            next_frame=esp_timer_get_time()+FRAME_US;
        } else if (lock.state==matrix::State::keypad && keypad_dirty) {
            draw_keypad(lock,ms);
            tab5_display_present();
            keypad_dirty=false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

}  // namespace tab5

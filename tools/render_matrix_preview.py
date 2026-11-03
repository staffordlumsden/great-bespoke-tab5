#!/usr/bin/env python3
"""Render production Matrix/clock/keypad code with synthetic RTC and RNG."""
from pathlib import Path
import io
import subprocess
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "projects/tab5-console/main"
OUTPUT = ROOT / "projects/tab5-console/build-native"
HARNESS = r'''
#include "matrix_clock.cpp"
static bool portrait_mode;
static uint32_t pixels[1280*1280];
extern "C" int tab5_display_width() { return portrait_mode ? 720 : 1280; }
extern "C" int tab5_display_height() { return portrait_mode ? 1280 : 720; }
extern "C" esp_err_t tab5_display_rect(int x,int y,int w,int h,uint32_t color) {
    if(x<0 || y<0 || x+w>tab5_display_width() || y+h>tab5_display_height()) std::abort();
    for(int r=y;r<y+h;++r) for(int c=x;c<x+w;++c) pixels[r*tab5_display_width()+c]=color;
    return ESP_OK;
}
extern "C" bool tab5_rtc_read(tab5_clock_time *out) { *out={2026,10,30,5,20,30,0,true}; return true; }
extern "C" esp_err_t tab5_display_present() { return ESP_OK; }
extern "C" esp_err_t tab5_display_set_brightness(uint8_t) { return ESP_OK; }
extern "C" bool tab5_touch_poll(int*,int*) { return false; }
int main(int argc,char**) {
    portrait_mode=argc>1;
    int rows=tab5_display_height()/tab5::matrix::cell_height;
    int cols=tab5_display_width()/tab5::matrix::cell_width;
    tab5::rain.reset(rows,42);
    for(int i=0;i<10;++i) tab5::rain.tick(cols,rows);
    tab5::fill_rect(0,0,tab5_display_width(),tab5_display_height(),0x050a05);
    tab5::draw_rain(cols,rows); tab5::draw_clock_panel();
    if(argc>2) {tab5::matrix::Lock lock;lock.state=tab5::matrix::State::keypad;tab5::draw_keypad(lock,0);}
    printf("P6\n%d %d\n255\n",tab5_display_width(),tab5_display_height());
    for(int i=0;i<tab5_display_width()*tab5_display_height();++i) {
        putchar(pixels[i]>>16);putchar(pixels[i]>>8);putchar(pixels[i]);
    }
}
'''
with tempfile.TemporaryDirectory(prefix="tab5-matrix-preview-") as directory:
    temp = Path(directory)
    (temp / "freertos").mkdir()
    for name, content in {
        "esp_err.h": "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n",
        "esp_random.h": "#pragma once\ninline unsigned esp_random(){return 42;}\n",
        "esp_timer.h": "#pragma once\ninline long long esp_timer_get_time(){return 0;}\n",
        "freertos/FreeRTOS.h": "#pragma once\n#define pdMS_TO_TICKS(x) (x)\n",
        "freertos/task.h": "#pragma once\ninline void vTaskDelay(int){}\n",
    }.items():
        (temp / name).write_text(content)
    (temp / "preview.cpp").write_text(HARNESS)
    subprocess.run(["c++", "-std=c++17", "-I", str(temp), "-I", str(MAIN),
                    "-I", str(MAIN / "fonts"), "-I", str(ROOT / "projects/tab5-console/components/tab5_hal/include"),
                    str(temp / "preview.cpp"), "-o", str(temp / "preview")], check=True)
    OUTPUT.mkdir(exist_ok=True)
    for name, args in (("matrix-landscape", []), ("matrix-portrait", ["portrait"]),
                       ("matrix-keypad", ["portrait", "keypad"])):
        data = subprocess.check_output([str(temp / "preview"), *args])
        Image.open(io.BytesIO(data)).save(OUTPUT / f"{name}.png")
        print(OUTPUT / f"{name}.png")

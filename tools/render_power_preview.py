#!/usr/bin/env python3
"""Render the real Console power controls with synthetic state, in both layouts."""
from pathlib import Path
import io
import subprocess
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "projects/tab5-console/main"
source = (MAIN / "tab5_console.cpp").read_text()
drawing = source[source.index("void rect("):source.index("void set_notice(")]
drawing += source[source.index("void draw_power_button("):source.index("void draw_battery_and_power(")]
preamble = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include "pixel_fonts.h"
#include "power_layout.hpp"
using cyd_fonts::Font;
using cyd_fonts::Glyph;
static bool is_portrait = false, power_confirm = false;
bool portrait() { return is_portrait; }
int screen_width() { return portrait() ? 720 : 1280; }
int screen_height() { return portrait() ? 1280 : 720; }
static uint32_t pixels[1280*1280];
constexpr uint32_t BLUE=0x3b82f6, ORANGE=0xf59e0b, RED=0xef4444;
constexpr uint32_t GREEN=0x22c55e, YELLOW=0xeab308;
struct {uint32_t surface_pressed=0x2a3139, border=0x353d46, text=0xf7f8fa;} theme;
void tab5_display_rect(int x,int y,int w,int h,uint32_t color) {
    for(int r=y;r<y+h;++r) for(int c=x;c<x+w;++c)
        pixels[r*screen_width()+c]=color;
}
'''
fixture = r'''
int main(int argc,char**) {
    is_portrait=argc>1;
    std::fill_n(pixels,1280*1280,0x101215);
    draw_power_controls();
    printf("P6\n%d %d\n255\n",screen_width(),screen_height());
    for(int i=0;i<screen_width()*screen_height();++i) {
        putchar(pixels[i]>>16); putchar(pixels[i]>>8); putchar(pixels[i]);
    }
}
'''
output = ROOT / "projects/tab5-console/build-native"
output.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="tab5-lock-preview-") as directory:
    cpp = Path(directory) / "preview.cpp"
    binary = Path(directory) / "preview"
    cpp.write_text(preamble + drawing + fixture)
    subprocess.run(["c++", "-std=c++17", "-I", str(MAIN), "-I", str(MAIN / "fonts"),
                    str(cpp), "-o", str(binary)], check=True)
    for mode in ("landscape", "portrait"):
        raw = subprocess.check_output([str(binary)] + (["portrait"] if mode == "portrait" else []))
        image = Image.open(io.BytesIO(raw))
        bounds = (24, 852, 696, 1064) if mode == "portrait" else (500, 512, 1256, 638)
        image.crop(bounds).save(output / f"power-{mode}.png")
        print(output / f"power-{mode}.png")

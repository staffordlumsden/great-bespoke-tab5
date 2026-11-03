#!/usr/bin/env python3
"""Render the real Console Weather card with synthetic cached readings only."""
import argparse
import io
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "projects/tab5-console"

def section(source, start, end):
    return start + source.split(start, 1)[1].split(end, 1)[0]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = (PROJECT / "main/tab5_console.cpp").read_text()
    drawing = (
        section(source, "struct Theme {", "bool dark_mode") +
        section(source, "void rect(", "void set_notice(") +
        section(source, "void draw_card_base(", "void draw_card_label(") +
        section(source, "void weather_card_text(", "const char *bluetooth_card_value()")
    )
    harness = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "pixel_fonts.h"
#include "tab5_weather_snapshot.h"
#define TAB5_DISPLAY_WIDTH 1280
#define TAB5_DISPLAY_HEIGHT 720
static uint32_t pixels[720][1280];
tab5_weather_snapshot_t weather_snapshot{};
bool weather_saved = true;
int tab5_display_rect(int x,int y,int w,int h,uint32_t rgb) {
    assert(x>=0 && y>=0 && x+w<=402 && y+h<=100);
    for(int row=y;row<y+h;++row) std::fill_n(pixels[row]+x,w,rgb);
    return 0;
}
'''
    fixture = r'''
int main(int argc,char**) {
    if(argc>1) theme=LIGHT;
    weather_snapshot.temperature_tenths_c=213;
    weather_snapshot.has_temperature=true;
    std::strcpy(weather_snapshot.city,"Fixture City");
    std::strcpy(weather_snapshot.conditions,"Partly cloudy");
    draw_weather_card(0,0);
    std::printf("P6\n402 100\n255\n");
    for(int y=0;y<100;++y)for(int x=0;x<402;++x){auto c=pixels[y][x];
        std::putchar(c>>16);std::putchar(c>>8);std::putchar(c);}
}
'''
    with tempfile.TemporaryDirectory(prefix="tab5-weather-card-") as directory:
        temporary = Path(directory)
        cpp = temporary / "card.cpp"
        cpp.write_text(harness + drawing + fixture)
        model = temporary / "model.o"
        include = PROJECT / "components/tab5_weather_snapshot/include"
        fake = PROJECT / "tests/boot_fakes"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-I", str(include),
                        "-I", str(fake), "-c",
                        str(PROJECT / "components/tab5_weather_snapshot/snapshot_model.c"),
                        "-o", str(model)], check=True)
        binary = temporary / "card"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-I", str(include),
                        "-I", str(fake), "-I", str(PROJECT / "main/fonts"),
                        str(cpp), str(model), "-o", str(binary)], check=True)
        samples = [Image.open(io.BytesIO(subprocess.check_output([str(binary), *flags])))
                   for flags in ([], ["light"])]
        output = Image.new("RGB", (402, 200))
        for row, sample in enumerate(samples): output.paste(sample, (0, row * 100))
        args.output.parent.mkdir(parents=True, exist_ok=True)
        output.save(args.output)
        print("Rendered real Weather card; synthetic data, dark/light, card bounds verified")

if __name__ == "__main__": main()

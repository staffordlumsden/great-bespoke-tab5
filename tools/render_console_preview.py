#!/usr/bin/env python3
"""Render the current Console drawing code with fixed, non-device telemetry.

Requires Python 3, Pillow and a C++17 compiler (clang++ by default). This does
not access a Tab5, its saved settings, credentials, SD card or radio.
"""

import argparse
import hashlib
import io
import os
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image, PngImagePlugin


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "projects/tab5-console"


def section(source, start, end):
    """Keep firmware drawing functions verbatim; fail if their boundaries move."""
    return source.split(start, 1)[1].split(end, 1)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Output PNG path")
    args = parser.parse_args()
    source_bytes = (PROJECT / "main/tab5_console.cpp").read_bytes()
    source = source_bytes.decode()
    version = re.search(
        r'set\(PROJECT_VER "([0-9.]+)"\)',
        (PROJECT / "CMakeLists.txt").read_text(),
    ).group(1)

    # Compile the real UI functions and font atlas, replacing only the HAL and
    # live readings. No screenshot approximation or reimplemented UI layout.
    drawing = (
        "namespace {"
        + section(source, "namespace {", "void set_notice(")
        + "void progress_bar("
        + section(source, "void progress_bar(", "\nvoid draw_overlay();")
        + "\nvoid draw_button(int, int, int, int, const char *, bool) {}\n"
        + "void overlay_frame(const char *, const char *, const char *) {}\n"
        + "}\n"
    )
    harness = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pixel_fonts.h"
#include "tab5_hal.h"
#include "tab5_installer.h"
#include "tab5_radio.h"
#include "tab5_weather_snapshot.h"
#include "tab5_app_boot.h"
#include "input_tab5_kbd.hpp"
#include "matrix_clock.hpp"
#include "power_layout.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
static uint16_t pixels[TAB5_DISPLAY_WIDTH * TAB5_DISPLAY_HEIGHT];
static int64_t esp_timer_get_time() { return 0; }
struct AppDescription { const char *version; };
static const AppDescription *esp_app_get_description() {
    static const AppDescription app{PREVIEW_VERSION};
    return &app;
}
extern "C" esp_err_t tab5_display_rect(int x, int y, int w, int h, uint32_t rgb) {
    // Match the HAL's RGB888 -> RGB565 conversion at native landscape size.
    uint16_t color = ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0)
                     | ((rgb >> 3) & 0x001f);
    for (int row = y; row < y + h; ++row)
        std::fill_n(pixels + row * TAB5_DISPLAY_WIDTH + x, w, color);
    return ESP_OK;
}
extern "C" esp_err_t tab5_display_present() { return ESP_OK; }
extern "C" esp_err_t tab5_display_present_region(int,int,int,int) { return ESP_OK; }
extern "C" int tab5_display_width() { return TAB5_DISPLAY_WIDTH; }
extern "C" int tab5_display_height() { return TAB5_DISPLAY_HEIGHT; }
namespace tab5 { bool tab5_kbd_connected() { return true; } }
extern "C" bool tab5_rtc_read(tab5_clock_time *out) {
    *out = {2026, 10, 27, 2, 9, 41, 0, true};
    return true;
}
extern "C" bool tab5_keyboard_connected() { return true; }
'''
    fixture = r'''
int main() {
    // Illustrative readings only. Wi-Fi/Bluetooth are off, and no SSID,
    // address, pairing data or actual device telemetry is read or embedded.
    metrics.sd_ready = true;
    metrics.sd_total = 64000000000ULL;
    metrics.sd_free = 63200000000ULL;
    metrics.flash_total = 16 * 1024 * 1024;
    metrics.flash_allocated = 0x407000;
    metrics.internal_total = 640 * 1024;
    metrics.internal_free = 400 * 1024;
    metrics.psram_total = 32 * 1024 * 1024;
    metrics.psram_free = 29 * 1024 * 1024;
    metrics.battery_ready = true;
    metrics.battery_voltage = 7.70f;
    metrics.battery_percent = 76;
    draw_ui();
    std::printf("P6\n%d %d\n255\n", TAB5_DISPLAY_WIDTH, TAB5_DISPLAY_HEIGHT);
    for (uint16_t pixel : pixels) {
        std::putchar(((pixel >> 11) & 31) * 255 / 31);
        std::putchar(((pixel >> 5) & 63) * 255 / 63);
        std::putchar((pixel & 31) * 255 / 31);
    }
    return std::ferror(stdout) ? 1 : 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="tab5-console-preview-") as temp:
        temp = Path(temp)
        fake_headers = {
            "esp_err.h": "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n"
                         "#define ESP_ERR_INVALID_ARG 1\n"
                         "inline const char *esp_err_to_name(esp_err_t){return \"error\";}\n",
            "esp_app_desc.h": "#pragma once\nstruct esp_app_desc_t { char version[32]; };\n"
                              "const esp_app_desc_t *esp_app_get_description();\n",
            "esp_flash.h": "#pragma once\n#include \"esp_err.h\"\n#include <cstdint>\n"
                           "esp_err_t esp_flash_get_size(void *, uint32_t *);\n",
            "esp_heap_caps.h": "#pragma once\n#include <cstddef>\n"
                               "#define MALLOC_CAP_INTERNAL 1\n#define MALLOC_CAP_SPIRAM 2\n"
                               "size_t heap_caps_get_free_size(int);\nsize_t heap_caps_get_total_size(int);\n",
            "esp_log.h": "#pragma once\n#define ESP_LOGW(...) do {} while (0)\n",
            "esp_netif_sntp.h": "#pragma once\n",
            "esp_partition.h": "#pragma once\n#include \"esp_err.h\"\n#include <cstdint>\n"
                               "typedef struct { uint32_t size; } esp_partition_t;\n"
                               "typedef void *esp_partition_iterator_t;\n"
                               "#define ESP_PARTITION_TYPE_ANY 0\n#define ESP_PARTITION_SUBTYPE_ANY 0\n"
                               "esp_partition_iterator_t esp_partition_find(int,int,const char *);\n"
                               "const esp_partition_t *esp_partition_get(esp_partition_iterator_t);\n"
                               "esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t);\n",
            "esp_sntp.h": "#pragma once\n",
            "esp_timer.h": "#pragma once\n#include <cstdint>\nint64_t esp_timer_get_time();\n",
            "nvs.h": "#pragma once\n#include \"esp_err.h\"\n#include <cstdint>\n#include <cstddef>\n"
                     "typedef uint32_t nvs_handle_t;\n#define NVS_READONLY 0\n#define NVS_READWRITE 1\n"
                     "esp_err_t nvs_open(const char *,int,nvs_handle_t *);\nvoid nvs_close(nvs_handle_t);\n"
                     "esp_err_t nvs_set_u8(nvs_handle_t,const char *,uint8_t);\n"
                     "esp_err_t nvs_get_u8(nvs_handle_t,const char *,uint8_t *);\n"
                     "esp_err_t nvs_set_str(nvs_handle_t,const char *,const char *);\n"
                     "esp_err_t nvs_get_str(nvs_handle_t,const char *,char *,size_t *);\n"
                     "esp_err_t nvs_commit(nvs_handle_t);\n",
            "nvs_flash.h": "#pragma once\n#include \"esp_err.h\"\nesp_err_t nvs_flash_init();\n",
            "sdkconfig.h": "#pragma once\n",
            "freertos/FreeRTOS.h": "#pragma once\n#include <cstdint>\n"
                                   "typedef void *QueueHandle_t;\n"
                                   "#define pdMS_TO_TICKS(x) (x)\n#define pdPASS 1\n#define pdTRUE 1\n"
                                   "#define portMAX_DELAY 0xffffffffu\n",
            "freertos/queue.h": "#pragma once\n#include \"freertos/FreeRTOS.h\"\n"
                                 "int xQueueOverwrite(QueueHandle_t, const void *);\n",
            "freertos/task.h": "#pragma once\n#include \"freertos/FreeRTOS.h\"\n"
                               "void vTaskDelete(void *);\nvoid vTaskDelay(int);\n",
        }
        for name, content in fake_headers.items():
            target = temp / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        cpp = temp / "preview.cpp"
        cpp.write_text(harness + drawing + fixture)
        executable = temp / "preview"
        subprocess.run(
            [os.environ.get("CXX", "clang++"), "-std=c++17", "-O2",
             f'-DPREVIEW_VERSION="{version}"', "-I", str(temp),
             "-I", str(PROJECT / "main/fonts"), "-I", str(PROJECT / "main"),
             "-I", str(PROJECT / "components/tab5_hal/include"),
             "-I", str(PROJECT / "components/tab5_radio/include"),
             "-I", str(PROJECT / "components/tab5_installer/include"),
             "-I", str(PROJECT / "components/tab5_weather_snapshot/include"),
             "-I", str(PROJECT / "components/tab5_app_boot/include"),
             "-ffunction-sections", "-fdata-sections", "-Wl,-dead_strip",
             str(cpp), "-o", str(executable)],
            check=True, timeout=60,
        )
        ppm = subprocess.check_output([str(executable)], timeout=10)

    preview = Image.open(io.BytesIO(ppm)).convert("RGB")
    assert preview.size == (1280, 720)
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text("Title", f"TAB5 Console {version}")
    metadata.add_text(
        "Description", "Source-rendered UI preview with synthetic telemetry; "
        "not a photograph or framebuffer capture from a physical Tab5.",
    )
    metadata.add_text("Source-SHA256", hashlib.sha256(source_bytes).hexdigest())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    preview.save(args.output, pnginfo=metadata, optimize=True)
    print(f"Rendered Console {version}: {args.output} (1280 x 720)")


if __name__ == "__main__":
    main()

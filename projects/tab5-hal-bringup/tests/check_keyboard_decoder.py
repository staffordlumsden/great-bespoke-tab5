#!/usr/bin/env python3
"""Exercise the actual C HID report/translation functions with recorded reports.

No ESP-IDF/USB hardware needed. Compile the production function bodies with a
capture sink and run under address/undefined-behaviour sanitizers. USB lifecycle
and electrical/display behaviour still require physical acceptance checks.
"""
from pathlib import Path
import subprocess
import tempfile

project = Path(__file__).resolve().parents[1]
source = (project / 'components/tab5_hal/tab5_keyboard.c').read_text()
functions = source[source.index('static void key_press('):source.index('static void interface_event(')]
preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_LOGI(...) ((void)0)
static uint8_t previous[6];
static bool caps_lock;
static uint8_t output[256];
static size_t count;
static void emit(const char *s, size_t n) {
    assert(count + n <= sizeof(output));
    memcpy(output + count, s, n); count += n;
}
'''
tests = r'''
static void clear(void) {
    memset(previous, 0, sizeof(previous)); caps_lock = false; count = 0;
}
static void press(uint8_t mods, uint8_t key) {
    uint8_t report_data[8] = {mods, 0, key, 0, 0, 0, 0, 0};
    report(report_data, sizeof(report_data));
}
static void expect(const char *s, size_t n) {
    assert(count == n); assert(memcmp(output, s, n) == 0); count = 0;
}
int main(void) {
    clear(); press(0, 4); expect("a", 1);
    press(0, 4); expect("", 0); // held key: no duplicate
    press(0, 0); press(0, 4); expect("a", 1);
    clear(); press(2, 4); expect("A", 1);
    clear(); press(0x20, 29); expect("Z", 1);
    clear(); press(1, 4); expect("\x01", 1);
    clear(); press(0x10, 29); expect("\x1a", 1);
    clear(); press(4, 4); expect("\x1b" "a", 2);
    clear(); press(0, 0x39); press(0, 0); press(0, 4); expect("A", 1);
    press(0, 0); press(2, 5); expect("b", 1); // Shift reverses Caps
    clear(); press(0, 30); expect("1", 1);
    clear(); press(2, 30); expect("!", 1);
    clear(); press(2, 39); expect(")", 1);
    clear(); press(0, 40); expect("\r", 1);
    clear(); press(0, 42); expect("\x7f", 1);
    clear(); press(0, 43); expect("\t", 1);
    clear(); press(0, 79); expect("\x1b[C", 3);
    clear(); press(0, 80); expect("\x1b[D", 3);
    clear(); press(0, 81); expect("\x1b[B", 3);
    clear(); press(0, 82); expect("\x1b[A", 3);
    clear(); press(0, 76); expect("\x1b[3~", 4);
    clear(); press(0, 4); expect("a", 1);
    press(0, 1); expect("", 0); // rollover must not release last valid key
    press(0, 4); expect("", 0);
    press(0, 0); press(0, 4); expect("a", 1);
    clear(); uint8_t duplicate[8] = {0,0,4,4,5,0,0,0};
    report(duplicate, 8); expect("ab", 2);
    clear(); report(duplicate, 7); expect("", 0);
    report(duplicate, 0); expect("", 0);
    // Exercise every possible usage/modifier byte; output always bounded.
    for (int mod = 0; mod < 256; mod++) for (int key = 0; key < 256; key++) {
        clear(); press(mod, key); assert(count <= 5);
    }
    puts("PASS: typing, modifiers, ANSI navigation, duplicates, rollover, invalid lengths; 65536 usage/modifier combinations");
}
'''
with tempfile.TemporaryDirectory(prefix='tab5-hid-test-') as directory:
    cfile = Path(directory) / 'decoder.c'
    binary = Path(directory) / 'decoder'
    cfile.write_text(preamble + functions + tests)
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

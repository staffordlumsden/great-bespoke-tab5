#!/usr/bin/env python3
"""Small release-contract check; physical touch acceptance remains separate."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main" / "tab5_console.cpp").read_text()
HAL = (ROOT / "components" / "tab5_hal" / "tab5_hal.c").read_text()
FONT = (ROOT / "main" / "fonts" / "pixel_fonts.h").read_text()
CMAKE = (ROOT / "CMakeLists.txt").read_text()
BINARY = ROOT / "build" / "tab5_console.bin"

for label in ("Console", "Thin Client", "AI Usage", "Files", "HTML View", "Camera"):
    assert f'"{label}"' in SOURCE, f"missing tab {label}"
assert '"Network"' not in SOURCE, "Network tab must be replaced with Camera"
assert '"Web Server"' not in SOURCE, "Web Server tab must be replaced with HTML View"
assert '"Shell"' not in SOURCE, "Shell tab must be replaced with Thin Client"
assert '"Ollama"' not in SOURCE, "Ollama tab must be replaced with AI Usage"

for face in ("pixelfont_7x5_thin", "pixelfont_11x5_thin", "pixelfont_13x7_semibold"):
    assert face in SOURCE and face in FONT, f"missing exact PixelFonts face {face}"

for control in ("Appearance", "Brightness", "Volume", "Bluetooth", "Battery", "Rotate", "Power Off"):
    assert f'"{control}"' in SOURCE, f"missing control {control}"

assert 'set(PROJECT_VER "0.3.8")' in CMAKE
assert 'i == 1 ? RED' in SOURCE, "Thin Client tab must use the red highlight"
assert 'i == 2 ? ORANGE' in SOURCE, "AI Usage tab must use the amber/yellow highlight"
assert 'i == 3 ? GREEN' in SOURCE, "Files tab must use the green highlight"
assert 'i == 4 ? BLUE' in SOURCE, "HTML View tab must use the blue highlight"
assert 'i == 5 ? PURPLE' in SOURCE, "Camera tab must use the purple highlight"
assert 'UPDATE_DESCRIPTION = "THIN CLIENT & AI USAGE TABS"' in SOURCE
assert "0xeef1f4" in SOURCE and "0xffffff" in SOURCE, "neutral light palette missing"
assert "matrix_clock_run" in SOURCE, "Matrix lockscreen integration missing"
sleep_handler = SOURCE.split("void screen_sleep()", 1)[1].split("void adjust_brightness", 1)[0]
assert "matrix_clock_run" not in sleep_handler, "Sleep must turn off the display"
assert "tab5_display_set_brightness(0)" in sleep_handler
assert "while (!tab5_touch_poll" in sleep_handler, "Sleep must wait for a wake touch"
assert "tab5_display_set_brightness(brightness)" in sleep_handler
assert 'centered("Lock"' in SOURCE and "Icon::lock" in SOURCE
assert "tab5::power_hit(portrait(), x, y)" in SOURCE
lock_art = SOURCE.split("void draw_lock_button", 1)[1].split("void draw_power_controls", 1)[0]
assert "esp_random" not in lock_art and "esp_timer" not in lock_art, "Lock artwork must be static"
assert "MINT" not in SOURCE.upper(), "mint must not be used in light mode"
assert "tab5_rtc_read" in SOURCE and "tab5_battery_read" in SOURCE
assert "tab5_display_present_region" in SOURCE, "dirty-region presentation missing"
assert "dirty |= DIRTY_CLOCK" in SOURCE, "clock must request only its dirty region"
clock_timer = SOURCE[SOURCE.index("if (now - last_clock"):]
clock_timer = clock_timer[:clock_timer.index("vTaskDelay")]
assert "draw_ui()" not in clock_timer, "one-second timer must not redraw the full UI"
assert "footer_top()" in SOURCE and "screen_width()" in SOURCE
assert '#include "tab5_radio.h"' in SOURCE
assert "tab5_radio_wifi_scan" in SOURCE and "tab5_radio_wifi_connect" in SOURCE
assert "tab5_radio_ble_scan" in SOURCE and "tab5_radio_ble_connect" in SOURCE
assert "tab5_radio_ble_pair_confirm" in SOURCE
assert "bluetooth_footer_value()" in SOURCE
assert "PASSWORD MUST BE 8+ CHARACTERS" in SOURCE
assert "PASSWORD_SYMBOLS_0" in SOURCE
assert 'password_symbols ? "ABC" : "SYM"' in SOURCE
assert "wifi_scroll" in SOURCE and "ble_scroll" in SOURCE
assert 'list_nav_h(), "UP"' in SOURCE and 'list_nav_h(), "DOWN"' in SOURCE
assert "radio_visible_state_changed" in SOURCE
assert 'THINCLIENT_PROJECT = "Tab5ThinClient"' in SOURCE
assert 'THINCLIENT_VERSION = "0.1.3-dev"' in SOURCE
assert 'AIUSAGE_PROJECT = "Tab5AIUsage"' in SOURCE
assert 'AIUSAGE_VERSION = "0.2.2"' in SOURCE
assert 'WEATHER_PROJECT = "Tab5Weather"' in SOURCE
assert 'WEATHER_VERSION = "0.1.2"' in SOURCE
assert "find_auto_launch_package" in SOURCE
assert "open_installer(AutoLaunch::none)" in SOURCE
assert "constexpr int HEADER_TOP = 8;" in SOURCE and "constexpr int TABS_TOP = 78;" in SOURCE
clock_fn = SOURCE.split("void draw_clock()", 1)[1].split("void draw_header()", 1)[0]
assert "panel(" not in clock_fn, "Clock tile must not have a box around it"
assert "battery_x()" in SOURCE and "clock_x()" in SOURCE
assert "Icon::rotate" in SOURCE and 'save_setting("rotation"' in SOURCE
for rotation in ("TAB5_ROTATION_0", "TAB5_ROTATION_90", "TAB5_ROTATION_180", "TAB5_ROTATION_270"):
    assert rotation in HAL, f"missing HAL transform {rotation}"
assert "rect(0, BODY_TOP, width, bottom - BODY_TOP" in SOURCE
assert "icon(Icon::sd" in SOURCE
assert "DIRTY_CONTROLS | DIRTY_STORAGE | DIRTY_POWER" in SOURCE
assert "tab5_radio_power_set(wifi_on || bluetooth_on)" not in SOURCE
assert (ROOT / "partitions.csv").is_file()
assert BINARY.is_file(), "build/tab5_console.bin is missing"
assert BINARY.stat().st_size < 0x400000, "application exceeds 4 MiB factory partition"
assert (ROOT / "third_party" / "PixelFonts" / "LICENSE").is_file()

assert "Overlay::clock" in SOURCE, "Clock settings overlay missing"
assert "tab5_rtc_set" in HAL and "tab5_rtc_set" in SOURCE, "RTC write missing"
assert "AutoLaunch::thinclient" in SOURCE, "ThinClient auto-launch missing"
assert "AutoLaunch::ai_usage" in SOURCE, "AI Usage auto-launch missing"
assert "AutoLaunch::weather" in SOURCE, "Weather auto-launch missing"
assert 'FILESERVER_PROJECT = "Tab5FileServer"' in SOURCE, "FileServer auto-launch project missing"
assert 'HTMLVIEWER_PROJECT = "Tab5HtmlViewer"' in SOURCE, "HtmlViewer auto-launch project missing"
assert 'CAMERA_PROJECT = "Tab5Camera"' in SOURCE, "Camera auto-launch project missing"
assert "AutoLaunch::fileserver" in SOURCE, "FileServer auto-launch missing"
assert "AutoLaunch::htmlviewer" in SOURCE, "HtmlViewer auto-launch missing"
assert "AutoLaunch::camera" in SOURCE, "Camera auto-launch missing"
assert "sel ? theme.text : theme.muted, 4" in SOURCE, "SD library 2x firmware font scale missing"
assert '#include "input_tab5_kbd.hpp"' in SOURCE, "Tab5 Keyboard driver integration missing"

print(f"PASS: TAB5 Console 0.3.8 UI/radio/rotation/matrix/clock/launcher contract; image={BINARY.stat().st_size} bytes")

#!/usr/bin/env python3
"""Static safety and delivery contract for the Tab5 Terminal port."""

from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = text("firmware/CMakeLists.txt")
defaults = text("firmware/sdkconfig.defaults")
manifest = text("firmware/main/idf_component.yml")
app = text("firmware/main/app_main.cpp")
display = text("firmware/main/tab5_terminal_display.cpp")
terminal_fonts = text("firmware/main/fonts/terminal_fonts.h")
font_generator = text("firmware/tools/generate_terminal_fonts.py")
notices = text("THIRD_PARTY_NOTICES.txt")
keyboard = text("firmware/main/input_tab5_kbd.cpp")
keyboard_header = text("firmware/main/input_tab5_kbd.hpp")
build_wrapper = text("firmware/tools/build.sh")
partitions = text("firmware/partitions.csv")
ignore = text(".gitignore")
provenance = json.loads(text("PROVENANCE.json"))

require('set(PROJECT_VER "0.1.4")' in cmake, "unexpected app version")
require("tab5_terminal_sd_only" in cmake, "ESP-IDF flash targets are not blocked")
require("erase-flash" in build_wrapper and "app-flash" in build_wrapper,
        "build wrapper does not block destructive/direct flash actions")
require('version: "1.4.0"' in manifest, "ESP-Hosted must remain exactly 1.4.0")
require('version: "0.8.5"' in manifest, "Wi-Fi Remote must remain exactly 0.8.5")
require("CONFIG_BT_ENABLED=n" in defaults, "Bluetooth must remain absent here")
require("esp_hosted_get_coprocessor_fwversion" in app, "C6 version is not queried")
require("action=NONE" in app, "C6 no-update evidence line is missing")

# Scan only authored application/build sources. Registry-managed dependencies
# are ignored build inputs and may legitimately implement their own updater
# APIs; the contract here is that this port never calls or packages one.
authored_paths = [
    ROOT / "firmware" / "CMakeLists.txt",
    ROOT / "firmware" / "sdkconfig.defaults",
    ROOT / "firmware" / "tools" / "build.sh",
]
authored_paths.extend((ROOT / "firmware" / "main").rglob("*"))
port_source = "\n".join(
    path.read_text(encoding="utf-8", errors="ignore")
    for path in authored_paths
    if path.is_file()
)
for forbidden in (
    "esp_hosted_slave_ota",
    "esp_hosted_coprocessor_ota",
    "update_slave",
    "c6_updater/updater.sh",
):
    require(forbidden not in port_source, f"forbidden C6 update path: {forbidden}")

require("TAB5_ROTATION_0" in display, "terminal is not fixed to landscape rotation 0")
require("ROTATE" not in display.upper(), "terminal contains a rotate control")
display_header = text("firmware/main/tab5_terminal_display.hpp")
require("kDefaultTerminalColumns = 60" in display_header,
        "readable default terminal width changed")
require("kDefaultTerminalRows = 22" in display_header,
        "readable default terminal height changed")
for geometry in (
    "{80, 30, 12, 20, 8}",
    "{60, 22, 16, 27, 10}",
    "{40, 15, 24, 40, 15}",
):
    require(geometry in display, f"missing zoom geometry: {geometry}")
for font_symbol in (
    "tab5_terminal_mono_8pt_regular",
    "tab5_terminal_mono_8pt_bold",
    "tab5_terminal_mono_10pt_regular",
    "tab5_terminal_mono_10pt_bold",
    "tab5_terminal_mono_15pt_regular",
    "tab5_terminal_mono_15pt_bold",
):
    require(font_symbol in terminal_fonts and font_symbol in display,
            f"missing terminal font: {font_symbol}")
require("terminal_glyph(cell.ch" in display,
        "terminal cells do not use the dedicated monospace atlas")
terminal_cell_renderer = display.split(
    "Tab5TerminalDisplay::draw_cells", 1
)[1].split("Tab5TerminalDisplay::flush", 1)[0]
require("cyd_fonts::" not in terminal_cell_renderer,
        "terminal cells still use the UI PixelFont atlas")
require('"v0.1.4  -  TERMINAL MONO 10 PT"' in display,
        "terminal header does not identify the 10 pt cell face")
require("Source Code Pro commit 803b7e23ec97ae58b6232ea76519a76d428ba268" in
        terminal_fonts, "terminal font provenance is missing")
require("EXPECTED_SHA256" in font_generator and "THRESHOLD = 96" in font_generator,
        "terminal font generator is not pinned or deterministic")
require("range(0x2500, 0x25A2)" in font_generator,
        "terminal font generator omits Unicode box/block glyphs")
codepoint_match = re.search(
    r"terminal_codepoints\[\] = \{(.*?)\n\};", terminal_fonts, re.DOTALL
)
require(codepoint_match is not None, "generated terminal codepoint table is missing")
codepoints = [
    int(value, 16)
    for value in re.findall(r"0x([0-9a-fA-F]{4})", codepoint_match.group(1))
]
require(codepoints == list(range(0x20, 0x7F)) + list(range(0x2500, 0x25A2)),
        "generated terminal codepoint coverage changed")
for atlas, glyph_stride in (
    ("tab5_terminal_mono_8pt_regular", 40),
    ("tab5_terminal_mono_8pt_bold", 40),
    ("tab5_terminal_mono_10pt_regular", 54),
    ("tab5_terminal_mono_10pt_bold", 54),
    ("tab5_terminal_mono_15pt_regular", 120),
    ("tab5_terminal_mono_15pt_bold", 120),
):
    atlas_match = re.search(
        rf"{atlas}_bits\[\] = \{{(.*?)\n\}};", terminal_fonts, re.DOTALL
    )
    require(atlas_match is not None, f"generated atlas is missing: {atlas}")
    atlas_bytes = [
        int(value, 16)
        for value in re.findall(r"0x([0-9a-fA-F]{2})", atlas_match.group(1))
    ]
    require(len(atlas_bytes) == len(codepoints) * glyph_stride,
            f"generated atlas length is inconsistent: {atlas}")
    for codepoint in (ord("-"), ord("_"), ord("|"), *range(0x2500, 0x25A2)):
        index = codepoints.index(codepoint)
        glyph = atlas_bytes[index * glyph_stride:(index + 1) * glyph_stride]
        require(any(glyph), f"{atlas} glyph U+{codepoint:04X} is blank")
require(provenance["terminal_font"]["license"] == "OFL-1.1",
        "terminal font license provenance changed")
require("Tab5 Terminal Mono bitmap atlas" in notices and
        "SIL OPEN FONT LICENSE Version 1.1" in notices,
        "terminal font notice or OFL text is missing")
require('"A-"' in display and '"A+"' in display,
        "touch zoom controls are missing")
require("active->resize(display.cols(), display.rows())" in app,
        "zoom does not resize the remote SSH PTY")
require("hid_usage == 0x2e" in app and "hid_usage == 0x2d" in app and
        "hid_usage == 0x27" in app,
        "Ctrl +/-/0 keyboard zoom shortcuts are missing")
require("Shift+PageUp" in app and "Shift+PageDown" in app,
        "keyboard scrollback shortcuts are missing")
require("terminal_contains" in app and "scroll_older" in app and
        "scroll_newer" in app,
        "touch scrollback gesture is missing")
require("kCollapsedTermW = 1200" in display and
        "TouchAction::toggle_sidebar" in display,
        "connected-session sidebar rail is missing")
require("status.session == SessionState::connected" in app and
        "display.set_sidebar_collapsed(true)" in app and
        "display.set_sidebar_collapsed(false)" in app,
        "sidebar does not collapse on connect and restore on disconnect")
sidebar_touch = app.split(
    "TouchAction::toggle_sidebar", 1
)[1].split("TouchAction::scroll_older", 1)[0]
require("active->resize" not in sidebar_touch,
        "sidebar toggle unexpectedly resizes the remote PTY")

local_screen_header = text("firmware/components/term_core/include/term_core/screen.hpp")
local_screen_source = text("firmware/components/term_core/src/screen.cpp")
require("upstream/components/term_core" not in cmake,
        "build still selects the immutable upstream term_core")
require("kMaximumScrollbackRows = 512" in local_screen_header,
        "bounded 512-row scrollback is missing")
require("scrollback_count_" in local_screen_header and
        "scrollback_head_" in local_screen_header,
        "fixed circular scrollback allocation is missing")
require("copy_view" in local_screen_header and "copy_view" in local_screen_source,
        "scrollback view API is missing")
require("alt_active_ || scroll_top_ != 0 || scroll_bot_ != capture_rows" in local_screen_source
        and "scrollback_capture_rows_ ? scrollback_capture_rows_ : rows_" in local_screen_source,
        "alternate/partial screen history exclusion or default full-height capture is missing")
require("dec_special_graphics" in local_screen_source and
        "intermediates[0] == '('" in local_screen_source and
        "intermediates[0] == ')'" in local_screen_source and
        "case 0x0E" in local_screen_source and "case 0x0F" in local_screen_source,
        "DEC G0/G1 Special Graphics translation is missing")

require("device_address  = 0x6D" in keyboard, "Tab5 Keyboard address changed")
require("sda_gpio   = 0" in keyboard_header, "Tab5 Keyboard SDA changed")
require("scl_gpio   = 1" in keyboard_header, "Tab5 Keyboard SCL changed")
require("int_gpio   = 50" in keyboard_header, "Tab5 Keyboard INT changed")
require("primary=TAB5_I2C fallback=USB_A" in app, "input precedence is not explicit")
require("tab5_keyboard_poll" in app, "USB-A HID fallback is missing")
require("tab5_keyboard_set_key_filter" in app,
        "USB keyboard local command filter is not registered")
require("USB-A keyboard input verified" in app,
        "offline USB-A keyboard acceptance feedback is missing")
require("PROFILE REQUIRED" in display,
        "missing-profile state is not explicit in the sidebar")
require("pixelfont_11x5_thin" in display and "USB-A: KEY VERIFIED" in display,
        "readable USB-A input status is missing")
ssh = text("firmware/main/ssh_connection.cpp")
require("LIBSSH2_METHOD_CRYPT_CS" in ssh and "aes128-ctr,aes256-ctr" in ssh,
        "proven SSH cipher policy is missing")
require("hmac-sha2-256-etm@openssh.com,hmac-sha2-256" in ssh,
        "proven SSH MAC policy is missing")
require("BENCH_START" not in ssh and "bench_no_render" not in ssh,
        "upstream benchmark output suppression remains in production")
require("msg='%.*s'" not in ssh, "raw remote authentication text reaches logs")

expected_partitions = {
    "factory": ("0x10000", "0x400000"),
    "otadata": ("0x410000", "0x2000"),
    "ota_0": ("0x420000", "0x500000"),
    "ota_1": ("0x920000", "0x500000"),
}
for name, (offset, size) in expected_partitions.items():
    require(re.search(rf"^{name},.*{offset},\s*{size},", partitions, re.MULTILINE),
            f"shared partition changed: {name}")

require("tab5_app_boot_ready(true)" in app, "first-boot confirmation is missing")
require("tab5_app_boot_return_to_console" in app, "Console return is missing")
require("const bool touch_pressed = tab5_touch_poll" in app,
        "touch coordinates are not sampled before dispatch")
require("if (boot_confirmed && !connection_requested" in app,
        "SSH may start before the app health contract is confirmed")
require("/private/" in text("config.public-key.example.json"),
        "private key example is not on the private SD path")
for entry in ("/private/", "/tab5-terminal.json", "**/build*/"):
    require(entry in ignore, f"missing ignore rule: {entry}")

require(provenance["upstream"]["commit"] ==
        "44a616ee333242e0fd3181d5771ead55ded58253",
        "upstream snapshot is not pinned")
require(provenance["target"]["orientation"] == "landscape-0",
        "provenance orientation changed")

print("TAB5_TERMINAL_CONTRACT PASS")

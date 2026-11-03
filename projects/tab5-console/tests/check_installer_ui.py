#!/usr/bin/env python3
"""Host checks for the Console SD-app installer UI integration."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main" / "tab5_console.cpp").read_text()
CMAKE = (ROOT / "main" / "CMakeLists.txt").read_text()

for token in (
    '#include "tab5_installer.h"',
    "Overlay::installer",
    "draw_installer_overlay",
    "handle_installer_tap",
    "draw_launch_button",
    "Launch from SD Card",
    "Icon::sd",
    "pending_installer_scan",
    "installer_catalog",
    "installer_selected",
    "installer_busy",
    "DIRTY_INSTALLER",
    "tab5_installer_scan",
    "installer_worker",
    "draw_install_progress",
    "post_installer_progress",
    "tab5_installer_install",
    "tab5_installer_get_status",
):
    assert token in SOURCE, f"missing {token}"

assert "tab5_installer" in CMAKE, "installer component not linked"
assert "WEATHER TAB IS NOT ACTIVE" not in SOURCE, "old placeholder remains"
assert "INSERT SD CARD TO LAUNCH" in SOURCE
assert "OLLAMA PACKAGE NEEDS THE SD CARD" in SOURCE
assert "SD APPS FOUND" in SOURCE
assert "NO PACKAGES - ADD /firmware/*.json" in SOURCE
assert '"INSTALL + RUN"' in SOURCE
assert '"VALIDATE"' not in SOURCE
assert "xTaskCreate(installer_worker" in SOURCE
assert "xQueueOverwrite" in SOURCE
assert "now - installer_last_paint >= 200000" in SOURCE
assert "tab5_display_set_brightness(0)" in SOURCE
assert "CHOOSE APP PACKAGE" in SOURCE
assert "panel(24, 374" in SOURCE
assert "esp_restart()" in SOURCE, "install success must reboot"

# Ensure the existing contract pieces are still present
assert "dirty |= DIRTY_CLOCK" in SOURCE
assert "tab5_display_present_region" in SOURCE
assert "draw_tabs" in SOURCE

assert "panel(439, 374, 402, 100" in SOURCE, "RAM panel not compact/readable"

assert "const int x = portrait() ? 24 : 854" in SOURCE, "responsive launch card missing"
assert 'OLLAMA_PROJECT = "Tab5OllamaAnalytics"' in SOURCE
assert "text(\"Storage\"" in SOURCE, "storage readout text missing"
assert "text(\"RAM\"" in SOURCE, "RAM readout text missing"
print("PASS: TAB5 Console installer UI contract")

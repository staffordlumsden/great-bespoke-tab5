# Console 0.3.8: Thin Client / AI Usage / Weather Auto-Launch & Header Layout

21 November 2026. TAB5 Console has been upgraded from `0.3.7` (also present on the connected unit's factory partition prior to this maintenance update) to `0.3.8`.

## Changes

1. **Top Tab Labels & Auto-Launch Targets**:
   - Renamed the red tab (index 1, previously `"Shell"`) to `"Thin Client"` and mapped it to `AutoLaunch::thinclient` (`Tab5ThinClient` `0.1.3-dev`).
   - Renamed the amber/yellow tab (index 2, previously `"Ollama"`) to `"AI Usage"` and mapped it to `AutoLaunch::ai_usage` (`Tab5AIUsage` `0.2.2`).
   - Mapped tapping the **Weather** card to `AutoLaunch::weather` (`Tab5Weather` `0.1.2`).
   - Added `find_auto_launch_package()` to match exact `(project, version)` pairs first (plus concatenated `project+version` if used) and fall back to the highest sorted version of the target project in `/sdcard/firmware/`.

2. **Header, Clock, Battery & Tabs Layout**:
   - Moved the 6-tab bar (`Console`, `Thin Client`, `AI Usage`, `Files`, `HTML View`, `Camera`) below the `TAB5 Console` header (`TABS_TOP = 78`, `TABS_HEIGHT = 54`).
   - Removed the box/border around the clock in `draw_clock()` while preserving its tap target and dirty-region repaint (`clock_x()`, `clock_y()`, `clock_w()`, `clock_h()`) to open the Clock & Date Settings overlay.
   - Shrunk the Battery tile (`battery_x()`, `battery_y()`, `battery_w()`, `battery_h()`) and moved it to the header row to the right of the clock in both landscape and portrait layouts, preserving the 5-segment indicator, percentage/voltage readout, and charging/discharging icon.
   - Expanded the bottom power control buttons (`Sleep`, `Lock`, `Rotate`, `Power Off`) in `power_layout.hpp` to fill the freed lower row cleanly in both landscape and portrait orientations.
   - Adjusted overlays and the SD loading screen (`BODY_TOP = 134`) so they sit below the header and tabs without clipping or obscuring the top bar.

3. **Version Bump & Host Contracts**:
   - Bumped `PROJECT_VER` to `0.3.8` in `projects/tab5-console/CMakeLists.txt`.
   - Updated `UPDATE_DESCRIPTION` in `tab5_console.cpp` to `"THIN CLIENT & AI USAGE TABS"`.
   - Updated `check_power_layout.cpp`, `check_console_contract.py`, and `check_installer_ui.py` to match the `0.3.8` layout and auto-launch contracts.

## Build & Flash Verification

- **Version**: `0.3.8`
- **Binary**: `projects/tab5-console/build/tab5_console.bin`
- **Size**: `1,410,800` bytes (`0x1586f0`, 66% free in 4 MiB factory partition at `0x10000`)
- **SHA-256**: `7d8236596a0dccc0c4d9aaa5b895f5e747be9f6071d848e22065394b173e3695`
- **Host Checks**: `check_b1_decoder.py`, `check_keyboard_decoder.py`, `check_console_contract.py`, and `bash tests/run-checks.sh` all passed after the final edit and build.
- **Flashed**: Installed only the resident application to `0x10000` (`factory`) via `IDF_COMPONENT_CHECK_NEW_VERSION=0 ../../idf.py -p /dev/cu.usbmodemXXXX app-flash` on ESP32-P4 revision v1.3. Existing partition table, NVS, OTA slots, factory ESP32-C6 firmware, and SD contents were not modified.
- **Readback Verification**: Independent `read_flash 0x10000 1410800` matched byte-for-byte with SHA-256 `7d8236596a0dccc0c4d9aaa5b895f5e747be9f6071d848e22065394b173e3695`.

## Physical Acceptance

- **Date**: 21 November 2026
- **Result**: Physically tested and accepted by user ("all works as intended"). Header, unboxed clock with Clock & Date Settings tap, shrunk top-right battery tile, relocated tab bar, and auto-launch for **Thin Client** (`Tab5ThinClient` `0.1.3-dev`), **AI Usage** (`Tab5AIUsage` `0.2.2`), and **Weather** (`Tab5Weather` `0.1.2`) confirmed working on hardware.

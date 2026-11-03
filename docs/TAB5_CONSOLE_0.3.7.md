# Console 0.3.7: SD App Library Expansion & Purple Camera Tab Auto-Launch

3 November 2026. Following user reporting that the SD Card App Library in Console was only displaying 8 firmwares when 9 or 10 were present on the SD card, and requesting a purple Camera launcher tab, TAB5 Console has been upgraded to `0.3.7`.

## Changes

1. **Purple `Camera` Top Tab & One-Tap `Tab5Camera` Auto-Launch**:
   - In `draw_tabs()`, tab index 5 (formerly greyed out `"Network"`) is replaced with `"Camera"`.
   - Uses `PURPLE` (`0xa855f7`) border and `PURPLE` label text highlighting matching the style of the other top tabs.
   - Tapping `"Camera"` invokes `open_installer(AutoLaunch::camera)`, scanning `/sdcard/firmware/*.json` on the SD card for project `"Tab5Camera"`, validating its image, flashing to `ota_1`, and booting directly into Tab5Camera.
   - Added auto-launch status messages: `"FINDING CAMERA PACKAGE"`, `"CHECKING CAMERA FIRMWARE"`, `"TAB5 CAMERA PACKAGE NOT FOUND"`, and `"CAMERA PACKAGE NEEDS THE SD CARD"`.

2. **Expanded Package Capacity**:
   - Increased `TAB5_INSTALLER_MAX_PACKAGES` from `8` to `32` in `tab5_installer.h`.
   - Accommodates up to 32 firmware packages in `/sdcard/firmware/` with minimal memory impact (~27 KB in BSS).

3. **Alphabetical Sorting**:
   - Added `compare_packages()` using case-insensitive `strcasecmp` on project name and secondary sort on version string.
   - Invokes `qsort()` over `catalog->packages` when multiple packages are discovered, ensuring deterministic, orderly presentation in the SD App Library list.

4. **Ignore Hidden/AppleDouble Files**:
   - Explicitly skips filenames starting with `.` (`if (entry->d_name[0] == '.') continue;`) to guard against macOS metadata artifacts.

5. **Version Bump & Contract**:
   - Bumped `PROJECT_VER` to `0.3.7` in `CMakeLists.txt`.
   - Updated `UPDATE_DESCRIPTION` in `tab5_console.cpp`.
   - Updated contract checks in `check_console_contract.py` and `check_installer.py`.

## Build & Flash Verification

- **Version**: `0.3.7`
- **Binary**: `projects/tab5-console/build/tab5_console.bin`
- **Size**: `1,410,048` bytes (`0x158400`, 66% free in 4 MiB factory partition at `0x10000`)
- **SHA-256**: `6f768dd713241b5f8bf317c152a79624a4e26269e75e888e0fe42547222af202`
- **Host Checks**: `check_b1_decoder.py`, `check_keyboard_decoder.py`, `check_console_contract.py`, and `run-checks.sh` all `PASS`.
- **Flashed**: Installed to `0x10000` (`factory`) via `app-flash` on `/dev/cu.usbmodemXXXX`.
- **Readback Verification**: Byte-identical readback verified via `read_flash 0x10000 1410048` with matching SHA-256 (`6f768dd713241b5f8bf317c152a79624a4e26269e75e888e0fe42547222af202`). Existing partition map and NVS preserved.
- **Public source rebuild**: SHA-256 `25d980c79f7c7f5995da5afa7350112cd992bf96e2fc79fd0a0ff4e59d68ea9f` (same 1,410,048-byte size). The only source change is replacement of the footer attribution name with a neutral project credit. Treat the rebuilt image as functionally equivalent source, not as the exact hardware-accepted binary.

## Physical Acceptance

- **Date**: 3 November 2026
- **Result**: Physically tested and accepted by user ("works as intended"). SD App Library displays all firmwares without the 8-package truncation, sorted alphabetically, and the purple Camera tab launches Tab5Camera directly from the SD card.

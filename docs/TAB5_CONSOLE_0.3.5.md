# Console 0.3.5: Files & HTML View Auto-Launch, SD Library 2x Font & Keyboard Navigation

31 October 2026. Following the creation of `Tab5FileServer` (`0.1.0`) and `Tab5HtmlViewer` (`0.1.0`),
TAB5 Console was updated to `0.3.5` with top-bar launcher integration and SD App Library improvements.

## Changes

### 1. Green `Files` Top Tab & One-Tap `Tab5FileServer` Auto-Launch
- In `draw_tabs()`, tab index 3 (`"Files"`) is highlighted with a `GREEN` (`0x22c55e`) border and `GREEN` label text.
- Tapping `"Files"` invokes `open_installer(AutoLaunch::fileserver)`, scanning `/firmware/*.json` on the SD card for project `"Tab5FileServer"`, validating its image, flashing to `ota_1`, and booting directly into the File Server app.

### 2. Blue `HTML View` Top Tab (Replacing `Web Server`) & One-Tap `Tab5HtmlViewer` Auto-Launch
- Replaced `"Web Server"` in `draw_tabs()` at tab index 4 with `"HTML View"`, highlighted with a `BLUE` (`0x3b82f6`) border and `BLUE` label text.
- Tapping `"HTML View"` invokes `open_installer(AutoLaunch::htmlviewer)`, scanning `/firmware/*.json` on the SD card for project `"Tab5HtmlViewer"`, validating its image, flashing to `ota_1`, and booting directly into the HTML Viewer app.

### 3. SD App Library 2x Firmware Names & Tab5 Keyboard Navigation
- Doubled the font scale of firmware package names (`"%s  %s", pkg.project, pkg.version`) in `draw_installer_overlay()` from `2` (`14px`) to `4` (`28px`) in both Landscape and Portrait layouts.
- Integrated the M5Stack Tab5 clip-on QWERTY keyboard driver (`input_tab5_kbd.cpp` / `hid_translate.cpp` on ExtPort1 I²C `0x6D`, SDA GPIO0, SCL GPIO1, INT GPIO50) alongside USB-A HID and BLE keyboards.
- Automatically selects the first package (`installer_selected = 0`) when the SD App Library opens.
- Supports Up/Down/Left/Right arrow key navigation (`\x1b[A`, `\x1b[B`, `\x1b[C`, `\x1b[D`) with automatic list scrolling, and immediate validation + launch when pressing the Return / `OK` key (`\r` / `\n`).

## Build & Flash Verification

- **Version**: `0.3.5`
- **Binary**: `projects/tab5-console/build/tab5_console.bin`
- **Size**: `1,409,712` bytes (`0x1582b0`, 66% free in 4 MiB factory partition at `0x10000`)
- **SHA-256**: `2ebaf26de788ef95c5c038ce33444d7fa7dbb6bb3fd14a934b6877cf4b4eaafd`
- **Host Checks**: `check_b1_decoder.py`, `check_keyboard_decoder.py`, `check_console_contract.py`, and `run-checks.sh` all `PASS`.
- **Flashed**: Installed to `0x10000` (`factory`) via `app-flash` on `/dev/cu.usbmodemXXXX`.

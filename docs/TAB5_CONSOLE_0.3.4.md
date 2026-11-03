# Console 0.3.4: Clock Sync, Manual RTC Setting, and Red Terminal Auto-Launch

31 October 2026. Following the physical testing and acceptance of Tab5 QuickNote 0.1.0,
two Console issues were addressed:
1. The Tab5 displayed the date as `01 October` instead of `31 October`. While the RX8130
   real-time clock tracked time continuously, it previously lacked a write interface
   and had remained on its initial build seed day.
2. The user requested that the "Terminal" tab across the top be given a red highlight
   in line with the Console styling, and that tapping it should automatically load
   the `Tab5Terminal` firmware from SD card if present (mirroring the one-tap launch
   pattern used for Ollama Analytics).

## Changes

### 1. HAL RX8130 RTC Write Support
- Added `bool tab5_rtc_set(const struct tab5_clock_time *time)` to `tab5_hal.h` and `tab5_hal.c`.
- Validates date/time inputs (ranges for second, minute, hour, day, month, leap years, and years 2000–2099).
- Commands the Epson RX8130 over I²C (`0x32`): stops RTC via control register `0x1e` bit 6, writes BCD values across registers `0x10..0x16` (including Sakamoto algorithm day-of-week), restarts the RTC, and clears the VLF (voltage-low) flag in register `0x1d`.

### 2. Clock Settings Overlay & Wi-Fi NTP Synchronization
- Tapping the clock display on the home screen (`y = 64..134`) opens `Overlay::clock`.
- **Main Clock View**:
  - Displays live time and full date (e.g. `Saturday, 31 October 2026`) formatted with weekday and month names.
  - Live clock ticking: the 1-second timer updates the clock overlay in real time without redrawing the background.
  - **Zone Button**: Cycles timezones (`Sydney (AEDT)`, `Melbourne (AEDT)`, `Brisbane (AEST)`, `Adelaide (ACDT)`, `Perth (AWST)`, `UTC`). Persists choice to NVS key `"zone"`.
  - **Wi-Fi Time Sync**: When connected to Wi-Fi with an IP address, tapping `WI-FI TIME SYNC` queries `pool.ntp.org` via ESP-IDF `esp_netif_sntp`, converts UTC to local time according to the selected zone rule, writes the exact civil time into the RX8130 RTC via `tab5_rtc_set()`, and displays confirmation.
  - **Set Manually Button**: Opens the manual date and time editor.
- **Manual Date & Time Editor**:
  - Interactive grid of 6 fields: `YEAR`, `MONTH`, `DAY`, `HOUR`, `MINUTE`, `SECOND`.
  - Tapping a field highlights it with a blue border.
  - Large `[- LOWER]` and `[+ HIGHER]` adjustment buttons modify the selected field with bounds checking and leap-year calendar awareness.
  - Live preview shows formatted date and time.
  - `[SAVE TO RTC]` writes the values directly to the RX8130 and updates Console state. `[CANCEL]` returns without saving.
  - Responsive layouts provided for both landscape (1280×720) and portrait (720×1280).

### 3. Red Terminal Tab & SD Auto-Launch
- In `draw_tabs()`, tab index 1 ("Terminal") is styled with `RED` edge and `RED` text.
- Tapping tab index 1 triggers `open_installer(AutoLaunch::terminal)`.
- If the SD card is present, the installer scans `/firmware/*.json` and searches for project `"Tab5Terminal"`. If found, it immediately validates, flashes to `ota_1`, and boots the terminal application. If not found, it shows notice `"TAB5 TERMINAL PACKAGE NOT FOUND"`.
- Tab index 2 ("Ollama") continues to auto-launch `"Tab5OllamaAnalytics"` as before.

## Verification

- Console host tests pass:
  - `python3 tests/check_b1_decoder.py`: PASS
  - `python3 tests/check_keyboard_decoder.py`: PASS
  - `python3 tests/check_console_contract.py`: PASS (verifies version 0.3.4, red tab, clock overlay, RTC write, and auto-launch targets).
  - `bash tests/run-checks.sh`: PASS (all 8 host tests pass under ASan/UBSan).
- ESP-IDF v5.5.5 build passed cleanly with zero warnings:
  - Image size: 1,403,264 bytes (0x156980 bytes).
  - 67% free in 4 MiB factory partition (`0x10000`).
  - SHA-256: `17da9e2a48f1cbefeacfed40e07d0bd74eb95bc7e743fffaa5248d779c5b10b1`.

## Installation & Readback

- Installed on physical ESP32-P4 (revision v1.3) via USB Serial/JTAG port `/dev/cu.usbmodemXXXX` using `app-flash` at `0x10000`.
- An independent readback of 1,403,264 bytes from `0x10000` was compared against `tab5_console.bin` and verified **byte-identical** (`cmp` exit code 0).
- Boot verified via serial trace after watchdog reset:
  - ESP32-P4 booted Console 0.3.4 cleanly.
  - Wi-Fi station initialized and re-associated with saved AP (`sta ip: <LAN_IP>`).

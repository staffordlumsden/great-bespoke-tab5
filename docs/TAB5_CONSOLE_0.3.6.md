# Console 0.3.6: Red Shell Tab & Tab5Shell Auto-Launch

1 November 2026. Following the implementation and validation of `Tab5Shell` (`0.1.3`),
TAB5 Console was updated to `0.3.6` with top-bar launcher integration for `Tab5Shell`.

## Changes

### 1. Red `Shell` Top Tab (Replacing `Terminal`) & One-Tap `Tab5Shell` Auto-Launch
- In `draw_tabs()`, tab index 1 is renamed from `"Terminal"` to `"Shell"`.
- It retains the `RED` (`0xef4444`) border and `RED` label text highlighting.
- Tapping `"Shell"` invokes `open_installer(AutoLaunch::shell)`, scanning `/firmware/*.json` on the SD card for project `"Tab5Shell"`, validating its image, flashing to `ota_1`, and booting directly into the Tab5 Shell app.
- Updated status messages to reference Shell: `"FINDING SHELL PACKAGE"`, `"CHECKING SHELL FIRMWARE"`, `"TAB5 SHELL PACKAGE NOT FOUND"`, and `"SHELL PACKAGE NEEDS THE SD CARD"`.

## Build & Flash Verification

- **Version**: `0.3.6`
- **Binary**: `projects/tab5-console/build/tab5_console.bin`
- **Size**: `1,409,712` bytes (`0x1582b0`, 66% free in 4 MiB factory partition at `0x10000`)
- **SHA-256**: `2930c76eaedf0fe781dffed39744d52fd3ea222e17bb4b39ece2bc995523d75f`
- **Host Checks**: `check_b1_decoder.py`, `check_keyboard_decoder.py`, `check_console_contract.py`, and `run-checks.sh` all `PASS`.
- **Flashed**: Installed to `0x10000` (`factory`) via `app-flash` on `/dev/cu.usbmodemXXXX`.

## Physical Acceptance

- **Date**: 1 November 2026
- **Result**: Physically tested and accepted by user ("works as intended"). One-tap launch from the red Shell top tab discovers, validates, and launches `Tab5Shell` from the SD card library.

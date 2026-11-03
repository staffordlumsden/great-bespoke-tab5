# Tab5 QuickNote 0.1.0 — Acceptance Checkpoint

31 October 2026. Physical acceptance confirmed by user: "works as intended. No issue."

## Scope and Delivery

Port of the Great Bespoke CYD Quicknote Micro firmware to the M5Stack Tab5 (`projects/tab5-quicknote/`), packaged as a native SD-installable application for the Tab5 resident Console launcher.

### Features Verified on Hardware

- **Terminal-Style Note Workspace**:
  - Replaces PixelFonts in the central editing and reading view with OFL-licensed **Tab5 Terminal Mono** typography (8pt, 10pt default, 15pt zoom levels).
  - Deep black terminal canvas (`#020406`) with slate border (`#334155`).
  - Gutter showing logical line numbers and continuation points.
  - Authentic blinking amber block cursor (`#f5b84b`).
  - History view highlighting markdown headings (`# ...`) in cyan/blue and timestamps (`- HH:MM`) in terminal green.
- **Great Bespoke Tab5 Console UI Chrome**:
  - Surrounding frame (header bar, status line, bottom touch toolbar) styled in neutral dark palette using CYD PixelFonts.
  - Live battery telemetry (voltage, percentage, charging indicator from INA226).
  - Real-time hardware clock display (RX8130 RTC).
  - Active keyboard connection indicators (`TAB5 KBD`, `USB`).
  - Six touch action buttons: `[HISTORY/EDIT]`, `[SAVE/PREV]`, `[CLEAR/NEXT]`, `[A-]`, `[A+]`, and `[CONSOLE]`.
- **Durable SD Card Storage**:
  - Timestamped daily notes saved to `/files/QuickNote/Daily/YYYY-MM-DD.md`.
  - Automatic draft checkpointing to `/files/QuickNote/.draft` after editing pauses and before returning to Console.
  - Atomic two-phase draft rotation (`.draft.tmp` → `.draft` → `.draft.prev`) for crash and power-loss resilience.
- **Dual Physical Keyboard Input**:
  - Primary: M5Stack Tab5 clip-on QWERTY keyboard on ExtPort1 (I²C addr `0x6D`, SDA GPIO0, SCL GPIO1, INT GPIO50).
  - Secondary: USB-A HID keyboard fallback through `tab5_hal`.
- **SD Launcher Lifecycle**:
  - Compliant with `tab5_app_boot`: presents the first complete frame before confirming OTA boot, setting factory Console as recovery target.
  - Console return button immediately unmounts SD and restarts into Console.

## Package Triplet

Packaged with `tools/package_sd_app.py` and staged to the 64 GB exFAT SD card under `/firmware/`:

- `Tab5QuickNote-0.1.0.bin`: 687,888 bytes (SHA-256: `3f381bd0f1de084540d89dd26e7d61df2eeb7a338840f564207dd308c3ef1d60`)
- `Tab5QuickNote-0.1.0.json`: 322 bytes
- `Tab5QuickNote-0.1.0-LICENSE.txt`: 3,537 bytes

# Tab5 QuickNote

Current version: `0.1.0`

Tab5 QuickNote is a native, SD-installable note-taking firmware for the M5Stack Tab5,
ported from the Great Bespoke CYD Quicknote Micro project.

It combines:
- **Terminal-Style Note Workspace**: Monospace typography using Tab5 Terminal Mono
  in 8pt, 10pt (default) and 15pt zoom levels, line numbers gutter, authentic terminal
  amber block cursor, and syntax highlighting for headings and timestamps.
- **Great Bespoke Tab5 Console UI Style**: Surrounding UI chrome (header bar, status
  bar, bottom touch buttons) rendered in CYD PixelFonts with battery telemetry, RTC
  time/date, and keyboard status.
- **Durable Markdown Storage on SD**: Notes are appended as timestamped Markdown
  entries to `/files/QuickNote/Daily/YYYY-MM-DD.md`. Unsaved drafts are checkpointed to
  `/files/QuickNote/.draft` for crash/power-loss recovery.
- **Dual Keyboard Support**: Primary physical input via the clip-on M5Stack Tab5
  Keyboard on ExtPort1 (I2C addr 0x6D) with USB-A HID keyboard fallback.
- **Touch Controls**: On-screen buttons for HISTORY/EDIT, SAVE/PREV, CLEAR/NEXT,
  A-, A+, and CONSOLE.

## Building and Packaging

```sh
cd firmware
../../idf.py build
python3 ../tools/package_sd_app.py \
  --image build/Tab5QuickNote.bin \
  --license ../Tab5QuickNote-LICENSE.txt \
  --output ../artifacts/sd-library
```

Install from Tab5 Console's SD Apps menu (`/firmware/Tab5QuickNote-0.1.0.bin`).

## Validation Status

- **Host sanitizer tests**: Passed (`clang++ -std=c++20`, covering editor operations, cursor movement, stats, draft checkpointing, `.draft.prev` rotation recovery, Markdown appending, day tail retrieval, and civil calendar math).
- **ESP-IDF v5.5.5 build**: Clean compilation (`Tab5QuickNote.bin`, 687,888 bytes).
- **Package verification**: Chip ID `0x0012` (ESP32-P4), 16 MB flash size, valid checksum and appended SHA-256 (`3f381bd0f1de084540d89dd26e7d61df2eeb7a338840f564207dd308c3ef1d60`).
- **Physical acceptance**: On 31 October 2026, the user confirmed: *"works as intended. No issue."* Version `0.1.0` is accepted.

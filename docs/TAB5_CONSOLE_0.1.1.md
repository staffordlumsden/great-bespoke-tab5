# TAB5 Console 0.1.1 — display and typography repair

Date: 2026-10-26 (Australia/Sydney)

## Outcome

Console 0.1.1 replaces the one-second full-screen repaint with dirty-region
rendering. The RTC clock alone updates every second. Battery telemetry, control
cards, storage/RAM information, power state and footer text redraw only when
their values or states change. The complete 1280x720 frame is reserved for
startup and whole-theme changes.

The Storage and RAM rows, USB keyboard status and copyright line now use the
exact PixelFonts face at 2x scale. Storage meters use the full available panel
width. A bounds calculation confirmed that all revised strings fit their
assigned panels.

## Image identity

- Project version: `0.1.1`
- Update label: `FLICKER FIX`
- Application binary: `608656` bytes (`0x94990`)
- Built and installed SHA-256:
  `2683146285450f93ef2224601bd97faa9ff447a3aa8bbf3a5b049a4a435ee04a`
- Independent readback: exact byte match

Exact installed readback:
`backups/2026-10-26-console-0.1.1/tab5-console-0.1.1-readback.bin`

## Recovery image

The previous complete 1 MiB application partition was preserved before flashing:

`backups/2026-10-26-pre-console-0.1.1/tab5-console-0.1.0-app.bin`

SHA-256:
`b65875c4df8b2a1d91fafdd855e403c18242c1df8f1922bb3f3e0f9b9a6bf0d4`

Only the app region at `0x10000` was changed. NVS, bootloader, partition table,
ESP32-C6 firmware and SD contents were preserved.

## Verification

- Build completed cleanly.
- Release contract passed, including a guard against `draw_ui()` in the
  one-second timer path.
- esptool transfer hash passed.
- Exact flash readback matched the built image.
- The Tab5 booted as version 0.1.1 and initialised ST7121 display/touch, 32 MB
  PSRAM, the 64 GB-class SD card and the USB HID keyboard.
- The firmware remained healthy through more than 15 one-second updates with no
  panic, cache error or display-driver error.

## Physical acceptance remaining

Serial and source evidence cannot measure perceived panel motion. Confirm on the
physical Tab5 that the clock changes without a whole-screen flash, and that the
larger Storage, RAM, keyboard and copyright text are readable and unclipped.

# TAB5 Console 0.3.2 — Matrix Lockscreen

30 October 2026 checkpoint. Console 0.3.2 adds a dedicated Lock button opening
the Matrix clock. Sleep retains screen-off/touch-to-wake behavior. The
Terminal-ready tab indicator from 0.3.1 remains green.

## Change summary

- New `main/matrix_clock.cpp` / `matrix_clock.hpp` module in the Console
  project. It renders a classic green glyph cascade using the same checked-in
  CYD PixelFonts atlas (7×5 thin at 2× scale) and overlays a central panel with
  the RX8130-backed time and date.
- Tapping **Lock** enters the Matrix clock. Its button has a closed padlock,
  bright label, green border and deterministic static character trails.
  Any touch returns to Console and redraws the dashboard.
- **Sleep** sets brightness to zero, consumes the wake press/release and
  restores saved brightness. No Matrix animation is invoked by Sleep.
- Landscape presents Sleep, Lock, Rotate and Power Off in one row. Portrait
  uses a two-by-two grid. Drawing and hit testing use `power_layout.hpp`.
- Version string is now `0.3.2` and the update description is
  `MATRIX LOCKSCREEN`. The Terminal tab remains green as in 0.3.1.

## Implementation notes

- The animation targets ~14 fps and draws the full logical display each frame.
  It is intentionally decoupled from the normal dirty-region renderer so it does
  not interfere with the one-second clock or periodic telemetry update rules.
- Glyphs are chosen from printable ASCII letters, digits and common symbols.
  Brightness levels give a bright head, green mid-tail and dim tail against a
  near-black background.
- The clock panel is recentered automatically for portrait and landscape through
  `tab5_display_width()` / `tab5_display_height()`; the HAL handles the active
  rotation transform.
- Source files added to the build:
  - `projects/tab5-console/main/matrix_clock.hpp`
  - `projects/tab5-console/main/matrix_clock.cpp`
  - `projects/tab5-console/main/CMakeLists.txt` now lists both source files.

## Verification

The complete Console host suite passed:

- `tests/run-checks.sh` — boot lifecycle, Weather snapshot, installer runtime,
  installer safety and installer UI checks.
- `tests/check_console_contract.py` — updated to assert `0.3.2`, the green
  Terminal branch, the `MATRIX LOCKSCREEN` description and the presence of
  `matrix_clock_run` in the source; separate Sleep/Lock paths and deterministic
  Lock art are checked too.
- `tests/check_power_layout.cpp` — ASan/UBSan tests for all control bounds,
  corner/centre hit tests and non-overlap in portrait and landscape.
- B1 decoder and USB keyboard decoder checks passed. The USB host test fixture
  was updated for the pre-existing optional key-filter callback; firmware USB
  decoding was not changed by this button work.
- `tools/render_power_preview.py` compiles the actual drawing functions and
  renders both layouts with synthetic state. Both previews were inspected.

ESP-IDF v5.5.5 built the ESP32-P4 application successfully. The image is
1,369,568 bytes, leaving 67 percent of the 4 MiB resident factory partition
free. SHA-256:
`46d2f4cd9cebd94d5daadfae9252614ce70015af0fb44726759513a613d70a36`.

## Installation on 30 October 2026

The connected target was confirmed as ESP32-P4 revision v1.3 with one serial
owner. A complete 16 MB flash backup was taken before the application-only
`app-flash` at `0x10000`. Its resident image identified as Console 0.3.1 and
the shared partition layout matched the expected factory/two-slot map.

The flash tool verified the write hash. A separate complete 16 MB readback
then established that the 1,369,568-byte application matches the build exactly.
Bootloader, partition table, NVS/PHY, OTA data and both app slots match their
pre-write bytes. The entire readback matches the expected application-only
write, including erased padding to the final application sector boundary.
No SD or C6 writes were performed.

The supported P4 watchdog reset booted Console 0.3.2. Serial evidence confirms
launcher layout READY, display/touch ready, ST7121 firmware 1, USB HID ready,
and a completed two-buffer first frame before backlight reveal. No panic or
download-mode marker was captured in the 20-second boot observation. SD was
not mounted during this boot. These are serial results; physical button and
animation acceptance remains pending.

Local recovery and raw evidence are under the ignored
`backups/2026-10-30-console-0.3.2/attempt.mhMy7h/` directory. It includes the
before/after flash dumps, exact installed app, write/boot logs and receipt.

## Physical acceptance required

A host build cannot prove the animation frame rate, touch-to-wake latency or
visual readability on the five-inch ST7121 panel. After flashing, confirm:

- the **Lock** button has static green glyph trails and a readable padlock;
- tapping **Lock** fills the screen with green falling glyphs;
- **Sleep** switches the screen off, and a wake touch restores Console;
- a central panel shows the current RTC time and date;
- any touch returns to the dashboard cleanly;
- the Terminal tab remains green;
- normal brightness, rotation, SD launch and power controls still work.

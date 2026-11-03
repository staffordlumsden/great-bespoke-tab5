# Console 0.3.3: Column rain and PIN lock

30 October 2026. The user accepted the basic 0.3.2 Lock/Sleep behavior but
reported that its moving single glyphs did not resemble the CYD rain.

The Matrix renderer now ports the CYD Matrix Rain column model: independent
head positions, per-column speeds, persistent glyph arrays, 8–23-character
tails, pale heads, green middle sections and dim tails. Columns respawn after
leaving the screen. The RTC clock/date stays centered, with an explicit
`CLOCK NOT SET` fallback. The dedicated Lock button retains its static art;
Sleep still switches off the display until a wake touch.

Unlocking now requires two completed taps within 450 ms and 80 pixels, followed
by a four-digit PIN configured at build time via a private, gitignored header and **OK**. The keypad has masked entry,
Back, Cancel and wrong-PIN feedback. Wrong input clears the entry and briefly
blocks further input; Cancel clears the entry and returns to rain without
unlocking. The opening touch is consumed, a held finger emits at most one tap,
and drags/long holds cannot open the keypad. This is a local UI lock with a fixed
PIN; reset continues to boot Console normally.

## Provenance

The reference is Great Bespoke CYD `projects/matrix-rain/` candidate
`0.1.1-fixed`, especially `matrix.cpp`, `touch_gate.h`, `pin.cpp` and
`ui_engine.cpp`. Tab5 adapts the column model to native portrait/landscape,
uses its existing PixelFonts/HAL/RTC and implements the user's double-tap
requirement. No CYD hardware or firmware was changed.

## Verification

- Console host suite and B1/USB decoder tests passed.
- ASan/UBSan model tests cover 2,000 animation frames, bounded glyph arrays,
  column speeds, tail intensities and repeated respawning.
- The production touch/PIN model is tested in both geometries: startup gating,
  single/slow/distant taps, double tap, held input, drags, incomplete/overflow
  entry, wrong PIN, retry, Back, Cancel and successful unlock after OK.
- `tools/render_matrix_preview.py` compiled the production drawing code with
  synthetic RTC/RNG/HAL data. Rain/clock and keypad previews were inspected.
- ESP-IDF 5.5.5 build passed for the P4 pre-v3/revision-v1.0-minimum branch.
  The image is 1,382,352 bytes; 67 percent of the 4 MiB factory slot remains free.
- Image SHA-256:
  `35b2876625a73da06a15424c4c164a5ddf72d52001e81f10b971b8adaa56ec63`.

## Acceptance

Installed on the connected ESP32-P4 revision v1.3 using `app-flash` at
`0x10000`. A fresh backup covered the entire factory partition and preceding
system regions. Before writing, the installed application matched the verified
0.3.2 recovery image. After writing, an independent readback verified every
application byte, plus unchanged bootloader, partition table, NVS/PHY and the
remaining factory bytes outside final-sector padding. OTA slots, C6 and SD
were not written. Local recovery/logs are ignored under
`backups/2026-10-30-console-0.3.3/attempt.bx7m0x/`.

The P4 watchdog restart booted Console 0.3.3; serial capture confirms launcher
READY and first-frame presentation, with no captured panic.

Physical acceptance of 0.3.3 is pending. Check falling multi-character columns,
double-tap keypad opening, the configured PIN plus OK, wrong-PIN rejection,
Back/Cancel, and portrait/landscape behavior. The previously accepted 0.3.2
functional report does not establish these new flows.

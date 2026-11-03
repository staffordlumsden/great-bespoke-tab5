# Tab5 isolated HAL diagnostic

Target: ESP32-P4 **v1.3**, ST7121, 16 MB flash, 32 MB PSRAM.
This project brings up display, touch, microSD and a USB-A boot-protocol HID
keyboard. It does not initialize networking, SSH, BLE, emulation or the future
I2C keyboard accessory.

## Build and flash

The root `idf.py` wrapper sources `.tools/export.sh`. On this host it uses the
existing ESP-IDF v5.5.5 installation and workspace-local Python/toolchains.
Configure the local environment script when moving this workspace to another
host. Internet is needed on the first build to resolve the registry manifest.
The resolved versions and hashes are recorded in `dependencies.lock`.

From this project directory:

```sh
../../idf.py build
../../idf.py -p /dev/cu.usbmodemXXXX flash
../../idf.py -p /dev/cu.usbmodemXXXX monitor
```

For a fresh target configuration (this resets sdkconfig/build state):

```sh
../../idf.py set-target esp32p4
```

Defaults select the **pre-v3 silicon** branch, minimum revision v1.0, and 16 MB
flash. Ordinary `idf.py flash` works. Do not force a v3.x image onto this v1.3
unit. The previous bootloader-entry illegal-instruction loop is absent with
the compatible configuration. No full-chip erase is needed.

The current factory app partition is 1 MB, sufficient for this roughly 531 KiB
bring-up image. This is not an OTA/application partition design.

## Controller verification and display

An ACK at 0x55 alone cannot distinguish ST7121 from ST7123. Before initializing
the display, the HAL reads the Sitronix firmware version at 16-bit I2C register
0x0000, revision at 0x000C, and geometry at 0x0005. It permits the ST7121 display
driver only for version 1 and the expected 720x1280 native geometry. Version 3
or an unexpected response stops display initialization with an error.

Panel reset uses expander 0x43: LCD_RST=P4, TP_RST=P5, direction register 0x03,
output 0x05. LCD_RST is asserted low, then released as input with pull-up, as
required by M5Stack for these panels. It is never driven high push-pull.

`components/esp_lcd_st7121` is the M5Stack factory driver's source and header.
The HAL uses native 720x1280 RGB565 scanout with a 1280x720 landscape API and
matching touch transform. It owns one 1.84 MB framebuffer. Rectangle operations
update it; `tab5_display_present()` flushes CPU cache to PSRAM. Updates may tear:
this is a single-buffer diagnostic, not a production graphics pipeline.
The drawing and touch APIs have one caller: the application task.

The diagnostic shows RGB bars, subsystem status, uppercase diagnostic text,
keyboard input, touch coordinates and an uptime counter. Touch the bottom strip
to draw white marks. It does not implement a terminal parser.

## SD card

The HAL uses SDMMC slot 0, 4-bit, 20 MHz; GPIO43=CLK, GPIO44=CMD,
GPIO39..42=DAT0..3, with LDO channel 4 power control. Mount path is `/sdcard`.
The actual 64 GB card uses exFAT. ESP-IDF 5.5.5 disables exFAT in its FatFs
configuration, so this project overrides the **fatfs component locally**, with
exFAT and heap-allocated long filenames enabled. The shared SDK is unchanged.

Automatic formatting is disabled. A boot self-test exclusively creates
`/sdcard/.tab5-hal-check.tmp`, writes and syncs its contents, closes/reopens it,
compares the bytes and deletes it. It refuses to overwrite an existing file.
If power is interrupted during this test, an existing temporary file causes
subsequent tests to fail; inspect it before removing it manually.
Mount and unmount operate only on SD resources and do not reinitialize the board.

## USB keyboard

The HAL enables USB5V_EN (P3 on expander 0x44), retaining other output bits.
It runs the USB host event loop, HID background driver and a separate connection
worker, with queued input and disconnect/reconnect handling. Enumeration alone
is not evidence that typing works; use the serial/on-screen checks below.

One boot-protocol keyboard is supported, including a boot keyboard interface
on a composite receiver. Other mouse/vendor interfaces are ignored. Translation
uses US layout: letters, numbers, basic punctuation, Enter, Escape, Backspace,
Tab, Shift, Caps Lock, Ctrl+A..Z, Alt prefix, arrows and navigation keys.
The polling API emits ASCII/control bytes and ANSI sequences. There is no key
repeat, keypad/function-key mapping, arbitrary HID report parser, or lock LED
synchronization yet. A 256-byte input queue drops complete keys on overflow
with a warning rather than emitting a partial ANSI sequence.

## Acceptance checks

1. Boot serial log confirms rev v1.3, 16 MB flash and 32 MB PSRAM; no panic loop.
2. Physical read logs firmware=1, revision 1.80.1.16 and 720x1280 geometry.
3. Verify the display is readable in landscape and RGB bars look correct.
4. Tap all four corners; coordinates should follow the visible landscape axes.
5. SD mount reports exFAT; exclusive create/write/read/compare/delete reports PASS.
6. Type `Tab5 test`, Shift, Backspace and arrows; inspect input and serial hex.
7. Disconnect/reconnect USB receiver; type again and confirm recovery.
8. Observe uptime/heap logs for stability. The diagnostic is left running after tests.

See `../../docs/BRINGUP_RESULTS.md` for measured results and logs.

## Upstream provenance

- Registry BSP: `espressif/m5stack_tab5` 1.3.2, ESP-BSP commit
  `f259a6abd5808019b1903a422092a4234ca95a51`.
- ST7121 source/header and board reset/timing/detection reference:
  https://github.com/m5stack/M5Tab5-UserDemo at commit
  `b4e356bc491ca070d54004718dad789c07d5fc93`.
- Local FatFs base: installed ESP-IDF v5.5.5, Git HEAD
  `b774170ff46c393eeb5e495ea37936038d3f4f4f` (SDK reports dirty).
- Board pins/reset guidance: https://docs.m5stack.com/en/core/Tab5.

The P4 flash was backed up before writing and the new flash segments verified
by esptool hashes. The C6 firmware has not been accessed or rewritten.

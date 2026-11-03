# Tab5 isolated HAL bring-up — 26 October 2026

The requested workspace and isolated HAL have been built, flashed and exercised
on the physical Tab5. Application porting stops here.

## Verified hardware and configuration

| Item | Evidence |
|---|---|
| Attached target | ESP32-P4 revision v1.3, USB Serial/JTAG (unit MAC retained in local evidence only; rediscover the serial port before use) |
| SDK target | ESP-IDF v5.5.5-dirty, `esp32p4`, pre-v3 silicon, supported revisions v1.0–v1.99 |
| Memory | 16 MB flash, 32 MB PSRAM; boot PSRAM memory test passes |
| Physical panel identification | I2C 0x55, register 0x0000 = **1**, revision **1.80.1.16**, max touch geometry **720x1280**, 10 touch slots |
| Panel selection | ST7121 driver selected only after physical identity/geometry check |
| Registry BSP | `espressif/m5stack_tab5` **1.3.2**, resolved dependency and lock file; it lacks the ST7121 driver |
| USB HID component | `espressif/usb_host_hid` **1.2.1** |

## Hardware acceptance

| Test | Result |
|---|---|
| Boot | PASS: ordinary `idf.py flash`, no forced image, no illegal-instruction/panic loop |
| Display | PASS: ST7121 initialized with M5Stack factory driver; operator confirms readable diagnostic and input |
| Touch | PASS: physical events near all four corners with landscape coordinates: roughly (25,28), (1231,46), (1223,667), (0,671) |
| SD | PASS: SD64G, 124735488 sectors × 512 bytes = 60906 MiB, exFAT, 4-bit SDMMC at 20 MHz |
| SD filesystem | PASS: exclusive create, write, fsync, close/reopen, read/compare, delete; formatting disabled |
| Keyboard enumeration | PASS: Logitech receiver VID 046d / PID c52b; boot keyboard interface opened; mouse/vendor interfaces ignored |
| Typing | PASS: physical key usages and output bytes observed, including shifted T, lowercase letters, digit 5 and spaces |
| Arrows | PASS: physical left/right/up/down emitted `1b 5b 44`, `1b 5b 43`, `1b 5b 41`, `1b 5b 42` |
| USB unplug/replug | Operator answered yes to the combined reconnect-and-type acceptance procedure; the captured serial trace does not independently show a disconnect/reconnect cycle |
| Stability | At least four minutes without panic; sampled free internal heap 546619 bytes, PSRAM 31675764 bytes, main-task stack high-water mark 6060 |

## Host decoder verification

`python3 projects/tab5-hal-bringup/tests/check_keyboard_decoder.py` passes.
It compiles the production translation/report functions with clang address and
undefined-behaviour sanitizers, then checks typing, Shift/Caps/Ctrl/Alt, ANSI
navigation, held-key suppression, duplicate usages, rollover and invalid lengths.
All 65536 usage/modifier byte combinations complete with bounded output.
This does not simulate USB electrical behaviour or replace the physical tests.

## What changed

- Corrected reset handling: expander 0x43 P4 is LCD_RST, P5 is TP_RST; registers
  0x03/0x05 are direction/output. LCD reset releases as input with pull-up.
- Replaced display and keyboard placeholders with working implementations.
- Added checked controller identification and selected ST7121 factory driver.
- Added native display scanout, landscape drawing/touch mapping, cache flush
  and the standalone diagnostic screen.
- Added SD power control, explicit SDMMC pins and independent mount/unmount APIs.
- Added project-local FatFs exFAT support and enabled heap long filenames;
  corrected an upstream FF_USE_LABEL configuration macro for the exFAT path.
- Enabled USB-A power without changing other expander output bits; installed
  host and HID event tasks, translation, queues and reconnect handling.
- Replaced obsolete offline/forced-flash instructions with the working workflow.

## Preserved state and evidence

- Before this repair: `backups/2026-10-26-pre-hal-fix/project-sources.tgz`.
- Complete pre-write P4 flash: `backups/2026-10-26-pre-hal-fix/esp32p4-flash.bin`
  (16777216 bytes), with `SHA256SUMS`.
- Build: `logs/hal-build.log`.
- Flash: `logs/hal-flash.log`; esptool verifies hashes of bootloader, app and
  partition-table writes.
- Flashed binary SHA-256 manifest: `logs/flashed-image-sha256.txt`.
- Physical serial trace: `logs/hal-hardware-test.log` and sanitized
  `logs/hal-hardware-test-clean.txt`.
- Decoder check: `logs/keyboard-decoder-test.txt`.
- Factory driver and local filesystem provenance are documented beside their
  components and in the project README.

## Bounds of this result

The diagnostic uses one framebuffer (updates may tear), one US-layout boot HID
keyboard, and press events without repeat or keypad/function-key support. Its
font displays uppercase diagnostic text; it is not a terminal renderer. The
factory 1 MB app partition is sufficient for this build, but OTA layout is not
planned here. The SDK's SD power-control setup emits an LDO initialization
warning about voltage 0; the card then mounts and its filesystem test passes.
SD unmount/remount and power-loss recovery were not separately exercised.

The operator confirmed the display/input acceptance check. Networking, BLE,
SSH, the I2C keyboard accessory and application integration are outside this
bring-up. The C6 firmware was not accessed or rewritten. The existing SD card
was not formatted. Leave the diagnostic running for further bench testing.

# Great Bespoke Tab5 - contributor instructions

Read `README.md`, the current Console checkpoint and the affected project's
`README.md` before changing firmware. Preserve checked-in `sdkconfig` and
`dependencies.lock` files.

## Device and partition safety

- Tested unit: ESP32-P4 revision v1.3, 360 MHz, 16 MB flash, 32 MB PSRAM,
  ST7121 display/touch and factory ESP32-C6 firmware.
- Keep the pre-v3 silicon build branch and minimum chip revision v1.0.
- Console is resident at `0x10000` in a 4 MiB factory partition. SD applications
  use the inactive 5 MiB OTA slot through the Console installer.
- Never flash an SD application over the factory Console, erase NVS, overwrite
  the factory C6 image, format the SD card or force an incompatible image.
- Confirm the actual P4 port and chip before any explicit device operation.
  Normal Console updates use `app-flash`; device work is never implied by a
  build or test request.

## Hardware rules

- Detect the panel at I2C `0x55` register `0x0000`; value 1 selects ST7121.
  An address ACK alone does not distinguish ST7121 from ST7123.
- Release LCD_RST as input with pull-up; never drive it high push-pull.
- Native scanout is 720×1280. Display and touch must use the same rotation
  transform, and portrait UI must reflow rather than clip a landscape canvas.
- Never call Wi-Fi or radio APIs before `tab5_radio_power_set(true)` and
  `tab5_radio_init()` succeed.
- Preserve PixelFonts for Console chrome, readable status text and dirty-region
  rendering. Do not introduce periodic full-screen redraws.

## Build and validation

Use ESP-IDF v5.5.5 through `../../idf.py` inside the selected project. Configure
host paths only in the ignored `.tools/export.local.sh`.

Run the affected project's native checks before its build. For Console:

```sh
cd projects/tab5-console
python3 tests/check_b1_decoder.py
python3 tests/check_keyboard_decoder.py
python3 tests/check_console_contract.py
bash tests/run-checks.sh
../../idf.py build
```

Host tests and successful builds do not prove physical display, touch, SD,
radio, pairing or input. Record observed results and limitations in the relevant
`docs/` checkpoint.

## Publication boundary

- Keep source, tests, locks, configuration, provenance and reviewed sanitized
  reports in Git.
- Keep raw logs, NVS/full-flash dumps, recovery binaries, SDKs, managed
  components, build trees, API keys, SSH material and host-specific settings
  out of Git.
- Use placeholders for serial ports, mount points, LAN addresses and local paths.
- Run `python3 tools/check_public_release.py` before every push.
- Do not add proprietary embedded fonts or other unlicensed binary assets.

Human instructions take precedence over historical handoff notes.

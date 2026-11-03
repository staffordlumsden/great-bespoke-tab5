# Tab5 Terminal operating rules

These instructions apply to this project, including the imported upstream
snapshot.

- The installed Tab5 is ESP32-P4 revision v1.3 with 16 MB flash, 32 MB PSRAM,
  an ST7121 display/touch controller and the factory ESP32-C6 Wi-Fi image.
- Keep the terminal permanently in landscape rotation 0. Do not add a rotate
  button or inherit Console's four-way rotation setting.
- The M5Stack Tab5 Keyboard accessory is the primary keyboard: I2C address
  `0x6D` on ExtPort1, SDA GPIO0, SCL GPIO1 and active-low INT GPIO50. USB-A HID
  remains an independent fallback.
- Preserve the resident Console and its shared partition table. This is an SD
  application image; never use this project's flash targets.
- Keep ESP-IDF at 5.5.5, `esp_hosted` at 1.4.0 and `esp_wifi_remote` at 0.8.5.
  The firmware may query and report the C6 version but must never update, erase
  or reflash the C6.
- Files under `upstream/c6_updater/` and `upstream/slave_c6_fw/` are provenance
  only. Never build or run their update scripts for Great Bespoke Tab5.
- Never commit `/private`, `tab5-terminal.json`, SSH keys, passwords, hostnames,
  SSIDs, raw device logs, SDKs or generated build trees.
- Use `tab5_app_boot`: present the first complete frame before confirming the
  app and leave reset/power-cycle recovery directed to the factory Console.
- Redraw only damaged terminal cells, the cursor cell, or changed status
  regions. Do not introduce periodic full-screen redraws.

Before delivery run the imported VT100 tests, the local contract checks, an
ESP-IDF build, package validation and a secret scan. Physical status is not
accepted until the exact packaged image has been installed through Console and
observed on the device.

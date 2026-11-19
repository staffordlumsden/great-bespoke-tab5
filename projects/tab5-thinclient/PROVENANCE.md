# Provenance

`Tab5ThinClient` is a Great Bespoke Tab5 Console-skinned ESP-IDF v5.5.5 fork of:

- **Upstream repository:** `https://github.com/airpocket-soundman/Tab5_thinclient`
- **Forked commit:** `7790627d53490677b40d2220cb5141f687d4c9e9` (MIT License)
- **GitHub fork:** `https://github.com/staffordlumsden/Tab5_thinclient`

## Adapted Upstream Materials

- **Core thin-client logic & UI workflows:** VT100/ANSI emulator, line-buffered
  CLI, settings screens, US/JP HID keyboard mapper, MicroPython GPIO bridge
  (`tab5_gpio_module.c` + external pin policy), FUSE bridge host helper
  (`data/tab5-fuse-server.py`), and MicroPython demos (`demos/`).
- **Embedded MicroPython runtime:** `firmware/components/micropython_embed/` from
  upstream `lib/micropython_embed` (MIT License).
- **Terminus bitmap font data:** `firmware/components/thinclient_fonts/TerminusBitmap.*`
  from upstream `src/fonts/TerminusBitmap.*` (derived from Terminus Font,
  SIL OFL 1.1).

## Great Bespoke Tab5 Integrations

- **Hardware & Boot Contract:** Shared `tab5_hal`, `tab5_app_boot`,
  `esp_lcd_st7121`, and exFAT `fatfs` components from `../tab5-console/components/`
  to run inside a 5 MiB OTA slot alongside the resident Tab5 Console with
  rollback confirmation (`tab5_app_boot_ready(true)`) and visible `CONSOLE`
  return button (`tab5_app_boot_return_to_console()`).
- **Console Chrome & Theme:** `PixelFonts` (`pixelfont_7x5_thin`) chrome buttons,
  Console Dark palette (`0x101215` / `0x1d2228` / `0x353d46`), and 4-way
  display/touch rotation reflow (`1280x720` landscape and `720x1280` portrait),
  while retaining `AsciiFont8x16`, `lgfxJapanGothic`, `TerminusBitmap`, and
  geometric/box/block/braille/powerline glyphs inside the thin-client main area.
- **Networking & SSH:** Hosted Wi-Fi management via `../tab5-weather/components/tab5_weather_wifi`
  (sharing Console's NVS Wi-Fi credentials) and BSD-3-Clause `libssh2_esp`
  (from `../tab5-terminal/firmware/components/libssh2_esp`) supporting public-key
  authentication (`/sdcard/private/...`), password authentication, and NVS
  `ssh_tofu` host-key verification. Replaces upstream LGPL `LibSSH-ESP32`.
- **Fonts & Image Decoding:** `AsciiFont8x16` (`Ascii8x16.h`) and U8g2-compressed
  `lgfxJapanGothic_{16,20,28,36}` tables extracted from `lovyan03/LovyanGFX`
  (`lgfx_font_japan.c`); JPEG/PNG/BMP decoding via shared `image_framework` and
  `compression_framework` from `../tab5-media-player/esp-devkit/libs/`.
- **Tab5 Keyboard Drivers:** Clip-on I2C (`0x6D`, GPIO 0/1/50) keyboard driver
  adapted from `../tab5-terminal` / `../tab5-shell` plus USB-A HID polling via
  `tab5_hal`.

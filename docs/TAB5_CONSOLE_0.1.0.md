# TAB5 Console 0.1.0 — release evidence

Date: 2026-10-26 (Australia/Sydney)

## Outcome

TAB5 Console 0.1.0 was built for ESP32-P4 and installed to the attached Tab5's
existing factory application partition. Only the application region beginning
at `0x10000` was changed. The bootloader, partition table, NVS, ESP32-C6
firmware and SD card contents were preserved.

The app uses the original Great Bespoke CYD Console PixelFonts atlas, supports
the verified ST7121 display/touch hardware, and implements the first Console
screen and controls documented in `projects/tab5-console/README.md`.

## Build and image identity

- Project version: `0.1.0`
- ESP-IDF target: `esp32p4`
- Application binary: `608080` bytes (`0x94750`)
- Factory partition: `1048576` bytes; 42% remains free
- Built image SHA-256:
  `2ca2b50c4bd328e8cfc954eb2ec7591fb3bccf7e4accf41ea15141597cb403ff`
- Independent flash readback SHA-256:
  `2ca2b50c4bd328e8cfc954eb2ec7591fb3bccf7e4accf41ea15141597cb403ff`
- Binary comparison: exact match
- Contract test: pass

The exact readback is retained at
`backups/2026-10-26-pre-tab5-console/tab5-console-readback.bin`.

## Recovery point

Before installing Console, the complete 1 MiB previous HAL application region
was read from `0x10000` and saved as
`backups/2026-10-26-pre-tab5-console/tab5-hal-app.bin`.

Its SHA-256 is
`3e9301ae27a607e760ef7107533619284cfa3b08392545e9f95b042c0636f5f8`.
The earlier full-device pre-HAL backup remains in
`backups/2026-10-26-pre-hal-fix/`.

## Boot evidence

The flashed image booted without panic and reported:

- TAB5 Console 0.1.0 on ESP32-P4 revision v1.3
- 16 MB flash and 32 MB PSRAM, with PSRAM memory test passing
- Physical controller at I2C address `0x55`, firmware `1`, identified as ST7121
- 1280 x 720 landscape RGB565 display and touch ready
- 64 GB-class SD mounted as exFAT: 60906 MiB capacity, 60873 MiB free
- USB host/HID ready, followed by successful keyboard attachment
- ESP32-C6 deliberately powered off because hosted Wi-Fi/BLE is not yet present

No RTC or INA226-unavailable warnings were emitted during initialization. The
clock and battery values are rendered by live reads in the Console UI.

## Physical acceptance still required

Serial evidence cannot prove visual alignment or touch hit boxes. On the Tab5,
check Appearance, brightness, volume, Wi-Fi/Bluetooth state labels, RAM refresh,
sleep/touch-wake and the first Power Off confirmation. Avoid the second Power
Off tap until all other checks are complete.

# CYD USB Drive companion snapshot

Read `README.md` and `projects/usb-drive/README.md` first. This is a selected,
portable snapshot of the original CYD USB Drive project, with shared components
kept at their original relative paths. `SOURCE_MANIFEST.json` records the copied
source bytes. It is not a complete CYD Console checkout or a Tab5 port.

- Target: original ESP32-2432S028R, ESP32-D0WD-V3, 4 MB flash, no enabled PSRAM.
- Firmware: USBDrive 0.1.2, ESP-IDF v5.5.5. Preserve the included partition map.
- Mac software: universal Intel/Apple Silicon UD1 serial-to-WebDAV bridge.
- Retain the PixelFonts and all third-party notices. No additional licence is
  granted for original project code.
- Run the native tests and Go tests as documented. A successful host test is
  distinct from physical device or Finder acceptance.
- Install the released application through the CYD Console SD library. Never
  flash it to the factory Console address or to an ESP32-P4/Tab5.
- `projects/usb-drive/tools/device_update.py` is historical source: it depends
  on original-host private identity/backups and a launcher build not included
  here. It is not a portable installer. Do not remove its checks to make it run.
- Never commit private SD data, keys, raw UART, flash/NVS dumps, SDK caches or
  generated test output. The release contains only the selected public SD app
  package and the existing signed Mac application.

Packaging this snapshot does not resume the paused Tab5 BLE investigation or
authorize flashing either device. Source on the original CYD workspace was not
changed during export.

# Tab5 QuickNote operating rules

These instructions apply to `projects/tab5-quicknote/`.

- Target hardware: ESP32-P4 revision v1.3 with 16 MB flash, 32 MB PSRAM, and
  an ST7121 display/touch controller.
- Rotation is permanently landscape rotation 0 (1280x720).
- Input: M5Stack Tab5 clip-on QWERTY keyboard on ExtPort1 (I2C addr 0x6D,
  SDA GPIO0, SCL GPIO1, INT GPIO50) is the primary physical keyboard; USB-A HID
  keyboard via HAL is an independent fallback. On-screen touch buttons provide
  touch navigation.
- Terminal styling: The main note editing and reading area uses Tab5 Terminal Mono
  typography (8pt, 10pt default, 15pt zoom levels) on deep terminal black with
  block cursor, line gutter, and markdown syntax highlighting. Surrounding UI
  chrome (header, status bar, toolbar) retains Great Bespoke Tab5 console style
  in PixelFonts.
- Storage: Daily timestamped notes are appended to
  `/sdcard/files/QuickNote/Daily/YYYY-MM-DD.md`. Unsaved text is checkpointed
  to `/sdcard/files/QuickNote/.draft`.
- Lifecycle: Uses `tab5_app_boot`: presents the first complete frame before
  confirming boot, and directs recovery to factory Console.
- Never use flash targets that overwrite factory Console. Package as an SD app
  and launch from Console.

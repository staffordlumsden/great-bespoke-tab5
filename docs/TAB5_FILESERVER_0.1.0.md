# TAB5 File Server 0.1.0 — SD Card Web Explorer & Hotspot

## Overview

`Tab5FileServer` (`0.1.0`) is an SD-installable application for the M5Stack Tab5
(ESP32-P4 revision v1.3 with ST7121 display and ESP32-C6 SDIO companion). It turns
the Tab5 into a network-accessible SD card File Explorer over either a local
Wi-Fi LAN or a dedicated WPA2-PSK Wi-Fi Hotspot.

## Requirements Implemented

1. **LAN Mode (`WIFI_MODE_STA`)**:
   - Connects automatically to saved Wi-Fi credentials from the shared
     `tab5console` NVS namespace (or via the on-device 2.4 GHz Wi-Fi scanner and
     touch/keyboard password picker).
   - Runs a UDP mDNS responder (`224.0.0.251:5353`) advertising `tab5.local`,
     `tab5-files.local`, and `tab5-fileserver.local` so any device on the same
     LAN can discover and open the Tab5 File Explorer.
2. **Wi-Fi Hotspot Mode (`WIFI_MODE_AP`)**:
   - Broadcasts the `Tab5-FileServer` WPA2-PSK hotspot at `192.168.4.1` and
     answers DNS/mDNS queries for `tab5.local` (`http://192.168.4.1/`).
3. **Browser File Explorer / Finder for SD Card Root**:
   - Provides browser navigation starting at the SD card Root directory
     (`/` mapped to `/sdcard`), with clickable breadcrumbs, parent navigation,
     real-time filtering, and folder creation (`+ FOLDER`).
4. **Console Visual Style (PixelFonts + Dark/Light Modes)**:
   - Serves TrueType versions of the checked-in `PixelFonts` faces
     (`Tab5Pixel13`, `Tab5Pixel11`, `Tab5Pixel7`) directly from flash
     (`/fonts/pixelfont*.ttf`) so authentic Console typography works even when
     offline in Hotspot mode.
   - Implements the exact Console Dark (`#101215` / `#1d2228`) and Light
     (`#eef1f4` / `#ffffff`) color schemes with a live theme toggle.
5. **Unified 8-Digit Passcode (Hotspot & Web Explorer)**:
   - Displays an 8-digit numeric passcode on the Tab5 screen in 4x PixelFonts.
   - Joining the `Tab5-FileServer` Hotspot requires this 8-digit passcode as the
     WPA2 password, and opening the Browser File Explorer requires entering the
     same 8-digit passcode on the Console-styled lock screen.
   - Tapping `GENERATE NEW 8-DIGIT PASSCODE` on the Tab5 rotates the code,
     updates the Hotspot WPA2 password, and invalidates existing browser sessions.
6. **File Uploads, Downloads & Inline HTML Streaming**:
   - **Uploads**: Supports single/multi-file picker and drag-and-drop uploads
     (`POST /api/upload`) streamed in 16 KB chunks to the SD card with a live
     progress bar.
   - **Downloads**: Accessing any non-HTML file sends
     `Content-Disposition: attachment; filename="..."` to download the file to
     the client device.
   - **HTML Streaming**: Accessing `.html` or `.htm` files sends
     `Content-Type: text/html; charset=utf-8` with `Content-Disposition: inline`,
     streaming the HTML document directly to the client browser.

## Validation & Staged SD Package

- **Host Unit & Sanitizer Suite**:
  - `bash projects/tab5-fileserver/tests/run_tests.sh` (`PASS` under `-fsanitize=address,undefined`).
- **Firmware Build & SD Triplet**:
  - `Tab5FileServer-0.1.0.bin`: `984,720` bytes (`0xf0690`)
  - SHA-256: `dbf0381c34b5d9619cd145bf776ab2f2c244530dc9818c066d44f19314604ce2`
  - Chip target: `esp32p4` (minimum chip revision `100` / v1.0, tested on v1.3)
  - Staged on mounted SD card at `<SD_MOUNT>/firmware/`:
    - `Tab5FileServer-0.1.0.bin`
    - `Tab5FileServer-0.1.0.json`
    - `Tab5FileServer-0.1.0-LICENSE.txt`

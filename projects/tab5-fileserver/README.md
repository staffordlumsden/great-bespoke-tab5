# Tab5 File Server (`Tab5FileServer` 0.1.0)

SD-installable HTTP File Explorer and Wi-Fi Hotspot / LAN server for the M5Stack Tab5.

## Features

1. **Dual Network Modes**:
   - **LAN Mode (Wi-Fi Station)**: Connects to saved Wi-Fi credentials from Console NVS (or scans and connects to any 2.4 GHz network on-device) and advertises `http://tab5.local/` (and `tab5-files.local`) via mDNS alongside its LAN IPv4 address.
   - **Hotspot Mode (Wi-Fi SoftAP)**: Creates a WPA2-PSK Wi-Fi Hotspot (`Tab5-FileServer`) at `http://192.168.4.1/` (`http://tab5.local/`).
2. **Unified 8-Digit Security Passcode**:
   - Displayed prominently in 4x PixelFonts on the Tab5 screen.
   - Serves as the WPA2 password for joining the `Tab5-FileServer` Hotspot AND as the unlock passcode required to enter the Browser File Explorer.
   - One-tap `[GENERATE NEW 8-DIGIT PASSCODE]` rotates the code, updates the Hotspot WPA2 passphrase, and resets active browser sessions.
3. **Console-Styled Browser File Explorer**:
   - Served at `/` with embedded TrueType PixelFonts (`Tab5Pixel13`, `Tab5Pixel11`, `Tab5Pixel7`) generated directly from `pixel_fonts.h` so typography works offline in Hotspot mode.
   - Supports Dark Mode (`#101215` / `#1d2228`) and Light Mode (`#eef1f4` / `#ffffff`) matching Tab5 Console.
   - Provides full access to the SD card starting at the Root directory (`/` -> `/sdcard`).
4. **Uploads, Downloads & HTML Streaming**:
   - **Uploads**: Upload single or multiple files (via button or drag-and-drop) with live progress bar directly to the current SD card folder.
   - **Downloads**: Accessing any non-HTML file sends `Content-Disposition: attachment` to download it to the client device.
   - **HTML Streaming**: Accessing `.html` or `.htm` files streams them inline (`Content-Type: text/html; charset=utf-8`, `Content-Disposition: inline`) directly to the client browser.

## Build & Test

```sh
bash tests/run_tests.sh
cd firmware && ../../../idf.py build
python3 ../tools/package_sd_app.py \
  --image build/Tab5FileServer.bin \
  --output ../artifacts/sd-library \
  --license ../Tab5FileServer-LICENSE.txt
```

# Tab5 ThinClient

Console-skinned fork of [airpocket-soundman/Tab5_thinclient](https://github.com/airpocket-soundman/Tab5_thinclient) packaged as an SD-card installable OTA application (`Tab5ThinClient`) for **M5Stack Tab5** running **Great Bespoke Tab5 Console**.

## Overview

- **UI Skin & Fonts**
  - **Chrome (status bar, mode tabs, telemetry pill, action buttons, borders):** Great Bespoke Console `PixelFonts` (`pixelfont_7x5_thin`, `pixelfont_11x5_thin`), with Console Dark default theme (`theme classic` switches back to the classic dark/cyan palette).
  - **Main area (terminal grid, settings lists, Python REPL, help/manual views):** Upstream `AsciiFont8x16` + LovyanGFX `lgfxJapanGothic` (`16`/`20`/`28`/`36` px via U8g2-compressed glyph tables), plus `TerminusBitmap` and block/box-drawing/braille/powerline/symbol fallbacks.
  - **Display & Touch Rotation:** 4-way orientation toggle (`ROT` button or `rot 0|90|180|270`), reflowing both landscape (`1280x720`) and portrait (`720x1280`) layouts through `tab5_hal` using dirty-region updates.
- **v0.1.0 Scope**
  - SSH2 / SCP client (`libssh2_esp`) with SHA-2 KEX/hostkey preference, TOFU host-key verification, password, keyboard-interactive, and SD public-key authentication.
  - Local interactive shell (`ls`, `cat`, `df`, `cd`, `pwd`, `mkdir`, `rmdir`, `cp`, `mv`, `rm`, `touch`, `stat`, `wifi`, `ssh`, `scp`, `python`, `image`, `rot`, `theme`, `man`, `help`, `status`, `clear`, `connect`, `disconnect`, `sd-sync`, `console`).
  - Embedded MicroPython (`micropython_embed`) with `gfx` drawing primitives (`gfx.size`, `clear`, `present`, `pixel`, `line`, `rect`, `fill_rect`, `circle`, `fill_circle`, `text`, `mono`) and pure ESP-IDF `Pin`/`ADC`/`PWM`/`I2C`/`SPI`/`UART` hardware module (`demos/*.py`).
  - Image viewer (`JPEG`/`PNG`/`BMP` via `image_framework`) and inline terminal image preview via `OSC 777;tab5-image;...`.
  - Reverse FUSE storage bridge (`sd-sync` / `tab5-fuse-server.py`, embedded directly in firmware) exposing `/sdcard` to the remote SSH host at `~/sd`.
  - Input via M5Stack Tab5 I2C Keyboard (`0x6D`) and USB-A HID keyboard (`US`/`JP`/`Caps=Ctrl` remapping).
  - **Deferred in v0.1.0:** BLE HID and USB-A Mass Storage (`usb:/`) are omitted while BLE/factory-C6 and MSC coexistence remain out of scope for this release.

## Logging In to Machines on Your LAN

### 1. Automatic Wi-Fi Reuse

On startup, `Tab5ThinClient` calls `tab5_radio_power_set(true)`, initializes `esp_hosted` against the factory ESP32-C6 coprocessor (`v1.4.x`), and reads the saved Wi-Fi SSID/password from Console's NVS (`tab5_weather_wifi`). You can also override or add a network in the **WIFI** tab (`wifi scan`, `wifi join <ssid> [password]`).

### 2. Reusing Existing Tab5 SD SSH Profiles & Keys

If you already configured an SSH profile or key on your Tab5 SD card for `tab5-terminal` or Console tools, `Tab5ThinClient` checks and imports it automatically:

1. **Existing `/sdcard/private/tab5-terminal.json`:** Imported automatically into the **SSH** tab when matching host/user profiles are missing (or explicitly via **Import SD**).
2. **Existing `/sdcard/private/tab5-shell-profiles.txt`:** Space-delimited fallback profiles (`<name> <user>@<host> [-p <port>] [-i <key_path>]`) are imported on boot and via **Import SD**.
3. **Private key lookup (`/sdcard/private/...`):**
   - Keys stored under `/sdcard/private/` (for example `/sdcard/private/<key_name>`) or referenced as `/private/<key_name>` or `<key_name>` are resolved automatically against `/sdcard/private/`.
   - Authentication order: if a valid private key file exists on SD (without passphrase, or with `passphrase`), public-key auth is attempted first (`libssh2_userauth_publickey_frommemory`), falling back to configured password or interactive prompt (`keyboard-interactive` / password) if needed.
4. **Host-key trust (TOFU):** On first connection to a LAN host, its SHA-256 host key fingerprint is cached in NVS via `ssh_tofu`; subsequent connections verify the fingerprint automatically.

### 3. Connecting from the Local CLI or UI

- **UI tabs (`Esc` or touch top bar):** Switch between `TERM`, `WIFI`, `SSH`, `FONT`, `CONF`, `ROT`, and `CONSOLE` (clean return to Tab5 Console).
- **Local CLI commands:**
  - `ssh <user>@<lan-host> [-p <port>] [-i <key>]` (connect directly and save/update profile)
  - `ssh sync [--replace]` / `sd-sync [--replace]` (refresh SD profiles; `--replace` clears stale NVS entries)
  - `ssh rm <index|name|host>` / `ssh clear` (remove stale SSH profiles from NVS)
  - `ssh tofu [clear [host]]` (inspect or clear cached host keys when a host is rebuilt)
  - `scp <user>@<lan-host>:<remote_path> /sdcard/<local_path>`
  - `scp /sdcard/<local_path> <user>@<lan-host>:<remote_path>`
  - `connect` (connect using active saved/imported SSH profile)
  - `sd-sync` (start/refresh SD sync)
  - `console` (return to Tab5 Console)

## Controls

| Input / Chrome | Action |
|---|---|
| `Esc` (outside active SSH session) / Top bar tap | Cycle or select chrome tab (`TERM`, `WIFI`, `SSH`, `FONT`, `CONF`) |
| `ROT` pill button / `rot` CLI command | Cycle display & touch orientation (`0`, `90`, `180`, `270`) |
| `CONSOLE` button / `console` CLI command | Unmount SD / tear down active SSH cleanly and reboot back into Tab5 Console |
| Touch scroll / hardware scroll keys | Scroll back through terminal history |

## Building & Packaging for SD Installation

Configure host ESP-IDF v5.5.5 paths in the repository's `.tools/export.local.sh`, then run:

```sh
# 1. Native host tests & firmware contract check
cd projects/tab5-thinclient
c++ -std=c++17 -O2 -Wall -Wextra -Ifirmware/main \
  tests/host_test.cpp \
  firmware/main/terminal_buffer.cpp \
  firmware/main/terminal_emulator.cpp \
  firmware/main/keyboard_mapper.cpp \
  firmware/main/settings_store.cpp \
  firmware/main/ssh_key_utils.cpp \
  -o /tmp/tab5_thinclient_host_test && /tmp/tab5_thinclient_host_test
python3 tests/check_thinclient_contract.py

# 2. Build firmware (ESP32-P4 pre-v3, 16 MiB flash, 32 MiB PSRAM)
cd firmware
../../../idf.py build

# 3. Package SD card bundle (.bin + .json + -LICENSE.txt)
cd ..
python3 tools/package_sd_app.py \
  --image firmware/build/Tab5ThinClient.bin \
  --output artifacts/sd-library \
  --license Tab5ThinClient-LICENSE.txt
```

Copy the generated `Tab5ThinClient-0.1.3-dev.bin`, `Tab5ThinClient-0.1.3-dev.json`, and `Tab5ThinClient-0.1.3-dev-LICENSE.txt` files to your Tab5 microSD card's Console SD library directory and install from **Tab5 Console**.

## Attribution & License

Forked from [airpocket-soundman/Tab5_thinclient](https://github.com/airpocket-soundman/Tab5_thinclient) (MIT). See [`LICENSE`](LICENSE), [`PROVENANCE.md`](PROVENANCE.md), [`THIRD_PARTY_NOTICES.txt`](THIRD_PARTY_NOTICES.txt), and [`Tab5ThinClient-LICENSE.txt`](Tab5ThinClient-LICENSE.txt).

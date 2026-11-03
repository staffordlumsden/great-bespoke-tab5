# TAB5 Console

Console 0.3.7 is the current resident release. It replaces the disabled
**Network** tab with a purple **Camera** one-tap auto-launcher for `Tab5Camera`,
increases the SD App Library capacity from 8 to 32 packages, sorts packages by
project name and version, and skips macOS `._*` metadata files. See the
[0.3.7 checkpoint](../../docs/TAB5_CONSOLE_0.3.7.md).

Console 0.3.6 added the red **Shell** launcher and retained green **Files** and
blue **HTML View** tabs. Earlier releases added the orange **Ollama** launcher,
the Matrix lockscreen with PIN and sleep, persistent four-way display/touch
rotation, power telemetry, RTC/NTP clock settings, Wi-Fi credential persistence,
Bluetooth scanning and the shared SD installer.

## Screen and controls

- Tabs: Console is active. Red **Shell**, orange **Ollama**, green **Files**,
  blue **HTML View** and purple **Camera** tabs find their matching SD packages
  and start the hash-checked inactive-slot installer. The general **SD Launch**
  button opens the complete App Library.
- Header: TAB5 Console, semantic version, two-word update description and the
  RX8130-backed date/time. Tapping the clock opens the Clock & Date Settings overlay.
- Appearance: toggles persistent dark/light palettes. Dark mode shows the sun
  action icon; light mode shows the crescent action icon. Light mode uses only
  white and neutral light greys, not the earlier CYD mint palette.
- SD Launch: a full-width `Launch from SD Card` button uses the original CYD
  SD-card silhouette and opens the SD App Library independently of Weather. It
  chooses and verifies a firmware package from the card, writes the inactive OTA
  slot if present, then reboots into it.
- Weather: opens the SD App Library. Select an ESP32-P4 package from
  `/sdcard/firmware` and tap **Install + run**.
  Validation and inactive-slot installation run automatically in a worker, with
  a steady stage/progress display, verified readback and a dimmed reboot. The factory image,
  partition table, NVS and C6 firmware are not erased.
  The button reads a saved snapshot from Weather 0.1.2 once at Console boot;
  it makes no weather request and displays no time. Until Weather 0.1.2 has
  fetched and saved data once, it shows `NO WEATHER SAVED`.
- Wi-Fi: tap the card to enable the service and open the SSID picker. Secure
  networks open a touch/USB-keyboard password dialog. Successful connections
  show the live IP address in the footer. The C6 supports 2.4 GHz Wi-Fi 6 only;
  it does not scan or connect to 5 GHz networks. The touch keyboard provides
  letters, numbers, Shift and a `SYM` layer for common ASCII punctuation.
  ESP-IDF persists the last station configuration; when Wi-Fi remains enabled
  across a reboot, 0.3.0 automatically reconnects that saved network.
- Bluetooth: tap the card to scan BLE advertisements, select a connectable
  keyboard, pair and persist the resulting bond. Winmaxle B1 uses BLE report
  protocol; no passkey prompt occurred in its accepted CYD pairing. The Tab5
  shows KEYBOARD READY only after encryption, bonding and descriptor validation.
  A local typing/arrow test appears in the Bluetooth panel; Enter clears its
  sample text. BLE input also works in Wi-Fi password fields. Numeric confirmation,
  passkey input and passkey display are supported when requested by a peer.
  Classic Bluetooth and mouse/media input are not supported.
- Result lists show eight entries at a time. `UP` and `DOWN` page through the
  remaining results without changing the selected-result indexing.
- Brightness: `+` and `-` drive the real GPIO22 LEDC backlight in 10% steps.
- Volume: `+` and `-` persist the future audio-engine volume in 10% steps. The resident Console does not initialize
  audio output.
- Storage: live 64 GB SD usage plus allocated/physical onboard flash, now using
  larger values and full-width meters.
- RAM: live internal RAM and 32 MB PSRAM usage with larger text; tap the panel
  to refresh.
- Battery: INA226 voltage and an estimated percentage using the documented
  6.0 V shutdown and 8.23 V full points, with the requested five segments.
- Sleep: turns the screen off and waits for a touch to restore brightness.
- Lock: a dedicated padlock button with static green Matrix character trails.
  It opens the Matrix rain clock/date screen. Double-tap opens the keypad;
  correct PIN plus OK returns to Console. Back deletes; Cancel clears entry
  and returns to rain; a wrong PIN is rejected with brief feedback.
  The four power controls share one row in landscape and a two-by-two grid in
  portrait. The button art is deterministic and does not animate.
- Power Off: requires a second tap within five seconds, then emits the Tab5
  PI4IO power-off pulse.
- Rotate: advances clockwise through landscape, portrait, inverted landscape
  and inverted portrait. Display and touch use the same transform, orientation
  is saved in NVS, and portrait reflows the dashboard and modal controls to
  720x1280 rather than rotating a fixed landscape composition.
- Footer: live IP state, a prominent 2x-scale Bluetooth state indicator,
  enlarged USB keyboard status and copyright text.

The RTC clock alone is repainted once per second. Battery, controls, storage and
footer regions redraw only when their values or states change. A complete
1280×720 repaint is reserved for startup and whole-theme changes.

While a picker is open it exclusively owns the region from y=120 to y=640;
periodic battery and telemetry refreshes defer base-panel painting until it
closes. High-frequency BLE advertisement/RSSI updates remain in memory and the
picker repaints once when scanning completes.

During app loading, the loading page fills all space between the persistent top
tabs and bottom information bar and displays the CYD SD-card icon. Only its
progress region is repainted, at most five times per second. Two framebuffers keep
incomplete drawing off screen; cache-safe DSI/DW-GDMA interrupts maintain
scanout during flash operations. See the
[0.2.2 checkpoint](../../docs/TAB5_SMOOTH_LAUNCH_0.2.2.md).

## Matrix lockscreen

The checked-in PixelFonts atlas also drives the lock screen. Tapping the
Lock button fills the display with
falling green glyphs at the original CYD 7×5 thin scale, renders a dim
date/time panel from the RX8130 RTC. Each column has an independent head,
speed and fading multi-character tail, adapted from the CYD Matrix Rain app.
Double-tap opens the PIN keypad. Only a correct PIN followed by OK unlocks.
See [0.3.3 verification](../../docs/TAB5_CONSOLE_0.3.3.md). The
animation is independent of the normal dirty-region renderer and only runs while
the Matrix screen is open, so it does not affect the one-second clock redraw
contract or background telemetry updates.

## Clock persistence and sync

The clock does **not** need Wi-Fi to keep running. Tab5's RX8130 RTC is backed
by the board's supercapacitor, so valid time survives MCU resets and normal
power-off. If the RTC reports voltage-loss/invalid state, Console seeds it once
from firmware build time.

Console 0.3.4 introduces full RTC write capability via `tab5_rtc_set()` and a
touch settings overlay:
- Tapping the clock on the home screen opens the **Clock & Date Settings** overlay.
- Users can select their timezone rule (Sydney AEDT, Melbourne AEDT, Brisbane AEST,
  Adelaide ACDT, Perth AWST, or UTC), persisted in NVS.
- **WI-FI TIME SYNC** queries NTP (`pool.ntp.org`), converts the UTC timestamp
  to local civil time, and programs the RX8130 RTC registers immediately.
- **SET MANUALLY** provides an interactive 6-field editor with increment/decrement
  controls and calendar leap-year bounds, writing directly to the RTC upon save.

## Fonts

The checked-in atlas is copied from Great Bespoke CYD Console and uses its
exact 7×5 Thin, 11×5 Thin and 13×7 Semibold PixelFonts faces. Upstream commit:
`344a6cfcd660b8546258485befb7576a47790d7d`. The MIT license is retained at
`third_party/PixelFonts/LICENSE`.

## Build, check and flash

This project retains the accepted ESP32-P4 revision-v1.3 configuration and
`espressif/m5stack_tab5` 1.3.2 dependency. ESP-Hosted is pinned to 1.4.0 and
ESP Wi-Fi Remote to 0.8.5 to match the installed M5Stack Wi-Fi-SDIO C6 image.
From this directory:

```sh
../../idf.py build
python3 tests/check_console_contract.py
python3 tests/check_b1_decoder.py
python3 tests/check_keyboard_decoder.py
../../idf.py -p /dev/cu.usbmodemNNNNN app-flash
../../idf.py -p /dev/cu.usbmodemNNNNN monitor
```

This branch retains 24 KiB NVS, 4 KiB PHY data and the 4 MiB factory Console at
their original offsets, and adds 8 KiB OTA data plus two 5 MiB app slots. Both
the bootloader and Console enable first-boot rollback. The SD installer checks
the layout and Console identity, alternates app slots, and validates the written
image before selecting it. App images cannot overwrite the resident Console
through this installer.

The one-time migration in
[`TAB5_SD_LAUNCHER.md`](../../docs/TAB5_SD_LAUNCHER.md) is installed on the tested
unit, which boots with `SD launcher layout=READY`. Its tool creates recovery
payloads from a complete backup and verifies all flash after replacement. Routine
`app-flash` does not perform this migration. Do not use `erase_flash`; factory
C6 firmware and SD contents are outside the migration.

Weather uses the shared `tab5_app_boot` component to confirm its first rendered
frame and arm Console for the next reset. Its Console action returns immediately.
An unconfirmed app reboot falls back to factory through bootloader rollback.

## Radio architecture

The P4 drives M5Stack's factory C6 firmware over 4-bit SDIO at 40 MHz. The C6
reports WLAN plus BLE-only HCI; ESP-Hosted exposes Wi-Fi through
`esp_wifi_remote` and Bluetooth through hosted VHCI. Bluedroid runs host-only on
the P4 with NVS-backed bonding. BLE 5 extended host APIs are disabled because
the factory C6 rejected Extended Create Connection (0x2043); the host uses the
legacy BLE scan/connection APIs used by the CYD B1 firmware.

Wi-Fi credentials are stored through ESP-IDF's standard Wi-Fi NVS storage.
NVS encryption is not enabled in this version, so treat the Tab5 flash as
sensitive if it has been connected to private networks. Toggling Wi-Fi off
stops the station; toggling Bluetooth off cancels scan/connection activity. The
C6 power rail remains enabled after hosted transport startup so either service
can be re-enabled without renegotiating the coprocessor.

## Physical acceptance

The 0.3.7 package was installed through `app-flash`, read back byte-identically
and physically accepted. The SD App Library displayed all test packages in
alphabetical order and the purple Camera tab launched `Tab5Camera` from SD.
Earlier physical runs cover all four rotations, Ollama installation and live
data, Shell/Files/HTML View launch, display/touch initialization and Console
return. See the linked checkpoint documents for exact evidence.

Build and serial evidence still cannot inspect individual rendered pixels.
Future visual review should cover exact loading-page spacing, icon appearance,
dark/light, brightness, volume, RAM refresh, sleep/wake and the two-tap power
confirmation. Do not complete the second power-off tap until other observations
are recorded. Winmaxle B1 BLE connection also remains unresolved.

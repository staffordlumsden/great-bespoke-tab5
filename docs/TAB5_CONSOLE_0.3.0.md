# Console 0.3.0: Ollama Quick Launch and Four-Way Rotation

27 October 2026. This release changes the former **AI Usage** tab to
**Ollama**, makes that tab install and launch only the
`Tab5OllamaAnalytics` SD package, enlarges the loading surface, restores the
original CYD SD-card icon, and adds persistent four-way display/touch
rotation.

## Console changes

- The six top tabs are `Console`, `Terminal`, `Ollama`, `Files`, `Web Server`
  and `Network`. The orange Ollama tab is the only non-Console action enabled
  in this release.
- One Ollama tap scans `/sdcard/firmware`, accepts only a manifest whose exact
  project identity is `Tab5OllamaAnalytics`, and immediately starts the
  existing hash-checked inactive-slot installer. A different package can
  never be selected by this shortcut.
- The loading page owns all pixels between the persistent top tabs and bottom
  IP/device-information bar. It shows the target project, progress and the
  original Great Bespoke CYD SD-card silhouette. Only the progress region is
  repainted during installation.
- The normal **Launch from SD Card** control uses the same CYD SD-card icon.
- Battery, Sleep and Power Off were made narrower. The new Rotate control uses
  two circular arrows and advances through 0, 90, 180 and 270 degrees.
- Display and touch share one transform. Landscape orientations are
  `1280x720`; portrait orientations are `720x1280`. Portrait mode reflows the
  dashboard, controls, pickers and touch targets rather than rotating a fixed
  landscape composition.
- Rotation is stored in the existing `tab5console` NVS namespace and restored
  before the first application frame. Four presses return to the original
  landscape orientation.

The one-second clock remains a dirty-region update. Rotation performs a full
redraw only when requested; installer progress does not repaint the tabs or
footer.

## Host verification

The release was built with ESP-IDF v5.5.5 for ESP32-P4. The complete Console
test suite passed:

- Console UI/radio/rotation contract;
- all 65,536 USB keyboard modifier/key mappings;
- Winmaxle B1 descriptor and key decoder;
- boot-lifecycle fault tests under ASan/UBSan;
- Weather snapshot integration;
- production installer runtime under ASan/UBSan;
- installer safety and UI contracts.

The factory application is 1,366,096 bytes. Its SHA-256 is:

```text
1cd85c14e23c5424b81217616de57d94d2f851d240ca14fee7fb511fe8601a1a
```

`idf.py app-flash` wrote only the resident factory application at `0x10000`.
An independent read of the same 1,366,096-byte flash range matched the build
byte for byte and produced the same SHA-256. The partition table, NVS/PHY,
factory C6 firmware and SD filesystem were not rewritten.

## Device verification

The attached ESP32-P4 revision v1.3 booted Console 0.3.0 from the factory
partition. Serial evidence confirmed:

- resident launcher geometry ready at factory `0x10000`, `ota_0` `0x420000`
  and `ota_1` `0x920000`;
- physical controller firmware 1 at I2C `0x55`, positively identified as
  ST7121;
- 32 MB PSRAM, the 64 GB exFAT SD card and USB HID host initialized;
- two-buffer first-frame presentation before backlight reveal;
- factory ESP32-C6 hosted Wi-Fi/BLE initialization and saved-Wi-Fi reconnect;
- rotation changes `1 -> 2 -> 3 -> 0`, with logical dimensions alternating
  `720x1280` and `1280x720` and the fourth press restoring the original layout.

One press of the Ollama tab then installed the exact SD package into inactive
`ota_1`, rebooted from `0x920000`, and reported:

```text
Project name: Tab5OllamaAnalytics
App version:  0.1.1
TAB5_OLLAMA_BOOT healthy=1 first_frame=1 confirmed=1 credentials=WITHHELD
TAB5_OLLAMA_FETCH balance_ok=1 models_ok=1 account_values=WITHHELD
```

That proves automatic package selection, verified installation, first-frame
confirmation, saved-network use and live Ollama fetch without exposing account
values. A subsequent reset returned to resident Console 0.3.0 as designed.

The host cannot inspect individual display pixels. The user confirmed the
post-flash boot and operated the on-screen Rotate and Ollama controls; a future
visual review can still refine spacing or icon appearance without changing the
verified installer and rotation mechanics.

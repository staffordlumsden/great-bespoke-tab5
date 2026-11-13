# Tab5 Weather

An ESP32-P4 weather firmware candidate for Great Bespoke Tab5, derived from the
validated parser, cache and HTTPS design of Great Bespoke CYD Weather Micro. The
target is an SD-library application installed by a resident Tab5 Console into an
OTA slot; ESP32 firmware is not executed directly from the SD filesystem.

## Current milestone

Version **0.1.2** provides:

- strict, bounded Open-Meteo forecast and geocoding clients with the ESP-IDF CA
  bundle, hostname verification, no redirects and no API key;
- current conditions, five-hour forecast, rain, wind, sun and moon views;
- touch location search, six saved locations, C/F and theme persistence in the
  app-owned `wx_ui` NVS namespace;
- on-device Wi-Fi scanning, network selection, masked password entry, joining,
  disconnect and retry, with the selected station shared with Tab5 Console;
- QWERTY city/password keyboards, visible search/wait/error states, a retained
  query when going Back, and explicit tap-to-save city results;
- a bounded last-successful-weather snapshot for Console 0.2.3's Weather button;
  Console displays saved city, temperature and conditions without fetching or
  including a time. Failed/offline launches retain the previous snapshot;
- explicit return to the verified factory `tab5_console` partition;
- first-frame OTA confirmation, but only when display, touch, SD, NVS, worker and
  Console-return checks have passed; the next reset/power cycle is then armed
  to return to Console;
- an ESP32-P4 package validator producing matched `.bin`, `.json` and
  `-LICENSE.txt` files for `/firmware` on the SD card.

The Weather settings screen can list package names and versions from the SD
library. The resident Console performs image validation and OTA installation;
Weather returns to Console for that operation.

No SSID or password is compiled into Weather. Join or change networks from its
Network screen; the last selected station remains available for automatic
reconnection and remains shared with Console.

The logical Weather Micro scene currently renders at a uniform 3x scale in the
centre of the 1280x720 Tab5 panel. This deliberately establishes the data,
network, touch, persistence and SD-app contracts before a native 1280x720 layout
rewrite. The user accepted the 0.1.1 launch, search/persistence and Console-return
behaviour; 0.1.2 adds the last-weather writer and remains a development build.

## SD launch dependency

The installed Tab5 Console has one 4 MiB factory partition. Both source projects
now use the same 16 MiB layout:

| Partition | Offset | Size |
|---|---:|---:|
| Factory Console | `0x10000` | 4 MiB |
| OTA data | `0x410000` | 8 KiB |
| OTA 0 | `0x420000` | 5 MiB |
| OTA 1 | `0x920000` | 5 MiB |

Both firmware builds and native checks pass. Installing the layout requires
the one-time [bootloader/partition/Console migration](../../docs/TAB5_SD_LAUNCHER.md).
Its tooling prepares exact recovery regions and requires the connected flash
to match the complete backup before applying changes. Routine `app-flash` does
not install this layout. Weather's direct flash targets are blocked so that it
can only be deployed through the SD library workflow. The migration is now
installed, and Console boot confirms both OTA slots and launcher readiness.
The user observed SD launch and Console return with 0.1.0-dev and subsequently
accepted the repaired 0.1.1 behaviour. The 0.1.2 snapshot populates the Console
card after a successful fetch and return. First-boot rollback fault testing is a
separate scenario. On 13 November 2026, the user confirmed the Tab5 firmwares
work as intended. See the [0.1.1 checkpoint](../../docs/TAB5_SMOOTH_LAUNCH_0.2.2.md)
and [the launcher contract](../../docs/TAB5_SD_LAUNCHER.md).

## Check and build

From this project directory:

```sh
bash tests/run-native.sh
bash firmware/tools/build.sh build
python3 tools/package_sd_app.py \
  --image firmware/build/Tab5Weather.bin \
  --output artifacts/sd-library \
  --license THIRD_PARTY_NOTICES.md
```

The native suite uses ASan/UBSan and checks the real weather parser, model, all
Wi-Fi/password UI scenes, HTTPS adapter, ESP32-P4 image gate, Wi-Fi source
contract and non-overlapping 16 MiB partition layout. An ESP-IDF build
additionally requires the repository's pinned v5.5.5 toolchain configuration.
Neither proves physical display, touch, hosted Wi-Fi, TLS, OTA boot, Console
return or recovery.

## Data and privacy

No location is guessed from GPS, IP address or SSID. A location is sent only
after the user enters and selects it. Forecast caches are in memory; selected
locations and UI preferences stay in `wx_ui`. A Wi-Fi password exists in the
Weather UI buffer only while it is being entered, is always masked, is cleared
after join or cancellation and is never logged. Joining a network calls
`esp_wifi_set_config` with flash-backed station storage, replacing the one saved
station configuration that Weather and Console share. Disconnecting does not
erase that configuration. Logs omit location, SSID, password and payload data.

Open-Meteo and GeoNames attribution and full dependency notices are in
`THIRD_PARTY_NOTICES.md`.

# Tab5 Weather contributor checkpoint

Read `README.md`, `../../docs/TAB5_WEATHER_ARCHITECTURE.md` and the repository
root `AGENTS.md` before changing this candidate.

- Keep the accepted Console and physical Tab5 untouched unless the human
  separately authorises a device migration or flash operation.
- Preserve the ESP32-P4 revision-v1.3 branch, 16 MB flash, factory C6 Hosted
  versions, project-local ST7121/exFAT overrides and full dependency notices.
- Weather owns an in-app station control surface. It may scan, call
  `esp_wifi_set_config`, connect, disconnect and retry. Keep the selected station
  interoperable with Console through standard flash-backed ESP-IDF Wi-Fi storage;
  never erase NVS, log credentials or retain the password in the UI buffer after
  join/cancellation.
- Retain fixed verified Open-Meteo origins, valid time before HTTPS, the 24 KiB
  response limit, strict JSON/units/null handling and owner-bound caches.
- Confirm a pending OTA image only after the first rendered frame and critical
  display/touch/SD/NVS/worker/Console-return checks.
- Separate native/build evidence from physical display, touch, Wi-Fi, TLS, OTA,
  rollback and Console-return acceptance.
- Run `bash tests/run-native.sh`; use `bash firmware/tools/build.sh build` only
  through the pinned ESP-IDF v5.5.5 environment.

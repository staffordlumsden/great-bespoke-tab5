# Tab5 File Server — Agent Instructions

- `Tab5FileServer` is an SD-installable application (`project(Tab5FileServer)`) and must never be flashed directly over the factory Console slot (`0x10000`).
- Always power the ESP32-C6 companion via `tab5_radio_power_set(true)` and wait 250 ms before calling `net_init()`.
- Present the first rendered frame before calling `tab5_app_boot_ready(true)`.
- Run `bash tests/run_tests.sh` before building firmware.

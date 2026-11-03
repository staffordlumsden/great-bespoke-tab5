# Console 0.2.4: Power Telemetry, UI Cleanup & Saved Wi-Fi

27 October 2026. The user requested:
1. Increasing the size of the battery percentage and voltage text.
2. Adding dynamic indicators for charging (⚡️) and discharging (🪫).
3. Removing the Weather tab from the top of the display (as the Weather card/launcher provides access).
4. Persisting Wi-Fi credentials to NVS so passwords do not need to be re-entered each time.
5. Flashing the update to the connected Tab 5.

## Telemetry & UI Updates

### Battery Percentage & Voltage Size
- Upgraded the battery percentage and voltage readout from `pixelfont_7x5_thin` at scale 1 to `pixelfont_11x5_thin` at scale 2 (22 px height).
- Positioned and right-aligned cleanly on the battery panel header row next to the "Battery" section title.

### ⚡️ Charging and 🪫 Discharging Detection
- `tab5_hal.c` exposes `tab5_battery_read_status(float *voltage, uint8_t *percent, bool *charging)` querying INA226 register `0x01` (16-bit signed shunt voltage).
- On the M5Stack Tab5 hardware, negative current/shunt voltage indicates current flowing into the NP-F550 battery pack (charging from external USB power). Positive current indicates current flowing out into the system load (discharging on battery).
- When charging: Renders a solid amber/yellow lightning bolt (`⚡`) preceding the percentage text.
- When discharging: Renders a battery outline with red drain indicator (`🪫`).
- Periodic refresh rate was shortened to 3 seconds, triggering `DIRTY_BATTERY` only when percentage or charge state changes.

### Navigation Bar Cleanup
- Removed `"Weather"` from the top tabs bar, leaving 6 active navigation tabs (`Console`, `Terminal`, `AI Usage`, `Files`, `Web Server`, `Network`) evenly divided across the 1280 px display width (`TAB5_DISPLAY_WIDTH / 6`).
- The home screen Weather card and SD launcher continue to provide access to weather conditions and the Weather app.

### Wi-Fi Credential Persistence
- Wi-Fi credentials (`wifi_ssid` and `wifi_pass`) are now saved in NVS namespace `tab5console` upon connecting.
- `tab5_radio_get_saved_wifi()` provides safe access to existing flash-saved networks once the radio stack is active.
- When tapping a saved secure network in the picker, `wifi_password` is pre-populated automatically so users can connect immediately without typing on the onscreen keyboard.

## Verification & Deployment
- All tests pass: `check_b1_decoder.py`, `check_keyboard_decoder.py`, `check_console_contract.py`, and `run-checks.sh`.
- Weather native tests pass: `projects/tab5-weather/tests/run-native.sh`.
- Firmware built and installed using `app-flash` to the factory application partition (`0x10000`).
- Serial output confirmed boot and a successful Wi-Fi connection.

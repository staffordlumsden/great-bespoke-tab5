# Console 0.2.2 and Weather 0.1.1

27 October 2026. The user reports that the previous SD Weather build launches
and its Console action returns successfully. They also report strong cyan/library
flashing during installation, flicker during validation, and a search that gives
no visible response or saved city. These observations do not establish a stack
overflow; no crash trace accompanied the report.

## Implementation

The reference is CYD launcher's worker, combined **Install + run** action and
stage/percentage display at commit `e0678298b2a694786ecaf83477abdadfd13f76ce`:
[launcher source](https://github.com/staffordlumsden/great-bespoke-cyd/blob/e0678298b2a694786ecaf83477abdadfd13f76ce/projects/launcher/firmware/main/app_main.cpp).

- Console presents one stable loading page before starting an installer task.
  Validation, sequential installation, readback and boot selection run in that
  worker. The UI receives snapshots through a one-element queue; the worker
  never draws. Only the progress region repaints, at most five times per second.
  Clock, battery and radio updates cannot repaint the loading page.
- **Install + run** replaces the separate Validate and Launch controls. The
  installer validates automatically and still refuses incompatible, changed or
  corrupted images. Progress advances through checking (0–20%), writing
  (20–80%), readback (80–98%) and boot selection/completion (99–100%). Terminal
  status is posted only after the worker operation returns, allowing safe retry.
- The installer uses ESP-IDF sequential OTA writes instead of erasing an entire
  5 MiB slot at once. Small SD reads/writes yield between chunks. Speed is
  secondary to maintaining a steady screen and responsive UI.
- Both firmwares enable `CONFIG_LCD_DSI_ISR_CACHE_SAFE`, which also selects the
  cache-safe DW-GDMA ISR. In the pinned SDK, the DMA completion ISR restarts
  continuous scanout, and its underrun handler explicitly describes a blue
  display when external-memory data cannot arrive in time. The old build had
  the cache-safe option disabled. This is a concrete mechanism matching the
  report; the new transition still requires visual acceptance on the device.
- The HAL draws into a second framebuffer, queues completed frames and waits
  for frame completion before reusing the former scanout buffer. Dirty-region
  updates remain supported. The backlight stays off until the first complete
  application frame and is dimmed before an app-to-Console or Console-to-app
  reboot. No high-contrast transition animation is introduced.

## Search and city persistence

- City search and Wi-Fi passwords share QWERTY rows, Shift, Space, symbols and
  Delete. Four layers retain every printable ASCII character. Keys are at least
  78 pixels high and 84 pixels wide at the actual Tab5 scale.
- Search previously disabled/ignored the action until networking, time and the
  fetch worker were ready, while also hiding status/error text on the search
  screen. It now accepts a valid query, displays the reason for waiting and
  waits up to 30 seconds for Wi-Fi, secure time or a current request. It never
  skips TLS verification or sends HTTPS before the time gate is satisfied.
- Network/provider/parse errors appear on the search page. Successful queries
  show selectable cities, with an explicit instruction to tap a city to save it.
  A zero-result response has its own visible state.
- A selected result is committed to `wx_ui` before becoming the saved/current
  city. Failed persistence leaves the previous saved settings intact. Back
  cancels pending work while preserving the typed query; it does not guess a
  location from an unselected city name.
- Weather's read-only SD manifest buffer now accommodates the complete package
  manifest, including its licence field.

## Verification and delivery

Both firmwares build with ESP-IDF v5.5.5. The production installer passes
ASan/UBSan tests using real SHA-256 and fault-injecting SDK fakes: automatic
validation, monotonic progress, successful readback, failed writes/finalisation,
source changes, corrupted readback and retry after terminal status. Existing boot
lifecycle, UI contracts and keyboard tests also pass.

Weather's native parser/UI/HTTPS suite covers all QWERTY layers, search waiting
states, timeout/cancellation, visible search errors and successful/failed city
persistence. Fresh public Open-Meteo geocoding and forecast responses for the
existing Sydney test location also pass the production parser/model/UI checks;
this is host API compatibility evidence, not a Tab5 network/TLS acceptance test.

Weather 0.1.1 has been copied to the mounted 64 GB exFAT card and compared
byte-for-byte with the packaged files. The older 0.1.0-dev package remains
recoverable under SD `/firmware/previous/Tab5Weather-0.1.0-dev/`, outside the
launcher's non-recursive catalogue.

- Weather image: 1,172,832 bytes.
- Weather image SHA-256:
  `f7b43511dedca423c8fb1a57cc16bb404063b9b0f375203004b1c7b8488664be`.
- Console 0.2.2 is installed. Final application: 1,354,656 bytes, SHA-256
  `505aaa9677a363f46bf44395e79e97f80c0ccae0b30ad94ba89eb027ae3d1e9d`.
  The final `app-flash` dependency rebuild regenerated the image after a header
  documentation update; this is the hash of the actual flashed/read-back image.

## Installed Console checkpoint

The reconnected port was verified as ESP32-P4 revision v1.3 with a single serial
owner. Before flashing, the complete flash prefix through `0x410000` was saved,
including the current bootloader, partition table, NVS/PHY and full 4 MiB Console
slot. An exact factory-region recovery image was also retained privately.

The pinned workspace `idf.py app-flash` command wrote only the Console image at
`0x10000`; esptool verified its transfer hash. A fresh readback matched the new
application and expected final-sector padding. Every other byte in the saved
prefix was unchanged, including bootloader, partition table, NVS, PHY and the
remaining factory-slot tail. C6 firmware and OTA slots were not written.

After the supported P4 watchdog reset, the passive serial capture recorded:

```text
boot:0x20f (SPI_FAST_FLASH_BOOT)
TAB5_CONSOLE: SD launcher layout=READY factory=0x10000 slots=0x420000,0x920000
TAB5_CONSOLE: TAB5 Console 0.2.2: display=1 touch=1 sd=1 USB HID=1 ST7121 firmware=1
TAB5_HAL: First application frame presented: buffers=2; revealing backlight
```

No display-underrun, crash, assertion or stack-canary message occurred in the
25-second capture. This establishes boot and completed-frame presentation; it
does not establish visual acceptance of SD installation or Weather search.
Raw logs, snapshots, recovery image and machine-readable receipt remain in the
ignored private backup tree.

The partition map, dependency pins, factory C6 firmware, NVS and unrelated SD
contents are unchanged by the application update. The user subsequently reports
that Console 0.2.2 / Weather 0.1.1 works as intended, accepting the steady launch
transition, city-search/persistence and return-to-Console behaviour that this
checkpoint repaired. That observation does not establish the separate destructive
rollback/recovery paths, which remain intentionally unexercised.

# Console 0.2.3: last-known Weather

27 October 2026. The user reports the Console 0.2.2 / Weather 0.1.1 behaviour
works as intended and requests that Console's Weather button show the most
recently fetched weather, without refreshing it or including a time.

## Data handoff

Weather 0.1.2 publishes the city, current temperature and conditions after an
accepted forecast response. The existing parser, owner and forecast-age checks
run before publication. Celsius is stored in tenths, alongside the user's C/F
preference; the button formats that unit locally. Changing units can re-publish
already fetched data without another request.

The shared `tab5_weather_snapshot` component stores one versioned 96-byte record
under NVS `wx_summary/last_v1`, with bounded printable strings, temperature and
flag validation, canonical encoding and CRC-32. It skips commits for identical
records. Failed requests and launches that obtain no forecast leave the previous
snapshot intact. It never erases NVS or modifies Weather's saved locations.

Console reads this record once after its existing NVS initialisation. Its Weather
button shows the city, temperature and conditions, and retains the existing SD
app-library action. It does not fetch weather, poll the cache, expire the record
or display a time. `INSERT SD LIBRARY` and the old disconnected degree marker
are removed. Before the first saved snapshot it shows `NO WEATHER SAVED`.

Weather 0.1.1 kept forecasts only in RAM. It cannot provide historical data to
this new consumer: Weather 0.1.2 must be installed and successfully fetch once.
No conditions are guessed or fetched by Console to fill that initial gap.

## Verification

- Console 0.2.3 and Weather 0.1.2 build with ESP-IDF v5.5.5 and the existing
  configuration/dependency pins and partition layout.
- ASan/UBSan tests exercise the real snapshot codec/store with NVS fakes:
  missing data, read-only consumption, C/F formatting, bytewise corruption,
  invalid/truncated records, deduplicated writes and storage failures.
- Integration checks enforce one Console load call, no Console store call,
  Weather publication and no network or clock dependency in the snapshot
  component. Existing installer, boot, UI, Weather/HTTPS and keyboard checks pass.
- The actual card renderer is compiled with synthetic readings in dark and
  light themes; all drawing is checked against the card bounds. The preview
  uses `Fixture City`, not device/user readings.

## Delivery checkpoint

- Console application: 1,356,288 bytes. SHA-256:
  `2866437d7abf484a0e744182aea232557f964abd7fed57cae4daa16c3e561c36`.
- Weather 0.1.2 package: 1,173,744-byte image. SHA-256:
  `a944669c89a1c893008a8f259b3276e0edfe0cbc21559ff0311b6be88587c41b`.
- Console 0.2.3 is installed. A fresh pre-update flash-prefix/factory recovery
  backup was taken from the verified P4 v1.3. `idf.py app-flash` wrote only the
  factory application, and readback matched the new image and sector padding;
  all other bytes through `0x410000` were unchanged, including bootloader,
  partition table, NVS/PHY and factory tail. C6 firmware and OTA slots were not
  written. Raw snapshots and the receipt are private.
- Boot confirms Console 0.2.3, launcher readiness, display/touch/SD/USB
  initialisation and the first completed two-buffer frame before backlight
  reveal. The 25-second capture contains no underrun/crash message and records
  `Last Weather snapshot=EMPTY (read-only; no weather fetch)`, as expected before
  the new Weather writer has run.
- Weather 0.1.2 is copied to the verified 64 GB exFAT SD card. Firmware,
  manifest and licence were compared byte-for-byte with the packaged files
  after flushing writes. The previous 0.1.1 triplet is preserved under SD
  `/firmware/previous/Tab5Weather-0.1.1/` and was also checked against its original
  package. It is outside the launcher's non-recursive catalogue.

The new populated Weather button still needs observation after a real Weather
0.1.2 fetch and return. The snapshot is local user data; raw NVS/flash captures
remain ignored and private, and logs report availability/errors without values.

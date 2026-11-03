# Tab5 Weather architecture checkpoint

Date: 27 October 2026 (Australia/Sydney).

## Verified starting evidence

- The accepted Tab5 HAL exposes the physically accepted 1280x720 ST7121 display,
  touch, SD, RX8130 RTC and 32 MB PSRAM interfaces.
- Console 0.2.1 uses the factory ESP32-C6 over ESP-Hosted for Wi-Fi. It stores the
  station configuration through standard ESP-IDF Wi-Fi NVS.
- At the start of this work its partition table contained only NVS, PHY data and a 4 MiB factory
  application. There is no OTA data partition or OTA application slot.
- Great Bespoke CYD Weather Micro 0.1.1 has accepted evidence for the basic
  location-search, current and five-hour workflow. Its parser, rate limits,
  cache ownership and HTTPS constraints are portable; its original ESP32 HAL,
  320x240 layout and SD JSON Wi-Fi profiles are not Tab5 interfaces.

## Architecture decision

Tab5 Weather is a separate application image. The resident Console should stay
small and install a validated application from `/firmware` into one of two OTA
slots. "Launch from SD" therefore means validate and copy from SD to flash,
select the OTA partition, then reboot. It does not mean execute instructions
from the exFAT filesystem.

The weather port reuses:

- Open-Meteo models, strict cJSON guards, bounded 24 KiB responses and cache
  ownership from CYD Weather Micro;
- the Tab5 HAL rather than duplicating display, touch or SD drivers;
- a Weather-owned Wi-Fi control component for bounded scan, selection, masked
  password entry, join, disconnect and retry. It uses standard flash-backed
  ESP-IDF station configuration so the last selected network remains
  interoperable with Console;
- the factory partition as the explicit Console-return target.

The earlier saved-network-only/read-only station constraint is deprecated and
superseded by this in-app control surface. Console remains a compatible consumer
of the same last-selected ESP-IDF station configuration, not the sole place from
which a network can be joined.

## Implemented boot flow

1. Factory Console mounts SD and validates the package manifest, image identity,
   ESP32-P4 target, 16 MB flash, revision compatibility, 5 MiB slot limit,
   segment checksum and appended SHA-256.
2. Console checks the shared partition geometry and resident identity, selects
   Console as the recovery target, revalidates the image, hashes the bytes while writing the inactive
   OTA slot, compares flash readback with the SD image and selects the partition
   only when the validated digest still matches.
3. Tab5 Weather initialises display, touch, SD, NVS, Wi-Fi worker and Console
   return. It renders a first frame before confirming a pending OTA image, then
   selects Console for the next reset/power cycle while Weather keeps running.
4. The visible Console action unmounts SD, selects the factory partition and
   restarts without erasing NVS or rewriting the other OTA slot.
5. Bootloader rollback returns to Console if the app resets before first-frame
   confirmation. Failed startup also attempts Console return after a 15-second
   grace period following initialisation. See the
   [launcher/migration contract](TAB5_SD_LAUNCHER.md).

## Evidence completed in this checkpoint

- ASan/UBSan weather/parser/UI and HTTPS checks pass with the SD library changes.
- Host tests cover all four password-keyboard layers (the full printable ASCII
  range), bounded/deduplicated scan-source contracts, station join/disconnect,
  credential-buffer clearing and the prohibition on NVS erase or credential
  logging.
- The actual HTTPS adapter passes fixed-origin, CA-bundle, redirect, size,
  content-type, chunking and failure-path tests against fakes.
- Both projects' partition maps are host-checked for matching addresses, overlap and
  the 16 MiB flash boundary.
- The package validator accepts only ESP32-P4/16 MB/revision-compatible app
  images and rejects a resident Console image.
- Console and Weather build with ESP-IDF v5.5.5 and the shared 16 MiB layout.
- Console's bootloader enables rollback; the shared application boot lifecycle
  passes native fault-injection tests for geometry, factory identity, slot
  rotation, confirmation order and Console return.
- The migration tool passes simulated apply/recover, changed-device rejection,
  interrupted-write and corruption checks. Offline preparation also succeeds
  against the existing local complete backup and the compiled Console images.
- The migration was then installed on the verified P4 v1.3. A fresh backup,
  matching pre-write snapshot and complete post-write comparison verify all
  replacements and unchanged NVS/PHY/other regions. Serial boot confirms both
  app slots, `SD launcher layout=READY` and display/touch/SD/USB initialisation.
  See [the launcher contract](TAB5_SD_LAUNCHER.md).

## Unverified or pending

- Native 1280x720 weather layout: the first port uses a centred 3x compatibility
  renderer to isolate functional porting from the later visual redesign.
- Destructive rollback/failure-path exercises and physical recovery using the
  saved pre-migration regions.
- Populated last-weather card observation after a real 0.1.2 fetch and return.

The partitioning request and continuation were carried through to the P4
migration. The migration did not write SD content or factory C6 firmware.

Later user feedback confirms basic SD launch and Console return with 0.1.0-dev,
along with severe flicker and a search failure. The
[Console 0.2.2 / Weather 0.1.1 checkpoint](TAB5_SMOOTH_LAUNCH_0.2.2.md) records
the repair: queued installer progress, cache-safe double-buffered scanout,
QWERTY entry, visible search gating/errors and commit-before-display city
persistence. Weather 0.1.1 is now copied to SD. The Console 0.2.2 USB update
is installed with readback and first-frame boot
verified. The user subsequently confirmed that the repaired 0.2.2 / 0.1.1
behaviour works as intended. Console 0.2.3 / Weather 0.1.2 later added the
last-weather snapshot; its populated Console card still needs observation after
a successful 0.1.2 fetch and return.

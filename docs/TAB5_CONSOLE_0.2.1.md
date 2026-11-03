# TAB5 Console 0.2.1 - B1 keyboard

Date: 26 October 2026 (Australia/Sydney).

## Source evidence and changes

The original CYD Contextual Micro already physically accepted the user's
Winmaxle Model B1. Its `docs/test-log.md` records BLE, report map 0/report ID 1,
eight-byte report-protocol input, and legacy bonding (auth mode 0x01) without a
passkey prompt. Its later acceptance covers punctuation, arrows, Ctrl keys,
repeat, saved bonds and reconnect. These are CYD results, not Tab5 acceptance.

0.2.1 replaces the Tab5's generic NimBLE connect/pair implementation with the
same Bluedroid ESP HID client used by CYD, attached to ESP-Hosted's existing
VHCI driver. The accepted CYD `keyboard_mapping` and `keyboard_report` files
are copied byte-for-byte, with their sanitizer tests. See
`projects/tab5-console/components/tab5_radio/cyd_keyboard/PROVENANCE.md`.

The keyboard is ready only after successful encryption, bonding and canonical
descriptor validation. Non-keyboard reports from the B1's mouse/media controls
are ignored. Report IDs come from descriptors, not a hard-coded ID-1 assumption.
New scans clear old names, active-scan responses preserve connectability, and
repeated connection taps are rejected while an open is pending. The blocking
HID open runs in a worker; the UI and USB input remain independent.

The Bluetooth panel includes a local typing/arrow test after connection. Its
input updates only that field, not the entire screen or picker. BLE input also
feeds the existing Wi-Fi password dialog. Keyboard payloads are not logged.
The footer distinguishes a ready BLE keyboard from the USB keyboard.

## Hosted compatibility finding

The first 0.2.1 hardware trace initialized Bluedroid, then rejected the automatic
BLE 5 extended connection command: HCI opcode 0x2043, status 0x0c (Command
Disallowed), followed by GATT open status 133. `sdkconfig.defaults` now explicitly
disables `CONFIG_BT_BLE_50_FEATURES_SUPPORTED`, retaining the legacy scan and
connection APIs used on the CYD. Hardware Bluetooth 5.x support does not prove
that a factory hosted-controller build accepts every extended HCI operation.

Both host libraries stay pinned: ESP-Hosted 1.4.0, Wi-Fi Remote 0.8.5. The C6
factory firmware, P4 bootloader/partitions, SD filesystem and shared CYD SDK
are not rewritten by this repair. NimBLE NVS data is preserved; Bluedroid uses
its own persistent bond storage. Legacy bonding is not proof of MITM protection.

## Verification and recovery

- `python3 tests/check_b1_decoder.py`: copied CYD descriptor and mapping suites
  pass under ASan/UBSan, including malformed layouts, punctuation, modifiers,
  ANSI sequences, held-key repeat and disconnect reset.
- `python3 tests/check_keyboard_decoder.py`: existing USB decoder passes all
  65,536 usage/modifier combinations and its edge cases.
- `python3 tests/check_console_contract.py`: UI/rendering release checks pass.
- `../../idf.py build`: ESP32-P4 revision v1.3-compatible application builds
  inside the existing 4 MiB factory app partition.
- `backups/2026-10-26-pre-b1/`: source archive, preserved 0.2.0 build, exact
  installed 4 MiB app slot, and 24 KiB NVS. Installed 0.2.0 prefix was verified
  byte-for-byte against its preserved build before the first write.
- `logs/console-0.2.1-build.log`, `logs/console-0.2.1-flash.log`, and
  `logs/console-0.2.1-b1-live.log`: build, transfer and initial compatibility
  evidence. Raw serial logs and NVS backups are private.

## Installed legacy-API build

- Application size: 1,313,600 bytes (inside the existing 4 MiB app slot).
- SHA-256: `d3476f7fc127a8e321394c15513d6a9bd5407a453c1e2b677d42e7210ea2bb12`.
- Application-only flash on the verified P4 port succeeded; esptool verified
  the transfer hash. Bootloader, partition table and NVS were not flashed.
- Serial boot confirms 0.2.1, ST7121 firmware 1, working HAL initialization for
  display/touch/SD/USB, and successful hosted Bluedroid HID initialization.
- Saved Wi-Fi automatically reconnects and obtains an address after this boot.
- Final flash log: `logs/console-0.2.1-legacy-flash.log`.
- Live trace: `logs/console-0.2.1-b1-legacy-live.log`.

## Paused checkpoint - 26 October 2026

User explicitly requested a pause and continuation tomorrow. Do not start more
device work until the user resumes. The installed image is the legacy-API build
identified above; no C6 firmware, bootloader, partition or NVS erase was performed.

The B1 still does not connect. The final trace shows repeated connection attempts
ending with controller reason 0x3e (connection establishment/synchronization
failure), GATT disconnect reason 62 and GATT open status 133. Disabling extended
BLE host APIs removed the earlier opcode-0x2043 rejection but did not resolve
the link failure. No successful authentication, validated input, or reconnect
has been observed on Tab5. Do not describe this repair as successful.

The user confirmed that previously paired devices are off, retried with FN+BT
pairing mode, and tested again with Tab5 Wi-Fi OFF and the B1 nearby. It still
did not connect. Leave Wi-Fi's current preference as the user set it; ask/check
its state on resumption rather than assuming it remains enabled.

The original CYD and canonical SSD sources were compared: `bluetooth_keyboard.cpp`,
`keyboard_mapping.cpp`, and `keyboard_report.cpp` match. CYD's accepted B1 report
layout and mapping are available locally, but link establishment fails before
the Tab5 can use that decoder. The native descriptor/mapping and USB tests pass.

Resume by inspecting the existing final serial trace and the hosted central
connection path. Useful next evidence is the exact selected B1 advertisement,
address type, controller connection parameters and HCI events. Avoid further
speculative driver changes or weakening pairing security. Preserve factory C6
firmware and the pre-change source/app/NVS backups. Verify any later flash
against the correct P4 device and update this checkpoint with actual results.

## SD launch checkpoint - 27 October 2026

- Added the Console SD App Library surface. Weather now lists and validates
  `/sdcard/firmware/*.json` packages and exposes an explicit `LAUNCH FROM SD`
  action. Validation checks P4 silicon, 16 MB flash, revision range, image
  metadata, segment checksum and appended SHA-256 before any flash write.
- The application-only update was flashed to the verified ESP32-P4 revision
  v1.3 USB-Serial/JTAG port. esptool reported `Chip is ESP32-P4 (revision
  v1.3)`, verified the transferred app hash and performed only a hard reset.
- The flashed application is 1,342,656 bytes with 68% free in the existing
  4 MiB factory partition. Build SHA-256:
  `7d873e12cdf75e4c62d41c4e7d5f271f40a919b5c511114c9a19c42f4560ca2f`.
- Bootloader, partition table, NVS, SD contents and factory C6 firmware were
  not flashed or erased.
- This checkpoint records source/build/flash evidence only. Physical acceptance
  of the launch flow still requires an SD package and hardware-observed install,
  reboot and back-to-Console behavior.

## Launcher button and compact usage - 27 October 2026

- Added a dedicated full-width `Launch from SD Card` button with an SD-card
  icon and a `Choose firmware / app package` hint. It opens the same SD App
  Library as Weather but remains independent of the weather app.
- The SD and RAM usage panels were condensed to 80 pt tall, retaining essential
  usage data while leaving room for the launcher button.
- The revised application is 1,343,152 bytes. Build SHA-256:
  `cde688918b2c28fc67ce6144f969e77738b6c7a188c4d37480ec29e87fba7c35`.
- Application-only flash on the verified ESP32-P4 revision v1.3 succeeded;
  esptool verified the transfer hash. Bootloader, partition table, NVS, SD
  contents and the factory C6 firmware were again not touched.
- The on-screen launcher and shortened panels still need physical acceptance.

## Resident Console partition implementation - 27 October 2026

Source now retains the factory Console and NVS/PHY addresses while adding two
5 MiB application slots and OTA data. Console's bootloader now enables rollback.
The shared boot lifecycle selects Console as the recovery target before staging
an SD image, rotates app slots, and lets Weather arm Console for the next reset
after first-frame confirmation. Weather also has an explicit Console action.

Both firmware builds pass with ESP-IDF v5.5.5. Native boot lifecycle, migration
simulation, weather/parser/UI/HTTPS and existing keyboard checks pass. The
one-time migration was installed against a fresh full-flash backup, with all
flash compared byte-for-byte before reset. Serial boot confirms both OTA slots,
`SD launcher layout=READY` and display/touch/SD/USB initialisation. The installed
Console application is 1,346,016 bytes, SHA-256
`dbbefca18b632b294d29088047699893e8b5d8ce1eeb96c4997e22270277c627`.
At this migration checkpoint, physical Weather SD launch/return acceptance
remained outstanding; the later 0.2.2 section records its subsequent acceptance.
See [partition layout, migration and recovery](TAB5_SD_LAUNCHER.md).

The subsequent [0.2.2 smooth-launch repair](TAB5_SMOOTH_LAUNCH_0.2.2.md)
supersedes the manual Validate/Launch UI and is now installed through an
application-only update. Exact readback confirms the new app and preserved
bootloader, partitions and settings; serial confirms Console 0.2.2 and its
first completed two-buffer frame. Weather 0.1.1 is already verified on SD. The
user observed launch/return with 0.1.0-dev; the new loading progress, smoothness
and search behaviour were subsequently accepted by the user as working as
intended. Console 0.2.3 and Weather 0.1.2 then added the read-only last-weather
handoff described in [the later checkpoint](TAB5_LAST_WEATHER_0.2.3.md).

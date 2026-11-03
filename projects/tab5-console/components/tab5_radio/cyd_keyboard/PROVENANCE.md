# Winmaxle B1 keyboard reuse

These four files are byte-identical copies from Great Bespoke CYD,
`projects/contextual-micro/firmware/main/`, as inspected on 26 October 2026:

- `keyboard_mapping.h`
- `keyboard_mapping.cpp`
- `keyboard_report.h`
- `keyboard_report.cpp`

The corresponding mapping and descriptor tests were copied into this project's
`tests/` directory. Run `python3 tests/check_b1_decoder.py`.

CYD evidence: `docs/test-log.md`, sections "M2 BLE keyboard first pairing" and
"M2 punctuation/release acceptance" (10 October 2026), and the original
`bluetooth_keyboard.cpp`. That firmware physically accepted the Winmaxle
advertised as Model B1 over BLE: report map 0, report ID 1, report protocol,
eight-byte canonical payload. Authentication negotiated mode 0x01 (legacy
bonding, without MITM or Secure Connections); no passkey prompt occurred.

The Tab5 adapter uses the same Bluedroid ESP HID client, report-map validation,
key mapping, repeat limits, release handling and disconnect reset. Controller
initialization is replaced with ESP-Hosted's documented Bluedroid VHCI driver
attachment. The installed C6 controller firmware stays in place. Wi-Fi, display,
touch, SD and USB keyboard services retain their existing implementations.

The first Tab5 attempt with Bluedroid enabled both BLE 4.2 and 5.0 APIs. Its
live C6 trace rejected LE Extended Create Connection, opcode 0x2043, with status
0x0c (Command Disallowed); GATT then reported open status 133. The Tab5 defaults
therefore explicitly disable `CONFIG_BT_BLE_50_FEATURES_SUPPORTED`, retaining
legacy BLE scan/connect commands, as used by the original ESP32 CYD. Do not
remove that setting merely because the C6 hardware supports Bluetooth 5.x.

The adapter accepts only encrypted, bonded, descriptor-validated keyboard input.
Mouse and consumer-control reports are ignored. Other report layouts are not
accepted merely because a device advertises itself as a keyboard. CYD physical
acceptance is prior evidence; Tab5 pairing, input and reconnect require separate
physical checks. Legacy bonding does not establish MITM protection.

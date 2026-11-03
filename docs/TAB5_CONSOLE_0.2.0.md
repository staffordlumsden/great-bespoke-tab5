# TAB5 Console 0.2.0 — hosted Wi-Fi and BLE

Date: 2026-10-26 (Australia/Sydney)

## Outcome

Version 0.2.0 installs on the physical Tab5 and initializes the factory ESP32-C6
firmware over SDIO. The coprocessor reports WLAN plus BLE-only HCI. ESP-Hosted
VHCI starts successfully and NimBLE synchronizes on the ESP32-P4.

The Console now exposes:

- Wi-Fi service enable/disable, asynchronous SSID scanning, open/secure network
  selection, touch and USB-keyboard password entry with a `SYM` punctuation
  layer, connection status, IP, and automatic reconnect to the last network
  saved in NVS when Wi-Fi remains enabled.
- BLE service enable/disable, advertisement scanning, device selection,
  connection, pairing and NVS-backed bonding displayed as `PAIRED + TRUSTED`.
- Eight-result pages with `UP`/`DOWN` navigation for larger Wi-Fi and BLE lists.
- Numeric-comparison confirmation and passkey input/display paths.
- A 2x-scale Bluetooth status in the persistent footer.

## Matching components

- ESP-Hosted host: `1.4.0`
- ESP Wi-Fi Remote: `0.8.5`
- C6 firmware: factory `ESP32C6-WiFi-SDIO-Interface-V1.4.1-96bea3a`
- C6 firmware was not read, erased or rewritten by this update.

## Image identity

- Project version: `0.2.0`
- Application size: `1048992` bytes (`0x1001a0`)
- Application SHA-256:
  `58319288216410efa06202e41015dd75fef2cc5d4cddc0b40857ddc2c224796a`
- Partition table SHA-256:
  `fd8026bff850ca0dee41c41305160317fffe604dda30a9bd5a701ac82d96fa17`
- Both installed regions were read back byte-for-byte identically.
- Factory partition is 4 MiB; 75% remains free.

## Boot evidence

- Bootloader accepted the new layout: NVS at `0x9000`, PHY at `0xf000`, app at
  `0x10000` with size `0x400000`.
- SDIO initialized on CLK 12, CMD 13, D0-D3 11/10/9/8 and reset 15.
- C6 capabilities reported WLAN and BLE-only HCI.
- The C6 radio is 2.4 GHz Wi-Fi 6 only; 5 GHz SSIDs are outside its capability.
- `vhci_drv: Host BT Support: Enabled` and `BT Transport Type: VHCI`.
- NimBLE host synchronized with result 0.
- A UI Wi-Fi scan completed, connected and obtained a LAN address (retained in local evidence).
- A reboot automatically reconnected the saved NVS network and obtained the
  same address without opening the picker.
- BLE active scanning accepts advertisement and scan-response reports, merges
  them by address, and prefers advertised device names over MAC fallbacks.
- The Wi-Fi picker remained intact past the periodic battery refresh; BLE scan
  results repaint only when scanning completes rather than on each RSSI report.
- Eight-result pages, `UP`/`DOWN` navigation and the Wi-Fi `SYM` punctuation
  keyboard are included in the final image.
- ST7121, 64 GB SD, USB HID and the app remained operational.

## NVS continuity

Before flashing, NVS was backed up at
`backups/2026-10-26-pre-radio-flash/nvs.bin`. After the radio boot it still
contained the Console keys:

- `tab5console:bright = 100`
- `tab5console:bt = 0`
- `tab5console:wifi = 0`

The only added namespace entry was `nimble_bond:local_irk_1`, expected when
NimBLE persistence initializes.

## Recovery

Pre-radio state is retained under `backups/2026-10-26-pre-radio-flash/`.
Exact installed 0.2.0 readbacks are under
`backups/2026-10-26-console-0.2.0/`.

## Physical acceptance still required

The transport and host stack are verified. The following interactive behavior
must still be checked on the device:

1. Wi-Fi card opens the picker and lists real SSIDs.
2. A secure SSID accepts password input and obtains an IP address.
3. Bluetooth card lists real BLE devices.
4. Selecting a device pairs it and reports `PAIRED + TRUSTED`.
5. The larger Bluetooth footer indicator is readable and does not overlap.

Classic Bluetooth is unavailable because the factory C6 image advertises BLE
only. It cannot drive a Classic-only keyboard.

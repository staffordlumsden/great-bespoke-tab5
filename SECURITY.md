# Security policy

## Reporting

Do not open a public issue containing credentials, private network details,
device dumps, personal data or an exploitable vulnerability.

Use this repository's GitHub **Security > Report a vulnerability** flow so the
report remains private. Include the affected version, impact, reproduction steps
and the minimum sanitized evidence needed to investigate.

## Device security model

These are development firmwares, not hardened security products.

- Wi-Fi credentials are stored in ESP-IDF NVS without NVS encryption. Treat a
  configured device's flash contents as sensitive.
- Tab5 File Server uses an eight-digit passcode for LAN browser access and
  hotspot authentication. Use it only on trusted networks and rotate the passcode
  if it may have been exposed.
- SSH profiles and keys are stored on the SD card. Protect or remove the card
  before transferring the device.
- Raw serial logs, NVS dumps and full-flash images can contain network names,
  addresses, credentials or Bluetooth bond material. Never attach them to public
  issues or commits.
- The factory C6 image is not updated by these projects. Do not run provenance
  updaters against the tested unit.

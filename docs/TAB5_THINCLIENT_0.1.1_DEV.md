# Tab5 ThinClient 0.1.1-dev: SSH session reliability

14 November 2026. The user reported SSH sessions returning to the local
ThinClient prompt after running commands, including a quota dashboard and
Antigravity CLI. The device did not reboot.

## Changes

- Set the TCP socket non-blocking before enabling libssh2 non-blocking mode.
  The transport drains incoming packets until `EAGAIN`; leaving the socket
  blocking could stall the interactive and secondary bridge channels.
- Give `select` separate read and write descriptor sets, and include the
  libssh2 numeric code in the local SSH error message.
- Replace the fixed-width SSH manager, ping and saved-host boxes with compact
  entries that wrap to the terminal width without truncating profile details.
- Define the disabled optional H.264 version selector required by the current
  dependency manager with the existing pinned video component. The saved
  `sdkconfig` and dependency lock were preserved. The build used a temporary
  copy of `sdkconfig`; its only difference was the disabled selector.

## Verification

- Native C++ host checks and `check_thinclient_contract.py`: passed.
- Host-side integration using the modified SSH client and system libssh2
  against an authenticated LAN host: idle reads returned promptly and shell
  output and a concurrent test bridge channel completed. This is not an ESP32
  or factory-radio test and does not reproduce the reported failure.
- ESP-IDF v5.5.5 build: passed. The package validator checked the ESP32-P4 app,
  16 MB flash declaration, minimum revision 1.0, checksum, digest and 5 MiB slot
  size. App size: 2,635,216 bytes. SHA-256:
  `e083aa9be42fbb705743f1e06e2d66b48085ab3cf07871c8e11122f115902d73`.
- The SD package triplet was copied to the mounted Tab5 card and read back
  byte-for-byte. The previous 0.1.0 triplet was backed up outside the card and
  removed from the SD library, leaving other app packages untouched.

The new build has not been installed or physically tested. Verify repeated
connections, command output, dashboard exit, Antigravity launch/exit and SD
bridge use on the device. The socket mismatch is a concrete defect but the
user's exact disconnect remains unconfirmed; record any local SSH error code
if it recurs. Host checks and successful builds do not prove physical display,
input, Wi-Fi, SSH or rollback behavior.

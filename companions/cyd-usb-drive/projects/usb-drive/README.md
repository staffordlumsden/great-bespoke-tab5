# USB Drive

USB Drive exposes the original ESP32-2432S028R CYD's public SD-card folders to a connected computer as a read/write file-system style volume. On macOS the companion app mounts **CYD SD Card** in Finder. Linux ARM64 and AMD64 bridge binaries expose the same WebDAV volume and open it through `gio` when available.

Current version: `0.1.2`. Live device, serial/WebDAV, Finder and preservation checks pass; final human touch acceptance of the repaired controls is pending.

The original CYD has USB-to-UART bridges rather than native USB device hardware, so this is deliberately a firmware-and-host pair. It does not claim USB Mass Storage. The host bridge translates Finder/WebDAV operations into the bounded `UD1` serial protocol. Only `/Files` and `/Firmware` are exposed. Configuration, credentials, keys, Console state and other private SD paths are unreachable.

## Use on this Mac

1. Install `USBDrive-0.1.2` from Console's SD library, or run it from the verified app slot.
2. Keep the SD card in the CYD and connect either USB data socket.
3. Open `Mount CYD Drive.app`. Finder opens **CYD SD Card** with **Files** and **Firmware**.
4. Eject the mounted volume in Finder before unplugging, or tap **DISCONNECT** on the CYD. **CONSOLE** safely unmounts the SD card in firmware and returns to resident Console.

Finder may attempt hidden metadata files. USB Drive rejects every dot-prefixed path so these cannot leak into the Console firmware library or public Files folder.

## Host artifacts

- `artifacts/host/macos/Mount CYD Drive.app`: universal Apple Silicon and Intel macOS app.
- `artifacts/host/linux-arm64/cyd-usb-drive`: static ARM64 Linux bridge, including Raspberry Pi 5.
- `artifacts/host/linux-amd64/cyd-usb-drive`: static x86-64 Linux bridge.

On Linux, run the binary while the CYD is connected. The bridge serves `dav://127.0.0.1:17688/` and asks `gio` to mount/open it when available. `--serve-only`, `--serial`, and `--listen` are available for manual integration.

## Safety and protocol

Writes stream into a hidden temporary file, are flushed, then replace the destination through a backup-and-rename publish step. Interrupted or malformed transfers are aborted. A single transfer owns the firmware write session. File payload chunks are bounded to 2 KiB and requests are serialized. Files larger than 4 GiB are rejected by protocol version 1.

The display uses the Console visual system and pinned PixelFonts, shows connection and operation state, SD capacity, byte progress, light/dark themes, a release-gated STOP/DISCONNECT control, and Console return. Version 0.1.2 samples touch continuously between state-driven redraws, arms touch only after a clean boot release, filters each gesture across multiple samples and gives the three visible controls the full bottom-row touch area.

## Build and test

```sh
bash projects/usb-drive/tests/run-native.sh
bash projects/usb-drive/firmware/tools/build.sh build
cd projects/usb-drive/host
go test ./...
./build-release.sh
cd ../../..
python3 projects/launcher/tools/package_sd_app.py \
  --image projects/usb-drive/firmware/build/USBDrive.bin \
  --output projects/usb-drive/artifacts/sd-library
```

The firmware build enforces a 2 KiB compiler stack-frame ceiling. Release requires native path/UI tests, Go host tests, ESP-IDF build, image inspection, app-slot-only flash verification, serial read/write/hash testing, Finder mount/read/write testing, Console return and cold-reconnect acceptance.

## Validation evidence

On 25 October 2026 the tested ESP32-D0WD-V3 CYD was backed up and identity-checked before `USBDrive` replaced only the non-helper OTA slot. Version 0.1.2 was installed through the same identity gate after the initial build's periodic full-screen redraws were found to starve touch sampling. Firmware readback matched the image; factory Console and the clock-helper app remained present; the partition table, NVS and PHY region remained byte-identical. The device passed live `UD1` ping, WebDAV create/read/hash/rename/overwrite/delete, Finder mounting with both public roots, the same file-operation sequence through the mounted path, package readback, and eject/helper-exit/serial-release checks. Native tests also cover the enlarged full-row control hit areas.

The firmware is 333,552 bytes with SHA-256 `fe247890e6ad24d4c1be3c3050c411d3ac37fb9b1156cbadc0e415d817754e42`; the build enforces a 2 KiB maximum compiler-reported stack frame. The universal macOS bridge contains arm64 and x86-64 slices. Static Linux ARM64 and AMD64 binaries cross-compile and their hashes are recorded in `artifacts/host/SHA256SUMS.txt`.

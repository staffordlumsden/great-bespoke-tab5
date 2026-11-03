# CYD USB Drive 0.1.2 and Mac bridge

This snapshot preserves the original CYD USB Drive firmware and the Mac
software that mounts **CYD SD Card** in Finder. They communicate over the CYD's
USB-to-UART bridge using the UD1 protocol and a loopback WebDAV server. Only
`/Files` and `/Firmware` are exposed. This targets the original ESP32 CYD;
it is separate from the ESP32-P4 Tab5 firmware.

[Download the firmware and universal Mac app](https://github.com/staffordlumsden/great-bespoke-tab5/releases/tag/cyd-usb-drive-0.1.2)
from this repository's public release.

## Downloads

- **CYD-USBDrive-0.1.2-SD-Package.zip** contains the application `.bin`, its JSON
  manifest, notices and installation instructions. Install it from the existing
  CYD Console SD library; it contains no bootloader, factory Console or NVS dump.
- **CYD-USBDrive-Mac-Bridge-universal.zip** contains the existing **Mount CYD
  Drive.app** from the Desktop, with its valid ad-hoc signature preserved, plus
  dependency licences. Its executable supports both `arm64` and `x86_64`.
  Open it while USB Drive is running on the connected CYD. Eject the Finder
  volume before unplugging.
- **SHA256SUMS.txt** verifies the two downloaded ZIP files. After downloading
  them together, run `shasum -a 256 -c SHA256SUMS.txt`.

The existing Mac bundle's version metadata says **0.1.0**. The paired firmware
is **0.1.2**; the package preserves the app's existing bytes and signature.
Ad-hoc signing does not provide Apple Developer ID notarization.
`RELEASE_MANIFEST.json` records package and executable identities.

## Source and shared dependencies

The original directory relationships are retained under this snapshot:

- `projects/usb-drive/firmware/`: firmware, configuration and build wrapper.
- `projects/usb-drive/host/`: Go bridge, Mac app packaging script, icons and
  dependency locks/licences.
- `projects/usb-drive/tests/`: native path, UI and touch hit-area tests.
- `projects/launcher/firmware/components/`: required CYD HAL and clock modules.
- `projects/launcher/firmware/main/pixel_fonts.h`: pinned font atlas.
- `projects/launcher/tools/package_sd_app.py`: SD package validation/helper.
- `partitions.csv`: the original 4 MB CYD layout.

`SOURCE_MANIFEST.json` lists SHA-256 and size for all 54 source files copied
unchanged from the CYD workspace, plus the supplemental MIT licence for the Go
bridge's `github.com/creack/goselect` dependency. The original project README
and third-party notices are retained under `projects/usb-drive/`.

## Build on another host

For the Mac bridge, install Go 1.24 or newer and Xcode Command Line Tools, then
run from this snapshot's root:

```sh
cd projects/usb-drive/host
go test ./...
bash build-release.sh
```

The original script builds universal macOS and Linux ARM64/AMD64 executables
under `projects/usb-drive/artifacts/host/`. It does not apply the Desktop app's
subsequent bundle signature. To ad-hoc sign a newly built Mac app, run:

```sh
codesign --force --deep --sign - '../artifacts/host/macos/Mount CYD Drive.app'
codesign --verify --deep --strict '../artifacts/host/macos/Mount CYD Drive.app'
```

For native firmware tests, from the snapshot root run:

```sh
bash projects/usb-drive/tests/run-native.sh
```

For firmware, install ESP-IDF **v5.5.5** (commit
`b774170ff46c393eeb5e495ea37936038d3f4f4f`) with ESP32 tools. With that SDK
environment activated, link the local SDK and tools to the paths expected by
the original wrapper, from the snapshot root:

```sh
mkdir -p .tools
ln -s "$IDF_PATH" .tools/esp-idf
ln -s "${IDF_TOOLS_PATH:-$HOME/.espressif}" .tools/espressif
bash projects/usb-drive/firmware/tools/build.sh build
python3 projects/launcher/tools/package_sd_app.py \
  --image projects/usb-drive/firmware/build/USBDrive.bin \
  --output projects/usb-drive/artifacts/sd-library
```

Use a checkout path without spaces on a fresh host; the original host SDK has
a path workaround documented in the main repository's SDK notes. Retain the
included `sdkconfig` and partition layout. A fresh-host firmware build still
needs independent validation; this export preserves the verified existing
0.1.2 binary. Do not use the Tab5 `idf.py` wrapper for this original ESP32 target.

The historical `projects/usb-drive/tools/device_update.py` depends on private
original-host identity evidence, full-flash backups and a factory Console build
that are intentionally absent. Use the Console SD installer instead.

## Verification and limits

Publication checks on 27 October 2026 verify the copied source hashes, native
ASan/UBSan tests, Go host tests, firmware image checksum/metadata, and the Mac
app's architectures/signature. The firmware image is 333,552 bytes, SHA-256
`fe247890e6ad24d4c1be3c3050c411d3ac37fb9b1156cbadc0e415d817754e42`.

The original project records live serial, WebDAV, Finder and preservation
checks; final human touch acceptance of the 0.1.2 control repair remains
pending. No device was flashed, no live mount was started, and no new physical
acceptance is claimed for this export. Private logs, SD data, credentials,
full-flash/NVS backups and SDK/build caches are excluded.

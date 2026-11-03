#!/bin/bash
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
release="$here/../artifacts/host"
cd "$here"
rm -rf "$release"
mkdir -p "$release/macos" "$release/linux-arm64" "$release/linux-amd64"
for arch in arm64 amd64; do
  GOOS=darwin GOARCH="$arch" CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' -o "$release/macos/cyd-usb-drive-$arch" .
done
lipo -create "$release/macos/cyd-usb-drive-arm64" "$release/macos/cyd-usb-drive-amd64" -output "$release/macos/cyd-usb-drive"
GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' -o "$release/linux-arm64/cyd-usb-drive" .
GOOS=linux GOARCH=amd64 CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' -o "$release/linux-amd64/cyd-usb-drive" .
app="$release/macos/Mount CYD Drive.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$release/macos/cyd-usb-drive" "$app/Contents/MacOS/Mount CYD Drive"
cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>Mount CYD Drive</string>
<key>CFBundleIdentifier</key><string>au.bespoke.cyd.usbdrive</string>
<key>CFBundleName</key><string>Mount CYD Drive</string>
<key>CFBundleDisplayName</key><string>Mount CYD Drive</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundleShortVersionString</key><string>0.1.0</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleIconFile</key><string>USBDrive</string>
<key>LSMinimumSystemVersion</key><string>11.0</string>
<key>LSUIElement</key><true/>
</dict></plist>
PLIST
if [[ -f "$here/assets/USBDrive.icns" ]]; then cp "$here/assets/USBDrive.icns" "$app/Contents/Resources/USBDrive.icns"; fi
chmod +x "$app/Contents/MacOS/Mount CYD Drive" "$release/linux-arm64/cyd-usb-drive" "$release/linux-amd64/cyd-usb-drive"
cp "$here"/third_party/*-LICENSE "$release/macos/"
cp "$here"/third_party/*-LICENSE "$release/linux-arm64/"
cp "$here"/third_party/*-LICENSE "$release/linux-amd64/"
file "$release/macos/cyd-usb-drive" "$release/linux-arm64/cyd-usb-drive" "$release/linux-amd64/cyd-usb-drive"
shasum -a 256 "$release/macos/cyd-usb-drive" "$release/linux-arm64/cyd-usb-drive" "$release/linux-amd64/cyd-usb-drive" > "$release/SHA256SUMS.txt"

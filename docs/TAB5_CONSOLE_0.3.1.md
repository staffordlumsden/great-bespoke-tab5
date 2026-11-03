# Console 0.3.1: Terminal Ready Indicator

28 October 2026. This patch marks the top **Terminal** tab ready by rendering
its border and label in green. Console remains the active blue tab and Ollama
remains orange. The Terminal tab's existing inactive tap behavior is unchanged;
Terminal can still be installed or launched through the SD application library.

The header update description is `TERMINAL READY`. No display pipeline, touch,
radio, installer, partition or C6 behavior changed.

## Verification

The complete Console host suite passed, including the boot lifecycle,
Weather-snapshot integration, production installer runtime, installer safety
and installer UI checks. The UI contract additionally asserts the green
Terminal branch and version string.

ESP-IDF v5.5.5 built the ESP32-P4 application successfully. The image is
1,366,208 bytes, leaving 67 percent of the 4 MiB resident factory partition
free. Its SHA-256 is:

```text
3b9e8b964bfb3590ed34591d09f8d52fb9a1d267ec31184028a80d2fdecb14f3
```

The attached device identified as ESP32-P4 revision v1.3. Before writing, the
complete existing 4 MiB factory application partition was saved locally under
the ignored recovery directory; its SHA-256 is:

```text
730ad59ba668b91eecca59514a15694931c097b1588bb13c1be630392bcdb70a
```

`idf.py app-flash` wrote only the resident application at `0x10000` and
esptool verified the write hash. An independent 1,366,208-byte readback then
matched the build byte for byte and produced the same SHA-256. NVS, PHY data,
the partition table, OTA slots, factory ESP32-C6 firmware and SD contents were
not rewritten.

## Device state

The Tab5 remained in its explicitly selected USB download mode after the flash
and readback. USB-triggered resets re-entered that mode, so no fresh 0.3.1 boot
trace or on-screen visual acceptance is claimed here. Press the physical reset
button once (or power-cycle normally) to boot the verified application, then
confirm that the Terminal tab appears green.

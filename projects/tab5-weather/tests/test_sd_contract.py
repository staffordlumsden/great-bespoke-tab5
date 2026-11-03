#!/usr/bin/env python3
"""Host checks for the proposed Tab5 OTA layout and P4 SD image packager."""

from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

PROJECT = Path(__file__).resolve().parents[1]
PACKAGER_PATH = PROJECT / "tools" / "package_sd_app.py"
SPEC = importlib.util.spec_from_file_location("tab5_packager", PACKAGER_PATH)
assert SPEC and SPEC.loader
PACKAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGER)


def image(*, chip=0x12, flash_size=4, project=b"Tab5Weather") -> bytes:
    header = bytearray(24)
    header[0] = 0xE9
    header[1] = 1
    header[2] = 2
    header[3] = flash_size << 4
    struct.pack_into("<H", header, 12, chip)
    struct.pack_into("<H", header, 15, 100)
    header[23] = 1
    segment = bytearray(256)
    struct.pack_into("<I", segment, 0, 0xABCD5432)
    segment[16:22] = b"0.1.0\0"
    segment[48 : 48 + len(project) + 1] = project + b"\0"
    data = header + struct.pack("<II", 0x4FF00000, len(segment)) + segment
    checksum = 0xEF
    for value in segment:
        checksum ^= value
    data.extend(b"\0" * ((len(data) | 15) - len(data)))
    data.append(checksum)
    data.extend(hashlib.sha256(data).digest())
    return bytes(data)


metadata = PACKAGER.inspect(image())
assert metadata["chip"] == "esp32p4"
assert metadata["slot_size"] == 0x500000
assert metadata["project"] == "Tab5Weather"

for bad, message in (
    (image(chip=0), "ESP32-P4"),
    (image(flash_size=2), "16 MB"),
    (image(project=b"tab5_console"), "resident Console"),
):
    try:
        PACKAGER.inspect(bad)
    except ValueError as error:
        assert message in str(error)
    else:
        raise AssertionError(f"packager accepted invalid case: {message}")

partitions = []
for raw in (PROJECT / "firmware" / "partitions.csv").read_text().splitlines():
    line = raw.strip()
    if not line or line.startswith("#"):
        continue
    fields = [field.strip() for field in line.split(",")]
    partitions.append((fields[0], int(fields[3], 0), int(fields[4], 0)))

expected = {
    "factory": (0x10000, 0x400000),
    "otadata": (0x410000, 0x2000),
    "ota_0": (0x420000, 0x500000),
    "ota_1": (0x920000, 0x500000),
}
actual = {name: (offset, size) for name, offset, size in partitions}
assert all(actual[name] == value for name, value in expected.items())
ordered = sorted(partitions, key=lambda partition: partition[1])
for left, right in zip(ordered, ordered[1:]):
    assert left[1] + left[2] <= right[1], f"overlap: {left[0]} and {right[0]}"
assert max(offset + size for _, offset, size in partitions) <= 0x1000000

wifi_source = (
    PROJECT / "components" / "tab5_weather_wifi" / "tab5_weather_wifi.c"
).read_text()
assert "esp_wifi_get_config" in wifi_source
assert "esp_wifi_set_config" in wifi_source
assert "esp_wifi_scan_start" in wifi_source
assert "esp_wifi_connect" in wifi_source
assert "esp_wifi_disconnect" in wifi_source
assert "WIFI_STORAGE_FLASH" in wifi_source
assert "nvs_flash_erase" not in wifi_source
assert "ESP_LOG" not in wifi_source

wifi_header = (
    PROJECT / "components" / "tab5_weather_wifi" / "include" / "tab5_weather_wifi.h"
).read_text()
for operation in ("scan", "get_network", "connect", "disconnect", "retry"):
    assert f"tab5_weather_wifi_{operation}" in wifi_header

main_source = (PROJECT / "firmware" / "main" / "tab5_weather.cpp").read_text()
assert "clear_wifi_password" in main_source
assert "Set Wi-Fi in Console first" not in main_source

firmware_cmake = (PROJECT / "firmware" / "CMakeLists.txt").read_text()
assert '"${CMAKE_CURRENT_LIST_DIR}/../components"' in firmware_cmake

sdkconfig = (PROJECT / "firmware" / "sdkconfig").read_text()
for setting in (
    "CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y",
    "CONFIG_ESP32P4_REV_MIN_100=y",
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
    "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y",
    "CONFIG_ESP_WIFI_NVS_ENABLED=y",
    "CONFIG_ESP_WIFI_REMOTE_ENABLED=y",
    "CONFIG_ESP_HOSTED_ENABLED=y",
    "CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y",
):
    assert setting in sdkconfig
assert "# CONFIG_BT_ENABLED is not set" in sdkconfig

dependency_lock = (PROJECT / "firmware" / "dependencies.lock").read_text()
assert "espressif/esp_hosted:" in dependency_lock and "version: 1.4.0" in dependency_lock
assert "espressif/esp_wifi_remote:" in dependency_lock and "version: 0.8.5" in dependency_lock

bridge = (PROJECT / "firmware" / "main" / "console_bridge.cpp").read_text()
assert "tab5_app_boot_ready" in bridge and "tab5_app_boot_return_to_console" in bridge
console = PROJECT.parent / "tab5-console"
assert (console / "partitions.csv").read_text().splitlines()[3:] == (
    PROJECT / "firmware/partitions.csv"
).read_text().splitlines()[3:]

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    source = root / "Tab5Weather.bin"
    notice = root / "NOTICE.txt"
    source.write_bytes(image())
    notice.write_text("Test notice\n")
    output = root / "firmware"
    completed = subprocess.run(
        [
            sys.executable,
            str(PACKAGER_PATH),
            "--image",
            str(source),
            "--output",
            str(output),
            "--license",
            str(notice),
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    assert "chip=esp32p4 verified=YES" in completed.stdout
    name = "Tab5Weather-0.1.0"
    assert (output / f"{name}.bin").read_bytes() == source.read_bytes()
    assert (output / f"{name}-LICENSE.txt").read_text() == "Test notice\n"
    assert '"chip": "esp32p4"' in (output / f"{name}.json").read_text()
    assert f'"license": "{name}-LICENSE.txt"' in (output / f"{name}.json").read_text()

print(
    "PASS Tab5 SD/Wi-Fi contract: P4 image gate, 16 MB OTA layout and "
    "bounded station scan/join controls without NVS erase or credential logging"
)

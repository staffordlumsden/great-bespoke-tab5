#!/usr/bin/env python3
"""Host checks for the Console SD-app installer component."""

from pathlib import Path
import hashlib
import importlib.util
import json
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components" / "tab5_installer"
SOURCE = (COMPONENT / "tab5_installer.c").read_text()
HEADER = (COMPONENT / "include" / "tab5_installer.h").read_text()

def assert_contains(text, needle, label):
    if needle not in text:
        raise AssertionError(f"{label}: missing {needle!r}")

for item in (
    "tab5_installer_scan", "tab5_installer_validate", "tab5_installer_install",
    "tab5_installer_confirm_running", "tab5_installer_return_to_factory",
    "tab5_installer_get_status",
):
    assert_contains(HEADER, item, "header")
    assert_contains(SOURCE, item, "source")

for invariant in (
    "tab5_app_boot_select",
    "tab5_app_boot_prepare_launch",
    "esp_ota_begin",
    "OTA_WITH_SEQUENTIAL_WRITES",
    "esp_ota_write",
    "esp_ota_end",
    "esp_partition_read",
    "mbedtls_sha256",
    "0xABCD5432",
    "0x0012",
    "0x500000",
    "tab5_console",
    "nvs_flash_erase",
):
    if invariant == "nvs_flash_erase":
        if invariant in SOURCE:
            raise AssertionError("installer must not erase NVS")
        continue
    assert_contains(SOURCE, invariant, f"source invariant {invariant}")

assert "ESP_PARTITION_SUBTYPE_APP_FACTORY" in (ROOT / "components/tab5_app_boot/tab5_app_boot.c").read_text()
assert "ESP_PARTITION_SUBTYPE_APP_OTA_1" in SOURCE
assert "TAB5_INSTALLER_MAX_PACKAGES 32" in HEADER
assert "qsort" in SOURCE
assert "compare_packages" in SOURCE

print("PASS: TAB5 Console installer contract; no NVS erase, factory preserved")

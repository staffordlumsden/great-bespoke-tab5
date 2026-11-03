#!/usr/bin/env python3
"""Offline migration/rollback checks, including a simulated esptool transport."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "migration", Path(__file__).resolve().parents[1] / "sd_launcher_migration.py")
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


def table(entries):
    data = b"".join(struct.pack("<HBBII16sI", 0x50aa, kind, subtype, address, size,
                                f"p{i}".encode(), 0)
                    for i, (kind, subtype, address, size) in enumerate(entries))
    return (data + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()).ljust(4096, b"\xff")


def image(application):
    header = bytearray(24)
    header[0:4] = bytes([0xe9, 1, 2, 0x40])
    struct.pack_into("<H", header, 12, 0x12)
    struct.pack_into("<H", header, 15, 100)
    header[23] = 1
    segment = bytearray(256)
    if application:
        struct.pack_into("<I", segment, 0, 0xabcd5432)
        segment[48:61] = b"tab5_console\0"
    checksum = 0xef
    for value in segment:
        checksum ^= value
    data = header + struct.pack("<II", 0x4ff00000, len(segment)) + segment
    data.extend(b"\0" * ((len(data) | 15) - len(data)))
    data.append(checksum)
    return bytes(data) + hashlib.sha256(data).digest()


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="tab5-migration-check-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        for folder in ("config", "bootloader/config", "partition_table"):
            (self.build / folder).mkdir(parents=True, exist_ok=True)
        (self.build / "project_description.json").write_text(json.dumps({
            "target": "esp32p4", "git_revision": "v5.5.5"}))
        self.config = {"BOOTLOADER_APP_ROLLBACK_ENABLE": True,
                       "ESP32P4_SELECTS_REV_LESS_V3": True, "ESP32P4_REV_MIN_FULL": 100,
                       "ESPTOOLPY_FLASHSIZE": "16MB"}
        (self.build / "config/sdkconfig.json").write_text(json.dumps(self.config))
        (self.build / "bootloader/config/sdkconfig.h").write_text(
            "#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1\n")
        (self.build / "tab5_console.bin").write_bytes(image(True))
        (self.build / "bootloader/bootloader.bin").write_bytes(image(False))
        (self.build / "partition_table/partition-table.bin").write_bytes(table(M.LAYOUT))
        original = bytearray(b"\xff" * M.FLASH_SIZE)
        original[0x8000:0x9000] = table(M.LEGACY)
        original[0x9000:0x10000] = b"N" * 0x7000  # stand-in for private NVS/PHY
        self.original = bytes(original)
        self.backup = self.root / "full-flash.bin"
        self.backup.write_bytes(self.original)
        self.bundle = self.root / "bundle"
        self.flash = bytearray(self.original)
        self.writes = []
        self.reset_count = 0

    def prepare(self):
        return M.prepare(self.backup, self.build, self.bundle)

    def transport(self, port, args, log, *, first=False, reset=False):
        self.assertEqual(port, "TEST-P4")
        operation = args[0]
        if operation == "get_security_info":
            self.assertTrue(first or reset)
            if reset:
                self.reset_count += 1
            return ("Chip is ESP32-P4 (revision v1.3)\nSecure Boot: Disabled\n"
                    "Flash Encryption: Disabled\n")
        if operation == "read_flash":
            address, size = int(args[1], 0), int(args[2], 0)
            Path(args[3]).write_bytes(self.flash[address:address + size])
        elif operation in ("write_flash", "verify_flash"):
            address = int(args[-2], 0)
            data = Path(args[-1]).read_bytes()
            self.assertIn((address, len(data)), [(a, s) for _, a, s in M.REGIONS])
            if operation == "write_flash":
                self.writes.append(address)
                self.flash[address:address + len(data)] = data
            else:
                self.assertEqual(self.flash[address:address + len(data)], data)
        elif operation == "run":
            self.assertTrue(reset)
            self.reset_count += 1
        else:
            self.fail(f"unexpected device command: {operation}")
        return "OK"

    def test_apply_and_recover_preserve_all_other_flash(self):
        plan = self.prepare()
        with patch.object(M, "run_tool", self.transport):
            receipt = M.execute(self.bundle, "TEST-P4")
            self.assertTrue(json.loads((receipt / "receipt.json").read_text())["nvs_phy_preserved"])
            self.assertEqual(self.writes, [address for _, address, _ in M.REGIONS])
            self.assertEqual(M.partition_layout(self.flash[0x8000:0x9000]), M.LAYOUT)
            self.assertEqual(self.flash[0x9000:0x10000], self.original[0x9000:0x10000])
            self.assertEqual(self.flash[0x420000:], self.original[0x420000:])
            M.execute(self.bundle, "TEST-P4", recover=True)
        self.assertEqual(bytes(self.flash), self.original)
        self.assertEqual(self.reset_count, 2)
        self.assertEqual(plan["backup_sha256"], M.digest(self.original))

    def test_changed_device_is_rejected_before_writes(self):
        self.prepare()
        self.flash[0x9000] ^= 1
        with patch.object(M, "run_tool", self.transport), self.assertRaises(ValueError):
            M.execute(self.bundle, "TEST-P4")
        self.assertFalse(self.writes)
        self.assertEqual(self.reset_count, 0)

    def test_payload_tampering_is_rejected(self):
        self.prepare()
        (self.bundle / "console.bin").write_bytes(b"invalid")
        with self.assertRaises(ValueError):
            M.load_bundle(self.bundle)

    def test_plan_cannot_target_nvs(self):
        plan = self.prepare()
        plan["regions"][0]["address"] = 0x9000
        (self.bundle / "plan.json").write_text(json.dumps(plan))
        with self.assertRaises(ValueError):
            M.load_bundle(self.bundle)

    def test_reject_incompatible_build_before_creating_bundle(self):
        self.config["BOOTLOADER_APP_ROLLBACK_ENABLE"] = False
        (self.build / "config/sdkconfig.json").write_text(json.dumps(self.config))
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.bundle.exists())

    def test_invalid_table_and_image(self):
        bad = bytearray(table(M.LAYOUT))
        bad[8] ^= 1
        with self.assertRaises(ValueError):
            M.partition_layout(bad)
        bad = bytearray(image(True))
        bad[50] ^= 1
        with self.assertRaises(ValueError):
            M.inspect_image(bad, application=True)

    def test_failed_write_does_not_reset_or_continue(self):
        self.prepare()
        def fail(port, args, log, **kwargs):
            if args[0] == "write_flash" and self.writes:
                raise RuntimeError("simulated power/transport failure")
            return self.transport(port, args, log, **kwargs)
        with patch.object(M, "run_tool", fail), self.assertRaises(RuntimeError):
            M.execute(self.bundle, "TEST-P4")
        self.assertEqual(self.writes, [0x10000])
        self.assertEqual(self.reset_count, 0)
        self.assertEqual(self.flash[0x9000:0x10000], self.original[0x9000:0x10000])


if __name__ == "__main__":
    unittest.main(verbosity=2)

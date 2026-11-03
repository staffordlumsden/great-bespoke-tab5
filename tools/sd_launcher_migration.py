#!/usr/bin/env python3
"""Prepare, apply or recover the one-time Tab5 resident-Console migration.

Preparation is offline. Apply/recover require an explicit P4 port, the ESP-IDF
Python environment and a private bundle made from a complete 16 MiB backup.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLASH_SIZE = 0x1000000
# The table is committed last; every write covers whole, non-overlapping sectors.
REGIONS = (
    ("console", 0x10000, 0x400000),
    ("otadata", 0x410000, 0x2000),
    ("bootloader", 0x2000, 0x6000),
    ("partition-table", 0x8000, 0x1000),
)
LEGACY = [(1, 2, 0x9000, 0x6000), (1, 1, 0xf000, 0x1000), (0, 0, 0x10000, 0x400000)]
LAYOUT = LEGACY + [(1, 0, 0x410000, 0x2000), (0, 16, 0x420000, 0x500000),
                   (0, 17, 0x920000, 0x500000)]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def partition_layout(data: bytes) -> list:
    """Parse ESP-IDF's table and require its MD5 record; refuse extra partitions."""
    entries = []
    for offset in range(0, min(len(data), 0xc00), 32):
        record = data[offset:offset + 32]
        if record[:2] == b"\xeb\xeb":
            if len(record) != 32 or record[16:] != hashlib.md5(data[:offset]).digest():
                raise ValueError("partition table MD5 mismatch")
            return entries
        if len(record) != 32 or record[:2] != b"\xaa\x50":
            break
        _, kind, subtype, address, size, _, flags = struct.unpack("<HBBII16sI", record)
        if flags or address + size > FLASH_SIZE:
            raise ValueError("unsupported partition flags or flash boundary")
        entries.append((kind, subtype, address, size))
    raise ValueError("partition table has no valid MD5 record")


def inspect_image(data: bytes, *, application: bool) -> None:
    """Verify P4/v1.3/16 MiB gates and the actual image checksum and SHA."""
    if len(data) < 64 or data[0] != 0xe9 or not 1 <= data[1] <= 16:
        raise ValueError("not an ESP image")
    minimum = struct.unpack_from("<H", data, 15)[0] or data[14] * 100
    maximum = struct.unpack_from("<H", data, 17)[0]
    if (struct.unpack_from("<H", data, 12)[0] != 0x12 or data[3] >> 4 != 4 or
            minimum > 103 or maximum not in (0, 65535) and maximum < 103):
        raise ValueError("image is incompatible with this P4 revision v1.3 / 16 MiB Tab5")
    offset, checksum = 24, 0xef
    for _ in range(data[1]):
        if offset + 8 > len(data):
            raise ValueError("truncated ESP segment")
        size = struct.unpack_from("<I", data, offset + 4)[0]
        offset += 8
        if size % 4 or offset + size > len(data):
            raise ValueError("invalid ESP segment length")
        for value in data[offset:offset + size]:
            checksum ^= value
        offset += size
    checksum_at = offset | 15
    if (data[23] != 1 or checksum_at + 33 != len(data) or
            data[checksum_at] != checksum or digest(data[:-32]) != data[-32:].hex()):
        raise ValueError("image checksum, length or SHA-256 mismatch")
    if application and (struct.unpack_from("<I", data, 32)[0] != 0xabcd5432 or
                        data[80:112].split(b"\0", 1)[0] != b"tab5_console"):
        raise ValueError("factory image is not tab5_console")


def prepare(backup: Path, build: Path, output: Path) -> dict:
    original = backup.read_bytes()
    if len(original) != FLASH_SIZE:
        raise ValueError("a complete 16 MiB flash backup is required")
    if partition_layout(original[0x8000:0x9000]) not in (LEGACY, LAYOUT):
        raise ValueError("backup does not contain the supported Console layout")
    description = json.loads((build / "project_description.json").read_text())
    config = json.loads((build / "config/sdkconfig.json").read_text())
    boot_config = (build / "bootloader/config/sdkconfig.h").read_text()
    if (description["target"] != "esp32p4" or description["git_revision"] != "v5.5.5" or
            not config.get("BOOTLOADER_APP_ROLLBACK_ENABLE") or
            not config.get("ESP32P4_SELECTS_REV_LESS_V3") or
            config.get("ESP32P4_REV_MIN_FULL") != 100 or
            config.get("ESPTOOLPY_FLASHSIZE") != "16MB" or
            config.get("SECURE_BOOT") or config.get("SECURE_FLASH_ENC_ENABLED") or
            "#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1" not in boot_config):
        raise ValueError("build must use IDF v5.5.5, pre-v3 P4, 16 MiB and bootloader rollback")
    images = {
        "console": (build / "tab5_console.bin").read_bytes(),
        "bootloader": (build / "bootloader/bootloader.bin").read_bytes(),
        "partition-table": (build / "partition_table/partition-table.bin").read_bytes(),
        "otadata": b"\xff" * 0x2000,
    }
    inspect_image(images["console"], application=True)
    inspect_image(images["bootloader"], application=False)
    if partition_layout(images["partition-table"]) != LAYOUT:
        raise ValueError("compiled partition table does not match the shared launcher layout")
    for name, _, size in REGIONS:
        if len(images[name]) > size:
            raise ValueError(f"{name} exceeds its reserved region")

    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    (output / "recovery").mkdir(mode=0o700)
    plan = {"schema": 1, "backup_sha256": digest(original), "flash_size": FLASH_SIZE,
            "nvs_phy_sha256": digest(original[0x9000:0x10000]), "regions": []}
    for name, address, size in REGIONS:
        new = images[name].ljust(size, b"\xff")
        old = original[address:address + size]
        (output / f"{name}.bin").write_bytes(new)
        (output / "recovery" / f"{name}.bin").write_bytes(old)
        plan["regions"].append({"name": name, "address": address, "size": size,
                                "sha256": digest(new), "recovery_sha256": digest(old)})
    (output / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    return plan


def load_bundle(bundle: Path, *, recover: bool = False) -> tuple[dict, list]:
    plan = json.loads((bundle / "plan.json").read_text())
    if plan["schema"] != 1 or plan["flash_size"] != FLASH_SIZE:
        raise ValueError("unsupported migration plan")
    if [(r["name"], r["address"], r["size"]) for r in plan["regions"]] != list(REGIONS):
        raise ValueError("plan attempts writes outside the fixed migration regions")
    images = []
    for region in plan["regions"]:
        source = bundle / ("recovery" if recover else "") / (region["name"] + ".bin")
        data = source.read_bytes()
        if (len(data) != region["size"] or
                digest(data) != region["recovery_sha256" if recover else "sha256"]):
            raise ValueError(f"bundle payload changed: {region['name']}")
        images.append((region["address"], source, data))
    return plan, images


def run_tool(port: str, args: list[str], log: Path, *, first: bool = False,
             reset: bool = False) -> str:
    command = [sys.executable, "-m", "esptool", "--chip", "esp32p4", "--port", port,
               "--baud", "460800", "--before", "default_reset" if first else "no_reset",
               "--after", "watchdog_reset" if reset else "no_reset", *args]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with log.open("a") as stream:
        stream.write(result.stdout)
    if result.returncode:
        raise RuntimeError(f"esptool {args[0]} failed; see the private migration log")
    return result.stdout


def execute(bundle: Path, port: str, *, recover: bool = False) -> Path:
    plan, images = load_bundle(bundle, recover=recover)
    attempt = Path(tempfile.mkdtemp(prefix="recovery-" if recover else "apply-", dir=bundle))
    log = attempt / "esptool.log"
    security = run_tool(port, ["get_security_info"], log, first=True)
    if ("Chip is ESP32-P4 (revision v1.3)" not in security or
            "Secure Boot: Disabled" not in security or "Flash Encryption: Disabled" not in security):
        raise ValueError("preflight did not confirm P4 v1.3 with secure boot/encryption disabled")
    before_file = attempt / "before.bin"
    run_tool(port, ["read_flash", "0", hex(FLASH_SIZE), str(before_file)], log)
    before = before_file.read_bytes()
    if len(before) != FLASH_SIZE or not recover and digest(before) != plan["backup_sha256"]:
        raise ValueError("device differs from the prepared full backup; prepare a fresh bundle")
    expected = bytearray(before)
    for address, source, data in images:
        # Keep image header bytes exact. Never force compatibility or security overrides.
        run_tool(port, ["write_flash", "--flash_mode", "keep", "--flash_freq", "keep",
                        "--flash_size", "keep", hex(address), str(source)], log)
        run_tool(port, ["verify_flash", hex(address), str(source)], log)
        expected[address:address + len(data)] = data
    after_file = attempt / "after.bin"
    run_tool(port, ["read_flash", "0", hex(FLASH_SIZE), str(after_file)], log)
    after = after_file.read_bytes()
    if after != expected:
        raise ValueError("full flash readback differs; device left in download mode for recovery")
    if before[0x9000:0x10000] != after[0x9000:0x10000]:
        raise ValueError("NVS/PHY preservation check failed")
    (attempt / "receipt.json").write_text(json.dumps({
        "operation": "recover" if recover else "apply", "before_sha256": digest(before),
        "after_sha256": digest(after), "nvs_phy_preserved": True,
        "full_flash_readback_verified": True,
    }, indent=2) + "\n")
    # P4's USB-UART reset can retain download mode. Use esptool's supported
    # P4 watchdog reset to leave ROM after verification, without flash writes.
    run_tool(port, ["get_security_info"], log, reset=True)
    return attempt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    prep = sub.add_parser("prepare", help="offline: validate build/backup and save recovery regions")
    prep.add_argument("--backup", type=Path, required=True)
    prep.add_argument("--build", type=Path, default=ROOT / "projects/tab5-console/build")
    prep.add_argument("--output", type=Path, required=True)
    for name in ("apply", "recover"):
        action = sub.add_parser(name, help="write the explicit P4 port and verify all flash")
        action.add_argument("--bundle", type=Path, required=True)
        action.add_argument("--port", required=True)
    args = parser.parse_args()
    try:
        if args.operation == "prepare":
            plan = prepare(args.backup, args.build, args.output)
            print(f"Prepared {len(plan['regions'])} regions and recovery payloads: {args.output}")
        else:
            result = execute(args.bundle, args.port, recover=args.operation == "recover")
            print(f"Verified {args.operation}; private receipt: {result / 'receipt.json'}")
    except (ValueError, OSError, KeyError, RuntimeError) as error:
        parser.exit(1, f"Stopped: {error}\nNo further writes or automatic recovery attempted.\n")


if __name__ == "__main__":
    main()

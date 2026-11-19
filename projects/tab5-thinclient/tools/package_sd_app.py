#!/usr/bin/env python3
"""Validate an ESP32-P4 application and prepare an additive Tab5 SD package."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct

SLOT_SIZE = 0x500000
ESP32P4_CHIP_ID = 0x0012
TESTED_REVISION = 103
FLASH_SIZE_16MB = 4


def inspect(data: bytes) -> dict:
    if not 320 <= len(data) <= SLOT_SIZE:
        raise ValueError("application does not fit a Tab5 app slot")
    if data[0] != 0xE9 or not 1 <= data[1] <= 16:
        raise ValueError("not an ESP application")
    if struct.unpack_from("<H", data, 12)[0] != ESP32P4_CHIP_ID:
        raise ValueError("image is not for ESP32-P4")
    if data[3] >> 4 != FLASH_SIZE_16MB:
        raise ValueError("image must declare the Tab5 16 MB flash size")
    if data[23] != 1 or struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
        raise ValueError("use an application .bin with appended SHA-256")

    minimum = struct.unpack_from("<H", data, 15)[0] or data[14] * 100
    maximum = struct.unpack_from("<H", data, 17)[0]
    if minimum > TESTED_REVISION or (
        maximum not in (0, 65535) and maximum < TESTED_REVISION
    ):
        raise ValueError("image does not support the tested ESP32-P4 revision v1.3")

    fields = []
    for offset in (48, 80):
        raw = data[offset : offset + 32]
        if b"\0" not in raw or not raw[0]:
            raise ValueError("invalid application description")
        value = raw.split(b"\0", 1)[0].decode("ascii")
        if any(ord(character) < 32 or ord(character) > 126 for character in value):
            raise ValueError("invalid application description")
        fields.append(value)
    version, project = fields
    if project == "tab5_console":
        raise ValueError("the resident Console is not an SD application")

    offset, checksum = 24, 0xEF
    for _ in range(data[1]):
        if offset + 8 > len(data):
            raise ValueError("truncated segment")
        length = struct.unpack_from("<I", data, offset + 4)[0]
        offset += 8
        if length % 4 or offset + length > len(data):
            raise ValueError("invalid segment length")
        for value in data[offset : offset + length]:
            checksum ^= value
        offset += length
    checksum_at = offset | 15
    if checksum_at + 33 != len(data) or data[checksum_at] != checksum:
        raise ValueError("invalid image length or checksum")
    if hashlib.sha256(data[:-32]).digest() != data[-32:]:
        raise ValueError("image SHA-256 mismatch")
    return {
        "project": project,
        "version": version,
        "size_bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "chip": "esp32p4",
        "minimum_chip_revision": minimum,
        "slot_size": SLOT_SIZE,
    }


def add_verified(path: Path, data: bytes) -> None:
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError(f"refusing to replace a different existing file: {path.name}")
        return
    temporary = path.with_name("." + path.name + ".copy")
    with temporary.open("xb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    if temporary.read_bytes() != data:
        raise OSError("copy verification failed")
    temporary.rename(path)
    if path.read_bytes() != data:
        raise OSError("final copy verification failed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--license", type=Path, required=True)
    args = parser.parse_args()

    data = args.image.read_bytes()
    metadata = inspect(data)
    name = f'{metadata["project"]}-{metadata["version"]}.bin'
    if len(name) > 100 or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9 ._-]*\.bin", name):
        raise ValueError("application metadata cannot form a safe library filename")
    licence = args.license.read_bytes()
    if not licence.strip():
        raise ValueError("licence notice is empty")

    args.output.mkdir(parents=True, exist_ok=True)
    add_verified(args.output / name, data)
    metadata["image"] = name
    metadata["license"] = name[:-4] + "-LICENSE.txt"
    add_verified(
        args.output / (name[:-4] + ".json"),
        (json.dumps(metadata, indent=2) + "\n").encode(),
    )
    add_verified(args.output / (name[:-4] + "-LICENSE.txt"), licence)
    print(
        f'PACKAGED {name} bytes={len(data)} sha256={metadata["sha256"]} '
        "chip=esp32p4 verified=YES"
    )


if __name__ == "__main__":
    main()

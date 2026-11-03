#!/usr/bin/env python3
"""Validate the Console-only release triplet against the compiled image."""
import argparse
import hashlib
import json
import re
from pathlib import Path
from package_sd_app import inspect, SLOT_SIZE


def validate(directory: Path, built_image: Path) -> dict:
    built = inspect(built_image.read_bytes())
    assert built['project'] == 'Tab5Shell'
    assert re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,31}', built['version'])
    stem = 'Tab5Shell-' + built['version']
    data = (directory / (stem + '.bin')).read_bytes()
    metadata = inspect(data)
    manifest = json.loads((directory / (stem + '.json')).read_text())
    assert data == built_image.read_bytes(), 'packaged image differs from build'
    assert metadata['project'] == 'Tab5Shell' and metadata['version'] == built['version']
    assert metadata['minimum_chip_revision'] == 100, 'pre-v3 minimum changed'
    for key, value in metadata.items():
        assert manifest[key] == value, 'manifest mismatch: ' + key
    assert manifest['image'] == stem + '.bin'
    assert manifest['license'] == stem + '-LICENSE.txt'
    licence = (directory / manifest['license']).read_text()
    for attribution in ('Lua', 'libssh2', 'PixelFonts', 'SIL OPEN FONT LICENSE', 'Apache', 'Boost'):
        assert attribution in licence, 'missing attribution: ' + attribution
    # Exercise the image inspector's rejection gates against this real image.
    for offset, value in ((0, 0), (12, 0), (3, 0), (15, 255), (23, 0), (100, data[100] ^ 1)):
        damaged = bytearray(data)
        damaged[offset] = value
        try:
            inspect(bytes(damaged))
        except (ValueError, UnicodeError):
            pass
        else:
            raise AssertionError('damaged image accepted at byte ' + str(offset))
    try:
        inspect(data[:-1])
    except ValueError:
        pass
    else:
        raise AssertionError('truncated image accepted')
    print('PASS release image/header/revision/checksum/SHA256/manifest/licences/negative images')
    print(f"image_bytes={len(data)} slot_bytes={SLOT_SIZE} headroom_bytes={SLOT_SIZE-len(data)}")
    for path in sorted(directory.iterdir()):
        if path.is_file():
            print(hashlib.sha256(path.read_bytes()).hexdigest(), path.name)
    return metadata


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package-dir', type=Path, required=True)
    parser.add_argument('--image', type=Path, required=True)
    args = parser.parse_args()
    validate(args.package_dir, args.image)

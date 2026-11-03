#!/usr/bin/env python3
"""Generate the fixed-width terminal atlases from Source Code Pro.

The generated header is committed so ESP-IDF builds do not need Pillow or a
network connection. Obtain the two input TTFs from the pinned upstream commit
recorded below, verify their SHA-256 values, then pass their local paths to
this script.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


UPSTREAM_COMMIT = "803b7e23ec97ae58b6232ea76519a76d428ba268"
EXPECTED_SHA256 = {
    "regular": "74bd80d3e42a08517cd7e1108ba3d86f2da29ac0f3065be95e0357956ab9db37",
    "bold": "b2095e0d657e6d28dc32444a9dacabab0c9241d0bf39d96371756cc9bdbc3a5f",
}
# Keep ordinary printable ASCII and add the complete Unicode Box Drawing,
# Block Elements and first geometric-symbol run supplied by Source Code Pro.
# Terminal applications use these codepoints for borders, separators, progress
# bars and shades; dropping them to '?' makes otherwise-correct TUIs unusable.
CODEPOINTS = tuple(range(0x20, 0x7F)) + tuple(range(0x2500, 0x25A2))
THRESHOLD = 96

# Nominal point sizes are the user-facing zoom labels. Pixel sizes are tuned
# to the Tab5's existing fixed terminal cells without changing the proven PTY
# geometries.
SPECS = (
    ("tab5_terminal_mono_8pt", 8, 18, 12, 20),
    ("tab5_terminal_mono_10pt", 10, 24, 16, 27),
    ("tab5_terminal_mono_15pt", 15, 38, 24, 40),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def render_block_element(image: Image.Image, code: int) -> None:
    """Render U+2580-U+259F against the actual terminal cell geometry.

    Font block glyphs are positioned against the font em square, which is
    intentionally taller than our fixed cells. Rasterising those outlines on
    the text baseline clips fractional upper blocks (U+2594 became empty) and
    distorts half blocks. These shapes are cell geometry, so construct them at
    exact cell fractions instead.
    """
    width, height = image.size

    def fill(left: int, top: int, right: int, bottom: int) -> None:
        if left < right and top < bottom:
            image.paste(255, (left, top, right, bottom))

    def lower(eighths: int) -> None:
        pixels = max(1, (height * eighths + 7) // 8)
        fill(0, height - pixels, width, height)

    def left(eighths: int) -> None:
        pixels = max(1, (width * eighths + 7) // 8)
        fill(0, 0, pixels, height)

    if code == 0x2580:
        fill(0, 0, width, (height + 1) // 2)
    elif 0x2581 <= code <= 0x2588:
        lower(code - 0x2580)
    elif 0x2589 <= code <= 0x258F:
        left(0x2590 - code)
    elif code == 0x2590:
        fill(width // 2, 0, width, height)
    elif 0x2591 <= code <= 0x2593:
        level = code - 0x2590  # 1/4, 1/2, 3/4 deterministic shade
        pixels = image.load()
        for row in range(height):
            for column in range(width):
                if (column + row * 2) % 4 < level:
                    pixels[column, row] = 255
    elif code == 0x2594:
        fill(0, 0, width, max(1, (height + 7) // 8))
    elif code == 0x2595:
        pixels = max(1, (width + 7) // 8)
        fill(width - pixels, 0, width, height)
    else:
        mid_x = (width + 1) // 2
        mid_y = (height + 1) // 2
        quadrants = {
            0x2596: (False, False, True, False),
            0x2597: (False, False, False, True),
            0x2598: (True, False, False, False),
            0x2599: (True, False, True, True),
            0x259A: (True, False, False, True),
            0x259B: (True, True, True, False),
            0x259C: (True, True, False, True),
            0x259D: (False, True, False, False),
            0x259E: (False, True, True, False),
            0x259F: (False, True, True, True),
        }[code]
        if quadrants[0]: fill(0, 0, mid_x, mid_y)
        if quadrants[1]: fill(mid_x, 0, width, mid_y)
        if quadrants[2]: fill(0, mid_y, mid_x, height)
        if quadrants[3]: fill(mid_x, mid_y, width, height)


def rasterise(path: Path, pixel_size: int, cell_width: int,
              cell_height: int) -> tuple[list[int], int]:
    font = ImageFont.truetype(path, pixel_size)
    # Base the common baseline on ASCII. Full-cell block glyphs deliberately
    # reach the cell edges and must not move letters or punctuation vertically.
    boxes = [font.getbbox(chr(code), anchor="ls") for code in range(0x20, 0x7F)]
    top = min(box[1] for box in boxes)
    bottom = max(box[3] for box in boxes)
    ink_height = bottom - top
    if ink_height > cell_height:
        raise ValueError(f"{path.name} {pixel_size}px is taller than {cell_height}px")

    baseline = (cell_height - ink_height) // 2 - top
    advance = round(font.getlength("M"))
    x = (cell_width - advance) // 2
    row_bytes = (cell_width + 7) // 8
    glyph_stride = row_bytes * cell_height
    output: list[int] = []

    for code in CODEPOINTS:
        image = Image.new("L", (cell_width, cell_height), 0)
        if 0x2580 <= code <= 0x259F:
            render_block_element(image, code)
        else:
            draw = ImageDraw.Draw(image)
            draw.text((x, baseline), chr(code), font=font, fill=255, anchor="ls")
        pixels = image.load()
        for row in range(cell_height):
            for byte_column in range(row_bytes):
                value = 0
                for bit in range(8):
                    column = byte_column * 8 + bit
                    if column < cell_width and pixels[column, row] >= THRESHOLD:
                        value |= 0x80 >> bit
                output.append(value)

    return output, glyph_stride


def array_lines(name: str, data: list[int]) -> list[str]:
    lines = [f"inline constexpr std::uint8_t {name}_bits[] = {{"]
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16])
        lines.append(f"    {values},")
    lines.append("};")
    return lines


def generate(regular: Path, bold: Path) -> str:
    for label, path in (("regular", regular), ("bold", bold)):
        actual = sha256(path)
        if actual != EXPECTED_SHA256[label]:
            raise ValueError(
                f"{path} SHA-256 {actual} does not match pinned {label} font"
            )

    lines = [
        "// Generated by firmware/tools/generate_terminal_fonts.py.",
        f"// Source Code Pro commit {UPSTREAM_COMMIT}; SIL OFL 1.1.",
        "// This converted subset's primary font name is Tab5 Terminal Mono.",
        "// See THIRD_PARTY_NOTICES.txt. Do not edit this file by hand.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace tab5_terminal_fonts {",
        "",
        "struct Font {",
        "    const std::uint8_t* bitmap;",
        "    const std::uint32_t* codepoints;",
        "    std::uint16_t glyph_stride;",
        "    std::uint16_t glyph_count;",
        "    std::uint8_t cell_width;",
        "    std::uint8_t cell_height;",
        "    std::uint8_t point_size;",
        "};",
        "",
        "inline constexpr std::uint32_t terminal_codepoints[] = {",
    ]

    for offset in range(0, len(CODEPOINTS), 12):
        values = ", ".join(
            f"0x{code:04x}" for code in CODEPOINTS[offset:offset + 12]
        )
        lines.append(f"    {values},")
    lines.extend(("};", ""))

    definitions: list[str] = []
    for name, point_size, pixel_size, cell_width, cell_height in SPECS:
        for weight, path in (("regular", regular), ("bold", bold)):
            symbol = f"{name}_{weight}"
            data, stride = rasterise(path, pixel_size, cell_width, cell_height)
            lines.extend(array_lines(symbol, data))
            lines.append("")
            definitions.append(
                f"inline constexpr Font {symbol} = "
                f"{{{symbol}_bits, terminal_codepoints, {stride}, "
                f"{len(CODEPOINTS)}, "
                f"{cell_width}, {cell_height}, {point_size}}};"
            )

    lines.extend(definitions)
    lines.extend(("", "}  // namespace tab5_terminal_fonts", ""))
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--regular", required=True, type=Path)
    parser.add_argument("--bold", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.write_text(generate(args.regular, args.bold), encoding="utf-8")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Convert a TrueType/OpenType font into a loom A8 atlas C source file.

The converter uses HarfBuzz command line tools (`hb-view` and `hb-shape`) for
host-side rasterization and metrics, keeping the Python side dependency-free.
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import struct
import subprocess
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Glyph:
    codepoint: int
    atlas_x: int
    atlas_y: int
    width: int
    height: int
    bearing_x: int
    bearing_y: int
    advance_x: int
    bitmap: list[int]


def _read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def _read_i16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">h", data, offset)[0]


def _read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def read_font_extents(font_path: Path, size_px: int) -> tuple[int, int]:
    data = font_path.read_bytes()
    if data[:4] == b"ttcf":
        raise ValueError("TTC collections are not supported; pass a .ttf/.otf")

    table_count = _read_u16(data, 4)
    tables: dict[bytes, tuple[int, int]] = {}
    for i in range(table_count):
        entry = 12 + i * 16
        tag = data[entry : entry + 4]
        offset = _read_u32(data, entry + 8)
        length = _read_u32(data, entry + 12)
        tables[tag] = (offset, length)

    head_offset, _ = tables[b"head"]
    hhea_offset, _ = tables[b"hhea"]
    units_per_em = _read_u16(data, head_offset + 18)
    ascender = _read_i16(data, hhea_offset + 4)
    descender = _read_i16(data, hhea_offset + 6)
    line_gap = _read_i16(data, hhea_offset + 8)

    baseline = math.ceil(ascender * size_px / units_per_em)
    line_height = math.ceil((ascender - descender + line_gap) * size_px / units_per_em)
    return max(1, baseline), max(1, line_height)


def paeth(left: int, up: int, upper_left: int) -> int:
    p = left + up - upper_left
    pa = abs(p - left)
    pb = abs(p - up)
    pc = abs(p - upper_left)
    if pa <= pb and pa <= pc:
        return left
    if pb <= pc:
        return up
    return upper_left


def read_png_rgba(path: Path) -> tuple[int, int, list[int]]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")

    pos = 8
    width = 0
    height = 0
    color_type = 0
    compressed = bytearray()
    while pos < len(data):
        length = _read_u32(data, pos)
        chunk_type = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, compression, png_filter, interlace = struct.unpack(
                ">IIBBBBB", chunk
            )
            if bit_depth != 8 or compression != 0 or png_filter != 0 or interlace != 0:
                raise ValueError("only non-interlaced 8-bit PNGs are supported")
        elif chunk_type == b"IDAT":
            compressed.extend(chunk)
        elif chunk_type == b"IEND":
            break

    channels_by_type = {0: 1, 2: 3, 4: 2, 6: 4}
    if color_type not in channels_by_type:
        raise ValueError(f"unsupported PNG color type {color_type}")

    channels = channels_by_type[color_type]
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    rows: list[bytearray] = []
    prev = bytearray(stride)
    idx = 0
    for _ in range(height):
        filter_type = raw[idx]
        idx += 1
        row = bytearray(raw[idx : idx + stride])
        idx += stride
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            up = prev[x]
            upper_left = prev[x - channels] if x >= channels else 0
            if filter_type == 1:
                row[x] = (row[x] + left) & 0xFF
            elif filter_type == 2:
                row[x] = (row[x] + up) & 0xFF
            elif filter_type == 3:
                row[x] = (row[x] + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                row[x] = (row[x] + paeth(left, up, upper_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter {filter_type}")
        rows.append(row)
        prev = row

    rgba: list[int] = []
    for row in rows:
        for x in range(width):
            src = x * channels
            if color_type == 0:
                v = row[src]
                rgba.extend((v, v, v, 255))
            elif color_type == 2:
                rgba.extend((row[src], row[src + 1], row[src + 2], 255))
            elif color_type == 4:
                v = row[src]
                rgba.extend((v, v, v, row[src + 1]))
            elif color_type == 6:
                rgba.extend((row[src], row[src + 1], row[src + 2], row[src + 3]))
    return width, height, rgba


def glyph_advance(font: Path, size_px: int, text: str) -> int:
    result = subprocess.run(
        [
            "hb-shape",
            "--output-format=json",
            f"--font-size={size_px}",
            str(font),
            text,
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    shaped = json.loads(result.stdout)
    if not shaped:
        return max(1, size_px // 2)
    return int(round(shaped[0].get("ax", max(1, size_px // 2))))


def render_glyph(font: Path, size_px: int, codepoint: int, baseline: int) -> Glyph:
    text = chr(codepoint)
    advance_x = glyph_advance(font, size_px, text)
    with tempfile.NamedTemporaryFile(suffix=".png") as tmp:
        subprocess.run(
            [
                "hb-view",
                f"--font-size={size_px}",
                "--margin=0",
                "--logical",
                "--background=00000000",
                "--foreground=ffffffff",
                "-O",
                "png",
                "-o",
                tmp.name,
                str(font),
                text,
            ],
            check=True,
            capture_output=True,
        )
        width, height, rgba = read_png_rgba(Path(tmp.name))

    alpha = [rgba[i + 3] for i in range(0, len(rgba), 4)]
    coords = [(x, y) for y in range(height) for x in range(width) if alpha[y * width + x]]
    if not coords:
        return Glyph(codepoint, 0, 0, 0, 0, 0, 0, advance_x, [])

    min_x = min(x for x, _ in coords)
    min_y = min(y for _, y in coords)
    max_x = max(x for x, _ in coords)
    max_y = max(y for _, y in coords)
    glyph_w = max_x - min_x + 1
    glyph_h = max_y - min_y + 1
    bitmap: list[int] = []
    for y in range(min_y, min_y + glyph_h):
        for x in range(min_x, min_x + glyph_w):
            bitmap.append(alpha[y * width + x])

    return Glyph(
        codepoint=codepoint,
        atlas_x=0,
        atlas_y=0,
        width=glyph_w,
        height=glyph_h,
        bearing_x=min_x,
        bearing_y=baseline - min_y,
        advance_x=advance_x,
        bitmap=bitmap,
    )


def pack_glyphs(glyphs: list[Glyph], atlas_width: int) -> tuple[int, list[int]]:
    x = 0
    y = 0
    row_h = 0
    for glyph in glyphs:
        if glyph.width == 0 or glyph.height == 0:
            continue
        if glyph.width > atlas_width:
            raise ValueError(f"glyph U+{glyph.codepoint:04X} is wider than atlas")
        if x > 0 and x + glyph.width > atlas_width:
            x = 0
            y += row_h + 1
            row_h = 0
        glyph.atlas_x = x
        glyph.atlas_y = y
        x += glyph.width + 1
        row_h = max(row_h, glyph.height)

    atlas_height = max(1, y + row_h)
    atlas = [0] * (atlas_width * atlas_height)
    for glyph in glyphs:
        for row in range(glyph.height):
            dst = (glyph.atlas_y + row) * atlas_width + glyph.atlas_x
            src = row * glyph.width
            atlas[dst : dst + glyph.width] = glyph.bitmap[src : src + glyph.width]
    return atlas_height, atlas


def c_array(values: list[int], indent: str = "    ", per_line: int = 12) -> str:
    lines = []
    for i in range(0, len(values), per_line):
        chunk = values[i : i + per_line]
        lines.append(indent + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def emit_c(
    output: Path,
    symbol: str,
    font_path: Path,
    size_px: int,
    first: int,
    last: int,
    atlas_width: int,
    atlas_height: int,
    atlas: list[int],
    glyphs: list[Glyph],
    baseline: int,
    line_height: int,
) -> None:
    glyph_entries = []
    for glyph in glyphs:
        glyph_entries.append(
            "    "
            f"{{0x{glyph.codepoint:04x}, {glyph.atlas_x}, {glyph.atlas_y}, "
            f"{glyph.width}, {glyph.height}, {glyph.bearing_x}, "
            f"{glyph.bearing_y}, {glyph.advance_x}}},"
        )

    source = f"""\
// Generated by scripts/loom_font_convert.py.
// Source: {font_path}
// Range: U+{first:04X}-U+{last:04X}, size: {size_px}px.

#include \"loom/fonts.h\"

static const uint8_t {symbol}_atlas[] = {{
{c_array(atlas)}
}};

static const loom_glyph_t {symbol}_glyphs[] = {{
{chr(10).join(glyph_entries)}
}};

const loom_font_t {symbol} = {{
    .format = LOOM_FONT_ATLAS_A8,
    .atlas_width = {atlas_width},
    .atlas_height = {atlas_height},
    .atlas_stride = {atlas_width},
    .atlas = {symbol}_atlas,
    .glyphs = {symbol}_glyphs,
    .glyph_count = {len(glyphs)},
    .fallback_codepoint = 0x003f,
    .line_height = {line_height},
    .baseline = {baseline},
    .sdf_px_range = 0,
}};
"""
    output.write_text(source)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path, help="input .ttf/.otf font")
    parser.add_argument("--output", type=Path, required=True, help="output .c file")
    parser.add_argument("--symbol", required=True, help="C symbol for loom_font_t")
    parser.add_argument("--size", type=int, default=18, help="font pixel size")
    parser.add_argument("--first", type=lambda value: int(value, 0), default=32)
    parser.add_argument("--last", type=lambda value: int(value, 0), default=126)
    parser.add_argument("--atlas-width", type=int, default=256)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if shutil.which("hb-view") is None or shutil.which("hb-shape") is None:
        raise SystemExit("hb-view and hb-shape are required; install HarfBuzz tools")
    if args.first > args.last:
        raise SystemExit("--first must be <= --last")

    baseline, line_height = read_font_extents(args.font, args.size)
    glyphs = [
        render_glyph(args.font, args.size, codepoint, baseline)
        for codepoint in range(args.first, args.last + 1)
    ]
    atlas_height, atlas = pack_glyphs(glyphs, args.atlas_width)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    emit_c(
        args.output,
        args.symbol,
        args.font,
        args.size,
        args.first,
        args.last,
        args.atlas_width,
        atlas_height,
        atlas,
        glyphs,
        baseline,
        line_height,
    )


if __name__ == "__main__":
    main()

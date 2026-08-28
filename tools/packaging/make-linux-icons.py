#!/usr/bin/env python3
"""Generate the Linux application icon set for QSanguosha.

The repository only ships a Windows ``.ico`` (32x32/16x16, 8-bit) and a macOS
``.icns`` whose only member is a JPEG 2000 image.  Neither is usable as a Linux
hicolor icon: freedesktop wants PNGs per size plus, ideally, a scalable SVG.

Rather than upscale a 32x32 bitmap, the icon is defined once as geometry here
and emitted twice - as an SVG for ``scalable/apps`` and as anti-aliased PNGs for
the fixed hicolor sizes.  Both outputs are committed; this script exists so the
set can be regenerated identically.

Stdlib only (zlib + struct): the build and CI machines have no PIL, no
librsvg and no ImageMagick.

Usage:
    python3 tools/packaging/make-linux-icons.py [--output-dir resource/icon/linux]
"""

from __future__ import annotations

import argparse
import math
import pathlib
import struct
import sys
import zlib

# The emblem: a crimson rounded tile, a gold ring, and three blades at 120
# degrees for the three kingdoms.  Coordinates are in a 256x256 design space.
DESIGN = 256.0

TILE_RADIUS = 46.0
TILE_INSET = 8.0
TILE_TOP = (0x7A, 0x10, 0x14)
TILE_BOTTOM = (0x40, 0x07, 0x0B)
RING_OUTER = 96.0
RING_INNER = 84.0
GOLD = (0xE8, 0xC0, 0x6A)
GOLD_DEEP = (0xC9, 0x99, 0x3A)
BLADE_INNER = 26.0
BLADE_OUTER = 92.0
BLADE_HALF_WIDTH = 21.0

SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
SUPERSAMPLE = 4


def _blade_polygon(angle_deg: float) -> list[tuple[float, float]]:
    """One tapered blade pointing outwards from the centre."""
    angle = math.radians(angle_deg)
    cos_a, sin_a = math.cos(angle), math.sin(angle)

    def point(along: float, across: float) -> tuple[float, float]:
        x = DESIGN / 2 + along * cos_a - across * sin_a
        y = DESIGN / 2 + along * sin_a + across * cos_a
        return (x, y)

    return [
        point(BLADE_OUTER, 0.0),
        point(BLADE_INNER + 6.0, BLADE_HALF_WIDTH),
        point(BLADE_INNER, 0.0),
        point(BLADE_INNER + 6.0, -BLADE_HALF_WIDTH),
    ]


BLADES = [_blade_polygon(angle) for angle in (-90.0, 30.0, 150.0)]


def _inside_polygon(polygon: list[tuple[float, float]], x: float, y: float) -> bool:
    inside = False
    count = len(polygon)
    for index in range(count):
        x0, y0 = polygon[index]
        x1, y1 = polygon[(index + 1) % count]
        if (y0 > y) != (y1 > y):
            crossing = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
            if crossing > x:
                inside = not inside
    return inside


def _inside_rounded_rect(x: float, y: float) -> bool:
    left = TILE_INSET
    top = TILE_INSET
    right = DESIGN - TILE_INSET
    bottom = DESIGN - TILE_INSET
    if x < left or x > right or y < top or y > bottom:
        return False
    radius = TILE_RADIUS
    cx = min(max(x, left + radius), right - radius)
    cy = min(max(y, top + radius), bottom - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius * radius


def _sample(x: float, y: float) -> tuple[int, int, int, int]:
    """Colour of the design at one point, or a fully transparent pixel."""
    if not _inside_rounded_rect(x, y):
        return (0, 0, 0, 0)

    ratio = (y - TILE_INSET) / (DESIGN - 2 * TILE_INSET)
    ratio = min(max(ratio, 0.0), 1.0)
    colour = tuple(
        int(round(TILE_TOP[channel] + (TILE_BOTTOM[channel] - TILE_TOP[channel]) * ratio))
        for channel in range(3)
    )

    distance = math.hypot(x - DESIGN / 2, y - DESIGN / 2)
    if RING_INNER <= distance <= RING_OUTER:
        return (*GOLD_DEEP, 255)
    for polygon in BLADES:
        if _inside_polygon(polygon, x, y):
            return (*GOLD, 255)
    return (*colour, 255)


def render(size: int) -> bytes:
    """Render one RGBA icon, supersampled for anti-aliasing."""
    scale = DESIGN / size
    step = scale / SUPERSAMPLE
    samples = SUPERSAMPLE * SUPERSAMPLE
    rows = bytearray()
    for row in range(size):
        rows.append(0)  # PNG filter type 0 (None)
        base_y = row * scale
        for column in range(size):
            base_x = column * scale
            red = green = blue = alpha = 0
            for sub_y in range(SUPERSAMPLE):
                y = base_y + (sub_y + 0.5) * step
                for sub_x in range(SUPERSAMPLE):
                    x = base_x + (sub_x + 0.5) * step
                    r, g, b, a = _sample(x, y)
                    # Premultiply so transparent samples do not darken the edge.
                    red += r * a
                    green += g * a
                    blue += b * a
                    alpha += a
            if alpha == 0:
                rows += b"\x00\x00\x00\x00"
                continue
            rows.append(min(255, round(red / alpha)))
            rows.append(min(255, round(green / alpha)))
            rows.append(min(255, round(blue / alpha)))
            rows.append(min(255, round(alpha / samples)))
    return bytes(rows)


def _chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def write_png(path: pathlib.Path, size: int) -> None:
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    data = zlib.compress(render(size), 9)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", data)
        + _chunk(b"IEND", b"")
    )


def svg() -> str:
    blades = "\n".join(
        '    <polygon points="{}" fill="#{:02X}{:02X}{:02X}"/>'.format(
            " ".join(f"{x:.2f},{y:.2f}" for x, y in polygon), *GOLD
        )
        for polygon in BLADES
    )
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- Generated by tools/packaging/make-linux-icons.py - edit that script, not this file. -->
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <defs>
    <linearGradient id="tile" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#{TILE_TOP[0]:02X}{TILE_TOP[1]:02X}{TILE_TOP[2]:02X}"/>
      <stop offset="1" stop-color="#{TILE_BOTTOM[0]:02X}{TILE_BOTTOM[1]:02X}{TILE_BOTTOM[2]:02X}"/>
    </linearGradient>
  </defs>
  <g>
    <rect x="{TILE_INSET}" y="{TILE_INSET}"
          width="{DESIGN - 2 * TILE_INSET}" height="{DESIGN - 2 * TILE_INSET}"
          rx="{TILE_RADIUS}" ry="{TILE_RADIUS}" fill="url(#tile)"/>
    <circle cx="{DESIGN / 2}" cy="{DESIGN / 2}" r="{(RING_OUTER + RING_INNER) / 2}"
            fill="none" stroke="#{GOLD_DEEP[0]:02X}{GOLD_DEEP[1]:02X}{GOLD_DEEP[2]:02X}"
            stroke-width="{RING_OUTER - RING_INNER}"/>
{blades}
  </g>
</svg>
"""


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        default="resource/icon/linux",
        type=pathlib.Path,
        help="directory to write qsanguosha.svg and the PNG sizes into",
    )
    arguments = parser.parse_args(argv)
    output = arguments.output_dir
    output.mkdir(parents=True, exist_ok=True)

    (output / "qsanguosha.svg").write_text(svg(), encoding="utf-8")
    print(f"wrote {output / 'qsanguosha.svg'}")
    for size in SIZES:
        path = output / f"qsanguosha-{size}.png"
        write_png(path, size)
        print(f"wrote {path} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

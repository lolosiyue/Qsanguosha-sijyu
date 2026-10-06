#!/usr/bin/env python3
"""Generate every platform icon for QSanguosha from one transparent master.

Source: ``resource/icon/qsanguosha.png`` (1024x1024 RGBA).  Outputs, all
committed so the build itself never needs this script:

    resource/icon/sgs.ico                     Windows executables (resource/icon.rc)
    resource/icon/sgs.icns                    macOS
    resource/icon/linux/qsanguosha-<n>.png    Linux hicolor sizes
    resource/android/res/drawable/icon.png    Android launcher
    web/public/favicon-<n>.png                Web client (web/index.html)

The .ico keeps every size below 256 as a 32-bit BMP entry and only 256x256 as
PNG: Windows XP (QSanguoshaXP) cannot decode PNG icon entries.

Requires Pillow (developer machines only).

Usage:
    python tools/packaging/make-icons.py
"""

from __future__ import annotations

import io
import pathlib
import struct
import sys

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
MASTER = ROOT / "resource" / "icon" / "qsanguosha.png"

ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
LINUX_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
ANDROID_SIZE = 256
# PNG rather than .ico: the portable web launcher serves .ico as octet-stream.
WEB_SIZES = (32, 192)


def scaled(master: Image.Image, size: int) -> Image.Image:
    # Pillow resamples RGBA in premultiplied form, so edges do not darken.
    return master.resize((size, size), Image.Resampling.LANCZOS)


def ico_bmp_entry(image: Image.Image) -> bytes:
    """32-bit DIB with an AND mask, bottom-up, as ICO entries require."""
    width, height = image.size
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    rows = [image.crop((0, y, width, y + 1)) for y in range(height - 1, -1, -1)]
    xor = b"".join(row.tobytes("raw", "BGRA") for row in rows)
    mask_stride = ((width + 31) // 32) * 4
    mask = bytearray()
    for row in rows:
        bits = bytearray(mask_stride)
        for x, alpha in enumerate(row.getchannel("A").tobytes()):
            if alpha == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    return header + xor + bytes(mask)


def write_ico(master: Image.Image, path: pathlib.Path) -> None:
    entries = []
    for size in ICO_SIZES:
        image = scaled(master, size)
        if size >= 256:
            buffer = io.BytesIO()
            image.save(buffer, "PNG", optimize=True)
            entries.append((size, buffer.getvalue()))
        else:
            entries.append((size, ico_bmp_entry(image)))
    data = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    for size, payload in entries:
        dim = size if size < 256 else 0
        data += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(payload), offset)
        offset += len(payload)
    data += b"".join(payload for _, payload in entries)
    path.write_bytes(data)


def main() -> int:
    master = Image.open(MASTER).convert("RGBA")
    if master.size != (1024, 1024):
        print(f"{MASTER} must be 1024x1024, got {master.size}", file=sys.stderr)
        return 1

    outputs = []
    ico = ROOT / "resource" / "icon" / "sgs.ico"
    write_ico(master, ico)
    outputs.append(ico)

    icns = ROOT / "resource" / "icon" / "sgs.icns"
    master.save(icns, "ICNS")
    outputs.append(icns)

    for size in LINUX_SIZES:
        path = ROOT / "resource" / "icon" / "linux" / f"qsanguosha-{size}.png"
        scaled(master, size).save(path, "PNG", optimize=True)
        outputs.append(path)

    android = ROOT / "resource" / "android" / "res" / "drawable" / "icon.png"
    scaled(master, ANDROID_SIZE).save(android, "PNG", optimize=True)
    outputs.append(android)

    for size in WEB_SIZES:
        path = ROOT / "web" / "public" / f"favicon-{size}.png"
        scaled(master, size).save(path, "PNG", optimize=True)
        outputs.append(path)

    for path in outputs:
        print(f"wrote {path.relative_to(ROOT)} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

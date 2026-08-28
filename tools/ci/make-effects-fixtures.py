#!/usr/bin/env python3
"""Generate the tiny visual fixtures used by the M2B-B effects smoke.

Nothing under tests/fixtures/effects/ may be a real game asset: the production
art set is large and copyrighted. Everything this script writes is synthetic and
a few hundred bytes, produced with the standard library only (no Pillow, no
external encoder), so a clean checkout can regenerate it on any runner.

What is produced, and what each file is for:

  animated.gif      2 frames, 4x4 - the GIF that must play under FULL and show
                    only its first frame under REDUCED.
  single-frame.gif  1 frame     - a still GIF; must never start a frame timer.
  truncated.gif     a valid header with the pixel data cut off - QMovie must
                    report it invalid and the caller must fall back to a static
                    image rather than leaving an empty character frame.
  not-a.gif         random bytes with a .gif name - the "malformed asset" case.
  emotion/smoke/N.png  numbered frames for PixmapAnimation (image/system/emotion
                    layout), so the frame-animation path can be exercised
                    without the real emotion art.
  spine/broken/     a Spine asset directory whose .atlas/.json are deliberately
                    malformed. There is no synthetic *valid* Spine fixture on
                    purpose - see tests/fixtures/effects/README.md.

Usage: python3 tools/ci/make-effects-fixtures.py [output-dir]
"""

import struct
import sys
import zlib
from pathlib import Path


# ── GIF ──────────────────────────────────────────────────────────────────────

def _lzw_encode(indices, min_code_size):
    """Minimal GIF-flavoured LZW encoder (LSB-first, variable code width)."""
    clear_code = 1 << min_code_size
    end_code = clear_code + 1
    code_size = min_code_size + 1
    next_code = end_code + 1
    table = {(i,): i for i in range(clear_code)}

    out = bytearray()
    bit_buffer = 0
    bit_count = 0

    def emit(code):
        nonlocal bit_buffer, bit_count
        bit_buffer |= code << bit_count
        bit_count += code_size
        while bit_count >= 8:
            out.append(bit_buffer & 0xFF)
            bit_buffer >>= 8
            bit_count -= 8

    emit(clear_code)
    prefix = ()
    for index in indices:
        candidate = prefix + (index,)
        if candidate in table:
            prefix = candidate
            continue
        emit(table[prefix])
        table[candidate] = next_code
        next_code += 1
        if next_code > (1 << code_size) and code_size < 12:
            code_size += 1
        prefix = (index,)
    if prefix:
        emit(table[prefix])
    emit(end_code)

    if bit_count:
        out.append(bit_buffer & 0xFF)

    # split into sub-blocks of at most 255 bytes
    blocks = bytearray()
    for start in range(0, len(out), 255):
        chunk = out[start:start + 255]
        blocks.append(len(chunk))
        blocks += chunk
    blocks.append(0)
    return bytes(blocks)


def make_gif(width, height, frames, delay_cs=10, loop=True):
    """frames: list of flat palette-index lists (len == width * height)."""
    palette = bytes((0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF))
    # packed: global colour table, colour resolution 1, table size 2^(1+1) = 4
    packed = 0x80 | 0x01
    data = bytearray(b"GIF89a")
    data += struct.pack("<HHBBB", width, height, packed, 0, 0)
    data += palette

    if loop and len(frames) > 1:
        data += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00"

    for indices in frames:
        assert len(indices) == width * height
        # graphic control extension: no disposal, no transparency
        data += b"\x21\xF9\x04\x00" + struct.pack("<H", delay_cs) + b"\x00\x00"
        # image descriptor
        data += b"\x2C" + struct.pack("<HHHHB", 0, 0, width, height, 0x00)
        data += bytes([2])                      # LZW minimum code size
        data += _lzw_encode(indices, 2)

    data += b"\x3B"
    return bytes(data)


# ── PNG ──────────────────────────────────────────────────────────────────────

def make_png(width, height, rgb):
    """Solid-colour RGB PNG."""
    raw = bytearray()
    for _ in range(height):
        raw.append(0)                            # filter type: none
        raw += bytes(rgb) * width

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("tests/fixtures/effects")
    root.mkdir(parents=True, exist_ok=True)

    checker = [0, 1, 1, 0,
               1, 0, 0, 1,
               1, 0, 0, 1,
               0, 1, 1, 0]
    inverted = [1 - value for value in checker]

    animated = make_gif(4, 4, [checker, inverted])
    (root / "animated.gif").write_bytes(animated)
    (root / "single-frame.gif").write_bytes(make_gif(4, 4, [checker], loop=False))
    # Cut the file where the pixel data would be: the header parses, the frame
    # does not. QMovie reports isValid() == false.
    (root / "truncated.gif").write_bytes(animated[:20])
    (root / "not-a.gif").write_bytes(b"this is not a GIF, it just claims to be one\n")

    emotion = root / "emotion" / "smoke"
    emotion.mkdir(parents=True, exist_ok=True)
    for index, colour in enumerate([(220, 60, 60), (60, 220, 60), (60, 60, 220)]):
        (emotion / f"{index}.png").write_bytes(make_png(8, 8, colour))

    spine = root / "spine" / "broken"
    spine.mkdir(parents=True, exist_ok=True)
    (spine / "broken.atlas").write_text(
        "this file is deliberately not a Spine atlas\n", encoding="utf-8")
    (spine / "broken.json").write_text(
        '{"this": "is deliberately not a Spine skeleton"\n', encoding="utf-8")

    for path in sorted(root.rglob("*")):
        if path.is_file():
            print(f"{path} ({path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()

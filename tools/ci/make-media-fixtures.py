#!/usr/bin/env python3
"""Generate the tiny audio fixtures used by the multimedia smoke.

The fixtures are produced here rather than downloaded or copied out of the
game's asset tree: nothing in tests/fixtures/media/ may be a real (large,
copyrighted) game asset. Everything this script writes is a synthetic sine
tone of a few kilobytes.

Usage: python3 tools/ci/make-media-fixtures.py [output-dir]
"""

import math
import struct
import sys
from pathlib import Path

SAMPLE_RATE = 8000
CHANNELS = 1
BITS = 16


def sine_wav(seconds: float, frequency: float, amplitude: float = 0.2) -> bytes:
    frames = int(SAMPLE_RATE * seconds)
    samples = bytearray()
    for i in range(frames):
        value = amplitude * math.sin(2.0 * math.pi * frequency * i / SAMPLE_RATE)
        samples += struct.pack("<h", int(max(-1.0, min(1.0, value)) * 32767))

    byte_rate = SAMPLE_RATE * CHANNELS * BITS // 8
    block_align = CHANNELS * BITS // 8
    fmt_chunk = struct.pack(
        "<4sIHHIIHH", b"fmt ", 16, 1, CHANNELS, SAMPLE_RATE, byte_rate, block_align, BITS
    )
    data_chunk = struct.pack("<4sI", b"data", len(samples)) + bytes(samples)
    riff_size = 4 + len(fmt_chunk) + len(data_chunk)
    return struct.pack("<4sI4s", b"RIFF", riff_size, b"WAVE") + fmt_chunk + data_chunk


# button-down 的檔名唔係求其改：classifyAudioFile() 靠 basename 認短 UI 音效，
# 所以呢個 fixture 一定要叫呢個名先會行 QSoundEffect 嗰條路。
FIXTURES = {
    "button-down.wav": (0.08, 880.0),
    "voice-line.wav": (0.15, 440.0),
    "bgm-loop.wav": (0.30, 220.0),
}


def main() -> int:
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/media")
    out.mkdir(parents=True, exist_ok=True)
    for name, (seconds, frequency) in FIXTURES.items():
        path = out / name
        path.write_bytes(sine_wav(seconds, frequency))
        print(f"{path} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

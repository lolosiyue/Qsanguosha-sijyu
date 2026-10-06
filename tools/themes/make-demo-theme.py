#!/usr/bin/env python3
"""Regenerate the art of the demo theme packs themes/demo-crimson and themes/demo-jade.

The art is original and procedural; no game asset is copied.
Usage: python3 tools/themes/make-demo-theme.py [themes-folder]
Needs Pillow. The theme.json files are hand-written and left untouched.
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

CRIMSON = (122, 18, 32)
DEEP = (46, 8, 16)
GOLD = (222, 178, 92)
PALE_GOLD = (246, 222, 160)
JADE = (24, 104, 84)
JADE_DEEP = (8, 40, 34)
SILVER = (200, 220, 214)


def vertical_gradient(size: tuple[int, int], top: tuple[int, ...], bottom: tuple[int, ...]) -> Image.Image:
    width, height = size
    image = Image.new("RGBA", size)
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / max(1, height - 1)
        color = tuple(round(a + (b - a) * t) for a, b in zip(top, bottom))
        draw.line([(0, y), (width, y)], fill=color)
    return image


def framed_panel(size: tuple[int, int], alpha: int = 235, radius: int = 10, border: int = 3,
                 top: tuple[int, ...] = CRIMSON, bottom: tuple[int, ...] = DEEP,
                 edge: tuple[int, ...] = GOLD) -> Image.Image:
    width, height = size
    panel = vertical_gradient(size, top + (alpha,), bottom + (alpha,))
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, width - 1, height - 1], radius, fill=255)
    result = Image.new("RGBA", size, (0, 0, 0, 0))
    result.paste(panel, (0, 0), mask)
    draw = ImageDraw.Draw(result)
    draw.rounded_rectangle([1, 1, width - 2, height - 2], radius, outline=edge + (255,), width=border)
    inset = border + 4
    draw.rounded_rectangle([inset, inset, width - 1 - inset, height - 1 - inset], max(1, radius - 4),
                           outline=edge + (110,), width=1)
    return result


def lattice(image: Image.Image, step: int, color: tuple[int, ...]) -> None:
    draw = ImageDraw.Draw(image)
    width, height = image.size
    for offset in range(-height, width + height, step):
        draw.line([(offset, 0), (offset + height, height)], fill=color, width=1)
        draw.line([(offset + height, 0), (offset, height)], fill=color, width=1)


def card_back() -> Image.Image:
    size = (93, 130)
    card = framed_panel(size, alpha=255, radius=6, border=3)
    pattern = Image.new("RGBA", size, (0, 0, 0, 0))
    lattice(pattern, 12, GOLD + (70,))
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([9, 9, size[0] - 10, size[1] - 10], 4, fill=255)
    card.paste(pattern, (0, 0), Image.composite(pattern, Image.new("RGBA", size), mask))
    draw = ImageDraw.Draw(card)
    cx, cy = size[0] // 2, size[1] // 2
    draw.regular_polygon((cx, cy, 22), 4, rotation=45, fill=DEEP + (255,), outline=GOLD + (255,), width=2)
    draw.regular_polygon((cx, cy, 11), 4, rotation=45, fill=GOLD + (255,))
    return card


def avatar_frame() -> Image.Image:
    # The general's portrait is drawn under this frame inside layout "avatarArea"
    # [3, 3, 165, 191], so that window must stay transparent.
    frame = framed_panel((171, 197), alpha=240, radius=8)
    window = Image.new("L", frame.size, 255)
    ImageDraw.Draw(window).rounded_rectangle([5, 5, 165, 191], 6, fill=0)
    frame.putalpha(Image.composite(frame.getchannel("A"), Image.new("L", frame.size, 0), window))
    ImageDraw.Draw(frame).rounded_rectangle([4, 4, 166, 192], 6, outline=GOLD + (200,), width=1)
    return frame


def log_border() -> Image.Image:
    # Nine-slice source: the game cuts 10px from each edge.
    return framed_panel((40, 40), alpha=170, radius=8, border=2)


def indicator_line() -> Image.Image:
    width, height = 256, 16
    line = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pixels = line.load()
    for x in range(width):
        t = x / (width - 1)
        color = tuple(round(a + (b - a) * t) for a, b in zip(PALE_GOLD, (230, 40, 40)))
        for y in range(height):
            distance = abs(y - (height - 1) / 2) / (height / 2)
            alpha = max(0.0, 1 - distance ** 1.6) * min(1.0, t * 4 + 0.1)
            pixels[x, y] = color + (round(255 * alpha),)
    return line


def emotion_frames(count: int = 8, size: int = 120) -> list[Image.Image]:
    frames = []
    for i in range(count):
        t = i / (count - 1)
        frame = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(frame)
        radius = 12 + t * (size / 2 - 16)
        alpha = round(255 * (1 - t) ** 0.7)
        c = size / 2
        draw.ellipse([c - radius, c - radius, c + radius, c + radius], outline=GOLD + (alpha,), width=5)
        inner = radius * 0.55
        for k in range(8):
            angle = k * math.pi / 4 + t
            x, y = c + math.cos(angle) * inner, c + math.sin(angle) * inner
            draw.ellipse([x - 4, y - 4, x + 4, y + 4], fill=(240, 70, 70, alpha))
        frames.append(frame.filter(ImageFilter.GaussianBlur(0.6)))
    return frames


def preview(parts: dict[str, Image.Image]) -> Image.Image:
    canvas = vertical_gradient((256, 144), (30, 20, 24, 255), (12, 8, 10, 255))
    canvas.alpha_composite(parts["container"].resize((150, 72)), (8, 10))
    canvas.alpha_composite(parts["card"].resize((52, 73)), (176, 8))
    canvas.alpha_composite(parts["hand"].resize((240, 40)), (8, 96))
    canvas.alpha_composite(parts["line"].resize((150, 10)), (8, 86))
    return canvas


def save_all(output: Path, images: dict[str, Image.Image]) -> None:
    for name, image in images.items():
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        image.save(target, optimize=True)


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    themes = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "themes"

    crimson = {
        "card": card_back(),
        "container": framed_panel((716, 346), alpha=225, radius=12, border=4),
        "equip": framed_panel((164, 170), alpha=240, radius=8),
        "hand": framed_panel((746, 170), alpha=200, radius=8),
        "avatar": avatar_frame(),
        "log": log_border(),
        "line": indicator_line(),
    }
    files = {
        "card-back.png": crimson["card"],
        "card-container.png": crimson["container"],
        "dashboard/equip.png": crimson["equip"],
        "dashboard/hand.png": crimson["hand"],
        "dashboard/avatar.png": crimson["avatar"],
        "log-border.png": crimson["log"],
        "indicator-line.png": crimson["line"],
        "preview.png": preview(crimson),
    }
    for index, frame in enumerate(emotion_frames()):
        files[f"emotion/peach/{index}.png"] = frame
    save_all(themes / "demo-crimson", files)

    # A second, partial pack to show stacking: only two slots, everything else falls through.
    jade = dict(crimson)
    jade["container"] = framed_panel((716, 346), alpha=225, radius=12, border=4,
                                     top=JADE, bottom=JADE_DEEP, edge=SILVER)
    jade["log"] = framed_panel((40, 40), alpha=170, radius=8, border=2,
                               top=JADE, bottom=JADE_DEEP, edge=SILVER)
    jade_preview = vertical_gradient((256, 144), (16, 30, 26, 255), (6, 12, 10, 255))
    jade_preview.alpha_composite(jade["container"].resize((200, 97)), (28, 24))
    save_all(themes / "demo-jade", {
        "card-container.png": jade["container"],
        "log-border.png": jade["log"],
        "preview.png": jade_preview,
    })
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

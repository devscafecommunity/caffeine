#!/usr/bin/env python3
"""Composes the material-slots documentation sheet from what the env-gated Rust test `material_slots_pictures_for_docs`
wrote: one two-group rock with its slots in both orders, and a Nature model placed merged (entity counts caption).
Needs Pillow."""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def font(size):
    for name in ("DejaVuSans.ttf", "NotoSans-Regular.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def label(image, text, size=13):
    draw = ImageDraw.Draw(image, "RGBA")
    f = font(size)
    w = draw.textlength(text, font=f) + 14
    draw.rectangle([8, 8, 8 + w, 8 + size + 14], fill=(12, 14, 18, 215))
    draw.text((15, 14), text, fill=(235, 238, 245), font=f)


def main(src, dst):
    src, dst = Path(src), Path(dst)
    dst.mkdir(parents=True, exist_ok=True)
    rows = dict(line.split("\t")[:2] for line in (src / "nature.txt").read_text().splitlines() if "\t" in line)
    panels = [
        ("rock_moss_north.png", "Material slots: moss;stone"),
        ("rock_moss_south.png", "Material slots: stone;moss"),
        ("nature_merged.png", f"Pink Flower: {rows.get('separate', '?')} entities -> {rows.get('merged', '?')} (Merge parts)"),
    ]
    sheet = Image.new("RGB", (480 * 3, 360), (18, 20, 24))
    for i, (name, text) in enumerate(panels):
        image = Image.open(src / name).convert("RGB")
        label(image, text)
        sheet.paste(image, (i * 480, 0))
    sheet.save(dst / "material-slots.png", optimize=True)
    print(rows)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: compose_material_slots_docs.py PICTURES_DIR OUT_DIR")
    main(sys.argv[1], sys.argv[2])

#!/usr/bin/env python3
"""Composes the terrain-mask sheet from the pictures the env-gated Rust test `terrain_mask_pictures_for_docs`
wrote (mask_<name>.png, terrain.png): the lit terrain and every mask, labelled. Needs Pillow."""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ORDER = [
    ("height", "height (built-in)"),
    ("slope", "slope (built-in)"),
    ("curvature", "curvature (built-in)"),
    ("flow", "flow (generator)"),
    ("wetness", "wetness (generator)"),
    ("wear", "wear (erosion)"),
    ("deposit", "deposit (erosion)"),
]


def font(size):
    for name in ("DejaVuSans.ttf", "NotoSans-Regular.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def main(src, dst):
    src, dst = Path(src), Path(dst)
    dst.mkdir(parents=True, exist_ok=True)
    size = 256
    tiles = [("lit terrain", Image.open(src / "terrain.png").convert("RGB").resize((size, size), Image.LANCZOS))]
    for key, title in ORDER:
        tiles.append((title, Image.open(src / f"mask_{key}.png").convert("RGB").resize((size, size), Image.NEAREST)))
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * size, rows * size), (18, 20, 24))
    f = font(14)
    for i, (title, image) in enumerate(tiles):
        draw = ImageDraw.Draw(image, "RGBA")
        width = draw.textlength(title, font=f) + 14
        draw.rectangle([6, 6, 6 + width, 32], fill=(12, 14, 18, 215))
        draw.text((13, 10), title, fill=(235, 238, 245), font=f)
        sheet.paste(image, ((i % cols) * size, (i // cols) * size))
    sheet.save(dst / "terrain-masks.png", optimize=True)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: compose_terrain_mask_docs.py PICTURES_DIR OUT_DIR")
    main(sys.argv[1], sys.argv[2])

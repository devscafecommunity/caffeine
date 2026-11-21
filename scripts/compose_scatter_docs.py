#!/usr/bin/env python3
"""Composes the Scatter documentation sheet from what the env-gated Rust test `scatter_pictures_for_docs` wrote: two
lit views of the scattered terrain and, over the slope, wetness and height masks, a dot for every instance of the class
that reads that mask (boulders on slope, pines on wetness, tufts on slope). Needs Pillow."""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

COLORS = {"boulder": (235, 90, 60), "pine": (70, 220, 110), "tuft": (250, 215, 80)}


def font(size):
    for name in ("DejaVuSans.ttf", "NotoSans-Regular.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def label(image, text, xy=(8, 8), size=14):
    draw = ImageDraw.Draw(image, "RGBA")
    f = font(size)
    w = draw.textlength(text, font=f) + 14
    draw.rectangle([xy[0], xy[1], xy[0] + w, xy[1] + size + 14], fill=(12, 14, 18, 215))
    draw.text((xy[0] + 7, xy[1] + 6), text, fill=(235, 238, 245), font=f)


def main(src, dst):
    src, dst = Path(src), Path(dst)
    dst.mkdir(parents=True, exist_ok=True)
    data = json.loads((src / "scatter.json").read_text())
    instances = data["instances"]
    size = 320
    panels = []
    for mask, classes, title in (
        ("slope", ("boulder", "tuft"), "slope: boulders (red) on steep, tufts (yellow) off rock"),
        ("wetness", ("pine",), "wetness: pines (green) stay out of the wet"),
        ("height", ("pine", "boulder"), "height: pines (green) below the bare summits"),
    ):
        image = Image.open(src / f"mask_{mask}.png").convert("RGB").resize((size, size), Image.NEAREST)
        draw = ImageDraw.Draw(image)
        for item in instances:
            if item["class"] in classes:
                x, y = item["u"] * size, item["v"] * size
                r = 1.6 if item["class"] == "tuft" else 2.4
                draw.ellipse([x - r, y - r, x + r, y + r], fill=COLORS[item["class"]])
        label(image, title, size=11)
        panels.append(image)
    views = [Image.open(src / f"view_{n}.png").convert("RGB").resize((480, 300), Image.LANCZOS) for n in ("aerial", "close")]
    for v, t in zip(views, ("aerial view", "close view")):
        label(v, t)
    sheet = Image.new("RGB", (960, 300 + size), (18, 20, 24))
    sheet.paste(views[0], (0, 0))
    sheet.paste(views[1], (480, 0))
    for i, p in enumerate(panels):
        sheet.paste(p, (i * 320, 300))
    sheet.save(dst / "scatter-on-terrain.png", optimize=True)
    counts = {}
    for item in instances:
        counts[item["class"]] = counts.get(item["class"], 0) + 1
    print(data["status"])
    print(counts)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: compose_scatter_docs.py PICTURES_DIR OUT_DIR")
    main(sys.argv[1], sys.argv[2])

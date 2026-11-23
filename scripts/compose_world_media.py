#!/usr/bin/env python3
"""Composes the water and nature media for the Doppio docs from engine renders.

usage: compose_world_media.py FRAMES_DIR DOCS_IMAGES_DIR
FRAMES_DIR holds storm/storm_*.png, oak/oak_*.png and presets/*.png (written by the env-gated Rust tests
`water_animation_frames_for_docs`, `nature_wind_frames_for_docs` and `water_presets_render_in_the_engine`).
Writes water/water-presets.png, water/water-storm.gif and nature/nature-oak-wind.gif. Needs Pillow and ffmpeg.
"""
import subprocess
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


def tag(image, text, size=15):
    draw = ImageDraw.Draw(image, "RGBA")
    f = font(size)
    w = draw.textlength(text, font=f) + 16
    draw.rectangle([8, 8, 8 + w, 8 + size * 1.5 + 8], fill=(12, 14, 18, 215))
    draw.text((16, 12), text, fill=(235, 238, 245), font=f)


def gif(frames_dir, pattern, out, fps, size, caption):
    work = out.parent / "_gif"
    work.mkdir(exist_ok=True)
    files = sorted(frames_dir.glob(pattern))
    for i, file in enumerate(files):
        image = Image.open(file).convert("RGB").resize(size, Image.LANCZOS)
        if caption:
            tag(image, caption, 14)
        image.save(work / f"f{i:03d}.png")
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps), "-i", str(work / "f%03d.png"),
         "-vf", "split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=3",
         "-loop", "0", str(out)],
        check=True,
    )
    for f in work.glob("*.png"):
        f.unlink()
    work.rmdir()


def main(src, dst):
    src, dst = Path(src), Path(dst)
    (dst / "water").mkdir(parents=True, exist_ok=True)
    (dst / "nature").mkdir(parents=True, exist_ok=True)

    order = ["open_sea", "storm", "calm_lake", "river", "tropical_lagoon", "swimming_pool", "pond", "swamp"]
    tiles = []
    for name in order:
        image = Image.open(src / "presets" / f"{name}.png").convert("RGB").resize((480, 270), Image.LANCZOS)
        tag(image, name.replace("_", " "))
        tiles.append(image)
    sheet = Image.new("RGB", (960, 270 * 4))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, ((i % 2) * 480, (i // 2) * 270))
    sheet.save(dst / "water" / "water-presets.png", optimize=True)

    gif(src / "storm", "storm_*.png", dst / "water" / "water-storm.gif", 14, (480, 270), "Storm: waves on the GPU")
    gif(src / "oak", "oak_*.png", dst / "nature" / "nature-oak-wind.gif", 14, (480, 270), "Oak in the wind (time-lapse)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])

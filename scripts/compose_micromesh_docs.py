#!/usr/bin/env python3
"""Composes the Micromesh documentation media from the frames the hidden C++ test wrote.

Input: a directory with cut_*.png, dolly_*.png and stats.json, and/or field_*.png, still_*.png and
field.json (see tests/test_micromesh_docs.cpp).
Output (into the second argument): micromesh-cuts.png (four budgets side by side with their triangle
counts), micromesh-dolly.gif (the cut coarsening as the camera pulls away, with live counters),
micromesh-field.gif (a flight over 196 instanced rocks), micromesh-field.png (stills with the
cluster debug view) and micromesh-normals.png (carried against facet normals at four cuts) and micromesh-uv.png (UVs
through the cuts: bump map and checker), and, from the scene renderer test, micromesh-scene.gif (a flight
over 81 lit, shadowed rocks) and micromesh-scene.png (the same aerial view with plain meshes and with the cut). Needs Pillow and ffmpeg.
"""
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def font(size):
    for name in ("DejaVuSans.ttf", "Noto Sans-Regular.ttf", "NotoSans-Regular.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def label(draw, xy, lines, size=15):
    f = font(size)
    x, y = xy
    width = max(draw.textlength(line, font=f) for line in lines) + 14
    draw.rectangle([x, y, x + width, y + size * len(lines) * 1.35 + 10], fill=(12, 14, 18, 215))
    for i, line in enumerate(lines):
        draw.text((x + 7, y + 5 + i * size * 1.35), line, fill=(235, 238, 245), font=f)


def compose_field(src, dst):
    stats = json.loads((src / "field.json").read_text())
    frames = stats["frames"]
    rocks = stats["instances"]
    full = rocks * stats["sourceTriangles"]

    def counters(info):
        ratio = info["exactTriangles"] / max(info["triangles"], 1)
        return [
            f"{info['triangles']:,} triangles drawn",
            f"{info['exactTriangles']:,} with the exact mesh ({ratio:.0f}x)",
            f"{rocks} rocks x {stats['sourceTriangles']:,} = {full:,} in the scene",
        ]

    work = dst / "_field"
    work.mkdir(exist_ok=True)
    keys = sorted(k for k in frames if k.startswith("field_"))
    for i, key in enumerate(keys):
        image = Image.open(src / f"{key}.png").convert("RGB")
        label(ImageDraw.Draw(image, "RGBA"), (10, 10), counters(frames[key]), 15)
        image.resize((480, 270), Image.LANCZOS).save(work / f"f{i:03d}.png")
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-framerate", "20", "-i", str(work / "f%03d.png"),
         "-vf", "split[a][b];[a]palettegen=max_colors=112[p];[b][p]paletteuse=dither=bayer:bayer_scale=4",
         "-loop", "0", str(dst / "micromesh-field.gif")],
        check=True,
    )
    for f in work.glob("*.png"):
        f.unlink()
    work.rmdir()

    # Stills: a low view, the same field from above, its cluster debug view, and the exact mesh counters.
    tiles = []
    for name, title in (("field_000", "Low flight, 2 px error"), ("field_040", "Closer, 2 px error"),
                        ("still_shaded", "From above, 2 px error"), ("still_clusters", "Same view, one colour per cluster")):
        image = Image.open(src / f"{name}.png").convert("RGB")
        info = frames[name]
        label(ImageDraw.Draw(image, "RGBA"), (8, 8), [title, f"{info['triangles']:,} triangles, {info['clusters']:,} clusters"], 14)
        tiles.append(image.resize((560, 315), Image.LANCZOS))
    sheet = Image.new("RGB", (1120, 630))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, ((i % 2) * 560, (i // 2) * 315))
    sheet.save(dst / "micromesh-field.png", optimize=True)


def compose_normals(src, dst):
    stats = json.loads((src / "normals.json").read_text())
    cuts = stats["cuts"]
    size = 300
    sheet = Image.new("RGB", (size * len(cuts), size * 2))
    for col, cut in enumerate(cuts):
        for row, kind in enumerate(("smooth", "facet")):
            image = Image.open(src / f"normals_{kind}_{col}.png").convert("RGB")
            w, h = image.size
            image = image.crop((int(w * 0.1), int(h * 0.08), int(w * 0.9), int(h * 0.92))).resize((size, size), Image.LANCZOS)
            draw = ImageDraw.Draw(image, "RGBA")
            name = "carried normals" if kind == "smooth" else "facet normals"
            error = cut["smoothError"] if kind == "smooth" else cut["facetError"]
            lines = [name, f"{cut['triangles']:,} triangles"]
            lines.append("reference" if cut["threshold"] == 0 else f"error vs exact: {error:.1f} / 255")
            label(draw, (6, 6), lines, 13)
            sheet.paste(image, (col * size, row * size))
    sheet.save(dst / "micromesh-normals.png", optimize=True)


def compose_uv(src, dst):
    stats = json.loads((src / "uv.json").read_text())
    cuts = stats["cuts"]
    size = 270
    rows = (("plain", "carried normals only"), ("bump", "+ bump map from the UVs"), ("checker", "UV checker (seam in blue)"))
    sheet = Image.new("RGB", (size * len(cuts), size * len(rows)))
    for col, cut in enumerate(cuts):
        for row, (kind, title) in enumerate(rows):
            image = Image.open(src / f"uv_{kind}_{col}.png").convert("RGB")
            w, h = image.size
            image = image.crop((int(w * 0.12), int(h * 0.1), int(w * 0.88), int(h * 0.86))).resize((size, size), Image.LANCZOS)
            lines = [title, f"{cut['triangles']:,} triangles"]
            if cut["threshold"] > 0:
                error = {"plain": cut["plainError"], "bump": cut["bumpError"], "checker": cut["checkerError"]}[kind]
                lines.append(f"error vs exact: {error:.1f} / 255")
            else:
                lines.append("reference")
            label(ImageDraw.Draw(image, "RGBA"), (5, 5), lines, 12)
            sheet.paste(image, (col * size, row * size))
    sheet.save(dst / "micromesh-uv.png", optimize=True)


def compose_scene(src, dst):
    stats = json.loads((src / "scene.json").read_text())
    frames = stats["frames"]
    rocks, per_rock = stats["rocks"], stats["rockTriangles"]

    def counters(f):
        ratio = f["plainTriangles"] / max(f["triangles"], 1)
        return [
            f"{f['triangles']:,} triangles drawn ({f['draws']} rocks cut)",
            f"{f['plainTriangles']:,} with the plain meshes ({ratio:.1f}x)",
            f"{rocks} rocks x {per_rock:,} triangles, lit, sun shadows cut too",
        ]

    work = dst / "_scene"
    work.mkdir(exist_ok=True)
    keys = sorted(Path(p).stem for p in src.glob("scene_*.png"))
    for i, key in enumerate(keys):
        image = Image.open(src / f"{key}.png").convert("RGB")
        label(ImageDraw.Draw(image, "RGBA"), (10, 10), counters(frames[i]), 14)
        image.resize((560, 315), Image.LANCZOS).save(work / f"f{i:03d}.png")
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-framerate", "18", "-i", str(work / "f%03d.png"),
         "-vf", "split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=4",
         "-loop", "0", str(dst / "micromesh-scene.gif")],
        check=True,
    )
    for f in work.glob("*.png"):
        f.unlink()
    work.rmdir()

    aerial = stats["aerial"]
    tiles = []
    for name, title, lines in (
        ("still_off", "Plain meshes", [f"{aerial['source']:,} triangles submitted"]),
        ("still_on", "Micromesh cut (1.5 px error)",
         [f"{aerial['triangles']:,} triangles ({aerial['source'] / max(aerial['triangles'], 1):.1f}x fewer)",
          f"mean difference {aerial['meanDifference']:.1f} / 255"]),
    ):
        image = Image.open(src / f"{name}.png").convert("RGB")
        label(ImageDraw.Draw(image, "RGBA"), (10, 10), [title] + lines, 15)
        tiles.append(image.resize((640, 360), Image.LANCZOS))
    sheet = Image.new("RGB", (1280, 360))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, (i * 640, 0))
    sheet.save(dst / "micromesh-scene.png", optimize=True)


def compose_occlusion(src, dst):
    stats = json.loads((src / "occlusion.json").read_text())
    frames = stats["frames"]
    work = dst / "_occ"
    work.mkdir(exist_ok=True)
    keys = sorted(Path(p).stem for p in src.glob("occ_[0-9]*.png"))
    for i, key in enumerate(keys):
        f = frames[i]
        image = Image.open(src / f"{key}.png").convert("RGB")
        ratio = f["openTriangles"] / max(f["occTriangles"], 1)
        label(ImageDraw.Draw(image, "RGBA"), (10, 10), [
            f"{f['occTriangles']:,} triangles with occlusion",
            f"{f['openTriangles']:,} without ({ratio:.1f}x)",
            f"picture differs by {f['meanDifference']:.2f} / 255",
        ], 15)
        image.resize((560, 315), Image.LANCZOS).save(work / f"f{i:03d}.png")
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-framerate", "16", "-i", str(work / "f%03d.png"),
         "-vf", "split[a][b];[a]palettegen=max_colors=96[p];[b][p]paletteuse=dither=bayer:bayer_scale=4",
         "-loop", "0", str(dst / "micromesh-occlusion.gif")],
        check=True,
    )
    for f in work.glob("*.png"):
        f.unlink()
    work.rmdir()
    mid = len(frames) // 2
    f = frames[mid]
    tiles = []
    for name, title, lines in (
        (f"occ_{mid:03d}", "Occlusion on", [f"{f['occTriangles']:,} triangles, {f['occClusters']:,} clusters"]),
        (f"occ_off_{mid:03d}", "Occlusion off", [f"{f['openTriangles']:,} triangles, {f['openClusters']:,} clusters"]),
    ):
        image = Image.open(src / f"{name}.png").convert("RGB")
        label(ImageDraw.Draw(image, "RGBA"), (10, 10), [title] + lines, 15)
        tiles.append(image.resize((640, 360), Image.LANCZOS))
    sheet = Image.new("RGB", (1280, 360))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, (i * 640, 0))
    sheet.save(dst / "micromesh-occlusion.png", optimize=True)


def main(src, dst):
    src, dst = Path(src), Path(dst)
    dst.mkdir(parents=True, exist_ok=True)
    if (src / "occlusion.json").exists():
        compose_occlusion(src, dst)
    if (src / "scene.json").exists():
        compose_scene(src, dst)
    if (src / "uv.json").exists():
        compose_uv(src, dst)
    if (src / "normals.json").exists():
        compose_normals(src, dst)
    if (src / "field.json").exists():
        compose_field(src, dst)
    if not (src / "stats.json").exists():
        return
    stats = json.loads((src / "stats.json").read_text())
    frames = stats["frames"]
    total = stats["sourceTriangles"]

    # Four budgets, finest to coarsest.
    names = ["cut_0", "cut_1", "cut_2", "cut_3"]
    budgets = ["exact (level 0)", "error 0.004", "error 0.016", "error 0.06"]
    tiles = []
    for name, budget in zip(names, budgets):
        image = Image.open(src / f"{name}.png").convert("RGB")
        w, h = image.size
        image = image.crop((int(w * 0.17), int(h * 0.02), int(w * 0.83), int(h * 0.98)))
        draw = ImageDraw.Draw(image, "RGBA")
        info = frames[name]
        label(draw, (8, 8), [budget, f"{info['triangles']:,} triangles", f"{info['clusters']} clusters, level {info['maxLevel']}"])
        tiles.append(image)
    tw, th = tiles[0].size
    sheet = Image.new("RGB", (tw * 2, th * 2))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, ((i % 2) * tw, (i // 2) * th))
    sheet.save(dst / "micromesh-cuts.png", optimize=True)

    # The dolly as a GIF: frames annotated with their counters, palette built by ffmpeg.
    work = dst / "_frames"
    work.mkdir(exist_ok=True)
    keys = sorted(k for k in frames if k.startswith("dolly_"))
    for i, key in enumerate(keys):
        image = Image.open(src / f"{key}.png").convert("RGB")
        draw = ImageDraw.Draw(image, "RGBA")
        info = frames[key]
        label(draw, (10, 10), [f"{info['triangles']:,} of {total:,} triangles", f"{info['clusters']} clusters, coarsest level {info['maxLevel']}"], 16)
        image = image.resize((480, 270), Image.LANCZOS)
        image.save(work / f"f{i:03d}.png")
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-framerate", "18", "-i", str(work / "f%03d.png"),
         "-vf", "split[a][b];[a]palettegen=max_colors=96[p];[b][p]paletteuse=dither=bayer:bayer_scale=4",
         "-loop", "0", str(dst / "micromesh-dolly.gif")],
        check=True,
    )
    for f in work.glob("*.png"):
        f.unlink()
    work.rmdir()


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: compose_micromesh_docs.py FRAMES_DIR OUT_DIR")
    main(sys.argv[1], sys.argv[2])

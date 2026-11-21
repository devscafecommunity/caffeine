#!/usr/bin/env python3
"""Summarises the Micromesh scene benchmark (scene.json written by the env-gated Rust test
`micromesh_scene_frames_for_docs`, see scripts/render-micromesh-scene-docs.sh) as a Markdown table.

The benchmark is fixed: 81 rocks of one dense mesh, a 72-frame flight over them, the camera cut at 1.5 px with
the sun's cascades cut at four shadow texels, and the same flight with plain (instanced) meshes. Triangle
counts are exact and reproducible; times are CPU encoding time (SDL3 GPU has no portable GPU timestamps).
"""
import json
import statistics as st
import sys
from pathlib import Path


def main(src):
    stats = json.loads((Path(src) / "scene.json").read_text())
    frames = stats["frames"]

    def row(name, values, fmt="{:,.0f}"):
        return f"| {name} | " + " | ".join(fmt.format(v) for v in (min(values), st.mean(values), max(values))) + " |"

    micro = [f["triangles"] for f in frames]
    plain = [f["plainTriangles"] for f in frames]
    ratio = [f["plainTriangles"] / max(f["triangles"], 1) for f in frames]
    print(f"{stats['rocks']} rocks x {stats['rockTriangles']:,} triangles, {len(frames)} frames\n")
    print("| per frame | min | mean | max |")
    print("|---|---|---|---|")
    print(row("triangles, camera cut", micro))
    print(row("triangles, plain meshes", plain))
    print(row("fewer triangles (x)", ratio, "{:.1f}"))
    print(row("rocks cut (draws)", [f["draws"] for f in frames]))
    print(row("clusters selected", [f["clusters"] for f in frames]))
    print(row("index arena used (indices, all views)", [f["arenaUsed"] for f in frames]))
    print(row("CPU encode ms, cut", [f["microMs"] for f in frames], "{:.1f}"))
    print(row("CPU encode ms, plain", [f["plainMs"] for f in frames], "{:.1f}"))
    print(f"\nviews per frame: {sorted(set(f['views'] for f in frames))}, arena capacity "
          f"{max(f['arenaCapacity'] for f in frames):,} indices, cuts that did not fit: {sum(f['overflowed'] for f in frames)}")
    a = stats["aerial"]
    print(f"aerial view: {a['triangles']:,} of {a['source']:,} triangles, picture difference {a['meanDifference']:.2f} / 255")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")

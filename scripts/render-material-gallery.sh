#!/usr/bin/env bash
# Renders the procedural material library with the real engine renderer, for docs.
#
#   scripts/render-material-gallery.sh [--out DIR] [--size 512] [--baked DIR]
#
# Needs libcaffeine-capi + caffeine-render-snapshot (./rebuild.sh) and ImageMagick.
# Produces in DIR (default doppio/docs/images/material):
#   gallery-albedo.png        contact sheet of every preset's base colour
#   gallery-height.png        contact sheet of every preset's height map
#   gallery-normal.png        contact sheet of the normal maps (height in alpha ignored)
#   depth-<preset>.png        floor close-up, parallax OFF | parallax ON, side by side
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/doppio/docs/images/material"
size=512
bake=1
baked_dir=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --out) out="$2"; shift 2 ;;
        --size) size="$2"; shift 2 ;;
        --baked) baked_dir="$2"; bake=0; shift 2 ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done
if [[ -n "${baked_dir}" ]]; then
    work="${baked_dir}"   # reuse an earlier bake (kept)
else
    work="$(mktemp -d)"
    trap 'rm -rf "${work}"' EXIT
fi
mkdir -p "${out}"

cd "${root}/doppio"
cargo build --quiet -p doppio-ui --bin doppio-material
if [[ ${bake} -eq 1 ]]; then
    ./target/debug/doppio-material bake-preset --all --out "${work}" --size "${size}" >/dev/null
fi

sheet() {  # sheet <suffix> <target>
    montage "${work}"/*"$1" -tile 8x -geometry 160x160+3+3 -background '#151515' \
        -label '%t' "${out}/$2" 2>/dev/null || \
    montage "${work}"/*"$1" -tile 8x -geometry 160x160+3+3 -background '#151515' "${out}/$2"
}
sheet "_base.png" gallery-albedo.png
sheet "_height.png" gallery-height.png
for f in "${work}"/*_normal.png; do convert "${f}" -alpha off "${f%.png}.rgb.png"; done
montage "${work}"/*_normal.rgb.png -tile 8x -geometry 160x160+3+3 -background '#151515' "${out}/gallery-normal.png"

for name in red_brick cobblestone oak_planks diamond_plate dragon_scales rusted_iron hex_armour bark; do
    for mode in flat pom; do
        mat="${work}/${name}.mat"; [[ ${mode} == flat ]] && mat="${work}/${name}_flat.mat"
        SNAP_ONLY_FLOOR=1 SNAP_FLOOR_MAT="${mat}" "${root}/build/caffeine-render-snapshot" \
            "${work}/${name}-${mode}.png" 800 450 40 0 4 >/dev/null
    done
    convert "${work}/${name}-flat.png" "${work}/${name}-pom.png" +append \
        -gravity North -background '#151515' -splice 0x28 \
        -font "${root}/assets/jetbrains-mono/static/jetbrains-mono-latin-400-normal.ttf" -fill white -pointsize 18 -annotate +0+4 "${name//_/ }   —   parallax off  |  parallax on" \
        "${out}/depth-${name}.png"
done
# Realistic library: contact sheet of the "Realistic ..." presets + engine renders.
mapfile -t realistic < <("${root}/doppio/target/debug/doppio-material" bake-preset --slugs \
    | awk -F'\t' '$2 ~ /^Realistic/ { print $1 }')
base_files=()
for slug in "${realistic[@]}"; do base_files+=("${work}/${slug}_base.png"); done
if [[ ${#base_files[@]} -gt 0 ]]; then
    montage "${base_files[@]}" -tile 7x -geometry 150x150+3+3 -background '#151515' "${out}/realistic-albedo.png"
fi
shots=()
for name in ocean_swell meadow_grass cliff_granite autumn_leaves lava_flow oak_bark river_pebbles carbon_fibre; do
    [[ -f "${work}/${name}.mat" ]] || continue
    SNAP_ONLY_FLOOR=1 SNAP_FLOOR_MAT="${work}/${name}.mat" "${root}/build/caffeine-render-snapshot" \
        "${work}/r-${name}.png" 800 450 40 0 4 >/dev/null
    shots+=("${work}/r-${name}.png")
done
if [[ ${#shots[@]} -gt 0 ]]; then
    montage "${shots[@]}" -tile 2x -geometry 640x360+3+3 -background '#151515' "${out}/realistic-renders.png"
fi
echo "wrote gallery to ${out}"

#!/usr/bin/env bash
# Builds the preview contact sheets of the Nature Generator presets used by
# caffeine-website/content/en/docs/doppio/nature-generator-guide.md (software renderer, no engine needed). Needs ImageMagick.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/caffeine-website/content/en/docs/images/doppio/nature"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"
(cd "${root}/doppio" && DOPPIO_NATURE_DUMP="$work" cargo test -p doppio-ui --lib nature_presets_dump >/dev/null)
label() { magick "$1" -resize 256x256 -gravity south -background '#111' -fill '#c8c8d0' -font Adwaita-Sans \
    -pointsize 14 -splice 0x22 -annotate +0+3 "$2" "$3"; }
sheet() { # output name... (slug:label pairs)
    local target="$1"; shift
    local files=()
    for pair in "$@"; do
        label "$work/${pair%%:*}.png" "${pair#*:}" "$work/l_${pair%%:*}.png"
        files+=("$work/l_${pair%%:*}.png")
    done
    montage "${files[@]}" -tile 4x -geometry +2+2 -background '#111' "$target"
}
sheet "$out/nature-rocks.png" boulder:Boulder cut_stone:"Cut Stone" low_poly_rock:"Low-poly Rock" flat_slab:"Flat Slab" \
    pebble:Pebble mossy_rock:"Mossy Rock (hi-fi)" crystal_spire:"Crystal Spire"
sheet "$out/nature-trees.png" oak:Oak pine:Pine birch:Birch autumn_maple:"Autumn Maple" dead_tree:"Dead Tree" low_poly_tree:"Low-poly Tree" \
    low_poly_pine:"Low-poly Pine" ancient_oak:"Ancient Oak (hi-fi)"
sheet "$out/nature-plants.png" grass_clump:"Grass Clump" tall_grass:"Tall Grass" fern:Fern shrub:Shrub \
    autumn_shrub:"Autumn Shrub" fern_patch:"Fern Patch (hi-fi)" pink_flower:"Pink Flower" white_flower:"White Flower"
echo "wrote $out/nature-{rocks,trees,plants}.png"

"${root}/scripts/mirror-doppio-images.sh"

#!/usr/bin/env bash
# Builds the images of the Variation node class used by doppio/docs/materials-guide.md:
#   variation-ops.png      each Variation op applied to the same brick input
#   variation-presets.png  the "Varied" material presets next to the plain Red Brick
# Needs ImageMagick (magick, montage) and a release doppio-material.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/doppio/docs/images/material"
bin="${root}/doppio/target/release/doppio-material"
[ -x "$bin" ] || (cd "${root}/doppio" && cargo build --release -p doppio-ui --bin doppio-material)
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

label() { # file text
    magick "$1" -resize 256x256 -gravity south -background '#18181b' -fill '#c8c8d0' \
        -font Adwaita-Sans -pointsize 14 -splice 0x22 -annotate +0+3 "$2" "$3"
}

# 1. The ops, from the unit test that dumps them (same inputs every time).
(cd "${root}/doppio" && DOPPIO_VAR_DUMP="$work/ops" cargo test -p doppio-ui --lib variation_ops_dump >/dev/null)
label "$work/ops/0-input-bricks.png" "Input: bricks" "$work/l0.png"
label "$work/ops/1-noise-variation.png" "Noise Variation" "$work/l1.png"
label "$work/ops/2-cell-variation.png" "Cell Variation (grid)" "$work/l2.png"
label "$work/ops/3-patch-blend.png" "Patch Blend (bricks + dirt)" "$work/l3.png"
label "$work/ops/4-tile-shuffle.png" "Tile Shuffle" "$work/l4.png"
label "$work/ops/6-wfc-builtin.png" "Wave Function Collapse" "$work/l5.png"
label "$work/ops/6b-wfc-other-seed.png" "WFC, another seed" "$work/l6.png"
label "$work/ops/7-diamond-square.png" "Diamond-Square" "$work/l7.png"
montage "$work"/l[0-7].png -tile 4x2 -geometry +3+3 -background '#222' "$out/variation-ops.png"

# 2. The Varied presets.
for name in "Red Brick" "Varied Brick Wall" "Varied Cobblestone" "Varied Planks" \
            "Varied Hex Tiles" "WFC Brick Remix" "Diamond-Square Badlands" "Diamond-Square Moss Rock"; do
    "$bin" bake-preset "$name" --out "$work/presets" --size 512 >/dev/null
    slug="$("$bin" bake-preset --slugs | awk -F'\t' -v n="$name" '$3 == n { print $1 }')"
    label "$work/presets/${slug}_base.png" "$name" "$work/p_${slug}.png"
done
montage "$work"/p_*.png -tile 4x2 -geometry +3+3 -background '#222' "$out/variation-presets.png"
echo "wrote $out/variation-ops.png and $out/variation-presets.png"

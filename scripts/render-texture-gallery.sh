#!/usr/bin/env bash
# Bakes every Texture Generator preset (and two flipbooks) and builds the contact sheets used by
# caffeine-website/content/en/docs/doppio/texture-generator-guide.md. Needs ImageMagick (`montage`, `convert`).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/caffeine-website/content/en/docs/images/doppio/texture"
bin="${root}/doppio/target/release/doppio-material"
[ -x "$bin" ] || (cd "${root}/doppio" && cargo build --release -p doppio-ui --bin doppio-material)
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"

"$bin" bake-texture --all --out "$work/png" --size 256 >/dev/null
# Show every sprite over a checkerboard so transparency is visible.
magick -size 256x256 pattern:checkerboard -scale 400% -crop 256x256+0+0 +repage \
    -fill '#2a2a2e' -opaque black -fill '#3a3a40' -opaque white -type TrueColor -colorspace sRGB PNG24:"$work/checker.png"
while IFS=$'\t' read -r slug category name; do
    magick "$work/checker.png" "$work/png/${slug}.png" -compose over -composite \
        -gravity south -font Adwaita-Sans -background '#18181b' -fill '#c8c8d0' -pointsize 15 \
        -splice 0x24 -annotate +0+4 "${name}" "$work/shot-${slug}.png"
done < <("$bin" bake-texture --slugs)
shopt -s nullglob
sheets=("$work"/shot-*.png)
montage "${sheets[@]}" -tile 4x -geometry 256x280+4+4 -background '#18181b' "$out/texture-gallery.png"

# Flipbooks: one sheet each, shown over the checkerboard.
for preset in "Torch Flame" "Smoke Puff"; do
    slug="$(echo "$preset" | tr 'A-Z ' 'a-z_')"
    "$bin" bake-texture "$preset" --out "$work/flip" --size 128 --flipbook 4x2 >/dev/null
    magick -size 512x256 pattern:checkerboard -scale 400% -crop 512x256+0+0 +repage \
        -fill '#2a2a2e' -opaque black -fill '#3a3a40' -opaque white -type TrueColor -colorspace sRGB PNG24:"$work/fc.png"
    magick "$work/fc.png" "$work/flip/${slug}_flipbook_4x2.png" -compose over -composite \
        "$out/flipbook-${slug}.png"
done
echo "wrote $out/texture-gallery.png and the flipbooks"

"${root}/scripts/mirror-doppio-images.sh"

#!/usr/bin/env bash
# Renders the terrain-mask documentation sheet: the Alpine Range placed in the engine (an env-gated Doppio test),
# every mask read back through the C ABI, composed into caffeine-website/content/en/docs/images/doppio/terrain/terrain-masks.png.
# Needs libcaffeine-capi built, a GPU and Pillow.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
cmake --build "${root}/build" --target caffeine-capi -j
cd "${root}/doppio"
DOPPIO_TERRAIN_MASKS="${tmp}" cargo test -p doppio-ui --lib terrain_mask_pictures_for_docs
cd "${root}"
python3 "${root}/scripts/compose_terrain_mask_docs.py" "${tmp}" "${root}/caffeine-website/content/en/docs/images/doppio/terrain"
echo "wrote caffeine-website/content/en/docs/images/doppio/terrain/terrain-masks.png"

"${root}/scripts/mirror-doppio-images.sh"

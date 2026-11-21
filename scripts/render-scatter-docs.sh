#!/usr/bin/env bash
# Renders the Scatter documentation sheet: the Alpine Range scattered with the default plan (an env-gated Doppio test
# through the real action and engine), composed into doppio/docs/images/terrain/scatter-on-terrain.png.
# Needs libcaffeine-capi built, a GPU and Pillow.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
cmake --build "${root}/build" --target caffeine-capi -j
cd "${root}/doppio"
DOPPIO_SCATTER_DOCS="${tmp}" cargo test -p doppio-ui --lib scatter_pictures_for_docs
cd "${root}"
python3 "${root}/scripts/compose_scatter_docs.py" "${tmp}" "${root}/doppio/docs/images/terrain"

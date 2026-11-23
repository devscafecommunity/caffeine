#!/usr/bin/env bash
# Renders the Micromesh documentation media on the GPU (hidden C++ test) and composes them into
# caffeine-website/content/en/docs/images/doppio/micromesh/. Needs a built CaffeineTest, a GPU, glslc, Pillow and ffmpeg.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
cmake --build "${root}/build" --target CaffeineTest -j
CAFFEINE_DOC_OUT="${tmp}" "${root}/build/tests/CaffeineTest" "[.micromesh-docs]"
CAFFEINE_DOC_OUT="${tmp}" "${root}/build/tests/CaffeineTest" "[.micromesh-field]"
CAFFEINE_DOC_OUT="${tmp}" "${root}/build/tests/CaffeineTest" "[.micromesh-normals]"
CAFFEINE_DOC_OUT="${tmp}" "${root}/build/tests/CaffeineTest" "[.micromesh-uv]"
python3 "${root}/scripts/compose_micromesh_docs.py" "${tmp}" "${root}/caffeine-website/content/en/docs/images/doppio/micromesh"
echo "wrote ${root}/caffeine-website/content/en/docs/images/doppio/micromesh"

"${root}/scripts/mirror-doppio-images.sh"

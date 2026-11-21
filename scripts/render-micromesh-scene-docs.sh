#!/usr/bin/env bash
# Renders the Micromesh scene-path documentation media through the real scene renderer (an env-gated
# Rust test: lit shader, sun shadows, G-buffer) and composes them into caffeine-website/content/en/docs/images/doppio/micromesh/.
# Needs libcaffeine-capi built, a GPU, Pillow and ffmpeg.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
cmake --build "${root}/build" --target caffeine-capi -j
cd "${root}/doppio"
DOPPIO_MICROMESH_ANIM="${tmp}" cargo test -p doppio-ui --lib micromesh_scene_frames_for_docs
DOPPIO_MICROMESH_OCCLUSION="${tmp}" cargo test -p doppio-ui --lib micromesh_occlusion_frames_for_docs
cd "${root}"
python3 "${root}/scripts/compose_micromesh_docs.py" "${tmp}" "${root}/caffeine-website/content/en/docs/images/doppio/micromesh"
python3 "${root}/scripts/micromesh_benchmark_report.py" "${tmp}"
echo "wrote micromesh-scene.gif/png and micromesh-occlusion.gif/png"

"${root}/scripts/mirror-doppio-images.sh"

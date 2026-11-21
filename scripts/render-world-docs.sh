#!/usr/bin/env bash
# Renders the water and vegetation documentation media on the GPU (env-gated Rust tests) and composes
# them into doppio/docs/images/{water,nature}/: the preset sheet, the animated storm, the oak in the
# wind. Needs libcaffeine-capi built, a GPU, Pillow and ffmpeg.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
mkdir -p "${tmp}/presets" "${tmp}/storm" "${tmp}/oak"
cmake --build "${root}/build" --target caffeine-capi -j
# One process per capture: engine sessions share global state and a second one in the same process can
# crash, so each test gets its own cargo invocation.
cd "${root}/doppio"
DOPPIO_WATER_DUMP="${tmp}/presets" cargo test -p doppio-ui --lib water_presets_render_in_the_engine
DOPPIO_WATER_ANIM="${tmp}/storm" cargo test -p doppio-ui --lib water_animation_frames_for_docs
DOPPIO_NATURE_ANIM="${tmp}/oak" cargo test -p doppio-ui --lib nature_wind_frames_for_docs
cd "${root}"
python3 "${root}/scripts/compose_world_media.py" "${tmp}" "${root}/doppio/docs/images"
echo "wrote water-presets.png, water-storm.gif, nature-oak-wind.gif"

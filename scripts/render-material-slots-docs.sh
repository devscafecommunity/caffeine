#!/usr/bin/env bash
# Renders the material-slots documentation sheet: a two-group rock with its slots in both orders and a Nature model
# placed merged (an env-gated Doppio test through the real engine), composed into
# doppio/docs/images/nature/material-slots.png. Needs libcaffeine-capi built, a GPU and Pillow.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
cmake --build "${root}/build" --target caffeine-capi -j
cd "${root}/doppio"
DOPPIO_SLOTS_DOCS="${tmp}" cargo test -p doppio-ui --lib material_slots_pictures_for_docs
cd "${root}"
python3 "${root}/scripts/compose_material_slots_docs.py" "${tmp}" "${root}/doppio/docs/images/nature"

#!/usr/bin/env bash
# Export raw assets to a .cap pack via caf-pack CLI.
# Used by Convoy, WaveShaper, CI, and manual workflows.
set -euo pipefail

INPUT_DIR="${1:-raw_assets}"
OUTPUT_CAP="${2:-game.cap}"
HEADER_OUT="${3:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CAF_PACK_BIN="${ROOT_DIR}/build/caf-pack/src/caf-pack"

if [[ ! -x "${CAF_PACK_BIN}" ]]; then
  CAF_PACK_BIN="$(command -v caf-pack || true)"
fi

if [[ -z "${CAF_PACK_BIN}" || ! -x "${CAF_PACK_BIN}" ]]; then
  echo "error: caf-pack binary not found. Build with: cmake --build build --target caf-pack" >&2
  exit 1
fi

ARGS=(--input "${INPUT_DIR}" --output "${OUTPUT_CAP}")
if [[ -n "${HEADER_OUT}" ]]; then
  ARGS+=(--gen-ids "${HEADER_OUT}")
fi

exec "${CAF_PACK_BIN}" "${ARGS[@]}"

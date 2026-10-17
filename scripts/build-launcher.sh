#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LAUNCHER="${ROOT}/caffeine-launcher"
if [[ ! -f "${LAUNCHER}/Cargo.toml" ]]; then
  echo "caffeine-launcher submodule missing. Run: git submodule update --init caffeine-launcher" >&2
  exit 1
fi
cd "${LAUNCHER}"
cargo build --release "$@"
echo "Built: ${LAUNCHER}/target/release/caffeine-launcher"

#!/usr/bin/env bash
# Initialize or update all Caffeine monorepo git submodules.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT}"

SUBMODULES=(
  caf-pack
  caffeine-website
  caffeine-launcher
  doppio-plugins
)

echo "==> git submodule update --init --recursive (${#SUBMODULES[@]} paths)"
git submodule update --init --recursive "${SUBMODULES[@]}"

echo ""
echo "Pinned revisions:"
git submodule status "${SUBMODULES[@]}"

echo ""
echo "Optional follow-ups:"
echo "  Docs site:  cd caffeine-website && npm install && npm run dev"
echo "  Launcher:   scripts/build-launcher.sh"
echo "  Plugins:    cmake -S doppio-plugins -B doppio-plugins/build -DCAFFEINE_ROOT=\"$ROOT\" && cmake --build doppio-plugins/build"
echo "  Engine:     scripts/build.sh"

#!/usr/bin/env bash
# Run the iced-mcp visual regression flow for Doppio.
#
# First run on a new machine/image (seeds baselines):
#   ./scripts/check-doppio-visual.sh --update-baselines
# Afterwards:
#   ./scripts/check-doppio-visual.sh
#
# Skips cleanly when iced-mcp is missing or no display is available.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v iced-mcp >/dev/null 2>&1; then
    echo "check-doppio-visual: iced-mcp not installed; skipping (cargo install iced-mcp-server)"
    exit 0
fi
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "check-doppio-visual: no display; skipping"
    exit 0
fi

if [[ -d "${root}/build" ]]; then
    cmake --build "${root}/build" --target caffeine-capi -j"$(nproc)"
fi

cd "${root}/doppio"
cargo build -p doppio-ui --features agent >/dev/null

export ICED_MCP=1
iced-mcp run-script .iced-mcp/scripts/visual-regression.luau \
    --launch "./target/debug/doppio ../assets/benchmarks/dense_outdoor.caf" \
    "$@"

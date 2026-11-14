#!/usr/bin/env bash
# Launch the Rust Doppio with the iced-mcp bridge enabled.
#
# Requires `cargo install iced-mcp-server` once, and an MCP client configured
# with the `iced` server (see .cursor/mcp.json or doppio/docs/rust-ui.md).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -d "${root}/build" ]]; then
    cmake --build "${root}/build" --target caffeine-capi -j"$(nproc)"
fi

cd "${root}/doppio"
export ICED_MCP="${ICED_MCP:-1}"
exec cargo run -p doppio-ui --features agent -- "$@"

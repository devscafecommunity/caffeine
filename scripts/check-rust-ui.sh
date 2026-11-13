#!/usr/bin/env bash
# Build and test the isolated Rust/Iced Doppio frontend proof of concept.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${root}/apps/doppio-rs"

cargo fmt --check
cargo test
cargo check --features agent
cargo clippy --all-targets --features agent -- -D warnings

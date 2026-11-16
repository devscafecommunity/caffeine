#!/usr/bin/env bash
# Build and test the isolated Rust/Iced Doppio frontend proof of concept.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${root}/doppio"

if [[ -d "${root}/build" ]]; then
    cmake --build "${root}/build" --target caffeine-capi -j"$(nproc)"
fi

cargo fmt --check
cargo test --workspace
cargo check -p doppio-ui --features agent
cargo build -p doppio-ui --features headless --bin doppio-headless
cargo clippy --workspace --all-targets -- -D warnings
cargo clippy -p doppio-ui --all-targets --features agent -- -D warnings
cargo clippy -p doppio-ui --all-targets --features headless -- -D warnings
# Every shipped UI flow must pass through the real CLI as well as `cargo test`.
./target/debug/doppio-headless run "${root}/doppio/tests/ui-flows" --out "${TMPDIR:-/tmp}/doppio-flows" --format text

# Panels compose reusable components; they must not build interactive widgets.
raw=$(grep -rnE '\b(text_input|checkbox|pick_list|slider|button)\(' \
    "${root}/doppio/crates/ui/src/uikit/panels" 2>/dev/null || true)
if [[ -n "${raw}" ]]; then
    echo "ERROR: raw interactive widgets in uikit/panels (use uikit/components):" >&2
    echo "${raw}" >&2
    exit 1
fi

# Visual tokens live in uikit/theme.rs; components and panels must not hardcode colours.
leaks=$(grep -rnE 'Color::(from_rgb|from_rgba8|WHITE|BLACK)' \
    "${root}/doppio/crates/ui/src/uikit" 2>/dev/null | grep -v '/theme.rs:' || true)
if [[ -n "${leaks}" ]]; then
    echo "ERROR: hardcoded colours outside uikit/theme.rs:" >&2
    echo "${leaks}" >&2
    exit 1
fi

# The Rust frontend is only useful if the C++ engine stays UI-free.
python3 "${root}/scripts/audit_core_ui_coupling.py" --fail-on-leaks

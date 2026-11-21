#!/usr/bin/env bash
# Headless UI driver for Doppio: scripted, human-like actions and screenshots
# without opening a window. All arguments are passed to `doppio-headless`, and
# relative paths resolve against the directory you call this from.
#
#   scripts/doppio-headless.sh widgets
#   scripts/doppio-headless.sh run doppio/tests/ui-flows --format text
#   scripts/doppio-headless.sh session
#
# Docs: caffeine-website/content/en/docs/doppio/headless-testing.md
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec cargo run --quiet --manifest-path "${root}/doppio/Cargo.toml" \
    -p doppio-ui --features headless --bin doppio-headless -- "$@"

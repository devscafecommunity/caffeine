#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"

if [[ ! -f "$BUILD_DIR/doppio" ]]; then
    echo "ERROR: doppio binary not found at $BUILD_DIR/doppio"
    echo "       Build the project first or set BUILD_DIR=/path/to/build"
    exit 1
fi

export DOPPIO_BIN="$BUILD_DIR/doppio"

echo "=== Doppio UI Automated Tests ==="
echo "Binary: $DOPPIO_BIN"
echo ""

python3 -m pytest "$SCRIPT_DIR/test_ui_automated.py" -v --tb=short "$@"

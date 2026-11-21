#!/usr/bin/env bash
# The Doppio guides' pictures live in caffeine-website/content/en/docs/images/doppio. The Portuguese
# pages link their own copy (so they render on GitHub too): this copies what is new or changed.
# Identical files are one git object, so the copy costs working-tree space only.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
from="${root}/caffeine-website/content/en/docs/images/doppio"
to="${root}/caffeine-website/content/pt/docs/images/doppio"
[[ -d "${from}" ]] || exit 0
mkdir -p "${to}"
cp -ru "${from}/." "${to}/"

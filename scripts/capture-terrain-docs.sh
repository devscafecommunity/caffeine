#!/usr/bin/env bash
# Captures the terrain UI screenshots (headless, no window) into
# doppio/docs/images/terrain/ui with stable names.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/doppio/docs/images/terrain/ui"
rm -rf "${out}"; mkdir -p "${out}"
"${root}/scripts/doppio-headless.sh" run "${root}/doppio/docs/flows/capture-terrain-docs.json" \
    --out "${out}" --format text
for file in "${out}"/[0-9][0-9][0-9]-*.png; do
    mv "${file}" "${out}/$(basename "${file}" | cut -c5-)"
done
echo "wrote ${out}"

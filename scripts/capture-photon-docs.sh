#!/usr/bin/env bash
# Captures the Photon tab screenshots (headless, no window) into
# caffeine-website/content/en/docs/images/doppio/photon/ui with stable names.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/caffeine-website/content/en/docs/images/doppio/photon/ui"
rm -rf "${out}"; mkdir -p "${out}"
"${root}/scripts/doppio-headless.sh" run "${root}/caffeine-website/captures/flows/capture-photon-docs.json" \
    --out "${out}" --format text
for file in "${out}"/[0-9][0-9][0-9]-*.png; do
    mv "${file}" "${out}/$(basename "${file}" | cut -c5-)"
done
echo "wrote ${out}"

"${root}/scripts/mirror-doppio-images.sh"

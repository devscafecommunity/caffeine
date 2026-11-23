#!/usr/bin/env bash
# Captures the editor screenshots used by caffeine-website (headless, no window)
# into caffeine-website/content/en/docs/images/doppio/editor, with stable names (the NNN- prefix is stripped).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/caffeine-website/content/en/docs/images/doppio/editor"
# scene3d needs the C ABI built in build/
if [ -d "${root}/build" ]; then
    export LD_LIBRARY_PATH="${root}/build:${LD_LIBRARY_PATH:-}"
    export DYLD_LIBRARY_PATH="${root}/build:${DYLD_LIBRARY_PATH:-}"
fi
rm -rf "${out}"; mkdir -p "${out}"
"${root}/scripts/doppio-headless.sh" run "${root}/caffeine-website/captures/flows/capture-editor-docs.json" \
    --out "${out}" --format text
for file in "${out}"/[0-9][0-9][0-9]-*.png; do
    mv "${file}" "${out}/$(basename "${file}" | cut -c5-)"
done
echo "wrote ${out}"

"${root}/scripts/mirror-doppio-images.sh"

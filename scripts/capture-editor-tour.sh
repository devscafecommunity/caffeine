#!/usr/bin/env bash
# Records the short Doppio workspace tour used by the docs (headless, no window).
# Writes doppio-tour.mp4 and, when ffmpeg is available, doppio-tour.gif.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/doppio/docs/images/editor"
mkdir -p "${out}"
if [ -d "${root}/build" ]; then
    export LD_LIBRARY_PATH="${root}/build:${LD_LIBRARY_PATH:-}"
    export DYLD_LIBRARY_PATH="${root}/build:${DYLD_LIBRARY_PATH:-}"
fi
"${root}/scripts/doppio-headless.sh" run "${root}/doppio/docs/flows/capture-editor-tour.json" \
    --out "${out}" --format text
if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -y -i "${out}/doppio-tour.mp4" \
        -vf "fps=10,scale=800:-1:flags=lanczos,split[s0][s1];[s0]palettegen=max_colors=128[p];[s1][p]paletteuse" \
        "${out}/doppio-tour.gif" >/dev/null 2>&1 || true
fi
echo "wrote ${out}/doppio-tour.mp4"

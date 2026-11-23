#!/usr/bin/env bash
# Records the generator clips used by the Capello Studio site (headless) and joins them
# into one 16:9 tour. Output: doppio/docs/images/site/{tour.mp4,tour-poster.jpg}. Needs ffmpeg.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/doppio/docs/images/site"
mkdir -p "${out}"
order=(terrain nature material water)
for n in "${order[@]}"; do
    "${root}/scripts/doppio-headless.sh" run "${root}/doppio/docs/flows/capture-site-${n}.json" --out "${out}" --format text
done
list="$(mktemp)"
for n in "${order[@]}"; do echo "file '${out}/${n}.mp4'" >> "${list}"; done
ffmpeg -y -v error -f concat -safe 0 -i "${list}" -c:v libx264 -crf 24 -pix_fmt yuv420p -movflags +faststart "${out}/tour.mp4"
ffmpeg -y -v error -ss 3 -i "${out}/tour.mp4" -frames:v 1 -q:v 3 "${out}/tour-poster.jpg"
rm -f "${list}"
for n in "${order[@]}"; do rm -f "${out:?}/${n}.mp4"; done
echo "wrote ${out}/tour.mp4"

#!/usr/bin/env bash
# Captures the Environment tab screenshots (headless, no window) into
# caffeine-website/content/en/docs/images/doppio/environment/ui, and the engine renders the guide shows
# (the road scene under each preset, and the sky under each preset) into .../environment, with stable names.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${root}/caffeine-website/content/en/docs/images/doppio/environment"
rm -rf "${out}"; mkdir -p "${out}/ui"
"${root}/scripts/doppio-headless.sh" run "${root}/caffeine-website/captures/flows/capture-environment-docs.json" \
    --out "${out}/ui" --format text
for file in "${out}"/ui/[0-9][0-9][0-9]-*.png; do
    mv "${file}" "${out}/ui/$(basename "${file}" | cut -c5-)"
done

# The renders come from the engine itself (the regression scene of the Rust tests): one road image and one
# sky image per preset. Needs libcaffeine-capi (cmake --build build --target caffeine-capi).
(cd "${root}/doppio" && ENV_SHOTS="${out}" cargo test -p doppio-ui --lib zz_environment_shots -- --ignored --nocapture)
echo "wrote ${out}"

"${root}/scripts/mirror-doppio-images.sh"

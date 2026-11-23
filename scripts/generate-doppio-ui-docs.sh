#!/usr/bin/env bash
# Generate Doppio UI reference images and Markdown from the UI catalog.
#
#   ./scripts/generate-doppio-ui-docs.sh            # writes doppio/target/doppio-ui-docs
#   ./scripts/generate-doppio-ui-docs.sh --publish  # also updates caffeine-website
#
# Descriptions come from caffeine-website/captures/ui-captions.json (en + pt). An agent can
# edit that file to improve or translate the captions without touching code.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${DOPPIO_DOCS_OUT:-${root}/doppio/target/doppio-ui-docs}"
captions="${DOPPIO_DOCS_CAPTIONS:-${root}/caffeine-website/captures/ui-captions.json}"

publish=0
if [[ "${1:-}" == "--publish" ]]; then
    publish=1
fi

(
    cd "${root}/doppio"
    DOPPIO_DOCS_OUT="${out}" DOPPIO_DOCS_CAPTIONS="${captions}" \
        cargo test -p doppio-ui capture_ui_gallery -- --ignored --nocapture
)

if [[ "${publish}" -eq 1 ]]; then
    website="${root}/caffeine-website"
    if [[ ! -d "${website}/content" ]]; then
        echo "caffeine-website submodule is missing; run ./scripts/submodules.sh" >&2
        exit 1
    fi
    for lang in en pt; do
        mkdir -p "${website}/content/${lang}/docs/editor" \
                 "${website}/content/${lang}/docs/images/ui-reference"
        cp "${out}/README.${lang}.md" \
           "${website}/content/${lang}/docs/editor/ui-reference.md"
        cp "${out}"/*.png \
           "${website}/content/${lang}/docs/images/ui-reference/"
    done
    echo "published UI reference to caffeine-website/content/{en,pt}/docs/editor/ui-reference.md"
else
    echo "wrote ${out}"
    echo "run with --publish to update caffeine-website content"
fi

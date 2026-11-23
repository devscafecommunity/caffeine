#!/usr/bin/env bash
# The reproducible Micromesh benchmark set: a dense rock field, a forest of dense trees and a terrain, each flown by a
# fixed camera at 640x360 with the sun's shadows on, with every renderer path that applies (plain instanced meshes,
# Micromesh). The content is generated in code by hashes (no assets): the triangle counts are the same every run.
# Needs libcaffeine-capi built and a GPU. Prints Markdown tables; the raw per-frame numbers stay in the temp dir
# unless BENCH_OUT=<dir> is set.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${BENCH_OUT:-$(mktemp -d)}"
mkdir -p "${out}"
cmake --build "${root}/build" --target caffeine-capi -j >/dev/null
cd "${root}/doppio"
DOPPIO_BENCH_DIR="${out}" cargo test -p doppio-ui --lib micromesh_benchmark_set >/dev/null
python3 "${root}/scripts/micromesh_benchmark_set_report.py" "${out}"
echo "raw numbers: ${out}/benchmarks.json"

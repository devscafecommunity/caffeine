#!/usr/bin/env bash
# Build the standalone Doppio plugins and install them into the engine build.
#
#   ./scripts/build-plugins.sh                 # build all, copy to build/plugins
#   CAFFEINE_BUILD_DIR=out ./scripts/build-plugins.sh
#   ./scripts/build-plugins.sh --target volumetric_clouds_plugin
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_BUILD="${CAFFEINE_BUILD_DIR:-${ROOT}/build}"
PLUGIN_ROOT="${ROOT}/doppio-plugins"
PLUGIN_BUILD="${PLUGIN_ROOT}/build"
TARGET=""

while (($#)); do
    case "$1" in
        --target)
            TARGET="${2:?--target requires a name}"
            shift 2
            ;;
        -h|--help)
            sed -n '2,8p' "$0"
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [[ ! -f "${PLUGIN_ROOT}/CMakeLists.txt" ]]; then
    echo "doppio-plugins submodule is missing; run ./scripts/submodules.sh" >&2
    exit 1
fi
if [[ ! -d "${ENGINE_BUILD}/_deps/imgui-src" ]]; then
    echo "ImGui sources not found in ${ENGINE_BUILD}/_deps/imgui-src" >&2
    echo "Build Doppio first: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

cmake -S "${PLUGIN_ROOT}" -B "${PLUGIN_BUILD}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
    -DCAFFEINE_ROOT="${ROOT}" \
    -DIMGUI_DIR="${ENGINE_BUILD}/_deps/imgui-src"

if [[ -n "${TARGET}" ]]; then
    cmake --build "${PLUGIN_BUILD}" --target "${TARGET}" -j"$(nproc)"
else
    cmake --build "${PLUGIN_BUILD}" -j"$(nproc)"
fi

mkdir -p "${ENGINE_BUILD}/plugins"
shopt -s nullglob
for lib in "${PLUGIN_BUILD}/plugins/"*.so "${PLUGIN_BUILD}/plugins/"*.dylib \
           "${PLUGIN_BUILD}/plugins/"*.dll; do
    cp -f "${lib}" "${ENGINE_BUILD}/plugins/"
done

echo ""
echo "Installed plugins in ${ENGINE_BUILD}/plugins:"
ls -1 "${ENGINE_BUILD}/plugins" 2>/dev/null || true
echo ""
echo "Start Doppio and enable them in Plugins -> Plugin Manager."

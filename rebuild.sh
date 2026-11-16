#!/usr/bin/env bash
#
# Caffeine + Doppio — rebuild everything in one go.
#
#   ./rebuild.sh                  clean Release build of engine, caf-pack and Doppio
#   ./rebuild.sh -i               incremental (keep build dirs; much faster)
#   ./rebuild.sh --debug          Debug instead of Release
#   ./rebuild.sh --test           also run ctest and cargo test afterwards
#   ./rebuild.sh --engine-only    only the C++ side (caffeine + caf-pack)
#   ./rebuild.sh --doppio-only    only the Rust editor (needs libcaffeine-capi already built)
#
# What it builds
#   1. Caffeine (CMake, ./build): caffeine-core, libcaffeine-capi (the C ABI the
#      editor loads), caffeine-runtime, caffeine-combined, caf-encode, plugins.
#   2. caf-pack (CMake, ./caf-pack/build): the asset packer used by the Assets
#      wizard (Pack tab).
#   3. Doppio (Cargo, ./doppio): the Iced editor `doppio` and the headless UI
#      driver `doppio-headless`.
#
# Doppio looks for libcaffeine-capi next to its executable, then at
# $CAFFEINE_CAPI_LIB, then at ./build; this script verifies it exists, because
# without it Doppio silently falls back to "fixtures" and shows no engine.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${root}"

profile="release"
incremental=0
run_tests=0
do_engine=1
do_doppio=1
optional_failed=0

usage() { sed -n '3,23p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

for arg in "$@"; do
    case "${arg}" in
        --debug) profile="debug" ;;
        --release) profile="release" ;;
        -i|--incremental) incremental=1 ;;
        --test|--tests) run_tests=1 ;;
        --engine-only) do_doppio=0 ;;
        --doppio-only) do_engine=0 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: ${arg}" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ "${profile}" == "release" ]]; then
    cmake_type="Release"
    cargo_flag=(--release)
else
    cmake_type="Debug"
    cargo_flag=()
fi
jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# A CMake build dir copied from another machine/path cannot be reused
# ("CMakeCache.txt directory is different…"); detect it and start fresh.
reset_if_stale() {
    local build_dir="$1" source_dir="$2" cached
    [[ -f "${build_dir}/CMakeCache.txt" ]] || return 0
    cached="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${build_dir}/CMakeCache.txt")"
    if [[ -n "${cached}" && "${cached}" != "${source_dir}" ]]; then
        warn "${build_dir} was configured for ${cached}; removing it"
        rm -rf "${build_dir}"
    fi
}

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
ok() { printf '\033[1;32m✓ %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m! %s\033[0m\n' "$*"; }

echo "════════════════════════════════════════════════════════════"
echo "  CAFFEINE + DOPPIO — $([[ ${incremental} -eq 1 ]] && echo INCREMENTAL || echo FULL) REBUILD (${cmake_type})"
echo "════════════════════════════════════════════════════════════"

if [[ ${do_engine} -eq 1 ]]; then
    if [[ ${incremental} -eq 0 ]]; then
        step "Cleaning previous engine builds"
        rm -rf build caf-pack/build
    fi

    reset_if_stale build "${root}"
    reset_if_stale caf-pack/build "${root}/caf-pack"

    step "Configuring Caffeine (CMake ${cmake_type})"
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE="${cmake_type}" \
        -DCAFFEINE_ENABLE_SCRIPTING=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    # The C ABI is what Doppio loads, so it must build; everything else
    # (runtime, tools, plugins, tests) is built afterwards and may fail
    # without taking the editor down with it.
    step "Building the C ABI (libcaffeine-capi) — required by Doppio"
    cmake --build build --target caffeine-capi -j"${jobs}"

    step "Building the rest of Caffeine (runtime, tools, plugins, tests)"
    if ! cmake --build build -j"${jobs}" -- -k; then
        warn "some optional Caffeine targets failed to build (see the errors above); the C ABI and Doppio are unaffected"
        optional_failed=1
    fi

    if [[ -f caf-pack/CMakeLists.txt ]]; then
        step "Building caf-pack"
        cmake -S caf-pack -B caf-pack/build -DCMAKE_BUILD_TYPE="${cmake_type}"
        cmake --build caf-pack/build -j"${jobs}"
    else
        warn "caf-pack submodule missing (run ./scripts/submodules.sh); the Pack tab will not find the packer"
    fi
fi

if [[ ${do_doppio} -eq 1 ]]; then
    if ! command -v cargo >/dev/null 2>&1; then
        echo "cargo not found: install Rust (rustup) to build Doppio" >&2
        exit 1
    fi
    if [[ ${incremental} -eq 0 ]]; then
        step "Cleaning the Doppio crates"
        cargo clean --manifest-path doppio/Cargo.toml -p doppio-ui -p doppio-engine \
            ${cargo_flag[@]+"${cargo_flag[@]}"} 2>/dev/null || true
    fi
    step "Building Doppio (${profile}): editor + headless UI driver"
    cargo build --manifest-path doppio/Cargo.toml -p doppio-ui --features headless \
        ${cargo_flag[@]+"${cargo_flag[@]}"}
fi

if [[ ${run_tests} -eq 1 ]]; then
    if [[ ${do_engine} -eq 1 ]]; then
        step "Running the C++ tests (ctest)"
        (cd build && ctest --output-on-failure -j"${jobs}")
    fi
    if [[ ${do_doppio} -eq 1 ]]; then
        step "Running the Doppio tests (cargo test)"
        cargo test --manifest-path doppio/Cargo.toml --workspace \
            ${cargo_flag[@]+"${cargo_flag[@]}"}
    fi
fi

echo
echo "════════════════════════════════════════════════════════════"
echo "  ✓ BUILD COMPLETE"
echo "════════════════════════════════════════════════════════════"
show() {
    if [[ -e "$1" ]]; then
        printf '  \033[32m✓\033[0m %-48s %s\n' "$1" "$(du -h "$1" | cut -f1)"
    else
        printf '  \033[31m✗\033[0m %-48s missing\n' "$1"
    fi
}
if [[ ${do_engine} -eq 1 ]]; then
    show build/libcaffeine-capi.so
    show build/caffeine-runtime
    show build/caf-encode
    show caf-pack/build/src/caf-pack
fi
if [[ ${do_doppio} -eq 1 ]]; then
    show "doppio/target/${profile}/doppio"
    show "doppio/target/${profile}/doppio-headless"
fi
if [[ ! -e build/libcaffeine-capi.so && ! -e build/libcaffeine-capi.dylib && -z "${CAFFEINE_CAPI_LIB:-}" ]]; then
    warn "libcaffeine-capi was not found: Doppio will start in fixture mode (no engine). Build the engine (drop --doppio-only)."
fi
if [[ ${optional_failed} -eq 1 ]]; then
    warn "optional engine targets failed (e.g. plugins); re-run with -i after fixing them"
fi
echo
echo "Run the editor:     doppio/target/${profile}/doppio"
echo "Headless UI driver: ./scripts/doppio-headless.sh help"

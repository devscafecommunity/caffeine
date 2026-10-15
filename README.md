# Caffeine Engine

High-performance C++ game engine (SDL3) for Codex Studio.

**Documentation, roadmaps, and PDF books** live in the [`caffeine-website`](./caffeine-website) submodule (Svelte site + TeX). Agents should use the docs MCP defined there — see [`AGENTS.md`](./AGENTS.md).

## Build

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest --output-on-failure
```

More detail: `scripts/build.sh` (Linux/macOS) or `scripts/build.bat` (Windows).

## Layout

- `src/` — engine source
- `apps/` — runtime and editor applications
- `tests/` — unit tests
- `caffeine-website/` — documentation submodule

# AGENTS.md — Caffeine Engine

Rules for humans and coding agents working in **this repository** (the C++ engine). User-facing documentation lives in the **`caffeine-website`** submodule, not here.

## Repository layout

| Path | Purpose |
|------|---------|
| `src/` | Engine runtime, editor, render, script bridge |
| `apps/` | Executables (runtime, editor shells, plugins) |
| `include/` | Public C++ headers shipped to games/tools |
| `tests/` | GTest suites |
| `cmake/` | CMake modules |
| `assets/` | Sample content (no `node_modules` in git) |
| `caf-pack/` | Asset packer CLI + `.caf` / `.cap` pipeline (submodule) |
| `Convoy/` | 2D art station (submodule, optional CMake target) |
| `WaveShaper/` | Game-audio DAW (submodule, optional; needs VST3 SDK) |
| `caffeine-website/` | Docs site (Svelte/HTML), TeX/PDF, docs MCP |
| `caffeine-launcher/` | Desktop hub — launch apps, plugins, updates (Rust/Iced) |
| `scripts/submodules.sh` | `git submodule update --init` for all paths below |

Do not add `docs/`, `planning/`, or `.planning/` back to this repo.

## Ecosystem submodules

All sibling repos live under [devscafecommunity](https://github.com/devscafecommunity) and are pinned by commit in this monorepo.

| Path | Repository | Role | Built from engine CMake? | Launcher `AppId` |
|------|------------|------|--------------------------|------------------|
| *(this repo)* | `caffeine` | C++ engine, **Doppio** editor (`apps/doppio`), runtime, tests | yes (`doppio`, `caffeine-runtime`, …) | `doppio`, `runtime` |
| `caf-pack/` | [caf-pack](https://github.com/devscafecommunity/caf-pack) | Raw assets → `.caf`, pack `.cap`; shared `CafTypes` with engine | linked into `doppio` when submodule present | — |
| `Convoy/` | [convoy](https://github.com/devscafecommunity/convoy) | Pixel art / atlas / animation tooling | `CAFFEINE_BUILD_CONVOY=ON` (default) | `convoy` |
| `WaveShaper/` | [waveshaper](https://github.com/devscafecommunity/waveshaper) | Synthesis + export WAV/CAF for games | `CAFFEINE_BUILD_WAVESHAPER=ON` (default **off**) | `waveshaper` |
| `caffeine-website/` | [caffeine-website](https://github.com/devscafecommunity/caffeine-website) | Public docs, i18n site, PDFs, **caffeine-docs** MCP | no (Node/npm) | — |
| `caffeine-launcher/` | [caffeine-launcher](https://github.com/devscafecommunity/caffeine-launcher) | Hub UI; discovers `build/doppio`, `~/.local/share/caffeine/apps/` | `cargo build` / `scripts/build-launcher.sh` | hosts all apps |

**Clone / refresh submodules:**

```bash
git clone --recurse-submodules git@github.com:devscafecommunity/caffeine.git
# or after a plain clone:
./scripts/submodules.sh
```

**Dev layout the launcher expects:** engine build dir `build/` with binaries such as `doppio`, `convoy`, `waveshaper`, `caffeine-runtime`; Doppio plugins under `build/plugins/`. Override with `CAFFEINE_ENGINE_ROOT`.

## Documentation policy

- **No Markdown documentation** in the engine tree (no `docs/**/*.md`, no planning RFCs here).
- **Specs and manuals**: Svelte/HTML in `caffeine-website/packages/docs-site`, or LaTeX under `caffeine-website/tex/` for PDFs.
- **Agents**: Use the **caffeine-docs MCP** from `caffeine-website` (`search_docs`, `get_doc`). Do not scrape removed paths from git history unless the user asks.
- **README.md** here is only a short entrypoint with build commands and a link to the site submodule.

## Implementation rules

1. **C++20**, CMake 3.20+, SDL3 — match existing style in the file you edit.
2. **Minimal diffs** — one concern per change; no drive-by refactors.
3. **Tests** — add or update tests under `tests/` when behavior changes; run `ctest` from the build dir.
4. **Submodules** — after clone, run `./scripts/submodules.sh` (or `git submodule update --init --recursive`). See **Ecosystem submodules** above.
5. **Editor vs runtime** — keep play-mode-only code out of editor-only paths unless shared APIs are intentional.
6. **Scripting / native modules** — follow public headers in `include/caffeine/`; document API changes in the website repo, not here.
7. **Commits** — only when the user requests; never commit secrets or local build artifacts.

## Build (quick)

```bash
./scripts/submodules.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest --output-on-failure
```

Cross-platform helpers: `scripts/build.sh`, `scripts/build.bat`, `scripts/build.fish`, `scripts/build-launcher.sh`.

**caffeine-website:** `cd caffeine-website && npm install && npm run dev`. Publish doc changes there; bump the submodule pointer in the engine when pinning a new docs revision.

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
| `caf-pack/`, `Convoy/`, `WaveShaper/` | Git submodules |
| `caffeine-website/` | Docs site (Svelte/HTML), TeX/PDF, docs MCP |

Do not add `docs/`, `planning/`, or `.planning/` back to this repo.

## Documentation policy

- **No Markdown documentation** in the engine tree (no `docs/**/*.md`, no planning RFCs here).
- **Specs and manuals**: Svelte/HTML in `caffeine-website/packages/docs-site`, or LaTeX under `caffeine-website/tex/` for PDFs.
- **Agents**: Use the **caffeine-docs MCP** from `caffeine-website` (`search_docs`, `get_doc`). Do not scrape removed paths from git history unless the user asks.
- **README.md** here is only a short entrypoint with build commands and a link to the site submodule.

## Implementation rules

1. **C++20**, CMake 3.20+, SDL3 — match existing style in the file you edit.
2. **Minimal diffs** — one concern per change; no drive-by refactors.
3. **Tests** — add or update tests under `tests/` when behavior changes; run `ctest` from the build dir.
4. **Submodules** — after clone, `git submodule update --init --recursive`.
5. **Editor vs runtime** — keep play-mode-only code out of editor-only paths unless shared APIs are intentional.
6. **Scripting / native modules** — follow public headers in `include/caffeine/`; document API changes in the website repo, not here.
7. **Commits** — only when the user requests; never commit secrets or local build artifacts.

## Build (quick)

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest --output-on-failure
```

Cross-platform helpers: `scripts/build.sh`, `scripts/build.bat`, `scripts/build.fish`.

## Submodule: caffeine-website

```bash
git submodule update --init caffeine-website
cd caffeine-website && npm install && npm run dev
```

Publish doc changes in that repository; bump the submodule pointer in the engine when the user wants the engine repo to pin a new docs revision.

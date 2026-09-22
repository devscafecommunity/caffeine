# Git Plugin

> **Plugin:** `git_plugin.so` (optional — not bundled by default)

Full Git workflow panel for the open Caffeine project. Requires `git` in PATH.

## Features

- Repository detection + `git init`
- **Changes** — porcelain status, stage/unstage per file or all
- **Commit** — message + commit
- **Branches** — checkout, create
- **Remotes** — fetch, pull, push
- **History** — recent log
- **Stash** — push, pop, list
- **Diff output** — per-file diff in output panel

## Open panel

Menu: **Version Control → Git Panel**, or enable plugin in **Plugins** manager.

## Project root

Uses `PluginHostApi::getProjectRootPath` — parent of `scenes/` from the open scene path.

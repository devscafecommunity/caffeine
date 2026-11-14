# Doppio C++ archive

Frozen snapshot of the legacy C++ Doppio editor, copied from `src/doppio/`,
`apps/doppio/`, `apps/build_smoke/` and their tests when the Rust/Iced frontend
replaced them.

The live CMake build no longer references these paths; this folder is a
behaviour reference for panel logic and migration, not a build target. Keep it
frozen: new editor work goes to `doppio/`.

The active rewrite lives in `doppio/`:

- `doppio/crates/engine` — FFI bindings to `libcaffeine-capi`
- `doppio/crates/ui` — components, panels and windows
- `doppio/docs/rust-ui.md` — architecture and migration status

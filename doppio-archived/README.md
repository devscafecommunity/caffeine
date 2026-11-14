# Doppio C++ archive

Frozen snapshot of the legacy C++ Doppio editor, copied from `src/doppio/` and
`apps/doppio/` when the Rust/Iced frontend started.

The live CMake build still references the original paths in `src/doppio/` and
`apps/doppio/` until the Rust frontend reaches parity. This folder is a
reference for panel behaviour and migration, not a second build target.

The active rewrite lives in `doppio/`:

- `doppio/crates/engine` — FFI bindings to `libcaffeine-capi`
- `doppio/crates/ui` — components, panels and windows

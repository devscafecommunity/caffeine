# 2D vector overlays (RHI path)

Play-mode debug and gameplay lines are batched in `Render::Overlay2D`.

## ImGui path (editor + current runtime)

When `CF_HAS_IMGUI` is defined, `Overlay2D::draw(ImDrawList*, ...)` renders after the world and before UI widgets.

## Shipping / no ImGui

1. Scripts record primitives via `caffeine.draw2d.*` (same buffer as ImGui).
2. `Overlay2D::buildRhiMesh(camera, screenOrigin, vertices)` tessellates lines and rect outlines into world-space triangles.
3. `Overlay2D::drawRhi(CommandBuffer*, camera, screenOrigin)` is the GPU submission entry point.

Shader sources live in `src/render/shaders/overlay2d.vert` and `overlay2d.frag`. Register them in the builtin shader list and `ShaderBytecode` when enabling GPU draw on your branch.

Until the pipeline is registered, tessellation still runs so gameplay logic can be tested; GPU upload is a no-op.

## Coordinate spaces

- **world** — values in scene units; transformed with `Camera2D`.
- **screen** — pixel offsets relative to the viewport origin; converted via `screenToWorld` for RHI meshes.

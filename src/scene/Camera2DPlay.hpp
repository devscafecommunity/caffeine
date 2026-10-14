#pragma once

#include "editor/EditorContext.hpp"
#include "render/Camera2D.hpp"
#include "render/RenderTypes.hpp"

namespace Caffeine::Scene {

/// Editor viewport pan/zoom (Mode2D) as a Camera2D for picking and overlays.
inline Render::Camera2D buildCamera2DFromEditorViewport(const Editor::EditorContext& ctx,
                                                      Render::Rect2D viewport) {
    Render::Camera2D cam;
    cam.setViewport(viewport);
    const f32 pixelsPerUnit = ctx.viewportZoom * 50.0f;
    const f32 halfW = viewport.size.x * 0.5f;
    const f32 halfH = viewport.size.y * 0.5f;
    const f32 worldX = (-ctx.viewportPanX) / pixelsPerUnit;
    const f32 worldY = ctx.viewportPanY / pixelsPerUnit;
    cam.setPosition({worldX, worldY});
    cam.setZoom(ctx.viewportZoom);
    return cam;
}

}  // namespace Caffeine::Scene

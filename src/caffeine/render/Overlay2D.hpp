#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"
#include "render/Camera2D.hpp"
#include "render/RenderTypes.hpp"

#include <vector>

struct ImDrawList;
struct ImVec2;

namespace Caffeine::RHI {
class CommandBuffer;
}

namespace Caffeine::Render {

enum class OverlaySpace : u8 { World, Screen };

struct OverlayColor {
    u8 r = 255;
    u8 g = 255;
    u8 b = 255;
    u8 a = 255;
};

/// Retained-mode vector overlay batch for play mode (cleared each frame).
class Overlay2D {
public:
    static void beginFrame();

    static void line(Vec2 a, Vec2 b, OverlayColor color, f32 thickness = 1.0f,
                     OverlaySpace space = OverlaySpace::World);
    static void circle(Vec2 center, f32 radius, OverlayColor color, f32 thickness = 1.0f,
                       int segments = 32, OverlaySpace space = OverlaySpace::World);
    static void circleFilled(Vec2 center, f32 radius, OverlayColor color, int segments = 32,
                             OverlaySpace space = OverlaySpace::World);
    static void rect(Vec2 min, Vec2 max, OverlayColor color, f32 thickness = 1.0f,
                     OverlaySpace space = OverlaySpace::World);
    static void polyline(const Vec2* points, u32 count, OverlayColor color, f32 thickness = 1.0f,
                         bool closed = false, OverlaySpace space = OverlaySpace::World);

#ifdef CF_HAS_IMGUI
    /// Draw after world sprites, before UI widgets.
    static void draw(ImDrawList* dl, const Camera2D& camera, ImVec2 screenOrigin, ImVec2 panelSize);
#endif

    struct RhiVertex {
        f32 x, y;
        f32 r, g, b, a;
    };

    /// Tessellate current frame for GPU overlay (no ImGui). World space uses camera + viewport origin.
    static void buildRhiMesh(const Camera2D& camera, Vec2 screenOrigin, std::vector<RhiVertex>& out);

#if defined(CF_HAS_SDL3) && !defined(CF_HEADLESS)
    static void drawRhi(RHI::CommandBuffer* cmd, const Camera2D& camera, Vec2 screenOrigin);
#endif

private:
    struct CmdLine {
        Vec2 a, b;
        OverlayColor color;
        f32 thickness;
        OverlaySpace space;
    };
    struct CmdCircle {
        Vec2 center;
        f32 radius;
        OverlayColor color;
        f32 thickness;
        int segments;
        bool filled;
        OverlaySpace space;
    };
    struct CmdPoly {
        std::vector<Vec2> points;
        OverlayColor color;
        f32 thickness;
        bool closed;
        OverlaySpace space;
    };
    struct CmdRect {
        Vec2 min, max;
        OverlayColor color;
        f32 thickness;
        OverlaySpace space;
    };

    static std::vector<CmdLine>& lines();
    static std::vector<CmdCircle>& circles();
    static std::vector<CmdPoly>& polys();
    static std::vector<CmdRect>& rects();
};

}  // namespace Caffeine::Render

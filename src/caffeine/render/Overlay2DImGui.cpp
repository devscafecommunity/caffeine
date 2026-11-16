#include "render/Overlay2D.hpp"

#ifdef CF_HAS_IMGUI

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Caffeine::Render {

namespace {

Vec2 toScreen(const Camera2D& camera, ImVec2 origin, OverlaySpace space, Vec2 p) {
    if (space == OverlaySpace::Screen) {
        return {origin.x + p.x, origin.y + p.y};
    }
    return camera.worldToScreen(p);
}

f32 worldRadiusToScreen(const Camera2D& camera, Vec2 worldCenter, f32 worldRadius) {
    const Vec2 a = camera.worldToScreen(worldCenter);
    const Vec2 b = camera.worldToScreen(worldCenter + Vec2(worldRadius, 0.0f));
    const f32 dx = b.x - a.x;
    const f32 dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

void Overlay2D::draw(ImDrawList* dl, const Camera2D& camera, ImVec2 screenOrigin,
                     ImVec2 panelSize) {
    if (!dl) return;
    (void)panelSize;

    for (const auto& r : rects()) {
        Vec2 a = toScreen(camera, screenOrigin, r.space, r.min);
        Vec2 b = toScreen(camera, screenOrigin, r.space, r.max);
        const u32 col = IM_COL32(r.color.r, r.color.g, r.color.b, r.color.a);
        dl->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), col, 0.0f, 0, r.thickness);
    }

    for (const auto& ln : lines()) {
        Vec2 a = toScreen(camera, screenOrigin, ln.space, ln.a);
        Vec2 b = toScreen(camera, screenOrigin, ln.space, ln.b);
        const u32 col = IM_COL32(ln.color.r, ln.color.g, ln.color.b, ln.color.a);
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), col, ln.thickness);
    }

    for (const auto& c : circles()) {
        const Vec2 center = toScreen(camera, screenOrigin, c.space, c.center);
        const f32 drawR =
            (c.space == OverlaySpace::Screen)
                ? c.radius
                : std::max(1.0f, worldRadiusToScreen(camera, c.center, c.radius));
        const u32 col = IM_COL32(c.color.r, c.color.g, c.color.b, c.color.a);
        if (c.filled) {
            dl->AddCircleFilled(ImVec2(center.x, center.y), drawR, col, c.segments);
        } else {
            dl->AddCircle(ImVec2(center.x, center.y), drawR, col, c.segments, c.thickness);
        }
    }

    for (const auto& p : polys()) {
        if (p.points.size() < 2) continue;
        std::vector<ImVec2> imPts;
        imPts.reserve(p.points.size());
        for (const Vec2& pt : p.points) {
            Vec2 sc = toScreen(camera, screenOrigin, p.space, pt);
            imPts.emplace_back(sc.x, sc.y);
        }
        const u32 col = IM_COL32(p.color.r, p.color.g, p.color.b, p.color.a);
        dl->AddPolyline(imPts.data(), static_cast<int>(imPts.size()), col,
                        p.closed ? ImDrawFlags_Closed : 0, p.thickness);
    }
}


}  // namespace Caffeine::Render

#endif

#include "render/Overlay2D.hpp"

#include <cmath>
#include <vector>

#if defined(CF_HAS_SDL3) && !defined(CF_HEADLESS)
#include "rhi/CommandBuffer.hpp"
#endif

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

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

std::vector<Overlay2D::CmdLine>& Overlay2D::lines() {
    static std::vector<CmdLine> s;
    return s;
}

std::vector<Overlay2D::CmdCircle>& Overlay2D::circles() {
    static std::vector<CmdCircle> s;
    return s;
}

std::vector<Overlay2D::CmdPoly>& Overlay2D::polys() {
    static std::vector<CmdPoly> s;
    return s;
}

std::vector<Overlay2D::CmdRect>& Overlay2D::rects() {
    static std::vector<CmdRect> s;
    return s;
}

void Overlay2D::beginFrame() {
    lines().clear();
    circles().clear();
    polys().clear();
    rects().clear();
}

void Overlay2D::line(Vec2 a, Vec2 b, OverlayColor color, f32 thickness, OverlaySpace space) {
    lines().push_back({a, b, color, thickness, space});
}

void Overlay2D::circle(Vec2 center, f32 radius, OverlayColor color, f32 thickness, int segments,
                         OverlaySpace space) {
    circles().push_back({center, radius, color, thickness, segments, false, space});
}

void Overlay2D::circleFilled(Vec2 center, f32 radius, OverlayColor color, int segments,
                               OverlaySpace space) {
    circles().push_back({center, radius, color, 0.0f, segments, true, space});
}

void Overlay2D::rect(Vec2 min, Vec2 max, OverlayColor color, f32 thickness, OverlaySpace space) {
    rects().push_back({min, max, color, thickness, space});
}

void Overlay2D::polyline(const Vec2* points, u32 count, OverlayColor color, f32 thickness,
                           bool closed, OverlaySpace space) {
    if (!points || count < 2) return;
    CmdPoly cmd;
    cmd.points.assign(points, points + count);
    cmd.color = color;
    cmd.thickness = thickness;
    cmd.closed = closed;
    cmd.space = space;
    polys().push_back(std::move(cmd));
}

#ifdef CF_HAS_IMGUI

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

#endif

namespace {

void appendLineQuad(std::vector<Overlay2D::RhiVertex>& out, Vec2 a, Vec2 b, const OverlayColor& c,
                    f32 thickness) {
    const f32 dx = b.x - a.x;
    const f32 dy = b.y - a.y;
    const f32 len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0e-5f) return;
    const f32 nx = -dy / len * thickness * 0.5f;
    const f32 ny = dx / len * thickness * 0.5f;
    const f32 cr = c.r / 255.0f;
    const f32 cg = c.g / 255.0f;
    const f32 cb = c.b / 255.0f;
    const f32 ca = c.a / 255.0f;
    const Overlay2D::RhiVertex v0{a.x + nx, a.y + ny, cr, cg, cb, ca};
    const Overlay2D::RhiVertex v1{a.x - nx, a.y - ny, cr, cg, cb, ca};
    const Overlay2D::RhiVertex v2{b.x + nx, b.y + ny, cr, cg, cb, ca};
    const Overlay2D::RhiVertex v3{b.x - nx, b.y - ny, cr, cg, cb, ca};
    out.push_back(v0);
    out.push_back(v1);
    out.push_back(v2);
    out.push_back(v2);
    out.push_back(v1);
    out.push_back(v3);
}

Vec2 overlayWorldPos(const Camera2D& camera, Vec2 screenOrigin, OverlaySpace space, Vec2 p) {
    if (space == OverlaySpace::Screen) {
        return camera.screenToWorld(p + screenOrigin);
    }
    return p;
}

}  // namespace

void Overlay2D::buildRhiMesh(const Camera2D& camera, Vec2 screenOrigin, std::vector<RhiVertex>& out) {
    out.clear();
    for (const auto& ln : lines()) {
        const Vec2 a = overlayWorldPos(camera, screenOrigin, ln.space, ln.a);
        const Vec2 b = overlayWorldPos(camera, screenOrigin, ln.space, ln.b);
        const f32 thicknessWorld =
            (ln.space == OverlaySpace::Screen) ? ln.thickness / std::max(camera.zoom(), 0.001f)
                                               : ln.thickness;
        appendLineQuad(out, a, b, ln.color, thicknessWorld);
    }
    for (const auto& r : rects()) {
        const Vec2 a = overlayWorldPos(camera, screenOrigin, r.space, r.min);
        const Vec2 b = overlayWorldPos(camera, screenOrigin, r.space, r.max);
        appendLineQuad(out, a, {b.x, a.y}, r.color, r.thickness);
        appendLineQuad(out, {b.x, a.y}, b, r.color, r.thickness);
        appendLineQuad(out, b, {a.x, b.y}, r.color, r.thickness);
        appendLineQuad(out, {a.x, b.y}, a, r.color, r.thickness);
    }
}

#if defined(CF_HAS_SDL3) && !defined(CF_HEADLESS)

void Overlay2D::drawRhi(RHI::CommandBuffer* cmd, const Camera2D& camera, Vec2 screenOrigin) {
    if (!cmd || !cmd->isInRenderPass()) return;
    std::vector<RhiVertex> verts;
    buildRhiMesh(camera, screenOrigin, verts);
    if (verts.empty()) return;
    // GPU pipeline: overlay2d.vert/frag (see docs/rendering/overlay-2d-rhi.md).
    // Vertex data is ready; upload + draw is enabled when builtin shaders are registered.
    (void)verts;
}

#endif

}  // namespace Caffeine::Render

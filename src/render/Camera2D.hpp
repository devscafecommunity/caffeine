#pragma once

#include "RenderTypes.hpp"
#include "../math/Mat4.hpp"
#include "../math/Math.hpp"
#include "../ecs/Entity.hpp"
#include "../ecs/World.hpp"
#include "../ecs/Components.hpp"
#include <cstdlib>
#include <cmath>

namespace Caffeine::Render {

using namespace Caffeine;

// ============================================================================
// Camera2D
// ============================================================================
class Camera2D {
public:
    Camera2D() = default;

    // ── Matrices ──────────────────────────────────────────────────────────────

    Mat4 viewMatrix() const {
        return calculateViewMatrix();
    }

    Mat4 projectionMatrix() const {
        return calculateProjectionMatrix();
    }

    /// Returns the cached view-projection matrix; recalculates only when dirty.
    Mat4 viewProjectionMatrix() const {
        if (m_dirty) {
            m_cachedViewProj = calculateProjectionMatrix() * calculateViewMatrix();
            m_dirty = false;
        }
        return m_cachedViewProj;
    }

    // ── Space conversion ──────────────────────────────────────────────────────

    /// Transform a world-space point to screen pixels.
    Vec2 worldToScreen(Vec2 worldPos) const {
        Mat4 vp = viewProjectionMatrix();
        Vec3 p  = vp.transformPoint({ worldPos.x, worldPos.y, 0.0f });
        // NDC → screen (Y flipped: NDC +Y = up, screen +Y = down)
        f32 sx = (p.x + 1.0f) * 0.5f * m_viewport.size.x + m_viewport.position.x;
        f32 sy = (1.0f - p.y) * 0.5f * m_viewport.size.y + m_viewport.position.y;
        return { sx, sy };
    }

    /// Transform screen pixels back to world-space.
    Vec2 screenToWorld(Vec2 screenPos) const {
        f32 halfW = m_viewport.size.x * 0.5f / m_zoom;
        f32 halfH = m_viewport.size.y * 0.5f / m_zoom;

        // Screen → NDC
        f32 ndcX = (screenPos.x - m_viewport.position.x) / m_viewport.size.x * 2.0f - 1.0f;
        f32 ndcY = 1.0f - (screenPos.y - m_viewport.position.y) / m_viewport.size.y * 2.0f;

        // NDC → camera space
        f32 camX = ndcX * halfW;
        f32 camY = ndcY * halfH;

        // Camera → world (inverse of rotZ(-θ), then add effective position)
        f32 rad = Math::degToRad(m_rotation);
        f32 c   = cosf(rad);
        f32 s   = sinf(rad);

        Vec2 ep = { m_position.x + m_shakeOffset.x, m_position.y + m_shakeOffset.y };
        return {
            c * camX - s * camY + ep.x,
            s * camX + c * camY + ep.y
        };
    }

    /// Returns true when worldRect overlaps the camera frustum (AABB test).
    bool isVisible(Rect2D worldRect) const {
        Vec2 camHalf = { m_viewport.size.x * 0.5f / m_zoom,
                         m_viewport.size.y * 0.5f / m_zoom };
        Vec2 ep = { m_position.x + m_shakeOffset.x, m_position.y + m_shakeOffset.y };

        f32 camLeft   = ep.x - camHalf.x;
        f32 camRight  = ep.x + camHalf.x;
        f32 camBottom = ep.y - camHalf.y;
        f32 camTop    = ep.y + camHalf.y;

        f32 objLeft   = worldRect.position.x;
        f32 objRight  = worldRect.position.x + worldRect.size.x;
        f32 objBottom = worldRect.position.y;
        f32 objTop    = worldRect.position.y + worldRect.size.y;

        return !(objRight  < camLeft  ||
                 objLeft   > camRight ||
                 objTop    < camBottom||
                 objBottom > camTop);
    }

    // ── Configuration ─────────────────────────────────────────────────────────

    void setPosition(Vec2 pos) {
        m_position = m_hasBounds ? applyBounds(pos) : pos;
        m_dirty    = true;
    }

    void setZoom(f32 zoom) {
        m_zoom  = zoom > 0.0f ? zoom : 0.0001f;
        m_dirty = true;
    }

    void setRotation(f32 degrees) {
        m_rotation = degrees;
        m_dirty    = true;
    }

    void setViewport(Rect2D viewport) {
        m_viewport = viewport;
        m_dirty    = true;
    }

    // ── Follow ────────────────────────────────────────────────────────────────

    void follow(ECS::Entity target, f32 smoothing = 0.1f) {
        m_followTarget    = target;
        m_followSmoothing = smoothing;
    }

    void stopFollowing() {
        m_followTarget = ECS::Entity::INVALID;
    }

    /// Call once per frame. Advances follow lerp and shake decay.
    void update(f64 dt, const ECS::World& world) {
        if (m_followTarget.isValid()) {
            const auto* pos = world.get<ECS::Position2D>(m_followTarget);
            if (pos) {
                m_position.x = Math::lerp(m_position.x, pos->x, m_followSmoothing);
                m_position.y = Math::lerp(m_position.y, pos->y, m_followSmoothing);
                if (m_hasBounds) {
                    m_position = applyBounds(m_position);
                }
                m_dirty = true;
            }
        }

        if (m_shakeTime > 0.0f) {
            m_shakeTime -= static_cast<f32>(dt);
            if (m_shakeTime <= 0.0f) {
                m_shakeTime   = 0.0f;
                m_shakeOffset = { 0.0f, 0.0f };
            } else {
                f32 t     = m_shakeTime / m_shakeDuration;
                f32 randX = (static_cast<f32>(rand()) / static_cast<f32>(RAND_MAX)) * 2.0f - 1.0f;
                f32 randY = (static_cast<f32>(rand()) / static_cast<f32>(RAND_MAX)) * 2.0f - 1.0f;
                m_shakeOffset = { m_shakeIntensity.x * t * randX,
                                  m_shakeIntensity.y * t * randY };
            }
            m_dirty = true;
        }
    }

    // ── Shake ─────────────────────────────────────────────────────────────────

    /// Trigger a camera shake. intensity = max offset in world units, duration in seconds.
    void shake(Vec2 intensity, f32 duration) {
        m_shakeIntensity = intensity;
        m_shakeDuration  = duration;
        m_shakeTime      = duration;
        m_dirty          = true;
    }

    // ── Bounds ────────────────────────────────────────────────────────────────

    void setBounds(Rect2D worldBounds) {
        m_worldBounds = worldBounds;
        m_hasBounds   = true;
        m_position    = applyBounds(m_position);
        m_dirty       = true;
    }

    void clearBounds() {
        m_hasBounds = false;
    }

    // ── Getters ───────────────────────────────────────────────────────────────

    Vec2   position() const { return m_position; }
    f32    zoom()     const { return m_zoom; }
    f32    rotation() const { return m_rotation; }
    Rect2D viewport() const { return m_viewport; }

    /// World-space visible area dimensions (viewport / zoom).
    Vec2 size() const {
        return { m_viewport.size.x / m_zoom, m_viewport.size.y / m_zoom };
    }

private:
    // ── Internal helpers ──────────────────────────────────────────────────────

    Mat4 calculateViewMatrix() const {
        Vec2 ep  = { m_position.x + m_shakeOffset.x, m_position.y + m_shakeOffset.y };
        f32  rad = Math::degToRad(m_rotation);
        // View = rotZ(-θ) * translate(-pos)
        return Mat4::rotationZ(-rad) * Mat4::translation(-ep.x, -ep.y, 0.0f);
    }

    Mat4 calculateProjectionMatrix() const {
        f32 halfW = m_viewport.size.x * 0.5f / m_zoom;
        f32 halfH = m_viewport.size.y * 0.5f / m_zoom;
        return Mat4::ortho(-halfW, halfW, -halfH, halfH, -1000.0f, 1000.0f);
    }

    Vec2 applyBounds(Vec2 pos) const {
        f32 halfW = m_viewport.size.x * 0.5f / m_zoom;
        f32 halfH = m_viewport.size.y * 0.5f / m_zoom;
        pos.x = Math::clamp(pos.x,
            m_worldBounds.position.x + halfW,
            m_worldBounds.position.x + m_worldBounds.size.x - halfW);
        pos.y = Math::clamp(pos.y,
            m_worldBounds.position.y + halfH,
            m_worldBounds.position.y + m_worldBounds.size.y - halfH);
        return pos;
    }

    // ── State ─────────────────────────────────────────────────────────────────

    Vec2   m_position { 0.0f, 0.0f };
    f32    m_zoom     { 1.0f };
    f32    m_rotation { 0.0f };
    Rect2D m_viewport {};  // default: position={0,0}, size={1280,720}

    ECS::Entity m_followTarget    { ECS::Entity::INVALID };
    f32         m_followSmoothing { 0.1f };

    Vec2 m_shakeIntensity { 0.0f, 0.0f };
    f32  m_shakeTime      { 0.0f };
    f32  m_shakeDuration  { 0.0f };
    Vec2 m_shakeOffset    { 0.0f, 0.0f };

    bool   m_hasBounds   { false };
    Rect2D m_worldBounds {};

    mutable Mat4 m_cachedViewProj;
    mutable bool m_dirty { true };
};

}  // namespace Caffeine::Render

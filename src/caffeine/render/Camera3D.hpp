#pragma once

#include "RenderTypes.hpp"
#include "../math/Vec2.hpp"
#include "../math/Vec3.hpp"
#include "../math/Mat4.hpp"
#include "../math/Quat.hpp"
#include "../math/Math.hpp"
#include "../spatial/Octree.hpp"
#include "../ecs/Entity.hpp"
#include "../ecs/World.hpp"
#include "../ecs/Components3D.hpp"
#include <cmath>

namespace Caffeine::Render {

using namespace Caffeine;

// ============================================================================
// @brief  Câmera 3D com projeção perspectiva.
//
//  Modos:
//  - FPS: mouse look, WASD movement
//  - Orbital: rotação em torno de um ponto (editor, estratégia)
//  - Follow: segue entidade com offset (terceira pessoa)
// ============================================================================
class Camera3D {
public:
    Camera3D() = default;

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
            m_cachedVP = calculateProjectionMatrix() * calculateViewMatrix();
            m_dirty = false;
        }
        return m_cachedVP;
    }

    // ── Frustum ──────────────────────────────────────────────────────────────

    /// Build frustum from current camera state using fromCamera().
    Spatial::Frustum frustum() const {
        Vec3 fwd = forward();
        Vec3 pos = effectivePosition();
        return Spatial::Frustum::fromCamera(
            pos, pos + fwd, Vec3::up(),
            Math::degToRad(m_fovY), m_aspect, m_near, m_far
        );
    }

    // ── Space conversion ─────────────────────────────────────────────────────

    /// Transform a world-space point to screen pixels + depth.
    Vec3 worldToScreen(Vec3 worldPos) const {
        Mat4 vp = viewProjectionMatrix();
        Vec3 p  = vp.transformPoint(worldPos);
        // NDC → screen (Y flipped: NDC +Y = up, screen +Y = down)
        f32 sx = (p.x + 1.0f) * 0.5f * m_viewport.size.x + m_viewport.position.x;
        f32 sy = (1.0f - p.y) * 0.5f * m_viewport.size.y + m_viewport.position.y;
        f32 sz = (p.z + 1.0f) * 0.5f; // depth 0-1
        return { sx, sy, sz };
    }

    /// Transform screen pixels back to world-space at the given depth.
    Vec3 screenToWorld(Vec2 screenPos, f32 depth = 0.5f) const {
        // Screen → NDC
        f32 ndcX = (screenPos.x - m_viewport.position.x) / m_viewport.size.x * 2.0f - 1.0f;
        f32 ndcY = 1.0f - (screenPos.y - m_viewport.position.y) / m_viewport.size.y * 2.0f;
        f32 ndcZ = depth * 2.0f - 1.0f;

        // Unproject via inverse VP
        Mat4 invVP = viewProjectionMatrix().inverted();
        return invVP.transformPoint({ ndcX, ndcY, ndcZ });
    }

    /// Returns true when bounds overlaps the camera frustum.
    bool isVisible(const Spatial::AABB3D& bounds) const {
        return frustum().intersects(bounds);
    }

    // ── Perspective configuration ────────────────────────────────────────────

    void setFOV(f32 fovYDegrees) {
        m_fovY  = Math::clamp(fovYDegrees, 1.0f, 179.0f);
        m_dirty = true;
    }

    void setAspect(f32 aspect) {
        m_aspect = (aspect > 0.0f) ? aspect : 0.0001f;
        m_dirty  = true;
    }

    void setNearFar(f32 nearPlane, f32 farPlane) {
        m_near  = (nearPlane > 0.0f) ? nearPlane : 0.1f;
        m_far   = (farPlane > m_near) ? farPlane : m_near + 0.1f;
        m_dirty = true;
    }

    void setViewport(Rect2D viewport) {
        m_viewport = viewport;
        m_dirty    = true;
    }

    // ── Transform ────────────────────────────────────────────────────────────

    void setPosition(Vec3 pos) {
        m_position = pos;
        syncRotationToAngles();
        m_dirty = true;
    }

    void setRotation(Quat rot) {
        m_rotation = rot;
        syncRotationToAngles();
        m_dirty = true;
    }

    void lookAt(Vec3 eye, Vec3 target, Vec3 up = {0, 1, 0}) {
        m_position = eye;
        Vec3 fwd = (target - eye).normalized();
        if (fwd.lengthSquared() < 1e-8f) fwd = {0, 0, 1};
        m_rotation = Quat::lookAt(fwd, up);
        syncRotationToAngles();
        m_dirty = true;
    }

    // ── FPS mode ─────────────────────────────────────────────────────────────

    /// Rotate camera by delta pitch/yaw in degrees. Pitch clamped to [-89, +89].
    void rotateFPS(f32 deltaPitch, f32 deltaYaw) {
        m_pitch += deltaPitch;
        m_yaw   += deltaYaw;
        m_pitch  = Math::clamp(m_pitch, -89.0f, 89.0f);
        syncAnglesToRotation();
        m_dirty = true;
    }

    /// Move camera in local space (forward = +Z, right = +X, up = +Y).
    void moveFPS(Vec3 localDelta) {
        m_position = m_position + right() * localDelta.x
                                 + Vec3::up() * localDelta.y
                                 + forward() * localDelta.z;
        m_dirty = true;
    }

    // ── Orbital mode ─────────────────────────────────────────────────────────

    /// Orbit around the orbit target by delta azimuth/elevation in degrees.
    void orbit(f32 deltaAzimuth, f32 deltaElevation) {
        m_azimuth   += deltaAzimuth;
        m_elevation += deltaElevation;
        m_elevation  = Math::clamp(m_elevation, -89.0f, 89.0f);
        updateOrbitalPosition();
        m_dirty = true;
    }

    void setOrbitTarget(Vec3 target) {
        m_orbitTarget = target;
        updateOrbitalPosition();
        m_dirty = true;
    }

    void setOrbitDistance(f32 distance) {
        m_orbitDistance = distance > 0.0f ? distance : 0.001f;
        updateOrbitalPosition();
        m_dirty = true;
    }

    void zoom(f32 delta) {
        setOrbitDistance(m_orbitDistance + delta);
    }

    // ── Follow mode ──────────────────────────────────────────────────────────

    /// Follow an entity with a local-space offset and lerp smoothing.
    void follow(ECS::Entity target, Vec3 offset = {0, 2, -5}, f32 smoothing = 0.05f) {
        m_followTarget    = target;
        m_followOffset    = offset;
        m_followSmoothing = smoothing;
        m_dirty           = true;
    }

    void stopFollowing() {
        m_followTarget = ECS::Entity::INVALID;
    }

    /// Call once per frame. Advances follow lerp.
    void update(f64 dt, const ECS::World& world) {
        (void)dt;
        if (m_followTarget.isValid()) {
            const auto* pos = world.get<ECS::Position3D>(m_followTarget);
            if (pos) {
                Vec3 target = pos->position + m_followOffset;
                m_position = Vec3(
                    Math::lerp(m_position.x, target.x, m_followSmoothing),
                    Math::lerp(m_position.y, target.y, m_followSmoothing),
                    Math::lerp(m_position.z, target.z, m_followSmoothing)
                );
                syncRotationToAngles();
                m_dirty = true;
            }
        }
    }

    // ── Getters ──────────────────────────────────────────────────────────────

    Vec3   position() const { return m_position; }
    Quat   rotation() const { return m_rotation; }
    f32    fov()       const { return m_fovY; }
    f32    aspect()    const { return m_aspect; }
    f32    nearPlane() const { return m_near; }
    f32    farPlane()  const { return m_far; }
    Rect2D viewport()  const { return m_viewport; }

    Vec3 forward() const {
        return m_rotation.rotate({0, 0, 1});
    }

    Vec3 right() const {
        return m_rotation.rotate({1, 0, 0});
    }

    Vec3 up() const {
        return m_rotation.rotate({0, 1, 0});
    }

private:
    // ── Matrix construction ───────────────────────────────────────────────────

    Mat4 calculateViewMatrix() const {
        // View = rotation⁻¹ * translation⁻¹
        // Since Quat is unit, inverse = conjugate
        Mat4 rot   = m_rotation.conjugate().toMatrix();
        Vec3 ep    = effectivePosition();
        Mat4 trans = Mat4::translation(-ep.x, -ep.y, -ep.z);
        return rot * trans;
    }

    Mat4 calculateProjectionMatrix() const {
        return Mat4::perspective(Math::degToRad(m_fovY), m_aspect, m_near, m_far);
    }

    Vec3 effectivePosition() const {
        return m_position;
    }

    // ── Rotation ↔ Euler sync ────────────────────────────────────────────────

    void syncRotationToAngles() {
        Vec3 euler = m_rotation.toEuler();
        m_pitch = Math::radToDeg(euler.x);
        m_yaw   = Math::radToDeg(euler.y);
    }

    void syncAnglesToRotation() {
        m_rotation = Quat::fromEuler(
            Math::degToRad(m_pitch),
            Math::degToRad(m_yaw),
            0.0f // roll always 0 for FPS/orbital
        );
    }

    // ── Orbital helper ───────────────────────────────────────────────────────

    void updateOrbitalPosition() {
        f32 azRad   = Math::degToRad(m_azimuth);
        f32 elRad   = Math::degToRad(m_elevation);
        Vec3 offset = {
            cosf(elRad) * sinf(azRad) * m_orbitDistance,
            sinf(elRad) * m_orbitDistance,
            cosf(elRad) * cosf(azRad) * m_orbitDistance
        };
        m_position = m_orbitTarget + offset;
        // Look at target
        Vec3 fwd = (m_orbitTarget - m_position).normalized();
        if (fwd.lengthSquared() > 1e-8f) {
            m_rotation = Quat::lookAt(fwd, Vec3::up());
        }
        syncRotationToAngles();
    }

    // ── State ────────────────────────────────────────────────────────────────

    Vec3   m_position { 0.0f, 0.0f, -5.0f };
    Quat   m_rotation { Quat::identity() };
    f32    m_fovY     { 60.0f };
    f32    m_aspect   { 16.0f / 9.0f };
    f32    m_near     { 0.1f };
    f32    m_far      { 1000.0f };
    Rect2D m_viewport {};

    // FPS
    f32 m_pitch { 0.0f };
    f32 m_yaw   { 0.0f };

    // Follow
    ECS::Entity m_followTarget    { ECS::Entity::INVALID };
    Vec3         m_followOffset    { 0.0f, 2.0f, -5.0f };
    f32          m_followSmoothing { 0.05f };

    // Orbital
    Vec3 m_orbitTarget   { 0.0f, 0.0f, 0.0f };
    f32  m_orbitDistance { 5.0f };
    f32  m_azimuth       { 0.0f };
    f32  m_elevation     { 30.0f };

    mutable Mat4 m_cachedVP;
    mutable bool m_dirty { true };
};

} // namespace Caffeine::Render

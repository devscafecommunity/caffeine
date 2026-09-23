#include "scene/LightingSystem.hpp"

#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/Components3D.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Scene {

namespace {

constexpr f32 kDegToRad = 3.14159265f / 180.0f;

Mat4 buildLocalMatrix(const ECS::Transform& t) {
    return Mat4::translation(t.position) * Mat4::rotationZ(t.rotation.z * kDegToRad) *
           Mat4::rotationY(t.rotation.y * kDegToRad) * Mat4::rotationX(t.rotation.x * kDegToRad) *
           Mat4::scale(t.scale.x, t.scale.y, t.scale.z);
}

Mat4 buildLocalMatrix3D(const ECS::Position3D* p, const ECS::Rotation3D* r, const ECS::Scale3D* s) {
    Mat4 T = p ? Mat4::translation(p->position) : Mat4::identity();
    Mat4 R = r ? Quat(r->quaternion.x, r->quaternion.y, r->quaternion.z, r->quaternion.w)
                     .normalized()
                     .toMatrix()
               : Mat4::identity();
    Mat4 S = s ? Mat4::scale(s->scale.x, s->scale.y, s->scale.z) : Mat4::identity();
    return T * R * S;
}

Mat4 entityMatrix(ECS::World& world, ECS::Entity entity) {
    if (auto* wt = world.get<WorldTransform>(entity)) return wt->matrix;
    if (auto* t = world.get<ECS::Transform>(entity)) return buildLocalMatrix(*t);
    auto* p3 = world.get<ECS::Position3D>(entity);
    auto* r3 = world.get<ECS::Rotation3D>(entity);
    auto* s3 = world.get<ECS::Scale3D>(entity);
    return buildLocalMatrix3D(p3, r3, s3);
}

Vec3 entityForward(ECS::World& world, ECS::Entity entity) {
    const Mat4 m = entityMatrix(world, entity);
    Vec3 axis(m(0, 2), m(1, 2), m(2, 2));
    const f32 lenSq = axis.lengthSquared();
    if (lenSq > 1e-6f) axis = axis / std::sqrt(lenSq);
    return -1.0f * axis;
}

bool tryGetEntityPosition(ECS::World& world, ECS::Entity entity, Vec3& outPosition) {
    if (auto* wt = world.get<WorldTransform>(entity)) {
        outPosition = wt->matrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
        return true;
    }
    if (auto* t = world.get<ECS::Transform>(entity)) {
        outPosition = t->position;
        return true;
    }
    if (auto* p3 = world.get<ECS::Position3D>(entity)) {
        outPosition = p3->position;
        return true;
    }
    return false;
}

Vec3 clampLighting(const Vec3& v, f32 minVal, f32 maxVal) {
    return Vec3(std::clamp(v.x, minVal, maxVal), std::clamp(v.y, minVal, maxVal),
                  std::clamp(v.z, minVal, maxVal));
}

}  // namespace

Vec3 defaultSunDirection() {
    return Vec3(0.35f, -0.82f, 0.45f).normalized();
}

bool directionalLightHasExplicitAim(ECS::World& world, ECS::Entity entity) {
    if (auto* r = world.get<ECS::Rotation3D>(entity)) {
        if (std::abs(r->quaternion.x) > 1e-4f || std::abs(r->quaternion.y) > 1e-4f ||
            std::abs(r->quaternion.z) > 1e-4f || std::abs(r->quaternion.w - 1.0f) > 1e-4f) {
            return true;
        }
    }
    if (auto* t = world.get<ECS::Transform>(entity)) {
        if (t->rotation.lengthSquared() > 1e-6f) return true;
    }
    return false;
}

void applyDefaultSunOrientation(ECS::World& world, ECS::Entity entity) {
    const Vec3 sun = defaultSunDirection();
    const Quat q = Quat::lookAt(-1.0f * sun, Vec3(0.0f, 1.0f, 0.0f));
    ECS::Rotation3D* rotation = world.get<ECS::Rotation3D>(entity);
    if (!rotation) rotation = &world.add<ECS::Rotation3D>(entity);
    rotation->quaternion = Vec4(q.x, q.y, q.z, q.w);

    if (auto* t = world.get<ECS::Transform>(entity)) {
        const Vec3 euler = q.toEuler();
        constexpr f32 kRadToDeg = 180.0f / 3.14159265f;
        t->rotation = Vec3(euler.x * kRadToDeg, euler.y * kRadToDeg, euler.z * kRadToDeg);
    }
}

void LightingData::clear() {
    directionals.clear();
    points.clear();
    spots.clear();
}

void SceneShadowMaps::clear() {
    directionals.clear();
    spots.clear();
}

void SceneLighting::clear() {
    lights.clear();
    shadows.clear();
}

void collectSceneLights(ECS::World& world, LightingData& out) {
    out.clear();

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::DirectionalLightComponent>();
        world.forEach<ECS::LightComponent, ECS::DirectionalLightComponent>(
            q, [&](ECS::Entity e, ECS::LightComponent& lc, ECS::DirectionalLightComponent& dl) {
                if (isEffectivelyDisabled(world, e)) return;
                Vec3 dir = entityForward(world, e).normalized();
                if (!directionalLightHasExplicitAim(world, e)) {
                    dir = defaultSunDirection();
                }
                out.directionals.push_back(
                    {dir, lc.color, lc.intensity, dl.shadowDistance, dl.castShadows});
            });
    }

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::PointLightComponent>();
        world.forEach<ECS::LightComponent, ECS::PointLightComponent>(
            q, [&](ECS::Entity e, ECS::LightComponent& lc, ECS::PointLightComponent& pl) {
                if (isEffectivelyDisabled(world, e)) return;
                Vec3 pos;
                if (!tryGetEntityPosition(world, e, pos)) return;
                out.points.push_back({pos, lc.color, lc.intensity, pl.radius, pl.castShadows});
            });
    }

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::SpotLightComponent>();
        world.forEach<ECS::LightComponent, ECS::SpotLightComponent>(
            q, [&](ECS::Entity e, ECS::LightComponent& lc, ECS::SpotLightComponent& sl) {
                if (isEffectivelyDisabled(world, e)) return;
                Vec3 pos;
                if (!tryGetEntityPosition(world, e, pos)) return;
                const Vec3 dir = entityForward(world, e).normalized();
                out.spots.push_back({pos, dir, lc.color, lc.intensity, sl.radius, sl.angle,
                                     sl.castShadows});
            });
    }
}

void buildSceneShadowMaps(ECS::World& world, const LightingData& lights, const Vec3& focus,
                          const std::string& projectRoot, SceneShadowMaps& out,
                          ECS::Entity skipEntity) {
    out.clear();
    out.directionals.resize(lights.directionals.size());
    out.spots.resize(lights.spots.size());

    for (size_t i = 0; i < lights.directionals.size(); ++i) {
        if (lights.directionals[i].castShadows) {
            buildDirectionalShadowMap(world, out.directionals[i], lights.directionals[i], focus,
                                      projectRoot, skipEntity);
        }
    }

    for (size_t i = 0; i < lights.spots.size(); ++i) {
        if (lights.spots[i].castShadows) {
            buildSpotShadowMap(world, out.spots[i], lights.spots[i], projectRoot, skipEntity);
        }
    }
}

void gatherSceneLighting(ECS::World& world, const Vec3& focus, const std::string& projectRoot,
                         SceneLighting& out, ECS::Entity skipEntity, bool buildCpuShadowMaps) {
    out.clear();
    collectSceneLights(world, out.lights);
    if (buildCpuShadowMaps) {
        buildSceneShadowMaps(world, out.lights, focus, projectRoot, out.shadows, skipEntity);
    }
}

Vec3 evaluateDiffuseLighting(const LightingData& lights, const SceneShadowMaps& shadows,
                             const Vec3& worldPos, const Vec3& worldNormal, bool receiveShadows,
                             const Vec3& ambient) {
    const Vec3 nn = worldNormal.normalized();
    Vec3 diffuse = ambient;

    for (size_t i = 0; i < lights.directionals.size(); ++i) {
        const auto& l = lights.directionals[i];
        const f32 ndotl = std::max(0.0f, nn.dot(-1.0f * l.direction));
        f32 shadow = 1.0f;
        if (receiveShadows && l.castShadows && i < shadows.directionals.size() &&
            shadows.directionals[i].valid) {
            shadow = shadows.directionals[i].samplePcf(worldPos);
        }
        diffuse += Vec3(l.color.x, l.color.y, l.color.z) * (ndotl * l.intensity * 0.65f * shadow);
    }

    for (const auto& l : lights.points) {
        Vec3 toLight = l.position - worldPos;
        const f32 dist = std::max(0.001f, toLight.length());
        if (dist > l.radius) continue;
        const Vec3 ldir = toLight / dist;
        const f32 atten = 1.0f - (dist / l.radius);
        const f32 ndotl = std::max(0.0f, nn.dot(ldir));
        diffuse += Vec3(l.color.x, l.color.y, l.color.z) *
                   (ndotl * l.intensity * atten * atten * 0.9f);
    }

    for (size_t i = 0; i < lights.spots.size(); ++i) {
        const auto& l = lights.spots[i];
        Vec3 toPoint = worldPos - l.position;
        const f32 dist = std::max(0.001f, toPoint.length());
        if (dist > l.radius) continue;
        const Vec3 fromLight = toPoint / dist;
        const f32 cone = l.direction.dot(fromLight);
        const f32 halfAngle = std::clamp(l.angle * kDegToRad * 0.5f, 0.01f, 1.5533f);
        const f32 cosHalfAngle = std::cos(halfAngle);
        if (cone < cosHalfAngle) continue;

        const f32 spotAtten =
            (cone - cosHalfAngle) / std::max(0.0001f, 1.0f - cosHalfAngle);
        const f32 rangeAtten = 1.0f - (dist / l.radius);
        const f32 ndotl = std::max(0.0f, nn.dot(-1.0f * fromLight));

        f32 shadow = 1.0f;
        if (receiveShadows && l.castShadows && i < shadows.spots.size() &&
            shadows.spots[i].valid) {
            shadow = shadows.spots[i].samplePcf(worldPos);
        }

        diffuse += Vec3(l.color.x, l.color.y, l.color.z) *
                   (ndotl * l.intensity * spotAtten * rangeAtten * rangeAtten * shadow);
    }

    return clampLighting(diffuse, 0.06f, 1.65f);
}

}  // namespace Caffeine::Scene

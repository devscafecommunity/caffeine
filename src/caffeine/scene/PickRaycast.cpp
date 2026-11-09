#include "scene/PickQuery.hpp"

#include "assets/MeshCache.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/MeshGeometry.hpp"
#include "ecs/TerrainComponents.hpp"
#include "effects/EffectSystem.hpp"
#include "effects/EffectTypes.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainSculptor.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Scene {

namespace {

constexpr f32 kDegToRad = 3.14159265f / 180.0f;

Mat4 entityMatrix(ECS::World& world, ECS::Entity entity) {
    if (auto* wt = world.get<WorldTransform>(entity)) return wt->matrix;
    if (auto* t = world.get<ECS::Transform>(entity)) {
        return Mat4::translation(t->position) * Mat4::rotationZ(t->rotation.z * kDegToRad) *
               Mat4::rotationY(t->rotation.y * kDegToRad) * Mat4::rotationX(t->rotation.x * kDegToRad) *
               Mat4::scale(t->scale.x, t->scale.y, t->scale.z);
    }
    auto* p3 = world.get<ECS::Position3D>(entity);
    auto* r3 = world.get<ECS::Rotation3D>(entity);
    auto* s3 = world.get<ECS::Scale3D>(entity);
    Mat4 T = p3 ? Mat4::translation(p3->position) : Mat4::identity();
    Mat4 R = r3 ? Quat(r3->quaternion.x, r3->quaternion.y, r3->quaternion.z, r3->quaternion.w)
                        .normalized()
                        .toMatrix()
                  : Mat4::identity();
    Mat4 S = s3 ? Mat4::scale(s3->scale.x, s3->scale.y, s3->scale.z) : Mat4::identity();
    return T * R * S;
}

bool tryGetEntityPosition(ECS::World& world, ECS::Entity entity, Vec3& out) {
    if (auto* wt = world.get<WorldTransform>(entity)) {
        out = wt->matrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
        return true;
    }
    if (auto* t = world.get<ECS::Transform>(entity)) {
        out = t->position;
        return true;
    }
    if (auto* p3 = world.get<ECS::Position3D>(entity)) {
        out = p3->position;
        return true;
    }
    return false;
}

}  // namespace

f32 rayIntersectsAABB(const Vec3& rayOrigin, const Vec3& rayDir, const Vec3& aabbMin,
                      const Vec3& aabbMax) {
    f32 t_enter = 0.0f;
    f32 t_exit = 1e10f;

    const f32 origins[3] = {rayOrigin.x, rayOrigin.y, rayOrigin.z};
    const f32 dirs[3] = {rayDir.x, rayDir.y, rayDir.z};
    const f32 bmins[3] = {aabbMin.x, aabbMin.y, aabbMin.z};
    const f32 bmaxs[3] = {aabbMax.x, aabbMax.y, aabbMax.z};

    for (int i = 0; i < 3; ++i) {
        const f32 origin = origins[i];
        const f32 dir = dirs[i];
        const f32 bmin = bmins[i];
        const f32 bmax = bmaxs[i];

        if (std::fabs(dir) < 1e-8f) {
            if (origin < bmin || origin > bmax) return -1.0f;
        } else {
            f32 t0 = (bmin - origin) / dir;
            f32 t1 = (bmax - origin) / dir;
            if (t0 > t1) std::swap(t0, t1);
            t_enter = std::max(t_enter, t0);
            t_exit = std::min(t_exit, t1);
            if (t_enter > t_exit) return -1.0f;
        }
    }

    if (t_enter <= t_exit && t_exit >= 0.0f) {
        return t_enter >= 0.0f ? t_enter : 0.0f;
    }
    return -1.0f;
}

ECS::Entity raycastSelectEntity(const Vec3& rayOrigin, const Vec3& rayDir, ECS::World& world,
                                const std::string& projectRoot) {
    ECS::Entity closestEntity = ECS::Entity::INVALID;
    f32 closestT = 1e10f;

    auto accept = [&](ECS::Entity entity, f32 t) {
        if (t >= 0.0f && t < closestT) {
            closestT = t;
            closestEntity = entity;
        }
    };

    auto padThin = [](Vec3& mn, Vec3& mx) {
        const f32 pad = 0.05f;
        if (mx.x - mn.x < pad) {
            const f32 c = (mn.x + mx.x) * 0.5f;
            mn.x = c - pad;
            mx.x = c + pad;
        }
        if (mx.y - mn.y < pad) {
            const f32 c = (mn.y + mx.y) * 0.5f;
            mn.y = c - pad;
            mx.y = c + pad;
        }
        if (mx.z - mn.z < pad) {
            const f32 c = (mn.z + mx.z) * 0.5f;
            mn.z = c - pad;
            mx.z = c + pad;
        }
    };

    auto testWorldAabb = [&](ECS::Entity entity, Vec3 mn, Vec3 mx) {
        padThin(mn, mx);
        accept(entity, rayIntersectsAABB(rayOrigin, rayDir, mn, mx));
    };

    auto testLocalAabb = [&](ECS::Entity entity, const Mat4& worldMatrix, Vec3 localMin,
                             Vec3 localMax) {
        Vec3 mn(1e30f, 1e30f, 1e30f);
        Vec3 mx(-1e30f, -1e30f, -1e30f);
        const Vec3 corners[2] = {localMin, localMax};
        for (int ix = 0; ix < 2; ++ix) {
            for (int iy = 0; iy < 2; ++iy) {
                for (int iz = 0; iz < 2; ++iz) {
                    const Vec3 p = worldMatrix.transformPoint(
                        Vec3(corners[ix].x, corners[iy].y, corners[iz].z));
                    mn.x = std::min(mn.x, p.x);
                    mn.y = std::min(mn.y, p.y);
                    mn.z = std::min(mn.z, p.z);
                    mx.x = std::max(mx.x, p.x);
                    mx.y = std::max(mx.y, p.y);
                    mx.z = std::max(mx.z, p.z);
                }
            }
        }
        testWorldAabb(entity, mn, mx);
    };

    auto testPoint = [&](ECS::Entity entity, const Vec3& point) {
        const f32 t = (point - rayOrigin).dot(rayDir);
        if (t < 0.0f) return;
        const f32 dist = ((rayOrigin + rayDir * t) - point).length();
        const f32 reach = std::max(0.25f, t * 0.03f);
        if (dist <= reach) accept(entity, t);
    };

    ECS::ComponentQuery meshes;
    meshes.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(meshes, [&](ECS::Entity entity, ECS::MeshFilterComponent& filter) {
        if (isEffectivelyDisabled(world, entity)) return;
        const Mat4 worldMatrix = entityMatrix(world, entity);
        if (const ECS::TerrainComponent* terrain = world.get<ECS::TerrainComponent>(entity)) {
            if (const Terrain::TerrainHeightmap* heightmap =
                    Terrain::TerrainCache::instance().heightmapFor(entity);
                heightmap && !heightmap->empty()) {
                Vec3 hit;
                if (Terrain::TerrainSculptor::raycast(worldMatrix, *heightmap, *terrain, rayOrigin,
                                                      rayDir, hit)) {
                    accept(entity, (hit - rayOrigin).dot(rayDir));
                }
                return;
            }
            const f32 halfX = terrain->worldSizeX * 0.5f;
            const f32 halfZ = terrain->worldSizeZ * 0.5f;
            testLocalAabb(entity, worldMatrix, Vec3(-halfX, -0.25f, -halfZ),
                          Vec3(halfX, 1.0f, halfZ));
            return;
        }

        Assets::Mesh3D* mesh = ECS::editedMesh(world, entity);
        if (!mesh && filter.primitive == ECS::MeshPrimitive::Custom) {
            if (!filter.customMeshPath.empty()) {
                mesh = Assets::MeshCache::getInstance().getMesh(filter.customMeshPath, projectRoot);
            }
        } else if (!mesh) {
            mesh = Render::GpuProceduralMeshes::get(filter.primitive);
        }
        if (mesh && (mesh->bounds.max - mesh->bounds.min).lengthSquared() > 1.0e-6f) {
            testLocalAabb(entity, worldMatrix, mesh->bounds.min, mesh->bounds.max);
        } else {
            testLocalAabb(entity, worldMatrix, Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
        }
    });

    auto testMarker = [&](ECS::Entity entity) {
        if (isEffectivelyDisabled(world, entity)) return;
        if (world.has<ECS::MeshFilterComponent>(entity)) return;
        Vec3 point;
        if (!tryGetEntityPosition(world, entity, point)) return;
        if (const Effects::EffectComponent* effect = world.get<Effects::EffectComponent>(entity)) {
            const auto kind = static_cast<Effects::EffectKind>(effect->kind);
            if (kind == Effects::EffectKind::VolumetricLight || kind == Effects::EffectKind::Fog) {
                const f32 radius = std::max(effect->radius, 0.25f);
                if (kind == Effects::EffectKind::VolumetricLight &&
                    Effects::volumetricShapeOf(*effect) != Effects::VolumetricShape::Sphere) {
                    const Mat4 matrix = entityMatrix(world, entity);
                    Vec3 forward = matrix.transformVector(Vec3(0.0f, 0.0f, -1.0f));
                    if (forward.lengthSquared() < 1.0e-8f) forward = Vec3(0.0f, 0.0f, -1.0f);
                    else forward = forward.normalized();
                    const Vec3 end = point + forward * radius;
                    const f32 pad = std::max(effect->startSize, effect->endSize) * 0.5f;
                    const Vec3 padVec(pad, pad, pad);
                    testWorldAabb(entity,
                                  Vec3(std::min(point.x, end.x), std::min(point.y, end.y),
                                       std::min(point.z, end.z)) -
                                      padVec,
                                  Vec3(std::max(point.x, end.x), std::max(point.y, end.y),
                                       std::max(point.z, end.z)) +
                                      padVec);
                } else {
                    testWorldAabb(entity, point - Vec3(radius, radius, radius),
                                  point + Vec3(radius, radius, radius));
                }
                return;
            }
        }
        if (const ECS::Transform* transform = world.get<ECS::Transform>(entity)) {
            if (world.has<ECS::Sprite>(entity)) {
                const f32 halfW = std::max(0.5f, std::abs(transform->scale.x) * 0.5f);
                const f32 halfH = std::max(0.5f, std::abs(transform->scale.y) * 0.5f);
                testWorldAabb(entity, point - Vec3(halfW, halfH, 0.05f),
                              point + Vec3(halfW, halfH, 0.05f));
                return;
            }
        }
        testPoint(entity, point);
    };

    ECS::ComponentQuery positioned;
    positioned.with<ECS::Position3D>();
    world.forEach<ECS::Position3D>(positioned, [&](ECS::Entity entity, ECS::Position3D&) {
        testMarker(entity);
    });
    ECS::ComponentQuery transforms;
    transforms.with<ECS::Transform>();
    transforms.without<ECS::Position3D>();
    world.forEach<ECS::Transform>(transforms, [&](ECS::Entity entity, ECS::Transform&) {
        testMarker(entity);
    });

    return closestEntity;
}

}  // namespace Caffeine::Scene

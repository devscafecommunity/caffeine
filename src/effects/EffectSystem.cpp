#include "effects/EffectSystem.hpp"

#include "assets/MaterialCache.hpp"
#include "assets/MeshCache.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshGeometry.hpp"
#include "ecs/ParticleSystem.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#ifdef CF_HAS_SDL3
#include "render/GpuProceduralMeshes.hpp"
#endif
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Caffeine::Effects {
namespace {

struct ParticlePool {
    u32 rng = 1;
    std::vector<SimParticle> particles;
};

std::unordered_map<u32, ParticlePool> g_pools;

u32 rngNext(u32& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    if (state == 0) state = 1;
    return state;
}

f32 rng01(u32& state) {
    return static_cast<f32>(rngNext(state) & 0xFFFFFFu) / static_cast<f32>(0xFFFFFFu);
}

f32 rngRange(u32& state, f32 minValue, f32 maxValue) {
    return minValue + (maxValue - minValue) * rng01(state);
}

Vec3 effectOrigin(ECS::World& world, ECS::Entity entity, const EffectComponent& effect,
                  const Vec3& cameraPos) {
    if (static_cast<EffectSpace>(effect.space) == EffectSpace::Camera) return cameraPos;
    if (const ECS::Position3D* position = world.get<ECS::Position3D>(entity)) return position->position;
    if (const ECS::Transform* transform = world.get<ECS::Transform>(entity)) return transform->position;
    return {};
}

Vec4 materialTint(const EffectComponent& effect) {
    if (effect.materialPath[0] == '\0') return Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    const Assets::MaterialSurface surface =
        Assets::MaterialCache::instance().resolve(effect.materialPath, "");
    if (!surface.valid) return Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Vec4 tint(surface.albedo.x, surface.albedo.y, surface.albedo.z, surface.albedo.w);
    if (surface.emissionStrength > 0.0f) {
        tint.x = std::clamp(tint.x + surface.emission.x * surface.emissionStrength, 0.0f, 4.0f);
        tint.y = std::clamp(tint.y + surface.emission.y * surface.emissionStrength, 0.0f, 4.0f);
        tint.z = std::clamp(tint.z + surface.emission.z * surface.emissionStrength, 0.0f, 4.0f);
    }
    return tint;
}

constexpr f32 kDegToRad = 3.14159265f / 180.0f;

Mat4 effectMatrix(ECS::World& world, ECS::Entity entity) {
    if (const Scene::WorldTransform* worldTransform = world.get<Scene::WorldTransform>(entity)) {
        return worldTransform->matrix;
    }
    if (const ECS::Transform* transform = world.get<ECS::Transform>(entity)) {
        return Mat4::translation(transform->position) * Mat4::rotationZ(transform->rotation.z * kDegToRad) *
               Mat4::rotationY(transform->rotation.y * kDegToRad) *
               Mat4::rotationX(transform->rotation.x * kDegToRad) *
               Mat4::scale(transform->scale.x, transform->scale.y, transform->scale.z);
    }
    const ECS::Position3D* position = world.get<ECS::Position3D>(entity);
    const ECS::Rotation3D* rotation = world.get<ECS::Rotation3D>(entity);
    const ECS::Scale3D* scale = world.get<ECS::Scale3D>(entity);
    const Mat4 translation = position ? Mat4::translation(position->position) : Mat4::identity();
    const Mat4 orient = rotation ? Quat(rotation->quaternion.x, rotation->quaternion.y, rotation->quaternion.z,
                                        rotation->quaternion.w)
                                      .normalized()
                                      .toMatrix()
                                : Mat4::identity();
    const Mat4 sized = scale ? Mat4::scale(scale->scale.x, scale->scale.y, scale->scale.z) : Mat4::identity();
    return translation * orient * sized;
}

struct BeamAxes {
    Vec3 forward{0.0f, 0.0f, -1.0f};
    Vec3 right{1.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
};

BeamAxes beamAxes(ECS::World& world, ECS::Entity entity) {
    const Mat4 matrix = effectMatrix(world, entity);
    BeamAxes axes;
    const Vec3 forward = matrix.transformVector(Vec3(0.0f, 0.0f, -1.0f));
    const Vec3 right = matrix.transformVector(Vec3(1.0f, 0.0f, 0.0f));
    const Vec3 up = matrix.transformVector(Vec3(0.0f, 1.0f, 0.0f));
    if (forward.lengthSquared() > 1.0e-8f) axes.forward = forward.normalized();
    if (right.lengthSquared() > 1.0e-8f) axes.right = right.normalized();
    if (up.lengthSquared() > 1.0e-8f) axes.up = up.normalized();
    return axes;
}

void pushQuad(Assets::Mesh3D& mesh, const Vec3& center, const Vec3& right, const Vec3& up, f32 size,
              const Vec4& color) {
    const Vec3 normal = right.cross(up).normalized();
    const Vec3 corners[4] = {
        center - right * size - up * size,
        center + right * size - up * size,
        center + right * size + up * size,
        center - right * size + up * size,
    };
    const Vec2 uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    const u32 base = static_cast<u32>(mesh.vertices.size());
    for (int i = 0; i < 4; ++i) {
        Assets::Vertex3D vertex{};
        vertex.position = corners[i];
        vertex.normal = normal;
        vertex.texcoord = uvs[i];
        vertex.tangent = color;
        mesh.vertices.push_back(vertex);
    }
    const u32 indices[6] = {0, 1, 2, 0, 2, 3};
    for (u32 index : indices) mesh.indices.push_back(base + index);
}

void pushColoredQuad(Assets::Mesh3D& mesh, const Vec3 corners[4], const Vec2 uvs[4], const Vec4 colors[4]) {
    if (mesh.vertices.size() + 4 > 4096) return;
    const Vec3 normal = (corners[1] - corners[0]).cross(corners[2] - corners[0]);
    const Vec3 n = normal.lengthSquared() > 1.0e-8f ? normal.normalized() : Vec3(0.0f, 1.0f, 0.0f);
    const u32 base = static_cast<u32>(mesh.vertices.size());
    for (int i = 0; i < 4; ++i) {
        Assets::Vertex3D vertex{};
        vertex.position = corners[i];
        vertex.normal = n;
        vertex.texcoord = uvs[i];
        vertex.tangent = colors[i];
        mesh.vertices.push_back(vertex);
    }
    const u32 indices[6] = {0, 1, 2, 0, 2, 3};
    for (u32 index : indices) mesh.indices.push_back(base + index);
}

void pushBeam(Assets::Mesh3D& mesh, const Vec3& start, const Vec3& end, const Vec3& side, f32 nearHalf,
              f32 farHalf, const Vec4& nearColor, const Vec4& farColor) {
    const Vec3 corners[4] = {
        start - side * nearHalf,
        start + side * nearHalf,
        end + side * farHalf,
        end - side * farHalf,
    };
    const Vec2 uvs[4] = {{0.0f, 2.0f}, {1.0f, 2.0f}, {1.0f, 2.0f}, {0.0f, 2.0f}};
    const Vec4 colors[4] = {nearColor, nearColor, farColor, farColor};
    pushColoredQuad(mesh, corners, uvs, colors);
}

void pushRect(Assets::Mesh3D& mesh, const Vec3& center, const Vec3& right, const Vec3& up, f32 halfW,
              f32 halfH, const Vec4& color) {
    const Vec3 corners[4] = {
        center - right * halfW - up * halfH,
        center + right * halfW - up * halfH,
        center + right * halfW + up * halfH,
        center - right * halfW + up * halfH,
    };
    const Vec2 uvs[4] = {{2.0f, 2.0f}, {3.0f, 2.0f}, {3.0f, 3.0f}, {2.0f, 3.0f}};
    const Vec4 colors[4] = {color, color, color, color};
    pushColoredQuad(mesh, corners, uvs, colors);
}

u32 shellCount(EffectQuality quality);

void emitVolumetricShafts(ECS::World& world, ECS::Entity entity, const EffectComponent& effect,
                          const Vec3& origin, const Vec3& cameraPos, Assets::Mesh3D& mesh) {
    const VolumetricShape shape = volumetricShapeOf(effect);
    const BeamAxes axes = beamAxes(world, entity);
    const f32 length = std::max(effect.radius, 0.2f);
    const Vec3 end = origin + axes.forward * length;
    const Vec3 toCamera = cameraPos - origin;
    Vec3 facing = axes.forward.cross(toCamera);
    if (facing.lengthSquared() < 1.0e-8f) facing = axes.right;
    else facing = facing.normalized();

    const f32 cosTheta = toCamera.lengthSquared() > 1.0e-8f
                             ? toCamera.normalized().dot(axes.forward * -1.0f)
                             : 0.0f;
    const f32 phase = henyeyGreenstein(cosTheta, effect.anisotropy);
    const f32 isotropic = henyeyGreenstein(0.0f, 0.0f);
    const f32 boost = std::clamp(phase / std::max(isotropic, 0.01f), 0.45f, 3.0f);
    const Vec3 rgb = effect.lightColor * effect.intensity;
    const f32 nearAlpha = std::clamp((0.1f + 0.7f * effect.density) * boost, 0.05f, 0.8f);
    const Vec4 nearColor(rgb.x, rgb.y, rgb.z, nearAlpha);
    const Vec4 farColor(rgb.x * 0.7f, rgb.y * 0.65f, rgb.z * 0.55f, nearAlpha * 0.18f);
    const Vec4 capColor(rgb.x, rgb.y, rgb.z, nearAlpha * 0.35f);

    u32 columns = 1;
    u32 rows = 1;
    f32 nearW = std::max(effect.startSize, 0.05f);
    f32 nearH = std::max(effect.endSize, 0.05f);
    f32 farW = nearW;
    f32 farH = nearH;
    if (shape == VolumetricShape::Cone) {
        nearH = nearW;
        farW = std::max(effect.endSize, 0.05f);
        farH = farW;
    } else if (shape == VolumetricShape::Cylinder) {
        nearH = nearW;
        farW = nearW;
        farH = nearW;
    } else if (shape == VolumetricShape::Window) {
        volumetricGridOf(effect, columns, rows);
    }

    const u32 layers = shellCount(static_cast<EffectQuality>(effect.quality));
    const f32 gap = shape == VolumetricShape::Window ? 0.22f : 0.0f;
    const f32 cellW = nearW / static_cast<f32>(columns);
    const f32 cellH = nearH / static_cast<f32>(rows);
    const f32 farCellW = farW / static_cast<f32>(columns);
    const f32 farCellH = farH / static_cast<f32>(rows);

    for (u32 row = 0; row < rows; ++row) {
        for (u32 column = 0; column < columns; ++column) {
            if (mesh.vertices.size() + 16 > 4096) return;
            const f32 offsetX = (static_cast<f32>(column) + 0.5f - static_cast<f32>(columns) * 0.5f) * cellW;
            const f32 offsetY = (static_cast<f32>(row) + 0.5f - static_cast<f32>(rows) * 0.5f) * cellH;
            const Vec3 start = origin + axes.right * offsetX + axes.up * offsetY;
            const f32 farOffsetX = (static_cast<f32>(column) + 0.5f - static_cast<f32>(columns) * 0.5f) * farCellW;
            const f32 farOffsetY = (static_cast<f32>(row) + 0.5f - static_cast<f32>(rows) * 0.5f) * farCellH;
            const Vec3 finish = end + axes.right * farOffsetX + axes.up * farOffsetY;
            const f32 halfW = cellW * (1.0f - gap) * 0.5f;
            const f32 halfH = cellH * (1.0f - gap) * 0.5f;
            const f32 farHalfW = farCellW * (1.0f - gap) * 0.5f;
            const f32 farHalfH = farCellH * (1.0f - gap) * 0.5f;
            const f32 facingNear = std::max(std::abs(axes.right.dot(facing)) * halfW +
                                                std::abs(axes.up.dot(facing)) * halfH,
                                            0.03f);
            const f32 facingFar = std::max(std::abs(axes.right.dot(facing)) * farHalfW +
                                               std::abs(axes.up.dot(facing)) * farHalfH,
                                           0.03f);
            pushBeam(mesh, start, finish, facing, facingNear, facingFar, nearColor, farColor);
            if (layers >= 2) {
                pushBeam(mesh, start, finish, axes.up, halfW, farHalfW, nearColor, farColor);
            }
            if (layers >= 3) {
                pushBeam(mesh, start, finish, axes.right, halfH, farHalfH, nearColor, farColor);
            }
            pushRect(mesh, finish, axes.right, axes.up, farHalfW, farHalfH, capColor);
        }
    }
}

u32 shellCount(EffectQuality quality) {
    switch (quality) {
        case EffectQuality::Performance: return 1;
        case EffectQuality::Quality: return 3;
        case EffectQuality::Balanced: break;
    }
    return 2;
}

EffectFogStyle fogStyleOf(const EffectComponent& effect) {
    return static_cast<EffectFogStyle>(std::min<u8>(effect.pad0, 3));
}

template <typename Emit>
void emitFog(const EffectComponent& effect, const Vec3& center, bool twoD, Emit&& emit) {
    const EffectFogStyle style = fogStyleOf(effect);
    u32 count = shellCount(static_cast<EffectQuality>(effect.quality));
    if (style == EffectFogStyle::Dust) count += 2;
    else if (style == EffectFogStyle::Smoke) count += 1;
    const f32 phase = effect.emitCarry;
    const f32 radius = std::max(effect.radius, 0.05f);
    for (u32 i = 0; i < count; ++i) {
        const f32 angle = phase * (style == EffectFogStyle::Smoke ? 0.4f : 0.18f) + static_cast<f32>(i) * 2.1f;
        const f32 rise = style == EffectFogStyle::Smoke
                             ? std::fmod(phase * 0.25f + static_cast<f32>(i) * 0.35f, 1.0f) * radius * 0.45f
                             : std::cos(angle * 0.8f) * radius * 0.18f;
        Vec3 offset(std::sin(angle) * radius * 0.28f, rise,
                    twoD ? 0.0f : std::sin(angle * 1.3f) * radius * 0.22f);
        f32 size = radius;
        if (style == EffectFogStyle::Dust) size *= 0.42f;
        else if (style == EffectFogStyle::Mist) size *= 1.15f;
        else size *= 0.85f;
        size *= std::max(0.35f, 1.0f - 0.18f * static_cast<f32>(i));
        f32 alpha = effect.density * (style == EffectFogStyle::Mist ? 0.55f : 1.0f);
        alpha = std::clamp(alpha, 0.0f, 0.85f) / static_cast<f32>(i + 1);
        const Vec4 color(effect.lightColor.x * effect.intensity, effect.lightColor.y * effect.intensity,
                         effect.lightColor.z * effect.intensity, alpha);
        emit(center + offset, std::max(size, 0.05f), color);
    }
}

Vec3 inverseAffinePoint(const Mat4& matrix, const Vec3& world) {
    const Vec3 c0(matrix(0, 0), matrix(1, 0), matrix(2, 0));
    const Vec3 c1(matrix(0, 1), matrix(1, 1), matrix(2, 1));
    const Vec3 c2(matrix(0, 2), matrix(1, 2), matrix(2, 2));
    const Vec3 origin(matrix(0, 3), matrix(1, 3), matrix(2, 3));
    const Vec3 delta = world - origin;
    const f32 det = c0.dot(c1.cross(c2));
    if (std::abs(det) < 1.0e-8f) return Vec3(0.0f, 0.0f, 0.0f);
    const f32 invDet = 1.0f / det;
    return Vec3(delta.dot(c1.cross(c2)) * invDet, delta.dot(c2.cross(c0)) * invDet,
                delta.dot(c0.cross(c1)) * invDet);
}

bool emitVolumetricMesh(ECS::World& world, ECS::Entity entity, const EffectComponent& effect,
                        const Vec3& cameraPos, Assets::Mesh3D& mesh) {
    const ECS::MeshFilterComponent* filter = world.get<ECS::MeshFilterComponent>(entity);
    if (!filter) return false;
    Assets::Mesh3D* source = ECS::editedMesh(world, entity);
#ifdef CF_HAS_SDL3
    if (!source) source = Render::GpuProceduralMeshes::get(filter->primitive);
#endif
    if (!source && filter->primitive == ECS::MeshPrimitive::Custom && !filter->customMeshPath.empty()) {
        source = Assets::MeshCache::getInstance().getMesh(filter->customMeshPath, "");
    }
    if (!source || source->vertices.empty() || source->indices.empty()) return false;

    Vec3 boundsMin(1.0e30f, 1.0e30f, 1.0e30f);
    Vec3 boundsMax(-1.0e30f, -1.0e30f, -1.0e30f);
    for (const Assets::Vertex3D& vertex : source->vertices) {
        boundsMin.x = std::min(boundsMin.x, vertex.position.x);
        boundsMin.y = std::min(boundsMin.y, vertex.position.y);
        boundsMin.z = std::min(boundsMin.z, vertex.position.z);
        boundsMax.x = std::max(boundsMax.x, vertex.position.x);
        boundsMax.y = std::max(boundsMax.y, vertex.position.y);
        boundsMax.z = std::max(boundsMax.z, vertex.position.z);
    }
    const Vec3 center = (boundsMin + boundsMax) * 0.5f;
    Vec3 extent = (boundsMax - boundsMin) * 0.5f;
    extent.x = std::max(extent.x, 1.0e-4f);
    extent.y = std::max(extent.y, 1.0e-4f);
    extent.z = std::max(extent.z, 1.0e-4f);

    const Mat4 matrix = effectMatrix(world, entity);
    const f32 alpha = std::clamp(0.12f + 0.35f * effect.density, 0.08f, 0.55f);
    const Vec3 rgb = effect.lightColor * effect.intensity * alpha;
    const Vec3 cameraMesh = inverseAffinePoint(matrix, cameraPos);
    const Vec3 cameraLocal((cameraMesh.x - center.x) / extent.x, (cameraMesh.y - center.y) / extent.y,
                           (cameraMesh.z - center.z) / extent.z);
    if (mesh.vertices.size() + source->vertices.size() > 4096) return false;
    const u32 base = static_cast<u32>(mesh.vertices.size());
    for (const Assets::Vertex3D& vertex : source->vertices) {
        const Vec3 local = vertex.position - center;
        Assets::Vertex3D out{};
        out.position = matrix.transformPoint(vertex.position);
        out.normal = Vec3(local.x / extent.x, local.y / extent.y, local.z / extent.z);
        // x encodes the volume sentinel plus cameraLocal.z; y is cameraLocal.x.
        out.texcoord = {-1.0e6f + cameraLocal.z, cameraLocal.x};
        out.tangent = Vec4(rgb.x, rgb.y, rgb.z, cameraLocal.y);
        mesh.vertices.push_back(out);
    }
    for (u32 index : source->indices) mesh.indices.push_back(base + index);
    return true;
}

}  // namespace

namespace {

bool transformDrivesEffect(ECS::World& world, ECS::Entity entity) {
    return world.has<ECS::Transform>(entity) && !world.has<ECS::Position3D>(entity);
}

void aimVolumetricMesh(ECS::World& world, ECS::Entity entity, const EffectComponent& effect) {
    if (volumetricShapeOf(effect) == VolumetricShape::Sphere) return;
    if (transformDrivesEffect(world, entity)) {
        world.get<ECS::Transform>(entity)->rotation.x -= 90.0f;
        return;
    }
    ECS::Rotation3D* rotation = world.get<ECS::Rotation3D>(entity);
    if (!rotation) rotation = &world.add<ECS::Rotation3D>(entity);
    const Quat current(rotation->quaternion.x, rotation->quaternion.y, rotation->quaternion.z,
                       rotation->quaternion.w);
    const Quat aimed = (current * Quat::fromEuler(-1.5707963f, 0.0f, 0.0f)).normalized();
    rotation->quaternion = Vec4(aimed.x, aimed.y, aimed.z, aimed.w);
}

}  // namespace

ECS::MeshPrimitive volumetricMeshPrimitive(VolumetricShape shape) {
    switch (shape) {
        case VolumetricShape::Cone: return ECS::MeshPrimitive::Cone;
        case VolumetricShape::Box: return ECS::MeshPrimitive::Cube;
        case VolumetricShape::Cylinder: return ECS::MeshPrimitive::Cylinder;
        case VolumetricShape::Window: return ECS::MeshPrimitive::Cube;
        case VolumetricShape::Sphere: break;
    }
    return ECS::MeshPrimitive::Sphere;
}

bool volumetricShapeForMesh(ECS::MeshPrimitive primitive, VolumetricShape& shape) {
    switch (primitive) {
        case ECS::MeshPrimitive::Sphere: shape = VolumetricShape::Sphere; return true;
        case ECS::MeshPrimitive::Cone: shape = VolumetricShape::Cone; return true;
        case ECS::MeshPrimitive::Cube: shape = VolumetricShape::Box; return true;
        case ECS::MeshPrimitive::Cylinder: shape = VolumetricShape::Cylinder; return true;
        default: break;
    }
    return false;
}

Vec3 volumetricScaleForEffect(const EffectComponent& effect) {
    const VolumetricShape shape = volumetricShapeOf(effect);
    const f32 length = std::max(effect.radius, 0.05f);
    const f32 width = std::max(effect.startSize, 0.05f);
    const f32 depth = std::max(effect.endSize, 0.05f);
    switch (shape) {
        case VolumetricShape::Sphere: {
            const f32 diameter = length * 2.0f;
            return {diameter, diameter, diameter};
        }
        case VolumetricShape::Cone:
            return {depth, length, depth};
        case VolumetricShape::Cylinder:
            return {width, length, width};
        case VolumetricShape::Box:
        case VolumetricShape::Window:
            break;
    }
    return {width, length, depth};
}

void pushVolumetricSizeToMesh(ECS::World& world, ECS::Entity entity, const EffectComponent& effect) {
    const Vec3 scale = volumetricScaleForEffect(effect);
    if (transformDrivesEffect(world, entity)) {
        world.get<ECS::Transform>(entity)->scale = scale;
        return;
    }
    if (!world.has<ECS::Scale3D>(entity)) world.add<ECS::Scale3D>(entity);
    world.get<ECS::Scale3D>(entity)->scale = scale;
}

void pullVolumetricSizeFromMesh(ECS::World& world, ECS::Entity entity, EffectComponent& effect) {
    if (!world.has<ECS::MeshFilterComponent>(entity)) return;
    Vec3 scale{1.0f, 1.0f, 1.0f};
    if (transformDrivesEffect(world, entity)) scale = world.get<ECS::Transform>(entity)->scale;
    else if (const ECS::Scale3D* sized = world.get<ECS::Scale3D>(entity)) scale = sized->scale;
    const f32 sx = std::max(std::abs(scale.x), 0.05f);
    const f32 sy = std::max(std::abs(scale.y), 0.05f);
    const f32 sz = std::max(std::abs(scale.z), 0.05f);
    switch (volumetricShapeOf(effect)) {
        case VolumetricShape::Sphere:
            effect.radius = sx * 0.5f;
            effect.startSize = effect.radius;
            effect.endSize = effect.radius;
            break;
        case VolumetricShape::Cone:
            effect.radius = sy;
            effect.endSize = sx;
            break;
        case VolumetricShape::Cylinder:
            effect.radius = sy;
            effect.startSize = sx;
            effect.endSize = sx;
            break;
        case VolumetricShape::Box:
        case VolumetricShape::Window:
            effect.radius = sy;
            effect.startSize = sx;
            effect.endSize = sz;
            break;
    }
}

void adoptVolumetricMesh(ECS::World& world, ECS::Entity entity, EffectComponent& effect) {
    if (static_cast<EffectKind>(effect.kind) != EffectKind::VolumetricLight) return;
    if (world.has<ECS::MeshGeometryComponent>(entity)) world.remove<ECS::MeshGeometryComponent>(entity);
    const ECS::MeshPrimitive primitive = volumetricMeshPrimitive(volumetricShapeOf(effect));
    const bool creating = !world.has<ECS::MeshFilterComponent>(entity);
    if (creating) {
        ECS::MeshFilterComponent filter;
        filter.primitive = primitive;
        world.add<ECS::MeshFilterComponent>(entity, filter);
        if (effect.pad0 == 0) {
            pushVolumetricSizeToMesh(world, entity, effect);
            aimVolumetricMesh(world, entity, effect);
            effect.pad0 = 1;
        }
        return;
    }
    if (ECS::MeshFilterComponent* filter = world.get<ECS::MeshFilterComponent>(entity)) {
        filter->primitive = primitive;
    }
    pushVolumetricSizeToMesh(world, entity, effect);
    effect.pad0 = 1;
}

void ensureVolumetricMesh(ECS::World& world, ECS::Entity entity, EffectComponent& effect) {
    if (static_cast<EffectKind>(effect.kind) != EffectKind::VolumetricLight) return;
    if (!world.has<ECS::MeshFilterComponent>(entity)) {
        adoptVolumetricMesh(world, entity, effect);
        return;
    }
    const ECS::MeshFilterComponent* filter = world.get<ECS::MeshFilterComponent>(entity);
    if (!filter) return;
    const VolumetricShape shape = volumetricShapeOf(effect);
    if (shape == VolumetricShape::Window && filter->primitive == ECS::MeshPrimitive::Cube) return;
    VolumetricShape fromMesh = VolumetricShape::Sphere;
    if (volumetricShapeForMesh(filter->primitive, fromMesh) && fromMesh != shape) {
        effect.pad1 = static_cast<u8>(fromMesh);
    }
}

void tickEffects(ECS::World& world, f32 dt, const Vec3& cameraPos) {
    ECS::ParticleSystem legacy;
    legacy.onUpdate(world, dt);
    if (dt < 0.0f) dt = 0.0f;

    ECS::ComponentQuery query;
    query.with<EffectComponent>();
    std::vector<u32> live;
    world.forEach<EffectComponent>(query, [&](ECS::Entity entity, EffectComponent& effect) {
        live.push_back(entity.id());
        if (!effect.enabled || Scene::isEffectivelyDisabled(world, entity)) return;
        if (static_cast<EffectKind>(effect.kind) == EffectKind::Fog) {
            effect.emitCarry += dt;
            if (effect.emitCarry > 10000.0f) effect.emitCarry = 0.0f;
            return;
        }
        if (static_cast<EffectKind>(effect.kind) != EffectKind::Particles) return;

        ParticlePool& pool = g_pools[entity.id()];
        if (pool.rng == 1) pool.rng = effect.seed ? effect.seed : entity.id() + 1u;
        const bool twoD = static_cast<EffectDomain>(effect.domain) == EffectDomain::TwoD;
        const Vec3 origin = effectOrigin(world, entity, effect, cameraPos);
        const Vec4 tint = materialTint(effect);
        const i32 cap = std::max(effect.maxParticles, 0);

        effect.emitCarry += std::max(effect.rate, 0.0f) * dt;
        while (effect.emitCarry >= 1.0f && static_cast<i32>(pool.particles.size()) < cap) {
            effect.emitCarry -= 1.0f;
            SimParticle particle;
            particle.position = origin;
            particle.velocity = Vec3(rngRange(pool.rng, effect.velocityMin.x, effect.velocityMax.x),
                                     rngRange(pool.rng, effect.velocityMin.y, effect.velocityMax.y),
                                     twoD ? 0.0f
                                          : rngRange(pool.rng, effect.velocityMin.z, effect.velocityMax.z));
            particle.life = std::max(effect.lifetime, 0.05f);
            particle.maxLife = particle.life;
            particle.size = effect.startSize;
            particle.color = Vec4(effect.startColor.x * tint.x, effect.startColor.y * tint.y,
                                  effect.startColor.z * tint.z, effect.startColor.w * tint.w);
            pool.particles.push_back(particle);
        }

        const Vec3 gravity = twoD ? Vec3(effect.gravity.x, effect.gravity.y, 0.0f) : effect.gravity;
        const f32 drag = std::clamp(1.0f - effect.drag * dt, 0.0f, 1.0f);
        for (SimParticle& particle : pool.particles) {
            particle.life -= dt;
            particle.velocity = particle.velocity * drag + gravity * dt;
            particle.position += particle.velocity * dt;
            if (twoD) particle.position.z = origin.z;
            const f32 t = std::clamp(1.0f - particle.life / particle.maxLife, 0.0f, 1.0f);
            particle.size = effect.startSize + (effect.endSize - effect.startSize) * t;
            const Vec4 start(effect.startColor.x * tint.x, effect.startColor.y * tint.y,
                             effect.startColor.z * tint.z, effect.startColor.w * tint.w);
            const Vec4 end(effect.endColor.x * tint.x, effect.endColor.y * tint.y,
                           effect.endColor.z * tint.z, effect.endColor.w * tint.w);
            particle.color = Vec4(start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t,
                                  start.z + (end.z - start.z) * t, start.w + (end.w - start.w) * t);
        }
        std::erase_if(pool.particles, [](const SimParticle& particle) { return particle.life <= 0.0f; });
    });

    for (auto it = g_pools.begin(); it != g_pools.end();) {
        if (std::find(live.begin(), live.end(), it->first) == live.end()) it = g_pools.erase(it);
        else ++it;
    }
}

const std::vector<SimParticle>* particlesFor(u32 entityId) {
    const auto it = g_pools.find(entityId);
    if (it == g_pools.end()) return nullptr;
    return &it->second.particles;
}

void collectOverlayEffects(ECS::World& world, bool onlyTwoD, std::vector<EffectSprite>& out) {
    ECS::ComponentQuery query;
    query.with<EffectComponent>();
    world.forEach<EffectComponent>(query, [&](ECS::Entity entity, EffectComponent& effect) {
        if (!effect.enabled) return;
        const EffectDomain domain = static_cast<EffectDomain>(effect.domain);
        if (onlyTwoD && domain != EffectDomain::TwoD) return;
        if (static_cast<EffectKind>(effect.kind) == EffectKind::Particles) {
            const std::vector<SimParticle>* particles = particlesFor(entity.id());
            if (!particles) return;
            for (const SimParticle& particle : *particles) {
                EffectSprite sprite;
                sprite.position = particle.position;
                sprite.size = particle.size;
                sprite.color = particle.color;
                out.push_back(sprite);
            }
        } else if (static_cast<EffectKind>(effect.kind) == EffectKind::VolumetricLight && !onlyTwoD) {
            ensureVolumetricMesh(world, entity, effect);
            const Mat4 matrix = effectMatrix(world, entity);
            const Vec3 center = matrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
            const Vec3 side = matrix.transformPoint(Vec3(0.5f, 0.0f, 0.0f));
            const f32 radius = std::max((side - center).length(), 0.1f);
            const Vec4 color(effect.lightColor.x, effect.lightColor.y, effect.lightColor.z,
                             std::clamp(effect.density, 0.0f, 1.0f));
            if (volumetricShapeOf(effect) == VolumetricShape::Sphere) {
                EffectSprite sprite;
                sprite.position = center;
                sprite.size = radius;
                sprite.color = color;
                out.push_back(sprite);
            } else {
                const Vec3 start = matrix.transformPoint(Vec3(0.0f, -0.5f, 0.0f));
                const Vec3 finish = matrix.transformPoint(Vec3(0.0f, 0.5f, 0.0f));
                for (int step = 0; step < 5; ++step) {
                    const f32 t = static_cast<f32>(step) / 4.0f;
                    EffectSprite sprite;
                    sprite.position = start + (finish - start) * t;
                    sprite.size = radius;
                    sprite.color = color;
                    out.push_back(sprite);
                }
            }
        } else if (static_cast<EffectKind>(effect.kind) == EffectKind::Fog) {
            const bool twoD = domain == EffectDomain::TwoD;
            emitFog(effect, effectOrigin(world, entity, effect, {}), twoD,
                    [&](const Vec3& position, f32 size, const Vec4& color) {
                        EffectSprite sprite;
                        sprite.position = position;
                        sprite.size = size;
                        sprite.color = color;
                        out.push_back(sprite);
                    });
        }
    });

    if (onlyTwoD) return;
    ECS::ComponentQuery legacy;
    legacy.with<ECS::ParticleEmitterComponent>();
    legacy.with<ECS::Transform>();
    world.forEach<ECS::ParticleEmitterComponent, ECS::Transform>(
        legacy, [&](ECS::Entity, ECS::ParticleEmitterComponent& emitter, ECS::Transform&) {
            for (const auto& particle : emitter.activeParticles) {
                EffectSprite sprite;
                sprite.position = Vec3(particle.position.x, particle.position.y, 0.0f);
                sprite.size = particle.size * 0.02f;
                const u32 color = particle.color;
                sprite.color = Vec4(((color >> 24) & 0xFFu) / 255.0f, ((color >> 16) & 0xFFu) / 255.0f,
                                    ((color >> 8) & 0xFFu) / 255.0f, (color & 0xFFu) / 255.0f);
                out.push_back(sprite);
            }
        });
}

void buildEffectMesh(ECS::World& world, const Vec3& cameraPos, const Vec3& cameraRight,
                     const Vec3& cameraUp, Assets::Mesh3D& mesh) {
    mesh.vertices.clear();
    mesh.indices.clear();
    Vec3 right = cameraRight;
    Vec3 up = cameraUp;
    if (right.lengthSquared() < 1.0e-6f) right = Vec3(1.0f, 0.0f, 0.0f);
    else right = right.normalized();
    if (up.lengthSquared() < 1.0e-6f) up = Vec3(0.0f, 1.0f, 0.0f);
    else up = up.normalized();

    ECS::ComponentQuery query;
    query.with<EffectComponent>();
    world.forEach<EffectComponent>(query, [&](ECS::Entity entity, EffectComponent& effect) {
        if (!effect.enabled || mesh.vertices.size() >= 4096) return;
        const EffectDomain domain = static_cast<EffectDomain>(effect.domain);
        if (domain == EffectDomain::TwoD) return;
        const EffectKind kind = static_cast<EffectKind>(effect.kind);
        if (kind == EffectKind::Particles) {
            const std::vector<SimParticle>* particles = particlesFor(entity.id());
            if (!particles) return;
            for (const SimParticle& particle : *particles) {
                if (mesh.vertices.size() >= 4096) break;
                pushQuad(mesh, particle.position, right, up, std::max(particle.size, 0.001f), particle.color);
            }
        } else if (kind == EffectKind::VolumetricLight) {
            ensureVolumetricMesh(world, entity, effect);
            if (!emitVolumetricMesh(world, entity, effect, cameraPos, mesh)) {
            const Vec3 center = effectOrigin(world, entity, effect, cameraPos);
            if (volumetricShapeOf(effect) == VolumetricShape::Sphere) {
                const Vec3 viewDir = cameraPos - center;
                const Vec3 view = viewDir.lengthSquared() > 1.0e-6f ? viewDir.normalized() : up;
                const f32 scatter = volumetricScatter(center + view * effect.radius * 0.25f, center, view,
                                                      effect.radius, effect.density, effect.anisotropy, effect.intensity);
                const u32 shells = shellCount(static_cast<EffectQuality>(effect.quality));
                const f32 scales[3] = {1.0f, 0.62f, 0.34f};
                for (u32 shell = 0; shell < shells; ++shell) {
                    const f32 alpha = std::clamp(0.35f * effect.density + scatter, 0.0f, 0.8f) /
                                      static_cast<f32>(shell + 1);
                    const Vec4 color(effect.lightColor.x * effect.intensity, effect.lightColor.y * effect.intensity,
                                     effect.lightColor.z * effect.intensity, alpha);
                    pushQuad(mesh, center, right, up, effect.radius * scales[shell], color);
                }
            } else {
                emitVolumetricShafts(world, entity, effect, center, cameraPos, mesh);
            }
            }
        } else if (kind == EffectKind::Fog) {
            const Vec3 center = effectOrigin(world, entity, effect, cameraPos);
            emitFog(effect, center, false, [&](const Vec3& position, f32 size, const Vec4& color) {
                if (mesh.vertices.size() >= 4096) return;
                pushQuad(mesh, position, right, up, size, color);
            });
        }
    });
}

void applyMaterialReflection(f32 reflection, u8 budget, SurfaceShade& shade) {
    reflection = std::clamp(reflection, 0.0f, 1.0f);
    if (reflection <= 0.0f) return;
    shade.reflection = std::max(shade.reflection, reflection);
    shade.roughness = shade.roughness + (std::min(shade.roughness, 0.14f) - shade.roughness) * reflection;
    shade.metallic = std::max(shade.metallic, reflection * 0.85f);
    shade.reflectance = std::max(shade.reflectance, 0.04f);
    if (budget >= 1 && budget <= 3) {
        shade.ssrSteps = static_cast<i32>(reflectionSteps(budget));
        const EffectQuality quality = budget == 1 ? EffectQuality::Performance
                                      : budget == 3 ? EffectQuality::Quality
                                                    : EffectQuality::Balanced;
        shade.ssrRoughness = reflectionRoughnessGate(quality);
    }
}

void applyEffectShade(const EffectComponent& effect, SurfaceShade& shade) {
    if (!effect.enabled) return;
    const EffectKind kind = static_cast<EffectKind>(effect.kind);
    if (kind == EffectKind::ReflectiveSurface || effect.reflection > 0.0f) {
        const f32 strength = std::clamp(effect.reflection > 0.0f ? effect.reflection : 0.85f, 0.0f, 1.0f);
        shade.roughness = shade.roughness + (effect.roughness - shade.roughness) * strength;
        shade.metallic = shade.metallic + (effect.metallic - shade.metallic) * strength;
        shade.reflectance = std::max(shade.reflectance, 0.04f);
        shade.reflection = std::max(shade.reflection, strength);
        shade.ssrSteps = static_cast<i32>(reflectionSteps(static_cast<EffectQuality>(effect.quality)));
        shade.ssrRoughness = reflectionRoughnessGate(static_cast<EffectQuality>(effect.quality));
        shade.planar = effect.reflectMode == 2;
    }
    if (kind == EffectKind::MaterialShade) {
        shade.albedo.x *= effect.tint.x;
        shade.albedo.y *= effect.tint.y;
        shade.albedo.z *= effect.tint.z;
        shade.emission = shade.emission + effect.tint * std::max(effect.emissionBoost, 0.0f);
        shade.roughness = std::clamp(shade.roughness + effect.roughnessBias, 0.02f, 1.0f);
    }
}

}  // namespace Caffeine::Effects

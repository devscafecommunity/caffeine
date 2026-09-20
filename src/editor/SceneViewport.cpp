#include "editor/SceneViewport.hpp"
#include "debug/Profiler.hpp"
#include "debug/CrashHandler.hpp"
#include "editor/DragDropSystem.hpp"
#include "scene/EnvironmentSystem.hpp"
#include "assets/MaterialCache.hpp"
#include "editor/PrefabSystem.hpp"
#include "editor/EditorContext.hpp"
#include "editor/TestInstrumentation.hpp"
#include "editor/TestUIMapper.hpp"
#include "editor/TestRequestHandler.hpp"
#include "audio/AudioComponents.hpp"
#include "assets/MeshLoader.hpp"
#include "assets/MeshCache.hpp"
#include "assets/MeshImportValidator.hpp"
#include <functional>
#include "ecs/CameraComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "animation/AnimationComponents.hpp"
#include "scene/SceneComponents.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/LightingSystem.hpp"
#include "scene/CpuDirectionalShadowMap.hpp"
#include "scene/TerrainSystem.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainSculptor.hpp"
#include "terrain/TerrainSplatPainter.hpp"
#include "terrain/TerrainLodSystem.hpp"
#include "terrain/TerrainGpuTextures.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "editor/EditorPaths.hpp"
#include "editor/EditorPanelUtils.hpp"
#include "ui/UIRenderer.hpp"
#include "spatial/Octree.hpp"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <array>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <stb/stb_image.h>
#ifdef CF_HAS_SDL3
#include <imgui_impl_sdlgpu3.h>
#endif

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

namespace {

constexpr f32 kDegToRad = 3.14159265f / 180.0f;
constexpr f32 kRadToDeg = 180.0f / 3.14159265f;

std::string resolveProjectRootFromScenePath(const std::string& scenePath) {
    if (scenePath.empty()) return {};
    const auto sceneDir = std::filesystem::path(scenePath).parent_path();
    const std::string root = sceneDir.parent_path().string();
    return root.empty() ? sceneDir.string() : root;
}

Mat4 buildLocalMatrix(const ECS::Transform& t) {
    return Mat4::translation(t.position)
         * Mat4::rotationZ(t.rotation.z * kDegToRad)
         * Mat4::rotationY(t.rotation.y * kDegToRad)
         * Mat4::rotationX(t.rotation.x * kDegToRad)
         * Mat4::scale(t.scale.x, t.scale.y, t.scale.z);
}

Mat4 buildLocalMatrix3D(const ECS::Position3D* p, const ECS::Rotation3D* r, const ECS::Scale3D* s) {
    Mat4 T = p ? Mat4::translation(p->position) : Mat4::identity();
    Mat4 R = r ? Quat(r->quaternion.x, r->quaternion.y, r->quaternion.z, r->quaternion.w).normalized().toMatrix()
               : Mat4::identity();
    Mat4 S = s ? Mat4::scale(s->scale.x, s->scale.y, s->scale.z) : Mat4::identity();
    return T * R * S;
}

struct ViewportRay {
    Vec3 origin;
    Vec3 direction;
};

ViewportRay computeViewportRay(const EditorContext& ctx, ImVec2 viewportOrigin, ImVec2 viewportSize,
                               ImVec2 mousePos) {
    const f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
    const f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
    const Vec3 camPos = ctx.camFocus + Vec3(sinY * cosP, -sinP, -cosY * cosP) * ctx.camDistance;
    const Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
    const f32 aspect = viewportSize.x / std::max(viewportSize.y, 1.0f);
    const Mat4 proj = Mat4::perspective(1.0472f, aspect, 0.1f, ctx.cameraFarPlane());
    const Mat4 vp = proj * view;
    const Mat4 vpInverse = vp.inverted();

    const f32 ndcX = (2.0f * (mousePos.x - viewportOrigin.x)) / viewportSize.x - 1.0f;
    const f32 ndcY = 1.0f - (2.0f * (mousePos.y - viewportOrigin.y)) / viewportSize.y;
    Vec4 worldNear = vpInverse.transformVec4(Vec4(ndcX, ndcY, -1.0f, 1.0f));
    if (std::abs(worldNear.w) > 0.0001f) {
        worldNear.x /= worldNear.w;
        worldNear.y /= worldNear.w;
        worldNear.z /= worldNear.w;
    }

    ViewportRay ray;
    ray.origin = camPos;
    ray.direction = (Vec3(worldNear.x, worldNear.y, worldNear.z) - camPos).normalized();
    return ray;
}

Mat4 entityMatrix(ECS::World& world, ECS::Entity entity) {
    if (auto* wt = world.get<Scene::WorldTransform>(entity)) return wt->matrix;
    if (auto* t = world.get<ECS::Transform>(entity)) return buildLocalMatrix(*t);
    auto* p3 = world.get<ECS::Position3D>(entity);
    auto* r3 = world.get<ECS::Rotation3D>(entity);
    auto* s3 = world.get<ECS::Scale3D>(entity);
    return buildLocalMatrix3D(p3, r3, s3);
}

bool tryGetEntityPosition(ECS::World& world, ECS::Entity entity, Vec3& outPosition) {
    if (auto* wt = world.get<Scene::WorldTransform>(entity)) {
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

struct EntityFocusBounds {
    Vec3 center{};
    f32 radius = 2.5f;
};

f32 maxScaleFromEntity(ECS::World& world, ECS::Entity entity) {
    if (auto* s3 = world.get<ECS::Scale3D>(entity)) {
        return std::max({s3->scale.x, s3->scale.y, s3->scale.z});
    }
    if (auto* t = world.get<ECS::Transform>(entity)) {
        return std::max({t->scale.x, t->scale.y, t->scale.z});
    }
    return 1.0f;
}

EntityFocusBounds computeEntityFocusBounds(ECS::World& world, ECS::Entity entity) {
    EntityFocusBounds bounds;
    Vec3 origin;
    if (!tryGetEntityPosition(world, entity, origin)) {
        return bounds;
    }

    if (auto* terrain = world.get<ECS::TerrainComponent>(entity)) {
        const f32 halfX = terrain->worldSizeX * 0.5f;
        const f32 halfZ = terrain->worldSizeZ * 0.5f;
        const f32 halfY = terrain->maxHeight * 0.5f;
        bounds.center = origin + Vec3(0.0f, halfY, 0.0f);
        bounds.radius = std::sqrt(halfX * halfX + halfY * halfY + halfZ * halfZ);
        return bounds;
    }

    bounds.center = origin;
    bounds.radius = std::max(1.5f, maxScaleFromEntity(world, entity) * 1.25f);

    if (auto* filter = world.get<ECS::MeshFilterComponent>(entity)) {
        switch (filter->primitive) {
            case ECS::MeshPrimitive::Sphere:
                bounds.radius = std::max(bounds.radius, 1.0f);
                break;
            case ECS::MeshPrimitive::Capsule:
            case ECS::MeshPrimitive::Cylinder:
                bounds.radius = std::max(bounds.radius, 2.0f);
                break;
            case ECS::MeshPrimitive::Plane:
                bounds.radius = std::max(bounds.radius, 5.0f);
                break;
            case ECS::MeshPrimitive::Cube:
            case ECS::MeshPrimitive::Custom:
            default:
                break;
        }
    }

    return bounds;
}

f32 focusDistanceForBounds(const EntityFocusBounds& bounds, f32 fovDeg = 60.0f) {
    constexpr f32 kDegToRad = 3.14159265f / 180.0f;
    const f32 halfFov = std::max(0.05f, fovDeg * kDegToRad * 0.5f);
    const f32 distance = bounds.radius / std::sin(halfFov);
    return std::clamp(distance * 1.35f, 3.0f, 5000.0f);
}

void frameEntityInViewport(EditorContext& ctx, ECS::World& world, ECS::Entity entity) {
    const EntityFocusBounds bounds = computeEntityFocusBounds(world, entity);
    ctx.camFocus = bounds.center;
    ctx.camDistance = focusDistanceForBounds(bounds);
    TestInstrumentation::onCameraFocused(bounds.center, ctx.camDistance);
}

Vec3 matrixAxis(const Mat4& m, int column, const Vec3& fallback) {
    Vec3 axis(m(0, column), m(1, column), m(2, column));
    const f32 lenSq = axis.lengthSquared();
    return (lenSq > 0.000001f) ? axis / std::sqrt(lenSq) : fallback;
}

Vec3 entityAxis(ECS::World& world, ECS::Entity entity, int column, const Vec3& fallback) {
    return matrixAxis(entityMatrix(world, entity), column, fallback);
}

Vec3 entityForward(ECS::World& world, ECS::Entity entity) {
    return -1.0f * entityAxis(world, entity, 2, Vec3(0.0f, 0.0f, -1.0f));
}

ImU32 lightColor(const ECS::LightComponent& lc, bool selected) {
    const f32 alphaScale = selected ? 1.0f : 0.85f;
    return IM_COL32(
        static_cast<ImU8>(std::clamp(lc.color.x, 0.0f, 1.0f) * 255.0f),
        static_cast<ImU8>(std::clamp(lc.color.y, 0.0f, 1.0f) * 255.0f),
        static_cast<ImU8>(std::clamp(lc.color.z, 0.0f, 1.0f) * 255.0f),
        static_cast<ImU8>(std::clamp(lc.color.w * lc.intensity * alphaScale, 0.0f, 1.0f) * 255.0f));
}

Vec3 sampleTextureRgb(const MeshDrawTexture& tex, const Vec2& uv) {
    if (!tex.pixels || tex.width == 0 || tex.height == 0) {
        return Vec3(0.39f, 0.59f, 0.78f);
    }

    const int channels = tex.channels > 0 ? tex.channels : 3;
    const u32 tw = tex.width;
    const u32 th = tex.height;

    const f32 u = uv.x - std::floor(uv.x);
    const f32 v = uv.y - std::floor(uv.y);
    const f32 sampleV = tex.flipV ? (1.0f - v) : v;
    const u32 x = static_cast<u32>(std::clamp(u, 0.0f, 1.0f) * (tw - 1));
    const u32 y = static_cast<u32>(std::clamp(sampleV, 0.0f, 1.0f) * (th - 1));
    const u32 idx = (y * tw + x) * static_cast<u32>(channels);
    const size_t pixelBytes = static_cast<size_t>(tw) * static_cast<size_t>(th) * static_cast<size_t>(channels);
    if (idx + static_cast<u32>(channels - 1) >= pixelBytes) {
        return Vec3(0.39f, 0.59f, 0.78f);
    }

    Vec3 rgb(static_cast<f32>(tex.pixels[idx]) / 255.0f,
             static_cast<f32>(tex.pixels[idx + 1]) / 255.0f,
             static_cast<f32>(tex.pixels[idx + 2]) / 255.0f);
    if (tex.hasMaterial) {
        rgb.x *= tex.materialAlbedo.x;
        rgb.y *= tex.materialAlbedo.y;
        rgb.z *= tex.materialAlbedo.z;
    }
    return rgb;
}

ImU32 shadeTextureRgb(const Vec3& rgb, const Vec3& lightRgb, const MeshDrawTexture& tex) {
    const f32 ambient = 0.5f;
    const f32 lit = 0.5f;
    const f32 spec = tex.hasMaterial
        ? tex.materialMetallic * (1.0f - tex.materialRoughness) * 0.35f
        : 0.0f;
    return IM_COL32(
        static_cast<ImU8>(std::clamp((rgb.x * ambient + rgb.x * lightRgb.x * lit + spec) * 255.0f, 0.0f, 255.0f)),
        static_cast<ImU8>(std::clamp((rgb.y * ambient + rgb.y * lightRgb.y * lit + spec) * 255.0f, 0.0f, 255.0f)),
        static_cast<ImU8>(std::clamp((rgb.z * ambient + rgb.z * lightRgb.z * lit + spec) * 255.0f, 0.0f, 255.0f)),
        255);
}

ImU32 sampleDrawTexture(const MeshDrawTexture& tex, const Vec2& uv, const Vec3& lightRgb) {
    if (!tex.pixels || tex.width == 0 || tex.height == 0) {
        const f32 a = 0.35f;
        return IM_COL32(
            static_cast<ImU8>(100.0f * a + lightRgb.x * 255.0f * (1.0f - a)),
            static_cast<ImU8>(150.0f * a + lightRgb.y * 255.0f * (1.0f - a)),
            static_cast<ImU8>(200.0f * a + lightRgb.z * 255.0f * (1.0f - a)),
            255);
    }
    return shadeTextureRgb(sampleTextureRgb(tex, uv), lightRgb, tex);
}

ImU32 sampleTerrainSurfaceColor(const TerrainSplatDrawContext& splat,
                                const Vec3& worldPos,
                                const Vec2& uv,
                                const MeshDrawTexture& fallback,
                                const Vec3& lightRgb) {
    if (!splat.active || !splat.splatmap || !splat.settings) {
        return sampleDrawTexture(fallback, uv, lightRgb);
    }

    const Vec3 local = splat.worldMatrixInverse.transformPoint(worldPos);
    const Vec4 weights = splat.splatmap->sampleWorldXZ(local.x, local.z,
                                                       splat.settings->worldSizeX,
                                                       splat.settings->worldSizeZ);

    Vec3 albedo(0.0f, 0.0f, 0.0f);
    for (u32 i = 0; i < splat.layerCount; ++i) {
        const f32 w = (i == 0) ? weights.x :
                      (i == 1) ? weights.y :
                      (i == 2) ? weights.z : weights.w;
        if (w <= 1e-4f || !splat.layers[i].pixels) continue;
        albedo += sampleTextureRgb(splat.layers[i], uv) * w;
    }

    if (albedo.x + albedo.y + albedo.z <= 1e-4f) {
        return sampleDrawTexture(fallback, uv, lightRgb);
    }
    return shadeTextureRgb(albedo, lightRgb, fallback);
}

ImVec2 projectVP(const Mat4& vp, ImVec2 origin, ImVec2 viewportSize, const Vec3& p) {
    Vec4 clip = vp.transformVec4(Vec4(p.x, p.y, p.z, 1.0f));
    if (clip.w <= 0.1f) return ImVec2(-10000.0f, -10000.0f);
    const f32 ndcX = clip.x / clip.w;
    const f32 ndcY = clip.y / clip.w;
    return ImVec2(
        origin.x + (ndcX + 1.0f) * 0.5f * viewportSize.x,
        origin.y + (1.0f - ndcY) * 0.5f * viewportSize.y
    );
}

f32 edgeFunction(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy) {
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

struct RasterVert {
    f32 sx = 0.0f;
    f32 sy = 0.0f;
    f32 ndcZ = 0.0f;
    f32 invW = 0.0f;
    Vec3 worldPos;
    Vec3 worldNormal;
    Vec2 uv;
};

void rasterizeTriangleCpu(
    MeshCpuRasterizer& fb,
    const RasterVert& v0, const RasterVert& v1, const RasterVert& v2,
    const Vec3& faceNormal, const Vec3& camPos,
    const MeshDrawTexture& tex,
    bool receiveShadows,
    const std::function<Vec3(const Vec3&, const Vec3&, bool)>& lightColorAt,
    const TerrainSplatDrawContext* splatContext = nullptr) {
    const Vec3 center = (v0.worldPos + v1.worldPos + v2.worldPos) * (1.0f / 3.0f);
    const f32 area = edgeFunction(v0.sx, v0.sy, v1.sx, v1.sy, v2.sx, v2.sy);
    if (std::abs(area) < 1e-4f) return;

    const int minX = std::max(0, static_cast<int>(std::floor(std::min({v0.sx, v1.sx, v2.sx}))));
    const int maxX = std::min(fb.width - 1, static_cast<int>(std::ceil(std::max({v0.sx, v1.sx, v2.sx}))));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min({v0.sy, v1.sy, v2.sy}))));
    const int maxY = std::min(fb.height - 1, static_cast<int>(std::ceil(std::max({v0.sy, v1.sy, v2.sy}))));

    for (int y = minY; y <= maxY; ++y) {
        const f32 py = static_cast<f32>(y) + 0.5f;
        for (int x = minX; x <= maxX; ++x) {
            const f32 px = static_cast<f32>(x) + 0.5f;
            const f32 w0 = edgeFunction(v1.sx, v1.sy, v2.sx, v2.sy, px, py) / area;
            const f32 w1 = edgeFunction(v2.sx, v2.sy, v0.sx, v0.sy, px, py) / area;
            const f32 w2 = edgeFunction(v0.sx, v0.sy, v1.sx, v1.sy, px, py) / area;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

            const f32 invWInterp = w0 * v0.invW + w1 * v1.invW + w2 * v2.invW;
            if (invWInterp <= 1e-8f) continue;

            const f32 z = (w0 * v0.ndcZ * v0.invW + w1 * v1.ndcZ * v1.invW + w2 * v2.ndcZ * v2.invW) / invWInterp;
            const int depthIdx = y * fb.width + x;
            if (z >= fb.depth[depthIdx]) continue;

            const f32 u = (w0 * v0.uv.x * v0.invW + w1 * v1.uv.x * v1.invW + w2 * v2.uv.x * v2.invW) / invWInterp;
            const f32 v = (w0 * v0.uv.y * v0.invW + w1 * v1.uv.y * v1.invW + w2 * v2.uv.y * v2.invW) / invWInterp;

            const Vec3 worldPos = v0.worldPos * w0 + v1.worldPos * w1 + v2.worldPos * w2;
            Vec3 worldNormal = v0.worldNormal * w0 + v1.worldNormal * w1 + v2.worldNormal * w2;
            const f32 normalLenSq = worldNormal.lengthSquared();
            if (normalLenSq > 1e-8f) {
                worldNormal = worldNormal / std::sqrt(normalLenSq);
            } else {
                worldNormal = faceNormal;
            }

            const Vec3 lightRgb = lightColorAt(worldPos, worldNormal, receiveShadows);
            const ImU32 col = splatContext && splatContext->active
                ? sampleTerrainSurfaceColor(*splatContext, worldPos, Vec2(u, v), tex, lightRgb)
                : sampleDrawTexture(tex, Vec2(u, v), lightRgb);
            fb.depth[depthIdx] = z;
            const int colorIdx = depthIdx * 4;
            fb.color[colorIdx + 0] = static_cast<u8>((col >> IM_COL32_R_SHIFT) & 0xFF);
            fb.color[colorIdx + 1] = static_cast<u8>((col >> IM_COL32_G_SHIFT) & 0xFF);
            fb.color[colorIdx + 2] = static_cast<u8>((col >> IM_COL32_B_SHIFT) & 0xFF);
            fb.color[colorIdx + 3] = 255;
        }
    }
}

} // namespace

constexpr int kMeshRasterMaxDim = 1920;

void SceneViewport::retireImTexture(std::unique_ptr<ImTextureData>& texture) {
#ifdef CF_HAS_SDL3
    if (!texture) return;
    if (texture->GetTexID() != ImTextureID_Invalid) {
        texture->UnusedFrames = 1;
        texture->SetStatus(ImTextureStatus_WantDestroy);
        ImGui_ImplSDLGPU3_UpdateTexture(texture.get());
    }
    m_retiredTextures.push_back(std::move(texture));
#endif
}

void SceneViewport::pruneRetiredTextures() {
#ifdef CF_HAS_SDL3
    m_retiredTextures.erase(
        std::remove_if(m_retiredTextures.begin(), m_retiredTextures.end(),
                       [](const std::unique_ptr<ImTextureData>& tex) {
                           return !tex || tex->Status == ImTextureStatus_Destroyed;
                       }),
        m_retiredTextures.end());
#endif
}

void MeshCpuRasterizer::begin(ImVec2 origin_, ImVec2 size, int maxDim) {
    origin = origin_;
    panelW = std::max(1, static_cast<int>(size.x));
    panelH = std::max(1, static_cast<int>(size.y));

    const int rasterCap = std::max(64, maxDim);
    width = panelW;
    height = panelH;
    if (width > rasterCap || height > rasterCap) {
        const f32 scale = static_cast<f32>(rasterCap) /
                          static_cast<f32>(std::max(width, height));
        width = std::max(1, static_cast<int>(static_cast<f32>(width) * scale));
        height = std::max(1, static_cast<int>(static_cast<f32>(height) * scale));
    }

    color.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    depth.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
}

void MeshCpuRasterizer::clear(u8 r, u8 g, u8 b, u8 a) {
    for (size_t i = 0; i < color.size(); i += 4) {
        color[i + 0] = r;
        color[i + 1] = g;
        color[i + 2] = b;
        color[i + 3] = a;
    }
    std::fill(depth.begin(), depth.end(), 1.1f);
}

void SceneViewport::blitMeshRasterizer(ImDrawList* dl, MeshCpuRasterizer& rasterizer,
                                       SpriteTextureCacheEntry& texEntry) {
    if (!dl || rasterizer.width <= 0 || rasterizer.height <= 0) return;

#ifdef CF_HAS_SDL3
    const bool sizeChanged = !texEntry.texture ||
        texEntry.width != rasterizer.width ||
        texEntry.height != rasterizer.height;

    if (sizeChanged) {
        retireImTexture(texEntry.texture);
        texEntry.width = rasterizer.width;
        texEntry.height = rasterizer.height;
        texEntry.texture = std::make_unique<ImTextureData>();
        texEntry.texture->Create(ImTextureFormat_RGBA32, rasterizer.width, rasterizer.height);
        texEntry.loadFailed = false;
    }

    std::memcpy(texEntry.texture->GetPixels(), rasterizer.color.data(), rasterizer.color.size());

    if (texEntry.texture->Status == ImTextureStatus_WantCreate) {
        ImGui_ImplSDLGPU3_UpdateTexture(texEntry.texture.get());
    } else {
        texEntry.texture->UpdateRect.x = 0;
        texEntry.texture->UpdateRect.y = 0;
        texEntry.texture->UpdateRect.w = static_cast<unsigned short>(rasterizer.width);
        texEntry.texture->UpdateRect.h = static_cast<unsigned short>(rasterizer.height);
        texEntry.texture->SetStatus(ImTextureStatus_WantUpdates);
        ImGui_ImplSDLGPU3_UpdateTexture(texEntry.texture.get());
    }

    if (texEntry.texture->GetTexID() != ImTextureID_Invalid) {
        dl->AddImage(texEntry.texture->GetTexRef(), rasterizer.origin,
                     ImVec2(rasterizer.origin.x + static_cast<f32>(rasterizer.panelW),
                            rasterizer.origin.y + static_cast<f32>(rasterizer.panelH)));
    }
#endif
}

static std::vector<ECS::Entity> getSceneEntities(ECS::World& world) {
    std::vector<ECS::Entity> entities;
    ECS::ComponentQuery q;
    world.forEach(q, [&](ECS::Entity e) {
        entities.push_back(e);
    });
    return entities;
}

static void processTestCommand(const std::string& cmd, ECS::World& world, EditorContext& ctx) {
    if (cmd.find("select_entity ") == 0) {
        try {
            u32 id = std::stoul(cmd.substr(13));
            ECS::Entity entity(id, &world);
            ctx.selectEntity(entity);
            TestInstrumentation::onEntitiesSelected(ctx.selectedEntities);
        } catch (...) {}
    }
    else if (cmd.find("multi_select ") == 0) {
        try {
            u32 id = std::stoul(cmd.substr(12));
            ECS::Entity entity(id, &world);
            ctx.toggleSelection(entity);
            TestInstrumentation::onEntitiesSelected(ctx.selectedEntities);
        } catch (...) {}
    }
    else if (cmd == "delete_selected") {
        if (ctx.selectedEntity.isValid()) {
            world.destroy(ctx.selectedEntity);
            ctx.selectedEntity = ECS::Entity::INVALID;
            TestInstrumentation::onSceneEntities(getSceneEntities(world));
        }
    }
    else if (cmd == "focus_selected") {
        if (ctx.selectedEntity.isValid()) {
            frameEntityInViewport(ctx, world, ctx.selectedEntity);
        }
    }
    else if (cmd == "get_scene") {
        TestInstrumentation::onSceneEntities(getSceneEntities(world));
    }
}

// ── Init / Shutdown ───────────────────────────────────────────────

#ifdef CF_HAS_SDL3
bool SceneViewport::init(RHI::RenderDevice* device, Config cfg) {
    m_device = device;
    m_config = cfg;

    RHI::TextureDesc colorDesc;
    colorDesc.width  = cfg.width;
    colorDesc.height = cfg.height;
    colorDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    colorDesc.usage  = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    m_colorTarget = device->createTexture(colorDesc);

    RHI::TextureDesc depthDesc;
    depthDesc.width  = cfg.width;
    depthDesc.height = cfg.height;
    depthDesc.format = RHI::TextureFormat::D32_FLOAT;
    depthDesc.usage  = RHI::TextureUsage::DepthStencil;
    m_depthTarget = device->createTexture(depthDesc);

     m_initialized = (m_colorTarget != nullptr);
     if (m_initialized) {
         m_lastCanvasWidth = cfg.width;
         m_lastCanvasHeight = cfg.height;
         m_gpuSceneReady = m_gpuSceneRenderer.init(device);
     }
     return m_initialized;
}

void SceneViewport::resizeCanvasIfNeeded(u32 newWidth, u32 newHeight) {
    if (!m_device || newWidth < 1 || newHeight < 1) return;
    if (m_lastCanvasWidth == newWidth && m_lastCanvasHeight == newHeight) return;
    
    if (m_colorTarget) m_device->destroyTexture(m_colorTarget);
    if (m_depthTarget) m_device->destroyTexture(m_depthTarget);
    
    RHI::TextureDesc colorDesc;
    colorDesc.width  = newWidth;
    colorDesc.height = newHeight;
    colorDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    colorDesc.usage  = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    m_colorTarget = m_device->createTexture(colorDesc);
    
    RHI::TextureDesc depthDesc;
    depthDesc.width  = newWidth;
    depthDesc.height = newHeight;
    depthDesc.format = RHI::TextureFormat::D32_FLOAT;
    depthDesc.usage  = RHI::TextureUsage::DepthStencil;
    m_depthTarget = m_device->createTexture(depthDesc);
    
    m_lastCanvasWidth = newWidth;
    m_lastCanvasHeight = newHeight;
}

void SceneViewport::resizePreviewCanvasIfNeeded(u32 newWidth, u32 newHeight) {
    if (!m_device || newWidth < 1 || newHeight < 1) return;
    if (m_previewCanvasWidth == newWidth && m_previewCanvasHeight == newHeight) return;

    if (m_previewColorTarget) m_device->destroyTexture(m_previewColorTarget);
    if (m_previewDepthTarget) m_device->destroyTexture(m_previewDepthTarget);

    RHI::TextureDesc colorDesc;
    colorDesc.width  = newWidth;
    colorDesc.height = newHeight;
    colorDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    colorDesc.usage  = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    m_previewColorTarget = m_device->createTexture(colorDesc);

    RHI::TextureDesc depthDesc;
    depthDesc.width  = newWidth;
    depthDesc.height = newHeight;
    depthDesc.format = RHI::TextureFormat::D32_FLOAT;
    depthDesc.usage  = RHI::TextureUsage::DepthStencil;
    m_previewDepthTarget = m_device->createTexture(depthDesc);

    m_previewCanvasWidth = newWidth;
    m_previewCanvasHeight = newHeight;
}

bool SceneViewport::renderCameraPreviewGpu(RHI::CommandBuffer* cmd, ECS::World& world,
                                            EditorContext& ctx, const Mat4& view,
                                            const Mat4& proj, const Vec3& cameraPos,
                                            const Vec3& focus, f32 fovRad, f32 nearClip,
                                            f32 farClip, u32 width, u32 height,
                                            const std::string& projectRoot) {
    if (!cmd || !m_gpuSceneReady || !m_useGpuScene || width < 1 || height < 1) return false;

    resizePreviewCanvasIfNeeded(width, height);
    if (!m_previewColorTarget || !m_previewDepthTarget) return false;

    Render::GpuSceneCamera camera;
    camera.position = cameraPos;
    camera.focus = focus;
    camera.view = view;
    camera.proj = proj;
    camera.fovRad = fovRad;
    camera.nearClip = nearClip;
    camera.farClip = farClip;

    m_gpuSceneRenderer.renderWithCamera(
        cmd, world, camera, m_previewColorTarget, m_previewDepthTarget, width, height, projectRoot);
    (void)ctx;
    return m_previewColorTarget && m_previewColorTarget->handle != nullptr;
}

void SceneViewport::shutdown() {
     releaseSpriteTextures();
     if (!m_initialized || !m_device) return;
     m_gpuSceneRenderer.shutdown();
     m_gpuSceneReady = false;
     m_device->destroyTexture(m_colorTarget);
     m_device->destroyTexture(m_depthTarget);
     m_device->destroyTexture(m_previewColorTarget);
     m_device->destroyTexture(m_previewDepthTarget);
     m_colorTarget = nullptr;
     m_depthTarget = nullptr;
     m_previewColorTarget = nullptr;
     m_previewDepthTarget = nullptr;
     m_initialized = false;
 }
#endif

// ── Main render entry point ───────────────────────────────────────

void SceneViewport::render(ECS::World& world, EditorContext& ctx) {
    // Make stdin non-blocking on first call in test mode
    static bool stdinConfigured = false;
    if (TestInstrumentation::isTestMode() && !stdinConfigured) {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        stdinConfigured = true;
    }
    
    if (TestInstrumentation::isTestMode()) {
        static std::string buffer;
        int ch;
        while ((ch = fgetc(stdin)) != EOF && ch != '\n') {
            buffer += static_cast<char>(ch);
        }
        if (ch == '\n' && !buffer.empty()) {
            TestRequestHandler::Request req;
            if (TestRequestHandler::tryParseRequest(buffer, req)) {
                ImVec2 viewportPos = ImGui::GetCursorScreenPos();
                ImVec2 viewportSize = ImGui::GetContentRegionAvail();
                
                auto resp = TestRequestHandler::handleRequest(
                    req, world, ctx,
                    viewportPos.x, viewportPos.y,
                    viewportSize.x, viewportSize.y
                );
                
                std::cout << "REQUEST_RESPONSE: " << resp.toJson() << std::endl;
            }
            buffer.clear();
        }
    }
    
    if (!m_open) return;

    CF_PROFILE_SCOPE("SceneViewport::render");
    pruneRetiredTextures();

    editorPanelApplyDetach(m_detached, ImVec2(1280, 720));
    if (!ImGui::Begin("Scene Viewport", &m_open,
            (ctx.viewMode == EditorContext::ViewMode::Mode3D ||
             ctx.viewMode == EditorContext::ViewMode::Isometric)
                ? ImGuiWindowFlags_NoNavInputs
                : ImGuiWindowFlags_None)) {
        ImGui::End();
        return;
    }
    editorPanelDetachTabButton(m_detached);

#ifdef CF_HAS_SDL3
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x < 1 || viewportSize.y < 1) {
        ImGui::End();
        return;
    }
    
    resizeCanvasIfNeeded((u32)viewportSize.x, (u32)viewportSize.y);

    if (ctx.viewMode == EditorContext::ViewMode::Mode3D) {
        Scene::syncTerrainMeshes(world);
    }

    const std::string projectRoot = resolveProjectRootFromScenePath(ctx.currentScenePath);
    u32 gpuMeshDrawCount = 0;
    // Textured meshes are composited via CPU raster on top of the skybox. The GPU
    // offscreen pass sits under that skybox layer, so enabling it disables the CPU
    // overlay and makes meshes vanish after the first successful GPU upload.
    const bool gpuSceneActive = m_frameCmd && m_gpuSceneReady && m_useGpuScene && m_colorTarget &&
                                m_depthTarget && ctx.viewMode == EditorContext::ViewMode::Mode3D;
    if (gpuSceneActive) {
        Caffeine::Debug::setCrashBreadcrumb("SceneViewport::gpuSceneRenderer");
        gpuMeshDrawCount = m_gpuSceneRenderer.render(m_frameCmd, world, ctx, m_colorTarget,
                                                     m_depthTarget, m_lastCanvasWidth,
                                                     m_lastCanvasHeight, projectRoot);
        Caffeine::Debug::setCrashBreadcrumb("SceneViewport::gpuSceneRenderer.done");
    }
    ImGui::Dummy(viewportSize);
    m_lastGpuSceneActive = gpuSceneActive;
    m_lastGpuMeshDrawCount = gpuMeshDrawCount;
#else
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x < 1 || viewportSize.y < 1) {
        ImGui::End();
        return;
    }
    ImGui::Dummy(viewportSize);
#endif

    // ── Drag-drop target for assets ──
    if (const auto* asset = DragDropManager::AcceptAssetDrop()) {
        std::filesystem::path assetPath(asset->path);
        bool canDrop = true;
        if (asset->type == AssetType::Mesh) {
            const auto report = Assets::MeshImportValidator::analyze(assetPath);
            if (!report.readyToLoad) {
                std::string msg = report.errorSummary;
                if (!report.suggestion.empty()) {
                    msg += " — " + report.suggestion;
                }
                ctx.pushTransientStatus(msg, true);
                canDrop = false;
            }
        }

        if (canDrop) {
            ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);

            const bool is3D = (ctx.viewMode == EditorContext::ViewMode::Mode3D ||
                               ctx.viewMode == EditorContext::ViewMode::Isometric);
            ImVec2 mousePos   = ImGui::GetMousePos();
            ImVec2 viewportTL = ImGui::GetItemRectMin();
            Vec3 dropPos(0.0f, 0.0f, 0.0f);

            if (is3D) {
                const f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
                const f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
                const Vec3 camPos = ctx.camFocus + Vec3(sinY * cosP, -sinP, -cosY * cosP) * ctx.camDistance;
                const Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
                const f32 aspect = viewportSize.x / std::max(viewportSize.y, 1.0f);
                const Mat4 proj = Mat4::perspective(1.0472f, aspect, 0.1f, ctx.cameraFarPlane());
                const Mat4 vp = proj * view;
                const Mat4 vpInverse = vp.inverted();

                const f32 ndcX = (2.0f * (mousePos.x - viewportTL.x)) / viewportSize.x - 1.0f;
                const f32 ndcY = 1.0f - (2.0f * (mousePos.y - viewportTL.y)) / viewportSize.y;
                Vec4 worldNear = vpInverse.transformVec4(Vec4(ndcX, ndcY, -1.0f, 1.0f));
                Vec4 worldFar  = vpInverse.transformVec4(Vec4(ndcX, ndcY,  1.0f, 1.0f));
                if (std::abs(worldNear.w) > 0.0001f) {
                    worldNear.x /= worldNear.w; worldNear.y /= worldNear.w; worldNear.z /= worldNear.w;
                }
                if (std::abs(worldFar.w) > 0.0001f) {
                    worldFar.x /= worldFar.w; worldFar.y /= worldFar.w; worldFar.z /= worldFar.w;
                }
                const Vec3 rayDir = (Vec3(worldFar.x, worldFar.y, worldFar.z) - camPos).normalized();
                if (std::abs(rayDir.y) > 1e-5f) {
                    const f32 t = -camPos.y / rayDir.y;
                    if (t > 0.0f) {
                        dropPos = camPos + rayDir * t;
                    }
                }
            } else {
                const f32 localX = (mousePos.x - viewportTL.x) - viewportSize.x * 0.5f;
                const f32 localY = (mousePos.y - viewportTL.y) - viewportSize.y * 0.5f;
                const f32 scale  = ctx.viewportZoom * 50.0f;
                dropPos.x = (localX - ctx.viewportPanX) / scale;
                dropPos.y = -(localY - ctx.viewportPanY) / scale;
            }

            if (asset->type == AssetType::Prefab) {
                ECS::Entity root = PrefabSystem::Instantiate(world, asset->path, dropPos);
                ctx.selectedEntity = root;
            } else {
                ECS::Entity entity = world.create();
                setEntityName(world, entity, assetPath.stem().string().c_str());

                if (is3D) {
                    world.add<ECS::Position3D>(entity, dropPos);
                    world.add<ECS::Rotation3D>(entity);
                    world.add<ECS::Scale3D>(entity);
                } else {
                    auto t = ECS::Transform{};
                    t.position.x = dropPos.x;
                    t.position.y = dropPos.y;
                    world.add<ECS::Transform>(entity, t);
                }

                if (asset->type == AssetType::Texture) {
                    world.add<ECS::Sprite>(entity, asset->path, 0);
                }

                if (asset->type == AssetType::Audio) {
                    auto& emitter = world.add<Audio::AudioEmitter>(entity);
                    emitter.clipPath = assetPath.filename().string().c_str();
                }

                if (asset->type == AssetType::Mesh) {
                    world.add<ECS::MeshFilterComponent>(entity,
                        ECS::MeshPrimitive::Custom, assetPath.string());
                    world.add<ECS::MeshRendererComponent>(entity,
                        assetPath.string(), "");
                }

                ctx.selectedEntity = entity;
            }

            ctx.endUndo(world);
        }
    }

    bool hovered = ImGui::IsItemHovered();

    const bool terrainSelected = ctx.selectedEntity.isValid() &&
                                 world.has<ECS::TerrainComponent>(ctx.selectedEntity) &&
                                 ctx.viewMode == EditorContext::ViewMode::Mode3D;
    const bool terrainSculptMode = terrainSelected &&
                                   ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt;
    const bool terrainSplatMode = terrainSelected &&
                                    ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat;
    const bool terrainEditActive = terrainSculptMode || terrainSplatMode;

    // Handle entity selection via raycasting (only in 3D mode, not during gizmo drag)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_gizmoDragging && !terrainEditActive &&
        ctx.viewMode == EditorContext::ViewMode::Mode3D) {
        
        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 vpMin = ImGui::GetItemRectMin();
        ImVec2 vpMax = ImGui::GetItemRectMax();
        
        bool mouseInViewport = (mousePos.x >= vpMin.x && mousePos.x <= vpMax.x &&
                                mousePos.y >= vpMin.y && mousePos.y <= vpMax.y);
        
        if (mouseInViewport) {
            ImVec2 vpSize = ImGui::GetContentRegionAvail();
            Vec2 screenClick(mousePos.x - vpMin.x, mousePos.y - vpMin.y);
            
            f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
            f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
            Vec3 camPos = ctx.camFocus + Vec3(sinY * cosP, -sinP, -cosY * cosP) * ctx.camDistance;
            Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
            f32 aspect = vpSize.x / std::max(vpSize.y, 1.0f);
            Mat4 proj = Mat4::perspective(1.0472f, aspect, 0.1f, ctx.cameraFarPlane());
            Mat4 vp = proj * view;
            Mat4 vpInverse = vp.inverted();
            
            f32 ndcX = (2.0f * screenClick.x) / vpSize.x - 1.0f;
            f32 ndcY = 1.0f - (2.0f * screenClick.y) / vpSize.y;
            Vec4 ndcNear(ndcX, ndcY, -1.0f, 1.0f);
            Vec4 worldNear = vpInverse.transformVec4(ndcNear);
            
            if (std::abs(worldNear.w) > 0.0001f) {
                worldNear.x /= worldNear.w;
                worldNear.y /= worldNear.w;
                worldNear.z /= worldNear.w;
            }
            
            Vec3 rayOrigin = camPos;
            Vec3 rayDirection = (Vec3(worldNear.x, worldNear.y, worldNear.z) - camPos).normalized();
            
            std::string projectRoot;
            if (!ctx.currentScenePath.empty()) {
                projectRoot = resolveProjectRootFromScenePath(ctx.currentScenePath);
            }
            ECS::Entity selectedEntity = raycastSelectEntity(rayOrigin, rayDirection, world, projectRoot);
            
            bool shiftPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
            
            if (selectedEntity.isValid()) {
                if (shiftPressed) {
                    ctx.toggleSelection(selectedEntity);
                } else {
                    ctx.selectEntity(selectedEntity);
                }
                TestInstrumentation::onEntitiesSelected(ctx.selectedEntities);
            } else {
                if (!shiftPressed) {
                    ctx.clearSelection();
                }
            }
        }
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_T)) ctx.gizmoMode = EditorContext::GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) ctx.gizmoMode = EditorContext::GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) ctx.gizmoMode = EditorContext::GizmoMode::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) ctx.gizmoMode = EditorContext::GizmoMode::None;
        
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && ctx.selectedEntity.isValid()) {
            ctx.beginUndo(EditorCommand::RemoveEntity, ctx.selectedEntity.id(), world);
            world.destroy(ctx.selectedEntity);
            ctx.selectedEntity = ECS::Entity::INVALID;
            ctx.endUndo(world);
            TestInstrumentation::onSceneEntities(getSceneEntities(world));
        }
    }

    // Frame selected entity only on double-click inside the viewport. ImGui reports double-clicks
    // globally, so clicks on Terrain Editor (Seed, Gerar, etc.) must not reset the camera.
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
        ctx.selectedEntity.isValid() && ctx.viewMode == EditorContext::ViewMode::Mode3D) {
        frameEntityInViewport(ctx, world, ctx.selectedEntity);
    }

    bool leftDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if ((hovered || m_gizmoDragging) && ctx.selectedEntity.isValid() &&
        ctx.gizmoMode != EditorContext::GizmoMode::None && !terrainEditActive) {
        if (leftDragging && !m_gizmoDragging) {
            ctx.beginUndo(EditorCommand::SetField, ctx.selectedEntity.id(), world);
            m_gizmoDragging = true;
        }
        if (leftDragging) {
            handleGizmoInput(world, ctx, viewportSize);
        }
    }
    if (!leftDown && m_gizmoDragging) {
        ctx.endUndo(world);
        m_gizmoDragging = false;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetItemRectMin();

    m_terrainBrushHitValid = false;
    if (terrainEditActive) {
        ImVec2 mousePos = ImGui::GetMousePos();
        const bool mouseInViewport = mousePos.x >= origin.x && mousePos.x <= origin.x + viewportSize.x &&
                                     mousePos.y >= origin.y && mousePos.y <= origin.y + viewportSize.y;

        if (mouseInViewport && (leftDown || hovered)) {
            const ViewportRay ray = computeViewportRay(ctx, origin, viewportSize, mousePos);
            auto* terrain = world.get<ECS::TerrainComponent>(ctx.selectedEntity);
            auto* heightmap = Terrain::TerrainCache::instance().heightmapFor(ctx.selectedEntity);
            if (terrain && heightmap) {
                const Mat4 worldMatrix = entityMatrix(world, ctx.selectedEntity);
                Vec3 hitWorld;
                if (Terrain::TerrainSculptor::raycast(worldMatrix, *heightmap, *terrain,
                                                       ray.origin, ray.direction, hitWorld)) {
                    m_terrainBrushHitWorld = hitWorld;
                    m_terrainBrushHitValid = true;

                    if (leftDown) {
                        if (!m_terrainSculptDragging) {
                            ctx.beginUndo(EditorCommand::SetField, ctx.selectedEntity.id(), world);
                            m_terrainSculptDragging = true;
                        }

                        if (terrainSculptMode) {
                            Terrain::TerrainBrushSettings brush;
                            switch (ctx.terrainBrushMode) {
                                case EditorContext::TerrainBrushMode::Raise:
                                    brush.mode = Terrain::TerrainBrushMode::Raise; break;
                                case EditorContext::TerrainBrushMode::Lower:
                                    brush.mode = Terrain::TerrainBrushMode::Lower; break;
                                case EditorContext::TerrainBrushMode::Smooth:
                                    brush.mode = Terrain::TerrainBrushMode::Smooth; break;
                                case EditorContext::TerrainBrushMode::Flatten:
                                    brush.mode = Terrain::TerrainBrushMode::Flatten; break;
                                case EditorContext::TerrainBrushMode::Noise:
                                    brush.mode = Terrain::TerrainBrushMode::Noise; break;
                            }
                            brush.radius = ctx.terrainBrushRadius;
                            brush.strength = ctx.terrainBrushStrength;
                            Terrain::TerrainVertexBounds bounds;
                            Terrain::TerrainSculptor::applyBrush(*heightmap, *terrain, worldMatrix, hitWorld,
                                                                 brush, ImGui::GetIO().DeltaTime, &bounds);
                            if (bounds.valid) {
                                Terrain::TerrainCache::instance().rebuildRegion(
                                    ctx.selectedEntity, *terrain,
                                    bounds.minX, bounds.minZ, bounds.maxX, bounds.maxZ);
                            } else {
                                terrain->dataRevision++;
                            }
                            ctx.isDirty = true;
                        } else if (terrainSplatMode) {
                            auto* splatmap = Terrain::TerrainCache::instance().splatmapFor(ctx.selectedEntity);
                            if (splatmap) {
                                Terrain::TerrainSplatBrushSettings brush;
                                brush.targetLayer = std::min(ctx.terrainSplatLayer,
                                                             ECS::kTerrainSplatLayerCount - 1);
                                brush.radius = ctx.terrainBrushRadius;
                                brush.strength = ctx.terrainBrushStrength;
                                Terrain::TerrainSplatPainter::applyBrush(*splatmap, *terrain, worldMatrix,
                                                                         hitWorld, brush,
                                                                         ImGui::GetIO().DeltaTime);
                                terrain->splatRevision++;
                                ctx.isDirty = true;
                            }
                        }
                    }
                }
            }
        }
    }
    if (!leftDown && m_terrainSculptDragging) {
        ctx.endUndo(world);
        m_terrainSculptDragging = false;
    }

    const ImVec2 viewportMax(origin.x + viewportSize.x, origin.y + viewportSize.y);
    if (ctx.viewMode == EditorContext::ViewMode::Mode3D) {
        if (!drawSkybox(drawList, origin, viewportSize, world, ctx)) {
            drawList->AddRectFilledMultiColor(
                origin, viewportMax,
                IM_COL32(26, 26, 31, 255), IM_COL32(26, 26, 31, 255),
                IM_COL32(42, 48, 62, 255), IM_COL32(42, 48, 62, 255));
        }
        // Grid behind GPU terrain so it is occluded instead of looking transparent.
        drawGrid(drawList, origin, viewportSize, ctx);
    } else {
        drawList->AddRectFilled(origin, viewportMax, IM_COL32(26, 26, 31, 255));
    }

#ifdef CF_HAS_SDL3
    // GPU scene is rendered into an offscreen target with transparent clear.
    // Composite it after the skybox so terrain/meshes sit in front of the sky.
    if (m_lastGpuSceneActive && m_colorTarget && m_colorTarget->handle) {
        drawList->AddImage(reinterpret_cast<ImTextureID>(m_colorTarget->handle), origin, viewportMax,
                           ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE);
    }
#endif

    if (m_config.grid) {
        char modeStr[16];
        switch (ctx.gizmoMode) {
            case EditorContext::GizmoMode::Translate: strcpy(modeStr, "Translate"); break;
            case EditorContext::GizmoMode::Rotate:    strcpy(modeStr, "Rotate"); break;
            case EditorContext::GizmoMode::Scale:     strcpy(modeStr, "Scale"); break;
            default: strcpy(modeStr, "Select"); break;
        }
        char buf[80];
        snprintf(buf, sizeof(buf), "%sGizmo: %s [T/E/R]  Grid: %s  1 u = 1 m",
                 ctx.isPlayMode ? "PLAY  " : "",
                 modeStr, m_config.grid ? "ON" : "OFF");
        drawList->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(200, 200, 200, 200), buf);
    }

    {
        ImVec2 btnPos(origin.x + 8, origin.y + 28);
        ImGui::SetCursorScreenPos(btnPos);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        if (ctx.physicsDebugVisible) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.85f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 0.75f));
        }
        if (ImGui::Button("Physics")) {
            ctx.physicsDebugVisible = !ctx.physicsDebugVisible;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle physics collider debug overlay");
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ctx.snapToGrid) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.1f, 0.85f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 0.75f));
        }
        if (ImGui::Button("Snap")) {
            ctx.snapToGrid = !ctx.snapToGrid;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle snap to grid (%.1f units)", ctx.snapGridSize);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        const bool texturedPreview = (m_meshPreviewMode == MeshPreviewMode::Textured);
        if (texturedPreview) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.75f, 0.78f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 0.75f));
        }
        if (ImGui::Button(texturedPreview ? "Textured" : "Wireframe")) {
            m_meshPreviewMode = texturedPreview ? MeshPreviewMode::Wireframe : MeshPreviewMode::Textured;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle 3D preview style (white/gray textured vs wireframe)");
        if (texturedPreview) {
            ImGui::PopStyleColor(2);
        } else {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        const char* densityLabels[] = {"Low", "Medium", "High"};
        int wireDensity = static_cast<int>(m_wireframeDensity);
        ImGui::BeginDisabled(texturedPreview);
        ImGui::SetNextItemWidth(88.0f);
        if (ImGui::SliderInt("##wire_density", &wireDensity, 0, 2, densityLabels[wireDensity])) {
            m_wireframeDensity = static_cast<WireframeDensity>(wireDensity);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wireframe polygon density");
        ImGui::EndDisabled();

        if (ctx.viewMode == EditorContext::ViewMode::Mode3D) {
            ImGui::SameLine();
            if (ctx.skyboxEnabled) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.75f, 0.85f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 0.75f));
            }
            if (ImGui::Button("Sky")) {
                ctx.skyboxEnabled = !ctx.skyboxEnabled;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Toggle 3D skybox background");
            }
            ImGui::PopStyleColor();

            if (!Scene::hasSceneSkybox(world)) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(88.0f);
                int skyIdx = std::clamp(ctx.skyboxIndex, 0, ECS::kSkyboxPresetCount - 1);
                if (ImGui::Combo("##skybox", &skyIdx, ECS::kSkyboxPresetLabels, ECS::kSkyboxPresetCount)) {
                    ctx.skyboxIndex = skyIdx;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Fallback skybox preset (create a Skybox entity to save in scene)");
                }
            } else {
                ImGui::SameLine();
                ImGui::BeginDisabled();
                ImGui::Button("Scene Sky");
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Skybox is controlled by a Skybox entity in the scene");
                }
            }
        }

        ImGui::PopStyleVar();
    }

    if (ctx.viewMode == EditorContext::ViewMode::Mode3D &&
        ctx.selectedEntity.isValid() &&
        world.has<ECS::TerrainComponent>(ctx.selectedEntity)) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 8, origin.y + 52));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));

        auto terrainToolButton = [&](const char* label, bool active) -> bool {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.65f, 0.35f, 0.9f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 0.75f));
            }
            const bool pressed = ImGui::Button(label);
            ImGui::PopStyleColor();
            return pressed;
        };

        if (terrainToolButton("Sculpt",
                              ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt)) {
            ctx.terrainEditMode = (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt)
                ? EditorContext::TerrainEditMode::None
                : EditorContext::TerrainEditMode::Sculpt;
            if (ctx.terrainEditMode != EditorContext::TerrainEditMode::None) {
                ctx.gizmoMode = EditorContext::GizmoMode::None;
            }
        }
        ImGui::SameLine();
        if (terrainToolButton("Splat",
                              ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat)) {
            ctx.terrainEditMode = (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat)
                ? EditorContext::TerrainEditMode::None
                : EditorContext::TerrainEditMode::Splat;
            if (ctx.terrainEditMode != EditorContext::TerrainEditMode::None) {
                ctx.gizmoMode = EditorContext::GizmoMode::None;
            }
        }
        ImGui::SameLine();

        if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Sculpt) {
            if (terrainToolButton("Raise",
                                  ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Raise)) {
                ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Raise;
            }
            ImGui::SameLine();
            if (terrainToolButton("Lower",
                                  ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Lower)) {
                ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Lower;
            }
            ImGui::SameLine();
            if (terrainToolButton("Smooth",
                                  ctx.terrainBrushMode == EditorContext::TerrainBrushMode::Smooth)) {
                ctx.terrainBrushMode = EditorContext::TerrainBrushMode::Smooth;
            }
            ImGui::SameLine();
        } else if (ctx.terrainEditMode == EditorContext::TerrainEditMode::Splat) {
            const char* layerNames[] = {"Grass", "Rock", "Sand", "Dirt"};
            for (u32 layer = 0; layer < ECS::kTerrainSplatLayerCount; ++layer) {
                if (layer > 0) ImGui::SameLine();
                if (terrainToolButton(layerNames[layer], ctx.terrainSplatLayer == layer)) {
                    ctx.terrainSplatLayer = layer;
                }
            }
            ImGui::SameLine();
        }

        if (ctx.terrainEditMode != EditorContext::TerrainEditMode::None) {
            ImGui::SetNextItemWidth(72.0f);
            ImGui::SliderFloat("##brush_radius", &ctx.terrainBrushRadius, 0.5f, 64.0f, "R %.0f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(72.0f);
            ImGui::SliderFloat("##brush_strength", &ctx.terrainBrushStrength, 0.01f, 1.0f, "S %.2f");
            ImGui::SameLine();
        }

        if (ImGui::Button("Chunks")) {
            ctx.terrainShowChunkDebug = !ctx.terrainShowChunkDebug;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle chunk bounds debug overlay");

        ImGui::SameLine();
        if (ImGui::Button("Flatten")) {
            if (auto* heightmap = Terrain::TerrainCache::instance().heightmapFor(ctx.selectedEntity)) {
                if (auto* terrain = world.get<ECS::TerrainComponent>(ctx.selectedEntity)) {
                    ctx.beginUndo(EditorCommand::SetField, ctx.selectedEntity.id(), world);
                    heightmap->fill(0.0f);
                    terrain->dataRevision++;
                    ctx.isDirty = true;
                    ctx.endUndo(world);
                }
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset terrain height to flat");

        ImGui::PopStyleVar();
    }

    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        f32 btnW   = 32.0f;
        f32 margin = 8.0f;
        ImVec2 btnPos(origin.x + viewportSize.x - margin - btnW * 3.0f - 4.0f, origin.y + 8.0f);

        auto viewBtn = [&](const char* label, EditorContext::ViewMode mode) {
            bool active = (ctx.viewMode == mode);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.9f, 0.9f));
            else        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.75f));
            ImGui::SetCursorScreenPos(btnPos);
            if (ImGui::Button(label, ImVec2(btnW, 22.0f))) ctx.viewMode = mode;
            ImGui::PopStyleColor();
            btnPos.x += btnW + 2.0f;
        };

        viewBtn("2D",  EditorContext::ViewMode::Mode2D);
        viewBtn("3D",  EditorContext::ViewMode::Mode3D);
        viewBtn("Iso", EditorContext::ViewMode::Isometric);
        ImGui::PopStyleVar();
    }

    if (ctx.viewMode != EditorContext::ViewMode::Mode3D) {
        drawGrid(drawList, origin, viewportSize, ctx);
    }
    drawSprites(world, ctx, origin, viewportSize);
    drawEmptyEntities(world, ctx, origin, viewportSize);
    drawPhysicsDebug(world, ctx, origin, viewportSize);
    drawCameraFrustums(world, ctx, origin, viewportSize);
    drawLightGizmos(world, ctx, origin, viewportSize);

    if (ctx.selectedEntity.isValid() && !terrainEditActive && !ctx.isPlayMode) {
        drawGizmo(world, ctx, origin, viewportSize);
    }

    if (terrainEditActive && m_terrainBrushHitValid) {
        const ImVec2 center = projectToScreen(m_terrainBrushHitWorld, origin, viewportSize, ctx);
        const Mat4 worldMatrix = entityMatrix(world, ctx.selectedEntity);
        const Vec3 axisX = matrixAxis(worldMatrix, 0, Vec3::right());
        const ImVec2 edge = projectToScreen(
            m_terrainBrushHitWorld + axisX.normalized() * ctx.terrainBrushRadius,
            origin, viewportSize, ctx);
        const f32 screenRadius = std::sqrt(
            (edge.x - center.x) * (edge.x - center.x) +
            (edge.y - center.y) * (edge.y - center.y));
        const ImU32 brushColor = IM_COL32(120, 220, 120, 200);
        drawList->AddCircle(center, screenRadius, brushColor, 48, 2.0f);
        drawList->AddCircleFilled(center, 3.0f, brushColor, 12);
    }

    drawNavigationWidget(world, ctx, origin, viewportSize);

    if (!ctx.isPlayMode && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        const bool is3DIso = (ctx.viewMode == EditorContext::ViewMode::Mode3D ||
                              ctx.viewMode == EditorContext::ViewMode::Isometric);
        if (is3DIso) {
            const f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
            const f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
            const Vec3 forward(-sinY * cosP, sinP, cosY * cosP);
            const Vec3 worldUp(0.0f, 1.0f, 0.0f);
            Vec3 right = forward.cross(worldUp);
            if (right.lengthSquared() < 1e-6f) right = Vec3(1.0f, 0.0f, 0.0f);
            else right = right.normalized();
            const Vec3 up = right.cross(forward).normalized();
            const f32 panSpeed = ctx.camDistance * 0.002f;
            ctx.camFocus += right * (delta.x * panSpeed);
            ctx.camFocus -= up * (delta.y * panSpeed);
        } else {
            ctx.viewportPanX += delta.x;
            ctx.viewportPanY += delta.y;
        }
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }

     // 2D View: Left mouse button drag to pan (disabled during play — camera drives the view)
     if (!ctx.isPlayMode && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
         if (ctx.viewMode == EditorContext::ViewMode::Mode2D) {
             ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
             f32 s = ctx.viewportZoom * 50.0f;
             ctx.viewportPanX -= delta.x / s;
             ctx.viewportPanY -= delta.y / s;
             ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
         }
     }

      if (hovered && !ImGui::GetIO().WantTextInput && !ctx.isPlayMode) {
          bool is3DIso = (ctx.viewMode == EditorContext::ViewMode::Mode3D ||
                          ctx.viewMode == EditorContext::ViewMode::Isometric);
          if (is3DIso) {
             const f32 moveBoost = (ImGui::IsKeyDown(ImGuiKey_LeftShift) ||
                                    ImGui::IsKeyDown(ImGuiKey_RightShift)) ? 4.0f : 1.0f;
             const f32 dt = std::max(ImGui::GetIO().DeltaTime, 0.0001f);
             const f32 speed = std::max(18.0f, ctx.camDistance * 1.35f) * dt * moveBoost;
             const f32 sinY  = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
             const f32 sinP  = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
             const Vec3 lookDir(-sinY * cosP, sinP, cosY * cosP);
             const Vec3 worldUp(0.0f, 1.0f, 0.0f);
             Vec3 right = lookDir.cross(worldUp);
             if (right.lengthSquared() < 1e-8f) right = Vec3(1.0f, 0.0f, 0.0f);
             else right = right.normalized();

             Vec3 camPos = ctx.camFocus - lookDir * ctx.camDistance;
             if (ImGui::IsKeyDown(ImGuiKey_W)) camPos += lookDir * speed;
             if (ImGui::IsKeyDown(ImGuiKey_S)) camPos -= lookDir * speed;
             if (ImGui::IsKeyDown(ImGuiKey_A)) camPos -= right * speed;
             if (ImGui::IsKeyDown(ImGuiKey_D)) camPos += right * speed;
             if (ImGui::IsKeyDown(ImGuiKey_Q)) camPos -= worldUp * speed;
             if (ImGui::IsKeyDown(ImGuiKey_E)) camPos += worldUp * speed;
             ctx.camFocus = camPos + lookDir * ctx.camDistance;
         }
     }

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
        if (ctx.viewMode == EditorContext::ViewMode::Mode3D) {
            const f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
            const f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
            const Vec3 lookDir(-sinY * cosP, sinP, cosY * cosP);
            const Vec3 camPos = ctx.camFocus - lookDir * ctx.camDistance;
            ctx.camYaw   += delta.x * 0.005f;
            ctx.camPitch += delta.y * 0.005f;
            ctx.camPitch = std::clamp(ctx.camPitch, EditorContext::kCamPitchMin,
                                      EditorContext::kCamPitchMax);
            const f32 nSinY = std::sin(ctx.camYaw), nCosY = std::cos(ctx.camYaw);
            const f32 nSinP = std::sin(ctx.camPitch), nCosP = std::cos(ctx.camPitch);
            const Vec3 newLook(-nSinY * nCosP, nSinP, nCosY * nCosP);
            ctx.camFocus = camPos + newLook * ctx.camDistance;
        } else if (ctx.viewMode == EditorContext::ViewMode::Isometric) {
            ctx.camYaw += delta.x * 0.005f;
        }
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
    }

     if (!ctx.isPlayMode && hovered && !ImGui::GetIO().WantCaptureMouse) {
         f32 scroll = ImGui::GetIO().MouseWheel;
         if (scroll != 0) {
             if (ctx.viewMode == EditorContext::ViewMode::Mode3D || 
                 ctx.viewMode == EditorContext::ViewMode::Isometric) {
                 const f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
                 const f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
                 const Vec3 lookDir(-sinY * cosP, sinP, cosY * cosP);
                 if (ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt)) {
                     const f32 zoomFactor = (scroll > 0) ? 0.85f : 1.18f;
                     ctx.camDistance *= zoomFactor;
                     ctx.camDistance = std::clamp(ctx.camDistance, EditorContext::kCamDistanceMin,
                                                  EditorContext::kCamDistanceMax);
                 } else {
                     const f32 boost = (ImGui::IsKeyDown(ImGuiKey_LeftShift) ||
                                        ImGui::IsKeyDown(ImGuiKey_RightShift)) ? 4.0f : 1.0f;
                     const f32 step = std::max(2.5f, ctx.camDistance * 0.35f) * boost;
                     ctx.camFocus += lookDir * (scroll * step);
                 }
             } else {
                 ctx.viewportZoom *= (scroll > 0) ? 1.1f : 0.9f;
                 ctx.viewportZoom = std::max(0.1f, std::min(10.0f, ctx.viewportZoom));
             }
         }
     }

    if (ctx.isPlayMode) {
        UI::drawWidgets(world, drawList, origin, viewportSize);
    }

    ImGui::End();
}

ImVec2 SceneViewport::projectToScreen(Vec3 p, ImVec2 origin, ImVec2 viewportSize,
                                       const EditorContext& ctx) {
    if (ctx.viewMode == EditorContext::ViewMode::Mode3D) {
        Mat4 vp = computeVP3D(viewportSize, ctx);
        return projectToScreenVP(p, origin, viewportSize, vp);
    }
    f32 cx = origin.x + viewportSize.x * 0.5f;
    f32 cy = origin.y + viewportSize.y * 0.5f;

    switch (ctx.viewMode) {
        case EditorContext::ViewMode::Mode2D: {
            f32 s = ctx.viewportZoom * 50.0f;
            return ImVec2(cx + (p.x + ctx.viewportPanX / s) * s,
                          cy + (-p.y + ctx.viewportPanY / s) * s);
        }
        case EditorContext::ViewMode::Isometric: {
            f32 s = ctx.viewportZoom * 50.0f;
            f32 cosA = std::cos(ctx.camYaw + 0.5236f);
            f32 sinA = std::sin(ctx.camYaw + 0.5236f);
            f32 iso_x = (p.x - p.y) * cosA * s;
            f32 iso_y = (p.x + p.y) * sinA * s * 0.5f - p.z * s * 0.866f;
            return ImVec2(cx + iso_x + ctx.viewportPanX,
                          cy - iso_y + ctx.viewportPanY);
        }
        default: break;
    }
    return ImVec2(cx, cy);
}

Mat4 SceneViewport::computeVP3D(ImVec2 viewportSize, const EditorContext& ctx) {
    f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
    f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
    Vec3 camPos;
    camPos.x = ctx.camFocus.x + sinY * cosP * ctx.camDistance;
    camPos.y = ctx.camFocus.y - sinP * ctx.camDistance;
    camPos.z = ctx.camFocus.z - cosY * cosP * ctx.camDistance;
    Mat4 view = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
    f32 aspect = viewportSize.x / std::max(viewportSize.y, 1.0f);
    Mat4 proj = Mat4::perspective(1.0472f, aspect, 0.1f, ctx.cameraFarPlane());
    return proj * view;
}

ImVec2 SceneViewport::projectToScreenVP(Vec3 p, ImVec2 origin, ImVec2 viewportSize,
                                         const Mat4& vp) {
    Vec4 clip = vp.transformVec4(Vec4(p.x, p.y, p.z, 1.0f));
    if (clip.w <= 0.1f) return ImVec2(-10000.0f, -10000.0f);
    f32 ndcX = clip.x / clip.w;
    f32 ndcY = clip.y / clip.w;
    // Off-screen NDC coords are allowed — ImGui clips line segments automatically.
    return ImVec2(
        origin.x + (ndcX + 1.0f) * 0.5f * viewportSize.x,
        origin.y + (1.0f - ndcY) * 0.5f * viewportSize.y
    );
}

// ── Gizmo drawing ─────────────────────────────────────────────────

void SceneViewport::drawSprites(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ECS::ComponentQuery query;
    query.with<ECS::Transform>();
    query.with<ECS::Sprite>();

    const f32 worldToScreen = ctx.viewportZoom * 50.0f;
    const f32 minHalfSize = 8.0f;

    world.forEach<ECS::Transform, ECS::Sprite>(query, [&](ECS::Entity entity, ECS::Transform& pos, ECS::Sprite& sprite) {
        if (Scene::isEffectivelyDisabled(world, entity)) return;

        Vec3 worldPosition = pos.position;
        f32 scaleX = std::max(0.1f, pos.scale.x);
        f32 scaleY = std::max(0.1f, pos.scale.y);
        f32 angle  = pos.rotation.z;

        if (auto* wt = world.get<Scene::WorldTransform>(entity)) {
            worldPosition = Vec3(wt->matrix(0,3), wt->matrix(1,3), wt->matrix(2,3));
            // scaleX/Y = length of matrix column 0/1 (upper 3x3)
            scaleX = std::max(0.1f, sqrtf(wt->matrix(0,0)*wt->matrix(0,0) + wt->matrix(1,0)*wt->matrix(1,0)));
            scaleY = std::max(0.1f, sqrtf(wt->matrix(0,1)*wt->matrix(0,1) + wt->matrix(1,1)*wt->matrix(1,1)));
            angle  = atan2f(wt->matrix(1,0), wt->matrix(0,0));
        }

        ImVec2 screenPos = projectToScreen(worldPosition, origin, viewportSize, ctx);

        f32 halfW = std::max(minHalfSize, 0.5f * worldToScreen * scaleX);
        f32 halfH = std::max(minHalfSize, 0.5f * worldToScreen * scaleY);

        ImTextureRef texRef;
        bool hasTexture = false;
        u32 texWidth = 0;
        u32 texHeight = 0;
        if (!sprite.name.empty()) {
            const std::string spritePath = resolveSpritePath(sprite.name, ctx);
            if (!spritePath.empty()) {
                auto it = m_spriteTextureCache.find(spritePath);
                if (it == m_spriteTextureCache.end()) {
                    // Try to load the texture
                    int width = 0, height = 0, channels = 0;
                    unsigned char* pixels = stbi_load(spritePath.c_str(), &width, &height, &channels, 4);
                    
                    SpriteTextureCacheEntry entry;
                    if (pixels && width > 0 && height > 0) {
                        entry.width = width;
                        entry.height = height;
                        entry.texture = std::make_unique<ImTextureData>();
                        entry.texture->Create(ImTextureFormat_RGBA32, width, height);
                        std::memcpy(entry.texture->GetPixels(), pixels, static_cast<size_t>(width * height * 4));
                        entry.texture->SetStatus(ImTextureStatus_WantCreate);
                        ImGui_ImplSDLGPU3_UpdateTexture(entry.texture.get());
                        entry.loadFailed = false;
                    } else {
                        entry.loadFailed = true;
                    }
                    
                    if (pixels) {
                        stbi_image_free(pixels);
                    }
                    
                    auto [newIt, inserted] = m_spriteTextureCache.emplace(spritePath, std::move(entry));
                    it = newIt;
                }

                if (!it->second.loadFailed && it->second.texture) {
                    // Ensure texture is properly initialized
                    if (it->second.texture->Status == ImTextureStatus_WantCreate) {
                        ImGui_ImplSDLGPU3_UpdateTexture(it->second.texture.get());
                    }
                    
                    ImTextureID texID = it->second.texture->GetTexID();
                    hasTexture = (texID != ImTextureID_Invalid);
                    
                    if (hasTexture) {
                        texRef = it->second.texture->GetTexRef();
                        texWidth = it->second.width;
                        texHeight = it->second.height;
                        if (it->second.width > 0 && it->second.height > 0) {
                            const f32 aspect = static_cast<f32>(it->second.width) / static_cast<f32>(it->second.height);
                            if (aspect > 1.0f) {
                                halfH = std::max(minHalfSize, halfW / aspect);
                            } else {
                                halfW = std::max(minHalfSize, halfH * aspect);
                            }
                        }
                    }
                }
            }
        }

        const f32 c = std::cos(angle);
        const f32 s = std::sin(angle);
        auto rotatePoint = [&](f32 x, f32 y) -> ImVec2 {
            return ImVec2(screenPos.x + x * c - y * s, screenPos.y + x * s + y * c);
        };

        const ImVec2 p1 = rotatePoint(-halfW, -halfH);
        const ImVec2 p2 = rotatePoint(halfW, -halfH);
        const ImVec2 p3 = rotatePoint(halfW, halfH);
        const ImVec2 p4 = rotatePoint(-halfW, halfH);

        f32 uv0x = 0.0f;
        f32 uv0y = 0.0f;
        f32 uv1x = 1.0f;
        f32 uv1y = 1.0f;
        if (hasTexture && texWidth > 0 && texHeight > 0) {
            const f32 invW = 1.0f / static_cast<f32>(texWidth);
            const f32 invH = 1.0f / static_cast<f32>(texHeight);
            bool resolved = false;
            if (auto* animator = world.get<Animation::Animator>(entity)) {
                const Animation::AnimationState* state =
                    animator->states.get(animator->currentState);
                if (state && state->clip && !state->clip->frames.empty()) {
                    const u32 frameIdx =
                        std::min(sprite.frameIndex, static_cast<u32>(state->clip->frames.size() - 1));
                    const Animation::FrameRect& frame = state->clip->frames[frameIdx];
                    if (frame.w > 0.0f && frame.h > 0.0f) {
                        uv0x = frame.x * invW;
                        uv0y = frame.y * invH;
                        uv1x = (frame.x + frame.w) * invW;
                        uv1y = (frame.y + frame.h) * invH;
                        resolved = true;
                    }
                }
            }
            if (!resolved && sprite.frameIndex > 0) {
                const u32 frameSize = std::max(1u, texHeight);
                const u32 frameCount = std::max(1u, texWidth / frameSize);
                const u32 frameIdx = sprite.frameIndex % frameCount;
                uv0x = static_cast<f32>(frameIdx * frameSize) * invW;
                uv0y = 0.0f;
                uv1x = static_cast<f32>((frameIdx + 1) * frameSize) * invW;
                uv1y = static_cast<f32>(frameSize) * invH;
            }
        }

        const bool selected = (ctx.selectedEntity == entity);
        const ImU32 fill = selected ? IM_COL32(100, 170, 255, 80) : IM_COL32(180, 180, 200, 45);
        const ImU32 border = selected ? IM_COL32(110, 210, 255, 255) : IM_COL32(190, 190, 220, 200);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hasTexture) {
            dl->AddImageQuad(texRef, p1, p2, p3, p4, ImVec2(uv0x, uv0y), ImVec2(uv1x, uv0y),
                             ImVec2(uv1x, uv1y), ImVec2(uv0x, uv1y));
        } else {
            // Draw checkerboard pattern for missing texture
            dl->AddQuadFilled(p1, p2, p3, p4, IM_COL32(64, 64, 64, 200));
            
            // Draw checkerboard pattern
            ImVec2 checkSize = ImVec2((p2.x - p1.x) / 4.0f, (p4.y - p1.y) / 4.0f);
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    if ((x + y) % 2 == 0) {
                        ImVec2 checkMin = ImVec2(p1.x + x * checkSize.x, p1.y + y * checkSize.y);
                        ImVec2 checkMax = ImVec2(checkMin.x + checkSize.x, checkMin.y + checkSize.y);
                        dl->AddRectFilled(checkMin, checkMax, IM_COL32(96, 96, 96, 200));
                    }
                }
            }
        }
        dl->AddQuad(p1, p2, p3, p4, border, selected ? 2.0f : 1.0f);

        if (!sprite.name.empty()) {
            std::filesystem::path labelPath(sprite.name);
            const std::string label = labelPath.filename().string();
            dl->AddText(ImVec2(screenPos.x - halfW, screenPos.y - halfH - 14.0f), IM_COL32(220, 220, 230, 230), label.c_str());
        }
    });
}

void SceneViewport::drawEmptyEntities(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = 7.0f;

    std::string projectRoot;
    if (!ctx.currentScenePath.empty()) {
        projectRoot = resolveProjectRootFromScenePath(ctx.currentScenePath);
    }
    Scene::SceneLighting sceneLighting;
    Scene::gatherSceneLighting(world, ctx.camFocus, projectRoot, sceneLighting);

    auto lightColorAt = [&](const Vec3& p, const Vec3& n, bool receiveShadows) -> Vec3 {
        return Scene::evaluateDiffuseLighting(sceneLighting.lights, sceneLighting.shadows, p, n,
                                              receiveShadows);
    };

    auto toColor = [](const Vec3& v, f32 a = 0.94f) -> ImU32 {
        const f32 r = std::clamp(v.x, 0.0f, 1.0f);
        const f32 g = std::clamp(v.y, 0.0f, 1.0f);
        const f32 b = std::clamp(v.z, 0.0f, 1.0f);
        const ImU8 cr = static_cast<ImU8>(r * 255.0f);
        const ImU8 cg = static_cast<ImU8>(g * 255.0f);
        const ImU8 cb = static_cast<ImU8>(b * 255.0f);
        const ImU8 alpha = static_cast<ImU8>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        return IM_COL32(cr, cg, cb, alpha);
    };

    const Mat4 vpCache3D = (ctx.viewMode == EditorContext::ViewMode::Mode3D)
        ? computeVP3D(viewportSize, ctx) : Mat4::identity();
    auto projectCached = [&](const Vec3& p) -> ImVec2 {
        return (ctx.viewMode == EditorContext::ViewMode::Mode3D)
            ? projectToScreenVP(p, origin, viewportSize, vpCache3D)
            : projectToScreen(p, origin, viewportSize, ctx);
    };

    auto drawMarker = [&](ECS::Entity entity, const Vec3& worldPosition) {
        ImVec2 sp = projectCached(worldPosition);

        const bool selected = (ctx.selectedEntity == entity);
        const ImU32 col     = selected ? IM_COL32(110, 210, 255, 255) : IM_COL32(180, 180, 200, 200);

        dl->AddQuadFilled(
            ImVec2(sp.x,     sp.y - r),
            ImVec2(sp.x + r, sp.y    ),
            ImVec2(sp.x,     sp.y + r),
            ImVec2(sp.x - r, sp.y    ),
            selected ? IM_COL32(110, 210, 255, 40) : IM_COL32(180, 180, 200, 30));
        dl->AddQuad(
            ImVec2(sp.x,     sp.y - r),
            ImVec2(sp.x + r, sp.y    ),
            ImVec2(sp.x,     sp.y + r),
            ImVec2(sp.x - r, sp.y    ),
            col, selected ? 2.0f : 1.0f);
        dl->AddLine(ImVec2(sp.x - r * 0.5f, sp.y), ImVec2(sp.x + r * 0.5f, sp.y), col, 1.5f);
        dl->AddLine(ImVec2(sp.x, sp.y - r * 0.5f), ImVec2(sp.x, sp.y + r * 0.5f), col, 1.5f);
    };

    auto drawSegment = [&](const Vec3& a, const Vec3& b, ImU32 col, float thickness) {
        dl->AddLine(projectCached(a), projectCached(b), col, thickness);
    };

    auto drawRing = [&](const Mat4& worldMatrix, const Vec3& axisA, const Vec3& axisB,
                        f32 radius, int segments, ImU32 col, float thickness) {
        Vec3 prev = worldMatrix.transformPoint(axisA * radius);
        for (int i = 1; i <= segments; ++i) {
            const f32 a = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(segments);
            const Vec3 local = axisA * (std::cos(a) * radius) + axisB * (std::sin(a) * radius);
            const Vec3 curr  = worldMatrix.transformPoint(local);
            drawSegment(prev, curr, col, thickness);
            prev = curr;
        }
    };

    const bool gpuTextured3D = (ctx.viewMode == EditorContext::ViewMode::Mode3D &&
                                m_meshPreviewMode == MeshPreviewMode::Textured &&
                                m_gpuSceneReady && m_useGpuScene);
    const bool useMeshRaster = (ctx.viewMode == EditorContext::ViewMode::Mode3D &&
                                m_meshPreviewMode == MeshPreviewMode::Textured &&
                                !gpuTextured3D);
    MeshCpuRasterizer meshRaster;
    if (useMeshRaster) {
        meshRaster.begin(origin, viewportSize);
        meshRaster.clear(0, 0, 0, 0);
    }

    auto drawMeshPreview = [&](ECS::Entity entity) {
        auto* mesh = world.get<ECS::MeshFilterComponent>(entity);
        if (!mesh) return false;

        const Mat4 worldMatrix = entityMatrix(world, entity);
        const bool selected = (ctx.selectedEntity == entity);
        const bool wireMode = (m_meshPreviewMode == MeshPreviewMode::Wireframe);
        const ImU32 wireCol = selected ? IM_COL32(110, 210, 255, 255) : IM_COL32(170, 190, 230, 210);
        const ImU32 polyCol = selected ? IM_COL32(95, 170, 255, 210) : IM_COL32(130, 150, 190, 170);
        const float thickness = selected ? 2.0f : 1.25f;
        const float polyThickness = selected ? 1.5f : 1.0f;

        const bool drawContour = wireMode || selected;
        const bool drawPolygons = wireMode;
        const int densityLevel = static_cast<int>(m_wireframeDensity);
        const int spherePolySegments = (densityLevel == 0) ? 8 : (densityLevel == 1 ? 12 : 18);
        const int sidePolySlices = (densityLevel == 0) ? 4 : (densityLevel == 1 ? 8 : 14);

        auto cameraDepth = [&](const Vec3& wp) -> f32 {
            if (ctx.viewMode == EditorContext::ViewMode::Mode3D) {
                f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
                f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
                f32 rx = wp.x - ctx.camFocus.x;
                f32 ry = wp.y - ctx.camFocus.y;
                f32 rz = wp.z - ctx.camFocus.z;
                f32 vz = -sinY * rx + cosY * rz;
                f32 vz2 = -sinP * ry + cosP * vz;
                return vz2;
            }
            if (ctx.viewMode == EditorContext::ViewMode::Isometric) {
                return wp.x + wp.y + wp.z;
            }
            return wp.z;
        };

        auto drawEdge = [&](const Vec3& a, const Vec3& b) {
            if (!drawContour) return;
            drawSegment(a, b, wireCol, thickness);
        };
        auto drawPoly = [&](const Vec3& a, const Vec3& b) {
            if (!drawPolygons) return;
            drawSegment(a, b, polyCol, polyThickness);
        };

        switch (mesh->primitive) {
            case ECS::MeshPrimitive::Cube: {
                const std::array<Vec3, 8> corners = {{
                    {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
                    { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
                    {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
                    { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
                }};
                const int edges[][2] = {
                    {0,1}, {1,2}, {2,3}, {3,0},
                    {4,5}, {5,6}, {6,7}, {7,4},
                    {0,4}, {1,5}, {2,6}, {3,7}
                };

                const int faces[][4] = {
                    {0,1,2,3}, // back
                    {4,5,6,7}, // front
                    {0,3,7,4}, // left
                    {1,2,6,5}, // right
                    {3,2,6,7}, // top
                    {0,1,5,4}, // bottom
                };

                const Vec3 faceNormalsLocal[] = {
                    {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}, {0,1,0}, {0,-1,0}
                };

                std::array<Vec3, 8> wp;
                for (usize i = 0; i < corners.size(); ++i) {
                    wp[i] = worldMatrix.transformPoint(corners[i]);
                }

                if (m_meshPreviewMode == MeshPreviewMode::Textured) {
                    struct FaceDraw {
                        int faceIndex;
                        f32 depth;
                    };
                    std::array<FaceDraw, 6> faceOrder{};
                    for (int fi = 0; fi < 6; ++fi) {
                        Vec3 center = (wp[faces[fi][0]] + wp[faces[fi][1]] + wp[faces[fi][2]] + wp[faces[fi][3]]) * 0.25f;
                        faceOrder[fi] = {fi, cameraDepth(center)};
                    }
                    std::sort(faceOrder.begin(), faceOrder.end(), [](const FaceDraw& a, const FaceDraw& b) {
                        return a.depth > b.depth;
                    });

                    for (const auto& fd : faceOrder) {
                        const int fi = fd.faceIndex;
                        Vec3 nWorld = matrixAxis(worldMatrix, (fi == 0 || fi == 1) ? 2 : (fi <= 3 ? 0 : 1), faceNormalsLocal[fi]);
                        if (fi == 0 || fi == 2 || fi == 5) nWorld = -1.0f * nWorld;

                        Vec3 center = (wp[faces[fi][0]] + wp[faces[fi][1]] + wp[faces[fi][2]] + wp[faces[fi][3]]) * 0.25f;
                        const Vec3 lit =
                            lightColorAt(center, nWorld, Scene::meshReceivesShadows(world, entity));
                        const f32 checker = (fi % 2 == 0) ? 0.86f : 0.74f;
                        const ImU32 fill = toColor(Vec3(lit.x * checker, lit.y * checker, lit.z * checker), 0.92f);

                        ImVec2 p0 = projectCached(wp[faces[fi][0]]);
                        ImVec2 p1 = projectCached(wp[faces[fi][1]]);
                        ImVec2 p2 = projectCached(wp[faces[fi][2]]);
                        ImVec2 p3 = projectCached(wp[faces[fi][3]]);
                        dl->AddQuadFilled(p0, p1, p2, p3, fill);
                    }
                }

                for (const auto& edge : edges) {
                    drawEdge(wp[edge[0]], wp[edge[1]]);
                }
                if (drawPolygons) {
                    for (int fi = 0; fi < 6; ++fi) {
                        const auto& face = faces[fi];
                        if (densityLevel == 0) {
                            if ((fi % 2) == 0) drawPoly(wp[face[0]], wp[face[2]]);
                        } else if (densityLevel == 1) {
                            drawPoly(wp[face[0]], wp[face[2]]);
                        } else {
                            drawPoly(wp[face[0]], wp[face[2]]);
                            drawPoly(wp[face[1]], wp[face[3]]);
                        }
                    }
                }
                break;
            }
            case ECS::MeshPrimitive::Custom: {
                if (!mesh->customMeshPath.empty() || world.has<ECS::TerrainComponent>(entity)) {
                    std::string projectRoot;
                    if (!ctx.currentScenePath.empty()) {
                        projectRoot = resolveProjectRootFromScenePath(ctx.currentScenePath);
                    }

                    auto& meshCache = Assets::MeshCache::getInstance();
                    Assets::Mesh3D* loadedMesh = Terrain::TerrainCache::instance().meshFor(entity);
                    if (!loadedMesh) {
                        loadedMesh = meshCache.getMesh(mesh->customMeshPath, projectRoot);
                    }
                    const std::string& loadError = meshCache.getLastError();

                    if (!loadedMesh || loadedMesh->vertices.empty() || loadedMesh->indices.empty()) {
                        const std::array<Vec3, 8> corners = {{
                            {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
                            { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
                            {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
                            { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
                        }};
                        const int edges[][2] = {
                            {0,1}, {1,2}, {2,3}, {3,0},
                            {4,5}, {5,6}, {6,7}, {7,4},
                            {0,4}, {1,5}, {2,6}, {3,7}
                        };
                        const ImU32 errCol = IM_COL32(255, 90, 90, 220);
                        for (const auto& edge : edges) {
                            drawEdge(worldMatrix.transformPoint(corners[edge[0]]),
                                     worldMatrix.transformPoint(corners[edge[1]]));
                        }
                        Vec3 labelPos = worldMatrix.transformPoint(Vec3(0.0f, 0.7f, 0.0f));
                        ImVec2 sp = projectCached(labelPos);
                        if (sp.x > -5000.0f) {
                            const char* msg = loadError.empty() ? "Mesh nao carregado" : loadError.c_str();
                            dl->AddText(sp, errCol, msg);
                        }
                        break;
                    }

                    f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
                    f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
                    Vec3 camPos;
                    camPos.x = ctx.camFocus.x + sinY * cosP * ctx.camDistance;
                    camPos.y = ctx.camFocus.y - sinP * ctx.camDistance;
                    camPos.z = ctx.camFocus.z - cosY * cosP * ctx.camDistance;
                    Mat4 viewMat = Mat4::lookAt(camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f));
                    f32 aspectR = viewportSize.x / std::max(viewportSize.y, 1.0f);
                    Mat4 projMat = Mat4::perspective(1.0472f, aspectR, 0.1f, ctx.cameraFarPlane());
                    Mat4 vpMat = projMat * viewMat;

                    const bool drawTextured = (m_meshPreviewMode == MeshPreviewMode::Textured);
                    const bool isTerrain = world.has<ECS::TerrainComponent>(entity);
                    // Textured terrain is rendered on the GPU path (splat blending in shader).
                    // CPU per-pixel splat was extremely slow when looking down at the surface
                    // (millions of pixels × 4 texture samples) while backface culling made
                    // looking up from below appear fast because almost no pixels were shaded.
                    const bool terrainGpuTextured = isTerrain && drawTextured &&
                                                    m_gpuSceneReady && m_useGpuScene &&
                                                    m_lastGpuSceneActive &&
                                                    Terrain::TerrainGpuTextureCache::instance()
                                                        .hasRenderableTextures(entity);
                    if (terrainGpuTextured) {
                        if (ctx.terrainShowChunkDebug) {
                            auto* terrain = world.get<ECS::TerrainComponent>(entity);
                            if (terrain) {
                                const f32 aspectFrustum = viewportSize.x / std::max(viewportSize.y, 1.0f);
                                const Spatial::Frustum frustum = Spatial::Frustum::fromCamera(
                                    camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f), 1.0472f, aspectFrustum,
                                    0.1f, ctx.cameraFarPlane());
                                m_terrainCullStats = {};
                                m_terrainDrawChunks.clear();
                                Terrain::TerrainCache::instance().gatherDrawMeshes(
                                    entity, *terrain, worldMatrix, camPos, frustum, m_terrainDrawChunks,
                                    &m_terrainCullStats);
                                drawTerrainChunkDebug(dl, worldMatrix, m_terrainDrawChunks,
                                                      origin, viewportSize, ctx);
                            }
                        }
                    } else if (isTerrain) {
                        auto* terrain = world.get<ECS::TerrainComponent>(entity);
                        const f32 aspectFrustum = viewportSize.x / std::max(viewportSize.y, 1.0f);
                        const Spatial::Frustum frustum = Spatial::Frustum::fromCamera(
                            camPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f), 1.0472f, aspectFrustum,
                            0.1f, ctx.cameraFarPlane());
                        m_terrainCullStats = {};
                        m_terrainDrawChunks.clear();
                        Terrain::TerrainCache::instance().gatherDrawMeshes(
                            entity, *terrain, worldMatrix, camPos, frustum, m_terrainDrawChunks,
                            &m_terrainCullStats);
                        // CPU fallback: single texture only (no per-pixel splat blending).
                        MeshCpuRasterizer* terrainRaster =
                            (drawTextured && useMeshRaster) ? &meshRaster : nullptr;
                        for (const Terrain::TerrainDrawChunk& drawChunk : m_terrainDrawChunks) {
                            drawCustomMeshGeometry(world, ctx, entity, mesh, worldMatrix, dl, vpMat,
                                                   camPos, origin, viewportSize, lightColorAt,
                                                   drawTextured, wireMode,
                                                   Scene::meshReceivesShadows(world, entity),
                                                   terrainRaster, drawChunk.mesh, nullptr);
                        }
                        if (ctx.terrainShowChunkDebug) {
                            drawTerrainChunkDebug(dl, worldMatrix, m_terrainDrawChunks,
                                                  origin, viewportSize, ctx);
                        }
                    } else if (!(gpuTextured3D && !wireMode)) {
                        drawCustomMeshGeometry(world, ctx, entity, mesh, worldMatrix, dl, vpMat, camPos,
                                               origin, viewportSize, lightColorAt, drawTextured, wireMode,
                                               Scene::meshReceivesShadows(world, entity),
                                               useMeshRaster ? &meshRaster : nullptr);
                    }

                    if (selected && drawTextured) {
                        Vec3 bMin = loadedMesh->bounds.min;
                        Vec3 bMax = loadedMesh->bounds.max;
                        const Vec3 bbCorners[8] = {
                            {bMin.x, bMin.y, bMin.z}, {bMax.x, bMin.y, bMin.z},
                            {bMin.x, bMax.y, bMin.z}, {bMax.x, bMax.y, bMin.z},
                            {bMin.x, bMin.y, bMax.z}, {bMax.x, bMin.y, bMax.z},
                            {bMin.x, bMax.y, bMax.z}, {bMax.x, bMax.y, bMax.z}
                        };
                        const int bbEdges[][2] = {
                            {0,1}, {1,3}, {3,2}, {2,0},
                            {4,5}, {5,7}, {7,6}, {6,4},
                            {0,4}, {1,5}, {3,7}, {2,6}
                        };
                        for (const auto& edge : bbEdges) {
                            drawEdge(worldMatrix.transformPoint(bbCorners[edge[0]]),
                                     worldMatrix.transformPoint(bbCorners[edge[1]]));
                        }
                    }
                }
                break;
            }
            case ECS::MeshPrimitive::Plane: {
                const Vec3 corners[] = {
                    {-0.5f, 0.0f, -0.5f}, { 0.5f, 0.0f, -0.5f},
                    { 0.5f, 0.0f,  0.5f}, {-0.5f, 0.0f,  0.5f}
                };
                Vec3 wp[4] = {
                    worldMatrix.transformPoint(corners[0]),
                    worldMatrix.transformPoint(corners[1]),
                    worldMatrix.transformPoint(corners[2]),
                    worldMatrix.transformPoint(corners[3])
                };

                if (m_meshPreviewMode == MeshPreviewMode::Textured) {
                    Vec3 nWorld = matrixAxis(worldMatrix, 1, Vec3::up());
                    Vec3 center = (wp[0] + wp[1] + wp[2] + wp[3]) * 0.25f;
                    const Vec3 lit =
                        lightColorAt(center, nWorld, Scene::meshReceivesShadows(world, entity));
                    const ImU32 fill = toColor(Vec3(lit.x * 0.80f, lit.y * 0.80f, lit.z * 0.80f), 0.88f);
                    dl->AddQuadFilled(projectToScreen(wp[0], origin, viewportSize, ctx),
                                      projectToScreen(wp[1], origin, viewportSize, ctx),
                                      projectToScreen(wp[2], origin, viewportSize, ctx),
                                      projectToScreen(wp[3], origin, viewportSize, ctx),
                                      fill);
                }

                for (int i = 0; i < 4; ++i) {
                    drawEdge(wp[i], wp[(i + 1) % 4]);
                }
                if (drawPolygons) {
                    drawPoly(wp[0], wp[2]);
                    if (densityLevel >= 2) drawPoly(wp[1], wp[3]);
                }
                break;
            }
            case ECS::MeshPrimitive::Sphere: {
                Vec3 center = worldMatrix.transformPoint(Vec3(0, 0, 0));
                Vec3 px = worldMatrix.transformPoint(Vec3(0.5f, 0, 0));
                ImVec2 cs = projectToScreen(center, origin, viewportSize, ctx);
                ImVec2 pxs = projectToScreen(px, origin, viewportSize, ctx);
                f32 rad = std::sqrt((pxs.x - cs.x) * (pxs.x - cs.x) + (pxs.y - cs.y) * (pxs.y - cs.y));
                if (m_meshPreviewMode == MeshPreviewMode::Textured) {
                    const Vec3 lit = lightColorAt(center, entityAxis(world, entity, 2, Vec3(0, 0, 1)),
                                                  Scene::meshReceivesShadows(world, entity));
                    dl->AddCircleFilled(cs, rad, toColor(Vec3(lit.x * 0.78f, lit.y * 0.78f, lit.z * 0.78f), 0.90f), 24);
                }
                if (drawContour) {
                    dl->AddCircle(cs, rad, wireCol, 32, thickness);
                    drawRing(worldMatrix, Vec3(1, 0, 0), Vec3(0, 1, 0), 0.5f, 28, wireCol, thickness);
                    drawRing(worldMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, 28, wireCol, thickness);
                    drawRing(worldMatrix, Vec3(0, 1, 0), Vec3(0, 0, 1), 0.5f, 28, wireCol, thickness);
                }
                if (drawPolygons) {
                    drawRing(worldMatrix, Vec3(1, 0, 0), Vec3(0, 1, 0), 0.5f, spherePolySegments, polyCol, polyThickness);
                    drawRing(worldMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, spherePolySegments, polyCol, polyThickness);
                    drawRing(worldMatrix, Vec3(0, 1, 0), Vec3(0, 0, 1), 0.5f, spherePolySegments, polyCol, polyThickness);
                }
                break;
            }
            case ECS::MeshPrimitive::Cylinder: {
                Mat4 topMatrix = worldMatrix * Mat4::translation(0.0f, 0.5f, 0.0f);
                Mat4 bottomMatrix = worldMatrix * Mat4::translation(0.0f, -0.5f, 0.0f);
                if (m_meshPreviewMode == MeshPreviewMode::Textured) {
                    Vec3 cTop = topMatrix.transformPoint(Vec3(0,0,0));
                    Vec3 cBottom = bottomMatrix.transformPoint(Vec3(0,0,0));
                    Vec3 cMid = (cTop + cBottom) * 0.5f;
                    const Vec3 lit = lightColorAt(cMid, entityAxis(world, entity, 0, Vec3::right()),
                                                  Scene::meshReceivesShadows(world, entity));
                    dl->AddLine(projectToScreen(cTop, origin, viewportSize, ctx),
                                projectToScreen(cBottom, origin, viewportSize, ctx),
                                toColor(Vec3(lit.x * 0.72f, lit.y * 0.72f, lit.z * 0.72f), 0.75f), 18.0f * ctx.viewportZoom);
                }
                if (drawContour) {
                    drawRing(topMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, 24, wireCol, thickness);
                    drawRing(bottomMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, 24, wireCol, thickness);
                }
                for (int i = 0; i < 4; ++i) {
                    const f32 a = (3.14159265f * 0.5f) * static_cast<f32>(i);
                    const Vec3 rim(std::cos(a) * 0.5f, 0.0f, std::sin(a) * 0.5f);
                    drawEdge(topMatrix.transformPoint(rim), bottomMatrix.transformPoint(rim));
                }
                if (drawPolygons) {
                    for (int i = 0; i < sidePolySlices; ++i) {
                        const f32 a0 = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(sidePolySlices);
                        const f32 a1 = (2.0f * 3.14159265f * static_cast<f32>((i + 1) % sidePolySlices)) / static_cast<f32>(sidePolySlices);
                        const Vec3 t0(std::cos(a0) * 0.5f, 0.5f, std::sin(a0) * 0.5f);
                        const Vec3 b1(std::cos(a1) * 0.5f, -0.5f, std::sin(a1) * 0.5f);
                        drawPoly(worldMatrix.transformPoint(t0), worldMatrix.transformPoint(b1));
                    }
                }
                break;
            }
            case ECS::MeshPrimitive::Capsule: {
                Mat4 topMatrix = worldMatrix * Mat4::translation(0.0f, 0.25f, 0.0f);
                Mat4 bottomMatrix = worldMatrix * Mat4::translation(0.0f, -0.25f, 0.0f);
                if (m_meshPreviewMode == MeshPreviewMode::Textured) {
                    Vec3 cTop = topMatrix.transformPoint(Vec3(0,0,0));
                    Vec3 cBottom = bottomMatrix.transformPoint(Vec3(0,0,0));
                    Vec3 cMid = (cTop + cBottom) * 0.5f;
                    const Vec3 lit = lightColorAt(cMid, entityAxis(world, entity, 1, Vec3::up()),
                                                  Scene::meshReceivesShadows(world, entity));
                    dl->AddLine(projectToScreen(cTop, origin, viewportSize, ctx),
                                projectToScreen(cBottom, origin, viewportSize, ctx),
                                toColor(Vec3(lit.x * 0.70f, lit.y * 0.70f, lit.z * 0.70f), 0.70f), 20.0f * ctx.viewportZoom);
                }
                if (drawContour) {
                    drawRing(topMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, 24, wireCol, thickness);
                    drawRing(bottomMatrix, Vec3(1, 0, 0), Vec3(0, 0, 1), 0.5f, 24, wireCol, thickness);
                    drawRing(worldMatrix, Vec3(1, 0, 0), Vec3(0, 1, 0), 0.5f, 24, wireCol, thickness);
                    drawRing(worldMatrix, Vec3(0, 1, 0), Vec3(0, 0, 1), 0.5f, 24, wireCol, thickness);
                }
                for (int i = 0; i < 4; ++i) {
                    const f32 a = (3.14159265f * 0.5f) * static_cast<f32>(i);
                    const Vec3 rim(std::cos(a) * 0.5f, 0.0f, std::sin(a) * 0.5f);
                    drawEdge(topMatrix.transformPoint(rim), bottomMatrix.transformPoint(rim));
                }
                if (drawPolygons) {
                    for (int i = 0; i < sidePolySlices; ++i) {
                        const f32 a0 = (2.0f * 3.14159265f * static_cast<f32>(i)) / static_cast<f32>(sidePolySlices);
                        const f32 a1 = (2.0f * 3.14159265f * static_cast<f32>((i + 1) % sidePolySlices)) / static_cast<f32>(sidePolySlices);
                        const Vec3 t0(std::cos(a0) * 0.5f, 0.25f, std::sin(a0) * 0.5f);
                        const Vec3 b1(std::cos(a1) * 0.5f, -0.25f, std::sin(a1) * 0.5f);
                        drawPoly(worldMatrix.transformPoint(t0), worldMatrix.transformPoint(b1));
                    }
                }
                break;
            }
        }

        return true;
    };

    auto drawEntity = [&](ECS::Entity entity) {
        if (Scene::isEffectivelyDisabled(world, entity)) return;
        if (world.has<ECS::LightComponent>(entity)) return;

        if (drawMeshPreview(entity)) return;

        Vec3 worldPosition;
        if (!tryGetEntityPosition(world, entity, worldPosition)) return;
        drawMarker(entity, worldPosition);
    };

    ECS::ComponentQuery transformQuery;
    transformQuery.with<ECS::Transform>();
    transformQuery.without<ECS::Sprite>();
    world.forEach<ECS::Transform>(transformQuery, [&](ECS::Entity entity, ECS::Transform&) {
        drawEntity(entity);
    });

    ECS::ComponentQuery pos3Query;
    pos3Query.with<ECS::Position3D>();
    pos3Query.without<ECS::Transform>();
    pos3Query.without<ECS::Sprite>();
    world.forEach<ECS::Position3D>(pos3Query, [&](ECS::Entity entity, ECS::Position3D&) {
        drawEntity(entity);
    });

    if (ctx.selectedEntity.isValid() &&
        world.has<ECS::TerrainComponent>(ctx.selectedEntity) &&
        m_terrainCullStats.totalChunks > 0) {
        char statsBuf[192];
        snprintf(statsBuf, sizeof(statsBuf),
                 "Chunks %u/%u (%u culled)  LOD0:%u LOD1:%u LOD2:%u LOD3:%u",
                 m_terrainCullStats.visibleChunks,
                 m_terrainCullStats.totalChunks,
                 m_terrainCullStats.culledChunks,
                 m_terrainCullStats.lodCounts[0],
                 m_terrainCullStats.lodCounts[1],
                 m_terrainCullStats.lodCounts[2],
                 m_terrainCullStats.lodCounts[3]);
        dl->AddText(ImVec2(origin.x + 8, origin.y + viewportSize.y - 22),
                    IM_COL32(180, 220, 180, 220), statsBuf);
    }

    if (useMeshRaster && !m_lastGpuSceneActive) {
        blitMeshRasterizer(dl, meshRaster, m_meshRasterTexture);
    }
}

void SceneViewport::drawLightGizmos(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const Mat4 vpCache3D = (ctx.viewMode == EditorContext::ViewMode::Mode3D)
        ? computeVP3D(viewportSize, ctx) : Mat4::identity();
    auto proj3D = [&](const Vec3& p) -> ImVec2 {
        return (ctx.viewMode == EditorContext::ViewMode::Mode3D)
            ? projectToScreenVP(p, origin, viewportSize, vpCache3D)
            : projectToScreen(p, origin, viewportSize, ctx);
    };

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::DirectionalLightComponent>();

        u32 directionalIndex = 0;
        world.forEach<ECS::LightComponent, ECS::DirectionalLightComponent>(
            q, [&](ECS::Entity entity, ECS::LightComponent& lc, ECS::DirectionalLightComponent&) {
                if (Scene::isEffectivelyDisabled(world, entity)) return;

                Vec3 anchor;
                if (!tryGetEntityPosition(world, entity, anchor)) {
                    anchor = Vec3(ctx.camFocus.x, ctx.camFocus.y, ctx.camFocus.z)
                           + Vec3(8.0f + static_cast<f32>(directionalIndex) * 2.5f, 6.0f, 0.0f);
                }
                ++directionalIndex;

                const bool selected = (ctx.selectedEntity == entity);
                const Vec3 dir = entityForward(world, entity);
                const Vec3 tip = anchor + dir * 2.5f;
                const ImVec2 screenPos = proj3D(anchor);
                const ImVec2 tipPos = proj3D(tip);
                const ImU32 color = lightColor(lc, selected);

                const f32 sunRadius = 8.0f;
                const f32 rayLength = 12.0f;
                const int rayCount = 6;

                dl->AddCircleFilled(screenPos, sunRadius * 0.5f, color, 12);

                for (int i = 0; i < rayCount; ++i) {
                    f32 angle = (2.0f * 3.14159265f / rayCount) * i;
                    f32 x1 = sunRadius * std::cos(angle);
                    f32 y1 = sunRadius * std::sin(angle);
                    f32 x2 = (sunRadius + rayLength) * std::cos(angle);
                    f32 y2 = (sunRadius + rayLength) * std::sin(angle);
                    dl->AddLine(ImVec2(screenPos.x + x1, screenPos.y + y1),
                               ImVec2(screenPos.x + x2, screenPos.y + y2), color, 2.0f);
                }

                dl->AddLine(screenPos, tipPos, color, selected ? 2.5f : 1.5f);
                dl->AddCircleFilled(tipPos, 3.0f, color, 10);

                dl->AddText(ImVec2(screenPos.x + 12, screenPos.y - 8), IM_COL32(220, 220, 230, 230), "Dir");
            });
    }

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::PointLightComponent>();

        world.forEach<ECS::LightComponent, ECS::PointLightComponent>(
            q, [&](ECS::Entity entity, ECS::LightComponent& lc, ECS::PointLightComponent& ptLight) {
                if (Scene::isEffectivelyDisabled(world, entity)) return;

                Vec3 position;
                if (!tryGetEntityPosition(world, entity, position)) return;

                ImVec2 screenPos = proj3D(position);
                const bool selected = (ctx.selectedEntity == entity);
                ImU32 color = lightColor(lc, selected);

                Vec3 radiusTestPoint = position + entityAxis(world, entity, 0, Vec3::right()) * ptLight.radius;
                ImVec2 radiusScreenPoint = proj3D(radiusTestPoint);
                f32 radiusScreenDist = std::sqrt(
                    (radiusScreenPoint.x - screenPos.x) * (radiusScreenPoint.x - screenPos.x) +
                    (radiusScreenPoint.y - screenPos.y) * (radiusScreenPoint.y - screenPos.y)
                );

                dl->AddCircleFilled(screenPos, 5.0f, color, 16);
                dl->AddCircle(screenPos, radiusScreenDist, color, 16, 1.5f);
                dl->AddText(ImVec2(screenPos.x + 8, screenPos.y - 8), IM_COL32(220, 220, 230, 230), "Pt");
            });
    }

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::SpotLightComponent>();

        world.forEach<ECS::LightComponent, ECS::SpotLightComponent>(
            q, [&](ECS::Entity entity, ECS::LightComponent& lc, ECS::SpotLightComponent& spotLight) {
                if (Scene::isEffectivelyDisabled(world, entity)) return;

                Vec3 position;
                if (!tryGetEntityPosition(world, entity, position)) return;

                const bool selected = (ctx.selectedEntity == entity);
                const Vec3 dir = entityForward(world, entity);
                Vec3 right = entityAxis(world, entity, 0, Vec3::right());
                if (std::abs(dir.dot(right)) > 0.95f) right = Vec3::up();
                Vec3 up = dir.cross(right).normalized();
                right = up.cross(dir).normalized();

                ImVec2 screenPos = proj3D(position);
                ImU32 color = lightColor(lc, selected);

                Vec3 coneEnd = position + dir * spotLight.radius;
                const f32 coneRadius = spotLight.radius * std::sin(spotLight.angle * kDegToRad * 0.5f);
                Vec3 baseRight = coneEnd + right * coneRadius;
                Vec3 baseLeft  = coneEnd - right * coneRadius;
                Vec3 baseUp    = coneEnd + up * coneRadius;
                Vec3 baseDown  = coneEnd - up * coneRadius;

                dl->AddLine(screenPos, proj3D(baseRight), color, selected ? 2.5f : 1.5f);
                dl->AddLine(screenPos, proj3D(baseLeft),  color, selected ? 2.5f : 1.5f);
                dl->AddLine(screenPos, proj3D(baseUp),    color, selected ? 2.0f : 1.25f);
                dl->AddLine(screenPos, proj3D(baseDown),  color, selected ? 2.0f : 1.25f);
                dl->AddLine(proj3D(baseRight), proj3D(baseUp),    color, 1.25f);
                dl->AddLine(proj3D(baseUp),    proj3D(baseLeft),  color, 1.25f);
                dl->AddLine(proj3D(baseLeft),  proj3D(baseDown),  color, 1.25f);
                dl->AddLine(proj3D(baseDown),  proj3D(baseRight), color, 1.25f);
                dl->AddCircleFilled(screenPos, 5.0f, color, 12);
                dl->AddText(ImVec2(screenPos.x + 8, screenPos.y - 8), IM_COL32(220, 220, 230, 230), "Sp");
            });
    }
}

void SceneViewport::createOrUpdateLightGizmoEntities(ECS::World& world) {
}

void SceneViewport::drawGizmo(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    if (!ctx.selectedEntity.isValid()) return;
    auto* pos = world.get<ECS::Transform>(ctx.selectedEntity);
    auto* pos3 = world.get<ECS::Position3D>(ctx.selectedEntity);
    if (!pos && !pos3) {
        pos = &world.add<ECS::Transform>(ctx.selectedEntity);
    }

    Vec3 worldPos;
    if (!tryGetEntityPosition(world, ctx.selectedEntity, worldPos)) return;
    ImVec2 screenPos = projectToScreen(worldPos, origin, viewportSize, ctx);
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    const float HL   = 30.0f * ctx.viewportZoom;
    const bool  is3D = (ctx.viewMode == EditorContext::ViewMode::Mode3D);
    const bool  zDimmed = (ctx.viewMode == EditorContext::ViewMode::Mode2D);

    float sinY = 0, cosY = 1, sinP = 0, cosP = 1;
    if (is3D) { sinY=std::sin(ctx.camYaw); cosY=std::cos(ctx.camYaw); sinP=std::sin(ctx.camPitch); cosP=std::cos(ctx.camPitch); }

    Vec3 worldAxisX(1.f, 0.f, 0.f);
    Vec3 worldAxisY(0.f, 1.f, 0.f);
    Vec3 worldAxisZ(0.f, 0.f, 1.f);
    if (is3D && ctx.gizmoSpace == EditorContext::GizmoSpace::Local) {
        const Mat4 worldMatrix = entityMatrix(world, ctx.selectedEntity);
        worldAxisX = matrixAxis(worldMatrix, 0, worldAxisX);
        worldAxisY = matrixAxis(worldMatrix, 1, worldAxisY);
        worldAxisZ = matrixAxis(worldMatrix, 2, worldAxisZ);
    }

    ImVec2 rawX, rawY, rawZ;
    if (is3D) {
        const float axisWorldStep = std::max(0.25f, ctx.camDistance * 0.02f);
        auto axisScreenDelta = [&](const Vec3& axis) -> ImVec2 {
            Vec3 dir = axis;
            const float len = dir.length();
            if (len > 1e-6f) dir = dir / len;
            ImVec2 s0 = projectToScreen(worldPos, origin, viewportSize, ctx);
            ImVec2 s1 = projectToScreen(worldPos + dir * axisWorldStep, origin, viewportSize, ctx);
            return ImVec2(s1.x - s0.x, s1.y - s0.y);
        };
        rawX = axisScreenDelta(worldAxisX);
        rawY = axisScreenDelta(worldAxisY);
        rawZ = axisScreenDelta(worldAxisZ);
    } else {
        rawX = ImVec2(1.f, 0.f);
        rawY = ImVec2(0.f, -1.f);
        rawZ = ImVec2(0.f, 0.f);
    }

    auto axisViewDepth = [&](const Vec3& axis) -> float {
        Vec3 dir = axis;
        const float len = dir.length();
        if (len > 1e-6f) dir = dir / len;
        float vzc = -sinY * dir.x + cosY * dir.z;
        return -sinP * dir.y + cosP * vzc;
    };
    auto axisForeshorten = [&](const Vec3& axis) -> float {
        if (!is3D) return 1.f;
        Vec3 dir = axis;
        const float len = dir.length();
        if (len > 1e-6f) dir = dir / len;
        float vx  = cosY * dir.x + sinY * dir.z;
        float vzc = -sinY * dir.x + cosY * dir.z;
        float vy2 = cosP * dir.y + sinP * vzc;
        return std::sqrt(vx * vx + vy2 * vy2);
    };
    auto norm2D = [](ImVec2 v) -> ImVec2 {
        float mag = std::sqrt(v.x * v.x + v.y * v.y);
        if (mag < 1e-4f) return ImVec2(0.f, 0.f);
        return ImVec2(v.x / mag, v.y / mag);
    };
    const ImVec2 nX = norm2D(rawX);
    const ImVec2 nY = norm2D(rawY);
    const ImVec2 nZ = norm2D(rawZ);
    m_axisRawDirs[0] = nX;
    m_axisRawDirs[1] = nY;
    m_axisRawDirs[2] = nZ;
    m_gizmoScreenOrigin = screenPos;

    const float fsX = axisForeshorten(worldAxisX);
    const float fsY = axisForeshorten(worldAxisY);
    const float fsZ = axisForeshorten(worldAxisZ);

    auto axisEnd = [&](ImVec2 dir, float foreshorten) -> ImVec2 {
        if (foreshorten < 0.001f) return screenPos;
        float len = HL * std::max(foreshorten, 0.4f);
        return ImVec2(screenPos.x + dir.x * len, screenPos.y + dir.y * len);
    };
    auto axisAlpha = [](float foreshorten) -> float {
        return 0.4f + 0.6f * std::min(foreshorten, 1.f);
    };

    ImVec2 endX = axisEnd(nX, fsX), endY = axisEnd(nY, fsY), endZ = axisEnd(nZ, fsZ);
    float  alpX = axisAlpha(fsX), alpY = axisAlpha(fsY), alpZ = axisAlpha(fsZ);

    struct DrawEntry { int id; float depth; };
    DrawEntry order[3] = {
        {1, axisViewDepth(worldAxisX)},
        {2, axisViewDepth(worldAxisY)},
        {3, axisViewDepth(worldAxisZ)}
    };
    std::sort(order, order+3, [](const DrawEntry& a, const DrawEntry& b){ return a.depth > b.depth; });

    int keyAxis = 0;
    if      (ImGui::IsKeyDown(ImGuiKey_X)) keyAxis = 1;
    else if (ImGui::IsKeyDown(ImGuiKey_Y)) keyAxis = 2;
    else if (ImGui::IsKeyDown(ImGuiKey_Z)) keyAxis = 3;

    ImVec2 mouse = ImGui::GetMousePos();
    bool mouseInVP = mouse.x >= origin.x && mouse.x <= origin.x+viewportSize.x &&
                     mouse.y >= origin.y && mouse.y <= origin.y+viewportSize.y;

    if (!m_gizmoDragging && mouseInVP && ImGui::IsWindowHovered()) {
        if (keyAxis != 0) {
            m_hoveredAxis = keyAxis;
        } else {
            m_hoveredAxis = 0;
            float cdist = std::sqrt((mouse.x-screenPos.x)*(mouse.x-screenPos.x)+(mouse.y-screenPos.y)*(mouse.y-screenPos.y));
            if (cdist < 9.f) {
                m_hoveredAxis = 4;
            } else if (ctx.gizmoMode == EditorContext::GizmoMode::Rotate) {
                auto screenScaleForAxis = [&](const Vec3& axis) -> float {
                    Vec3 dir = axis;
                    const float len = dir.length();
                    if (len < 1e-6f) return 0.f;
                    dir = dir / len;
                    ImVec2 s0 = projectToScreen(worldPos, origin, viewportSize, ctx);
                    ImVec2 s1 = projectToScreen(worldPos + dir, origin, viewportSize, ctx);
                    const float dx = s1.x - s0.x;
                    const float dy = s1.y - s0.y;
                    return std::sqrt(dx * dx + dy * dy);
                };
                auto worldRadiusForRing = [&](const Vec3& axisA, const Vec3& axisB) -> float {
                    const float scaleA = screenScaleForAxis(axisA);
                    const float scaleB = screenScaleForAxis(axisB);
                    const float pxPerUnit = std::max(0.01f, 0.5f * (scaleA + scaleB));
                    return HL / pxPerUnit;
                };
                auto ringHit = [&](const Vec3& axisA, const Vec3& axisB) -> bool {
                    Vec3 a = axisA;
                    Vec3 b = axisB;
                    const float aLen = a.length();
                    const float bLen = b.length();
                    if (aLen < 1e-6f || bLen < 1e-6f) return false;
                    a = a / aLen;
                    b = b / bLen;
                    const float worldRadius = worldRadiusForRing(a, b);
                    const int N = 48;
                    for (int i = 0; i < N; ++i) {
                        const float ang = 6.28318f * static_cast<float>(i) / static_cast<float>(N);
                        const Vec3 wp = worldPos + (a * std::cos(ang) + b * std::sin(ang)) * worldRadius;
                        const ImVec2 sp = projectToScreen(wp, origin, viewportSize, ctx);
                        const float dx = mouse.x - sp.x;
                        const float dy = mouse.y - sp.y;
                        if (dx * dx + dy * dy < 64.f) return true;
                    }
                    return false;
                };
                if      (ringHit(worldAxisY, worldAxisZ)) m_hoveredAxis = 1;
                else if (ringHit(worldAxisX, worldAxisZ)) m_hoveredAxis = 2;
                else if (ringHit(worldAxisX, worldAxisY)) m_hoveredAxis = 3;
            } else {
                auto ptLineDist = [&](ImVec2 b, ImVec2 e) -> float {
                    float dx=e.x-b.x, dy=e.y-b.y, l2=dx*dx+dy*dy;
                    if (l2 < 0.0001f) return std::sqrt((mouse.x-b.x)*(mouse.x-b.x)+(mouse.y-b.y)*(mouse.y-b.y));
                    float t = std::max(0.f,std::min(1.f,((mouse.x-b.x)*dx+(mouse.y-b.y)*dy)/l2));
                    float px=b.x+t*dx, py=b.y+t*dy;
                    return std::sqrt((mouse.x-px)*(mouse.x-px)+(mouse.y-py)*(mouse.y-py));
                };
                if      (ptLineDist(screenPos, endX) < 8.f) m_hoveredAxis = 1;
                else if (ptLineDist(screenPos, endY) < 8.f) m_hoveredAxis = 2;
                else if (!zDimmed && ptLineDist(screenPos, endZ) < 8.f) m_hoveredAxis = 3;
            }
        }
    }

    const u32 COL_X = IM_COL32(255,50,50,255), COL_Y = IM_COL32(50,255,50,255), COL_Z = IM_COL32(50,100,255,255);
    const u32 COL_HOVER = IM_COL32(255,220,0,255), COL_DRAG = IM_COL32(255,255,255,255);

    auto axisColor = [&](int axId, float alpha) -> u32 {
        int activeAx = m_gizmoDragging ? m_gizmoDragAxis : (keyAxis ? keyAxis : m_hoveredAxis);
        if (activeAx == axId) return m_gizmoDragging ? COL_DRAG : COL_HOVER;
        u32 base = (axId==1) ? COL_X : (axId==2) ? COL_Y : COL_Z;
        return (base & 0x00FFFFFFu) | (u32(std::min(alpha,1.f)*255.f) << 24);
    };

    auto drawArrow = [&](ImVec2 from, ImVec2 to, u32 col) {
        float dx=to.x-from.x, dy=to.y-from.y, d=std::sqrt(dx*dx+dy*dy);
        dl->AddLine(from, to, col, 3.f);
        if (d < 1.f) { dl->AddCircleFilled(from, 4.f, col, 12); return; }
        float ux=dx/d, uy=dy/d;
        ImVec2 tip(to.x+ux*8.f, to.y+uy*8.f);
        dl->AddTriangleFilled(tip, ImVec2(to.x-uy*5.f,to.y+ux*5.f), ImVec2(to.x+uy*5.f,to.y-ux*5.f), col);
    };
    auto drawScaleBox = [&](ImVec2 from, ImVec2 to, u32 col) {
        dl->AddLine(from, to, col, 2.f);
        dl->AddRectFilled(ImVec2(to.x-5.f,to.y-5.f), ImVec2(to.x+5.f,to.y+5.f), col);
    };
    auto screenScaleForAxis = [&](const Vec3& axis) -> float {
        Vec3 dir = axis;
        const float len = dir.length();
        if (len < 1e-6f) return 0.f;
        dir = dir / len;
        ImVec2 s0 = projectToScreen(worldPos, origin, viewportSize, ctx);
        ImVec2 s1 = projectToScreen(worldPos + dir, origin, viewportSize, ctx);
        const float dx = s1.x - s0.x;
        const float dy = s1.y - s0.y;
        return std::sqrt(dx * dx + dy * dy);
    };
    auto worldRadiusForRing = [&](const Vec3& axisA, const Vec3& axisB) -> float {
        const float scaleA = screenScaleForAxis(axisA);
        const float scaleB = screenScaleForAxis(axisB);
        const float pxPerUnit = std::max(0.01f, 0.5f * (scaleA + scaleB));
        return HL / pxPerUnit;
    };
    auto drawRing = [&](const Vec3& axisA, const Vec3& axisB, u32 col) {
        Vec3 a = axisA;
        Vec3 b = axisB;
        const float aLen = a.length();
        const float bLen = b.length();
        if (aLen < 1e-6f || bLen < 1e-6f) return;
        a = a / aLen;
        b = b / bLen;
        const float worldRadius = worldRadiusForRing(a, b);
        const int N = 64;
        ImVec2 prev;
        bool hasPrev = false;
        for (int i = 0; i <= N; ++i) {
            const float ang = 6.28318f * static_cast<float>(i) / static_cast<float>(N);
            const Vec3 wp = worldPos + (a * std::cos(ang) + b * std::sin(ang)) * worldRadius;
            const ImVec2 sp = projectToScreen(wp, origin, viewportSize, ctx);
            if (hasPrev) dl->AddLine(prev, sp, col, 2.f);
            prev = sp;
            hasPrev = true;
        }
    };

    for (int i = 0; i < 3; ++i) {
        int axId = order[i].id;
        if (axId == 3 && zDimmed) continue;
        ImVec2 end  = (axId==1) ? endX : (axId==2) ? endY : endZ;
        float  alp  = (axId==1) ? alpX : (axId==2) ? alpY : alpZ;
        u32    col  = axisColor(axId, alp);
        if      (ctx.gizmoMode == EditorContext::GizmoMode::Translate) drawArrow(screenPos, end, col);
        else if (ctx.gizmoMode == EditorContext::GizmoMode::Scale)     drawScaleBox(screenPos, end, col);
        else if (ctx.gizmoMode == EditorContext::GizmoMode::Rotate) {
            const Vec3& ringA = (axId == 1) ? worldAxisY : worldAxisX;
            const Vec3& ringB = (axId == 3) ? worldAxisY : worldAxisZ;
            drawRing(ringA, ringB, axisColor(axId, 1.f));
        }
    }

    if (ctx.gizmoMode == EditorContext::GizmoMode::None)
        dl->AddCircle(screenPos, 6.f, IM_COL32(255,255,255,180), 12, 2.f);

    if (world.has<Audio::AudioEmitter>(ctx.selectedEntity)) {
        auto* emitter = world.get<Audio::AudioEmitter>(ctx.selectedEntity);
        if (emitter->spatial && emitter->maxDistance > 0.0f) {
            f32 w2s = ctx.viewportZoom * 50.0f;
            f32 fullVolumeRadius = emitter->maxDistance * 0.5f;
            char buf[64];
            dl->AddCircle(screenPos, fullVolumeRadius * w2s, IM_COL32(0, 255, 255, 80), 48, 2.0f);
            snprintf(buf, sizeof(buf), "near %.0f", fullVolumeRadius);
            dl->AddText(ImVec2(screenPos.x + fullVolumeRadius * w2s + 4, screenPos.y - 8), IM_COL32(0, 255, 255, 180), buf);
            dl->AddCircle(screenPos, emitter->maxDistance * w2s, IM_COL32(50, 130, 255, 60), 64, 2.0f);
            snprintf(buf, sizeof(buf), "max %.0f", emitter->maxDistance);
            dl->AddText(ImVec2(screenPos.x + emitter->maxDistance * w2s + 4, screenPos.y - 8), IM_COL32(50, 130, 255, 180), buf);
            dl->AddText(ImVec2(screenPos.x + 8, screenPos.y - 20), IM_COL32(180, 180, 255, 220), "S");
        }
    }
}

void SceneViewport::drawPhysicsDebug(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    if (!ctx.physicsDebugVisible) return;
    using namespace Physics2D;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const f32 worldToScreen = ctx.viewportZoom * 50.0f;

    auto worldToScreen_fn = [&](f32 wx, f32 wy) -> ImVec2 {
        return projectToScreen({wx, wy, 0.0f}, origin, viewportSize, ctx);
    };

    ECS::ComponentQuery q;
    q.with<Collider2D>();
    q.with<ECS::Transform>();

    world.forEach<Collider2D, ECS::Transform>(q,
        [&](ECS::Entity entity, Collider2D& col, ECS::Transform& pos) {
            f32 cx = pos.position.x + col.offset.x;
            f32 cy = pos.position.y + col.offset.y;
            ImU32 color = IM_COL32(col.debugColor[0], col.debugColor[1], col.debugColor[2], col.debugColor[3]);
            if (col.shape == ColliderShape::AABB) {
                f32 hw = col.size.x * 0.5f * worldToScreen;
                f32 hh = col.size.y * 0.5f * worldToScreen;
                ImVec2 sc = worldToScreen_fn(cx, cy);
                dl->AddRect(ImVec2(sc.x - hw, sc.y - hh),
                            ImVec2(sc.x + hw, sc.y + hh),
                            color, 0.0f, 0, 1.5f);
            } else if (col.shape == ColliderShape::Circle) {
                ImVec2 sc = worldToScreen_fn(cx, cy);
                dl->AddCircle(sc, col.radius * worldToScreen, color, 32, 1.5f);
            }
        });
}

bool SceneViewport::drawSkyboxForView(ImDrawList* drawList, ImVec2 origin, ImVec2 viewportSize,
                                      ECS::World& world, const EditorContext& ctx,
                                      const Render::SkyboxCamera& camera,
                                      bool respectEditorToggle,
                                      Render::SkyboxRenderer* renderer) {
    if (!drawList) return false;
    if (respectEditorToggle && !ctx.skyboxEnabled) return false;

    const std::string projectRoot = resolveProjectRootFromScenePath(ctx.currentScenePath);

    int presetIndex = ctx.skyboxIndex;
    std::filesystem::path texturePath;

    const Scene::ActiveSkybox active = Scene::findActiveSkybox(world);
    if (active.component) {
        presetIndex = active.component->presetIndex;
        texturePath = Scene::resolveSkyboxTexturePath(*active.component, projectRoot);
    } else {
        texturePath = Scene::resolveBuiltinSkyboxPath(presetIndex);
    }

    if (texturePath.empty()) return false;

    const std::string textureKey = texturePath.string();
    Render::SkyboxRenderer& target = renderer ? *renderer : m_skyboxRenderer;
    return target.draw(drawList, origin, viewportSize, camera, textureKey);
}

bool SceneViewport::drawSkybox(ImDrawList* drawList, ImVec2 origin, ImVec2 viewportSize,
                               ECS::World& world, EditorContext& ctx) {
    if (ctx.viewMode != EditorContext::ViewMode::Mode3D) return false;

    Render::SkyboxCamera skyCamera;
    const f32 sinY = std::sin(ctx.camYaw);
    const f32 cosY = std::cos(ctx.camYaw);
    const f32 sinP = std::sin(ctx.camPitch);
    const f32 cosP = std::cos(ctx.camPitch);
    skyCamera.forward = Vec3(-sinY * cosP, sinP, cosY * cosP).normalized();
    const Vec3 worldUp(0.0f, 1.0f, 0.0f);
    skyCamera.right = skyCamera.forward.cross(worldUp);
    if (skyCamera.right.lengthSquared() < 1e-6f) {
        skyCamera.right = Vec3(1.0f, 0.0f, 0.0f);
    } else {
        skyCamera.right = skyCamera.right.normalized();
    }
    skyCamera.up = skyCamera.right.cross(skyCamera.forward).normalized();
    skyCamera.fovY = 1.0472f;
    skyCamera.aspect = viewportSize.x / std::max(viewportSize.y, 1.0f);

    return drawSkyboxForView(drawList, origin, viewportSize, world, ctx, skyCamera, true);
}

void SceneViewport::drawGrid(ImDrawList* drawList, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx) {
    if (!m_config.grid) return;

    if (ctx.viewMode != EditorContext::ViewMode::Mode2D) {
        drawGrid3D(drawList, origin, viewportSize, ctx);
        return;
    }

    f32 baseSpacing = m_config.gridSpacing;
    f32 scaledSpacing = baseSpacing * ctx.viewportZoom;
    const f32 minPixelSpacing = 12.0f;
    while (scaledSpacing < minPixelSpacing) {
        scaledSpacing *= 2.0f;
    }
    
    f32 centerX = origin.x + viewportSize.x * 0.5f;
    f32 centerY = origin.y + viewportSize.y * 0.5f;
    f32 offsetX = ctx.viewportPanX;
    f32 offsetY = ctx.viewportPanY;
    f32 axisX = centerX + offsetX;
    f32 axisY = centerY + offsetY;
    
    ImU32 gridColor = IM_COL32(100, 100, 120, 80);
    ImU32 axisColor = IM_COL32(200, 100, 100, 150);

    f32 startX = axisX - std::floor((axisX - origin.x) / scaledSpacing) * scaledSpacing;
    f32 startY = axisY - std::floor((axisY - origin.y) / scaledSpacing) * scaledSpacing;

    for (f32 x = startX; x <= origin.x + viewportSize.x; x += scaledSpacing) {
        if (std::fabs(x - axisX) < 1.0f) {
            drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + viewportSize.y), axisColor, 1.5f);
        } else {
            drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + viewportSize.y), gridColor, 0.5f);
        }
    }
    
    for (f32 y = startY; y <= origin.y + viewportSize.y; y += scaledSpacing) {
        if (std::fabs(y - axisY) < 1.0f) {
            drawList->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + viewportSize.x, y), axisColor, 1.5f);
        } else {
            drawList->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + viewportSize.x, y), gridColor, 0.5f);
        }
    }
    
    drawList->AddCircle(ImVec2(centerX, centerY), 8.0f, IM_COL32(255, 200, 0, 200), 12, 2.0f);
}

void SceneViewport::drawGrid3D(ImDrawList* dl, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx) {
    const ImU32 gridColorBase  = IM_COL32(100, 100, 120, 255);
    const ImU32 majorGridColor = IM_COL32(130, 130, 155, 255);
    ImU32 axisColorX = IM_COL32(220, 60, 60, 200);
    ImU32 axisColorZ = IM_COL32(60, 60, 220, 200);

    Mat4 vp = computeVP3D(viewportSize, ctx);
    const f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
    const f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
    const Vec3 camPos(
        ctx.camFocus.x + sinY * cosP * ctx.camDistance,
        ctx.camFocus.y - sinP * ctx.camDistance,
        ctx.camFocus.z - cosY * cosP * ctx.camDistance);

    // Projects a world point to screen. Returns false only if behind the near plane (w <= 0.1).
    // Off-screen (NDC > 1) coords are intentionally allowed — ImGui clips them automatically,
    // which is required for lines whose far endpoint is outside the viewport but still in front.
    auto projectLine = [&](Vec3 worldPt, ImVec2& screenOut) -> bool {
        Vec4 clip = vp.transformVec4(Vec4(worldPt.x, worldPt.y, worldPt.z, 1.0f));
        if (clip.w <= 0.1f) return false;
        f32 ndcX = clip.x / clip.w;
        f32 ndcY = clip.y / clip.w;
        screenOut.x = origin.x + (ndcX + 1.0f) * 0.5f * viewportSize.x;
        screenOut.y = origin.y + (1.0f - ndcY) * 0.5f * viewportSize.y;
        return true;
    };

    auto fadeColor = [&](ImU32 color, const Vec3& a, const Vec3& b, bool major) -> ImU32 {
        const Vec3 mid = (a + b) * 0.5f;
        const f32 dist = (mid - camPos).length();
        const f32 t = std::clamp((dist - ctx.camDistance * 0.4f) / (ctx.camDistance * 2.5f), 0.0f, 1.0f);
        const f32 fade = (1.0f - t) * (major ? 1.0f : 0.75f);
        const ImU32 baseAlpha = (color >> 24) & 0xFF;
        const ImU32 alpha = static_cast<ImU32>(baseAlpha * fade);
        return (color & 0x00FFFFFF) | (alpha << 24);
    };

    auto drawLine3D = [&](Vec3 a, Vec3 b, ImU32 color, float thickness, bool major = false) {
        ImVec2 sa, sb;
        bool va = projectLine(a, sa);
        bool vb = projectLine(b, sb);
        if (!va && !vb) return;
        const ImU32 faded = fadeColor(color, a, b, major);
        if (va && vb) {
            dl->AddLine(sa, sb, faded, thickness);
            return;
        }
        // One endpoint is behind the near plane — clip the segment in clip space.
        Vec4 clipA = vp.transformVec4(Vec4(a.x, a.y, a.z, 1.0f));
        Vec4 clipB = vp.transformVec4(Vec4(b.x, b.y, b.z, 1.0f));
        const f32 wMin = 0.1f;
        if (clipA.w < wMin) {
            f32 t = (wMin - clipA.w) / (clipB.w - clipA.w);
            Vec4 clipped(clipA.x + t*(clipB.x-clipA.x), clipA.y + t*(clipB.y-clipA.y),
                         clipA.z + t*(clipB.z-clipA.z), wMin);
            sa.x = origin.x + (clipped.x/wMin + 1.0f) * 0.5f * viewportSize.x;
            sa.y = origin.y + (1.0f - clipped.y/wMin) * 0.5f * viewportSize.y;
        } else {
            f32 t = (wMin - clipB.w) / (clipA.w - clipB.w);
            Vec4 clipped(clipB.x + t*(clipA.x-clipB.x), clipB.y + t*(clipA.y-clipB.y),
                         clipB.z + t*(clipA.z-clipB.z), wMin);
            sb.x = origin.x + (clipped.x/wMin + 1.0f) * 0.5f * viewportSize.x;
            sb.y = origin.y + (1.0f - clipped.y/wMin) * 0.5f * viewportSize.y;
        }
        dl->AddLine(sa, sb, faded, thickness);
    };

    float visibleRange = ctx.camDistance;
    float spacing = 1.0f;
    while (visibleRange / spacing > 40.0f) spacing *= 2.0f;
    while (visibleRange / spacing < 8.0f && spacing > 0.25f) spacing *= 0.5f;
    spacing = std::max(spacing, 0.25f);

    const float majorSpacing = spacing * 10.0f;
    float renderDist = visibleRange * 4.0f;
    const int maxLines = 200;
    if ((renderDist * 2.0f / spacing) > maxLines) {
        renderDist = (maxLines * spacing) / 2.0f;
    }

    auto isMajorLine = [&](float coord) -> bool {
        const float m = std::fmod(std::abs(coord), majorSpacing);
        return m < spacing * 0.05f || std::abs(coord) < spacing * 0.05f;
    };

    float camX = ctx.camFocus.x;
    float camZ = ctx.camFocus.z;
    // Use float to preserve sub-unit precision when spacing < 1 (e.g. 0.5f).
    float startX = std::floor((camX - renderDist) / spacing) * spacing;
    float endX   = std::ceil ((camX + renderDist) / spacing) * spacing;
    float startZ = std::floor((camZ - renderDist) / spacing) * spacing;
    float endZ   = std::ceil ((camZ + renderDist) / spacing) * spacing;

    float lineDist = renderDist;

    if (ctx.viewMode == EditorContext::ViewMode::Isometric) {
        for (float x = startX; x <= endX; x += spacing) {
            if (x == 0.f) continue;
            const bool major = isMajorLine(x);
            drawLine3D({x, camZ - lineDist, 0.f}, {x, camZ + lineDist, 0.f},
                       major ? majorGridColor : gridColorBase, major ? 1.0f : 0.5f, major);
        }
        for (float z = startZ; z <= endZ; z += spacing) {
            if (z == 0.f) continue;
            const bool major = isMajorLine(z);
            drawLine3D({camX - lineDist, z, 0.f}, {camX + lineDist, z, 0.f},
                       major ? majorGridColor : gridColorBase, major ? 1.0f : 0.5f, major);
        }
        float axisLen = visibleRange * 100.0f;
        drawLine3D({0.f, -axisLen, 0.f}, {0.f, axisLen, 0.f}, axisColorX, 2.5f);
        drawLine3D({-axisLen, 0.f, 0.f}, {axisLen, 0.f, 0.f}, axisColorZ, 2.5f);
    } else {
        for (float x = startX; x <= endX; x += spacing) {
            if (x == 0.f) continue;
            const bool major = isMajorLine(x);
            drawLine3D({x, 0.f, camZ - lineDist}, {x, 0.f, camZ + lineDist},
                       major ? majorGridColor : gridColorBase, major ? 1.0f : 0.5f, major);
        }
        for (float z = startZ; z <= endZ; z += spacing) {
            if (z == 0.f) continue;
            const bool major = isMajorLine(z);
            drawLine3D({camX - lineDist, 0.f, z}, {camX + lineDist, 0.f, z},
                       major ? majorGridColor : gridColorBase, major ? 1.0f : 0.5f, major);
        }
        float axisLen = visibleRange * 100.0f;
        drawLine3D({0.f, 0.f, -axisLen}, {0.f, 0.f, axisLen}, axisColorZ, 2.5f);
        drawLine3D({-axisLen, 0.f, 0.f}, {axisLen, 0.f, 0.f}, axisColorX, 2.5f);
    }
}

void SceneViewport::drawNavigationWidget(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float padding = 12.0f;
    const float buttonWidth = 62.0f;
    const float widgetSize = 84.0f;

    ImVec2 widgetMin(
        origin.x + viewportSize.x - padding - buttonWidth - 6.0f - widgetSize,
        origin.y + viewportSize.y - padding - widgetSize
    );
    ImVec2 widgetMax(widgetMin.x + widgetSize, widgetMin.y + widgetSize);

    dl->AddRectFilled(widgetMin, widgetMax, IM_COL32(18, 20, 26, 190), 6.0f);
    dl->AddRect(widgetMin, widgetMax, IM_COL32(90, 100, 130, 180), 6.0f, 0, 1.0f);

    bool is3D = (ctx.viewMode == EditorContext::ViewMode::Mode3D ||
                 ctx.viewMode == EditorContext::ViewMode::Isometric);

    ImVec2 center(widgetMin.x + widgetSize * 0.5f, widgetMin.y + widgetSize * 0.5f);
    const float axisLen = 22.0f;

    {
        float sinY = std::sin(ctx.camYaw),  cosY = std::cos(ctx.camYaw);
        float sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);

        auto axisScreenDir = [&](float wx, float wy, float wz) -> ImVec2 {
            float sx  =  cosY * wx + sinY * wz;
            float sy  =  wy;
            float sz  = -sinY * wx + cosY * wz;
            float sy2 =  cosP * sy + sinP * sz;
            float len = std::sqrt(sx * sx + sy2 * sy2);
            if (len < 0.001f) return ImVec2(0.f, 0.f);
            return ImVec2(sx / len * axisLen, -sy2 / len * axisLen);
        };

        ImVec2 xDir = axisScreenDir(1.f, 0.f, 0.f);
        ImVec2 yDir = axisScreenDir(0.f, 1.f, 0.f);
        ImVec2 zDir = axisScreenDir(0.f, 0.f, 1.f);

        dl->AddLine(center, ImVec2(center.x + xDir.x, center.y + xDir.y), IM_COL32(255, 70, 70, 255), 2.0f);
        dl->AddText(ImVec2(center.x + xDir.x + 3.f, center.y + xDir.y - 8.f), IM_COL32(255, 90, 90, 255), "X");

        dl->AddLine(center, ImVec2(center.x + yDir.x, center.y + yDir.y), IM_COL32(70, 255, 90, 255), 2.0f);
        dl->AddText(ImVec2(center.x + yDir.x - 4.f, center.y + yDir.y - 14.f), IM_COL32(90, 255, 110, 255), "Y");

        if (is3D) {
            dl->AddLine(center, ImVec2(center.x + zDir.x, center.y + zDir.y), IM_COL32(90, 140, 255, 255), 2.0f);
            dl->AddText(ImVec2(center.x + zDir.x - 12.f, center.y + zDir.y - 6.f), IM_COL32(120, 165, 255, 255), "Z");
        }
    }

    dl->AddCircleFilled(center, 2.8f, IM_COL32(240, 240, 240, 255));
    dl->AddText(ImVec2(widgetMin.x + 6.0f, widgetMin.y + widgetSize - 18.0f), IM_COL32(220, 220, 230, 220), is3D ? "3D" : "2D");

    ImVec2 btnPos(widgetMax.x + 6.0f, widgetMin.y + widgetSize - 24.0f);
    ImGui::SetCursorScreenPos(btnPos);
    if (ImGui::Button(m_projectionMode == ProjectionMode::Perspective ? "Persp" : "Ortho", ImVec2(buttonWidth, 24.0f))) {
        toggleProjectionMode();
    }
}

f32 SceneViewport::rayIntersectsAABB(const Vec3& rayOrigin, const Vec3& rayDir,
                                     const Vec3& aabbMin, const Vec3& aabbMax) {
    f32 t_enter = 0.0f;
    f32 t_exit = 1e10f;
    
    // Test X slab
    if (std::abs(rayDir.x) > 1e-6f) {
        f32 t0 = (aabbMin.x - rayOrigin.x) / rayDir.x;
        f32 t1 = (aabbMax.x - rayOrigin.x) / rayDir.x;
        if (t0 > t1) std::swap(t0, t1);
        t_enter = std::max(t_enter, t0);
        t_exit = std::min(t_exit, t1);
    } else {
        if (rayOrigin.x < aabbMin.x || rayOrigin.x > aabbMax.x) {
            return -1.0f;
        }
    }
    
    // Test Y slab
    if (std::abs(rayDir.y) > 1e-6f) {
        f32 t0 = (aabbMin.y - rayOrigin.y) / rayDir.y;
        f32 t1 = (aabbMax.y - rayOrigin.y) / rayDir.y;
        if (t0 > t1) std::swap(t0, t1);
        t_enter = std::max(t_enter, t0);
        t_exit = std::min(t_exit, t1);
    } else {
        if (rayOrigin.y < aabbMin.y || rayOrigin.y > aabbMax.y) {
            return -1.0f;
        }
    }
    
    // Test Z slab
    if (std::abs(rayDir.z) > 1e-6f) {
        f32 t0 = (aabbMin.z - rayOrigin.z) / rayDir.z;
        f32 t1 = (aabbMax.z - rayOrigin.z) / rayDir.z;
        if (t0 > t1) std::swap(t0, t1);
        t_enter = std::max(t_enter, t0);
        t_exit = std::min(t_exit, t1);
    } else {
        if (rayOrigin.z < aabbMin.z || rayOrigin.z > aabbMax.z) {
            return -1.0f;
        }
    }
    
    if (t_enter <= t_exit && t_enter >= 0.0f) {
        return t_enter;
    }
    
    return -1.0f;
}

ECS::Entity SceneViewport::raycastSelectEntity(const Vec3& rayOrigin, const Vec3& rayDir,
                                               ECS::World& world,
                                               const std::string& projectRoot) {
    ECS::Entity closestEntity = ECS::Entity::INVALID;
    f32 closestT = 1e10f;
    
    ECS::ComponentQuery query;
    query.with<ECS::Transform>();
    
    world.forEach<ECS::Transform>(query, [&](ECS::Entity entity, ECS::Transform& transform) {
        if (Scene::isEffectivelyDisabled(world, entity)) return;
        
        Vec3 aabbMin = transform.position;
        Vec3 aabbMax = transform.position;
        
        if (auto* meshFilter = world.get<ECS::MeshFilterComponent>(entity)) {
            if (!meshFilter->customMeshPath.empty()) {
                auto* mesh = Assets::MeshCache::getInstance().getMesh(
                    meshFilter->customMeshPath, projectRoot);
                
                if (mesh && !mesh->vertices.empty()) {
                    Vec3 meshMin = mesh->bounds.min;
                    Vec3 meshMax = mesh->bounds.max;
                    
                    aabbMin = transform.position + Vec3(meshMin.x * transform.scale.x, 
                                                         meshMin.y * transform.scale.y, 
                                                         meshMin.z * transform.scale.z);
                    aabbMax = transform.position + Vec3(meshMax.x * transform.scale.x, 
                                                         meshMax.y * transform.scale.y, 
                                                         meshMax.z * transform.scale.z);
                    
                    if (aabbMin.x > aabbMax.x) std::swap(aabbMin.x, aabbMax.x);
                    if (aabbMin.y > aabbMax.y) std::swap(aabbMin.y, aabbMax.y);
                    if (aabbMin.z > aabbMax.z) std::swap(aabbMin.z, aabbMax.z);
                } else {
                    if (aabbMin.x >= aabbMax.x || aabbMin.y >= aabbMax.y || aabbMin.z >= aabbMax.z) {
                        Vec3 toEntity = transform.position - rayOrigin;
                        Vec3 proj = rayDir * toEntity.dot(rayDir);
                        f32 distToRay = (toEntity - proj).length();
                        
                        if (distToRay < 0.1f) {
                            f32 t = proj.length();
                            if (t >= 0.0f && t < closestT) {
                                closestT = t;
                                closestEntity = entity;
                            }
                        }
                        return;
                    }
                }
            }
        }
        
        f32 t = rayIntersectsAABB(rayOrigin, rayDir, aabbMin, aabbMax);
        
        if (t >= 0.0f && t < closestT) {
            closestT = t;
            closestEntity = entity;
        }
    });
    
    return closestEntity;
}

void SceneViewport::handleGizmoInput(ECS::World& world, EditorContext& ctx, ImVec2 viewportSize) {
    if (!ctx.selectedEntity.isValid()) return;
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) return;

    ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);

    int keyAxis = 0;
    if      (ImGui::IsKeyDown(ImGuiKey_X)) keyAxis = 1;
    else if (ImGui::IsKeyDown(ImGuiKey_Y)) keyAxis = 2;
    else if (ImGui::IsKeyDown(ImGuiKey_Z)) keyAxis = 3;

    int axis = keyAxis ? keyAxis : m_hoveredAxis;
    m_gizmoDragAxis = axis;

    auto* pos = world.get<ECS::Transform>(ctx.selectedEntity);
    if (!pos) {
        ECS::Transform initial;
        if (auto* p3 = world.get<ECS::Position3D>(ctx.selectedEntity)) initial.position = p3->position;
        if (auto* s3 = world.get<ECS::Scale3D>(ctx.selectedEntity))    initial.scale = s3->scale;
        if (auto* r3 = world.get<ECS::Rotation3D>(ctx.selectedEntity)) {
            const Vec3 eulerRad = Quat(r3->quaternion.x, r3->quaternion.y, r3->quaternion.z, r3->quaternion.w)
                                      .normalized()
                                      .toEuler();
            initial.rotation = Vec3(eulerRad.x * kRadToDeg, eulerRad.y * kRadToDeg, eulerRad.z * kRadToDeg);
        }
        pos = &world.add<ECS::Transform>(ctx.selectedEntity, initial);
    }

    const float scale = ctx.viewportZoom * 50.0f;

    auto projectDelta = [&](int axIdx) -> float {
        ImVec2 raw = m_axisRawDirs[axIdx - 1];
        float mag2 = raw.x*raw.x + raw.y*raw.y;
        if (mag2 < 0.0001f) return 0.f;
        return (delta.x*raw.x + delta.y*raw.y) / mag2 / scale;
    };

    switch (ctx.gizmoMode) {
        case EditorContext::GizmoMode::Translate:
            if (axis == 1)                           pos->position.x += projectDelta(1);
            else if (axis == 2)                      pos->position.y += projectDelta(2);
            else if (axis == 3)                      pos->position.z += projectDelta(3);
            else {
                pos->position.x += delta.x / scale;
                pos->position.y -= delta.y / scale;
            }
            if (ctx.snapToGrid && ctx.snapGridSize > 0.f) {
                pos->position.x = roundf(pos->position.x / ctx.snapGridSize) * ctx.snapGridSize;
                pos->position.y = roundf(pos->position.y / ctx.snapGridSize) * ctx.snapGridSize;
            }
            break;
        case EditorContext::GizmoMode::Rotate:
            if      (axis == 1) pos->rotation.x += delta.y * 0.01f;
            else if (axis == 2) pos->rotation.y += delta.x * 0.01f;
            else                pos->rotation.z += delta.x * 0.01f;
            break;
        case EditorContext::GizmoMode::Scale:
            if (axis == 1) { pos->scale.x = std::max(0.01f, pos->scale.x * (1.f + projectDelta(1) * 0.5f)); }
            else if (axis == 2) { pos->scale.y = std::max(0.01f, pos->scale.y * (1.f + projectDelta(2) * 0.5f)); }
            else if (axis == 3) { pos->scale.z = std::max(0.01f, pos->scale.z * (1.f + projectDelta(3) * 0.5f)); }
            else {
                pos->scale.x = std::max(0.01f, pos->scale.x * (1.f + delta.x * 0.005f));
                pos->scale.y = std::max(0.01f, pos->scale.y * (1.f + delta.y * 0.005f));
            }
            break;
        case EditorContext::GizmoMode::None: break;
    }

    if (auto* p3 = world.get<ECS::Position3D>(ctx.selectedEntity)) {
        p3->position = pos->position;
    }
    if (auto* s3 = world.get<ECS::Scale3D>(ctx.selectedEntity)) {
        s3->scale = pos->scale;
    }
    if (auto* r3 = world.get<ECS::Rotation3D>(ctx.selectedEntity)) {
        const Quat q = Quat::fromEuler(pos->rotation.x * kDegToRad,
                                       pos->rotation.y * kDegToRad,
                                       pos->rotation.z * kDegToRad).normalized();
        r3->quaternion = Vec4(q.x, q.y, q.z, q.w);
    }

    ctx.isDirty = true;
}

MeshDrawTexture SceneViewport::resolveTextureFromPath(const std::string& path,
                                                      const std::string& projectRoot) {
    MeshDrawTexture result{};
    if (path.empty()) return result;

    std::vector<std::string> fileCandidates;
    auto addResolved = [&](const std::string& candidatePath) {
        if (candidatePath.empty()) return;
        const std::string resolved =
            Assets::MeshCache::resolveTexturePath(candidatePath, projectRoot);
        if (!resolved.empty() &&
            std::find(fileCandidates.begin(), fileCandidates.end(), resolved) == fileCandidates.end()) {
            fileCandidates.push_back(resolved);
        }
    };

    addResolved(path);
    if (EditorPaths::isReady()) {
        addResolved(EditorPaths::resolve(path).string());
    }

    for (const std::string& candidate : fileCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec) || ec) continue;

        std::string cacheKey = candidate;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, ec);
        if (!ec) cacheKey = canonical.string();

        auto& entry = m_fileTextureCache[cacheKey];
        if (!entry.loaded) {
            int width = 0;
            int height = 0;
            int channels = 0;
            u8* data = stbi_load(cacheKey.c_str(), &width, &height, &channels, 0);
            if (!data) continue;
            entry.pixels.assign(data, data + static_cast<size_t>(width * height * channels));
            entry.width = static_cast<u32>(width);
            entry.height = static_cast<u32>(height);
            entry.channels = channels;
            entry.loaded = true;
            stbi_image_free(data);
        }

        if (entry.loaded) {
            result.pixels = entry.pixels.data();
            result.width = entry.width;
            result.height = entry.height;
            result.channels = entry.channels;
            result.flipV = false;
            return result;
        }
    }

    return result;
}

TerrainSplatDrawContext SceneViewport::resolveTerrainSplatContext(ECS::World& world,
                                                                  ECS::Entity entity,
                                                                  const Mat4& worldMatrix,
                                                                  const std::string& projectRoot) {
    TerrainSplatDrawContext context;
    auto* terrain = world.get<ECS::TerrainComponent>(entity);
    if (!terrain || !terrain->useSplatmap) return context;

    context.splatmap = Terrain::TerrainCache::instance().splatmapFor(entity);
    context.settings = terrain;
    context.worldMatrixInverse = worldMatrix.inverted();
    context.layerCount = ECS::kTerrainSplatLayerCount;
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        context.layers[i] = resolveTextureFromPath(terrain->splatLayerPaths[i], projectRoot);
    }
    context.active = context.splatmap != nullptr;
    return context;
}

void SceneViewport::drawTerrainChunkDebug(ImDrawList* dl, const Mat4& worldMatrix,
                                          const std::vector<Terrain::TerrainDrawChunk>& chunks,
                                          ImVec2 origin, ImVec2 viewportSize,
                                          const EditorContext& ctx) {
    const Mat4 vp = (ctx.viewMode == EditorContext::ViewMode::Mode3D)
        ? computeVP3D(viewportSize, ctx) : Mat4::identity();

    for (const Terrain::TerrainDrawChunk& chunk : chunks) {
        const Vec3 corners[8] = {
            {chunk.localBoundsMin.x, chunk.localBoundsMin.y, chunk.localBoundsMin.z},
            {chunk.localBoundsMax.x, chunk.localBoundsMin.y, chunk.localBoundsMin.z},
            {chunk.localBoundsMin.x, chunk.localBoundsMax.y, chunk.localBoundsMin.z},
            {chunk.localBoundsMax.x, chunk.localBoundsMax.y, chunk.localBoundsMin.z},
            {chunk.localBoundsMin.x, chunk.localBoundsMin.y, chunk.localBoundsMax.z},
            {chunk.localBoundsMax.x, chunk.localBoundsMin.y, chunk.localBoundsMax.z},
            {chunk.localBoundsMin.x, chunk.localBoundsMax.y, chunk.localBoundsMax.z},
            {chunk.localBoundsMax.x, chunk.localBoundsMax.y, chunk.localBoundsMax.z},
        };
        ImVec2 screen[8];
        for (int i = 0; i < 8; ++i) {
            screen[i] = projectVP(vp, origin, viewportSize, worldMatrix.transformPoint(corners[i]));
        }

        const ImU32 col = IM_COL32(120, 220, 120, 180);
        const int edges[][2] = {
            {0,1}, {1,3}, {3,2}, {2,0},
            {4,5}, {5,7}, {7,6}, {6,4},
            {0,4}, {1,5}, {3,7}, {2,6}
        };
        for (const auto& edge : edges) {
            if (screen[edge[0]].x > -5000.0f && screen[edge[1]].x > -5000.0f) {
                dl->AddLine(screen[edge[0]], screen[edge[1]], col, 1.5f);
            }
        }
    }
}

MeshDrawTexture SceneViewport::resolveDrawTexture(
    const Assets::Mesh3D* mesh,
    const ECS::MeshFilterComponent* filter,
    const std::string& resolvedMeshPath,
    const std::string& projectRoot) {
    if (filter && !filter->customTexturePath.empty()) {
        MeshDrawTexture result = resolveTextureFromPath(filter->customTexturePath, projectRoot);
        if (result.pixels) return result;
    }

    MeshDrawTexture result = resolveTextureFromPath(
        std::filesystem::path(resolvedMeshPath).replace_extension(".png").string(), projectRoot);
    if (result.pixels) return result;

    for (const std::string& uri :
         Assets::MeshImportValidator::listGltfExternalUris(resolvedMeshPath)) {
        result = resolveTextureFromPath(
            (std::filesystem::path(resolvedMeshPath).parent_path() / uri).string(), projectRoot);
        if (result.pixels) return result;
    }

    if (mesh && mesh->textureWidth > 0 && !mesh->baseColorTexture.empty()) {
        result.pixels = mesh->baseColorTexture.data();
        result.width = mesh->textureWidth;
        result.height = mesh->textureHeight;
        result.channels = mesh->textureChannels > 0 ? mesh->textureChannels : 4;
        result.flipV = mesh->flipTextureV;
    }

    return result;
}

void SceneViewport::drawCustomMeshGeometry(
    ECS::World& world,
    EditorContext& ctx,
    ECS::Entity entity,
    ECS::MeshFilterComponent* meshFilter,
    const Mat4& worldMatrix,
    ImDrawList* dl,
    const Mat4& vpMat,
    const Vec3& camPos,
    ImVec2 origin,
    ImVec2 panelSize,
    const std::function<Vec3(const Vec3&, const Vec3&, bool)>& lightColorAt,
    bool drawTextured,
    bool wireMode,
    bool receiveShadows,
    MeshCpuRasterizer* rasterizer,
    Assets::Mesh3D* meshOverride,
    const TerrainSplatDrawContext* splatContext) {
    if (!meshFilter) return;
    if (meshFilter->customMeshPath.empty() && !world.has<ECS::TerrainComponent>(entity)) return;

    const std::string projectRoot = resolveProjectRootFromScenePath(ctx.currentScenePath);

    auto& meshCache = Assets::MeshCache::getInstance();
    Assets::Mesh3D* loadedMesh = meshOverride;
    if (!loadedMesh) {
        loadedMesh = Terrain::TerrainCache::instance().meshFor(entity);
    }
    if (!loadedMesh && !meshFilter->customMeshPath.empty()) {
        loadedMesh = meshCache.getMesh(meshFilter->customMeshPath, projectRoot);
    }
    const std::string& meshPath = meshCache.getResolvedPath().empty()
        ? meshFilter->customMeshPath : meshCache.getResolvedPath();

    if (!loadedMesh || loadedMesh->vertices.empty() || loadedMesh->indices.empty()) {
        return;
    }

    MeshDrawTexture drawTex = resolveDrawTexture(loadedMesh, meshFilter, meshPath, projectRoot);
    if (!meshFilter->customMaterialPath.empty()) {
        const Assets::MaterialSurface surface =
            Assets::MaterialCache::instance().resolve(meshFilter->customMaterialPath, projectRoot);
        if (surface.valid) {
            drawTex.hasMaterial = true;
            drawTex.materialAlbedo = surface.albedo;
            drawTex.materialMetallic = surface.metallic;
            drawTex.materialRoughness = surface.roughness;
        }
    }

    auto transformNormal = [&](const Vec3& n) -> Vec3 {
        Vec3 wn = worldMatrix.transformVector(n);
        const f32 lenSq = wn.lengthSquared();
        return lenSq > 1e-8f ? wn / std::sqrt(lenSq) : Vec3(0.0f, 1.0f, 0.0f);
    };

    struct WireEdge { ImVec2 a, b; };
    std::vector<WireEdge> wireEdges;
    if (wireMode) {
        wireEdges.reserve(loadedMesh->indices.size());
    }

    const f32 rasterW = rasterizer ? static_cast<f32>(rasterizer->width) : panelSize.x;
    const f32 rasterH = rasterizer ? static_cast<f32>(rasterizer->height) : panelSize.y;

    auto buildRasterVert = [&](const Vec3& worldPos, const Vec3& worldNormal, const Vec2& uv) -> RasterVert {
        RasterVert rv;
        const Vec4 clip = vpMat.transformVec4(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));
        if (clip.w <= 0.1f) {
            rv.sx = -10000.0f;
            rv.sy = -10000.0f;
            return rv;
        }
        const f32 invW = 1.0f / clip.w;
        const f32 ndcX = clip.x * invW;
        const f32 ndcY = clip.y * invW;
        rv.sx = (ndcX + 1.0f) * 0.5f * rasterW;
        rv.sy = (1.0f - ndcY) * 0.5f * rasterH;
        rv.ndcZ = clip.z * invW;
        rv.invW = invW;
        rv.worldPos = worldPos;
        rv.worldNormal = worldNormal;
        rv.uv = uv;
        return rv;
    };

    for (size_t i = 0; i + 2 < loadedMesh->indices.size(); i += 3) {
        const u32 i0 = loadedMesh->indices[i];
        const u32 i1 = loadedMesh->indices[i + 1];
        const u32 i2 = loadedMesh->indices[i + 2];

        if (i0 >= loadedMesh->vertices.size() ||
            i1 >= loadedMesh->vertices.size() ||
            i2 >= loadedMesh->vertices.size()) continue;

        const Vec3 p0 = worldMatrix.transformPoint(loadedMesh->vertices[i0].position);
        const Vec3 p1 = worldMatrix.transformPoint(loadedMesh->vertices[i1].position);
        const Vec3 p2 = worldMatrix.transformPoint(loadedMesh->vertices[i2].position);

        const Vec3 edge1 = p1 - p0;
        const Vec3 edge2 = p2 - p0;
        Vec3 faceNormal = edge1.cross(edge2);
        const f32 normalLenSq = faceNormal.lengthSquared();
        if (normalLenSq < 1e-8f) continue;
        faceNormal = faceNormal / std::sqrt(normalLenSq);

        const ImVec2 sp0 = projectVP(vpMat, origin, panelSize, p0);
        const ImVec2 sp1 = projectVP(vpMat, origin, panelSize, p1);
        const ImVec2 sp2 = projectVP(vpMat, origin, panelSize, p2);
        if (sp0.x < -5000.0f && sp1.x < -5000.0f && sp2.x < -5000.0f) continue;

        if (drawTextured && rasterizer) {
            const RasterVert rv0 = buildRasterVert(p0, transformNormal(loadedMesh->vertices[i0].normal),
                                                   loadedMesh->vertices[i0].texcoord);
            const RasterVert rv1 = buildRasterVert(p1, transformNormal(loadedMesh->vertices[i1].normal),
                                                   loadedMesh->vertices[i1].texcoord);
            const RasterVert rv2 = buildRasterVert(p2, transformNormal(loadedMesh->vertices[i2].normal),
                                                   loadedMesh->vertices[i2].texcoord);
            if (rv0.sx < -5000.0f || rv1.sx < -5000.0f || rv2.sx < -5000.0f) continue;
            rasterizeTriangleCpu(*rasterizer, rv0, rv1, rv2, faceNormal, camPos, drawTex, receiveShadows,
                                 lightColorAt, splatContext);
        }

        if (wireMode) {
            wireEdges.push_back({sp0, sp1});
            wireEdges.push_back({sp1, sp2});
            wireEdges.push_back({sp2, sp0});
        }
    }

    if (!wireEdges.empty()) {
        const ImU32 edgeCol = IM_COL32(170, 190, 230, 210);
        for (const WireEdge& edge : wireEdges) {
            dl->AddLine(edge.a, edge.b, edgeCol, 1.25f);
        }
    }
}

void SceneViewport::drawSceneMeshesForCamera(
    ECS::World& world,
    EditorContext& ctx,
    ImDrawList* dl,
    const Mat4& vp,
    const Vec3& camPos,
    ImVec2 origin,
    ImVec2 panelSize,
    ECS::Entity skipEntity,
    int maxRasterDim) {
    // Viewport already syncs terrain in 3D mode. Avoid rebuilding meshes mid-frame
    // while GPU commands from the viewport pass may still reference chunk buffers.
    if (ctx.viewMode != EditorContext::ViewMode::Mode3D) {
        Scene::syncTerrainMeshes(world);
    }

    std::string projectRoot;
    if (!ctx.currentScenePath.empty()) {
        projectRoot = resolveProjectRootFromScenePath(ctx.currentScenePath);
    }
    Scene::SceneLighting sceneLighting;
    Scene::gatherSceneLighting(world, camPos, projectRoot, sceneLighting, skipEntity);

    auto lightColorAt = [&](const Vec3& p, const Vec3& n, bool receiveShadows) -> Vec3 {
        return Scene::evaluateDiffuseLighting(sceneLighting.lights, sceneLighting.shadows, p, n,
                                              receiveShadows, Vec3(0.22f, 0.22f, 0.24f));
    };

    MeshCpuRasterizer meshRaster;
    meshRaster.begin(origin, panelSize, maxRasterDim);
    meshRaster.clear(0, 0, 0, 0);

    const Spatial::Frustum frustum = Spatial::Frustum::fromMatrix(vp);
    (void)frustum;

    ECS::ComponentQuery meshQ;
    meshQ.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(meshQ,
        [&](ECS::Entity entity, ECS::MeshFilterComponent& meshFilter) {
            if (entity == skipEntity) return;
            if (Scene::isEffectivelyDisabled(world, entity)) return;

            const Mat4 worldMatrix = entityMatrix(world, entity);
            const bool isTerrain = world.has<ECS::TerrainComponent>(entity);
            const bool receiveShadows = Scene::meshReceivesShadows(world, entity);

            if (isTerrain) {
                // Camera Preview must not CPU-rasterize terrain chunks. A 257²
                // heightfield is hundreds of thousands of triangles per frame.
                return;
            }

            if (meshFilter.primitive == ECS::MeshPrimitive::Custom) {
                if (meshFilter.customMeshPath.empty()) return;
                drawCustomMeshGeometry(world, ctx, entity, &meshFilter, worldMatrix, dl, vp,
                                       camPos, origin, panelSize, lightColorAt, true, false,
                                       receiveShadows, &meshRaster);
                return;
            }

            Assets::Mesh3D* proceduralMesh = Render::GpuProceduralMeshes::get(meshFilter.primitive);
            if (!proceduralMesh || proceduralMesh->vertices.empty()) return;
            drawCustomMeshGeometry(world, ctx, entity, &meshFilter, worldMatrix, dl, vp, camPos,
                                   origin, panelSize, lightColorAt, true, false, receiveShadows,
                                   &meshRaster, proceduralMesh);
        });

    blitMeshRasterizer(dl, meshRaster, m_camMeshRasterTexture);
}

std::string SceneViewport::resolveSpritePath(const std::string& spriteName, const EditorContext& ctx) const {
    if (spriteName.empty()) {
        return "";
    }

    // 1. Try as-is (absolute path or relative to cwd)
    {
        std::filesystem::path path(spriteName);
        if (std::filesystem::exists(path)) {
            return spriteName;
        }
    }

    // 2. Derive project root from current scene path (if available)
    std::filesystem::path projectRoot;
    if (!ctx.currentScenePath.empty()) {
        projectRoot = std::filesystem::path(ctx.currentScenePath).parent_path();
    } else {
        projectRoot = std::filesystem::current_path();
    }

    // Only the filename (without directory)
    std::string filename = std::filesystem::path(spriteName).filename().string();

    // 3. Search in common asset directories relative to project root
    std::vector<std::filesystem::path> searchDirs = {
        projectRoot,
        projectRoot / "assets",
        projectRoot / "assets" / "raw",
        projectRoot / "assets" / "processed",
        projectRoot / "assets" / "textures",
    };

    for (const auto& dir : searchDirs) {
        // Try the full relative path
        auto candidate = dir / spriteName;
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
        // Try just the filename
        candidate = dir / filename;
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }

    // 4. Return as-is and let stbi_load report the error
    return spriteName;
}

void SceneViewport::releaseSpriteTextures() {
    auto destroyEntry = [this](SpriteTextureCacheEntry& entry) {
        retireImTexture(entry.texture);
        entry.width = 0;
        entry.height = 0;
        entry.loadFailed = false;
    };

    for (auto& pair : m_spriteTextureCache) {
        destroyEntry(pair.second);
    }
    m_spriteTextureCache.clear();
    destroyEntry(m_meshRasterTexture);
    destroyEntry(m_camMeshRasterTexture);
    m_skyboxRenderer.releaseGpuTextures();
    pruneRetiredTextures();
}

void SceneViewport::drawCameraFrustums(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto w2s = [&](f32 wx, f32 wy) -> ImVec2 {
        return projectToScreen({wx, wy, 0.0f}, origin, viewportSize, ctx);
    };

    ECS::ComponentQuery q2d;
    q2d.with<ECS::Camera2DComponent>();
    q2d.with<ECS::Transform>();

    world.forEach<ECS::Camera2DComponent, ECS::Transform>(q2d,
        [&](ECS::Entity, ECS::Camera2DComponent& cam, ECS::Transform& pos) {
        const f32 halfW = 8.0f / cam.zoom;
        const f32 halfH = 4.5f / cam.zoom;

        ImVec2 tl = w2s(pos.position.x - halfW, pos.position.y + halfH);
        ImVec2 tr = w2s(pos.position.x + halfW, pos.position.y + halfH);
        ImVec2 br = w2s(pos.position.x + halfW, pos.position.y - halfH);
        ImVec2 bl = w2s(pos.position.x - halfW, pos.position.y - halfH);

        const ImU32 col = IM_COL32(255, 220, 50, 220);
        dl->AddQuad(tl, tr, br, bl, col, 1.5f);

        const f32 cLen = 8.0f;
        dl->AddLine(tl, ImVec2(tl.x + cLen, tl.y), col, 1.5f);
        dl->AddLine(tl, ImVec2(tl.x, tl.y + cLen), col, 1.5f);
        dl->AddLine(tr, ImVec2(tr.x - cLen, tr.y), col, 1.5f);
        dl->AddLine(tr, ImVec2(tr.x, tr.y + cLen), col, 1.5f);
        dl->AddLine(br, ImVec2(br.x - cLen, br.y), col, 1.5f);
        dl->AddLine(br, ImVec2(br.x, br.y - cLen), col, 1.5f);
        dl->AddLine(bl, ImVec2(bl.x + cLen, bl.y), col, 1.5f);
        dl->AddLine(bl, ImVec2(bl.x, bl.y - cLen), col, 1.5f);

        dl->AddText(ImVec2(tl.x + 4.0f, tl.y - 16.0f), col, "Camera 2D");
    });

    if (ctx.viewMode != EditorContext::ViewMode::Mode3D &&
        ctx.viewMode != EditorContext::ViewMode::Isometric) {
        return;
    }

    const Mat4 vp = computeVP3D(viewportSize, ctx);
    auto projectFrustum = [&](const Vec3& p) -> ImVec2 {
        return projectToScreenVP(p, origin, viewportSize, vp);
    };
    auto drawFrustumLine = [&](const Vec3& a, const Vec3& b, ImU32 col, float thickness) {
        ImVec2 sa = projectFrustum(a);
        ImVec2 sb = projectFrustum(b);
        if (sa.x < -5000.0f && sb.x < -5000.0f) return;
        dl->AddLine(sa, sb, col, thickness);
    };

    ECS::ComponentQuery q3d;
    q3d.with<ECS::Camera3DComponent>();
    world.forEach<ECS::Camera3DComponent>(q3d,
        [&](ECS::Entity entity, ECS::Camera3DComponent& cam) {
        Vec3 camPos;
        if (!tryGetEntityPosition(world, entity, camPos)) return;

        const Mat4 worldMatrix = entityMatrix(world, entity);
        const Vec3 forward = entityForward(world, entity).normalized();
        const Vec3 right   = matrixAxis(worldMatrix, 0, Vec3(1.0f, 0.0f, 0.0f));
        const Vec3 up      = matrixAxis(worldMatrix, 1, Vec3(0.0f, 1.0f, 0.0f));

        const f32 fovRad = cam.fov * kDegToRad;
        const f32 aspect = std::max(0.1f, cam.aspectRatio);
        const f32 nearD  = std::max(0.01f, cam.nearClip);
        const f32 farD   = std::min(std::max(cam.farClip, nearD * 2.0f), nearD * 40.0f);

        const f32 nearHalfH = std::tan(fovRad * 0.5f) * nearD;
        const f32 nearHalfW = nearHalfH * aspect;
        const f32 farHalfH  = std::tan(fovRad * 0.5f) * farD;
        const f32 farHalfW  = farHalfH * aspect;

        const Vec3 nearCenter = camPos + forward * nearD;
        const Vec3 farCenter  = camPos + forward * farD;

        const Vec3 nearCorners[4] = {
            nearCenter + right * nearHalfW + up * nearHalfH,
            nearCenter - right * nearHalfW + up * nearHalfH,
            nearCenter - right * nearHalfW - up * nearHalfH,
            nearCenter + right * nearHalfW - up * nearHalfH,
        };
        const Vec3 farCorners[4] = {
            farCenter + right * farHalfW + up * farHalfH,
            farCenter - right * farHalfW + up * farHalfH,
            farCenter - right * farHalfW - up * farHalfH,
            farCenter + right * farHalfW - up * farHalfH,
        };

        const ImU32 col = IM_COL32(255, 220, 50, 220);
        for (int i = 0; i < 4; ++i) {
            const int next = (i + 1) % 4;
            drawFrustumLine(nearCorners[i], nearCorners[next], col, 1.5f);
            drawFrustumLine(farCorners[i], farCorners[next], col, 1.5f);
            drawFrustumLine(nearCorners[i], farCorners[i], col, 1.5f);
        }
        drawFrustumLine(camPos, nearCenter, col, 1.5f);

        ImVec2 labelPos = projectFrustum(nearCenter);
        if (labelPos.x > -5000.0f) {
            dl->AddText(ImVec2(labelPos.x + 4.0f, labelPos.y - 16.0f), col, "Camera 3D");
        }
    });
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI

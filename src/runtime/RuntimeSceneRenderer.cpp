#include "runtime/RuntimeSceneRenderer.hpp"

#ifdef CF_HAS_IMGUI

#include "assets/MeshCache.hpp"
#include "assets/MeshImportValidator.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"

#include <stb/stb_image.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <vector>

namespace Caffeine::Runtime {

namespace {

constexpr f32 kDegToRad = 3.14159265f / 180.0f;

struct MeshDrawTexture {
    const u8* pixels = nullptr;
    u32 width = 0;
    u32 height = 0;
    int channels = 0;
    bool flipV = false;
};

struct RasterVert {
    f32 sx = 0.0f;
    f32 sy = 0.0f;
    f32 ndcZ = 0.0f;
    f32 invW = 0.0f;
    Vec3 worldPos;
    Vec3 worldNormal;
    Vec2 uv;
};

struct MeshCpuRasterizer {
    int width = 0;
    int height = 0;
    std::vector<u8> color;
    std::vector<f32> depth;

    void begin(ImVec2 size) {
        width = std::max(1, static_cast<int>(size.x));
        height = std::max(1, static_cast<int>(size.y));
        constexpr int kMax = 1080;
        if (width > kMax || height > kMax) {
            const f32 scale = static_cast<f32>(kMax) / static_cast<f32>(std::max(width, height));
            width = std::max(1, static_cast<int>(static_cast<f32>(width) * scale));
            height = std::max(1, static_cast<int>(static_cast<f32>(height) * scale));
        }
        color.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        depth.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    }

    void clear(u8 r, u8 g, u8 b, u8 a = 0) {
        for (size_t i = 0; i < color.size(); i += 4) {
            color[i + 0] = r;
            color[i + 1] = g;
            color[i + 2] = b;
            color[i + 3] = a;
        }
        std::fill(depth.begin(), depth.end(), 1.1f);
    }
};

Mat4 buildLocalMatrix3D(const ECS::Position3D* p, const ECS::Rotation3D* r, const ECS::Scale3D* s) {
    Mat4 T = p ? Mat4::translation(p->position) : Mat4::identity();
    Mat4 R = r ? Quat(r->quaternion.x, r->quaternion.y, r->quaternion.z, r->quaternion.w).normalized().toMatrix()
               : Mat4::identity();
    Mat4 S = s ? Mat4::scale(s->scale.x, s->scale.y, s->scale.z) : Mat4::identity();
    return T * R * S;
}

Mat4 entityMatrix(ECS::World& world, ECS::Entity entity) {
    if (auto* wt = world.get<Scene::WorldTransform>(entity)) return wt->matrix;
    if (auto* t = world.get<ECS::Transform>(entity)) {
        return Mat4::translation(t->position) * Mat4::rotationZ(t->rotation.z * kDegToRad) *
               Mat4::rotationY(t->rotation.y * kDegToRad) * Mat4::rotationX(t->rotation.x * kDegToRad) *
               Mat4::scale(t->scale.x, t->scale.y, t->scale.z);
    }
    auto* p3 = world.get<ECS::Position3D>(entity);
    auto* r3 = world.get<ECS::Rotation3D>(entity);
    auto* s3 = world.get<ECS::Scale3D>(entity);
    return buildLocalMatrix3D(p3, r3, s3);
}

Vec3 matrixAxis(const Mat4& m, int column, const Vec3& fallback) {
    Vec3 axis(m(0, column), m(1, column), m(2, column));
    const f32 lenSq = axis.lengthSquared();
    return (lenSq > 0.000001f) ? axis / std::sqrt(lenSq) : fallback;
}

Vec3 entityForward(ECS::World& world, ECS::Entity entity) {
    return -1.0f * matrixAxis(entityMatrix(world, entity), 2, Vec3(0.0f, 0.0f, -1.0f));
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

f32 edgeFunction(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy) {
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

ImU32 sampleDrawTexture(const MeshDrawTexture& tex, const Vec2& uv, const Vec3& lightRgb) {
    if (!tex.pixels || tex.width == 0 || tex.height == 0) {
        const f32 a = 0.35f;
        return IM_COL32(static_cast<ImU8>(100.0f * a + lightRgb.x * 255.0f * (1.0f - a)),
                        static_cast<ImU8>(150.0f * a + lightRgb.y * 255.0f * (1.0f - a)),
                        static_cast<ImU8>(200.0f * a + lightRgb.z * 255.0f * (1.0f - a)), 255);
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
    if (idx + static_cast<u32>(channels - 1) >= pixelBytes) return IM_COL32(100, 150, 200, 255);

    const f32 r = static_cast<f32>(tex.pixels[idx]) / 255.0f;
    const f32 g = static_cast<f32>(tex.pixels[idx + 1]) / 255.0f;
    const f32 b = static_cast<f32>(tex.pixels[idx + 2]) / 255.0f;
    const f32 ambient = 0.5f;
    const f32 lit = 0.5f;
    return IM_COL32(static_cast<ImU8>(std::clamp((r * ambient + r * lightRgb.x * lit) * 255.0f, 0.0f, 255.0f)),
                  static_cast<ImU8>(std::clamp((g * ambient + g * lightRgb.y * lit) * 255.0f, 0.0f, 255.0f)),
                  static_cast<ImU8>(std::clamp((b * ambient + b * lightRgb.z * lit) * 255.0f, 0.0f, 255.0f)),
                  255);
}

void rasterizeTriangleCpu(MeshCpuRasterizer& fb, const RasterVert& v0, const RasterVert& v1,
                          const RasterVert& v2, const Vec3& faceNormal, const Vec3& camPos,
                          const MeshDrawTexture& tex,
                          const std::function<Vec3(const Vec3&, const Vec3&)>& lightColorAt) {
    const Vec3 center = (v0.worldPos + v1.worldPos + v2.worldPos) * (1.0f / 3.0f);
    const Vec3 viewDelta(camPos.x - center.x, camPos.y - center.y, camPos.z - center.z);
    if (faceNormal.dot(viewDelta) <= 0.0f) return;

    const f32 area = edgeFunction(v0.sx, v0.sy, v1.sx, v1.sy, v2.sx, v2.sy);
    if (std::abs(area) < 1e-4f) return;

    const int minX = std::max(0, static_cast<int>(std::floor(std::min({v0.sx, v1.sx, v2.sx}))));
    const int maxX =
        std::min(fb.width - 1, static_cast<int>(std::ceil(std::max({v0.sx, v1.sx, v2.sx}))));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min({v0.sy, v1.sy, v2.sy}))));
    const int maxY =
        std::min(fb.height - 1, static_cast<int>(std::ceil(std::max({v0.sy, v1.sy, v2.sy}))));

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

            const f32 z =
                (w0 * v0.ndcZ * v0.invW + w1 * v1.ndcZ * v1.invW + w2 * v2.ndcZ * v2.invW) / invWInterp;
            const int depthIdx = y * fb.width + x;
            if (z >= fb.depth[depthIdx]) continue;

            const f32 u =
                (w0 * v0.uv.x * v0.invW + w1 * v1.uv.x * v1.invW + w2 * v2.uv.x * v2.invW) / invWInterp;
            const f32 v =
                (w0 * v0.uv.y * v0.invW + w1 * v1.uv.y * v1.invW + w2 * v2.uv.y * v2.invW) / invWInterp;

            Vec3 worldPos = v0.worldPos * w0 + v1.worldPos * w1 + v2.worldPos * w2;
            Vec3 worldNormal = v0.worldNormal * w0 + v1.worldNormal * w1 + v2.worldNormal * w2;
            const f32 normalLenSq = worldNormal.lengthSquared();
            if (normalLenSq > 1e-8f) {
                worldNormal = worldNormal / std::sqrt(normalLenSq);
            } else {
                worldNormal = faceNormal;
            }

            const ImU32 col = sampleDrawTexture(tex, Vec2(u, v), lightColorAt(worldPos, worldNormal));
            fb.depth[depthIdx] = z;
            const int colorIdx = depthIdx * 4;
            fb.color[colorIdx + 0] = static_cast<u8>((col >> IM_COL32_R_SHIFT) & 0xFF);
            fb.color[colorIdx + 1] = static_cast<u8>((col >> IM_COL32_G_SHIFT) & 0xFF);
            fb.color[colorIdx + 2] = static_cast<u8>((col >> IM_COL32_B_SHIFT) & 0xFF);
            fb.color[colorIdx + 3] = 255;
        }
    }
}

struct FileTextureEntry {
    std::vector<u8> pixels;
    u32 width = 0;
    u32 height = 0;
    int channels = 0;
    bool loaded = false;
};

MeshDrawTexture resolveDrawTexture(
    const Assets::Mesh3D* mesh, const ECS::MeshFilterComponent* filter,
    const std::string& resolvedMeshPath, const std::string& projectRoot,
    std::unordered_map<std::string, FileTextureEntry>& fileTextures) {
    MeshDrawTexture result{};
    std::vector<std::string> fileCandidates;

    auto addFileCandidates = [&](const std::filesystem::path& path) {
        if (path.empty()) return;
        for (const std::string& candidate :
             Assets::MeshCache::buildCandidatePaths(path.string(), projectRoot)) {
            if (std::find(fileCandidates.begin(), fileCandidates.end(), candidate) ==
                fileCandidates.end()) {
                fileCandidates.push_back(candidate);
            }
        }
    };

    if (filter && !filter->customTexturePath.empty()) {
        addFileCandidates(filter->customTexturePath);
    } else {
        addFileCandidates(std::filesystem::path(resolvedMeshPath).replace_extension(".png"));
        for (const std::string& uri :
             Assets::MeshImportValidator::listGltfExternalUris(resolvedMeshPath)) {
            addFileCandidates(std::filesystem::path(resolvedMeshPath).parent_path() / uri);
        }
    }

    for (const std::string& candidate : fileCandidates) {
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec) || ec) continue;

        std::string cacheKey = candidate;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, ec);
        if (!ec) cacheKey = canonical.string();

        auto& entry = fileTextures[cacheKey];
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

    if (mesh && mesh->textureWidth > 0 && !mesh->baseColorTexture.empty()) {
        result.pixels = mesh->baseColorTexture.data();
        result.width = mesh->textureWidth;
        result.height = mesh->textureHeight;
        result.channels = mesh->textureChannels > 0 ? mesh->textureChannels : 4;
        result.flipV = mesh->flipTextureV;
    }

    return result;
}

struct RasterTextureState {
    std::unique_ptr<ImTextureData> texture;
    int width = 0;
    int height = 0;
};

void blitRasterizer(ImDrawList* dl, ImVec2 origin, ImVec2 panelSize, MeshCpuRasterizer& rasterizer,
                    RasterTextureState& texEntry) {
    if (!dl || rasterizer.width <= 0 || rasterizer.height <= 0) return;

    const bool sizeChanged =
        !texEntry.texture || texEntry.width != rasterizer.width || texEntry.height != rasterizer.height;

    if (sizeChanged) {
        if (texEntry.texture && texEntry.texture->GetTexID() != ImTextureID_Invalid) {
            texEntry.texture->UnusedFrames = 1;
            texEntry.texture->SetStatus(ImTextureStatus_WantDestroy);
            ImGui_ImplSDLGPU3_UpdateTexture(texEntry.texture.get());
        }
        texEntry.width = rasterizer.width;
        texEntry.height = rasterizer.height;
        texEntry.texture = std::make_unique<ImTextureData>();
        texEntry.texture->Create(ImTextureFormat_RGBA32, rasterizer.width, rasterizer.height);
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

    const ImTextureRef texRef = texEntry.texture->GetTexRef();
    dl->AddImage(texRef, origin, ImVec2(origin.x + panelSize.x, origin.y + panelSize.y));
}

}  // namespace

void RuntimeSceneRenderer::render(ECS::World& world, Editor::EditorContext& ctx, ImDrawList* dl,
                                  const Mat4& vp, const Vec3& camPos, ImVec2 origin,
                                  ImVec2 panelSize, ECS::Entity skipEntity) {
    struct DirLightEval {
        Vec3 dir;
        f32 intensity;
        Vec3 color;
    };
    struct PointLightEval {
        Vec3 pos;
        f32 intensity;
        f32 radius;
        Vec3 color;
    };
    std::vector<DirLightEval> dirLights;
    std::vector<PointLightEval> pointLights;

    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::DirectionalLightComponent>();
        world.forEach<ECS::LightComponent, ECS::DirectionalLightComponent>(
            q, [&](ECS::Entity e, ECS::LightComponent& lc, ECS::DirectionalLightComponent&) {
                if (Scene::isEffectivelyDisabled(world, e)) return;
                dirLights.push_back(
                    {entityForward(world, e).normalized(), lc.intensity,
                     Vec3(lc.color.x, lc.color.y, lc.color.z)});
            });
    }
    {
        ECS::ComponentQuery q;
        q.with<ECS::LightComponent>();
        q.with<ECS::PointLightComponent>();
        world.forEach<ECS::LightComponent, ECS::PointLightComponent>(
            q, [&](ECS::Entity e, ECS::LightComponent& lc, ECS::PointLightComponent& pl) {
                if (Scene::isEffectivelyDisabled(world, e)) return;
                Vec3 p;
                if (!tryGetEntityPosition(world, e, p)) return;
                pointLights.push_back({p, lc.intensity, std::max(0.001f, pl.radius),
                                       Vec3(lc.color.x, lc.color.y, lc.color.z)});
            });
    }

    auto lightColorAt = [&](const Vec3& p, const Vec3& n) -> Vec3 {
        const Vec3 nn = n.normalized();
        Vec3 diffuse(0.22f, 0.22f, 0.24f);
        for (const auto& l : dirLights) {
            const f32 ndotl = std::max(0.0f, nn.dot(-1.0f * l.dir));
            diffuse += l.color * (ndotl * l.intensity * 0.65f);
        }
        for (const auto& l : pointLights) {
            Vec3 toLight = l.pos - p;
            const f32 dist = std::max(0.001f, toLight.length());
            if (dist > l.radius) continue;
            const Vec3 ldir = toLight / dist;
            const f32 atten = 1.0f - (dist / l.radius);
            const f32 ndotl = std::max(0.0f, nn.dot(ldir));
            diffuse += l.color * (ndotl * l.intensity * atten * atten * 0.9f);
        }
        diffuse.x = std::clamp(diffuse.x, 0.08f, 1.65f);
        diffuse.y = std::clamp(diffuse.y, 0.08f, 1.65f);
        diffuse.z = std::clamp(diffuse.z, 0.08f, 1.65f);
        return diffuse;
    };

    std::string projectRoot = std::filesystem::current_path().string();
    {
        std::filesystem::path probe = std::filesystem::current_path();
        for (int i = 0; i < 5; ++i) {
            if (std::filesystem::exists(probe / "project.caffeine")) {
                projectRoot = probe.string();
                break;
            }
            if (!probe.has_parent_path() || probe.parent_path() == probe) break;
            probe = probe.parent_path();
        }
    }

    std::unordered_map<std::string, FileTextureEntry> fileTextures;

    MeshCpuRasterizer meshRaster;
    meshRaster.begin(panelSize);
    meshRaster.clear(0, 0, 0, 0);

    ECS::ComponentQuery meshQ;
    meshQ.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(
        meshQ, [&](ECS::Entity entity, ECS::MeshFilterComponent& meshFilter) {
            if (entity == skipEntity) return;
            if (Scene::isEffectivelyDisabled(world, entity)) return;
            if (meshFilter.primitive != ECS::MeshPrimitive::Custom) return;
            if (meshFilter.customMeshPath.empty()) return;

            const Mat4 worldMatrix = entityMatrix(world, entity);
            auto& meshCache = Assets::MeshCache::getInstance();
            auto* loadedMesh = meshCache.getMesh(meshFilter.customMeshPath, projectRoot);
            const std::string& meshPath = meshCache.getResolvedPath().empty()
                                               ? meshFilter.customMeshPath
                                               : meshCache.getResolvedPath();
            if (!loadedMesh || loadedMesh->vertices.empty() || loadedMesh->indices.empty()) return;

            const MeshDrawTexture drawTex =
                resolveDrawTexture(loadedMesh, &meshFilter, meshPath, projectRoot, fileTextures);

            auto transformNormal = [&](const Vec3& n) -> Vec3 {
                Vec3 wn = worldMatrix.transformVector(n);
                const f32 lenSq = wn.lengthSquared();
                return lenSq > 1e-8f ? wn / std::sqrt(lenSq) : Vec3(0.0f, 1.0f, 0.0f);
            };

            const f32 rasterW = static_cast<f32>(meshRaster.width);
            const f32 rasterH = static_cast<f32>(meshRaster.height);

            auto buildRasterVert = [&](const Vec3& worldPos, const Vec3& worldNormal,
                                       const Vec2& uv) -> RasterVert {
                RasterVert rv;
                const Vec4 clip = vp.transformVec4(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));
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
                if (i0 >= loadedMesh->vertices.size() || i1 >= loadedMesh->vertices.size() ||
                    i2 >= loadedMesh->vertices.size())
                    continue;

                const Vec3 p0 = worldMatrix.transformPoint(loadedMesh->vertices[i0].position);
                const Vec3 p1 = worldMatrix.transformPoint(loadedMesh->vertices[i1].position);
                const Vec3 p2 = worldMatrix.transformPoint(loadedMesh->vertices[i2].position);

                const Vec3 edge1 = p1 - p0;
                const Vec3 edge2 = p2 - p0;
                Vec3 faceNormal = edge1.cross(edge2);
                const f32 normalLenSq = faceNormal.lengthSquared();
                if (normalLenSq < 1e-8f) continue;
                faceNormal = faceNormal / std::sqrt(normalLenSq);

                const RasterVert rv0 =
                    buildRasterVert(p0, transformNormal(loadedMesh->vertices[i0].normal),
                                    loadedMesh->vertices[i0].texcoord);
                const RasterVert rv1 =
                    buildRasterVert(p1, transformNormal(loadedMesh->vertices[i1].normal),
                                    loadedMesh->vertices[i1].texcoord);
                const RasterVert rv2 =
                    buildRasterVert(p2, transformNormal(loadedMesh->vertices[i2].normal),
                                    loadedMesh->vertices[i2].texcoord);
                if (rv0.sx < -5000.0f || rv1.sx < -5000.0f || rv2.sx < -5000.0f) continue;

                rasterizeTriangleCpu(meshRaster, rv0, rv1, rv2, faceNormal, camPos, drawTex,
                                     lightColorAt);
            }
        });

    RasterTextureState texState;
    texState.texture = std::move(m_frameTexture.texture);
    texState.width = m_frameTexture.width;
    texState.height = m_frameTexture.height;
    blitRasterizer(dl, origin, panelSize, meshRaster, texState);
    m_frameTexture.texture = std::move(texState.texture);
    m_frameTexture.width = texState.width;
    m_frameTexture.height = texState.height;
}

}  // namespace Caffeine::Runtime

#endif

#include "render/GpuSceneRenderer.hpp"
#include "animation/SkinLibrary.hpp"
#include "caffeine/environment/VolumetricClouds.hpp"
#include "render/GpuProfile.hpp"
#include "render/ShadowCulling.hpp"
#include "caffeine/environment/NaturalFluids.hpp"
#include "caffeine/photon/Photon.hpp"
#include "effects/EffectSystem.hpp"
#include "render/PostProcessRenderer.hpp"
#include "render/CoarseOcclusion.hpp"
#include "render/InstanceBatch.hpp"
#include "render/TerrainVegetationGpu.hpp"
#include "micromesh/Micromesh.hpp"
#include "render/ForwardRenderFeatures.hpp"
#include "debug/Profiler.hpp"
#include "render/ShaderBytecode.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "scene/EditorContext.hpp"
#include "scene/EditorCameraMath.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/MeshGeometry.hpp"
#include "ecs/WorldInstancesComponents.hpp"
#include "world/WorldInstanceDraw.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"
#include "scene/EnvironmentSystem.hpp"
#include "scene/NaturalFluidSystem.hpp"
#include "scene/CpuDirectionalShadowMap.hpp"
#include "assets/MaterialCache.hpp"
#include "assets/MeshCache.hpp"
#include "assets/MeshLOD.hpp"
#include "assets/MeshSubdivide.hpp"
#include "assets/MeshImportValidator.hpp"
#include <filesystem>
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainCache.hpp"
#include "render/GpuTextureCache.hpp"
#include "terrain/TerrainGpuTextures.hpp"
#include "terrain/TerrainLodSystem.hpp"
#include "spatial/Octree.hpp"
#include "math/Quat.hpp"
#include "ecs/Components.hpp"
#include "debug/CrashHandler.hpp"

#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <cstring>

namespace Caffeine::Render {
namespace {

using StatsClock = std::chrono::steady_clock;

/// Adds the milliseconds of its scope to `out`.
struct ScopedMs {
    f32& out;
    StatsClock::time_point start = StatsClock::now();
    explicit ScopedMs(f32& target) : out(target) {}
    ~ScopedMs() {
        out += std::chrono::duration<f32, std::milli>(StatsClock::now() - start).count();
    }
};

constexpr u32 kVertexStride = sizeof(Assets::Vertex3D);

struct VertexUBO {
    float mvp[16];
    float model[16];
    float wind[4]; // strength per metre of height, speed, flutter, time (s); 0 strength = static
};

struct WaterVertexUBO {
    float mvp[16];
    float model[16];
    float params[4];     // time, wave count, foam threshold
    float waves[16 * 8]; // 16 waves x two vec4
};

struct DisplaceVertexUBO {
    float mvp[16];
    float model[16];
    float disp[4]; // depth (m), finite-difference step (tiled uv), metres per tiled uv, w = uv follows object scale
    float uv[4];   // tiling xy, offset zw
};

struct DisplaceShadowUBO {
    float mvp[16];
    float model[16];
    float lightPos[4];
    int mode;
    int pad[3];
    float disp[4]; // x depth (m), y = uv follows object scale
    float uv[4];
};

struct LightingUBO {
    float cameraPos[4];
    float ambient[4];
    float albedo[4];
    float metallic;
    float roughness;
    float reflectance; // std140 slot shared with legacy uShininess name in terrain_lit
    float padFlags; // std140: vec4 uFlags must start at offset 64
    float flags[4];
    int dirCount;
    int pointCount;
    int spotCount;
    int alignPad;
    float dirData[16];
    float dirColor[16];
    float pointData[16];
    float pointColor[16];
    float spotData[16];
    float spotDir[16];
    float spotColor[16];
    float spotAngle[16];
    float iblColor[4];
    float iblParams[4];
    float volParams[4];
    float reflectParams[4];
    float extra[4];
    float reflectVP[16];
    float emission[4];
    float uvTransform[4];
    float materialParams[4];
    float coat[4];
    float sheen[4];
    float iridescence[4];
    float materialFlags[4];
    float ssrParams[4];
    float projParams[4];
    float prevViewProj[16];
    float prevView[16];
    float screen[4];
    float viewProj[16];
    float reflectPerf[4];
    float reflectQuality[4];
    float reflectExtra[4];
    float foliage[4]; // x translucency
    float modifiers[4]; // x wetness, y snow, z age
    float modTint[4];   // rgb albedo tint
    float water[4];     // x water surface, y foam scale, z ripple detail, w time
    float cloudShadowA[4];  // x strength (0 = off), y coverage, z density
    float cloudShadowB[4];  // xyz wind offset of the cloud noise, w base altitude
    float cloudShadowC[4];  // xyz direction toward the sun, w top altitude
};

static_assert(offsetof(LightingUBO, iblColor) == 608, "LightingUBO.iblColor must match GLSL std140");
static_assert(offsetof(LightingUBO, reflectVP) == 688, "LightingUBO.reflectVP must match GLSL std140");
static_assert(offsetof(LightingUBO, emission) == 752, "LightingUBO.emission must match GLSL std140");
static_assert(offsetof(LightingUBO, prevViewProj) == 896, "LightingUBO.prevViewProj must match GLSL std140");
static_assert(offsetof(LightingUBO, viewProj) == 1040, "LightingUBO.viewProj must match GLSL std140");
static_assert(offsetof(LightingUBO, reflectPerf) == 1104, "LightingUBO.reflectPerf must match GLSL std140");

struct SkyUBO {
    float invViewProj[16];
    float params[4];
    float zenith[4];
    float horizon[4];
    float ground[4];
    float camera[4];
    float cloudA[4];
    float cloudB[4];
    float cloudC[4];
    float sunDir[4];
    float sunColor[4];
    float cloudD[4];
    float cloudE[4];
    float boltA[4];
    float boltB[4];
};

struct GridUBO {
    float invViewProj[16];
    float viewProj[16];
    float camera[4];
    float params[4];
    float minor[4];
    float major[4];
    float axisX[4];
    float axisZ[4];
};

constexpr RHI::TextureFormat kHdrFormat = RHI::TextureFormat::R16G16B16A16_FLOAT;
constexpr u32 kMaxViewTargets = 4;

f32 srgbToLinear(f32 c) {
    c = std::max(c, 0.0f);
    if (c > 1.0f) return std::pow(c, 2.2f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

Vec3 srgbToLinear(const Vec3& c) {
    return Vec3(srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z));
}

static_assert(offsetof(LightingUBO, flags) == 64, "LightingUBO.flags must match GLSL std140 vec4");
static_assert(offsetof(LightingUBO, dirCount) == 80, "LightingUBO.dirCount must match GLSL std140");
static_assert(offsetof(LightingUBO, dirData) == 96, "LightingUBO.dirData must match GLSL std140");

struct SceneShadowUBO {
    float dirShadowVP[128];
    float dirCascadeSplits[8];
    float dirShadowValid[4];
    float cameraView[16];
    float pointShadowPos[8];
    float pointShadowRadius[4];
    float pointShadowValid[4];
    float spotShadowVP[32];
    float spotShadowPos[8];
    float spotShadowParams[8];
    float spotShadowValid[4];
};

struct ShadowUBO {
    float mvp[16];
    float model[16];
    float lightPos[4];
    int mode;
    int pad[3];
    float wind[4]; // same wind as the colour pass, so shadows sway with the foliage
};

struct TerrainMaterialUBO {
    float worldSize[4];
    float flags[4];
    float modelInv[16];
    // Layered painting: metres per repeat and turn (radians) of each of up to 64 array slices (std140: four to a vec4).
    float layerTile[64];
    float layerRot[64];
};

Mat4 buildLocalMatrix(const ECS::Transform& t) {
    constexpr f32 kDegToRad = 3.14159265f / 180.0f;
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
    if (auto* wt = world.get<Scene::WorldTransform>(entity)) return wt->matrix;
    if (auto* t = world.get<ECS::Transform>(entity)) return buildLocalMatrix(*t);
    auto* p3 = world.get<ECS::Position3D>(entity);
    auto* r3 = world.get<ECS::Rotation3D>(entity);
    auto* s3 = world.get<ECS::Scale3D>(entity);
    return buildLocalMatrix3D(p3, r3, s3);
}

/// Changes whenever something a probe would see changes: placement, mesh or material.
template <typename Draws>
u64 hashProbeScene(const Draws& draws, const std::string& environmentPath) {
    u64 h = 1469598103934665603ull;
    auto mix = [&h](const void* data, size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
    };
    mix(environmentPath.data(), environmentPath.size());
    for (const auto& draw : draws) {
        const u32 id = draw.entity.id();
        mix(&id, sizeof(id));
        mix(&draw.mesh, sizeof(draw.mesh));
        mix(draw.worldMatrix.data(), sizeof(float) * 16);
        mix(&draw.albedo, sizeof(draw.albedo));
        mix(&draw.emission, sizeof(draw.emission));
        mix(&draw.metallic, sizeof(draw.metallic));
        mix(&draw.roughness, sizeof(draw.roughness));
        mix(&draw.reflectance, sizeof(draw.reflectance));
        mix(draw.customTexturePath.data(), draw.customTexturePath.size());
    }
    return h;
}

/// Mat4::ortho / perspective produce GL depth (-1..1) but SDL GPU clips z < 0, which would cut
/// away the half of the light volume nearest the light. Remap shadow projections to 0..1.
Mat4 shadowDepthRange(const Mat4& proj) {
    Mat4 remap = Mat4::identity();
    remap(2, 2) = 0.5f;
    remap(2, 3) = 0.5f;
    return remap * proj;
}

Mat4 buildDirectionalLightVP(const Vec3& lightDirection, const Vec3& focus, f32 shadowDistance) {
    Vec3 lightDir = lightDirection;
    const f32 len = lightDir.length();
    if (len < 1e-6f) return Mat4::identity();
    lightDir = lightDir / len;

    const f32 extent = std::max(5.0f, shadowDistance);
    const Vec3 eye = focus - lightDir * extent;
    Vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(lightDir.dot(up)) > 0.95f) {
        up = Vec3(0.0f, 0.0f, 1.0f);
    }

    const Mat4 view = Mat4::lookAt(eye, focus, up);
    const Mat4 proj =
        shadowDepthRange(Mat4::ortho(-extent, extent, -extent, extent, 0.1f, extent * 4.0f));
    return proj * view;
}

void buildDirectionalCascadeVPs(const GpuSceneCamera& camera, const Vec3& lightDirection,
                                f32 shadowDistance, u32 cascadeCount, Mat4* outVP, f32* outSplits) {
    const f32 nearClip = std::max(camera.nearClip, 0.05f);
    const f32 farClip = std::min(std::max(shadowDistance, nearClip + 1.0f), camera.farClip);
    const f32 lambda = 0.5f;
    const f32 ratio = farClip / nearClip;

    outSplits[0] = nearClip;
    for (u32 i = 1; i <= cascadeCount; ++i) {
        const f32 p = static_cast<f32>(i) / static_cast<f32>(cascadeCount);
        const f32 logSplit = nearClip * std::pow(ratio, p);
        const f32 uniformSplit = nearClip + (farClip - nearClip) * p;
        outSplits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }

    Vec3 lightDir = lightDirection;
    const f32 len = lightDir.length();
    if (len < 1e-6f) {
        for (u32 c = 0; c < cascadeCount; ++c) {
            outVP[c] = Mat4::identity();
        }
        return;
    }
    lightDir = lightDir / len;

    Vec3 camForward = camera.focus - camera.position;
    if (camForward.lengthSquared() < 1e-8f) {
        camForward = Vec3(0.0f, 0.0f, -1.0f);
    } else {
        camForward = camForward.normalized();
    }
    const f32 tanHalfFov = std::tan(camera.fovRad * 0.5f);

    for (u32 c = 0; c < cascadeCount; ++c) {
        const f32 splitNear = outSplits[c];
        const f32 splitFar = outSplits[c + 1];
        const f32 midDist = (splitNear + splitFar) * 0.5f;
        const f32 extent = std::max(8.0f, splitFar * tanHalfFov * 2.5f);
        const Vec3 focus = camera.position + camForward * midDist;
        const Vec3 eye = focus - lightDir * extent;
        Vec3 up(0.0f, 1.0f, 0.0f);
        if (std::abs(lightDir.dot(up)) > 0.95f) {
            up = Vec3(0.0f, 0.0f, 1.0f);
        }
        const Mat4 view = Mat4::lookAt(eye, focus, up);
        const Mat4 proj =
            shadowDepthRange(Mat4::ortho(-extent, extent, -extent, extent, 0.1f, extent * 4.0f));
        outVP[c] = proj * view;
    }
}

Mat4 buildSpotLightVP(const Vec3& position, const Vec3& direction, f32 radius, f32 angleDegrees) {
    Vec3 lightDir = direction;
    const f32 len = lightDir.length();
    if (len < 1e-6f || radius <= 0.01f || angleDegrees <= 1.0f) {
        return Mat4::identity();
    }
    lightDir = lightDir / len;

    const Vec3 target = position + lightDir * radius;
    Vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(lightDir.dot(up)) > 0.95f) {
        up = Vec3(0.0f, 0.0f, 1.0f);
    }
    const Mat4 view = Mat4::lookAt(position, target, up);
    const Mat4 proj = shadowDepthRange(
        Mat4::perspective(angleDegrees * 3.14159265f / 180.0f, 1.0f, 0.1f, radius));
    return proj * view;
}

struct ResolvedMeshAlbedo {
    RHI::Texture* texture = nullptr;
    Vec4 factor{1.0f, 1.0f, 1.0f, 1.0f};
    f32 metallic = 0.0f;
    f32 roughness = 0.5f;
    u8 alphaMode = 0;
    f32 alphaCutoff = 0.5f;
};

Vec3 meshWorldCenter(const Mat4& worldMatrix, const Assets::Mesh3D* mesh) {
    if (!mesh) return worldMatrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
    const Vec3 localCenter = (mesh->bounds.min + mesh->bounds.max) * 0.5f;
    return worldMatrix.transformPoint(localCenter);
}

/// The external texture files a mesh file names. Reading and scanning the whole mesh file for every
/// draw of every frame cost milliseconds per object on a dense OBJ without materials, so the answer is
/// remembered until the file's modification time changes.
const std::vector<std::string>& externalMeshUris(const std::string& meshPath) {
    struct Entry {
        std::filesystem::file_time_type stamp{};
        std::vector<std::string> uris;
    };
    static std::unordered_map<std::string, Entry> cache;
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(meshPath, ec);
    auto it = cache.find(meshPath);
    if (it == cache.end() || (!ec && it->second.stamp != stamp)) {
        Entry entry;
        if (!ec) entry.stamp = stamp;
        entry.uris = Assets::MeshImportValidator::listGltfExternalUris(meshPath);
        it = cache.insert_or_assign(meshPath, std::move(entry)).first;
    }
    return it->second.uris;
}

ResolvedMeshAlbedo resolveMeshAlbedo(RHI::RenderDevice* device, const Assets::Mesh3D* mesh,
                                     const std::string& meshPath, const std::string& projectRoot,
                                     const std::string& customTexturePath, u32 materialIndex,
                                     u32 qualityTier) {
    ResolvedMeshAlbedo result;
    auto& cache = GpuTextureCache::instance();

    if (!customTexturePath.empty()) {
        result.texture = cache.acquire(device, customTexturePath, projectRoot, qualityTier, true);
        if (result.texture) return result;
    }

    if (mesh && materialIndex < mesh->materials.size()) {
        const Assets::MeshSurfaceMaterial& mat = mesh->materials[materialIndex];
        result.factor = Vec4(mat.albedoColor.r, mat.albedoColor.g, mat.albedoColor.b,
                           mat.albedoColor.a);
        result.metallic = mat.metallic;
        result.roughness = mat.roughness;
        result.alphaMode = mat.alphaMode;
        result.alphaCutoff = mat.alphaCutoff;

        if (!mat.albedoPath.empty()) {
            result.texture = cache.acquire(device, mat.albedoPath, projectRoot, qualityTier, true);
            if (result.texture) return result;
        }
        if (!mat.albedoPixels.empty() && mat.albedoWidth > 0 && mat.albedoHeight > 0) {
            const std::string key = meshPath + "#mat" + std::to_string(materialIndex);
            result.texture = cache.acquireFromPixels(device, key, mat.albedoPixels.data(),
                                                     mat.albedoWidth, mat.albedoHeight,
                                                     mat.albedoChannels, qualityTier, true);
            if (result.texture) return result;
        }
        return result;
    }

    if (!meshPath.empty()) {
        const std::string pngPath =
            std::filesystem::path(meshPath).replace_extension(".png").string();
        result.texture = cache.acquire(device, pngPath, projectRoot, qualityTier, true);
        if (result.texture) return result;

        for (const std::string& uri : externalMeshUris(meshPath)) {
            const std::string texPath =
                (std::filesystem::path(meshPath).parent_path() / uri).string();
            result.texture = cache.acquire(device, texPath, projectRoot, qualityTier, true);
            if (result.texture) return result;
        }
    }

    if (mesh && mesh->textureWidth > 0 && !mesh->baseColorTexture.empty()) {
        const std::string key = meshPath + "#embedded";
        result.texture = cache.acquireFromPixels(device, key, mesh->baseColorTexture.data(),
                                                 mesh->textureWidth, mesh->textureHeight,
                                                 mesh->textureChannels, qualityTier, true);
    }

    return result;
}

void fillMeshPipelineDesc(RHI::GraphicsPipelineDesc& pipe, RHI::VertexBufferLayoutDesc& layout,
                          RHI::VertexAttributeDesc* attrs, RHI::TextureFormat colorFormat,
                          bool depthTest, bool depthWrite) {
    layout = {0, kVertexStride, false};
    attrs[0] = {0, 0, RHI::VertexFormat::Float3, 0};
    attrs[1] = {1, 0, RHI::VertexFormat::Float3, 12};
    attrs[2] = {2, 0, RHI::VertexFormat::Float2, 24};
    attrs[3] = {3, 0, RHI::VertexFormat::Float4, 32};
    pipe.vertexBuffers = &layout;
    pipe.numVertexBuffers = 1;
    pipe.attributes = attrs;
    pipe.numAttributes = 4;
    pipe.colorFormat = colorFormat;
    pipe.depthFormat = RHI::TextureFormat::D32_FLOAT;
    pipe.depthTest = depthTest;
    pipe.depthWrite = depthWrite;
    pipe.enableBlend = false;
}

/// Adds the three G-buffer attachments shared by every scene pipeline. The
/// order must match GBufferTargets and the shader `layout(location = 1..3)`
/// outputs: albedo, octahedral normal, material.
void addGBufferTargets(RHI::GraphicsPipelineDesc& pipe) {
    pipe.numColorTargets = 4;
    pipe.extraColorFormats[0] = RHI::TextureFormat::R8G8B8A8_UNORM;       // gAlbedo
    pipe.extraColorFormats[1] = RHI::TextureFormat::R8G8B8A8_UNORM;       // normal + material
    pipe.extraColorFormats[2] = RHI::TextureFormat::R16G16B16A16_FLOAT;  // D_t
}

bool createGBufferTargets(RHI::RenderDevice* device, u32 width, u32 height,
                          GBufferTargets& out) {
    if (!device || width == 0 || height == 0) return false;
    RHI::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    desc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    out.albedo = device->createTexture(desc);
    out.normalMaterial = device->createTexture(desc);
    desc.format = RHI::TextureFormat::R16G16B16A16_FLOAT;
    out.direct = device->createTexture(desc);
    return out.valid();
}

void destroyGBufferTargets(RHI::RenderDevice* device, GBufferTargets& targets) {
    if (!device) return;
    if (targets.albedo) device->destroyTexture(targets.albedo);
    if (targets.normalMaterial) device->destroyTexture(targets.normalMaterial);
    if (targets.direct) device->destroyTexture(targets.direct);
    targets = GBufferTargets{};
}

}  // namespace

bool GpuSceneRenderer::init(RHI::RenderDevice* device) {
    if (!device || !device->isInitialized()) return false;
    shutdown();

    m_device = device;
    if (!m_pointShadows.init(device)) return false;
    if (!m_directionalShadows.init(device)) return false;
    if (!m_spotShadows.init(device)) return false;
    if (!createPipelines()) {
        shutdown();
        return false;
    }

    m_sampler = m_device->createSampler();
    RHI::SamplerDesc repeatDesc;
    repeatDesc.linearFilter = true;
    repeatDesc.clampToEdge = false;
    repeatDesc.maxAnisotropy = 8.0f;
    m_repeatSampler = m_device->createSampler(repeatDesc);
    RHI::SamplerDesc pointDesc;
    pointDesc.linearFilter = false;
    pointDesc.clampToEdge = true;
    m_pointSampler = m_device->createSampler(pointDesc);
    RHI::TextureDesc cubeDesc;
    cubeDesc.width = 4;
    cubeDesc.height = 4;
    cubeDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    cubeDesc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    cubeDesc.type = RHI::TextureType::Cube;
    m_fallbackCube = m_device->createTexture(cubeDesc);
    m_views.reserve(kMaxViewTargets);
    m_post.init(device);
    m_ready = m_sampler && m_repeatSampler && m_pointSampler && m_post.ready();
    return m_ready;
}

void GpuSceneRenderer::destroyMeshGpu(Assets::Mesh3D& mesh) {
    if (m_device) {
        if (mesh.vertexBuffer) m_device->destroyBuffer(mesh.vertexBuffer);
        if (mesh.indexBuffer) m_device->destroyBuffer(mesh.indexBuffer);
        if (mesh.colorBuffer) m_device->destroyBuffer(mesh.colorBuffer);
    }
    mesh.vertexBuffer = nullptr;
    mesh.indexBuffer = nullptr;
    mesh.colorBuffer = nullptr;
}

Assets::Mesh3D* GpuSceneRenderer::selectMeshLod(const Assets::Mesh3D* source, ECS::Entity entity,
                                                 f32 distance) {
    if (!source || source->indices.size() < 600) return const_cast<Assets::Mesh3D*>(source);

    u32& level = m_entityLod[entity.id()];
    constexpr f32 kNear = 120.0f;
    constexpr f32 kFar = 350.0f;
    if (level == 0 && distance > kNear * 1.12f) level = 1;
    else if (level == 1 && distance > kFar * 1.12f) level = 2;
    else if (level >= 1 && distance < kNear * 0.88f) level = 0;
    else if (level == 2 && distance < kFar * 0.88f) level = 1;
    if (level == 0) return const_cast<Assets::Mesh3D*>(source);

    MeshLodCache& cache = m_meshLods[source];
    if (!cache.built) {
        const Vec3 extent = source->bounds.max - source->bounds.min;
        const f32 diagonal = std::max(extent.length(), 0.25f);
        Assets::MeshLOD::buildClusteredLod(*source, cache.medium, diagonal * 0.03f);
        Assets::MeshLOD::buildClusteredLod(*source, cache.far, diagonal * 0.08f);
        cache.built = true;
    }
    Assets::Mesh3D* chosen = (level == 1) ? &cache.medium : &cache.far;
    if (!chosen || chosen->indices.size() < 3) return const_cast<Assets::Mesh3D*>(source);
    return chosen;
}

RHI::Buffer* GpuSceneRenderer::vertexBufferFor(const MeshDraw& draw) {
    if (!draw.mesh) return nullptr;
    if (!draw.skinnedVertices || draw.skinnedVertices->empty() || !m_device) {
        return draw.mesh->vertexBuffer;
    }
    const u64 bytes = static_cast<u64>(draw.skinnedVertices->size() * sizeof(Assets::Vertex3D));
    const u64 generation = Animation::skinnedGenerationFor(draw.entity.id());
    RHI::Buffer*& slot = m_skinBuffers[draw.entity.id()];
    u64& uploaded = m_skinUploadGen[draw.entity.id()];
    if (!slot || slot->size != bytes) {
        if (slot) m_device->destroyBuffer(slot);
        RHI::BufferDesc desc;
        desc.size = bytes;
        slot = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);
        uploaded = 0;
    }
    if (slot && generation != 0 && uploaded == generation) return slot;
    if (slot) m_device->uploadBuffer(slot, draw.skinnedVertices->data(), bytes);
    if (slot) uploaded = generation;
    return slot ? slot : draw.mesh->vertexBuffer;
}

GpuSceneRenderer::DisplacedMesh* GpuSceneRenderer::displacedMeshFor(const Assets::Mesh3D* source,
                                                                    u32 levels) {
    if (!source || levels == 0 || !source->skin.empty()) return nullptr;
    DisplacedMesh& entry = m_displacedMeshes[{source, levels}];
    if (entry.valid && entry.sourceRevision == source->geometryRevision) return &entry;
    if (entry.valid) destroyMeshGpu(entry.mesh);
    entry.valid = false;
    // Dense meshes (a UV sphere is ~3k indices) cannot take the full level count: lower it until
    // the result fits the index budget instead of silently giving up.
    constexpr size_t kBudget = 3'000'000;
    while (levels > 0 && source->indices.size() * (size_t{1} << (2 * levels)) > kBudget) --levels;
    if (levels == 0 || !Assets::subdivideMesh(*source, levels, entry.mesh)) return nullptr;
    const Assets::MeshDensity density = Assets::measureMeshDensity(entry.mesh);
    entry.metersPerUv = density.metersPerUv;
    entry.edgeUv = density.averageEdgeUv;
    entry.sourceRevision = source->geometryRevision;
    if (!ensureMeshUploaded(&entry.mesh)) return nullptr;
    entry.valid = true;
    return &entry;
}

void GpuSceneRenderer::shutdown() {
    if (m_device) {
        for (auto& entry : m_skinBuffers) {
            if (entry.second) m_device->destroyBuffer(entry.second);
        }
    }
    m_skinBuffers.clear();
    m_skinUploadGen.clear();
    for (auto& [_, cache] : m_meshLods) {
        destroyMeshGpu(cache.medium);
        destroyMeshGpu(cache.far);
    }
    m_meshLods.clear();
    for (auto& [_, entry] : m_displacedMeshes) destroyMeshGpu(entry.mesh);
    m_displacedMeshes.clear();
    m_entityLod.clear();
    m_post.shutdown();
    if (m_device) {
        for (ViewTargets& view : m_views) releaseViewTargets(view);
        for (ReflectionProbe& probe : m_probes) releaseProbe(probe);
        if (m_sceneVert) m_device->destroyShader(m_sceneVert);
        if (m_sceneFrag) m_device->destroyShader(m_sceneFrag);
        if (m_terrainFrag) m_device->destroyShader(m_terrainFrag);
        if (m_shadowVert) m_device->destroyShader(m_shadowVert);
        if (m_displaceVert) m_device->destroyShader(m_displaceVert);
        if (m_shadowDisplaceVert) m_device->destroyShader(m_shadowDisplaceVert);
        if (m_displacePipeline) m_device->destroyPipeline(m_displacePipeline);
        if (m_shadowDisplacePipeline) m_device->destroyPipeline(m_shadowDisplacePipeline);
        if (m_shadowFrag) m_device->destroyShader(m_shadowFrag);
        if (m_shadowCutoutFrag) m_device->destroyShader(m_shadowCutoutFrag);
        if (m_shadowCutoutPipeline) m_device->destroyPipeline(m_shadowCutoutPipeline);
        if (m_foliageVert) m_device->destroyShader(m_foliageVert);
        releaseMicromeshes();
        if (m_micromeshVert) m_device->destroyShader(m_micromeshVert);
        if (m_micromeshPipeline) m_device->destroyPipeline(m_micromeshPipeline);
        if (m_shadowMicromeshVert) m_device->destroyShader(m_shadowMicromeshVert);
        if (m_shadowMicromeshPipeline) m_device->destroyPipeline(m_shadowMicromeshPipeline);
        if (m_waterVert) m_device->destroyShader(m_waterVert);
        if (m_waterPipeline) m_device->destroyPipeline(m_waterPipeline);
        if (m_waterBlendPipeline) m_device->destroyPipeline(m_waterBlendPipeline);
        if (m_foliageInstancedVert) m_device->destroyShader(m_foliageInstancedVert);
        if (m_foliageInstancedPipeline) m_device->destroyPipeline(m_foliageInstancedPipeline);
        if (m_shadowFoliageVert) m_device->destroyShader(m_shadowFoliageVert);
        if (m_foliagePipeline) m_device->destroyPipeline(m_foliagePipeline);
        if (m_shadowFoliagePipeline) m_device->destroyPipeline(m_shadowFoliagePipeline);
        if (m_shadowFoliageCutoutPipeline) m_device->destroyPipeline(m_shadowFoliageCutoutPipeline);
        if (m_fullscreenVert) m_device->destroyShader(m_fullscreenVert);
        if (m_skyFrag) m_device->destroyShader(m_skyFrag);
        if (m_gridFrag) m_device->destroyShader(m_gridFrag);
        if (m_scenePipeline) m_device->destroyPipeline(m_scenePipeline);
        if (m_wireframePipeline) m_device->destroyPipeline(m_wireframePipeline);
        if (m_terrainPipeline) m_device->destroyPipeline(m_terrainPipeline);
        if (m_shadowPipeline) m_device->destroyPipeline(m_shadowPipeline);
        if (m_instancedPipeline) m_device->destroyPipeline(m_instancedPipeline);
        if (m_blendPipeline) m_device->destroyPipeline(m_blendPipeline);
        if (m_skyPipeline) m_device->destroyPipeline(m_skyPipeline);
        if (m_gridPipeline) m_device->destroyPipeline(m_gridPipeline);
        if (m_effectPipeline) m_device->destroyPipeline(m_effectPipeline);
        if (m_effectVert) m_device->destroyShader(m_effectVert);
        if (m_effectFrag) m_device->destroyShader(m_effectFrag);
        destroyMeshGpu(m_effectMesh);
        if (m_instancedVert) m_device->destroyShader(m_instancedVert);
        if (m_instanceBuffer) m_device->destroyBuffer(m_instanceBuffer);
        if (m_reflectionColor) m_device->destroyTexture(m_reflectionColor);
        if (m_reflectionDepth) m_device->destroyTexture(m_reflectionDepth);
        destroyGBufferTargets(m_device, m_reflectionGbuffer);
        for (auto& entry : m_photonViews) entry.second->cascades.shutdown(m_device);
        m_photonViews.clear();
        m_pv = nullptr;
        if (m_fallbackCube) m_device->destroyTexture(m_fallbackCube);
        m_environment.release(m_device);
        if (m_sampler) m_device->destroySampler(m_sampler);
        if (m_repeatSampler) m_device->destroySampler(m_repeatSampler);
        if (m_pointSampler) m_device->destroySampler(m_pointSampler);
    }
    m_views.clear();
    m_probes.clear();
    m_sceneVert = nullptr;
    m_sceneFrag = nullptr;
    m_terrainFrag = nullptr;
    m_shadowVert = nullptr;
    m_displaceVert = nullptr;
    m_shadowDisplaceVert = nullptr;
    m_displacePipeline = nullptr;
    m_shadowDisplacePipeline = nullptr;
    m_shadowFrag = nullptr;
    m_shadowCutoutFrag = nullptr;
    m_shadowCutoutPipeline = nullptr;
    m_foliageVert = nullptr;
    m_micromeshVert = nullptr;
    m_micromeshPipeline = nullptr;
    m_shadowMicromeshVert = nullptr;
    m_shadowMicromeshPipeline = nullptr;
    m_waterVert = nullptr;
    m_waterPipeline = nullptr;
    m_waterBlendPipeline = nullptr;
    m_foliageInstancedVert = nullptr;
    m_foliageInstancedPipeline = nullptr;
    m_shadowFoliageVert = nullptr;
    m_foliagePipeline = nullptr;
    m_shadowFoliagePipeline = nullptr;
    m_shadowFoliageCutoutPipeline = nullptr;
    m_fullscreenVert = nullptr;
    m_skyFrag = nullptr;
    m_gridFrag = nullptr;
    m_effectVert = nullptr;
    m_effectFrag = nullptr;
    m_effectPipeline = nullptr;
    m_scenePipeline = nullptr;
    m_wireframePipeline = nullptr;
    m_terrainPipeline = nullptr;
    m_shadowPipeline = nullptr;
    m_instancedPipeline = nullptr;
    m_blendPipeline = nullptr;
    m_skyPipeline = nullptr;
    m_gridPipeline = nullptr;
    m_instancedVert = nullptr;
    m_instanceBuffer = nullptr;
    m_reflectionColor = nullptr;
    m_reflectionDepth = nullptr;
    m_fallbackCube = nullptr;
    m_reflectionWidth = 0;
    m_reflectionHeight = 0;
    m_reflectionValid = false;
    m_probesPending = false;
    m_needsAnotherFrame = false;
    m_inReflectionPass = false;
    m_sampler = nullptr;
    m_repeatSampler = nullptr;
    m_pointSampler = nullptr;
    m_pointShadows.shutdown();
    m_directionalShadows.shutdown();
    m_spotShadows.shutdown();
    m_device = nullptr;
    m_ready = false;
}

bool GpuSceneRenderer::createPipelines() {
    const auto format = detectShaderFormat(m_device);
    if (format != RHI::ShaderBytecodeFormat::SPIRV
#ifdef CF_HAS_GENERATED_SHADERS_DXBC
        && format != RHI::ShaderBytecodeFormat::DXBC
#endif
#ifdef CF_HAS_GENERATED_SHADERS_MSL
        && format != RHI::ShaderBytecodeFormat::MSL
        && format != RHI::ShaderBytecodeFormat::Metallib
#endif
        ) {
        return false;
    }

    m_sceneVert = createBuiltinShader(m_device, BuiltinShader::SceneLitVertex,
                                      RHI::ShaderStage::Vertex, 1);
    m_sceneFrag = createBuiltinShader(m_device, BuiltinShader::SceneLitFragment,
                                      RHI::ShaderStage::Fragment, 2, 16);
    m_instancedVert = createBuiltinShader(m_device, BuiltinShader::SceneInstancedVertex,
                                          RHI::ShaderStage::Vertex, 1);
    m_terrainFrag = createBuiltinShader(m_device, BuiltinShader::TerrainLitFragment,
                                        RHI::ShaderStage::Fragment, 3, 15);
    m_shadowVert = createBuiltinShader(m_device, BuiltinShader::ShadowDepthVertex,
                                       RHI::ShaderStage::Vertex, 1);
    m_shadowFrag = createBuiltinShader(m_device, BuiltinShader::ShadowDepthFragment,
                                       RHI::ShaderStage::Fragment, 0);
    m_foliageVert = createBuiltinShader(m_device, BuiltinShader::SceneFoliageVertex,
                                        RHI::ShaderStage::Vertex, 1);
    m_foliageInstancedVert = createBuiltinShader(m_device, BuiltinShader::SceneFoliageInstancedVertex,
                                                 RHI::ShaderStage::Vertex, 1);
    m_waterVert = createBuiltinShader(m_device, BuiltinShader::WaterVertex, RHI::ShaderStage::Vertex, 1);
    m_shadowFoliageVert = createBuiltinShader(m_device, BuiltinShader::ShadowFoliageVertex,
                                              RHI::ShaderStage::Vertex, 1);
    m_shadowCutoutFrag = createBuiltinShader(m_device, BuiltinShader::ShadowCutoutFragment,
                                             RHI::ShaderStage::Fragment, 1, 1);
    m_fullscreenVert = createBuiltinShader(m_device, BuiltinShader::FullscreenVertex,
                                           RHI::ShaderStage::Vertex, 0);
    m_skyFrag = createBuiltinShader(m_device, BuiltinShader::SkyFragment,
                                    RHI::ShaderStage::Fragment, 1, 1);
    m_gridFrag = createBuiltinShader(m_device, BuiltinShader::GridFragment,
                                     RHI::ShaderStage::Fragment, 1, 0);

    if (!m_sceneVert || !m_sceneFrag || !m_terrainFrag || !m_shadowVert || !m_shadowFrag) {
        return false;
    }

    RHI::VertexBufferLayoutDesc layout{};
    RHI::VertexAttributeDesc attrs[4]{};
    RHI::GraphicsPipelineDesc sceneDesc{};
    fillMeshPipelineDesc(sceneDesc, layout, attrs, kHdrFormat, true, true);
    addGBufferTargets(sceneDesc);
    m_scenePipeline = m_device->createGraphicsPipeline(m_sceneVert, m_sceneFrag, sceneDesc);
    RHI::GraphicsPipelineDesc wireDesc = sceneDesc;
    wireDesc.fillMode = RHI::FillMode::Line;
    m_wireframePipeline = m_device->createGraphicsPipeline(m_sceneVert, m_sceneFrag, wireDesc);
    m_terrainPipeline = m_device->createGraphicsPipeline(m_sceneVert, m_terrainFrag, sceneDesc);
    RHI::GraphicsPipelineDesc blendDesc = sceneDesc;
    blendDesc.depthWrite = false;
    blendDesc.blendMode = RHI::BlendMode::Premultiplied;
    m_blendPipeline = m_device->createGraphicsPipeline(m_sceneVert, m_sceneFrag, blendDesc);
    // Geometric displacement: one vertex sampler (set 0), one vertex UBO. Optional - when the
    // backend cannot build these shaders, materials silently fall back to parallax.
    m_displaceVert = createBuiltinShader(m_device, BuiltinShader::SceneDisplaceVertex,
                                         RHI::ShaderStage::Vertex, 1, 1);
    m_shadowDisplaceVert = createBuiltinShader(m_device, BuiltinShader::ShadowDisplaceVertex,
                                               RHI::ShaderStage::Vertex, 1, 1);
    if (m_displaceVert) {
        m_displacePipeline = m_device->createGraphicsPipeline(m_displaceVert, m_sceneFrag, sceneDesc);
    }

    if (m_fullscreenVert && m_skyFrag) {
        RHI::GraphicsPipelineDesc skyDesc{};
        skyDesc.colorFormat = kHdrFormat;
        skyDesc.depthFormat = RHI::TextureFormat::D32_FLOAT;
        skyDesc.depthTest = true;
        skyDesc.depthWrite = false;
        addGBufferTargets(skyDesc);
        m_skyPipeline = m_device->createGraphicsPipeline(m_fullscreenVert, m_skyFrag, skyDesc);
    }
    if (m_fullscreenVert && m_gridFrag) {
        RHI::GraphicsPipelineDesc gridDesc{};
        gridDesc.colorFormat = kHdrFormat;
        gridDesc.depthFormat = RHI::TextureFormat::D32_FLOAT;
        gridDesc.depthTest = true;
        gridDesc.depthWrite = false;
        gridDesc.blendMode = RHI::BlendMode::Alpha;
        addGBufferTargets(gridDesc);
        m_gridPipeline = m_device->createGraphicsPipeline(m_fullscreenVert, m_gridFrag, gridDesc);
    }
    m_effectVert = createBuiltinShader(m_device, BuiltinShader::EffectBillboardVertex,
                                       RHI::ShaderStage::Vertex, 1);
    m_effectFrag = createBuiltinShader(m_device, BuiltinShader::EffectBillboardFragment,
                                       RHI::ShaderStage::Fragment, 0);
    if (m_effectVert && m_effectFrag) {
        RHI::GraphicsPipelineDesc effectDesc{};
        fillMeshPipelineDesc(effectDesc, layout, attrs, kHdrFormat, true, true);
        addGBufferTargets(effectDesc);
        effectDesc.depthWrite = false;
        effectDesc.blendMode = RHI::BlendMode::Premultiplied;
        m_effectPipeline = m_device->createGraphicsPipeline(m_effectVert, m_effectFrag, effectDesc);
    }

    // Color-only shadow pass (no depth attachment) for cubemap / 2D shadow maps.
    RHI::GraphicsPipelineDesc shadowDesc{};
    fillMeshPipelineDesc(shadowDesc, layout, attrs, RHI::TextureFormat::R16_FLOAT, false, false);
    // No depth attachment: keep the nearest occluder with MIN blending so the result does not
    // depend on draw order (a floor drawn after a sphere used to erase the sphere's shadow).
    shadowDesc.blendMode = RHI::BlendMode::Min;
    m_shadowPipeline = m_device->createGraphicsPipeline(m_shadowVert, m_shadowFrag, shadowDesc);
    if (m_shadowCutoutFrag) {
        m_shadowCutoutPipeline =
            m_device->createGraphicsPipeline(m_shadowVert, m_shadowCutoutFrag, shadowDesc);
    }
    if (m_shadowDisplaceVert) {
        m_shadowDisplacePipeline =
            m_device->createGraphicsPipeline(m_shadowDisplaceVert, m_shadowFrag, shadowDesc);
    }

    // Water: the same blended pass as translucent meshes, waves displaced in the vertex shader.
    if (m_waterVert) {
        m_waterPipeline = m_device->createGraphicsPipeline(m_waterVert, m_sceneFrag, sceneDesc);
        m_waterBlendPipeline = m_device->createGraphicsPipeline(m_waterVert, m_sceneFrag, blendDesc);
    }

    // Micromeshes: the cluster buffers (positions, normals, UVs, tangents) and the instance matrix from a
    // storage buffer, into the same G-buffer through scene_lit.frag. Optional: without it (a backend
    // other than SPIR-V, or no storage buffers) a Micromesh entity just draws as a normal mesh.
    {
        const RHI::DeviceCapabilities caps = m_device->capabilities();
        if (caps.indirectDraw && caps.graphicsStorageBuffers && caps.computeStorageWrite &&
            format == RHI::ShaderBytecodeFormat::SPIRV) {
            m_micromeshVert = createBuiltinShader(m_device, BuiltinShader::SceneMicromeshVertex,
                                                  RHI::ShaderStage::Vertex, 1, 0, 1);
        }
        if (m_micromeshVert) {
            RHI::VertexBufferLayoutDesc microLayouts[5]{};
            RHI::VertexAttributeDesc microAttrs[5]{};
            microLayouts[0] = {0, static_cast<u32>(sizeof(float) * 3), false};
            microLayouts[1] = {1, static_cast<u32>(sizeof(float) * 3), false};
            microLayouts[2] = {2, static_cast<u32>(sizeof(float) * 2), false};
            microLayouts[3] = {3, static_cast<u32>(sizeof(float) * 4), false};
            microAttrs[0] = {0, 0, RHI::VertexFormat::Float3, 0};
            microAttrs[1] = {1, 1, RHI::VertexFormat::Float3, 0};
            microAttrs[2] = {2, 2, RHI::VertexFormat::Float2, 0};
            microAttrs[3] = {3, 3, RHI::VertexFormat::Float4, 0};
            microLayouts[4] = {4, static_cast<u32>(sizeof(float) * 4), false};
            microAttrs[4] = {4, 4, RHI::VertexFormat::Float4, 0};
            RHI::GraphicsPipelineDesc microDesc = sceneDesc;
            microDesc.vertexBuffers = microLayouts;
            microDesc.numVertexBuffers = 5;
            microDesc.attributes = microAttrs;
            microDesc.numAttributes = 5;
            m_micromeshPipeline = m_device->createGraphicsPipeline(m_micromeshVert, m_sceneFrag, microDesc);
        }
        // The directional shadow cascades of Micromesh objects: positions only, the same colour-only target.
        if (m_micromeshPipeline && caps.graphicsStorageBuffers) {
            m_shadowMicromeshVert = createBuiltinShader(m_device, BuiltinShader::ShadowMicromeshVertex,
                                                        RHI::ShaderStage::Vertex, 1, 0, 1);
        }
        if (m_shadowMicromeshVert) {
            RHI::VertexBufferLayoutDesc posLayout{0, static_cast<u32>(sizeof(float) * 3), false};
            RHI::VertexAttributeDesc posAttr{0, 0, RHI::VertexFormat::Float3, 0};
            RHI::GraphicsPipelineDesc posDesc{};
            fillMeshPipelineDesc(posDesc, layout, attrs, RHI::TextureFormat::R16_FLOAT, false, false);
            posDesc.blendMode = RHI::BlendMode::Min;
            posDesc.vertexBuffers = &posLayout;
            posDesc.numVertexBuffers = 1;
            posDesc.attributes = &posAttr;
            posDesc.numAttributes = 1;
            m_shadowMicromeshPipeline =
                m_device->createGraphicsPipeline(m_shadowMicromeshVert, m_shadowFrag, posDesc);
        }
    }

    // Meshes with per-vertex colour read it from a second vertex stream (never instanced).
    RHI::VertexBufferLayoutDesc foliageLayouts[2]{};
    RHI::VertexAttributeDesc foliageAttrs[5]{};
    foliageLayouts[0] = {0, kVertexStride, false};
    foliageLayouts[1] = {1, static_cast<u32>(sizeof(float) * 4), false};
    for (u32 i = 0; i < 4; ++i) foliageAttrs[i] = attrs[i];
    foliageAttrs[4] = {4, 1, RHI::VertexFormat::Float4, 0};
    if (m_foliageVert) {
        RHI::GraphicsPipelineDesc foliageDesc = sceneDesc;
        foliageDesc.vertexBuffers = foliageLayouts;
        foliageDesc.numVertexBuffers = 2;
        foliageDesc.attributes = foliageAttrs;
        foliageDesc.numAttributes = 5;
        m_foliagePipeline = m_device->createGraphicsPipeline(m_foliageVert, m_sceneFrag, foliageDesc);
    }
    if (m_shadowFoliageVert) {
        RHI::GraphicsPipelineDesc foliageShadow = shadowDesc;
        foliageShadow.vertexBuffers = foliageLayouts;
        foliageShadow.numVertexBuffers = 2;
        foliageShadow.attributes = foliageAttrs;
        foliageShadow.numAttributes = 5;
        m_shadowFoliagePipeline =
            m_device->createGraphicsPipeline(m_shadowFoliageVert, m_shadowFrag, foliageShadow);
        if (m_shadowCutoutFrag) {
            m_shadowFoliageCutoutPipeline = m_device->createGraphicsPipeline(
                m_shadowFoliageVert, m_shadowCutoutFrag, foliageShadow);
        }
    }

    if (m_instancedVert && m_sceneFrag) {
        RHI::VertexBufferLayoutDesc instancedLayouts[2]{};
        instancedLayouts[0] = {0, kVertexStride, false};
        instancedLayouts[1] = {1, sizeof(float) * 16, true};
        RHI::VertexAttributeDesc instancedAttrs[8]{};
        instancedAttrs[0] = {0, 0, RHI::VertexFormat::Float3, 0};
        instancedAttrs[1] = {1, 0, RHI::VertexFormat::Float3, 12};
        instancedAttrs[2] = {2, 0, RHI::VertexFormat::Float2, 24};
        instancedAttrs[3] = {3, 0, RHI::VertexFormat::Float4, 32};
        instancedAttrs[4] = {4, 1, RHI::VertexFormat::Float4, 0};
        instancedAttrs[5] = {5, 1, RHI::VertexFormat::Float4, 16};
        instancedAttrs[6] = {6, 1, RHI::VertexFormat::Float4, 32};
        instancedAttrs[7] = {7, 1, RHI::VertexFormat::Float4, 48};
        RHI::GraphicsPipelineDesc instancedDesc = sceneDesc;
        instancedDesc.vertexBuffers = instancedLayouts;
        instancedDesc.numVertexBuffers = 2;
        instancedDesc.attributes = instancedAttrs;
        instancedDesc.numAttributes = 8;
        m_instancedPipeline = m_device->createGraphicsPipeline(m_instancedVert, m_sceneFrag, instancedDesc);

        // The same with the per-vertex colour stream as a third buffer (location 8).
        if (m_foliageInstancedVert) {
            RHI::VertexBufferLayoutDesc layouts[3]{};
            layouts[0] = instancedLayouts[0];
            layouts[1] = instancedLayouts[1];
            layouts[2] = {2, static_cast<u32>(sizeof(float) * 4), false};
            RHI::VertexAttributeDesc colourAttrs[9]{};
            for (u32 i = 0; i < 8; ++i) colourAttrs[i] = instancedAttrs[i];
            colourAttrs[8] = {8, 2, RHI::VertexFormat::Float4, 0};
            RHI::GraphicsPipelineDesc colourDesc = instancedDesc;
            colourDesc.vertexBuffers = layouts;
            colourDesc.numVertexBuffers = 3;
            colourDesc.attributes = colourAttrs;
            colourDesc.numAttributes = 9;
            m_foliageInstancedPipeline =
                m_device->createGraphicsPipeline(m_foliageInstancedVert, m_sceneFrag, colourDesc);
        }
    }

    if (m_instancedPipeline) {
        RHI::BufferDesc instanceDesc;
        instanceDesc.size = 256ull * sizeof(float) * 16ull;
        instanceDesc.name = "SceneInstances";
        m_instanceBuffer = m_device->createBuffer(instanceDesc, RHI::BufferUsage::Vertex);
    }

    return m_scenePipeline && m_wireframePipeline && m_terrainPipeline && m_shadowPipeline &&
           m_blendPipeline;
}

void GpuSceneRenderer::setWind(float* out, const MeshDraw& draw) const {
    // A weather wind sways foliage harder the stronger it blows, in gusts (0 leaves the material as authored).
    const f32 windScale =
        m_weather.windFoliage > 0.0f
            ? 1.0f + m_weather.windFoliage * (m_weather.rainWindSpeed / 8.0f) * m_gust
            : 1.0f;
    out[0] = draw.windStrength * windScale;
    out[1] = draw.windSpeed;
    out[2] = draw.windFlutter;
    out[3] = m_simTime;
}

void GpuSceneRenderer::pushShadowDraw(RHI::CommandBuffer* cmd, const MeshDraw& draw,
                                      const Mat4& mvp, const Vec3& lightPos, int mode,
                                      u32 indexCount, u32 firstIndex) {
    if (!m_shadowPipeline || !draw.mesh || indexCount == 0) return;

    if (draw.displaced && m_shadowDisplacePipeline && draw.displacementTexture) {
        DisplaceShadowUBO dubo{};
        std::memcpy(dubo.mvp, mvp.data(), sizeof(dubo.mvp));
        std::memcpy(dubo.model, draw.worldMatrix.data(), sizeof(dubo.model));
        dubo.lightPos[0] = lightPos.x;
        dubo.lightPos[1] = lightPos.y;
        dubo.lightPos[2] = lightPos.z;
        dubo.mode = mode;
        dubo.disp[0] = draw.displacementScale;
        dubo.disp[1] = draw.uvObjectScale ? 1.0f : 0.0f;
        dubo.uv[0] = draw.uvTiling.x;
        dubo.uv[1] = draw.uvTiling.y;
        dubo.uv[2] = draw.uvOffset.x;
        dubo.uv[3] = draw.uvOffset.y;
        cmd->bindPipeline(m_shadowDisplacePipeline);
        cmd->bindVertexTexture(draw.displacementTexture, 0, m_repeatSampler);
        cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &dubo, sizeof(dubo));
        cmd->bindVertexBuffer(vertexBufferFor(draw));
        cmd->bindIndexBuffer(draw.mesh->indexBuffer);
        cmd->drawIndexed(indexCount, firstIndex, 0);
        return;
    }

    ShadowUBO ubo{};
    std::memcpy(ubo.mvp, mvp.data(), sizeof(ubo.mvp));
    std::memcpy(ubo.model, draw.worldMatrix.data(), sizeof(ubo.model));
    ubo.lightPos[0] = lightPos.x;
    ubo.lightPos[1] = lightPos.y;
    ubo.lightPos[2] = lightPos.z;
    ubo.mode = mode;
    setWind(ubo.wind, draw);

    // Cut-out cards (leaves, petals, grass) test the albedo's alpha, so the sun falls through the
    // gaps and the crown shades its own interior.
    RHI::Texture* cutoutTex = nullptr;
    if (draw.alphaMode == 1 && m_shadowCutoutPipeline) {
        cutoutTex = resolveMeshAlbedo(m_device, draw.mesh, draw.meshPath, m_shadowProjectRoot,
                                      draw.customTexturePath, 0,
                                      textureQualityTier(draw.viewerDistance, m_shadowTextureQuality))
                        .texture;
    }
    // A painted wind weight lives in the mesh's colour stream: use the foliage variants.
    const bool coloured = draw.mesh->colorBuffer && m_shadowFoliagePipeline;
    if (cutoutTex) {
        float cutout[8] = {draw.alphaCutoff, 0.0f, 0.0f, 0.0f,
                           draw.uvTiling.x, draw.uvTiling.y, draw.uvOffset.x, draw.uvOffset.y};
        cmd->bindPipeline(coloured && m_shadowFoliageCutoutPipeline ? m_shadowFoliageCutoutPipeline
                                                                    : m_shadowCutoutPipeline);
        cmd->bindTexture(cutoutTex, 0, m_repeatSampler);
        cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, cutout, sizeof(cutout));
    } else {
        cmd->bindPipeline(coloured ? m_shadowFoliagePipeline : m_shadowPipeline);
    }
    cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &ubo, sizeof(ubo));
    cmd->bindVertexBuffer(vertexBufferFor(draw));
    if (coloured) cmd->bindVertexBuffer(draw.mesh->colorBuffer, 1);
    cmd->bindIndexBuffer(draw.mesh->indexBuffer);
    cmd->drawIndexed(indexCount, firstIndex, 0);
}

void GpuSceneRenderer::pushShadowDrawsForMesh(RHI::CommandBuffer* cmd, const MeshDraw& draw,
                                              const Mat4& mvp, const Vec3& lightPos, int mode) {
    if (!draw.mesh || draw.mesh->indices.empty()) return;

    if (draw.mesh->subMeshes.size() > 1) {
        u32 index = 0;
        for (const Assets::SubMesh& submesh : draw.mesh->subMeshes) {
            const u32 current = index++;
            if (submesh.indexCount == 0) continue;
            if (draw.onlySubMesh >= 0 && static_cast<u32>(draw.onlySubMesh) != current) continue;
            pushShadowDraw(cmd, draw, mvp, lightPos, mode, submesh.indexCount, submesh.indexOffset);
        }
        return;
    }

    pushShadowDraw(cmd, draw, mvp, lightPos, mode, static_cast<u32>(draw.mesh->indices.size()), 0);
}

bool GpuSceneRenderer::ensureMeshUploaded(Assets::Mesh3D* mesh) {
    if (!mesh || !m_device) return false;
    if (mesh->vertices.empty() || mesh->indices.empty()) return false;

    const u64 vertexBytes = mesh->vertices.size() * sizeof(Assets::Vertex3D);
    const u64 indexBytes = mesh->indices.size() * sizeof(u32);

    // Buffers of another device (a mesh from a process-wide cache that outlived its device) are not ours to use or
    // free: forget them and upload again.
    if (mesh->gpuDeviceSerial != m_device->serial()) {
        mesh->vertexBuffer = nullptr;
        mesh->indexBuffer = nullptr;
        mesh->colorBuffer = nullptr;
        mesh->uploadedRevision = 0;
        mesh->gpuDeviceSerial = m_device->serial();
    }

    if (mesh->vertexBuffer && mesh->indexBuffer &&
        mesh->uploadedRevision == mesh->geometryRevision) {
        return true;
    }

    // The optional colour stream follows the geometry: (re)uploaded whenever the mesh is.
    auto syncColors = [&]() {
        const u64 bytes = mesh->vertexColors.size() == mesh->vertices.size()
                              ? mesh->vertexColors.size() * sizeof(float) * 4
                              : 0;
        if (bytes == 0) {
            if (mesh->colorBuffer) {
                m_device->destroyBuffer(mesh->colorBuffer);
                mesh->colorBuffer = nullptr;
            }
            return;
        }
        if (mesh->colorBuffer && mesh->colorBuffer->size != bytes) {
            m_device->destroyBuffer(mesh->colorBuffer);
            mesh->colorBuffer = nullptr;
        }
        if (!mesh->colorBuffer) {
            RHI::BufferDesc cdesc;
            cdesc.size = bytes;
            mesh->colorBuffer = m_device->createBuffer(cdesc, RHI::BufferUsage::Vertex);
        }
        if (mesh->colorBuffer &&
            !m_device->uploadBuffer(mesh->colorBuffer, mesh->vertexColors.data(), bytes)) {
            m_device->destroyBuffer(mesh->colorBuffer);
            mesh->colorBuffer = nullptr;
        }
    };

    // Dynamic meshes (water, fluid voxels) change every frame. Reuse the GPU
    // buffers when the size is unchanged and only re-upload the contents.
    if (mesh->vertexBuffer && mesh->indexBuffer && mesh->vertexBuffer->size == vertexBytes &&
        mesh->indexBuffer->size == indexBytes) {
        if (!m_device->uploadBuffer(mesh->vertexBuffer, mesh->vertices.data(), vertexBytes)) {
            return false;
        }
        if (!m_device->uploadBuffer(mesh->indexBuffer, mesh->indices.data(), indexBytes)) {
            return false;
        }
        syncColors();
        mesh->uploadedRevision = mesh->geometryRevision;
        return true;
    }

    if (mesh->vertexBuffer) {
        m_device->destroyBuffer(mesh->vertexBuffer);
        mesh->vertexBuffer = nullptr;
    }
    if (mesh->indexBuffer) {
        m_device->destroyBuffer(mesh->indexBuffer);
        mesh->indexBuffer = nullptr;
    }

    RHI::BufferDesc vdesc;
    vdesc.size = vertexBytes;
    mesh->vertexBuffer = m_device->createBuffer(vdesc, RHI::BufferUsage::Vertex);
    RHI::BufferDesc idesc;
    idesc.size = indexBytes;
    mesh->indexBuffer = m_device->createBuffer(idesc, RHI::BufferUsage::Index);
    if (!mesh->vertexBuffer || !mesh->indexBuffer) return false;

    if (!m_device->uploadBuffer(mesh->vertexBuffer, mesh->vertices.data(), vertexBytes)) {
        return false;
    }
    if (!m_device->uploadBuffer(mesh->indexBuffer, mesh->indices.data(), indexBytes)) {
        return false;
    }
    syncColors();
    mesh->uploadedRevision = mesh->geometryRevision;
    return true;
}

void GpuSceneRenderer::renderDirectionalShadows(RHI::CommandBuffer* cmd, ECS::World& world,
                                                const Scene::SceneLighting& lighting,
                                                const std::vector<MeshDraw>& draws,
                                                const GpuSceneCamera& camera) {
    (void)world;
    if (!m_shadowPipeline || draws.empty()) return;

    const f32 cascadeSize =
        static_cast<f32>(GpuDirectionalShadowMap::kCascadeResolution);
    const f32 atlasSize = static_cast<f32>(GpuDirectionalShadowMap::kAtlasResolution);

    u32 shadowSlot = 0;
    for (const auto& dir : lighting.lights.directionals) {
        if (!dir.castShadows || shadowSlot >= GpuDirectionalShadowMap::kMaxDirectionalShadowLights) {
            continue;
        }

        RHI::Texture* map = m_directionalShadows.texture(shadowSlot);
        if (!map) continue;

        const u32 cascadeCount = shadowSlot == 0
            ? std::clamp(m_activeCascadeCount, 1u, GpuDirectionalShadowMap::kMaxCascades)
            : 1u;
        Mat4 cascadeVPs[GpuDirectionalShadowMap::kMaxCascades]{};
        f32 splits[GpuDirectionalShadowMap::kMaxCascades + 1]{};
        buildDirectionalCascadeVPs(camera, dir.direction, dir.shadowDistance, cascadeCount,
                                 cascadeVPs, splits);
        m_directionalShadows.setCascadeData(shadowSlot, cascadeCount, cascadeVPs, splits, true);

        // Micromesh objects cast their own coarser cut into each cascade: a compute pass per cascade, before
        // the render pass. The error budget is four shadow texels (view independent: the cascade is ortho).
        u32 microViews[GpuDirectionalShadowMap::kMaxCascades];
        for (u32 cascade = 0; cascade < GpuDirectionalShadowMap::kMaxCascades; ++cascade) microViews[cascade] = ~0u;
        if (!m_micromeshDraws.empty() && m_shadowMicromeshPipeline) {
            for (u32 cascade = 0; cascade < cascadeCount; ++cascade) {
                const Mat4& vp = cascadeVPs[cascade];
                const f32 scaleX = Vec3(vp(0, 0), vp(0, 1), vp(0, 2)).length();
                const f32 texels = cascadeCount > 1 ? cascadeSize : atlasSize;
                MicromeshCullParams params;
                params.planes = planesFromViewProj(vp);
                params.camera = camera.position;
                params.threshold = scaleX > 1e-9f ? (2.0f / scaleX) / texels * 4.0f : 0.0f;
                params.frustum = true;
                params.cone = false;
                params.viewDependent = false;
                microViews[cascade] = cullMicromeshView(cmd, params);
            }
        }

        RHI::RenderPassDesc pass;
        pass.colorTarget = map;
        pass.clearColor[0] = 1.0f;
        pass.clearColor[1] = 1.0f;
        pass.clearColor[2] = 1.0f;
        pass.clearColor[3] = 1.0f;
        cmd->beginRenderPass(pass);

        for (u32 cascade = 0; cascade < cascadeCount; ++cascade) {
            const f32 offsetX = (cascade % 2) * cascadeSize;
            const f32 offsetY = (cascade / 2) * cascadeSize;
            const f32 viewportW = cascadeCount > 1 ? cascadeSize : atlasSize;
            const f32 viewportH = cascadeCount > 1 ? cascadeSize : atlasSize;
            cmd->setViewport(offsetX, offsetY, viewportW, viewportH);

            // A cascade is a box round one slice of the view: what lies outside its sides is not drawn into it
            // (a caster in the far cascade's range used to be submitted to all four).
            const std::array<Vec4, 6> sides = planesFromViewProj(cascadeVPs[cascade]);
            for (const auto& draw : draws) {
                if (!draw.castShadows || !draw.mesh || !ensureMeshUploaded(draw.mesh)) continue;
                // Wind, displacement and waves move vertices off the box the draw was gathered with.
                if (draw.hasAabb && !draw.displaced && !draw.gpuWater) {
                    const Vec3 margin(0.5f, 0.5f, 0.5f);
                    if (outsideCascadeSides(sides, draw.aabbMin - margin, draw.aabbMax + margin)) {
                        ++m_building.shadowDrawsCulled;
                        continue;
                    }
                }
                ++m_building.shadowDraws;
                const MicromeshDrawRef* microRef = nullptr;
                if (microViews[cascade] != ~0u && microDrawParts(&draw, microRef)) {
                    cmd->bindPipeline(m_shadowMicromeshPipeline);
                    float viewProj[16];
                    std::memcpy(viewProj, cascadeVPs[cascade].data(), sizeof(viewProj));
                    cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, viewProj, sizeof(viewProj));
                    for (const auto& part : microRef->parts) {
                        RHI::Buffer* storage[1] = {part.first->gpu.instanceBuffer()};
                        cmd->bindVertexStorageBuffers(storage, 0, 1);
                        part.first->gpu.drawSceneSlotPositions(cmd, m_micromeshArena, microViews[cascade], part.second);
                    }
                    continue;
                }
                pushShadowDrawsForMesh(cmd, draw, cascadeVPs[cascade] * draw.worldMatrix,
                                       camera.focus, 1);
            }
        }
        cmd->endRenderPass();
        ++shadowSlot;
    }

    for (; shadowSlot < GpuDirectionalShadowMap::kMaxDirectionalShadowLights; ++shadowSlot) {
        m_directionalShadows.clearSlot(shadowSlot);
    }
}

void GpuSceneRenderer::renderSpotShadows(RHI::CommandBuffer* cmd, ECS::World& world,
                                       const Scene::SceneLighting& lighting,
                                       const std::vector<MeshDraw>& draws) {
    (void)world;
    if (!m_shadowPipeline || draws.empty()) return;

    u32 shadowSlot = 0;
    for (const auto& spot : lighting.lights.spots) {
        if (!spot.castShadows || shadowSlot >= GpuSpotShadowMap::kMaxSpotShadowLights) continue;

        RHI::Texture* map = m_spotShadows.texture(shadowSlot);
        if (!map) continue;

        const Mat4 lightVP = buildSpotLightVP(spot.position, spot.direction, spot.radius, spot.angle);
        const f32 halfAngle = std::clamp(spot.angle * 3.14159265f / 180.0f * 0.5f, 0.01f, 1.5533f);
        m_spotShadows.setSlot(shadowSlot, lightVP, spot.position, spot.radius,
                              std::cos(halfAngle), true);

        RHI::RenderPassDesc pass;
        pass.colorTarget = map;
        pass.clearColor[0] = 1.0f;
        pass.clearColor[1] = 1.0f;
        pass.clearColor[2] = 1.0f;
        pass.clearColor[3] = 1.0f;
        cmd->beginRenderPass(pass);
        cmd->setViewport(0, 0, static_cast<f32>(GpuSpotShadowMap::kResolution),
                         static_cast<f32>(GpuSpotShadowMap::kResolution));

        for (const auto& draw : draws) {
            if (!draw.castShadows || !draw.mesh || !ensureMeshUploaded(draw.mesh)) continue;
            pushShadowDrawsForMesh(cmd, draw, lightVP * draw.worldMatrix, spot.position, 1);
        }
        cmd->endRenderPass();
        ++shadowSlot;
    }

    for (; shadowSlot < GpuSpotShadowMap::kMaxSpotShadowLights; ++shadowSlot) {
        m_spotShadows.clearSlot(shadowSlot);
    }
}

namespace {

constexpr f32 kPi = 3.14159265f;

/// Camera for cube face `face` (+X, -X, +Y, -Y, +Z, -Z). Render targets have a top-left origin
/// with y-up NDC, so the projection is flipped vertically to match cube-map addressing.
void cubeFaceCamera(const Vec3& origin, u32 face, f32 nearClip, f32 farClip, Mat4& view,
                    Mat4& proj, Vec3& focus) {
    static const Vec3 kDirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const Vec3 kUps[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    focus = origin + kDirs[face];
    view = Mat4::lookAt(origin, focus, kUps[face]);
    proj = Mat4::perspective(kPi * 0.5f, 1.0f, nearClip, farClip);
    proj(1, 1) = -proj(1, 1);
}

void setVec4(float* out, f32 x, f32 y, f32 z, f32 w) {
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

/// Linear-space fallback sky used when the scene has no environment map.
const Vec3 kSkyZenith{0.18f, 0.38f, 0.74f};
const Vec3 kSkyHorizon{0.64f, 0.78f, 0.94f};
const Vec3 kSkyGround{0.42f, 0.50f, 0.60f};
const Vec3 kFallbackAmbient{0.03f, 0.03f, 0.035f};

u32 probeMipCount(u32 resolution) {
    u32 mips = 1;
    while ((resolution >> mips) >= 4u) ++mips;
    return mips;
}

}  // namespace

void GpuSceneRenderer::renderPointShadows(RHI::CommandBuffer* cmd, ECS::World& world,
                                          const Scene::SceneLighting& lighting,
                                          const std::vector<MeshDraw>& draws) {
    (void)world;
    if (!m_shadowPipeline || draws.empty()) return;

    u32 shadowSlot = 0;
    for (const auto& point : lighting.lights.points) {
        if (!point.castShadows || shadowSlot >= GpuPointShadowMap::kMaxPointShadowLights) continue;

        RHI::Texture* cube = m_pointShadows.cubemap(shadowSlot);
        if (!cube) continue;

        for (u32 face = 0; face < 6; ++face) {
            Mat4 view;
            Mat4 proj;
            Vec3 focus;
            cubeFaceCamera(point.position, face, 0.1f, point.radius, view, proj, focus);

            RHI::RenderPassDesc pass;
            pass.colorTarget = cube;
            pass.colorLayer = face;
            pass.clearColor[0] = point.radius;
            pass.clearColor[1] = point.radius;
            pass.clearColor[2] = point.radius;
            pass.clearColor[3] = 1.0f;
            cmd->beginRenderPass(pass);
            cmd->setViewport(0, 0, static_cast<f32>(GpuPointShadowMap::kFaceSize),
                             static_cast<f32>(GpuPointShadowMap::kFaceSize));

            for (const auto& draw : draws) {
                if (!draw.castShadows || !draw.mesh || !ensureMeshUploaded(draw.mesh)) continue;
                pushShadowDrawsForMesh(cmd, draw, proj * view * draw.worldMatrix, point.position, 0);
            }
            cmd->endRenderPass();
        }
        m_pointShadows.setSlot(shadowSlot, point.position, point.radius, true);
        ++shadowSlot;
    }

    for (; shadowSlot < GpuPointShadowMap::kMaxPointShadowLights; ++shadowSlot) {
        m_pointShadows.clearSlot(shadowSlot);
    }
}

void GpuSceneRenderer::drawSky(RHI::CommandBuffer* cmd, const GpuSceneCamera& camera,
                               u32 targetHeight) {
    if (!m_skyPipeline) return;
    SkyUBO ubo{};
    const Mat4 inv = (camera.proj * camera.view).inverted();
    std::memcpy(ubo.invViewProj, inv.data(), sizeof(ubo.invViewProj));
    RHI::Texture* env = m_environment.texture();
    if (env) {
        // Match the environment's texel density to the target's so distant detail doesn't alias.
        const f32 envTexelsPerRadian = static_cast<f32>(env->width) / (2.0f * kPi);
        const f32 screenTexelsPerRadian =
            static_cast<f32>(std::max(targetHeight, 1u)) / std::max(camera.fovRad, 0.01f);
        const f32 maxLod = static_cast<f32>(m_environment.mipCount() > 0 ? m_environment.mipCount() - 1 : 0);
        const f32 lod = std::clamp(std::log2(std::max(envTexelsPerRadian / screenTexelsPerRadian, 1.0f)),
                                   0.0f, maxLod);
        setVec4(ubo.params, std::max(m_environmentExposure, 1e-4f), lod, 0.0f, 0.0f);
    }
    setVec4(ubo.zenith, kSkyZenith.x, kSkyZenith.y, kSkyZenith.z, 1.0f);
    setVec4(ubo.horizon, kSkyHorizon.x, kSkyHorizon.y, kSkyHorizon.z, 1.0f);
    setVec4(ubo.ground, kSkyGround.x, kSkyGround.y, kSkyGround.z, 1.0f);

    const GpuCloudSettings& clouds = m_cloudSettings;
    setVec4(ubo.camera, camera.position.x, camera.position.y, camera.position.z, clouds.time);
    setVec4(ubo.cloudA, clouds.coverage, clouds.density, clouds.baseAltitude,
            clouds.topAltitude);
    setVec4(ubo.cloudB, clouds.windSpeed, clouds.windDirection, clouds.detail, clouds.seed);
    setVec4(ubo.cloudC, static_cast<f32>(clouds.steps), clouds.mistDensity, clouds.mistHeight,
            clouds.mistFalloff);
    setVec4(ubo.sunDir, clouds.sunDirection.x, clouds.sunDirection.y, clouds.sunDirection.z,
            clouds.sunIntensity * clouds.sunScatter);
    setVec4(ubo.sunColor, clouds.sunColor.x, clouds.sunColor.y, clouds.sunColor.z, 1.0f);
    setVec4(ubo.cloudD, clouds.tint.x, clouds.tint.y, clouds.tint.z, clouds.darkness);
    setVec4(ubo.cloudE, clouds.skyLight, 0.0f, 0.0f, 0.0f);
    setVec4(ubo.boltA, m_lightning.strike.azimuth, m_lightning.strike.elevation, m_lightning.bolt,
            m_lightning.flash);
    setVec4(ubo.boltB, m_lightning.strike.start, clouds.silverLining, 0.0f, 0.0f);

    RHI::Texture* bound = env ? env : GpuTextureCache::instance().whiteTexture(m_device);
    cmd->bindPipeline(m_skyPipeline);
    if (bound) cmd->bindTexture(bound, 0, m_repeatSampler);
    cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &ubo, sizeof(ubo));
    cmd->draw(3);
}

void GpuSceneRenderer::applyWeatherToLights(Scene::LightingData& lights) const {
    if (m_weather.sunScale != 1.0f) {
        for (auto& d : lights.directionals) d.intensity *= m_weather.sunScale;
    }
    // A flash lights the scene from where the bolt struck: a cold directional light with no shadow of its own,
    // seen by the forward shader and by Photon's injection alike (so the bounce flashes too).
    const f32 flash = m_lightning.flash;
    if (flash > 0.02f && lights.directionals.size() < 4) {
        const Scene::LightningStrike& strike = m_lightning.strike;
        const f32 elevation = strike.elevation * 0.6f;
        const Vec3 toBolt(std::sin(strike.azimuth) * std::cos(elevation), std::sin(elevation),
                          std::cos(strike.azimuth) * std::cos(elevation));
        Scene::DirectionalLightData bolt;
        bolt.direction = toBolt * -1.0f;
        bolt.color = Vec4(0.78f, 0.84f, 1.0f, 1.0f);
        bolt.intensity = flash * 5.0f;
        bolt.shadowDistance = 0.0f;
        bolt.castShadows = false;
        lights.directionals.push_back(bolt);
    }
}

void GpuSceneRenderer::updateWeatherDynamics() {
    m_gust = Scene::windGustFactor(m_cloudTime, m_weather.windGust);
    m_lightning = {};
    if (m_weather.lightning) {
        Scene::LightningSettings settings;
        settings.rate = m_weather.lightningRate;
        settings.intensity = m_weather.lightningIntensity;
        settings.distanceMin = m_weather.lightningDistanceMin;
        settings.distanceMax = m_weather.lightningDistanceMax;
        settings.seed = m_weather.lightningSeed;
        m_lightning = Scene::lightningAt(settings, m_cloudTime);
    }
}

u32 GpuSceneRenderer::pollThunder(Scene::ThunderEvent* out, u32 capacity) {
    if (!m_weather.lightning || !m_weather.thunder) {
        m_thunderLast = m_cloudTime;
        return 0;
    }
    if (m_cloudTime < m_thunderLast) m_thunderLast = 0.0f;  // the clock wrapped
    Scene::LightningSettings settings;
    settings.rate = m_weather.lightningRate;
    settings.intensity = m_weather.lightningIntensity;
    settings.distanceMin = m_weather.lightningDistanceMin;
    settings.distanceMax = m_weather.lightningDistanceMax;
    settings.seed = m_weather.lightningSeed;
    const u32 found = Scene::thunderBetween(settings, m_thunderLast, m_cloudTime, out, capacity);
    m_thunderLast = m_cloudTime;
    return std::min(found, capacity);
}

PostWeather GpuSceneRenderer::buildPostWeather(const GpuSceneCamera& camera, u32 width,
                                               u32 height) const {
    (void)width;
    (void)height;
    PostWeather wx;
    const Scene::WeatherResolved& w = m_weather;
    if (w.fog) {
        wx.fog = true;
        wx.fogDensity = w.fogDensity;
        wx.fogHeightFalloff = w.fogHeightFalloff;
        wx.fogBaseHeight = w.fogBaseHeight;
        wx.fogStart = w.fogStart;
        wx.fogMaxOpacity = w.fogMaxOpacity;
        wx.fogColor = w.fogColor;
        wx.fogSkyBlend = w.fogSkyBlend;
        wx.fogSunScatter = w.fogSunScatter;
        wx.fogSunExponent = w.fogSunExponent;
        wx.sunDirection = m_weatherSunDirection;
        wx.sunColor = m_weatherSunColor * (m_weatherSunIntensity * 0.5f);
        wx.horizon = Vec3(kSkyHorizon.x * w.skyScale * w.skyTint.x, kSkyHorizon.y * w.skyScale * w.skyTint.y,
                          kSkyHorizon.z * w.skyScale * w.skyTint.z);
        RHI::Texture* env = m_environment.texture();
        if (env && m_environmentExposure > 0.0f) {
            wx.environment = env;
            wx.environmentExposure = m_environmentExposure * w.skyScale;
            const u32 mips = m_environment.mipCount();
            wx.environmentLod = static_cast<f32>(std::min(mips > 0 ? mips - 1 : 0u, 6u));
        }
    }
    if (m_cloudSettings.enabled && m_cloudSettings.sunShafts > 0.0f) {
        // The sun's place on the screen (a far point along its direction), and whether it is in front of the camera.
        const Vec3 toward = m_weatherSunDirection;
        const Vec4 clip = (camera.proj * camera.view)
                              .transformVec4(Vec4(camera.position.x + toward.x * 1000.0f,
                                                  camera.position.y + toward.y * 1000.0f,
                                                  camera.position.z + toward.z * 1000.0f, 1.0f));
        if (clip.w > 1e-4f) {
            wx.sunAhead = true;
            wx.shaftSun = Vec2(clip.x / clip.w * 0.5f + 0.5f, 0.5f - clip.y / clip.w * 0.5f);
            wx.shafts = m_cloudSettings.sunShafts;
        }
    }
    if (m_lightning.flash > 0.0f) {
        wx.flash = m_lightning.flash;
        wx.flashColor = Vec3(0.78f, 0.84f, 1.0f);
    }
    if (w.rain) {
        wx.rain = true;
        wx.rainIntensity = w.rainIntensity;
        wx.rainDropLength = w.rainDropLength;
        wx.rainFallSpeed = w.rainSpeed;
        wx.rainBrightness = w.rainBrightness;
        // The wind's velocity in the world, surging with the gusts: the shader tilts the whole lattice of drops by it.
        const f32 angle = w.rainWindDirection * kPi / 180.0f;
        const f32 speed = w.rainWindSpeed * m_gust;
        wx.rainWind = Vec3(std::cos(angle) * speed, 0.0f, std::sin(angle) * speed);
        wx.rainSplashes = w.rainSplashes;
        const f32 level = std::clamp(0.12f + 0.30f * m_weatherSunIntensity, 0.08f, 0.6f);
        wx.rainColor = Vec3(0.75f * level * w.skyTint.x, 0.80f * level * w.skyTint.y,
                            0.92f * level * w.skyTint.z);
    }
    return wx;
}

void GpuSceneRenderer::drawGrid(RHI::CommandBuffer* cmd, const GpuSceneCamera& camera,
                                const GpuGridOverlay& grid) {
    if (!m_gridPipeline || grid.opacity <= 0.0f) return;
    GridUBO ubo{};
    const Mat4 vp = camera.proj * camera.view;
    const Mat4 inv = vp.inverted();
    std::memcpy(ubo.invViewProj, inv.data(), sizeof(ubo.invViewProj));
    std::memcpy(ubo.viewProj, vp.data(), sizeof(ubo.viewProj));
    setVec4(ubo.camera, camera.position.x, camera.position.y, camera.position.z,
            std::max(grid.fadeDistance, 1.0f));
    setVec4(ubo.params, std::max(grid.cellSize, 1e-3f), std::max(grid.majorEvery, 2.0f),
            std::clamp(grid.opacity, 0.0f, 1.0f), 1.0f);
    setVec4(ubo.minor, 0.20f, 0.20f, 0.22f, 0.45f);
    setVec4(ubo.major, 0.34f, 0.34f, 0.37f, 0.75f);
    setVec4(ubo.axisX, 0.75f, 0.10f, 0.10f, 0.95f);
    setVec4(ubo.axisZ, 0.10f, 0.22f, 0.80f, 0.95f);
    cmd->bindPipeline(m_gridPipeline);
    cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &ubo, sizeof(ubo));
    cmd->draw(3);
}

void GpuSceneRenderer::drawMeshList(RHI::CommandBuffer* cmd,
                                    const std::vector<const MeshDraw*>& draws,
                                    const GpuSceneCamera& camera,
                                    const Scene::SceneLighting& lighting,
                                    const std::string& projectRoot,
                                    const GpuSceneRenderOptions& options,
                                    const ScenePassContext& pass, RHI::Pipeline* basePipeline) {
    if (draws.empty()) return;
    const bool wireframe = options.wireframeMeshes;
    const RenderFeatureSettings& features = options.features;
    RHI::Texture* whiteTex = GpuTextureCache::instance().whiteTexture(m_device);
    RHI::Texture* fallbackCube = m_fallbackCube ? m_fallbackCube : whiteTex;
    auto& texCache = GpuTextureCache::instance();

    auto bindShadowTextures = [&](u32 dirBase, u32 pointBase, u32 spotBase) {
        for (u32 slot = 0; slot < GpuDirectionalShadowMap::kMaxDirectionalShadowLights; ++slot) {
            RHI::Texture* tex = m_directionalShadows.texture(slot);
            if (!tex) tex = whiteTex;
            if (tex) cmd->bindTexture(tex, dirBase + slot, m_sampler);
        }
        for (u32 slot = 0; slot < GpuPointShadowMap::kMaxPointShadowLights; ++slot) {
            RHI::Texture* cube = m_pointShadows.cubemap(slot);
            if (!cube) cube = fallbackCube;
            if (cube) cmd->bindTexture(cube, pointBase + slot, m_sampler);
        }
        for (u32 slot = 0; slot < GpuSpotShadowMap::kMaxSpotShadowLights; ++slot) {
            RHI::Texture* tex = m_spotShadows.texture(slot);
            if (!tex) tex = whiteTex;
            if (tex) cmd->bindTexture(tex, spotBase + slot, m_sampler);
        }
    };

    // Everything that is the same for every draw of the pass.
    LightingUBO base{};
    setVec4(base.cameraPos, camera.position.x, camera.position.y, camera.position.z, 0.0f);
    // Weather greys and dims the sky light: overcast or fog changes the ambient like it does Photon's sky.
    const Vec3 weatherSky = m_weather.skyTint * (m_weather.skyScale * (1.0f + 1.2f * m_lightning.flash));
    if (options.overrideAmbient) {
        setVec4(base.ambient, options.ambientColor.x * weatherSky.x, options.ambientColor.y * weatherSky.y,
                options.ambientColor.z * weatherSky.z, 0.0f);
    } else {
        setVec4(base.ambient, kFallbackAmbient.x * weatherSky.x, kFallbackAmbient.y * weatherSky.y,
                kFallbackAmbient.z * weatherSky.z, 0.0f);
    }
    base.dirCount = static_cast<int>(std::min(lighting.lights.directionals.size(), size_t{4}));
    for (int i = 0; i < base.dirCount; ++i) {
        const auto& d = lighting.lights.directionals[i];
        const Vec3 c = srgbToLinear(Vec3(d.color.x, d.color.y, d.color.z));
        setVec4(base.dirData + i * 4, d.direction.x, d.direction.y, d.direction.z, d.intensity);
        setVec4(base.dirColor + i * 4, c.x, c.y, c.z, 0.0f);
    }
    {
        // Cloud shadows: the forward shaders read the clouds' noise at the point where the sun's ray crosses the layer.
        const GpuCloudSettings& cs = m_cloudSettings;
        const bool shade = cs.enabled && cs.groundShadows && !wireframe;
        const f32 strength = shade ? std::max(cs.shadowStrength, m_weather.cloudShadowStrength) : 0.0f;
        const f32 windAngle = cs.windDirection * kPi / 180.0f;
        const f32 drift = cs.windSpeed * cs.time;
        setVec4(base.cloudShadowA, strength, cs.coverage, cs.density, 0.0f);
        setVec4(base.cloudShadowB, std::cos(windAngle) * drift + cs.seed, cs.seed, std::sin(windAngle) * drift + cs.seed,
                cs.baseAltitude);
        setVec4(base.cloudShadowC, cs.sunDirection.x, cs.sunDirection.y, cs.sunDirection.z, cs.topAltitude);
    }
    base.pointCount = static_cast<int>(std::min(lighting.lights.points.size(), size_t{4}));
    for (int i = 0; i < base.pointCount; ++i) {
        const auto& p = lighting.lights.points[i];
        const Vec3 c = srgbToLinear(Vec3(p.color.x, p.color.y, p.color.z));
        setVec4(base.pointData + i * 4, p.position.x, p.position.y, p.position.z, p.radius);
        setVec4(base.pointColor + i * 4, c.x, c.y, c.z, p.intensity);
    }
    base.spotCount = static_cast<int>(std::min(lighting.lights.spots.size(), size_t{4}));
    for (int i = 0; i < base.spotCount; ++i) {
        const auto& s = lighting.lights.spots[i];
        const Vec3 c = srgbToLinear(Vec3(s.color.x, s.color.y, s.color.z));
        setVec4(base.spotData + i * 4, s.position.x, s.position.y, s.position.z, s.radius);
        setVec4(base.spotDir + i * 4, s.direction.x, s.direction.y, s.direction.z, s.intensity);
        setVec4(base.spotColor + i * 4, c.x, c.y, c.z, 0.0f);
        const f32 halfAngle = std::clamp(s.angle * kPi / 180.0f * 0.5f, 0.01f, 1.5533f);
        base.spotAngle[i * 4] = std::cos(halfAngle);
    }

    const bool environmentOn = m_environment.texture() != nullptr && !wireframe;
    Vec3 irradiance =
        environmentOn ? m_environment.averageColor() * m_environmentExposure : kFallbackAmbient;
    irradiance = Vec3(irradiance.x * weatherSky.x, irradiance.y * weatherSky.y, irradiance.z * weatherSky.z);
    setVec4(base.iblColor, irradiance.x, irradiance.y, irradiance.z, features.iblSpecular);
    base.iblParams[0] = features.iblDiffuse;
    base.iblParams[1] = (features.iblEnabled && !wireframe) ? 1.0f : 0.0f;
    base.iblParams[3] = features.volumetricAnisotropy;
    base.extra[0] = features.volumetricShadows ? 1.0f : 0.0f;
    base.extra[1] = 6.0f;
    base.extra[2] = environmentOn ? std::max(m_environmentExposure, 0.0f) : 0.0f;
    base.extra[3] = environmentOn
        ? static_cast<f32>(m_environment.mipCount() > 0 ? m_environment.mipCount() - 1 : 0)
        : 0.0f;
    base.volParams[0] = features.volumetrics == VolumetricQuality::Off ? 0.0f : 1.0f;
    base.volParams[1] = features.volumetricDensity;
    base.volParams[2] = features.volumetricHeight;
    base.volParams[3] = features.volumetrics == VolumetricQuality::High   ? 24.0f
                        : features.volumetrics == VolumetricQuality::Medium ? 12.0f
                        : features.volumetrics == VolumetricQuality::Low  ? 6.0f
                                                                          : 0.0f;
    const bool planarOn = m_reflectionValid && m_reflectionColor && !m_inReflectionPass &&
                          !options.clipBelowEnabled &&
                          (features.reflections == ReflectionMode::Planar || m_materialPlanar);
    base.reflectParams[0] = planarOn ? features.reflectionIntensity : 0.0f;
    base.reflectParams[1] = options.clipBelowEnabled ? options.clipBelowY : features.reflectionPlaneY;
    base.reflectParams[2] = options.clipBelowEnabled ? 1.0f : 0.0f;
    base.reflectParams[3] = 0.5f;
    std::memcpy(base.reflectVP, m_reflectionVP.data(), sizeof(base.reflectVP));

    ViewTargets* view = pass.view;
    const bool historyOn = view && view->hasPrevious && !m_inReflectionPass;
    const bool ssrOn = historyOn && pass.ssrSteps > 0 && !wireframe;
    setVec4(base.ssrParams, pass.ssrIntensity, pass.ssrMaxRoughness,
            static_cast<f32>(pass.ssrSteps), pass.ssrMaxDistance);
    if (historyOn) {
        base.projParams[0] = view->prevProj(2, 2);
        base.projParams[1] = view->prevProj(2, 3);
        std::memcpy(base.prevViewProj, view->prevViewProj.data(), sizeof(base.prevViewProj));
        std::memcpy(base.prevView, view->prevView.data(), sizeof(base.prevView));
    }
    base.screen[0] = 1.0f / static_cast<f32>(std::max(view ? view->width : 1u, 1u));
    base.screen[1] = 1.0f / static_cast<f32>(std::max(view ? view->height : 1u, 1u));
    base.screen[2] = static_cast<f32>(view ? view->frameIndex % 64u : 0u);
    std::memcpy(base.viewProj, pass.viewProj.data(), sizeof(base.viewProj));

    RHI::Texture* prevColor = historyOn ? view->color[view->current ^ 1u] : whiteTex;
    RHI::Texture* prevDepth = historyOn ? view->depth[view->current ^ 1u] : whiteTex;
    RHI::Texture* opaqueScene =
        (pass.opaqueCopyBound && view && view->opaqueCopy) ? view->opaqueCopy : whiteTex;
    RHI::Texture* reflectionTex = planarOn ? m_reflectionColor : whiteTex;
    RHI::Texture* envTex = m_environment.texture() ? m_environment.texture() : whiteTex;

    ECS::Entity lastTerrainTexEntity = ECS::Entity::INVALID;
    u32 lastTerrainTexTier = ~0u;

    for (const MeshDraw* drawPtr : draws) {
        const MeshDraw& draw = *drawPtr;
        if (draw.skipColorDraw || !draw.mesh || draw.mesh->indices.empty()) continue;
        if (!draw.mesh->vertexBuffer || !draw.mesh->indexBuffer) continue;

        CF_PROFILE_SCOPE(draw.isTerrain ? "GpuSceneRenderer::terrain" : "GpuSceneRenderer::mesh");
        RHI::Pipeline* pipeline = basePipeline;
        const bool displaced = draw.displaced && !wireframe && !draw.isTerrain &&
                               m_displacePipeline && basePipeline == m_scenePipeline;
        if (displaced) pipeline = m_displacePipeline;
        if (wireframe) pipeline = m_wireframePipeline;
        else if (draw.isTerrain && m_terrainPipeline) pipeline = m_terrainPipeline;
        // Per-vertex colour (baked occlusion, painted wind weight) comes from a second stream.
        const bool coloured = !wireframe && !displaced && !draw.isTerrain && m_foliagePipeline &&
                              draw.mesh->colorBuffer && basePipeline == m_scenePipeline;
        if (coloured) pipeline = m_foliagePipeline;
        const bool blendedPass = basePipeline == m_blendPipeline;
        const bool gpuWater = draw.gpuWater && !wireframe && m_waterPipeline && m_waterBlendPipeline &&
                              (blendedPass || basePipeline == m_scenePipeline);
        if (gpuWater) pipeline = blendedPass ? m_waterBlendPipeline : m_waterPipeline;
        // A Micromesh draw: its cull pass ran before this render pass (see beginMicromeshFrame).
        const MicromeshDrawRef* microRef = nullptr;
        if (!wireframe && !draw.isTerrain && !gpuWater && m_micromeshMainView != ~0u && !m_inReflectionPass) {
            microDrawParts(drawPtr, microRef);
        }
        const bool micro = microRef != nullptr;
        if (micro) pipeline = m_micromeshPipeline;
        cmd->bindPipeline(pipeline);

        LightingUBO lights = base;
        const u32 texTier = textureQualityTier(draw.viewerDistance, options.textureQuality);
        if (wireframe) {
            setVec4(lights.ambient, 0.45f, 0.52f, 0.66f, 0.0f);
            setVec4(lights.albedo, 0.30f, 0.36f, 0.48f, 1.0f);
            lights.roughness = 1.0f;
            lights.reflectance = 0.04f;
            lights.dirCount = 0;
            lights.pointCount = 0;
            lights.spotCount = 0;
            setVec4(lights.uvTransform, 1.0f, 1.0f, 0.0f, 0.0f);
            setVec4(lights.materialParams, 1.0f, 1.0f, 0.0f, 0.5f);
        } else {
            setVec4(lights.albedo, draw.albedo.x, draw.albedo.y, draw.albedo.z, draw.albedo.w);
            lights.metallic = draw.metallic;
            lights.roughness = draw.roughness;
            lights.reflectance = draw.reflectance;
            lights.flags[0] = draw.receiveShadows ? 1.0f : 0.0f;
            lights.flags[1] = draw.customNormalPath.empty() ? 0.0f : 1.0f;
            lights.flags[2] = (planarOn && draw.planarReceiver) ? 1.0f : 0.0f;
            lights.flags[3] = draw.ormMapPath.empty() ? 0.0f : 1.0f;
            setVec4(lights.emission, draw.emission.x, draw.emission.y, draw.emission.z, 0.0f);
            setVec4(lights.uvTransform, draw.uvTiling.x, draw.uvTiling.y, draw.uvOffset.x,
                    draw.uvOffset.y);
            lights.alignPad = draw.uvObjectScale ? 1 : 0; // shader: uUvObjectScale
            f32 thickness = 0.5f;
            if (draw.hasAabb) {
                const Vec3 extent = draw.aabbMax - draw.aabbMin;
                thickness = std::max(0.01f, std::min(extent.x, std::min(extent.y, extent.z)) * 0.5f);
            }
            setVec4(lights.materialParams, draw.normalStrength, draw.aoStrength, thickness,
                    draw.alphaCutoff);
            // uProjParams.w is unused by the shader; carry the normal-map convention.
            lights.projParams[3] = draw.normalDirectX ? 1.0f : 0.0f;
            setVec4(lights.coat, draw.clearcoat, draw.clearcoatRoughness, draw.ior,
                    draw.transmission);
            setVec4(lights.foliage, draw.translucency, 0.0f, 0.0f, 0.0f);
            setVec4(lights.modifiers, std::max(draw.modWetness, m_weather.wetness), draw.modSnow, draw.modAge, 0.0f);
            setVec4(lights.modTint, draw.modTint.x, draw.modTint.y, draw.modTint.z, 1.0f);
            setVec4(lights.water, draw.isWater ? 1.0f : 0.0f, draw.gpuWater ? 1.0f : 0.0f, 0.4f,
                    draw.waterTime);
            setVec4(lights.sheen, draw.sheenColor.x, draw.sheenColor.y, draw.sheenColor.z,
                    draw.sheenRoughness);
            // iridescence.w is free: it carries the parallax depth (needs a normal map whose
            // alpha channel holds the height; the shader skips it otherwise).
            setVec4(lights.iridescence, draw.iridescence, draw.iridescenceThickness,
                    draw.iridescenceIor, draw.customNormalPath.empty() ? 0.0f : draw.heightScale);
            setVec4(lights.materialFlags, static_cast<f32>(draw.alphaMode),
                    draw.emissionMapPath.empty() ? 0.0f : 1.0f, pass.opaqueCopyBound ? 1.0f : 0.0f,
                    ssrOn ? 1.0f : 0.0f);
            if (draw.ssrStepsOverride >= 0) {
                lights.ssrParams[1] = draw.ssrRoughnessGate > 0.0f ? draw.ssrRoughnessGate : lights.ssrParams[1];
                lights.ssrParams[2] = static_cast<f32>(draw.ssrStepsOverride);
                if (draw.ssrStepsOverride == 0) lights.materialFlags[3] = 0.0f;
            }
            setVec4(lights.reflectPerf, draw.reflectPerformance ? 1.0f : 0.0f, draw.ssrResolution,
                    draw.ssrMaxSteps, draw.ssrTemporalFrames);
            setVec4(lights.reflectQuality, draw.reflectQuality ? 1.0f : 0.0f, draw.ssrSamples,
                    draw.ssrDenoise, draw.ssrDistance);
            setVec4(lights.reflectExtra, draw.planarReceiver ? 1.0f : 0.0f, draw.ssrProbeBlend,
                    draw.ssrBounces, draw.parallaxSteps);
        }

        if (!wireframe && draw.isTerrain) {
            CF_PROFILE_SCOPE("terrain::material");
            lights.roughness = 1.0f;
            lights.reflectance = 0.04f;
            TerrainMaterialUBO mat{};
            if (draw.terrainSettings) {
                mat.worldSize[0] = draw.terrainSettings->worldSizeX;
                mat.worldSize[1] = draw.terrainSettings->worldSizeZ;
                mat.worldSize[2] = draw.terrainSettings->useSplatmap
                    ? draw.terrainSettings->splatTileSize
                    : draw.terrainSettings->textureTileSize;
                const Mat4 inv = draw.worldMatrix.inverted();
                std::memcpy(mat.modelInv, inv.data(), sizeof(mat.modelInv));
            }
            const Terrain::TerrainGpuTextures* gpu =
                Terrain::TerrainCache::instance().gpuTexturesFor(draw.entity);
            const bool layered = gpu && gpu->useLayers && gpu->layerArray && gpu->indexMap && gpu->weightMap;
            if (layered) {
                mat.flags[2] = 1.0f;
                std::memcpy(mat.layerTile, gpu->layerTile, sizeof(mat.layerTile));
                std::memcpy(mat.layerRot, gpu->layerRotation, sizeof(mat.layerRot));
            } else if (gpu && gpu->useSplatmap) {
                mat.flags[0] = 1.0f;
            } else if (gpu && (!gpu->cachedAlbedoPath.empty() || gpu->albedo)) {
                mat.flags[1] = 1.0f;
            }
            if (gpu && (!gpu->cachedNormalPath.empty() || gpu->normalMap)) {
                lights.flags[1] = 1.0f;
            }

            if (draw.entity != lastTerrainTexEntity || texTier != lastTerrainTexTier) {
                CF_PROFILE_SCOPE("terrain::textures");
                lastTerrainTexEntity = draw.entity;
                lastTerrainTexTier = texTier;
                for (u32 slot = 0; slot < 6; ++slot) {
                    if (whiteTex) cmd->bindTexture(whiteTex, slot, m_repeatSampler);
                }
                // The layered maps (slots 12 to 14) are always bound, plain white when there are none: a pipeline
                // must find every sampler it declares.
                RHI::Texture* whiteArray = Render::GpuTextureCache::instance().whiteTextureArray(m_device);
                cmd->bindTexture(layered ? gpu->indexMap : whiteTex, 12, m_pointSampler);
                cmd->bindTexture(layered ? gpu->weightMap : whiteTex, 13, m_pointSampler);
                if (layered ? gpu->layerArray != nullptr : whiteArray != nullptr) {
                    cmd->bindTexture(layered ? gpu->layerArray : whiteArray, 14, m_repeatSampler);
                }
                if (gpu && gpu->useSplatmap && !layered) {
                    if (gpu->splatMap) cmd->bindTexture(gpu->splatMap, 0, m_sampler);
                    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
                        RHI::Texture* layerTex = whiteTex;
                        if (!gpu->cachedLayerPaths[i].empty()) {
                            layerTex = texCache.acquire(m_device, gpu->cachedLayerPaths[i],
                                                        projectRoot, texTier, true);
                        } else if (gpu->layers[i]) {
                            layerTex = gpu->layers[i];
                        }
                        if (layerTex) cmd->bindTexture(layerTex, 1 + i, m_repeatSampler);
                    }
                } else if (gpu) {
                    RHI::Texture* albedoTex = whiteTex;
                    if (!gpu->cachedAlbedoPath.empty()) {
                        albedoTex = texCache.acquire(m_device, gpu->cachedAlbedoPath, projectRoot,
                                                     texTier, true);
                    } else if (gpu->albedo) {
                        albedoTex = gpu->albedo;
                    }
                    if (albedoTex) cmd->bindTexture(albedoTex, 4, m_repeatSampler);
                }
                if (gpu) {
                    RHI::Texture* normalTex = nullptr;
                    if (!gpu->cachedNormalPath.empty()) {
                        normalTex = texCache.acquire(m_device, gpu->cachedNormalPath, projectRoot,
                                                     texTier);
                    } else if (gpu->normalMap) {
                        normalTex = gpu->normalMap;
                    }
                    if (normalTex) cmd->bindTexture(normalTex, 5, m_repeatSampler);
                }
            }
            {
                CF_PROFILE_SCOPE("terrain::shadowBind");
                bindShadowTextures(6, 8, 10);
            }
            cmd->pushUniformData(RHI::ShaderStage::Fragment, 2, &mat, sizeof(mat));
        }

        const Mat4 mvp = pass.viewProj * draw.worldMatrix;
        if (displaced) {
            DisplaceVertexUBO dubo{};
            std::memcpy(dubo.mvp, mvp.data(), sizeof(dubo.mvp));
            std::memcpy(dubo.model, draw.worldMatrix.data(), sizeof(dubo.model));
            dubo.disp[0] = draw.displacementScale;
            dubo.disp[1] = draw.displacementEps;
            dubo.disp[2] = draw.displacementMetresPerUv;
            dubo.disp[3] = draw.uvObjectScale ? 1.0f : 0.0f;
            dubo.uv[0] = draw.uvTiling.x;
            dubo.uv[1] = draw.uvTiling.y;
            dubo.uv[2] = draw.uvOffset.x;
            dubo.uv[3] = draw.uvOffset.y;
            RHI::Texture* heightTex = draw.displacementTexture;
            if (heightTex) cmd->bindVertexTexture(heightTex, 0, m_repeatSampler);
            cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &dubo, sizeof(dubo));
        } else if (gpuWater) {
            WaterVertexUBO wubo{};
            std::memcpy(wubo.mvp, mvp.data(), sizeof(wubo.mvp));
            std::memcpy(wubo.model, draw.worldMatrix.data(), sizeof(wubo.model));
            wubo.params[0] = draw.waterTime;
            wubo.params[1] = static_cast<f32>(draw.waveCount);
            wubo.params[2] = draw.waterFoamThreshold;
            std::memcpy(wubo.waves, draw.waveData, sizeof(wubo.waves));
            cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &wubo, sizeof(wubo));
        } else if (micro) {
            float viewProj[16];
            std::memcpy(viewProj, pass.viewProj.data(), sizeof(viewProj));
            cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, viewProj, sizeof(viewProj));
        } else {
            VertexUBO vubo{};
            std::memcpy(vubo.mvp, mvp.data(), sizeof(vubo.mvp));
            std::memcpy(vubo.model, draw.worldMatrix.data(), sizeof(vubo.model));
            setWind(vubo.wind, draw);
            cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &vubo, sizeof(vubo));
        }
        cmd->bindVertexBuffer(vertexBufferFor(draw));
        if (coloured) cmd->bindVertexBuffer(draw.mesh->colorBuffer, 1);
        cmd->bindIndexBuffer(draw.mesh->indexBuffer);

        if (draw.isTerrain) {
            CF_PROFILE_SCOPE("terrain::submit");
            if (wireframe) {
                for (u32 slot = 0; slot < 6; ++slot) {
                    if (whiteTex) cmd->bindTexture(whiteTex, slot, m_repeatSampler);
                }
                bindShadowTextures(6, 8, 10);
            }
            cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &lights, sizeof(lights));
            cmd->drawIndexed(static_cast<u32>(draw.mesh->indices.size()));
            // (Terrain used to skip the frame counters: it showed as 0 triangles and 0 draws.)
            ++m_building.drawCalls;
            m_building.triangles += draw.mesh->indices.size() / 3;
            continue;
        }

        // Per-object probe, else sky.
        RHI::Texture* probeTex = fallbackCube;
        if (!m_inReflectionPass && !wireframe && draw.probeIndex >= 0 &&
            static_cast<usize>(draw.probeIndex) < m_probes.size()) {
            const ReflectionProbe& probe = m_probes[static_cast<usize>(draw.probeIndex)];
            if (probe.valid && probe.cube) {
                probeTex = probe.cube;
                lights.iblParams[2] = 1.0f;
                lights.projParams[2] = static_cast<f32>(probe.mipLevels > 0 ? probe.mipLevels - 1 : 0);
            }
        }

        bindShadowTextures(2, 4, 6);
        if (reflectionTex) cmd->bindTexture(reflectionTex, 8, m_sampler);
        if (probeTex) cmd->bindTexture(probeTex, 9, m_sampler);
        if (envTex) cmd->bindTexture(envTex, 11, m_repeatSampler);
        if (prevColor) cmd->bindTexture(prevColor, 12, m_sampler);
        if (prevDepth) cmd->bindTexture(prevDepth, 13, m_pointSampler);
        if (opaqueScene) cmd->bindTexture(opaqueScene, 15, m_sampler);

        RHI::Texture* normalTex = whiteTex;
        RHI::Texture* ormTex = whiteTex;
        RHI::Texture* emissionTex = whiteTex;
        if (!wireframe) {
            if (!draw.customNormalPath.empty()) {
                if (RHI::Texture* t = texCache.acquire(m_device, draw.customNormalPath, projectRoot, texTier)) {
                    normalTex = t;
                }
            }
            if (!draw.ormMapPath.empty()) {
                if (RHI::Texture* t = texCache.acquire(m_device, draw.ormMapPath, projectRoot, texTier)) {
                    ormTex = t;
                }
            }
            if (!draw.emissionMapPath.empty()) {
                if (RHI::Texture* t = texCache.acquire(m_device, draw.emissionMapPath, projectRoot,
                                                       texTier, true)) {
                    emissionTex = t;
                }
            }
        }
        if (normalTex) cmd->bindTexture(normalTex, 1, m_repeatSampler);
        if (ormTex) cmd->bindTexture(ormTex, 10, m_repeatSampler);
        if (emissionTex) cmd->bindTexture(emissionTex, 14, m_repeatSampler);

        auto drawMeshRange = [&](u32 firstIndex, u32 indexCount, u32 materialIndex, u32 partIndex) {
            if (indexCount == 0) return;
            LightingUBO rangeLights = lights;
            RHI::Texture* albedoTex = whiteTex;
            u8 rangeAlphaMode = draw.alphaMode;
            f32 rangeAlphaCutoff = draw.alphaCutoff;
            if (!wireframe) {
                const ResolvedMeshAlbedo albedo =
                    resolveMeshAlbedo(m_device, draw.mesh, draw.meshPath, projectRoot,
                                      draw.customTexturePath, materialIndex, texTier);
                const Vec4 factor = draw.hasAuthoredMaterial ? Vec4(1.0f, 1.0f, 1.0f, 1.0f) : albedo.factor;
                rangeLights.albedo[0] = draw.albedo.x * factor.x;
                rangeLights.albedo[1] = draw.albedo.y * factor.y;
                rangeLights.albedo[2] = draw.albedo.z * factor.z;
                rangeLights.albedo[3] = draw.albedo.w * factor.w;
                if (!draw.hasAuthoredMaterial && materialIndex < draw.mesh->materials.size()) {
                    rangeLights.metallic = albedo.metallic;
                    rangeLights.roughness = albedo.roughness;
                    rangeAlphaMode = albedo.alphaMode;
                    rangeAlphaCutoff = albedo.alphaCutoff;
                }
                if (albedo.texture) albedoTex = albedo.texture;
            }
            // Alpha mode is per glTF material, not per mesh: a trunk can be opaque while
            // its leaf submesh is cutout. Set it on the per-range uniform copy.
            rangeLights.materialFlags[0] = static_cast<f32>(rangeAlphaMode);
            rangeLights.materialParams[3] = rangeAlphaCutoff;
            if (albedoTex) cmd->bindTexture(albedoTex, 0, m_repeatSampler);
            cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &rangeLights, sizeof(rangeLights));

            if (micro) {
                // One indexed draw of this instance's compacted cut (sub-mesh `partIndex`'s asset).
                if (partIndex < microRef->parts.size()) {
                    MicromeshAsset* asset = microRef->parts[partIndex].first;
                    RHI::Buffer* storage[1] = {asset->gpu.instanceBuffer()};
                    cmd->bindVertexStorageBuffers(storage, 0, 1);
                    asset->gpu.drawSceneSlot(cmd, m_micromeshArena, m_micromeshMainView,
                                             microRef->parts[partIndex].second);
                    ++m_building.drawCalls;
                }
                return;
            }
            RHI::Buffer* const instanceSource = draw.batchBuffer ? draw.batchBuffer : m_instanceBuffer;
            const bool instanced = !wireframe && !displaced && draw.batchCount > 1 && instanceSource &&
                                   (coloured ? m_foliageInstancedPipeline : m_instancedPipeline);
            if (instanced) {
                VertexUBO instUbo{};
                std::memcpy(instUbo.mvp, pass.viewProj.data(), sizeof(instUbo.mvp));
                setWind(instUbo.wind, draw);
                cmd->bindPipeline(coloured ? m_foliageInstancedPipeline : m_instancedPipeline);
                cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &instUbo, sizeof(instUbo));
                cmd->bindVertexBuffer(draw.mesh->vertexBuffer, 0);
                cmd->bindVertexBuffer(instanceSource, 1, draw.batchByteOffset);
                if (coloured) cmd->bindVertexBuffer(draw.mesh->colorBuffer, 2);
                cmd->bindIndexBuffer(draw.mesh->indexBuffer);
                cmd->drawIndexedInstanced(indexCount, draw.batchCount, firstIndex, 0, 0);
                ++m_building.drawCalls;
                ++m_building.instancedDrawCalls;
                m_building.triangles += static_cast<u64>(indexCount / 3) * draw.batchCount;
            } else {
                cmd->drawIndexed(indexCount, firstIndex, 0);
                ++m_building.drawCalls;
                m_building.triangles += indexCount / 3;
            }
            if (gpuWater) ++m_building.gpuWaterDraws;
        };

        if (draw.mesh->subMeshes.size() > 1) {
            u32 partIndex = 0;
            u32 subIndex = 0;
            for (const Assets::SubMesh& submesh : draw.mesh->subMeshes) {
                const u32 current = subIndex++;
                if (draw.onlySubMesh >= 0 && static_cast<u32>(draw.onlySubMesh) != current) continue;
                drawMeshRange(submesh.indexOffset, submesh.indexCount, submesh.materialIndex, partIndex++);
            }
        } else {
            const u32 materialIndex =
                draw.mesh->subMeshes.empty() ? 0u : draw.mesh->subMeshes[0].materialIndex;
            drawMeshRange(0, static_cast<u32>(draw.mesh->indices.size()), materialIndex, 0u);
        }
    }
}

GpuSceneRenderer::MicromeshAsset* GpuSceneRenderer::micromeshAssetFor(Assets::Mesh3D* mesh, u32 subMesh,
                                                                        u32 firstIndex, u32 indexCount) {
    if (!mesh || !m_device) return nullptr;
    std::unique_ptr<MicromeshAsset>& slot = m_micromeshAssets[{mesh, subMesh}];
    if (slot && slot->revision == mesh->geometryRevision) return slot->ready ? slot.get() : nullptr;
    // (Re)build: once per mesh asset, sub-mesh and geometry revision. It is baking work: a mesh of ~80 000
    // triangles takes about a second on the first frame it is seen (an offline `.micromesh` section in
    // the `.caf` is the planned way to not pay it at run time).
    slot = std::make_unique<MicromeshAsset>();
    slot->revision = mesh->geometryRevision;
    const size_t total = mesh->indices.size();
    if (mesh->vertices.empty() || firstIndex >= total || indexCount < 3) return nullptr;
    const size_t last = std::min<size_t>(total, static_cast<size_t>(firstIndex) + indexCount);
    // Meshes are often stored unindexed (three vertices per triangle) or with a vertex per face corner: weld
    // the vertices that agree on position, normal and UV, or no edge would be shared and every edge would
    // look like a boundary (nothing could be simplified). A UV seam or a hard edge differs in UV or normal,
    // so it stays split, and stays locked. Only the vertices this sub-mesh uses are kept.
    struct WeldKey {
        float v[8];
        bool operator==(const WeldKey& o) const { return std::memcmp(v, o.v, sizeof(v)) == 0; }
    };
    struct WeldHash {
        size_t operator()(const WeldKey& k) const {
            u64 h = 1469598103934665603ull;
            for (float f : k.v) {
                u32 bits;
                std::memcpy(&bits, &f, sizeof(bits));
                h = (h ^ bits) * 1099511628211ull;
            }
            return static_cast<size_t>(h);
        }
    };
    std::unordered_map<WeldKey, u32, WeldHash> welded;
    std::vector<Vec3> positions, normals;
    std::vector<Vec2> uvs;
    std::vector<Vec4> tangents;
    std::vector<Vec4> colors;
    const bool coloured = mesh->vertexColors.size() == mesh->vertices.size();
    std::vector<u32> indices;
    indices.reserve(last - firstIndex);
    for (size_t k = firstIndex; k < last; ++k) {
        const u32 original = mesh->indices[k];
        if (original >= mesh->vertices.size()) return nullptr;
        const Assets::Vertex3D& v = mesh->vertices[original];
        const WeldKey key{{v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z,
                           v.texcoord.x, v.texcoord.y}};
        const auto [it, inserted] = welded.emplace(key, static_cast<u32>(positions.size()));
        if (inserted) {
            positions.push_back(v.position);
            normals.push_back(v.normal);
            uvs.push_back(v.texcoord);
            tangents.push_back(v.tangent);
            colors.push_back(coloured ? mesh->vertexColors[original] : Vec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            // The same vertex seen again (another face): average the tangents.
            Vec4& t = tangents[it->second];
            const Vec3 sum(t.x + v.tangent.x, t.y + v.tangent.y, t.z + v.tangent.z);
            const f32 len = sum.length();
            if (len > 1e-6f) t = Vec4(sum.x / len, sum.y / len, sum.z / len, t.w);
        }
        indices.push_back(it->second);
    }
    Micromesh::Attributes attributes;
    attributes.normals = &normals;
    attributes.uvs = &uvs;
    attributes.tangents = &tangents;
    attributes.colors = &colors;
    const Micromesh::Data data = Micromesh::build(positions, indices, attributes);
    // A mesh that reduces to one level has nothing to cut: draw it as a normal mesh.
    if (data.clusters.empty() || data.levelCount < 2) return nullptr;
    if (!slot->gpu.init(m_device) || !slot->gpu.upload(data) || !slot->gpu.hasSceneAttributes()) return nullptr;
    slot->ready = true;
    return slot.get();
}

void GpuSceneRenderer::beginMicromeshFrame(RHI::CommandBuffer* cmd, const std::vector<MeshDraw>& draws) {
    m_micromeshDraws.clear();
    m_micromeshNextView = 0;
    m_micromeshMainView = ~0u;
    ++m_micromeshFrame;
    for (auto& entry : m_micromeshAssets) {
        if (entry.second) {
            entry.second->users.clear();
            entry.second->viewsUsed = 0;
        }
    }
    if (!m_micromeshPipeline || !cmd) return;
    bool any = false;
    for (const MeshDraw& draw : draws) any = any || draw.micromeshPixels > 0.0f;
    if (!any) return;
    if (!m_micromeshArena.ready() && !m_micromeshArena.init(m_device, kMicromeshArenaStart)) return;
    // The arena grows to what the scene needs: every few frames, read back last frame's allocation counter (it
    // counts what was asked for, also past the capacity). A cut that did not fit was not drawn that frame, so
    // growth shows within a few frames.
    if (m_micromeshFrame % 15 == 0) {
        const u32 asked = m_micromeshArena.readUsed();
        const u32 capacity = m_micromeshArena.capacity();
        if (asked + asked / 8u > capacity && capacity < kMicromeshArenaMax) {
            u32 grown = capacity;
            while (grown < kMicromeshArenaMax && asked + asked / 8u > grown) grown *= 2u;
            m_micromeshArena.init(m_device, std::min(grown, kMicromeshArenaMax));
        }
    }

    // Group by asset: every entity of a mesh (and sub-mesh) is one instance of its asset.
    for (const MeshDraw& draw : draws) {
        if (draw.micromeshPixels <= 0.0f || draw.skipColorDraw || !draw.mesh || draw.isTerrain || draw.displaced ||
            draw.isWater || draw.skinnedVertices || draw.windStrength > 0.0f || draw.windFlutter > 0.0f ||
            draw.isTranslucent() || draw.alphaMode == 1 || !draw.mesh->vertexBuffer || !draw.mesh->indexBuffer) {
            continue;
        }
        Assets::Mesh3D* mesh = draw.mesh;
        MicromeshDrawRef ref;
        bool ok = true;
        auto addPart = [&](u32 subMesh, u32 firstIndex, u32 indexCount) {
            MicromeshAsset* asset = micromeshAssetFor(mesh, subMesh, firstIndex, indexCount);
            if (!asset) {
                ok = false;
                return;
            }
            ref.parts.emplace_back(asset, static_cast<u32>(asset->users.size()));
            asset->users.push_back(&draw);
        };
        if (mesh->subMeshes.size() > 1) {
            for (u32 s = 0; s < mesh->subMeshes.size() && ok; ++s) {
                if (draw.onlySubMesh >= 0 && static_cast<u32>(draw.onlySubMesh) != s) continue;
                addPart(s, mesh->subMeshes[s].indexOffset, mesh->subMeshes[s].indexCount);
            }
        } else {
            addPart(0u, 0u, static_cast<u32>(mesh->indices.size()));
        }
        if (!ok) {
            // A sub-mesh that cannot be cut (too small, build failure): the whole entity draws plain. Undo
            // the parts already added.
            for (auto& part : ref.parts) {
                if (!part.first->users.empty() && part.first->users.back() == &draw) part.first->users.pop_back();
            }
            continue;
        }
        m_micromeshDraws[&draw] = std::move(ref);
    }
    // Upload every asset's matrices (one buffer per asset, once per frame) and the arena reset.
    for (auto& entry : m_micromeshAssets) {
        MicromeshAsset* asset = entry.second.get();
        if (!asset || !asset->ready || asset->users.empty()) continue;
        std::vector<Mat4> transforms;
        std::vector<u8> occlusion;
        transforms.reserve(asset->users.size());
        for (const MeshDraw* user : asset->users) {
            transforms.push_back(user->worldMatrix);
            occlusion.push_back(user->micromeshOcclusion ? 1u : 0u);
        }
        if (!asset->gpu.setSceneInstances(transforms, kMicromeshViews, occlusion)) {
            // Could not size its buffers: drop its users from the frame (they draw plain).
            for (const MeshDraw* user : asset->users) m_micromeshDraws.erase(user);
            asset->users.clear();
        }
    }
    m_building.micromeshDraws = static_cast<u32>(m_micromeshDraws.size());
    m_micromeshArena.beginFrame(cmd);
}

u32 GpuSceneRenderer::cullMicromeshView(RHI::CommandBuffer* cmd, const MicromeshCullParams& params) {
    if (!cmd || m_micromeshDraws.empty() || m_micromeshNextView >= kMicromeshViews) return ~0u;
    const u32 view = m_micromeshNextView++;
    for (auto& entry : m_micromeshAssets) {
        MicromeshAsset* asset = entry.second.get();
        if (!asset || !asset->ready || asset->users.empty()) continue;
        if (asset->gpu.cullScene(cmd, params, m_micromeshArena, view)) asset->viewsUsed = view + 1;
    }
    return view;
}

bool GpuSceneRenderer::microDrawParts(const MeshDraw* draw, const MicromeshDrawRef*& out) const {
    const auto it = m_micromeshDraws.find(draw);
    if (it == m_micromeshDraws.end()) return false;
    out = &it->second;
    return true;
}

GpuSceneRenderer::MicromeshFrameStats GpuSceneRenderer::readMicromeshStats() {
    MicromeshFrameStats total;
    if (m_micromeshMainView == ~0u) return total;
    total.draws = static_cast<u32>(m_micromeshDraws.size());
    total.views = m_micromeshNextView;
    total.occlusionActive = m_micromeshOcclusionActive;
    total.arenaUsed = m_micromeshArena.readUsed();
    total.arenaCapacity = m_micromeshArena.capacity();
    for (auto& entry : m_micromeshAssets) {
        MicromeshAsset* asset = entry.second.get();
        if (!asset || !asset->ready || asset->users.empty()) continue;
        for (u32 view = 0; view < asset->viewsUsed; ++view) {
            const MicromeshGpu::SceneViewStats stats = asset->gpu.readSceneView(view);
            total.overflowed += stats.overflowed;
            if (view != m_micromeshMainView) continue;
            total.clusters += stats.visibleClusters;
            total.triangles += stats.triangles;
        }
        total.sourceTriangles += static_cast<u64>(asset->gpu.sourceTriangles()) * asset->users.size();
    }
    return total;
}

void GpuSceneRenderer::releaseMicromeshes() {
    m_micromeshDraws.clear();
    m_micromeshAssets.clear();
    m_micromeshArena.shutdown();
    m_micromeshHzb.shutdown();
}

u32 GpuSceneRenderer::renderMeshes(RHI::CommandBuffer* cmd, const std::vector<MeshDraw>& draws,
                                   const GpuSceneCamera& camera,
                                   const Scene::SceneLighting& lighting,
                                   RHI::Texture* colorTarget, RHI::Texture* depthTarget,
                                   u32 width, u32 height, const std::string& projectRoot,
                                   const GpuSceneRenderOptions& options, ScenePassContext& pass) {
    Caffeine::Debug::setCrashBreadcrumb("GpuSceneRenderer::renderMeshes");
    if (!m_scenePipeline || !m_wireframePipeline || !colorTarget || !depthTarget) return 0;
    // Every scene pipeline declares four colour attachments; a pass without a
    // G-buffer would be an invalid framebuffer.
    if (!pass.gbuffer.valid()) return 0;
    const bool wireframe = options.wireframeMeshes;

    SceneShadowUBO shadowUbo{};
    std::memcpy(shadowUbo.cameraView, camera.view.data(), sizeof(shadowUbo.cameraView));
    for (u32 slot = 0; slot < GpuDirectionalShadowMap::kMaxDirectionalShadowLights; ++slot) {
        if (!m_directionalShadows.valid(slot)) continue;
        const u32 cascades = m_directionalShadows.cascadeCount(slot);
        for (u32 c = 0; c < cascades; ++c) {
            std::memcpy(shadowUbo.dirShadowVP + (slot * 4 + c) * 16,
                        m_directionalShadows.cascadeVP(slot, c).data(), sizeof(float) * 16);
        }
        const f32* splits = m_directionalShadows.cascadeSplits(slot);
        shadowUbo.dirCascadeSplits[slot * 4 + 0] = splits[1];
        shadowUbo.dirCascadeSplits[slot * 4 + 1] = splits[2];
        shadowUbo.dirCascadeSplits[slot * 4 + 2] = splits[3];
        shadowUbo.dirCascadeSplits[slot * 4 + 3] = static_cast<f32>(cascades);
        shadowUbo.dirShadowValid[slot] = 1.0f;
    }
    for (u32 slot = 0; slot < GpuPointShadowMap::kMaxPointShadowLights; ++slot) {
        if (!m_pointShadows.valid(slot)) continue;
        const Vec3 pos = m_pointShadows.lightPosition(slot);
        shadowUbo.pointShadowPos[slot * 4 + 0] = pos.x;
        shadowUbo.pointShadowPos[slot * 4 + 1] = pos.y;
        shadowUbo.pointShadowPos[slot * 4 + 2] = pos.z;
        shadowUbo.pointShadowRadius[slot] = m_pointShadows.radius(slot);
        shadowUbo.pointShadowValid[slot] = 1.0f;
    }
    for (u32 slot = 0; slot < GpuSpotShadowMap::kMaxSpotShadowLights; ++slot) {
        if (!m_spotShadows.valid(slot)) continue;
        const Vec3 pos = m_spotShadows.lightPosition(slot);
        std::memcpy(shadowUbo.spotShadowVP + slot * 16, m_spotShadows.lightVP(slot).data(),
                    sizeof(float) * 16);
        shadowUbo.spotShadowPos[slot * 4 + 0] = pos.x;
        shadowUbo.spotShadowPos[slot * 4 + 1] = pos.y;
        shadowUbo.spotShadowPos[slot * 4 + 2] = pos.z;
        shadowUbo.spotShadowParams[slot * 4 + 0] = m_spotShadows.radius(slot);
        shadowUbo.spotShadowParams[slot * 4 + 1] = m_spotShadows.cosHalfAngle(slot);
        shadowUbo.spotShadowValid[slot] = 1.0f;
    }

    std::vector<const MeshDraw*> opaque;
    std::vector<const MeshDraw*> translucent;
    opaque.reserve(draws.size());
    bool anyTransmission = false;
    for (const MeshDraw& draw : draws) {
        if (draw.skipColorDraw) continue;
        if (!wireframe && draw.isTranslucent()) {
            translucent.push_back(&draw);
            anyTransmission = anyTransmission || draw.transmission > 0.0f;
        } else {
            opaque.push_back(&draw);
        }
    }

    RHI::RenderPassDesc rp;
    rp.colorTarget = colorTarget;
    // Attachment order: 0 HDR, 1 albedo, 2 octahedral normal, 3 material.
    rp.numColorTargets = 4;
    rp.extraColorTargets[0] = pass.gbuffer.albedo;
    rp.extraColorTargets[1] = pass.gbuffer.normalMaterial;
    rp.extraColorTargets[2] = pass.gbuffer.direct;
    rp.depthTarget = depthTarget;
    rp.colorLayer = options.colorLayer;
    rp.clearColor[0] = 0.0f;
    rp.clearColor[1] = 0.0f;
    rp.clearColor[2] = 0.0f;
    rp.clearColor[3] = 0.0f;
    rp.clearDepth = true;
    rp.cycle = pass.cycleTargets;

    {
        CF_GPU_SCOPE(cmd, "GpuSceneRenderer::opaque");
        ScopedMs timer(m_building.opaqueMs);
        // Micromesh cull passes are compute: they run before the render pass opens.
        cmd->beginRenderPass(rp);
        cmd->setViewport(0, 0, static_cast<f32>(width), static_cast<f32>(height));
        cmd->pushUniformData(RHI::ShaderStage::Fragment, 1, &shadowUbo, sizeof(shadowUbo));
        drawMeshList(cmd, opaque, camera, lighting, projectRoot, options, pass,
                     wireframe ? m_wireframePipeline : m_scenePipeline);
        if (options.drawSky) drawSky(cmd, camera, height);
        if (options.grid.enabled) drawGrid(cmd, camera, options.grid);
        cmd->endRenderPass();
    }

    if (!translucent.empty()) {
        CF_GPU_SCOPE(cmd, "GpuSceneRenderer::translucent");
        ScopedMs timer(m_building.translucentMs);
        auto distanceSq = [&](const MeshDraw* d) {
            const Vec3 center = d->hasAabb ? (d->aabbMin + d->aabbMax) * 0.5f
                                           : d->worldMatrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
            return (center - camera.position).lengthSquared();
        };
        std::sort(translucent.begin(), translucent.end(),
                  [&](const MeshDraw* a, const MeshDraw* b) { return distanceSq(a) > distanceSq(b); });

        if (anyTransmission && pass.view && pass.view->opaqueCopy && options.colorLayer == 0 &&
            colorTarget == pass.view->color[pass.view->current]) {
            cmd->copyTexture(colorTarget, pass.view->opaqueCopy, width, height);
            pass.opaqueCopyBound = true;
        }

        RHI::RenderPassDesc rp2 = rp;
        rp2.loadColor = true;
        rp2.clearDepth = false;
        rp2.cycle = false;
        cmd->beginRenderPass(rp2);
        cmd->setViewport(0, 0, static_cast<f32>(width), static_cast<f32>(height));
        cmd->pushUniformData(RHI::ShaderStage::Fragment, 1, &shadowUbo, sizeof(shadowUbo));
        // Back to front; transmissive surfaces write depth and composite the copied scene,
        // alpha-blended ones are premultiplied over what is already there.
        std::vector<const MeshDraw*> run;
        bool runTransmissive = false;
        auto flush = [&]() {
            if (run.empty()) return;
            drawMeshList(cmd, run, camera, lighting, projectRoot, options, pass,
                         runTransmissive ? m_scenePipeline : m_blendPipeline);
            run.clear();
        };
        for (const MeshDraw* d : translucent) {
            const bool transmissive = d->transmission > 0.0f;
            if (!run.empty() && transmissive != runTransmissive) flush();
            runTransmissive = transmissive;
            run.push_back(d);
        }
        flush();
        cmd->endRenderPass();
        pass.opaqueCopyBound = false;
    }

    if (!wireframe && m_effectPipeline && m_effectMesh.vertexBuffer && m_effectMesh.indexBuffer &&
        !m_effectMesh.indices.empty()) {
        RHI::RenderPassDesc effectPass = rp;
        effectPass.loadColor = true;
        effectPass.clearDepth = false;
        effectPass.cycle = false;
        cmd->beginRenderPass(effectPass);
        cmd->setViewport(0, 0, static_cast<f32>(width), static_cast<f32>(height));
        cmd->bindPipeline(m_effectPipeline);
        VertexUBO effectUbo{};
        std::memcpy(effectUbo.mvp, pass.viewProj.data(), sizeof(effectUbo.mvp));
        const Mat4 identity = Mat4::identity();
        std::memcpy(effectUbo.model, identity.data(), sizeof(effectUbo.model));
        cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &effectUbo, sizeof(effectUbo));
        cmd->bindVertexBuffer(m_effectMesh.vertexBuffer);
        cmd->bindIndexBuffer(m_effectMesh.indexBuffer);
        cmd->drawIndexed(static_cast<u32>(m_effectMesh.indices.size()));
        cmd->endRenderPass();
    }

    return static_cast<u32>(opaque.size() + translucent.size());
}

void GpuSceneRenderer::applyAuthoredMaterial(MeshDraw& draw, const std::string& materialPath,
                                             const Mat4& worldMatrix, const std::string& projectRoot) {
    if (materialPath.empty()) return;
    const Assets::MaterialSurface surface =
        Assets::MaterialCache::instance().resolve(materialPath, projectRoot);
    if (surface.valid) {
        draw.hasAuthoredMaterial = true;
        const Vec3 albedo = srgbToLinear(Vec3(surface.albedo.x, surface.albedo.y, surface.albedo.z));
        draw.albedo = Vec4(albedo.x, albedo.y, albedo.z, std::clamp(surface.albedo.w, 0.0f, 1.0f));
        draw.metallic = std::clamp(surface.metallic, 0.0f, 1.0f);
        draw.roughness = std::clamp(surface.roughness, 0.02f, 1.0f);
        draw.reflectance = std::clamp(surface.reflectance, 0.0f, 1.0f);
        draw.emission = srgbToLinear(surface.emission) * std::max(surface.emissionStrength, 0.0f);
        if (draw.customTexturePath.empty()) draw.customTexturePath = surface.albedoMap;
        if (draw.customNormalPath.empty()) draw.customNormalPath = surface.normalMap;
        draw.ormMapPath = surface.ormMap;
        draw.emissionMapPath = surface.emissionMap;
        draw.uvTiling = surface.uvTiling;
        draw.uvOffset = surface.uvOffset;
        draw.uvObjectScale = surface.uvObjectScale != 0;
        draw.normalStrength = surface.normalStrength;
        draw.normalDirectX = surface.normalDirectX;
        draw.aoStrength = surface.aoStrength;
        draw.heightScale = surface.heightScale;
        draw.parallaxSteps = surface.parallaxSteps;
        if (surface.displacementMode == 1 && m_displacePipeline && draw.mesh &&
            !surface.normalMap.empty() && surface.displacementScale > 0.0f) {
            const u32 levels = std::min<u32>(surface.meshSubdivisions, 9u);
            RHI::Texture* heightTex = GpuTextureCache::instance().acquire(
                m_device, surface.normalMap, projectRoot, 0);
            DisplacedMesh* dm = heightTex ? displacedMeshFor(draw.mesh, levels) : nullptr;
            if (dm) {
                draw.displacementTexture = heightTex;
                const f32 sx = Vec3(worldMatrix.data()[0], worldMatrix.data()[1], worldMatrix.data()[2]).length();
                const f32 sz = Vec3(worldMatrix.data()[8], worldMatrix.data()[9], worldMatrix.data()[10]).length();
                const f32 avgScale = std::max(0.5f * (sx + sz), 1e-4f);
                const f32 tile = std::max(0.5f * (surface.uvTiling.x + surface.uvTiling.y), 1e-4f);
                draw.mesh = &dm->mesh;
                draw.displaced = true;
                draw.heightScale = 0.0f; // real geometry replaces parallax
                draw.displacementScale = surface.displacementScale;
                draw.displacementMetresPerUv = dm->metersPerUv * avgScale / tile;
                draw.displacementEps = std::max(dm->edgeUv * tile, 1e-5f);
            }
        }
        draw.alphaMode = static_cast<u8>(surface.alphaMode);
        draw.alphaCutoff = surface.alphaCutoff;
        draw.transmission = std::clamp(surface.transmission, 0.0f, 1.0f);
        draw.ior = std::max(surface.ior, 1.0f);
        draw.clearcoat = std::clamp(surface.clearcoat, 0.0f, 1.0f);
        draw.clearcoatRoughness = std::clamp(surface.clearcoatRoughness, 0.02f, 1.0f);
        draw.sheenColor = srgbToLinear(surface.sheenColor);
        draw.sheenRoughness = surface.sheenRoughness;
        draw.windStrength = surface.windStrength;
        draw.windSpeed = surface.windSpeed;
        draw.windFlutter = surface.windFlutter;
        draw.translucency = surface.translucency;
        // The material's modifiers combine with the entity's: tint multiplies, amounts add.
        draw.modTint = Vec3(draw.modTint.x * surface.modTint.x, draw.modTint.y * surface.modTint.y,
                            draw.modTint.z * surface.modTint.z);
        draw.modWetness = std::clamp(draw.modWetness + surface.modWetness, 0.0f, 1.0f);
        draw.modSnow = std::clamp(draw.modSnow + surface.modSnow, 0.0f, 1.0f);
        draw.modAge = std::clamp(draw.modAge + surface.modAge, 0.0f, 1.0f);
        draw.iridescence = std::clamp(surface.iridescence, 0.0f, 1.0f);
        draw.iridescenceThickness = surface.iridescenceThickness;
        draw.iridescenceIor = surface.iridescenceIor;
        if (draw.alphaMode == 0 && draw.albedo.w < 1.0f) draw.albedo.w = 1.0f;
        Effects::SurfaceShade shade;
        shade.albedo = draw.albedo;
        shade.emission = draw.emission;
        shade.metallic = draw.metallic;
        shade.roughness = draw.roughness;
        shade.reflectance = draw.reflectance;
        Effects::applyMaterialReflection(surface.reflection, surface.reflectionBudget, shade);
        draw.albedo = shade.albedo;
        draw.emission = shade.emission;
        draw.metallic = shade.metallic;
        draw.roughness = shade.roughness;
        draw.reflectance = shade.reflectance;
        draw.reflection = shade.reflection;
        draw.ssrStepsOverride = shade.ssrSteps;
        draw.ssrRoughnessGate = shade.ssrRoughness;
        draw.reflectPerformance = surface.reflectionPerformance != 0;
        draw.reflectQuality = surface.reflectionQuality != 0;
        draw.ssrResolution = surface.ssrResolution;
        draw.ssrMaxSteps = surface.ssrMaxSteps;
        draw.ssrTemporalFrames = surface.ssrTemporalFrames;
        draw.ssrDistance = surface.ssrDistance;
        draw.ssrSamples = surface.ssrSamples;
        draw.ssrDenoise = surface.ssrDenoise;
        draw.ssrProbeBlend = surface.ssrProbeBlend;
        draw.ssrBounces = surface.ssrBounces;
        if (draw.reflectQuality && surface.reflectionPlanar) draw.planarReceiver = true;
    }
}

std::vector<GpuSceneRenderer::MeshDraw> GpuSceneRenderer::gatherMeshDraws(
    ECS::World& world, const Vec3& cameraPos, const Spatial::Frustum& frustum,
    const std::string& projectRoot, const GpuSceneRenderOptions& options) {
    std::vector<MeshDraw> draws;
    const std::vector<Vec3>& viewers = options.textureQualityViewers;
    auto viewerDistanceFor = [&](const Vec3& worldPos) -> f32 {
        if (!viewers.empty()) {
            return distanceToNearestViewer(worldPos, viewers);
        }
        return (worldPos - cameraPos).length();
    };

    ECS::ComponentQuery q;
    q.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(q, [&](ECS::Entity entity, ECS::MeshFilterComponent& filter) {
        if (Scene::isEffectivelyDisabled(world, entity)) return;
        if (options.excludeEntity.isValid() && entity == options.excludeEntity) return;
        if (const Effects::EffectComponent* volume = world.get<Effects::EffectComponent>(entity)) {
            if (volume->enabled &&
                volume->kind == static_cast<u8>(Effects::EffectKind::VolumetricLight)) {
                return;
            }
        }

        const Mat4 worldMatrix = entityMatrix(world, entity);
        if (auto* terrain = world.get<ECS::TerrainComponent>(entity)) {
            auto& cache = Terrain::TerrainCache::instance();
            cache.syncGpuTextures(m_device, entity, *terrain, projectRoot);

            std::vector<Terrain::TerrainDrawChunk> terrainChunks;
            cache.gatherDrawMeshes(entity, *terrain, worldMatrix, cameraPos, frustum,
                                   terrainChunks, nullptr, options.terrainLodDistanceScale);
            for (const Terrain::TerrainDrawChunk& chunk : terrainChunks) {
                if (!chunk.mesh || chunk.mesh->vertices.empty()) continue;
                MeshDraw draw;
                draw.entity = entity;
                draw.mesh = chunk.mesh;
                draw.worldMatrix = worldMatrix;
                draw.terrainSettings = terrain;
                draw.isTerrain = true;
                draw.castShadows = terrain->castShadows;
                draw.receiveShadows = terrain->receiveShadows;
                draw.shininess = terrain->shininess;
                draw.albedo = Vec4(terrain->tint.x, terrain->tint.y, terrain->tint.z, 1.0f);
                draw.roughness = 1.0f;
                if (auto* renderer = world.get<ECS::MeshRendererComponent>(entity)) {
                    draw.receiveShadows = renderer->receiveShadows;
                }
                const Vec3 chunkCenter = worldMatrix.transformPoint(
                    (chunk.localBoundsMin + chunk.localBoundsMax) * 0.5f);
                draw.viewerDistance = viewerDistanceFor(chunkCenter);
                draws.push_back(draw);
            }
            return;
        }

        Assets::Mesh3D* mesh = ECS::editedMesh(world, entity);
        if (!mesh && filter.primitive == ECS::MeshPrimitive::Custom) {
            if (!filter.customMeshPath.empty()) {
                mesh = Assets::MeshCache::getInstance().getMesh(filter.customMeshPath, projectRoot);
            }
            if (!mesh) return;
        } else if (!mesh) {
            mesh = GpuProceduralMeshes::get(filter.primitive);
        }
        if (!mesh || mesh->vertices.empty()) return;

        const Vec3 bsz = mesh->bounds.max - mesh->bounds.min;
        Spatial::AABB3D aabb;
        bool hasAabb = false;
        if (bsz.lengthSquared() > 1e-6f) {
            const Vec3 c[2] = {mesh->bounds.min, mesh->bounds.max};
            aabb.min = Vec3(1e30f, 1e30f, 1e30f);
            aabb.max = Vec3(-1e30f, -1e30f, -1e30f);
            for (int ix = 0; ix < 2; ++ix) {
                for (int iy = 0; iy < 2; ++iy) {
                    for (int iz = 0; iz < 2; ++iz) {
                        const Vec3 wp = worldMatrix.transformPoint(Vec3(c[ix].x, c[iy].y, c[iz].z));
                        aabb.min.x = std::min(aabb.min.x, wp.x);
                        aabb.min.y = std::min(aabb.min.y, wp.y);
                        aabb.min.z = std::min(aabb.min.z, wp.z);
                        aabb.max.x = std::max(aabb.max.x, wp.x);
                        aabb.max.y = std::max(aabb.max.y, wp.y);
                        aabb.max.z = std::max(aabb.max.z, wp.z);
                    }
                }
            }
            hasAabb = true;
            if (!frustum.intersects(aabb)) {
                ++m_building.frustumCulled;
                return;
            }
        }

        const f32 sx = Vec3(worldMatrix(0, 0), worldMatrix(1, 0), worldMatrix(2, 0)).length();
        const f32 sy = Vec3(worldMatrix(0, 1), worldMatrix(1, 1), worldMatrix(2, 1)).length();
        const f32 sz = Vec3(worldMatrix(0, 2), worldMatrix(1, 2), worldMatrix(2, 2)).length();
        const f32 scale = std::max(sx, std::max(sy, sz));
        const f32 lodDistance = viewerDistanceFor(meshWorldCenter(worldMatrix, mesh)) / std::max(scale, 0.001f);

        // One draw per material slot (see MeshFilterComponent::slotMaterials), or one for the whole mesh.
        auto buildDraw = [&](const std::string& materialPath, i32 onlySubMesh) {
        MeshDraw draw;
        draw.onlySubMesh = onlySubMesh;
        draw.entity = entity;
        draw.mesh = selectMeshLod(mesh, entity, lodDistance);
        if (const std::vector<Assets::Vertex3D>* posed = Animation::skinnedVerticesFor(entity.id())) {
            if (posed->size() == mesh->vertices.size()) {
                draw.mesh = mesh;
                draw.skinnedVertices = posed;
            }
        }
        draw.worldMatrix = worldMatrix;
        // A lower LOD may have fewer sub-mesh ranges than the slots: slot 0 then draws the whole of it, the rest nothing.
        if (draw.mesh && draw.onlySubMesh >= 0 &&
            static_cast<usize>(draw.onlySubMesh) >= draw.mesh->subMeshes.size()) {
            if (draw.onlySubMesh > 0) return;
            draw.onlySubMesh = -1;
        }
        draw.castShadows = Scene::meshCastsShadows(world, entity);
        draw.receiveShadows = Scene::meshReceivesShadows(world, entity);
        if (auto* renderer = world.get<ECS::MeshRendererComponent>(entity)) {
            draw.castShadows = renderer->castShadows;
            draw.receiveShadows = renderer->receiveShadows;
            draw.modTint = Vec3(renderer->modTint.x, renderer->modTint.y, renderer->modTint.z);
            draw.modWetness = renderer->modWetness;
            draw.modSnow = renderer->modSnow;
            draw.modAge = renderer->modAge;
            if (renderer->micromesh) draw.micromeshPixels = std::max(renderer->micromeshPixels, 0.05f);
            draw.micromeshOcclusion = renderer->micromesh && renderer->micromeshOcclusion;
        }
        draw.shininess = filter.shininess;
        draw.customTexturePath = filter.customTexturePath;
        draw.customNormalPath = filter.customNormalPath;
        if (draw.mesh && !draw.mesh->materials.empty()) {
            // Use the most expensive alpha mode for pass selection. The shader still
            // receives the exact per-material mode for each submesh.
            u8 mode = 0;
            f32 cutoff = 0.5f;
            for (const Assets::MeshSurfaceMaterial& material : draw.mesh->materials) {
                if (material.alphaMode > mode) mode = material.alphaMode;
                if (material.alphaMode != 0) cutoff = material.alphaCutoff;
            }
            draw.alphaMode = mode;
            draw.alphaCutoff = cutoff;
        }
        applyAuthoredMaterial(draw, materialPath, worldMatrix, projectRoot);
        if (filter.primitive == ECS::MeshPrimitive::Custom) {
            const std::string& resolved = Assets::MeshCache::getInstance().getResolvedPath();
            draw.meshPath = resolved.empty() ? filter.customMeshPath : resolved;
        }
        draw.viewerDistance = lodDistance * std::max(scale, 0.001f);
        draw.hasAabb = hasAabb;
        if (hasAabb) {
            draw.aabbMin = aabb.min;
            draw.aabbMax = aabb.max;
        }
        if (const ECS::NaturalFluidComponent* fluid =
                world.get<ECS::NaturalFluidComponent>(entity)) {
            if (fluid->enabled) {
                if (fluid->mode != static_cast<u8>(ECS::NaturalFluidMode::FluidVolume) &&
                    draw.hasAuthoredMaterial) {
                    // The entity has its own material (a .mat, for example one written by the
                    // Water Generator): it is the water's material, as for any mesh. The waves
                    // and the water look (ripples, foam) still come from the water surface.
                    draw.isWater = true;
                    Scene::GpuWaterWaves waves;
                    if (Scene::naturalFluidGpuWaves(world, entity.id(), waves)) {
                        draw.gpuWater = true;
                        draw.waveCount = waves.count;
                        draw.waterTime = waves.time;
                        draw.waterFoamThreshold = waves.foamThreshold;
                        std::memcpy(draw.waveData, waves.packed, sizeof(draw.waveData));
                    } else if (const Fluids::FluidWorld* surface =
                                   Scene::naturalFluidWorldFor(world, entity.id())) {
                        draw.waterTime = surface->time();
                    }
                } else if (fluid->mode != static_cast<u8>(ECS::NaturalFluidMode::FluidVolume)) {
                    // No material assigned: a physically-based transparent liquid with a sharp
                    // clearcoat, so reflections read as water.
                    const Vec3 albedo =
                        srgbToLinear(Vec3(fluid->albedoR, fluid->albedoG, fluid->albedoB));
                    draw.albedo = Vec4(albedo.x, albedo.y, albedo.z,
                                       std::clamp(fluid->albedoA, 0.0f, 1.0f));
                    draw.metallic = std::clamp(fluid->metallic, 0.0f, 1.0f);
                    draw.roughness = std::clamp(fluid->roughness, 0.02f, 1.0f);
                    // Water reflects about 2% head-on and much more at grazing angles
                    // (Fresnel): a low F0 keeps the body colour, the clearcoat adds the
                    // sharp sky reflection.
                    draw.reflectance = 0.5f;
                    draw.alphaMode = 2;
                    draw.transmission = 0.12f;
                    draw.ior = 1.333f;
                    draw.clearcoat = 0.35f;
                    draw.clearcoatRoughness = 0.03f;
                    draw.hasAuthoredMaterial = true;
                    draw.isWater = true;
                    Scene::GpuWaterWaves waves;
                    if (Scene::naturalFluidGpuWaves(world, entity.id(), waves)) {
                        draw.gpuWater = true;
                        draw.waveCount = waves.count;
                        draw.waterTime = waves.time;
                        draw.waterFoamThreshold = waves.foamThreshold;
                        std::memcpy(draw.waveData, waves.packed, sizeof(draw.waveData));
                    } else if (const Fluids::FluidWorld* world3 =
                                   Scene::naturalFluidWorldFor(world, entity.id())) {
                        draw.waterTime = world3->time();
                    }
                } else {
                    // Fluid volume voxels. The density field is real; the colour
                    // and emission come from the selected material preset.
                    switch (static_cast<ECS::NaturalFluidPreset>(fluid->preset)) {
                        case ECS::NaturalFluidPreset::Fire: {
                            const f32 strength = std::max(fluid->fluidEmission, 2.5f);
                            draw.albedo = Vec4(1.0f, 0.36f, 0.05f, 0.55f);
                            draw.emission = srgbToLinear(Vec3(1.0f, 0.42f, 0.08f)) * strength;
                            break;
                        }
                        case ECS::NaturalFluidPreset::Fog:
                            draw.albedo = Vec4(0.74f, 0.78f, 0.83f, 0.22f);
                            break;
                        case ECS::NaturalFluidPreset::Smoke:
                            draw.albedo = Vec4(0.24f, 0.24f, 0.27f, 0.42f);
                            break;
                        case ECS::NaturalFluidPreset::Water:
                        default:
                            draw.albedo = Vec4(0.05f, 0.24f, 0.46f, 0.65f);
                            break;
                    }
                    draw.metallic = 0.0f;
                    draw.roughness = 1.0f;
                    draw.alphaMode = 2;
                    draw.castShadows = false;
                    draw.hasAuthoredMaterial = true;
                }
            }
        }
        if (const Effects::EffectComponent* effect = world.get<Effects::EffectComponent>(entity)) {
            Effects::SurfaceShade shade;
            shade.albedo = draw.albedo;
            shade.emission = draw.emission;
            shade.metallic = draw.metallic;
            shade.roughness = draw.roughness;
            shade.reflectance = draw.reflectance;
            shade.reflection = draw.reflection;
            shade.ssrSteps = draw.ssrStepsOverride;
            shade.ssrRoughness = draw.ssrRoughnessGate;
            Effects::applyEffectShade(*effect, shade);
            draw.albedo = shade.albedo;
            draw.emission = shade.emission;
            draw.metallic = shade.metallic;
            draw.roughness = std::clamp(shade.roughness, 0.02f, 1.0f);
            draw.reflectance = shade.reflectance;
            draw.reflection = shade.reflection;
            draw.ssrStepsOverride = shade.ssrSteps;
            draw.ssrRoughnessGate = shade.ssrRoughness;
            if (shade.planar) draw.planarReceiver = true;
        }
        const bool flat = (mesh->bounds.max.y - mesh->bounds.min.y) < 0.08f;
        const f32 midY = (mesh->bounds.min.y + mesh->bounds.max.y) * 0.5f;
        const f32 worldY = worldMatrix.transformPoint(Vec3(0.0f, midY, 0.0f)).y;
        const bool effectPlanar = draw.planarReceiver;
        draw.planarReceiver = effectPlanar ||
                              (flat && options.features.reflections == ReflectionMode::Planar &&
                               std::abs(worldY - options.features.reflectionPlaneY) < 0.4f);
        draws.push_back(draw);
        };
        // The Mesh Filter's material wins; the Mesh Renderer's is the fallback (a scene or script may only set that one).
        const ECS::MeshRendererComponent* rendererForMaterial = world.get<ECS::MeshRendererComponent>(entity);
        const std::string& baseMaterial =
            !filter.customMaterialPath.empty() || !rendererForMaterial ? filter.customMaterialPath
                                                                        : rendererForMaterial->materialPath;
        const std::vector<std::string> slots = ECS::splitMaterialSlots(filter.slotMaterials);
        if (slots.empty() || mesh->subMeshes.size() < 2) {
            buildDraw(baseMaterial, -1);
        } else {
            for (u32 s = 0; s < mesh->subMeshes.size(); ++s) {
                const bool own = s < slots.size() && !slots[s].empty();
                buildDraw(own ? slots[s] : baseMaterial, static_cast<i32>(s));
            }
        }
    });

    gatherWorldInstanceDraws(world, cameraPos, frustum, projectRoot, options, draws);
    return draws;
}

void GpuSceneRenderer::gatherWorldInstanceDraws(ECS::World& world, const Vec3& cameraPos,
                                                const Spatial::Frustum& frustum, const std::string& projectRoot,
                                                const GpuSceneRenderOptions& options,
                                                std::vector<MeshDraw>& draws) {
    namespace CW = Caffeine::World;
    const std::vector<Vec3>& viewers = options.textureQualityViewers;
    ECS::ComponentQuery q;
    q.with<ECS::WorldInstancesComponent>();
    world.forEach<ECS::WorldInstancesComponent>(q, [&](ECS::Entity entity, ECS::WorldInstancesComponent& component) {
        if (Scene::isEffectivelyDisabled(world, entity)) return;
        if (options.excludeEntity.isValid() && entity == options.excludeEntity) return;
        const CW::WorldInstanceSet& set =
            CW::WorldInstanceCache::instance().get(component.bakePath, component.modelsPath, projectRoot);
        if (set.bake.chunks.empty() || set.models.empty()) return;

        const Mat4 worldMatrix = entityMatrix(world, entity);
        const f32 entityScale =
            std::max({Vec3(worldMatrix(0, 0), worldMatrix(1, 0), worldMatrix(2, 0)).length(),
                      Vec3(worldMatrix(0, 1), worldMatrix(1, 1), worldMatrix(2, 1)).length(),
                      Vec3(worldMatrix(0, 2), worldMatrix(1, 2), worldMatrix(2, 2)).length()});

        // The draws of one model part, one per material slot, made once per entity and frame and copied per
        // instance: the same fields a Mesh Filter draw gets from its mesh and materials.
        struct PartDraws {
            std::vector<MeshDraw> slots;
            f32 radius = 0.0f;  ///< pivot to the farthest corner of the mesh bounds
        };
        std::unordered_map<const CW::InstanceModelPart*, PartDraws> parts;
        f32& modelRadius = m_worldInstanceRadius[entity.id()];
        auto partDraws = [&](const CW::InstanceModelPart& part) -> const PartDraws& {
            auto found = parts.find(&part);
            if (found != parts.end()) return found->second;
            PartDraws& out = parts[&part];
            Assets::Mesh3D* mesh = Assets::MeshCache::getInstance().getMesh(part.meshPath, projectRoot);
            if (!mesh || mesh->vertices.empty()) return out;
            const std::string resolved = Assets::MeshCache::getInstance().getResolvedPath();
            for (int i = 0; i < 8; ++i) {
                const Vec3 corner((i & 1) ? mesh->bounds.max.x : mesh->bounds.min.x,
                                  (i & 2) ? mesh->bounds.max.y : mesh->bounds.min.y,
                                  (i & 4) ? mesh->bounds.max.z : mesh->bounds.min.z);
                out.radius = std::max(out.radius, corner.length());
            }
            modelRadius = std::max(modelRadius, out.radius);
            u8 alphaMode = 0;
            f32 alphaCutoff = 0.5f;
            for (const Assets::MeshSurfaceMaterial& material : mesh->materials) {
                if (material.alphaMode > alphaMode) alphaMode = material.alphaMode;
                if (material.alphaMode != 0) alphaCutoff = material.alphaCutoff;
            }
            auto make = [&](const std::string& materialPath, i32 onlySubMesh) {
                MeshDraw draw;
                draw.entity = entity;
                draw.mesh = mesh;
                draw.onlySubMesh = onlySubMesh;
                draw.worldMatrix = worldMatrix;
                draw.castShadows = component.castShadows && !part.cutout;
                draw.receiveShadows = true;
                draw.alphaMode = alphaMode;
                draw.alphaCutoff = alphaCutoff;
                applyAuthoredMaterial(draw, materialPath, worldMatrix, projectRoot);
                draw.meshPath = resolved.empty() ? part.meshPath : resolved;
                out.slots.push_back(std::move(draw));
            };
            // Multi-material meshes draw one sub-mesh per draw so each part still batches (see
            // assignInstanceBatches: only single sub-mesh draws are instanced).
            const std::vector<std::string> slots = ECS::splitMaterialSlots(part.slots);
            if (mesh->subMeshes.size() < 2) {
                make(slots.empty() ? std::string() : slots.front(), -1);
            } else {
                for (u32 s = 0; s < mesh->subMeshes.size(); ++s) {
                    make(s < slots.size() ? slots[s] : std::string(), static_cast<i32>(s));
                }
            }
            return out;
        };
        // The shared models first: their radius bounds the chunks before any instance is looked at.
        for (const auto& [key, list] : set.models.variants) {
            for (const CW::InstanceModelPart& part : list) partDraws(part);
        }

        for (usize c = 0; c < set.bake.chunks.size(); ++c) {
            if (c >= set.chunkBounds.size() ||
                !CW::chunkVisible(set.chunkBounds[c], worldMatrix, modelRadius, &frustum, cameraPos,
                                  component.drawDistance)) {
                ++m_building.frustumCulled;
                continue;
            }
            for (const CW::InstanceRecord& record : set.bake.chunks[c].instances) {
                const std::vector<CW::InstanceModelPart>* model = set.models.partsFor(record);
                if (!model) continue;
                const Mat4 instanceWorld = worldMatrix * CW::instanceLocalMatrix(record);
                const Vec3 center = instanceWorld.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
                // Quantised (1/32 steps): the modifiers are part of the instancing batch key, so neighbouring
                // tints must compare equal for the instances to keep sharing one instanced draw.
                const Vec3 tint = CW::quantisedInstanceTint(record);
                const f32 wetness = std::clamp(CW::quantiseInstanceModifier(record.wetness), 0.0f, 1.0f);
                const f32 snow = std::clamp(CW::quantiseInstanceModifier(record.snow), 0.0f, 1.0f);
                const f32 viewerDistance =
                    viewers.empty() ? (center - cameraPos).length() : distanceToNearestViewer(center, viewers);
                for (const CW::InstanceModelPart& part : *model) {
                    const PartDraws& source = partDraws(part);
                    if (source.slots.empty()) continue;
                    const f32 r = source.radius * std::abs(record.scale) * entityScale;
                    Spatial::AABB3D box;
                    box.min = center - Vec3(r, r, r);
                    box.max = center + Vec3(r, r, r);
                    if (!frustum.intersects(box)) {
                        ++m_building.frustumCulled;
                        continue;
                    }
                    for (const MeshDraw& slot : source.slots) {
                        MeshDraw draw = slot;
                        draw.worldMatrix = instanceWorld;
                        draw.modTint = Vec3(draw.modTint.x * tint.x, draw.modTint.y * tint.y, draw.modTint.z * tint.z);
                        draw.modWetness = std::clamp(draw.modWetness + wetness, 0.0f, 1.0f);
                        draw.modSnow = std::clamp(draw.modSnow + snow, 0.0f, 1.0f);
                        draw.viewerDistance = viewerDistance;
                        draw.hasAabb = true;
                        draw.aabbMin = box.min;
                        draw.aabbMax = box.max;
                        draws.push_back(std::move(draw));
                    }
                }
            }
        }
    });
}

namespace {

/// One run of triangles of an object with one albedo (a material slot, or the whole mesh).
struct GiPiece {
    const Assets::Mesh3D* mesh = nullptr;
    const std::vector<Assets::Vertex3D>* posed = nullptr;  ///< the skinned pose, when the mesh has one
    Mat4 world = Mat4::identity();
    Vec3 albedo{1.0f, 1.0f, 1.0f};
    u32 firstIndex = 0;
    u32 indexCount = 0;
};

/// An object's identity, before any triangle is made.
struct GiSource {
    u64 key = 0;
    u64 hash = 0;
    Vec3 lo{};
    Vec3 hi{};
    u32 count = 0;       ///< triangles
    u32 pieceFirst = 0;
    u32 pieceCount = 0;
};

constexpr u32 kMaxGiTriangles = 1u << 20;  ///< what the voxeliser takes (PhotonCascades.cpp: kMaxVoxelTriangles)

void emitGiTriangles(const GiPiece& piece, std::vector<PhotonTriangle>& out) {
    const Assets::Mesh3D& mesh = *piece.mesh;
    const std::vector<Assets::Vertex3D>& vertices = piece.posed ? *piece.posed : mesh.vertices;
    const u32 end = std::min<u32>(piece.firstIndex + piece.indexCount, static_cast<u32>(mesh.indices.size()));
    for (u32 i = piece.firstIndex; i + 2 < end; i += 3) {
        const u32 ia = mesh.indices[i];
        const u32 ib = mesh.indices[i + 1];
        const u32 ic = mesh.indices[i + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) continue;
        const Vec3 a = piece.world.transformPoint(vertices[ia].position);
        const Vec3 b = piece.world.transformPoint(vertices[ib].position);
        const Vec3 c = piece.world.transformPoint(vertices[ic].position);
        PhotonTriangle t{};
        t.p0[0] = a.x; t.p0[1] = a.y; t.p0[2] = a.z;
        t.p1[0] = b.x; t.p1[1] = b.y; t.p1[2] = b.z;
        t.p2[0] = c.x; t.p2[1] = c.y; t.p2[2] = c.z;
        t.albedo[0] = piece.albedo.x; t.albedo[1] = piece.albedo.y; t.albedo[2] = piece.albedo.z;
        out.push_back(t);
    }
}

/// Boxes that overlap or touch become their union: the tiles of one brush stroke, an object's old and new bounds. The
/// voxeliser takes only a few boxes before it rebuilds a level whole, so many neighbouring tiles are worth fewer, larger
/// boxes (a superset of the same voxels, still a part of the volume). Used for terrain tiles only: a scene that changed
/// wholesale has to keep its many boxes, which is what makes the voxeliser rebuild whole and the light settle again.
void coalesceBoxes(std::vector<PhotonBox>& boxes) {
    bool merged = true;
    while (merged && boxes.size() > 1) {
        merged = false;
        for (usize i = 0; i < boxes.size() && !merged; ++i) {
            for (usize j = i + 1; j < boxes.size() && !merged; ++j) {
                const PhotonBox& a = boxes[i];
                const PhotonBox& b = boxes[j];
                const bool apart = a.hi.x < b.lo.x || b.hi.x < a.lo.x || a.hi.y < b.lo.y || b.hi.y < a.lo.y ||
                                   a.hi.z < b.lo.z || b.hi.z < a.lo.z;
                if (apart) continue;
                boxes[i] = PhotonBox{Vec3(std::min(a.lo.x, b.lo.x), std::min(a.lo.y, b.lo.y), std::min(a.lo.z, b.lo.z)),
                                     Vec3(std::max(a.hi.x, b.hi.x), std::max(a.hi.y, b.hi.y), std::max(a.hi.z, b.hi.z))};
                boxes.erase(boxes.begin() + static_cast<std::ptrdiff_t>(j));
                merged = true;
            }
        }
    }
}

}  // namespace

GpuSceneRenderer::PhotonView& GpuSceneRenderer::photonViewFor(u64 viewId) {
    for (auto& entry : m_photonViews) {
        if (entry.first == viewId) {
            entry.second->lastUsed = m_frameCounter;
            return *entry.second;
        }
    }
    // A few views at most (the editor, the game, a camera preview): the least recently used makes room.
    constexpr usize kMaxPhotonViews = 4;
    if (m_photonViews.size() >= kMaxPhotonViews) {
        usize oldest = 0;
        for (usize i = 1; i < m_photonViews.size(); ++i) {
            if (m_photonViews[i].second->lastUsed < m_photonViews[oldest].second->lastUsed) oldest = i;
        }
        m_photonViews[oldest].second->cascades.shutdown(m_device);
        m_photonViews.erase(m_photonViews.begin() + static_cast<std::ptrdiff_t>(oldest));
    }
    auto view = std::make_unique<PhotonView>();
    view->cascades.init(m_device);
    view->lastUsed = m_frameCounter;
    m_photonViews.emplace_back(viewId, std::move(view));
    return *m_photonViews.back().second;
}

bool GpuSceneRenderer::updatePhotonGeometry(ECS::World& world, const std::string& projectRoot,
                                            const ECS::PhotonGIComponent& settings) {
    std::vector<GiSource> sources;
    std::vector<GiPiece> pieces;
    u64 totalTriangles = 0;

    ECS::ComponentQuery q;
    q.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(q, [&](ECS::Entity entity, ECS::MeshFilterComponent& filter) {
        if (Scene::isEffectivelyDisabled(world, entity)) return;
        // Water and light volumes are not solid.
        if (const ECS::NaturalFluidComponent* fluid = world.get<ECS::NaturalFluidComponent>(entity)) {
            if (fluid->enabled) return;
        }
        if (const Effects::EffectComponent* volume = world.get<Effects::EffectComponent>(entity)) {
            if (volume->enabled &&
                volume->kind == static_cast<u8>(Effects::EffectKind::VolumetricLight)) {
                return;
            }
        }

        u64 hash = 1469598103934665603ull;
        auto mix = [&hash](const void* data, usize bytes) {
            const u8* p = static_cast<const u8*>(data);
            for (usize i = 0; i < bytes; ++i) {
                hash ^= p[i];
                hash *= 1099511628211ull;
            }
        };
        const Mat4 worldMatrix = entityMatrix(world, entity);
        const u32 pieceFirst = static_cast<u32>(pieces.size());
        u32 triangles = 0;
        const Assets::Mesh3D* boundsMesh = nullptr;
        const std::vector<Assets::Vertex3D>* posedForBounds = nullptr;
        mix(worldMatrix.data(), sizeof(f32) * 16);

        // Terrain: cut into tiles, each its own object with the tile's own revision, so an edit (a brush stroke)
        // revoxelises the tiles it touched and not the whole terrain. Earth colour: its splat layers are not one picture.
        if (const ECS::TerrainComponent* terrain = world.get<ECS::TerrainComponent>(entity)) {
            if (!settings.voxelTerrain) return;
            const Vec3 tint = srgbToLinear(Vec3(terrain->tint.x, terrain->tint.y, terrain->tint.z));
            const Vec3 albedo(tint.x * 0.4f, tint.y * 0.4f, tint.z * 0.4f);
            for (const Terrain::TerrainGiTile& tile : Terrain::TerrainCache::instance().giTilesFor(entity, *terrain)) {
                if (tile.mesh.vertices.empty() || tile.mesh.indices.size() < 3) continue;
                u64 tileHash = 1469598103934665603ull;
                auto mixTile = [&tileHash](const void* data, usize bytes) {
                    const u8* p = static_cast<const u8*>(data);
                    for (usize i = 0; i < bytes; ++i) {
                        tileHash ^= p[i];
                        tileHash *= 1099511628211ull;
                    }
                };
                mixTile(worldMatrix.data(), sizeof(f32) * 16);
                mixTile(&tile.revision, sizeof(tile.revision));
                mixTile(&albedo, sizeof(albedo));

                GiPiece piece;
                piece.mesh = &tile.mesh;
                piece.world = worldMatrix;
                piece.albedo = albedo;
                piece.indexCount = static_cast<u32>(tile.mesh.indices.size());
                Vec3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
                for (int corner = 0; corner < 8; ++corner) {
                    const Vec3 c((corner & 1) ? tile.hi.x : tile.lo.x, (corner & 2) ? tile.hi.y : tile.lo.y,
                                 (corner & 4) ? tile.hi.z : tile.lo.z);
                    const Vec3 w = worldMatrix.transformPoint(c);
                    lo = Vec3(std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z));
                    hi = Vec3(std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z));
                }
                GiSource source;
                // Unique among objects: the high bit marks a terrain tile, then the entity, then the tile.
                source.key = (1ull << 62) | (static_cast<u64>(entity.id()) << 24) | tile.index;
                source.hash = tileHash;
                source.lo = lo;
                source.hi = hi;
                source.count = piece.indexCount / 3;
                source.pieceFirst = static_cast<u32>(pieces.size());
                source.pieceCount = 1;
                pieces.push_back(piece);
                sources.push_back(source);
                totalTriangles += source.count;
            }
            return;
        } else {
            Assets::Mesh3D* mesh = ECS::editedMesh(world, entity);
            if (!mesh && filter.primitive == ECS::MeshPrimitive::Custom) {
                if (!filter.customMeshPath.empty()) {
                    mesh = Assets::MeshCache::getInstance().getMesh(filter.customMeshPath, projectRoot);
                }
                if (!mesh) return;
            } else if (!mesh) {
                mesh = GpuProceduralMeshes::get(filter.primitive);
            }
            if (!mesh || mesh->vertices.empty() || mesh->indices.size() < 3) return;

            // A skinned mesh is voxelised in its current pose, and moves with it.
            const std::vector<Assets::Vertex3D>* posed = Animation::skinnedVerticesFor(entity.id());
            if (posed && posed->size() != mesh->vertices.size()) posed = nullptr;
            if (posed) {
                if (!settings.voxelSkinned) return;
                // The pose's generation bumps exactly when the posed vertices change: it stands for them. Hashing every
                // vertex of a 40k-vertex tree byte by byte, for every view, every frame, was the whole cost of this pass.
                const u64 generation = Animation::skinnedGenerationFor(entity.id());
                const usize posedCount = posed->size();
                mix(&generation, sizeof(generation));
                mix(&posedCount, sizeof(posedCount));
                posedForBounds = posed;
            }
            const ECS::MeshRendererComponent* renderer = world.get<ECS::MeshRendererComponent>(entity);
            const std::string& baseMaterial =
                !filter.customMaterialPath.empty() || !renderer ? filter.customMaterialPath
                                                                : renderer->materialPath;
            const std::vector<std::string> slots = ECS::splitMaterialSlots(filter.slotMaterials);

            // The albedo a voxel gets: the material's colour times the mean colour of its picture (mid grey
            // when the picture cannot be read). Blended surfaces are not solid; cut-out ones only when the
            // Photon component says so.
            auto albedoOf = [&](const std::string& materialPath, Vec3& albedo) {
                albedo = Vec3(1.0f, 1.0f, 1.0f);
                std::string picture = filter.customTexturePath;
                if (!materialPath.empty()) {
                    const Assets::MaterialSurface surface =
                        Assets::MaterialCache::instance().resolve(materialPath, projectRoot);
                    if (surface.valid) {
                        if (surface.alphaMode == Assets::MaterialAlphaMode::Blend) return false;
                        if (surface.alphaMode == Assets::MaterialAlphaMode::Cutout && !settings.voxelCutout) {
                            return false;
                        }
                        albedo = srgbToLinear(Vec3(surface.albedo.x, surface.albedo.y, surface.albedo.z));
                        if (picture.empty()) picture = surface.albedoMap;
                    }
                }
                if (!picture.empty()) {
                    mix(picture.data(), picture.size());
                    Vec3 average(0.5f, 0.5f, 0.5f);
                    GpuTextureCache::instance().averageColor(picture, projectRoot, average);
                    albedo = Vec3(albedo.x * average.x, albedo.y * average.y, albedo.z * average.z);
                } else if (!mesh->baseColorTexture.empty() && mesh->textureWidth > 0 &&
                           mesh->textureHeight > 0 && mesh->textureChannels >= 3) {
                    // A picture embedded in the mesh (glTF): its mean, from at most about 65k texels.
                    const size_t channels = static_cast<size_t>(mesh->textureChannels);
                    const size_t total = static_cast<size_t>(mesh->textureWidth) * mesh->textureHeight;
                    const size_t stride = std::max<size_t>(1, total / 65536);
                    Vec3 sum(0.0f, 0.0f, 0.0f);
                    size_t count = 0;
                    for (size_t t = 0; t < total && (t + 1) * channels <= mesh->baseColorTexture.size();
                         t += stride) {
                        const u8* p = mesh->baseColorTexture.data() + t * channels;
                        sum = sum + srgbToLinear(Vec3(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f));
                        ++count;
                    }
                    const f32 inv = count ? 1.0f / static_cast<f32>(count) : 0.0f;
                    const Vec3 average =
                        count ? Vec3(sum.x * inv, sum.y * inv, sum.z * inv) : Vec3(0.5f, 0.5f, 0.5f);
                    albedo = Vec3(albedo.x * average.x, albedo.y * average.y, albedo.z * average.z);
                }
                return true;
            };

            mix(&mesh, sizeof(mesh));
            mix(&mesh->geometryRevision, sizeof(mesh->geometryRevision));
            auto addPiece = [&](u32 firstIndex, u32 indexCount, const Vec3& albedo) {
                GiPiece piece;
                piece.mesh = mesh;
                piece.posed = posed;
                piece.world = worldMatrix;
                piece.albedo = albedo;
                piece.firstIndex = firstIndex;
                piece.indexCount = indexCount;
                mix(&firstIndex, sizeof(firstIndex));
                mix(&indexCount, sizeof(indexCount));
                mix(&albedo, sizeof(albedo));
                pieces.push_back(piece);
                triangles += indexCount / 3;
            };
            if (slots.empty() || mesh->subMeshes.size() < 2) {
                Vec3 albedo;
                if (albedoOf(baseMaterial, albedo)) {
                    addPiece(0, static_cast<u32>(mesh->indices.size()), albedo);
                }
            } else {
                for (u32 s = 0; s < mesh->subMeshes.size(); ++s) {
                    const bool own = s < slots.size() && !slots[s].empty();
                    Vec3 albedo;
                    if (albedoOf(own ? slots[s] : baseMaterial, albedo)) {
                        addPiece(mesh->subMeshes[s].indexOffset, mesh->subMeshes[s].indexCount, albedo);
                    }
                }
            }
            boundsMesh = mesh;
        }
        if (pieces.size() == pieceFirst || triangles == 0) return;

        // World bounds: the mesh's (or the pose's) box through the transform.
        Vec3 lo(1e30f, 1e30f, 1e30f);
        Vec3 hi(-1e30f, -1e30f, -1e30f);
        Vec3 boxMin = boundsMesh->bounds.min;
        Vec3 boxMax = boundsMesh->bounds.max;
        if (posedForBounds) {
            boxMin = Vec3(1e30f, 1e30f, 1e30f);
            boxMax = Vec3(-1e30f, -1e30f, -1e30f);
            for (const Assets::Vertex3D& v : *posedForBounds) {
                boxMin = Vec3(std::min(boxMin.x, v.position.x), std::min(boxMin.y, v.position.y),
                              std::min(boxMin.z, v.position.z));
                boxMax = Vec3(std::max(boxMax.x, v.position.x), std::max(boxMax.y, v.position.y),
                              std::max(boxMax.z, v.position.z));
            }
        }
        for (int corner = 0; corner < 8; ++corner) {
            const Vec3 c((corner & 1) ? boxMax.x : boxMin.x, (corner & 2) ? boxMax.y : boxMin.y,
                         (corner & 4) ? boxMax.z : boxMin.z);
            const Vec3 w = worldMatrix.transformPoint(c);
            lo = Vec3(std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z));
            hi = Vec3(std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z));
        }

        GiSource source;
        source.key = entity.id();
        source.hash = hash;
        source.lo = lo;
        source.hi = hi;
        source.count = triangles;
        source.pieceFirst = pieceFirst;
        source.pieceCount = static_cast<u32>(pieces.size()) - pieceFirst;
        sources.push_back(source);
        totalTriangles += triangles;
    });

    // Nothing to do while every object is as it was (the common frame: a hash per object and no more).
    const u32 flags = (settings.voxelSkinned ? 1u : 0u) | (settings.voxelCutout ? 2u : 0u) |
                      (settings.voxelTerrain ? 4u : 0u);
    const bool first = flags != m_pv->flags || !m_pv->cascades.hasGeometry();
    if (!first && sources.size() == m_pv->geometry.objects.size()) {
        bool same = true;
        for (usize i = 0; i < sources.size() && same; ++i) {
            same = sources[i].key == m_pv->geometry.objects[i].key &&
                   sources[i].hash == m_pv->geometry.objects[i].hash;
        }
        if (same) return false;
    }

    // Something changed: rebuild the list, copying the triangles of what did not.
    std::unordered_map<u64, usize> before;
    for (usize i = 0; i < m_pv->geometry.objects.size(); ++i) before[m_pv->geometry.objects[i].key] = i;

    PhotonGeometry next;
    next.overBudget = totalTriangles > kMaxGiTriangles;
    std::vector<PhotonBox> dirty;
    std::vector<PhotonBox> tileDirty;  // the boxes of terrain tiles (keys with bit 62 set)
    auto isTerrainTile = [](u64 key) { return (key >> 62) == 1u; };
    std::vector<bool> kept(m_pv->geometry.objects.size(), false);
    if (!next.overBudget) next.triangles.reserve(static_cast<usize>(totalTriangles));
    for (const GiSource& source : sources) {
        PhotonObject object;
        object.key = source.key;
        object.hash = source.hash;
        object.lo = source.lo;
        object.hi = source.hi;
        object.count = source.count;
        object.first = static_cast<u32>(next.triangles.size());
        const auto it = before.find(source.key);
        // An over-budget set holds no triangles to copy from, so nothing in it counts as unchanged.
        const bool unchanged = it != before.end() && !first && !m_pv->geometry.overBudget &&
                               m_pv->geometry.objects[it->second].hash == source.hash;
        if (it != before.end()) kept[it->second] = true;
        if (!next.overBudget) {
            if (unchanged) {
                const PhotonObject& old = m_pv->geometry.objects[it->second];
                next.triangles.insert(next.triangles.end(), m_pv->geometry.triangles.begin() + old.first,
                                      m_pv->geometry.triangles.begin() + old.first + old.count);
            } else {
                for (u32 k = 0; k < source.pieceCount; ++k) {
                    emitGiTriangles(pieces[source.pieceFirst + k], next.triangles);
                }
            }
        }
        if (!unchanged) {
            std::vector<PhotonBox>& into = isTerrainTile(source.key) ? tileDirty : dirty;
            into.push_back({source.lo, source.hi});
            if (it != before.end()) {
                const PhotonObject& old = m_pv->geometry.objects[it->second];
                into.push_back({old.lo, old.hi});
            }
        }
        next.objects.push_back(object);
    }
    for (usize i = 0; i < kept.size(); ++i) {
        if (!kept[i]) {
            (isTerrainTile(m_pv->geometry.objects[i].key) ? tileDirty : dirty)
                .push_back({m_pv->geometry.objects[i].lo, m_pv->geometry.objects[i].hi});
        }
    }

    // Neighbouring terrain tiles (one brush stroke) become fewer, larger boxes. Only those: a scene that changed
    // wholesale must still be seen as many boxes, which makes the voxeliser rebuild whole and the light settle again.
    coalesceBoxes(tileDirty);
    dirty.insert(dirty.end(), tileDirty.begin(), tileDirty.end());
    const bool full = first || settings.fullRebuild || next.overBudget || m_pv->geometry.overBudget;
    m_pv->cascades.setGeometry(next, dirty, full);
    m_pv->geometry = std::move(next);
    m_pv->flags = flags;
    return true;
}

void GpuSceneRenderer::applyCoarseOcclusion(std::vector<MeshDraw>& draws, const GpuSceneCamera& camera,
                                            const GpuSceneRenderOptions& options) const {
    if (options.features.occlusion != OcclusionMode::Coarse || draws.size() < 2) return;
    CF_PROFILE_SCOPE("GpuSceneRenderer::occlusion");
    std::vector<OcclusionPrimitive> prims(draws.size());
    for (u32 i = 0; i < draws.size(); ++i) {
        prims[i].aabbMin = draws[i].aabbMin;
        prims[i].aabbMax = draws[i].aabbMax;
        if (!draws[i].hasAabb || draws[i].isTerrain) continue;
        const f32 radius = (draws[i].aabbMax - draws[i].aabbMin).length() * 0.5f;
        prims[i].canBeOccluded = true;
        prims[i].canOcclude = radius >= options.features.occlusionMinRadius && !draws[i].isTranslucent();
    }
    Vec3 forward = camera.focus - camera.position;
    if (forward.lengthSquared() < 1e-8f) return;
    forward = forward.normalized();
    const u32 maxOccluders = std::min(options.features.occlusionMaxOccluders, 16u);
    const std::vector<u32> visible = visibleAfterCoarseOcclusion(
        prims, camera.proj * camera.view, camera.position, forward, maxOccluders);
    if (visible.size() == draws.size()) return;
    std::vector<MeshDraw> kept;
    kept.reserve(visible.size());
    for (u32 index : visible) kept.push_back(std::move(draws[index]));
    draws.swap(kept);
}

void GpuSceneRenderer::appendVegetationDraws(ECS::World& world, const Vec3& cameraPos, const Spatial::Frustum& frustum,
                                             const std::string& projectRoot, std::vector<MeshDraw>& draws) {
    if (!m_instancedPipeline) return;
    ECS::ComponentQuery q;
    q.with<ECS::TerrainComponent>();
    world.forEach<ECS::TerrainComponent>(q, [&](ECS::Entity entity, ECS::TerrainComponent& terrain) {
        if (!terrain.useVegetation || Scene::isEffectivelyDisabled(world, entity)) return;
        auto& cache = Terrain::TerrainCache::instance();
        const Terrain::TerrainVegetation* veg = cache.vegetationFor(entity);
        if (!veg) return;
        const Mat4 worldMatrix = entityMatrix(world, entity);
        std::vector<VegetationBatch> batches;
        TerrainVegetationGpu::instance().collect(m_device, entity, terrain, cache, worldMatrix, cameraPos, frustum, batches, nullptr);
        for (const VegetationBatch& batch : batches) {
            if (batch.species >= veg->species().size()) continue;
            const Terrain::VegetationSpecies& species = veg->species()[batch.species];
            if (species.meshes.empty()) continue;
            const std::string& meshPath = species.meshes[batch.variant % species.meshes.size()];
            Assets::Mesh3D* mesh = Assets::MeshCache::getInstance().getMesh(meshPath, projectRoot);
            if (!mesh || mesh->vertices.empty() || !ensureMeshUploaded(mesh) || !mesh->vertexBuffer || !mesh->indexBuffer) continue;
            // One draw per sub-mesh: a tree's bark and its leaves have their own materials (`slotMaterials`, in sub-mesh order).
            const usize slots = std::max<usize>(mesh->subMeshes.size(), 1);
            for (usize slot = 0; slot < slots; ++slot) {
                const std::string& materialPath =
                    slot < species.slotMaterials.size() && !species.slotMaterials[slot].empty() ? species.slotMaterials[slot] : species.material;
                MeshDraw draw;
                draw.entity = entity;
                draw.mesh = mesh;
                draw.onlySubMesh = mesh->subMeshes.size() > 1 ? static_cast<i32>(slot) : -1;
                // A batch of one is a plain mesh at its own matrix; more are one instanced draw from the chunk's buffer.
                draw.worldMatrix = batch.count == 1 ? batch.first : Mat4::identity();
                if (batch.count > 1) {
                    draw.batchCount = batch.count;
                    draw.batchBuffer = batch.buffer;
                    draw.batchByteOffset = 0;
                }
                draw.castShadows = false;  // shadows from instanced vegetation are not drawn yet
                draw.receiveShadows = true;
                draw.meshPath = Assets::MeshCache::getInstance().getResolvedPath().empty() ? meshPath : Assets::MeshCache::getInstance().getResolvedPath();
                draw.modTint = Vec3(species.tint[0], species.tint[1], species.tint[2]);
                draw.viewerDistance = batch.distance;
                if (!mesh->materials.empty()) {
                    u8 mode = 0;
                    f32 cutoff = 0.5f;
                    for (const Assets::MeshSurfaceMaterial& material : mesh->materials) {
                        if (material.alphaMode > mode) mode = material.alphaMode;
                        if (material.alphaMode != 0) cutoff = material.alphaCutoff;
                    }
                    draw.alphaMode = mode;
                    draw.alphaCutoff = cutoff;
                }
                if (!materialPath.empty()) {
                    const Assets::MaterialSurface surface = Assets::MaterialCache::instance().resolve(materialPath, projectRoot);
                    if (surface.valid) {
                        draw.hasAuthoredMaterial = true;
                        const Vec3 albedo = srgbToLinear(Vec3(surface.albedo.x, surface.albedo.y, surface.albedo.z));
                        draw.albedo = Vec4(albedo.x, albedo.y, albedo.z, std::clamp(surface.albedo.w, 0.0f, 1.0f));
                        draw.metallic = std::clamp(surface.metallic, 0.0f, 1.0f);
                        draw.roughness = std::clamp(surface.roughness, 0.02f, 1.0f);
                        draw.reflectance = std::clamp(surface.reflectance, 0.0f, 1.0f);
                        draw.customTexturePath = surface.albedoMap;
                        draw.customNormalPath = surface.normalMap;
                        draw.ormMapPath = surface.ormMap;
                        draw.uvTiling = surface.uvTiling;
                        draw.uvOffset = surface.uvOffset;
                        draw.alphaMode = static_cast<u8>(surface.alphaMode);
                        draw.alphaCutoff = surface.alphaCutoff;
                        draw.windStrength = surface.windStrength;
                        draw.windSpeed = surface.windSpeed;
                        draw.windFlutter = surface.windFlutter;
                        draw.translucency = surface.translucency;
                        if (draw.alphaMode == 0 && draw.albedo.w < 1.0f) draw.albedo.w = 1.0f;
                    }
                }
                draws.push_back(draw);
            }
        }
    });
}

void GpuSceneRenderer::assignInstanceBatches(std::vector<MeshDraw>& draws,
                                             const GpuSceneRenderOptions& options,
                                             std::vector<float>& instanceMatrices) const {
    instanceMatrices.clear();
    for (MeshDraw& draw : draws) {
        draw.batchCount = 1;
        draw.skipColorDraw = false;
        draw.batchByteOffset = 0;
    }
    if (!options.features.instancingEnabled || options.wireframeMeshes || !m_instancedPipeline) return;

    std::vector<InstanceBatchItem> items(draws.size());
    for (u32 i = 0; i < draws.size(); ++i) {
        const MeshDraw& draw = draws[i];
        const bool singleSubmesh = draw.mesh && (draw.mesh->subMeshes.size() <= 1 || draw.onlySubMesh >= 0);
        // Probed and translucent draws need per-object data (cube map, sort order).
        const bool hasColours = draw.mesh && !draw.mesh->vertexColors.empty() &&
                                draw.mesh->vertexColors.size() == draw.mesh->vertices.size();
        items[i].allow = singleSubmesh && !draw.isTerrain && !draw.displaced && draw.probeIndex < 0 &&
                         draw.micromeshPixels <= 0.0f &&
                         !draw.isTranslucent() && (!hasColours || m_foliageInstancedPipeline);
        u64 key = static_cast<u64>(reinterpret_cast<uintptr_t>(draw.mesh));
        auto mixBytes = [&](const std::string& text) {
            for (unsigned char c : text) key = key * 131u + c;
        };
        auto mixFloat = [&](f32 value) {
            u32 bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            key = key * 131u + bits;
        };
        mixBytes(draw.customTexturePath);
        mixBytes(draw.customNormalPath);
        mixBytes(draw.ormMapPath);
        mixBytes(draw.emissionMapPath);
        mixFloat(draw.albedo.x);
        mixFloat(draw.albedo.y);
        mixFloat(draw.albedo.z);
        mixFloat(draw.albedo.w);
        mixFloat(draw.metallic);
        mixFloat(draw.roughness);
        mixFloat(draw.reflectance);
        mixFloat(draw.emission.x);
        mixFloat(draw.emission.y);
        mixFloat(draw.emission.z);
        mixFloat(draw.uvTiling.x);
        mixFloat(draw.uvTiling.y);
        mixFloat(draw.uvOffset.x);
        mixFloat(draw.uvOffset.y);
        mixFloat(draw.uvObjectScale ? 1.0f : 0.0f);
        mixFloat(draw.normalStrength);
        mixFloat(draw.aoStrength);
        mixFloat(draw.heightScale);
        mixFloat(draw.parallaxSteps);
        mixFloat(draw.clearcoat);
        mixFloat(draw.clearcoatRoughness);
        mixFloat(draw.sheenColor.x);
        mixFloat(draw.sheenColor.y);
        mixFloat(draw.sheenColor.z);
        mixFloat(draw.sheenRoughness);
        mixFloat(draw.iridescence);
        mixFloat(draw.iridescenceThickness);
        mixFloat(draw.windStrength);
        mixFloat(draw.windSpeed);
        mixFloat(draw.windFlutter);
        mixFloat(draw.translucency);
        mixFloat(draw.modTint.x);
        mixFloat(draw.modTint.y);
        mixFloat(draw.modTint.z);
        mixFloat(draw.modWetness);
        mixFloat(draw.modSnow);
        mixFloat(draw.modAge);
        key = key * 131u + draw.alphaMode;
        key = key * 131u + static_cast<u32>(draw.onlySubMesh + 1);
        if (draw.receiveShadows) key ^= 0x9e3779b97f4a7c15ull;
        items[i].key = key == 0 ? 1 : key;
    }
    const u32 limit = std::clamp(options.features.maxInstancesPerBatch, 2u, 256u);
    const std::vector<InstanceBatchGroup> groups = buildInstanceBatches(items, limit);
    for (const InstanceBatchGroup& group : groups) {
        if (group.members.size() < 2) continue;
        MeshDraw& head = draws[group.members.front()];
        head.batchCount = static_cast<u32>(group.members.size());
        head.batchByteOffset = static_cast<u32>(instanceMatrices.size() * sizeof(float));
        for (u32 n = 0; n < group.members.size(); ++n) {
            const float* matrix = draws[group.members[n]].worldMatrix.data();
            instanceMatrices.insert(instanceMatrices.end(), matrix, matrix + 16);
            if (n > 0) draws[group.members[n]].skipColorDraw = true;
        }
    }
}

bool GpuSceneRenderer::ensureReflectionTargets(u32 width, u32 height) {
    if (m_reflectionColor && m_reflectionDepth && m_reflectionGbuffer.valid() &&
        m_reflectionWidth == width && m_reflectionHeight == height) {
        return true;
    }
    if (m_reflectionColor) m_device->destroyTexture(m_reflectionColor);
    if (m_reflectionDepth) m_device->destroyTexture(m_reflectionDepth);
    destroyGBufferTargets(m_device, m_reflectionGbuffer);
    m_reflectionColor = nullptr;
    m_reflectionDepth = nullptr;
    RHI::TextureDesc colorDesc;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = kHdrFormat;
    colorDesc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    RHI::TextureDesc depthDesc;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.format = RHI::TextureFormat::D32_FLOAT;
    depthDesc.usage = RHI::TextureUsage::DepthStencil;
    m_reflectionColor = m_device->createTexture(colorDesc);
    m_reflectionDepth = m_device->createTexture(depthDesc);
    const bool gbufferOk = createGBufferTargets(m_device, width, height, m_reflectionGbuffer);
    m_reflectionWidth = width;
    m_reflectionHeight = height;
    m_reflectionValid = false;
    return m_reflectionColor && m_reflectionDepth && gbufferOk;
}

void GpuSceneRenderer::renderPlanarReflection(RHI::CommandBuffer* cmd, ECS::World& world,
                                              const GpuSceneCamera& camera, u32 width, u32 height,
                                              const std::string& projectRoot,
                                              const GpuSceneRenderOptions& options) {
    const f32 scale = std::clamp(options.features.reflectionResolutionScale, 0.25f, 1.0f);
    const u32 rw = std::max(64u, static_cast<u32>(static_cast<f32>(width) * scale));
    const u32 rh = std::max(64u, static_cast<u32>(static_cast<f32>(height) * scale));
    if (!ensureReflectionTargets(rw, rh)) return;

    const f32 planeY = options.features.reflectionPlaneY;
    auto mirrorY = [&](Vec3 p) {
        p.y = planeY * 2.0f - p.y;
        return p;
    };
    GpuSceneCamera reflected = camera;
    reflected.position = mirrorY(camera.position);
    reflected.focus = mirrorY(camera.focus);
    reflected.view = Mat4::lookAt(reflected.position, reflected.focus, Vec3(0.0f, -1.0f, 0.0f));
    const f32 aspect = static_cast<f32>(rw) / static_cast<f32>(std::max(rh, 1u));
    reflected.proj = Mat4::perspective(camera.fovRad, aspect, camera.nearClip, camera.farClip);
    m_reflectionVP = reflected.proj * reflected.view;

    GpuSceneRenderOptions inner = options;
    inner.features.reflections = ReflectionMode::Off;
    inner.features.volumetrics = VolumetricQuality::Off;
    inner.features.instancingEnabled = false;
    inner.enableShadows = false;
    inner.clipBelowEnabled = true;
    inner.clipBelowY = planeY;
    inner.cameraSettled = true;
    inner.resolveFeaturesFromScene = false;
    inner.resolveEnvironmentFromScene = false;
    inner.colorLayer = 0;
    inner.grid.enabled = false;
    CF_GPU_SCOPE(cmd, "GpuSceneRenderer::planarReflection");
    const bool wasInReflection = m_inReflectionPass;
    m_inReflectionPass = true;
    ScenePassContext innerPass;
    innerPass.gbuffer = m_reflectionGbuffer;
    renderSceneHdr(cmd, world, reflected, m_reflectionColor, m_reflectionDepth, rw, rh, projectRoot,
                   inner, innerPass);
    m_inReflectionPass = wasInReflection;
    m_reflectionValid = true;
}

void GpuSceneRenderer::resolveEnvironment(ECS::World& world, const std::string& projectRoot,
                                          const GpuSceneRenderOptions& options) {
    std::string path = options.environmentPath;
    f32 exposure = options.environmentExposure;
    if (options.resolveEnvironmentFromScene) {
        const Scene::ActiveSkybox sky = Scene::findActiveSkybox(world);
        if (sky.component) {
            path = Scene::resolveSkyboxTexturePath(*sky.component, projectRoot).string();
            exposure = sky.component->exposure;
        }
    }
    m_environment.load(m_device, path);
    m_environmentExposure = std::clamp(exposure, 0.0f, 8.0f);
}

void GpuSceneRenderer::releaseProbe(ReflectionProbe& probe) {
    if (m_device) {
        if (probe.cube) m_device->destroyTexture(probe.cube);
        if (probe.depth) m_device->destroyTexture(probe.depth);
        destroyGBufferTargets(m_device, probe.gbuffer);
    }
    probe = ReflectionProbe{};
}

bool GpuSceneRenderer::captureProbe(RHI::CommandBuffer* cmd, ECS::World& world,
                                    ReflectionProbe& probe, const GpuSceneCamera& camera,
                                    const std::string& projectRoot,
                                    const GpuSceneRenderOptions& options) {
    u32 resolution = 32;
    while (resolution < std::clamp(options.features.probeResolution, 32u, 512u)) resolution <<= 1;
    if (!probe.cube || !probe.depth || !probe.gbuffer.valid() ||
        probe.resolution != resolution) {
        if (probe.cube) m_device->destroyTexture(probe.cube);
        if (probe.depth) m_device->destroyTexture(probe.depth);
        destroyGBufferTargets(m_device, probe.gbuffer);
        probe.cube = nullptr;
        probe.depth = nullptr;
        RHI::TextureDesc cubeDesc;
        cubeDesc.width = resolution;
        cubeDesc.height = resolution;
        cubeDesc.format = kHdrFormat;
        cubeDesc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
        cubeDesc.type = RHI::TextureType::Cube;
        cubeDesc.mipLevels = probeMipCount(resolution);
        probe.cube = m_device->createTexture(cubeDesc);
        RHI::TextureDesc depthDesc;
        depthDesc.width = resolution;
        depthDesc.height = resolution;
        depthDesc.format = RHI::TextureFormat::D32_FLOAT;
        depthDesc.usage = RHI::TextureUsage::DepthStencil;
        probe.depth = m_device->createTexture(depthDesc);
        probe.resolution = resolution;
        probe.mipLevels = cubeDesc.mipLevels;
        probe.valid = false;
        if (!probe.cube || !probe.depth ||
            !createGBufferTargets(m_device, resolution, resolution, probe.gbuffer)) {
            return false;
        }
    }

    GpuSceneRenderOptions inner = options;
    inner.features.reflections = ReflectionMode::Off;
    inner.features.volumetrics = VolumetricQuality::Off;
    inner.features.occlusion = OcclusionMode::Off;
    inner.features.instancingEnabled = false;
    inner.resolveFeaturesFromScene = false;
    inner.resolveEnvironmentFromScene = false;
    inner.enableShadows = false;
    inner.cameraSettled = true;
    inner.clipBelowEnabled = false;
    inner.excludeEntity = probe.entity;
    inner.grid.enabled = false;
    inner.drawSky = true;
    inner.textureQualityViewers.clear();

    CF_GPU_SCOPE(cmd, "GpuSceneRenderer::probe");
    const bool wasInReflection = m_inReflectionPass;
    m_inReflectionPass = true;
    for (u32 face = 0; face < 6; ++face) {
        GpuSceneCamera faceCam;
        faceCam.position = probe.origin;
        faceCam.fovRad = kPi * 0.5f;
        faceCam.nearClip = 0.05f;
        faceCam.farClip = camera.farClip;
        cubeFaceCamera(probe.origin, face, faceCam.nearClip, faceCam.farClip, faceCam.view,
                       faceCam.proj, faceCam.focus);
        inner.colorLayer = face;
        ScenePassContext innerPass;
        innerPass.cycleTargets = false;
        innerPass.gbuffer = probe.gbuffer;
        renderSceneHdr(cmd, world, faceCam, probe.cube, probe.depth, resolution, resolution,
                       projectRoot, inner, innerPass);
    }
    m_inReflectionPass = wasInReflection;
    if (probe.mipLevels > 1) cmd->generateMipmaps(probe.cube);
    probe.valid = true;
    return true;
}

void GpuSceneRenderer::updateReflectionProbes(RHI::CommandBuffer* cmd, ECS::World& world,
                                              std::vector<MeshDraw>& draws,
                                              const GpuSceneCamera& camera,
                                              const std::string& projectRoot,
                                              const GpuSceneRenderOptions& options,
                                              u64 sceneHash, bool allowCapture) {
    for (MeshDraw& draw : draws) draw.probeIndex = -1;
    const RenderFeatureSettings& features = options.features;
    const u32 maxProbes = std::min(features.maxReflectionProbes, 16u);
    if (features.reflections != ReflectionMode::Probe || maxProbes == 0 || options.wireframeMeshes) {
        return;
    }

    struct Candidate {
        ECS::Entity entity;
        Vec3 center;
        f32 distanceSq = 0.0f;
    };
    std::vector<Candidate> candidates;
    for (const MeshDraw& draw : draws) {
        if (!draw.isGlossy() || !draw.hasAabb) continue;
        const bool seen = std::any_of(candidates.begin(), candidates.end(),
                                      [&](const Candidate& c) { return c.entity == draw.entity; });
        if (seen) continue;
        const Vec3 center = (draw.aabbMin + draw.aabbMax) * 0.5f;
        candidates.push_back({draw.entity, center, (center - camera.position).lengthSquared()});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.distanceSq < b.distanceSq; });
    if (candidates.size() > maxProbes) candidates.resize(maxProbes);

    constexpr usize kMaxProbeSlots = 16;
    std::vector<i32> selected;
    for (const Candidate& candidate : candidates) {
        i32 index = -1;
        for (usize i = 0; i < m_probes.size(); ++i) {
            if (m_probes[i].world == &world && m_probes[i].entity == candidate.entity) {
                index = static_cast<i32>(i);
                break;
            }
        }
        if (index < 0) {
            for (usize i = 0; i < m_probes.size(); ++i) {
                if (!m_probes[i].entity.isValid()) {
                    index = static_cast<i32>(i);
                    break;
                }
            }
        }
        if (index < 0 && m_probes.size() < kMaxProbeSlots) {
            m_probes.emplace_back();
            index = static_cast<i32>(m_probes.size() - 1);
        }
        if (index < 0) {
            u64 oldest = m_frameCounter;
            for (usize i = 0; i < m_probes.size(); ++i) {
                if (m_probes[i].lastUsed < oldest) {
                    oldest = m_probes[i].lastUsed;
                    index = static_cast<i32>(i);
                }
            }
            if (index < 0) break;
        }
        ReflectionProbe& probe = m_probes[static_cast<usize>(index)];
        if (probe.world != &world || probe.entity != candidate.entity) {
            probe.world = &world;
            probe.entity = candidate.entity;
            probe.valid = false;
        }
        if ((probe.origin - candidate.center).lengthSquared() > 1e-4f) {
            probe.origin = candidate.center;
            probe.valid = false;
        }
        probe.lastUsed = m_frameCounter;
        selected.push_back(index);
        for (MeshDraw& draw : draws) {
            if (draw.entity == candidate.entity) draw.probeIndex = index;
        }
    }

    // Capture at most one probe per frame, nearest stale one first.
    u32 stale = 0;
    for (i32 index : selected) {
        ReflectionProbe& probe = m_probes[static_cast<usize>(index)];
        if (probe.valid && probe.sceneHash == sceneHash) continue;
        if (allowCapture && stale == 0 && captureProbe(cmd, world, probe, camera, projectRoot, options)) {
            probe.sceneHash = sceneHash;
            continue;
        }
        ++stale;
    }
    m_probesPending = m_probesPending || stale > 0;

    for (ReflectionProbe& probe : m_probes) {
        if (probe.entity.isValid() && probe.lastUsed + 600 < m_frameCounter) releaseProbe(probe);
    }
}

GpuSceneRenderer::ViewTargets* GpuSceneRenderer::acquireViewTargets(u64 viewId, u32 width,
                                                                    u32 height) {
    auto create = [&](ViewTargets& view) -> bool {
        view = ViewTargets{};
        view.viewId = viewId;
        view.width = width;
        view.height = height;
        RHI::TextureDesc colorDesc;
        colorDesc.width = width;
        colorDesc.height = height;
        colorDesc.format = kHdrFormat;
        colorDesc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
        RHI::TextureDesc depthDesc;
        depthDesc.width = width;
        depthDesc.height = height;
        depthDesc.format = RHI::TextureFormat::D32_FLOAT;
        depthDesc.usage = RHI::TextureUsage::DepthStencil | RHI::TextureUsage::Sampler;
        for (u32 i = 0; i < 2; ++i) {
            view.color[i] = m_device->createTexture(colorDesc);
            view.depth[i] = m_device->createTexture(depthDesc);
        }
        view.opaqueCopy = m_device->createTexture(colorDesc);
        RHI::TextureDesc sDesc = colorDesc;
        sDesc.format = RHI::TextureFormat::R16G16B16A16_FLOAT;
        view.sBuffer[0] = m_device->createTexture(sDesc);
        view.sBuffer[1] = m_device->createTexture(sDesc);
        return view.color[0] && view.color[1] && view.depth[0] && view.depth[1] &&
               view.opaqueCopy && view.sBuffer[0] && view.sBuffer[1] &&
               createGBufferTargets(m_device, width, height, view.gbuffer);
    };

    for (ViewTargets& view : m_views) {
        if (view.viewId != viewId) continue;
        if (view.width != width || view.height != height) {
            releaseViewTargets(view);
            if (!create(view)) return nullptr;
        }
        view.lastUsed = m_frameCounter;
        return &view;
    }

    ViewTargets* slot = nullptr;
    if (m_views.size() < kMaxViewTargets) {
        m_views.emplace_back();
        slot = &m_views.back();
    } else {
        slot = &m_views.front();
        for (ViewTargets& view : m_views) {
            if (view.lastUsed < slot->lastUsed) slot = &view;
        }
        releaseViewTargets(*slot);
    }
    if (!create(*slot)) {
        releaseViewTargets(*slot);
        return nullptr;
    }
    slot->lastUsed = m_frameCounter;
    return slot;
}

void GpuSceneRenderer::releaseViewTargets(ViewTargets& view) {
    if (m_device) {
        for (u32 i = 0; i < 2; ++i) {
            if (view.color[i]) m_device->destroyTexture(view.color[i]);
            if (view.depth[i]) m_device->destroyTexture(view.depth[i]);
        }
        if (view.opaqueCopy) m_device->destroyTexture(view.opaqueCopy);
        if (view.sBuffer[0]) m_device->destroyTexture(view.sBuffer[0]);
        if (view.sBuffer[1]) m_device->destroyTexture(view.sBuffer[1]);
        destroyGBufferTargets(m_device, view.gbuffer);
    }
    const u64 id = view.viewId;
    view = ViewTargets{};
    view.viewId = id;
}

u32 GpuSceneRenderer::renderSceneHdr(RHI::CommandBuffer* cmd, ECS::World& world,
                                     const GpuSceneCamera& camera, RHI::Texture* colorTarget,
                                     RHI::Texture* depthTarget, u32 width, u32 height,
                                     const std::string& projectRoot,
                                     const GpuSceneRenderOptions& opts, ScenePassContext& pass) {
    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(std::max(height, 1u));
    pass.viewProj = camera.proj * camera.view;
    const Spatial::Frustum frustum = Spatial::Frustum::fromCamera(
        camera.position, camera.focus, Vec3(0.0f, 1.0f, 0.0f), camera.fovRad, aspect,
        camera.nearClip, camera.farClip);

    // Stats belong to the outermost scene pass; reflection passes add to the same frame.
    struct FrameStats {
        RenderStats& building;
        RenderStats& committed;
        bool top;
        StatsClock::time_point start = StatsClock::now();
        ~FrameStats() {
            if (!top) return;
            building.totalMs =
                std::chrono::duration<f32, std::milli>(StatsClock::now() - start).count();
            committed = building;
        }
    } frameStats{m_building, m_stats, !m_inReflectionPass};
    if (frameStats.top) {
        m_building = RenderStats{};
        // The Micromesh draw references point into last frame's draw list: forget them before anything
        // (a planar reflection, a probe) can match a reused address.
        m_micromeshDraws.clear();
        m_micromeshMainView = ~0u;
    }

    std::vector<MeshDraw> draws;
    {
        CF_PROFILE_SCOPE("GpuSceneRenderer::gather");
        ScopedMs timer(m_building.gatherMs);
        draws = gatherMeshDraws(world, camera.position, frustum, projectRoot, opts);
    }
    // Upload GPU buffers before any render pass. Creating/uploading buffers
    // while a pass is recording is a common NVIDIA driver SIGSEGV.
    draws.erase(std::remove_if(draws.begin(), draws.end(),
                               [this](const MeshDraw& draw) {
                                   return !draw.mesh || !ensureMeshUploaded(draw.mesh) ||
                                          !draw.mesh->vertexBuffer || !draw.mesh->indexBuffer;
                               }),
                draws.end());
    m_building.meshesGathered += static_cast<u32>(draws.size());
    applyCoarseOcclusion(draws, camera, opts);

        if (pass.view) {
        m_materialPlanar = false;
        for (const MeshDraw& draw : draws) {
            if (draw.planarReceiver && draw.reflectQuality) m_materialPlanar = true;
        }
        pass.sceneHash = hashProbeScene(draws, m_environment.path());
        const bool allowExpensive =
            !opts.features.expensiveEffectsOnlyWhenSettled || opts.cameraSettled;
        if ((opts.features.reflections == ReflectionMode::Planar || m_materialPlanar) &&
            !opts.wireframeMeshes) {
            if (allowExpensive && !opts.clipBelowEnabled) {
                renderPlanarReflection(cmd, world, camera, width, height, projectRoot, opts);
            }
        } else {
            m_reflectionValid = false;
        }
        updateReflectionProbes(cmd, world, draws, camera, projectRoot, opts, pass.sceneHash,
                               allowExpensive);
    }

    std::vector<float> instanceMatrices;
    assignInstanceBatches(draws, opts, instanceMatrices);
    if (!instanceMatrices.empty() && m_instanceBuffer) {
        m_device->uploadBuffer(m_instanceBuffer, instanceMatrices.data(),
                               instanceMatrices.size() * sizeof(float));
    }
    // Terrain vegetation has instance buffers of its own: appended after the batching, which would reset their counts.
    if (!opts.wireframeMeshes && !m_inReflectionPass) appendVegetationDraws(world, camera.position, frustum, projectRoot, draws);

    if (!m_inReflectionPass) beginMicromeshFrame(cmd, draws);
    if (!m_micromeshDraws.empty() && !opts.wireframeMeshes) {
        // The camera's cut comes first: it is the one that must always fit in the arena (the shadow
        // cascades' cuts take what is left).
        f32 pixels = 1.5f;
        for (const auto& entry : m_micromeshDraws) {
            pixels = entry.first->micromeshPixels;
            break;
        }
        const f32 proj11 = std::max(std::abs(camera.proj(1, 1)), 1e-4f);
        MicromeshCullParams params;
        params.planes = planesFromViewProj(pass.viewProj);
        params.camera = camera.position;
        // Error in pixels -> world units per metre of distance: one pixel spans 2 / (proj11 * height).
        params.threshold = pixels * 2.0f / (proj11 * static_cast<f32>(std::max(height, 1u)));
        params.frustum = true;
        params.cone = true;
        params.viewDependent = true;
        // Occlusion: last frame's depth, reduced to a farthest-depth pyramid, tests the clusters of the
        // instances that opted in. Needs a previous frame of this view at the same size.
        MicromeshOcclusion occlusion;
        bool wantsOcclusion = false;
        for (const auto& entry : m_micromeshDraws) wantsOcclusion = wantsOcclusion || entry.first->micromeshOcclusion;
        m_micromeshOcclusionActive = false;
        if (wantsOcclusion && pass.view && pass.view->hasPrevious && !m_inReflectionPass) {
            RHI::Texture* previousDepth = pass.view->depth[pass.view->current ^ 1u];
            if (previousDepth && m_micromeshHzb.init(m_device, pass.view->width, pass.view->height) &&
                m_micromeshHzb.build(cmd, previousDepth, m_pointSampler)) {
                occlusion.hzb = &m_micromeshHzb;
                occlusion.prevView = pass.view->prevView;
                occlusion.p00 = pass.view->prevProj(0, 0);
                occlusion.p11 = pass.view->prevProj(1, 1);
                occlusion.p22 = pass.view->prevProj(2, 2);
                occlusion.p23 = pass.view->prevProj(2, 3);
                params.occlusion = &occlusion;
                m_micromeshOcclusionActive = true;
            }
        }
        m_micromeshMainView = cullMicromeshView(cmd, params);
    }

    // Weather first: it scales the sun every later stage of this frame sees (forward shader, clouds, fog, Photon).
    m_weather = Scene::resolveSceneWeather(world);
    updateWeatherDynamics();
    Scene::SceneLighting lighting;
    lighting.clear();
    Scene::collectSceneLights(world, lighting.lights);
    applyWeatherToLights(lighting.lights);

    // Resolve the procedural cloud/mist profile for the sky pass.
    m_cloudSettings = {};
    {
        ECS::ComponentQuery cloudQuery;
        cloudQuery.with<ECS::VolumetricCloudsComponent>();
        world.forEach<ECS::VolumetricCloudsComponent>(
            cloudQuery, [&](ECS::Entity, ECS::VolumetricCloudsComponent& clouds) {
                if (!clouds.enabled || clouds.quality == 0) return;
                GpuCloudSettings settings;
                settings.enabled = true;
                settings.steps = clouds.quality >= 3 ? 24u : clouds.quality == 2 ? 16u : 8u;
                settings.coverage = std::clamp(clouds.coverage, 0.0f, 1.0f);
                settings.density = std::max(clouds.density, 0.0f);
                settings.baseAltitude = clouds.baseAltitude;
                settings.topAltitude =
                    std::max(clouds.topAltitude, clouds.baseAltitude + 1.0f);
                settings.detail = std::clamp(clouds.detail, 0.0f, 1.0f);
                settings.seed = clouds.seed;
                settings.windSpeed = clouds.windSpeed;
                settings.windDirection = clouds.windDirection;
                settings.mistDensity = std::clamp(clouds.mistDensity, 0.0f, 1.0f);
                settings.mistHeight = std::max(clouds.mistHeight, 1.0f);
                settings.mistFalloff = clouds.mistFalloff;
                settings.sunScatter = std::max(clouds.sunScatter, 0.0f);
                settings.tint = Vec3(std::max(clouds.tint.x, 0.0f), std::max(clouds.tint.y, 0.0f),
                                     std::max(clouds.tint.z, 0.0f));
                settings.darkness = std::clamp(clouds.darkness, 0.0f, 1.0f);
                settings.skyLight = std::max(clouds.skyLight, 0.0f);
                settings.silverLining = std::clamp(clouds.silverLining, 0.0f, 2.0f);
                settings.groundShadows = clouds.groundShadows;
                settings.shadowStrength = std::clamp(clouds.shadowStrength, 0.0f, 1.0f);
                settings.sunShafts = std::clamp(clouds.sunShafts, 0.0f, 1.0f);
                // A weather component couples the clouds to the rain (thicker, darker, more cover).
                if (m_weather.cloudsLinked) {
                    settings.coverage = std::clamp(m_weather.cloudCoverage, 0.0f, 1.0f);
                    settings.density = std::max(m_weather.cloudDensity, 0.0f);
                    settings.darkness = std::clamp(m_weather.cloudDarkness, 0.0f, 1.0f);
                }
                settings.time = m_cloudTime;
                if (!lighting.lights.directionals.empty()) {
                    const auto& d = lighting.lights.directionals[0];
                    settings.sunDirection = (d.direction * -1.0f).normalized();
                    settings.sunColor =
                        srgbToLinear(Vec3(d.color.x, d.color.y, d.color.z));
                    settings.sunIntensity = std::max(d.intensity, 0.0f);
                }
                m_cloudSettings = settings;
            });
    }

    m_weatherSunIntensity = 0.0f;
    if (!lighting.lights.directionals.empty()) {
        const auto& d = lighting.lights.directionals[0];
        m_weatherSunDirection = (d.direction * -1.0f).normalized();
        m_weatherSunColor = srgbToLinear(Vec3(d.color.x, d.color.y, d.color.z));
        m_weatherSunIntensity = std::max(d.intensity, 0.0f);
    }

    // Let the cloud mist drive the existing scene volumetric fog so ground
    // geometry is wrapped in the same mist as the sky.
    GpuSceneRenderOptions renderOptions = opts;
    if (m_cloudSettings.enabled && m_cloudSettings.mistDensity > 0.0f) {
        if (renderOptions.features.volumetrics == VolumetricQuality::Off) {
            renderOptions.features.volumetrics = VolumetricQuality::Low;
        }
        renderOptions.features.volumetricDensity = std::max(
            renderOptions.features.volumetricDensity, m_cloudSettings.mistDensity * 0.05f);
        renderOptions.features.volumetricHeight = std::max(
            renderOptions.features.volumetricHeight, m_cloudSettings.mistHeight);
    }

    m_activeCascadeCount = opts.directionalCascadeCount;
    m_shadowProjectRoot = projectRoot;
    m_shadowTextureQuality = opts.textureQuality;
    if (!opts.wireframeMeshes && opts.enableShadows && !draws.empty()) {
        CF_GPU_SCOPE(cmd, "GpuSceneRenderer::shadows");
        ScopedMs timer(m_building.shadowsMs);
        renderDirectionalShadows(cmd, world, lighting, draws, camera);
        renderPointShadows(cmd, world, lighting, draws);
        renderSpotShadows(cmd, world, lighting, draws);
    } else {
        // Shadows off: stale maps from an earlier frame must not keep shading the scene.
        for (u32 slot = 0; slot < GpuDirectionalShadowMap::kMaxDirectionalShadowLights; ++slot) {
            m_directionalShadows.clearSlot(slot);
        }
        for (u32 slot = 0; slot < GpuPointShadowMap::kMaxPointShadowLights; ++slot) {
            m_pointShadows.clearSlot(slot);
        }
        for (u32 slot = 0; slot < GpuSpotShadowMap::kMaxSpotShadowLights; ++slot) {
            m_spotShadows.clearSlot(slot);
        }
    }

    {
        const Vec3 right(camera.view(0, 0), camera.view(0, 1), camera.view(0, 2));
        const Vec3 up(camera.view(1, 0), camera.view(1, 1), camera.view(1, 2));
        Effects::buildEffectMesh(world, camera.position, right, up, m_effectMesh);
        if (!m_device || m_effectMesh.vertices.empty() || m_effectMesh.indices.empty()) {
            destroyMeshGpu(m_effectMesh);
        } else {
            const u64 vertexBytes = m_effectMesh.vertices.size() * sizeof(Assets::Vertex3D);
            const u64 indexBytes = m_effectMesh.indices.size() * sizeof(u32);
            if (!m_effectMesh.vertexBuffer || m_effectMesh.vertexBuffer->size != vertexBytes ||
                !m_effectMesh.indexBuffer || m_effectMesh.indexBuffer->size != indexBytes) {
                destroyMeshGpu(m_effectMesh);
                RHI::BufferDesc vertices;
                vertices.size = vertexBytes;
                m_effectMesh.vertexBuffer = m_device->createBuffer(vertices, RHI::BufferUsage::Vertex);
                RHI::BufferDesc indices;
                indices.size = indexBytes;
                m_effectMesh.indexBuffer = m_device->createBuffer(indices, RHI::BufferUsage::Index);
            }
            if (m_effectMesh.vertexBuffer) {
                m_device->uploadBuffer(m_effectMesh.vertexBuffer, m_effectMesh.vertices.data(), vertexBytes);
            }
            if (m_effectMesh.indexBuffer) {
                m_device->uploadBuffer(m_effectMesh.indexBuffer, m_effectMesh.indices.data(), indexBytes);
            }
        }
    }

    CF_PROFILE_SCOPE("GpuSceneRenderer::draw");
    return renderMeshes(cmd, draws, camera, lighting, colorTarget, depthTarget, width, height,
                        projectRoot, renderOptions, pass);
}

ECS::PostProcessComponent GpuSceneRenderer::resolvePostProcess(
    ECS::World& world, ECS::Entity camera, const ECS::PostProcessComponent& fallback) {
    if (const ECS::PostProcessComponent* fx = findPostProcessForCamera(world, camera)) return *fx;
    return fallback;
}

u32 GpuSceneRenderer::renderWithCamera(RHI::CommandBuffer* cmd, ECS::World& world,
                                        const GpuSceneCamera& cameraIn, RHI::Texture* colorTarget,
                                        RHI::Texture* depthTarget, u32 width, u32 height,
                                        const std::string& projectRoot,
                                        const GpuSceneRenderOptions& options) {
    CF_PROFILE_SCOPE("GpuSceneRenderer::renderWithCamera");
    Caffeine::Debug::setCrashBreadcrumb("GpuSceneRenderer::renderWithCamera");
    (void)depthTarget;
    if (!m_ready || !cmd || !colorTarget || width < 1 || height < 1) return 0;
    ++m_frameCounter;
    m_probesPending = false;

    GpuSceneRenderOptions opts = options;
    // The simulation clock: what clouds, weather, wind and foliage animate by. A delta of 0 (the editor while it
    // is not playing) freezes all of them; the frame counter above keeps counting for the temporal effects.
    const f32 simStep = std::clamp(options.deltaTime, 0.0f, 0.25f);
    m_cloudTime = std::fmod(m_cloudTime + simStep, 3600.0f);
    m_simTime = std::fmod(m_simTime + simStep, 86400.0f);
    if (options.resolveFeaturesFromScene) opts.features = resolveForwardRenderFeatures(world);
    resolveEnvironment(world, projectRoot, opts);
    m_weather = Scene::resolveSceneWeather(world);
    updateWeatherDynamics();
    ECS::PostProcessComponent post =
        opts.resolvePostProcessFromScene
            ? resolvePostProcess(world, opts.postProcessCamera, opts.postProcess)
            : opts.postProcess;

    ECS::PhotonGIComponent photon;
    {
        ECS::ComponentQuery query;
        query.with<ECS::PhotonGIComponent>();
        world.forEach<ECS::PhotonGIComponent>(query, [&](ECS::Entity, ECS::PhotonGIComponent& c) {
            if (c.enabled) photon = c;
        });
    }
    // Overcast / fog change the sky light Photon falls back to (same factors as the forward ambient).
    photon.skyIntensity *= m_weather.skyScale * (1.0f + 2.5f * m_lightning.flash);
    if (opts.overrideAntiAliasing) post.antiAliasing = opts.antiAliasingOverride;
    // Wireframe is a debug view: stale G-buffer data would feed Photon garbage.
    if (opts.wireframeMeshes) photon.enabled = false;

    const f32 scale = std::clamp(opts.renderScale, 0.25f, 2.0f);
    const u32 sceneW = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(width) * scale)));
    const u32 sceneH = std::max(1u, static_cast<u32>(std::lround(static_cast<f32>(height) * scale)));
    const u64 viewId = opts.viewId ? opts.viewId : static_cast<u64>(reinterpret_cast<uintptr_t>(colorTarget));
    ViewTargets* view = acquireViewTargets(viewId, sceneW, sceneH);
    if (!view) return 0;

    // A large jump between frames is a camera cut: history would smear.
    if (view->hasPrevious && (cameraIn.position - view->prevCameraPos).lengthSquared() > 25.0f) {
        view->hasPrevious = false;
    }

    GpuSceneCamera camera = cameraIn;
    const Mat4 unjitteredViewProj = cameraIn.proj * cameraIn.view;
    const bool taa = PostProcessStack::usesTaa(post) && !opts.wireframeMeshes && !opts.skipTemporalSettle;
    Vec2 jitter(0.0f, 0.0f);
    if (taa) {
        jitter = PostProcessStack::taaJitter(view->frameIndex);
        camera.proj(0, 2) += jitter.x * 2.0f / static_cast<f32>(sceneW);
        camera.proj(1, 2) += jitter.y * 2.0f / static_cast<f32>(sceneH);
    }

    ScenePassContext pass;
    pass.view = view;
    pass.gbuffer = view->gbuffer;
    const bool ssrFromPost = post.enabled && post.screenSpaceReflections.enabled;
    const bool ssrFromFeatures =
        opts.features.reflections == ReflectionMode::ScreenSpace ||
        (opts.features.reflections != ReflectionMode::Off && opts.features.screenSpaceTrace);
    if ((ssrFromPost || ssrFromFeatures) && !opts.wireframeMeshes) {
        pass.ssrIntensity = ssrFromPost ? post.screenSpaceReflections.intensity : opts.features.ssrIntensity;
        pass.ssrMaxRoughness =
            ssrFromPost ? post.screenSpaceReflections.maxRoughness : opts.features.ssrMaxRoughness;
        pass.ssrSteps = std::clamp(ssrFromPost ? post.screenSpaceReflections.maxSteps
                                               : opts.features.ssrMaxSteps,
                                   4u, 64u);
        pass.ssrMaxDistance = std::max(opts.features.ssrMaxDistance, 1.0f);
    }

    RHI::Texture* hdr = view->color[view->current];
    RHI::Texture* depth = view->depth[view->current];
    const u32 drawn = renderSceneHdr(cmd, world, camera, hdr, depth, sceneW, sceneH, projectRoot,
                                     opts, pass);

    // P2 compute path: Min-Z + cascade-0 trace into the SH atlas. Any failure
    // leaves photonSH null and the post stack uses the screen-space fallback.
    RHI::Texture* photonSH = nullptr;
    u32 photonGridW = 0;
    u32 photonGridH = 0;
    u32 photonS0 = 0;
    const bool feedbackValid = view->sValid && view->hasPrevious;
    RHI::Texture* feedbackPrev = feedbackValid ? view->sBuffer[view->sIndex] : nullptr;
    const bool photonWanted = photon.enabled && photon.quality != ECS::PhotonQuality::Off &&
                              photon.path == ECS::PhotonPath::ComputeC0 && !opts.wireframeMeshes &&
                              view->gbuffer.valid();
    m_pv = photonWanted ? &photonViewFor(viewId) : nullptr;
    f32 giTemporalWeight = 0.4f;
    if (photonWanted && m_pv->cascades.ready()) {
        m_pv->cascades.setSkyTint(m_weather.skyTint);
        CF_GPU_SCOPE(cmd, "Photon::cascades");
        // The whole scene's triangles for the voxeliser: hashed every frame (cheap), gathered and
        // uploaded only when something that shapes them changed.
        PhotonLights lights;
        {
            // The scene's direct lights for the voxel injection (the same radiance the forward pass uses).
            Scene::SceneLighting gi;
            gi.clear();
            Scene::collectSceneLights(world, gi.lights);
            applyWeatherToLights(gi.lights);
            for (const auto& d : gi.lights.directionals) {
                if (lights.directionalCount >= PhotonLights::kMaxDirectional) break;
                const Vec3 c = srgbToLinear(Vec3(d.color.x, d.color.y, d.color.z)) * d.intensity;
                lights.directionalDir[lights.directionalCount] = Vec4(d.direction.x, d.direction.y, d.direction.z, 0.0f);
                lights.directionalColor[lights.directionalCount++] = Vec4(c.x, c.y, c.z, 0.0f);
            }
            for (const auto& p : gi.lights.points) {
                if (lights.pointCount >= PhotonLights::kMaxPoint) break;
                const Vec3 c = srgbToLinear(Vec3(p.color.x, p.color.y, p.color.z)) * p.intensity;
                lights.pointPosition[lights.pointCount] = Vec4(p.position.x, p.position.y, p.position.z, p.radius);
                lights.pointColor[lights.pointCount++] = Vec4(c.x, c.y, c.z, 0.0f);
            }
            for (const auto& sp : gi.lights.spots) {
                if (lights.spotCount >= PhotonLights::kMaxSpot) break;
                const Vec3 c = srgbToLinear(Vec3(sp.color.x, sp.color.y, sp.color.z)) * sp.intensity;
                const f32 halfAngle = std::clamp(sp.angle * kPi / 180.0f * 0.5f, 0.01f, 1.5533f);
                lights.spotPosition[lights.spotCount] = Vec4(sp.position.x, sp.position.y, sp.position.z, sp.radius);
                lights.spotDirection[lights.spotCount] = Vec4(sp.direction.x, sp.direction.y, sp.direction.z, std::cos(halfAngle));
                lights.spotColor[lights.spotCount++] = Vec4(c.x, c.y, c.z, 0.0f);
            }
            {
                // The clouds' shadow on the sun, so what bounces off the ground follows it (the near field already
                // does: it reads the forward pass's direct light).
                const GpuCloudSettings& cs = m_cloudSettings;
                if (cs.enabled && cs.groundShadows && !opts.wireframeMeshes) {
                    const f32 windAngle = cs.windDirection * kPi / 180.0f;
                    const f32 drift = cs.windSpeed * cs.time;
                    lights.cloudShadow[0] = Vec4(std::max(cs.shadowStrength, m_weather.cloudShadowStrength),
                                                 cs.coverage, cs.density, 0.0f);
                    lights.cloudShadow[1] = Vec4(std::cos(windAngle) * drift + cs.seed, cs.seed,
                                                 std::sin(windAngle) * drift + cs.seed, cs.baseAltitude);
                    lights.cloudShadow[2] = Vec4(cs.sunDirection.x, cs.sunDirection.y, cs.sunDirection.z,
                                                 cs.topAltitude);
                }
            }
            m_pv->cascades.setLights(lights);
        }
        bool sceneChanged = false;
        if (m_pv->cascades.hasTriangleVoxelizer()) {
            CF_PROFILE_SCOPE("Photon::collectGeometry");
            sceneChanged = updatePhotonGeometry(world, projectRoot, photon);
        }
        // The resolve averages frames in time, which makes a moved object or light leave a ghost. While the scene
        // changes the average follows the present frame closely; it narrows again as the scene stays still.
        {
            u64 lightHash = 1469598103934665603ull;
            auto mix = [&lightHash](const void* data, usize bytes) {
                const u8* p = static_cast<const u8*>(data);
                for (usize i = 0; i < bytes; ++i) {
                    lightHash ^= p[i];
                    lightHash *= 1099511628211ull;
                }
            };
            mix(&lights.directionalCount, sizeof(u32));
            mix(&lights.pointCount, sizeof(u32));
            mix(&lights.spotCount, sizeof(u32));
            for (u32 i = 0; i < lights.directionalCount; ++i) {
                mix(&lights.directionalDir[i], sizeof(Vec4));
                mix(&lights.directionalColor[i], sizeof(Vec4));
            }
            for (u32 i = 0; i < lights.pointCount; ++i) {
                mix(&lights.pointPosition[i], sizeof(Vec4));
                mix(&lights.pointColor[i], sizeof(Vec4));
            }
            for (u32 i = 0; i < lights.spotCount; ++i) {
                mix(&lights.spotPosition[i], sizeof(Vec4));
                mix(&lights.spotDirection[i], sizeof(Vec4));
                mix(&lights.spotColor[i], sizeof(Vec4));
            }
            if (lightHash != m_pv->lightHash) sceneChanged = true;
            m_pv->lightHash = lightHash;
            m_pv->calmFrames = sceneChanged ? 0u : std::min(m_pv->calmFrames + 1u, 1000u);
            // 0.4 right after a change, falling to 0.07 within about five calm frames. The resolve also follows a
            // large difference between history and present quickly, so a slow average cannot keep a stale picture.
            const f32 settled = 0.07f;
            m_pv->temporalWeight =
                std::max(settled, 0.4f * std::pow(0.7f, static_cast<f32>(m_pv->calmFrames)));
            giTemporalWeight = m_pv->temporalWeight;
            m_pv->cascades.setTemporalWeight(m_pv->temporalWeight);
        }
        if (m_pv->cascades.execute(cmd, depth, view->gbuffer.albedo,
                                     view->gbuffer.normalMaterial, view->gbuffer.direct,
                                     camera.view, camera.proj, sceneW, sceneH, photon,
                                     static_cast<u32>(view->frameIndex), feedbackPrev,
                                     view->prevViewProj, feedbackValid)) {
            photonSH = m_pv->cascades.shAtlas();
            photonGridW = m_pv->cascades.gridWidth();
            photonGridH = m_pv->cascades.gridHeight();
            photonS0 = m_pv->cascades.probeSpacing();
        }
    }

    {
        CF_GPU_SCOPE(cmd, "GpuSceneRenderer::post");
        PostProcessInputs in;
        in.sceneColor = hdr;
        in.sceneDepth = depth;
        in.sceneWidth = sceneW;
        in.sceneHeight = sceneH;
        in.output = colorTarget;
        in.outputWidth = width;
        in.outputHeight = height;
        in.proj = camera.proj;
        in.viewProj = camera.proj * camera.view;
        in.view = camera.view;
        in.prevViewProj = view->hasPrevious ? view->prevViewProj : unjitteredViewProj;
        in.cameraPos = camera.position;
        in.deltaTime = std::clamp(opts.deltaTime, 1e-4f, 0.25f);
        in.time = m_simTime;
        in.frameIndex = view->frameIndex;
        in.historyValid = view->hasPrevious && !opts.skipTemporalSettle;
        in.jitterPixels = jitter;
        in.viewId = viewId;
        in.photon = photon;
        in.weather = buildPostWeather(camera, width, height);
        in.photonSkyTint = m_weather.skyTint;
        in.gbufferAlbedo = view->gbuffer.albedo;
        in.gbufferNormalMaterial = view->gbuffer.normalMaterial;
        in.gbufferDirect = view->gbuffer.direct;
        in.photonSH = photonSH;
        in.photonGridW = photonGridW;
        in.photonGridH = photonGridH;
        in.photonS0 = photonS0;
        in.photonS = photonSH ? view->sBuffer[view->sIndex ^ 1] : nullptr;
        in.photonSPrev = photonSH ? feedbackPrev : nullptr;
        in.photonTemporalWeight = giTemporalWeight;
        m_post.execute(cmd, post, in);
    }
    if (photonSH) {
        // The resolve wrote S(t) into the other buffer.
        view->sIndex ^= 1;
        view->sValid = true;
    }

    // Keep redrawing until temporal effects converge after anything changes.
    u64 changeHash = pass.sceneHash;
    {
        // Weather changes what every pixel shows without touching a draw.
        const f32 weatherKey[] = {m_weather.fogDensity, m_weather.fogHeightFalloff, m_weather.fogBaseHeight,
                                  m_weather.fogStart, m_weather.fogMaxOpacity, m_weather.fogColor.x,
                                  m_weather.fogColor.y, m_weather.fogColor.z, m_weather.fogSkyBlend,
                                  m_weather.fogSunScatter, m_weather.rainIntensity, m_weather.wetness,
                                  m_weather.sunScale, m_weather.skyScale, m_weather.cloudCoverage,
                                  m_weather.cloudDensity, m_weather.cloudDarkness};
        for (f32 value : weatherKey) {
            u32 bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            changeHash = (changeHash ^ bits) * 1099511628211ull;
        }
    }
    for (u32 i = 0; i < 16; ++i) {
        u32 bits = 0;
        std::memcpy(&bits, unjitteredViewProj.data() + i, sizeof(bits));
        changeHash = (changeHash ^ bits) * 1099511628211ull;
    }
    if (opts.skipTemporalSettle) {
        view->settleFrames = 0;
    } else if (changeHash != view->prevSceneHash) {
        u32 settle = 2;
        if (taa) settle = 16;
        view->settleFrames = std::max(view->settleFrames, settle);
    } else if (view->settleFrames > 0) {
        --view->settleFrames;
    }
    view->prevSceneHash = changeHash;

    view->prevViewProj = unjitteredViewProj;
    view->prevView = cameraIn.view;
    view->prevProj = cameraIn.proj;
    view->prevCameraPos = cameraIn.position;
    view->hasPrevious = true;
    view->current ^= 1u;
    ++view->frameIndex;

    const bool continuous = post.enabled && (post.autoExposure.enabled ||
                                             (post.grain.enabled && post.grain.intensity > 0.0f));
    // Falling rain moves every frame.
    m_needsAnotherFrame = continuous || m_weather.rain || m_weather.lightning || m_probesPending || view->settleFrames > 0;
    return drawn;
}

u32 GpuSceneRenderer::render(RHI::CommandBuffer* cmd, ECS::World& world,
                              const Editor::EditorContext& ctx, RHI::Texture* colorTarget,
                              RHI::Texture* depthTarget, u32 width, u32 height,
                              const std::string& projectRoot, const GpuSceneRenderOptions& options) {
    if (!m_ready || !cmd || !colorTarget || width < 1 || height < 1) return 0;
    if (ctx.viewMode != Editor::EditorContext::ViewMode::Mode3D &&
        ctx.viewMode != Editor::EditorContext::ViewMode::Isometric) {
        return 0;
    }

    const Vec3 cameraPos =
        Editor::editorCameraPosition(ctx.camYaw, ctx.camPitch, ctx.camDistance, ctx.camFocus);
    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(std::max(height, 1u));
    const f32 farPlane = ctx.cameraFarPlane();
    constexpr f32 kFovY = 60.0f * kPi / 180.0f;

    GpuSceneCamera camera;
    camera.position = cameraPos;
    camera.focus = ctx.camFocus;
    camera.view = Mat4::lookAt(cameraPos, ctx.camFocus, Vec3(0, 1, 0));
    const bool ortho = options.orthographic ||
                       ctx.projection == Editor::EditorContext::ViewProjection::Orthographic;
    if (ortho) {
        const f32 halfHeight =
            std::max(0.1f, std::abs(ctx.camDistance) * std::tan(kFovY * 0.5f));
        const f32 halfWidth = halfHeight * aspect;
        camera.proj = Mat4::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, 0.1f,
                                  farPlane);
    } else {
        camera.proj = Mat4::perspective(kFovY, aspect, 0.1f, farPlane);
    }
    camera.fovRad = kFovY;
    camera.nearClip = 0.1f;
    camera.farClip = farPlane;

    return renderWithCamera(cmd, world, camera, colorTarget, depthTarget, width, height,
                            projectRoot, options);
}

}  // namespace Caffeine::Render

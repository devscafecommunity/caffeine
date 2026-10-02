#include "render/GpuSceneRenderer.hpp"
#include "animation/SkinLibrary.hpp"
#include "effects/EffectSystem.hpp"
#include "render/PostProcessRenderer.hpp"
#include "render/CoarseOcclusion.hpp"
#include "render/InstanceBatch.hpp"
#include "render/ForwardRenderFeatures.hpp"
#include "debug/Profiler.hpp"
#include "render/ShaderBytecode.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "editor/EditorContext.hpp"
#include "editor/EditorCameraMath.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"
#include "scene/EnvironmentSystem.hpp"
#include "scene/CpuDirectionalShadowMap.hpp"
#include "assets/MaterialCache.hpp"
#include "assets/MeshCache.hpp"
#include "assets/MeshLOD.hpp"
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Caffeine::Render {
namespace {

constexpr u32 kVertexStride = sizeof(Assets::Vertex3D);

struct VertexUBO {
    float mvp[16];
    float model[16];
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
};

struct TerrainMaterialUBO {
    float worldSize[4];
    float flags[4];
    float modelInv[16];
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
};

Vec3 meshWorldCenter(const Mat4& worldMatrix, const Assets::Mesh3D* mesh) {
    if (!mesh) return worldMatrix.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
    const Vec3 localCenter = (mesh->bounds.min + mesh->bounds.max) * 0.5f;
    return worldMatrix.transformPoint(localCenter);
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

        for (const std::string& uri : Assets::MeshImportValidator::listGltfExternalUris(meshPath)) {
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
    }
    mesh.vertexBuffer = nullptr;
    mesh.indexBuffer = nullptr;
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
    RHI::Buffer*& slot = m_skinBuffers[draw.entity.id()];
    if (!slot || slot->size != bytes) {
        if (slot) m_device->destroyBuffer(slot);
        RHI::BufferDesc desc;
        desc.size = bytes;
        slot = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);
    }
    if (slot) m_device->uploadBuffer(slot, draw.skinnedVertices->data(), bytes);
    return slot ? slot : draw.mesh->vertexBuffer;
}

void GpuSceneRenderer::shutdown() {
    if (m_device) {
        for (auto& entry : m_skinBuffers) {
            if (entry.second) m_device->destroyBuffer(entry.second);
        }
    }
    m_skinBuffers.clear();
    for (auto& [_, cache] : m_meshLods) {
        destroyMeshGpu(cache.medium);
        destroyMeshGpu(cache.far);
    }
    m_meshLods.clear();
    m_entityLod.clear();
    m_post.shutdown();
    if (m_device) {
        for (ViewTargets& view : m_views) releaseViewTargets(view);
        for (ReflectionProbe& probe : m_probes) releaseProbe(probe);
        if (m_sceneVert) m_device->destroyShader(m_sceneVert);
        if (m_sceneFrag) m_device->destroyShader(m_sceneFrag);
        if (m_terrainFrag) m_device->destroyShader(m_terrainFrag);
        if (m_shadowVert) m_device->destroyShader(m_shadowVert);
        if (m_shadowFrag) m_device->destroyShader(m_shadowFrag);
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
    m_shadowFrag = nullptr;
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
                                        RHI::ShaderStage::Fragment, 3, 12);
    m_shadowVert = createBuiltinShader(m_device, BuiltinShader::ShadowDepthVertex,
                                       RHI::ShaderStage::Vertex, 1);
    m_shadowFrag = createBuiltinShader(m_device, BuiltinShader::ShadowDepthFragment,
                                       RHI::ShaderStage::Fragment, 0);
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
    m_scenePipeline = m_device->createGraphicsPipeline(m_sceneVert, m_sceneFrag, sceneDesc);
    RHI::GraphicsPipelineDesc wireDesc = sceneDesc;
    wireDesc.fillMode = RHI::FillMode::Line;
    m_wireframePipeline = m_device->createGraphicsPipeline(m_sceneVert, m_sceneFrag, wireDesc);
    m_terrainPipeline = m_device->createGraphicsPipeline(m_sceneVert, m_terrainFrag, sceneDesc);
    RHI::GraphicsPipelineDesc blendDesc = sceneDesc;
    blendDesc.depthWrite = false;
    blendDesc.blendMode = RHI::BlendMode::Premultiplied;
    m_blendPipeline = m_device->createGraphicsPipeline(m_sceneVert, m_sceneFrag, blendDesc);

    if (m_fullscreenVert && m_skyFrag) {
        RHI::GraphicsPipelineDesc skyDesc{};
        skyDesc.colorFormat = kHdrFormat;
        skyDesc.depthFormat = RHI::TextureFormat::D32_FLOAT;
        skyDesc.depthTest = true;
        skyDesc.depthWrite = false;
        m_skyPipeline = m_device->createGraphicsPipeline(m_fullscreenVert, m_skyFrag, skyDesc);
    }
    if (m_fullscreenVert && m_gridFrag) {
        RHI::GraphicsPipelineDesc gridDesc{};
        gridDesc.colorFormat = kHdrFormat;
        gridDesc.depthFormat = RHI::TextureFormat::D32_FLOAT;
        gridDesc.depthTest = true;
        gridDesc.depthWrite = false;
        gridDesc.blendMode = RHI::BlendMode::Alpha;
        m_gridPipeline = m_device->createGraphicsPipeline(m_fullscreenVert, m_gridFrag, gridDesc);
    }
    m_effectVert = createBuiltinShader(m_device, BuiltinShader::EffectBillboardVertex,
                                       RHI::ShaderStage::Vertex, 1);
    m_effectFrag = createBuiltinShader(m_device, BuiltinShader::EffectBillboardFragment,
                                       RHI::ShaderStage::Fragment, 0);
    if (m_effectVert && m_effectFrag) {
        RHI::GraphicsPipelineDesc effectDesc{};
        fillMeshPipelineDesc(effectDesc, layout, attrs, kHdrFormat, true, true);
        effectDesc.depthWrite = false;
        effectDesc.blendMode = RHI::BlendMode::Premultiplied;
        m_effectPipeline = m_device->createGraphicsPipeline(m_effectVert, m_effectFrag, effectDesc);
    }

    // Color-only shadow pass (no depth attachment) for cubemap / 2D shadow maps.
    RHI::GraphicsPipelineDesc shadowDesc{};
    fillMeshPipelineDesc(shadowDesc, layout, attrs, RHI::TextureFormat::R16_FLOAT, false, false);
    m_shadowPipeline = m_device->createGraphicsPipeline(m_shadowVert, m_shadowFrag, shadowDesc);

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

void GpuSceneRenderer::pushShadowDraw(RHI::CommandBuffer* cmd, const MeshDraw& draw,
                                      const Mat4& mvp, const Vec3& lightPos, int mode,
                                      u32 indexCount, u32 firstIndex) {
    if (!m_shadowPipeline || !draw.mesh || indexCount == 0) return;

    ShadowUBO ubo{};
    std::memcpy(ubo.mvp, mvp.data(), sizeof(ubo.mvp));
    std::memcpy(ubo.model, draw.worldMatrix.data(), sizeof(ubo.model));
    ubo.lightPos[0] = lightPos.x;
    ubo.lightPos[1] = lightPos.y;
    ubo.lightPos[2] = lightPos.z;
    ubo.mode = mode;

    cmd->bindPipeline(m_shadowPipeline);
    cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &ubo, sizeof(ubo));
    cmd->bindVertexBuffer(vertexBufferFor(draw));
    cmd->bindIndexBuffer(draw.mesh->indexBuffer);
    cmd->drawIndexed(indexCount, firstIndex, 0);
}

void GpuSceneRenderer::pushShadowDrawsForMesh(RHI::CommandBuffer* cmd, const MeshDraw& draw,
                                              const Mat4& mvp, const Vec3& lightPos, int mode) {
    if (!draw.mesh || draw.mesh->indices.empty()) return;

    if (draw.mesh->subMeshes.size() > 1) {
        for (const Assets::SubMesh& submesh : draw.mesh->subMeshes) {
            if (submesh.indexCount == 0) continue;
            pushShadowDraw(cmd, draw, mvp, lightPos, mode, submesh.indexCount, submesh.indexOffset);
        }
        return;
    }

    pushShadowDraw(cmd, draw, mvp, lightPos, mode, static_cast<u32>(draw.mesh->indices.size()), 0);
}

bool GpuSceneRenderer::ensureMeshUploaded(Assets::Mesh3D* mesh) {
    if (!mesh || !m_device) return false;
    if (mesh->vertexBuffer && mesh->indexBuffer) return true;
    if (mesh->vertices.empty() || mesh->indices.empty()) return false;

    RHI::BufferDesc vdesc;
    vdesc.size = mesh->vertices.size() * sizeof(Assets::Vertex3D);
    mesh->vertexBuffer = m_device->createBuffer(vdesc, RHI::BufferUsage::Vertex);
    RHI::BufferDesc idesc;
    idesc.size = mesh->indices.size() * sizeof(u32);
    mesh->indexBuffer = m_device->createBuffer(idesc, RHI::BufferUsage::Index);
    if (!mesh->vertexBuffer || !mesh->indexBuffer) return false;

    if (!m_device->uploadBuffer(mesh->vertexBuffer, mesh->vertices.data(), vdesc.size)) return false;
    return m_device->uploadBuffer(mesh->indexBuffer, mesh->indices.data(), idesc.size);
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

            for (const auto& draw : draws) {
                if (!draw.castShadows || !draw.mesh || !ensureMeshUploaded(draw.mesh)) continue;
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
const Vec3 kSkyZenith{0.16f, 0.30f, 0.62f};
const Vec3 kSkyHorizon{0.58f, 0.66f, 0.76f};
const Vec3 kSkyGround{0.16f, 0.15f, 0.14f};
const Vec3 kFallbackAmbient{0.34f, 0.38f, 0.45f};

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

    RHI::Texture* bound = env ? env : GpuTextureCache::instance().whiteTexture(m_device);
    cmd->bindPipeline(m_skyPipeline);
    if (bound) cmd->bindTexture(bound, 0, m_repeatSampler);
    cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &ubo, sizeof(ubo));
    cmd->draw(3);
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
    setVec4(base.ambient, kFallbackAmbient.x, kFallbackAmbient.y, kFallbackAmbient.z, 0.0f);
    base.dirCount = static_cast<int>(std::min(lighting.lights.directionals.size(), size_t{4}));
    for (int i = 0; i < base.dirCount; ++i) {
        const auto& d = lighting.lights.directionals[i];
        const Vec3 c = srgbToLinear(Vec3(d.color.x, d.color.y, d.color.z));
        setVec4(base.dirData + i * 4, d.direction.x, d.direction.y, d.direction.z, d.intensity);
        setVec4(base.dirColor + i * 4, c.x, c.y, c.z, 0.0f);
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
    const Vec3 irradiance =
        environmentOn ? m_environment.averageColor() * m_environmentExposure : kFallbackAmbient;
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
    base.volParams[3] = features.volumetrics == VolumetricQuality::Medium ? 12.0f
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
        if (wireframe) pipeline = m_wireframePipeline;
        else if (draw.isTerrain && m_terrainPipeline) pipeline = m_terrainPipeline;
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
            f32 thickness = 0.5f;
            if (draw.hasAabb) {
                const Vec3 extent = draw.aabbMax - draw.aabbMin;
                thickness = std::max(0.01f, std::min(extent.x, std::min(extent.y, extent.z)) * 0.5f);
            }
            setVec4(lights.materialParams, draw.normalStrength, draw.aoStrength, thickness,
                    draw.alphaCutoff);
            setVec4(lights.coat, draw.clearcoat, draw.clearcoatRoughness, draw.ior,
                    draw.transmission);
            setVec4(lights.sheen, draw.sheenColor.x, draw.sheenColor.y, draw.sheenColor.z,
                    draw.sheenRoughness);
            setVec4(lights.iridescence, draw.iridescence, draw.iridescenceThickness,
                    draw.iridescenceIor, 0.0f);
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
                    draw.ssrBounces, 0.0f);
        }

        if (!wireframe && draw.isTerrain) {
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
            if (gpu && gpu->useSplatmap) {
                mat.flags[0] = 1.0f;
            } else if (gpu && (!gpu->cachedAlbedoPath.empty() || gpu->albedo)) {
                mat.flags[1] = 1.0f;
            }
            if (gpu && (!gpu->cachedNormalPath.empty() || gpu->normalMap)) {
                lights.flags[1] = 1.0f;
            }

            if (draw.entity != lastTerrainTexEntity || texTier != lastTerrainTexTier) {
                lastTerrainTexEntity = draw.entity;
                lastTerrainTexTier = texTier;
                for (u32 slot = 0; slot < 6; ++slot) {
                    if (whiteTex) cmd->bindTexture(whiteTex, slot, m_repeatSampler);
                }
                if (gpu && gpu->useSplatmap) {
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
            bindShadowTextures(6, 8, 10);
            cmd->pushUniformData(RHI::ShaderStage::Fragment, 2, &mat, sizeof(mat));
        }

        VertexUBO vubo{};
        const Mat4 mvp = pass.viewProj * draw.worldMatrix;
        std::memcpy(vubo.mvp, mvp.data(), sizeof(vubo.mvp));
        std::memcpy(vubo.model, draw.worldMatrix.data(), sizeof(vubo.model));
        cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &vubo, sizeof(vubo));
        cmd->bindVertexBuffer(vertexBufferFor(draw));
        cmd->bindIndexBuffer(draw.mesh->indexBuffer);

        if (draw.isTerrain) {
            if (wireframe) {
                for (u32 slot = 0; slot < 6; ++slot) {
                    if (whiteTex) cmd->bindTexture(whiteTex, slot, m_repeatSampler);
                }
                bindShadowTextures(6, 8, 10);
            }
            cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &lights, sizeof(lights));
            cmd->drawIndexed(static_cast<u32>(draw.mesh->indices.size()));
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

        auto drawMeshRange = [&](u32 firstIndex, u32 indexCount, u32 materialIndex) {
            if (indexCount == 0) return;
            LightingUBO rangeLights = lights;
            RHI::Texture* albedoTex = whiteTex;
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
                }
                if (albedo.texture) albedoTex = albedo.texture;
            }
            if (albedoTex) cmd->bindTexture(albedoTex, 0, m_repeatSampler);
            cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &rangeLights, sizeof(rangeLights));

            const bool instanced = !wireframe && draw.batchCount > 1 && m_instancedPipeline &&
                                   m_instanceBuffer;
            if (instanced) {
                VertexUBO instUbo{};
                std::memcpy(instUbo.mvp, pass.viewProj.data(), sizeof(instUbo.mvp));
                cmd->bindPipeline(m_instancedPipeline);
                cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &instUbo, sizeof(instUbo));
                cmd->bindVertexBuffer(draw.mesh->vertexBuffer, 0);
                cmd->bindVertexBuffer(m_instanceBuffer, 1, draw.batchByteOffset);
                cmd->bindIndexBuffer(draw.mesh->indexBuffer);
                cmd->drawIndexedInstanced(indexCount, draw.batchCount, firstIndex, 0, 0);
            } else {
                cmd->drawIndexed(indexCount, firstIndex, 0);
            }
        };

        if (draw.mesh->subMeshes.size() > 1) {
            for (const Assets::SubMesh& submesh : draw.mesh->subMeshes) {
                drawMeshRange(submesh.indexOffset, submesh.indexCount, submesh.materialIndex);
            }
        } else {
            const u32 materialIndex =
                draw.mesh->subMeshes.empty() ? 0u : draw.mesh->subMeshes[0].materialIndex;
            drawMeshRange(0, static_cast<u32>(draw.mesh->indices.size()), materialIndex);
        }
    }
}

u32 GpuSceneRenderer::renderMeshes(RHI::CommandBuffer* cmd, const std::vector<MeshDraw>& draws,
                                   const GpuSceneCamera& camera,
                                   const Scene::SceneLighting& lighting,
                                   RHI::Texture* colorTarget, RHI::Texture* depthTarget,
                                   u32 width, u32 height, const std::string& projectRoot,
                                   const GpuSceneRenderOptions& options, ScenePassContext& pass) {
    Caffeine::Debug::setCrashBreadcrumb("GpuSceneRenderer::renderMeshes");
    if (!m_scenePipeline || !m_wireframePipeline || !colorTarget || !depthTarget) return 0;
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
    rp.depthTarget = depthTarget;
    rp.colorLayer = options.colorLayer;
    rp.clearColor[0] = 0.0f;
    rp.clearColor[1] = 0.0f;
    rp.clearColor[2] = 0.0f;
    rp.clearColor[3] = 0.0f;
    rp.clearDepth = true;
    rp.cycle = pass.cycleTargets;

    {
        CF_PROFILE_SCOPE("GpuSceneRenderer::opaque");
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
        CF_PROFILE_SCOPE("GpuSceneRenderer::translucent");
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

        Assets::Mesh3D* mesh = nullptr;
        if (filter.primitive == ECS::MeshPrimitive::Custom) {
            if (!filter.customMeshPath.empty()) {
                mesh = Assets::MeshCache::getInstance().getMesh(filter.customMeshPath, projectRoot);
            }
            if (!mesh) return;
        } else {
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
            if (!frustum.intersects(aabb)) return;
        }

        const f32 sx = Vec3(worldMatrix(0, 0), worldMatrix(1, 0), worldMatrix(2, 0)).length();
        const f32 sy = Vec3(worldMatrix(0, 1), worldMatrix(1, 1), worldMatrix(2, 1)).length();
        const f32 sz = Vec3(worldMatrix(0, 2), worldMatrix(1, 2), worldMatrix(2, 2)).length();
        const f32 scale = std::max(sx, std::max(sy, sz));
        const f32 lodDistance = viewerDistanceFor(meshWorldCenter(worldMatrix, mesh)) / std::max(scale, 0.001f);

        MeshDraw draw;
        draw.entity = entity;
        draw.mesh = selectMeshLod(mesh, entity, lodDistance);
        if (const std::vector<Assets::Vertex3D>* posed = Animation::skinnedVerticesFor(entity.id())) {
            if (posed->size() == mesh->vertices.size()) {
                draw.mesh = mesh;
                draw.skinnedVertices = posed;
            }
        }
        draw.worldMatrix = worldMatrix;
        draw.castShadows = Scene::meshCastsShadows(world, entity);
        draw.receiveShadows = Scene::meshReceivesShadows(world, entity);
        if (auto* renderer = world.get<ECS::MeshRendererComponent>(entity)) {
            draw.castShadows = renderer->castShadows;
            draw.receiveShadows = renderer->receiveShadows;
        }
        draw.shininess = filter.shininess;
        draw.customTexturePath = filter.customTexturePath;
        draw.customNormalPath = filter.customNormalPath;
        if (!filter.customMaterialPath.empty()) {
            const Assets::MaterialSurface surface =
                Assets::MaterialCache::instance().resolve(filter.customMaterialPath, projectRoot);
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
                draw.normalStrength = surface.normalStrength;
                draw.aoStrength = surface.aoStrength;
                draw.alphaMode = static_cast<u8>(surface.alphaMode);
                draw.alphaCutoff = surface.alphaCutoff;
                draw.transmission = std::clamp(surface.transmission, 0.0f, 1.0f);
                draw.ior = std::max(surface.ior, 1.0f);
                draw.clearcoat = std::clamp(surface.clearcoat, 0.0f, 1.0f);
                draw.clearcoatRoughness = std::clamp(surface.clearcoatRoughness, 0.02f, 1.0f);
                draw.sheenColor = srgbToLinear(surface.sheenColor);
                draw.sheenRoughness = surface.sheenRoughness;
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
    });

    return draws;
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
        const bool singleSubmesh = draw.mesh && draw.mesh->subMeshes.size() <= 1;
        // Probed and translucent draws need per-object data (cube map, sort order).
        items[i].allow = singleSubmesh && !draw.isTerrain && draw.probeIndex < 0 &&
                         !draw.isTranslucent();
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
        mixFloat(draw.normalStrength);
        mixFloat(draw.aoStrength);
        mixFloat(draw.clearcoat);
        mixFloat(draw.clearcoatRoughness);
        mixFloat(draw.sheenColor.x);
        mixFloat(draw.sheenColor.y);
        mixFloat(draw.sheenColor.z);
        mixFloat(draw.sheenRoughness);
        mixFloat(draw.iridescence);
        mixFloat(draw.iridescenceThickness);
        key = key * 131u + draw.alphaMode;
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
    if (m_reflectionColor && m_reflectionDepth && m_reflectionWidth == width &&
        m_reflectionHeight == height) {
        return true;
    }
    if (m_reflectionColor) m_device->destroyTexture(m_reflectionColor);
    if (m_reflectionDepth) m_device->destroyTexture(m_reflectionDepth);
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
    m_reflectionWidth = width;
    m_reflectionHeight = height;
    m_reflectionValid = false;
    return m_reflectionColor && m_reflectionDepth;
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
    CF_PROFILE_SCOPE("GpuSceneRenderer::planarReflection");
    const bool wasInReflection = m_inReflectionPass;
    m_inReflectionPass = true;
    ScenePassContext innerPass;
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
    }
    probe = ReflectionProbe{};
}

bool GpuSceneRenderer::captureProbe(RHI::CommandBuffer* cmd, ECS::World& world,
                                    ReflectionProbe& probe, const GpuSceneCamera& camera,
                                    const std::string& projectRoot,
                                    const GpuSceneRenderOptions& options) {
    u32 resolution = 32;
    while (resolution < std::clamp(options.features.probeResolution, 32u, 512u)) resolution <<= 1;
    if (!probe.cube || !probe.depth || probe.resolution != resolution) {
        if (probe.cube) m_device->destroyTexture(probe.cube);
        if (probe.depth) m_device->destroyTexture(probe.depth);
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
        if (!probe.cube || !probe.depth) return false;
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

    CF_PROFILE_SCOPE("GpuSceneRenderer::probe");
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
        return view.color[0] && view.color[1] && view.depth[0] && view.depth[1] && view.opaqueCopy;
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

    std::vector<MeshDraw> draws;
    {
        CF_PROFILE_SCOPE("GpuSceneRenderer::gather");
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

    Scene::SceneLighting lighting;
    lighting.clear();
    Scene::collectSceneLights(world, lighting.lights);

    m_activeCascadeCount = opts.directionalCascadeCount;
    if (!opts.wireframeMeshes && opts.enableShadows && !draws.empty()) {
        CF_PROFILE_SCOPE("GpuSceneRenderer::shadows");
        renderDirectionalShadows(cmd, world, lighting, draws, camera);
        renderPointShadows(cmd, world, lighting, draws);
        renderSpotShadows(cmd, world, lighting, draws);
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
                        projectRoot, opts, pass);
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
    if (options.resolveFeaturesFromScene) opts.features = resolveForwardRenderFeatures(world);
    resolveEnvironment(world, projectRoot, opts);
    const ECS::PostProcessComponent post =
        opts.resolvePostProcessFromScene
            ? resolvePostProcess(world, opts.postProcessCamera, opts.postProcess)
            : opts.postProcess;

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
    const bool taa = PostProcessStack::usesTaa(post) && !opts.wireframeMeshes;
    Vec2 jitter(0.0f, 0.0f);
    if (taa) {
        jitter = PostProcessStack::taaJitter(view->frameIndex);
        camera.proj(0, 2) += jitter.x * 2.0f / static_cast<f32>(sceneW);
        camera.proj(1, 2) += jitter.y * 2.0f / static_cast<f32>(sceneH);
    }

    ScenePassContext pass;
    pass.view = view;
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

    {
        CF_PROFILE_SCOPE("GpuSceneRenderer::post");
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
        in.prevViewProj = view->hasPrevious ? view->prevViewProj : unjitteredViewProj;
        in.cameraPos = camera.position;
        in.deltaTime = std::clamp(opts.deltaTime, 1e-4f, 0.25f);
        in.time = static_cast<f32>(m_frameCounter) * (1.0f / 60.0f);
        in.frameIndex = view->frameIndex;
        in.historyValid = view->hasPrevious;
        in.jitterPixels = jitter;
        in.viewId = viewId;
        m_post.execute(cmd, post, in);
    }

    // Keep redrawing until temporal effects converge after anything changes.
    u64 changeHash = pass.sceneHash;
    for (u32 i = 0; i < 16; ++i) {
        u32 bits = 0;
        std::memcpy(&bits, unjitteredViewProj.data() + i, sizeof(bits));
        changeHash = (changeHash ^ bits) * 1099511628211ull;
    }
    if (changeHash != view->prevSceneHash) {
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
    m_needsAnotherFrame = continuous || m_probesPending || view->settleFrames > 0;
    return drawn;
}

u32 GpuSceneRenderer::render(RHI::CommandBuffer* cmd, ECS::World& world,
                              const Editor::EditorContext& ctx, RHI::Texture* colorTarget,
                              RHI::Texture* depthTarget, u32 width, u32 height,
                              const std::string& projectRoot, const GpuSceneRenderOptions& options) {
    if (!m_ready || !cmd || !colorTarget || width < 1 || height < 1) return 0;
    if (ctx.viewMode != Editor::EditorContext::ViewMode::Mode3D) return 0;

    const Vec3 cameraPos =
        Editor::editorCameraPosition(ctx.camYaw, ctx.camPitch, ctx.camDistance, ctx.camFocus);
    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(std::max(height, 1u));
    const f32 farPlane = ctx.cameraFarPlane();

    GpuSceneCamera camera;
    camera.position = cameraPos;
    camera.focus = ctx.camFocus;
    camera.view = Mat4::lookAt(cameraPos, ctx.camFocus, Vec3(0, 1, 0));
    camera.proj = Mat4::perspective(60.0f * kPi / 180.0f, aspect, 0.1f, farPlane);
    camera.fovRad = 60.0f * kPi / 180.0f;
    camera.nearClip = 0.1f;
    camera.farClip = farPlane;

    return renderWithCamera(cmd, world, camera, colorTarget, depthTarget, width, height,
                            projectRoot, options);
}

}  // namespace Caffeine::Render

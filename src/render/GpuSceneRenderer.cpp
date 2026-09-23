#include "render/GpuSceneRenderer.hpp"
#include "debug/Profiler.hpp"
#include "render/ShaderBytecode.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "editor/EditorContext.hpp"
#include "editor/EditorCameraMath.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"
#include "scene/CpuDirectionalShadowMap.hpp"
#include "assets/MeshCache.hpp"
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
    float shininess;
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
};

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
    const Mat4 proj = Mat4::ortho(-extent, extent, -extent, extent, 0.1f, extent * 4.0f);
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
        const Mat4 proj = Mat4::ortho(-extent, extent, -extent, extent, 0.1f, extent * 4.0f);
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
    const Mat4 proj =
        Mat4::perspective(angleDegrees * 3.14159265f / 180.0f, 1.0f, 0.1f, radius);
    return proj * view;
}

RHI::Shader* createShaderFromBuiltin(RHI::RenderDevice* device, BuiltinShader shader,
                                     RHI::ShaderStage stage, u32 numUniformBuffers,
                                     u32 numSamplers = 0) {
    const auto format = detectShaderFormat(device);
    const auto bytecode = getBuiltinShaderBytecode(shader, format);
    if (!bytecode.data || bytecode.size == 0) return nullptr;

    RHI::ShaderDesc desc;
    desc.code = bytecode.data;
    desc.codeSize = bytecode.size;
    desc.stage = stage;
    desc.format = format;
    desc.entryPoint = bytecode.entryPoint;
    desc.numUniformBuffers = numUniformBuffers;
    desc.numSamplers = numSamplers;
    return device->createShader(desc);
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
        result.texture = cache.acquire(device, customTexturePath, projectRoot, qualityTier);
        if (result.texture) return result;
    }

    if (mesh && materialIndex < mesh->materials.size()) {
        const Assets::MeshSurfaceMaterial& mat = mesh->materials[materialIndex];
        result.factor = Vec4(mat.albedoColor.r, mat.albedoColor.g, mat.albedoColor.b,
                           mat.albedoColor.a);
        result.metallic = mat.metallic;
        result.roughness = mat.roughness;

        if (!mat.albedoPath.empty()) {
            result.texture = cache.acquire(device, mat.albedoPath, projectRoot, qualityTier);
            if (result.texture) return result;
        }
        if (!mat.albedoPixels.empty() && mat.albedoWidth > 0 && mat.albedoHeight > 0) {
            const std::string key = meshPath + "#mat" + std::to_string(materialIndex);
            result.texture = cache.acquireFromPixels(device, key, mat.albedoPixels.data(),
                                                     mat.albedoWidth, mat.albedoHeight,
                                                     mat.albedoChannels, qualityTier);
            if (result.texture) return result;
        }
        return result;
    }

    if (!meshPath.empty()) {
        const std::string pngPath =
            std::filesystem::path(meshPath).replace_extension(".png").string();
        result.texture = cache.acquire(device, pngPath, projectRoot, qualityTier);
        if (result.texture) return result;

        for (const std::string& uri : Assets::MeshImportValidator::listGltfExternalUris(meshPath)) {
            const std::string texPath =
                (std::filesystem::path(meshPath).parent_path() / uri).string();
            result.texture = cache.acquire(device, texPath, projectRoot, qualityTier);
            if (result.texture) return result;
        }
    }

    if (mesh && mesh->textureWidth > 0 && !mesh->baseColorTexture.empty()) {
        const std::string key = meshPath + "#embedded";
        result.texture = cache.acquireFromPixels(device, key, mesh->baseColorTexture.data(),
                                                 mesh->textureWidth, mesh->textureHeight,
                                                 mesh->textureChannels, qualityTier);
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
    m_repeatSampler = m_device->createSampler(repeatDesc);
    m_ready = m_sampler && m_repeatSampler;
    return m_ready;
}

void GpuSceneRenderer::shutdown() {
    if (m_device) {
        if (m_sceneVert) m_device->destroyShader(m_sceneVert);
        if (m_sceneFrag) m_device->destroyShader(m_sceneFrag);
        if (m_terrainFrag) m_device->destroyShader(m_terrainFrag);
        if (m_shadowVert) m_device->destroyShader(m_shadowVert);
        if (m_shadowFrag) m_device->destroyShader(m_shadowFrag);
        if (m_scenePipeline) m_device->destroyPipeline(m_scenePipeline);
        if (m_wireframePipeline) m_device->destroyPipeline(m_wireframePipeline);
        if (m_terrainPipeline) m_device->destroyPipeline(m_terrainPipeline);
        if (m_shadowPipeline) m_device->destroyPipeline(m_shadowPipeline);
        if (m_sampler) m_device->destroySampler(m_sampler);
        if (m_repeatSampler) m_device->destroySampler(m_repeatSampler);
    }
    m_sceneVert = nullptr;
    m_sceneFrag = nullptr;
    m_terrainFrag = nullptr;
    m_shadowVert = nullptr;
    m_shadowFrag = nullptr;
    m_scenePipeline = nullptr;
    m_wireframePipeline = nullptr;
    m_terrainPipeline = nullptr;
    m_shadowPipeline = nullptr;
    m_sampler = nullptr;
    m_repeatSampler = nullptr;
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

    m_sceneVert = createShaderFromBuiltin(m_device, BuiltinShader::SceneLitVertex,
                                          RHI::ShaderStage::Vertex, 1);
    m_sceneFrag = createShaderFromBuiltin(m_device, BuiltinShader::SceneLitFragment,
                                          RHI::ShaderStage::Fragment, 2, 8);
    m_terrainFrag = createShaderFromBuiltin(m_device, BuiltinShader::TerrainLitFragment,
                                            RHI::ShaderStage::Fragment, 3, 12);
    m_shadowVert = createShaderFromBuiltin(m_device, BuiltinShader::ShadowDepthVertex,
                                           RHI::ShaderStage::Vertex, 1);
    m_shadowFrag = createShaderFromBuiltin(m_device, BuiltinShader::ShadowDepthFragment,
                                           RHI::ShaderStage::Fragment, 0);

    if (!m_sceneVert || !m_sceneFrag || !m_terrainFrag || !m_shadowVert || !m_shadowFrag) {
        return false;
    }

    RHI::VertexBufferLayoutDesc layout{};
    RHI::VertexAttributeDesc attrs[4]{};
    RHI::GraphicsPipelineDesc sceneDesc{};
    fillMeshPipelineDesc(sceneDesc, layout, attrs, RHI::TextureFormat::R8G8B8A8_UNORM, true, true);
    m_scenePipeline = m_device->createGraphicsPipeline(m_sceneVert, m_sceneFrag, sceneDesc);
    RHI::GraphicsPipelineDesc wireDesc = sceneDesc;
    wireDesc.fillMode = RHI::FillMode::Line;
    m_wireframePipeline = m_device->createGraphicsPipeline(m_sceneVert, m_sceneFrag, wireDesc);
    m_terrainPipeline = m_device->createGraphicsPipeline(m_sceneVert, m_terrainFrag, sceneDesc);

    // Color-only shadow pass (no depth attachment) for cubemap / 2D shadow maps.
    RHI::GraphicsPipelineDesc shadowDesc{};
    fillMeshPipelineDesc(shadowDesc, layout, attrs, RHI::TextureFormat::R16_FLOAT, false, false);
    m_shadowPipeline = m_device->createGraphicsPipeline(m_shadowVert, m_shadowFrag, shadowDesc);

    return m_scenePipeline && m_wireframePipeline && m_terrainPipeline && m_shadowPipeline;
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
    cmd->bindVertexBuffer(draw.mesh->vertexBuffer);
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

        const u32 cascadeCount =
            shadowSlot == 0 ? GpuDirectionalShadowMap::kMaxCascades : 1u;
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

        const Vec3 faceDirs[6] = {
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
        };
        const Vec3 faceUps[6] = {
            {0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}
        };

        for (u32 face = 0; face < 6; ++face) {
            const Mat4 view = Mat4::lookAt(point.position, point.position + faceDirs[face], faceUps[face]);
            const Mat4 proj = Mat4::perspective(90.0f * 3.14159265f / 180.0f, 1.0f, 0.1f, point.radius);

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

u32 GpuSceneRenderer::renderMeshes(RHI::CommandBuffer* cmd, const std::vector<MeshDraw>& draws,
                                   const Mat4& vp, const Vec3& cameraPos, const Mat4& cameraView,
                                   const Scene::SceneLighting& lighting,
                                   RHI::Texture* colorTarget, RHI::Texture* depthTarget,
                                   u32 width, u32 height, const std::string& projectRoot,
                                   const GpuSceneRenderOptions& options) {
    Caffeine::Debug::setCrashBreadcrumb("GpuSceneRenderer::renderMeshes");
    const bool wireframeMeshes = options.wireframeMeshes;
    if ((!wireframeMeshes && !m_scenePipeline) ||
        (wireframeMeshes && !m_wireframePipeline) || draws.empty()) {
        return 0;
    }

    u32 drawnMeshes = 0;

    RHI::RenderPassDesc pass;
    pass.colorTarget = colorTarget;
    pass.depthTarget = depthTarget;
    pass.clearColor[0] = 0.0f;
    pass.clearColor[1] = 0.0f;
    pass.clearColor[2] = 0.0f;
    pass.clearColor[3] = 0.0f;
    pass.clearDepth = true;
    pass.cycle = true;
    SceneShadowUBO shadowUbo{};
    std::memcpy(shadowUbo.cameraView, cameraView.data(), sizeof(shadowUbo.cameraView));
    for (u32 slot = 0; slot < GpuDirectionalShadowMap::kMaxDirectionalShadowLights; ++slot) {
        if (m_directionalShadows.valid(slot)) {
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
    }
    for (u32 slot = 0; slot < GpuPointShadowMap::kMaxPointShadowLights; ++slot) {
        if (m_pointShadows.valid(slot)) {
            const Vec3 pos = m_pointShadows.lightPosition(slot);
            shadowUbo.pointShadowPos[slot * 4 + 0] = pos.x;
            shadowUbo.pointShadowPos[slot * 4 + 1] = pos.y;
            shadowUbo.pointShadowPos[slot * 4 + 2] = pos.z;
            shadowUbo.pointShadowRadius[slot] = m_pointShadows.radius(slot);
            shadowUbo.pointShadowValid[slot] = 1.0f;
        }
    }
    for (u32 slot = 0; slot < GpuSpotShadowMap::kMaxSpotShadowLights; ++slot) {
        if (m_spotShadows.valid(slot)) {
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
    }

    RHI::Texture* whiteTex = GpuTextureCache::instance().whiteTexture(m_device);

    cmd->beginRenderPass(pass);
    cmd->bindPipeline(wireframeMeshes ? m_wireframePipeline : m_scenePipeline);
    cmd->setViewport(0, 0, static_cast<f32>(width), static_cast<f32>(height));
    if (!wireframeMeshes) {
        cmd->pushUniformData(RHI::ShaderStage::Fragment, 1, &shadowUbo, sizeof(shadowUbo));
    }

    auto bindShadowTextures = [&](u32 dirBase, u32 pointBase, u32 spotBase) {
        for (u32 slot = 0; slot < GpuDirectionalShadowMap::kMaxDirectionalShadowLights; ++slot) {
            RHI::Texture* shadowTex = m_directionalShadows.texture(slot);
            if (shadowTex) {
                cmd->bindTexture(shadowTex, dirBase + slot, m_sampler);
            } else if (whiteTex) {
                cmd->bindTexture(whiteTex, dirBase + slot, m_sampler);
            }
        }
        for (u32 slot = 0; slot < GpuPointShadowMap::kMaxPointShadowLights; ++slot) {
            RHI::Texture* cube = m_pointShadows.cubemap(slot);
            if (cube) {
                cmd->bindTexture(cube, pointBase + slot, m_sampler);
            } else if (whiteTex) {
                cmd->bindTexture(whiteTex, pointBase + slot, m_sampler);
            }
        }
        for (u32 slot = 0; slot < GpuSpotShadowMap::kMaxSpotShadowLights; ++slot) {
            RHI::Texture* spotTex = m_spotShadows.texture(slot);
            if (spotTex) {
                cmd->bindTexture(spotTex, spotBase + slot, m_sampler);
            } else if (whiteTex) {
                cmd->bindTexture(whiteTex, spotBase + slot, m_sampler);
            }
        }
    };
    // Shaders always declare shadow samplers — bind fallbacks even when shadow passes are skipped.
    bindShadowTextures(2, 4, 6);

    ECS::Entity lastTerrainTexEntity = ECS::Entity::INVALID;
    u32 lastTerrainTexTier = ~0u;

    for (const auto& draw : draws) {
        if (!draw.mesh || draw.mesh->indices.empty()) continue;
        if (!draw.mesh->vertexBuffer || !draw.mesh->indexBuffer) continue;

        if (wireframeMeshes) {
            cmd->bindPipeline(m_wireframePipeline);
        } else if (draw.isTerrain && m_terrainPipeline) {
            cmd->bindPipeline(m_terrainPipeline);
        } else {
            cmd->bindPipeline(m_scenePipeline);
        }

        LightingUBO lights{};
        lights.cameraPos[0] = cameraPos.x;
        lights.cameraPos[1] = cameraPos.y;
        lights.cameraPos[2] = cameraPos.z;
        if (wireframeMeshes) {
            lights.ambient[0] = 0.72f;
            lights.ambient[1] = 0.78f;
            lights.ambient[2] = 0.88f;
            lights.albedo[0] = 0.55f;
            lights.albedo[1] = 0.62f;
            lights.albedo[2] = 0.74f;
            lights.metallic = 0.0f;
            lights.roughness = 1.0f;
            lights.shininess = 1.0f;
            lights.flags[0] = 0.0f;
            lights.flags[1] = 0.0f;
            lights.dirCount = 0;
            lights.pointCount = 0;
            lights.spotCount = 0;
        } else {
            lights.ambient[0] = draw.isTerrain ? 0.28f : 0.32f;
            lights.ambient[1] = draw.isTerrain ? 0.28f : 0.32f;
            lights.ambient[2] = draw.isTerrain ? 0.30f : 0.34f;
            lights.albedo[0] = draw.albedo.x;
            lights.albedo[1] = draw.albedo.y;
            lights.albedo[2] = draw.albedo.z;
            lights.metallic = draw.metallic;
            lights.roughness = draw.roughness;
            lights.shininess = draw.shininess;
            lights.flags[0] = draw.receiveShadows ? 1.0f : 0.0f;
            lights.flags[1] = draw.customNormalPath.empty() ? 0.0f : 1.0f;

            lights.dirCount = static_cast<int>(std::min(lighting.lights.directionals.size(), size_t{4}));
            for (int i = 0; i < lights.dirCount; ++i) {
                const auto& d = lighting.lights.directionals[i];
                lights.dirData[i * 4 + 0] = d.direction.x;
                lights.dirData[i * 4 + 1] = d.direction.y;
                lights.dirData[i * 4 + 2] = d.direction.z;
                lights.dirData[i * 4 + 3] = d.intensity;
                lights.dirColor[i * 4 + 0] = d.color.x;
                lights.dirColor[i * 4 + 1] = d.color.y;
                lights.dirColor[i * 4 + 2] = d.color.z;
            }

            lights.pointCount = static_cast<int>(std::min(lighting.lights.points.size(), size_t{4}));
            for (int i = 0; i < lights.pointCount; ++i) {
                const auto& p = lighting.lights.points[i];
                lights.pointData[i * 4 + 0] = p.position.x;
                lights.pointData[i * 4 + 1] = p.position.y;
                lights.pointData[i * 4 + 2] = p.position.z;
                lights.pointData[i * 4 + 3] = p.radius;
                lights.pointColor[i * 4 + 0] = p.color.x;
                lights.pointColor[i * 4 + 1] = p.color.y;
                lights.pointColor[i * 4 + 2] = p.color.z;
                lights.pointColor[i * 4 + 3] = p.intensity;
            }

            lights.spotCount = static_cast<int>(std::min(lighting.lights.spots.size(), size_t{4}));
            for (int i = 0; i < lights.spotCount; ++i) {
                const auto& s = lighting.lights.spots[i];
                lights.spotData[i * 4 + 0] = s.position.x;
                lights.spotData[i * 4 + 1] = s.position.y;
                lights.spotData[i * 4 + 2] = s.position.z;
                lights.spotData[i * 4 + 3] = s.radius;
                lights.spotDir[i * 4 + 0] = s.direction.x;
                lights.spotDir[i * 4 + 1] = s.direction.y;
                lights.spotDir[i * 4 + 2] = s.direction.z;
                lights.spotDir[i * 4 + 3] = s.intensity;
                lights.spotColor[i * 4 + 0] = s.color.x;
                lights.spotColor[i * 4 + 1] = s.color.y;
                lights.spotColor[i * 4 + 2] = s.color.z;
                const f32 halfAngle =
                    std::clamp(s.angle * 3.14159265f / 180.0f * 0.5f, 0.01f, 1.5533f);
                lights.spotAngle[i * 4 + 0] = std::cos(halfAngle);
            }
        }

        if (!wireframeMeshes && draw.isTerrain) {
            // Terrain is matte: no Phong specular (shininess is ignored in terrain_lit.frag).
            lights.roughness = 1.0f;
            lights.shininess = 1.0f;
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
            const u32 texTier =
                textureQualityTier(draw.viewerDistance, options.textureQuality);
            if (gpu && gpu->useSplatmap) {
                mat.flags[0] = 1.0f;
            } else if (gpu && (!gpu->cachedAlbedoPath.empty() || gpu->albedo)) {
                mat.flags[1] = 1.0f;
            }
            if (gpu && (!gpu->cachedNormalPath.empty() || gpu->normalMap)) {
                lights.flags[1] = 1.0f;
            }

            const bool rebindTerrainTextures = draw.entity != lastTerrainTexEntity
                || texTier != lastTerrainTexTier;
            if (rebindTerrainTextures) {
                lastTerrainTexEntity = draw.entity;
                lastTerrainTexTier = texTier;
                auto& texCache = GpuTextureCache::instance();
                for (u32 slot = 0; slot < 6; ++slot) {
                    if (whiteTex) cmd->bindTexture(whiteTex, slot, m_repeatSampler);
                }
                if (gpu && gpu->useSplatmap) {
                    if (gpu->splatMap) {
                        cmd->bindTexture(gpu->splatMap, 0, m_sampler);
                    }
                    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
                        RHI::Texture* layerTex = whiteTex;
                        if (!gpu->cachedLayerPaths[i].empty()) {
                            layerTex = texCache.acquire(m_device, gpu->cachedLayerPaths[i],
                                                          projectRoot, texTier);
                        } else if (gpu->layers[i]) {
                            layerTex = gpu->layers[i];
                        }
                        if (layerTex) {
                            cmd->bindTexture(layerTex, 1 + i, m_repeatSampler);
                        }
                    }
                } else if (gpu) {
                    RHI::Texture* albedoTex = whiteTex;
                    if (!gpu->cachedAlbedoPath.empty()) {
                        albedoTex =
                            texCache.acquire(m_device, gpu->cachedAlbedoPath, projectRoot, texTier);
                    } else if (gpu->albedo) {
                        albedoTex = gpu->albedo;
                    }
                    if (albedoTex) {
                        cmd->bindTexture(albedoTex, 4, m_repeatSampler);
                    }
                }
                if (gpu) {
                    RHI::Texture* normalTex = nullptr;
                    if (!gpu->cachedNormalPath.empty()) {
                        normalTex =
                            texCache.acquire(m_device, gpu->cachedNormalPath, projectRoot, texTier);
                    } else if (gpu->normalMap) {
                        normalTex = gpu->normalMap;
                    }
                    if (normalTex) {
                        cmd->bindTexture(normalTex, 5, m_repeatSampler);
                    }
                }
            }
            bindShadowTextures(6, 8, 10);
            cmd->pushUniformData(RHI::ShaderStage::Fragment, 2, &mat, sizeof(mat));
        }

        VertexUBO vubo{};
        const Mat4 mvp = vp * draw.worldMatrix;
        std::memcpy(vubo.mvp, mvp.data(), sizeof(vubo.mvp));
        std::memcpy(vubo.model, draw.worldMatrix.data(), sizeof(vubo.model));
        cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &vubo, sizeof(vubo));
        cmd->bindVertexBuffer(draw.mesh->vertexBuffer);
        cmd->bindIndexBuffer(draw.mesh->indexBuffer);

        auto drawMeshRange = [&](u32 firstIndex, u32 indexCount, u32 materialIndex) {
            if (indexCount == 0) return;

            if (wireframeMeshes) {
                bindShadowTextures(2, 4, 6);
                if (whiteTex) {
                    cmd->bindTexture(whiteTex, 0, m_sampler);
                    cmd->bindTexture(whiteTex, 1, m_sampler);
                }
            } else if (!draw.isTerrain) {
                bindShadowTextures(2, 4, 6);
                const u32 texTier =
                    textureQualityTier(draw.viewerDistance, options.textureQuality);
                const ResolvedMeshAlbedo albedo =
                    resolveMeshAlbedo(m_device, draw.mesh, draw.meshPath, projectRoot,
                                      draw.customTexturePath, materialIndex, texTier);
                lights.albedo[0] = draw.albedo.x * albedo.factor.x;
                lights.albedo[1] = draw.albedo.y * albedo.factor.y;
                lights.albedo[2] = draw.albedo.z * albedo.factor.z;
                if (!draw.mesh->materials.empty() &&
                    materialIndex < draw.mesh->materials.size()) {
                    lights.metallic = albedo.metallic;
                    lights.roughness = albedo.roughness;
                }

                RHI::Texture* albedoTex = albedo.texture ? albedo.texture : whiteTex;
                if (albedoTex) cmd->bindTexture(albedoTex, 0, m_repeatSampler);
                RHI::Texture* normalTex = whiteTex;
                if (!draw.customNormalPath.empty()) {
                    normalTex = GpuTextureCache::instance().acquire(
                        m_device, draw.customNormalPath, projectRoot, texTier);
                }
                if (normalTex) {
                    cmd->bindTexture(normalTex, 1, m_repeatSampler);
                }
            }

            cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &lights, sizeof(lights));
            cmd->drawIndexed(indexCount, firstIndex, 0);
        };

        if (draw.isTerrain) {
            if (wireframeMeshes) {
                for (u32 slot = 0; slot < 6; ++slot) {
                    if (whiteTex) cmd->bindTexture(whiteTex, slot, m_repeatSampler);
                }
                bindShadowTextures(6, 8, 10);
            }
            cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &lights, sizeof(lights));
            cmd->drawIndexed(static_cast<u32>(draw.mesh->indices.size()));
        } else if (draw.mesh->subMeshes.size() > 1) {
            for (const Assets::SubMesh& submesh : draw.mesh->subMeshes) {
                drawMeshRange(submesh.indexOffset, submesh.indexCount, submesh.materialIndex);
            }
        } else {
            const u32 materialIndex =
                draw.mesh->subMeshes.empty() ? 0u : draw.mesh->subMeshes[0].materialIndex;
            drawMeshRange(0, static_cast<u32>(draw.mesh->indices.size()), materialIndex);
        }
        ++drawnMeshes;
    }

    cmd->endRenderPass();
    return drawnMeshes;
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
        if (bsz.lengthSquared() > 1e-6f) {
            Spatial::AABB3D aabb;
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
            if (!frustum.intersects(aabb)) return;
        }

        MeshDraw draw;
        draw.entity = entity;
        draw.mesh = mesh;
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
        if (filter.primitive == ECS::MeshPrimitive::Custom) {
            const std::string& resolved = Assets::MeshCache::getInstance().getResolvedPath();
            draw.meshPath = resolved.empty() ? filter.customMeshPath : resolved;
        }
        draw.viewerDistance = viewerDistanceFor(meshWorldCenter(worldMatrix, mesh));
        draws.push_back(draw);
    });

    return draws;
}

u32 GpuSceneRenderer::renderWithCamera(RHI::CommandBuffer* cmd, ECS::World& world,
                                        const GpuSceneCamera& camera, RHI::Texture* colorTarget,
                                        RHI::Texture* depthTarget, u32 width, u32 height,
                                        const std::string& projectRoot,
                                        const GpuSceneRenderOptions& options) {
    CF_PROFILE_SCOPE("GpuSceneRenderer::renderWithCamera");
    Caffeine::Debug::setCrashBreadcrumb("GpuSceneRenderer::renderWithCamera");
    if (!m_ready || !cmd || !colorTarget || !depthTarget || width < 1 || height < 1) return 0;

    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(std::max(height, 1u));
    const Mat4 vp = camera.proj * camera.view;
    const Spatial::Frustum frustum = Spatial::Frustum::fromCamera(
        camera.position, camera.focus, Vec3(0.0f, 1.0f, 0.0f), camera.fovRad, aspect,
        camera.nearClip, camera.farClip);

    std::vector<MeshDraw> draws;
    {
        CF_PROFILE_SCOPE("GpuSceneRenderer::gather");
        draws = gatherMeshDraws(world, camera.position, frustum, projectRoot, options);
    }

    // Upload GPU buffers before any render pass. Creating/uploading buffers
    // while a pass is recording is a common NVIDIA driver SIGSEGV.
    draws.erase(std::remove_if(draws.begin(), draws.end(),
                               [this](const MeshDraw& draw) {
                                   return !draw.mesh || !ensureMeshUploaded(draw.mesh) ||
                                          !draw.mesh->vertexBuffer || !draw.mesh->indexBuffer;
                               }),
                draws.end());
    if (draws.empty()) return 0;

    Scene::SceneLighting lighting;
    lighting.clear();
    Scene::collectSceneLights(world, lighting.lights);

    if (!options.wireframeMeshes && options.enableShadows) {
        CF_PROFILE_SCOPE("GpuSceneRenderer::shadows");
        renderDirectionalShadows(cmd, world, lighting, draws, camera);
        renderPointShadows(cmd, world, lighting, draws);
        renderSpotShadows(cmd, world, lighting, draws);
    }

    {
        CF_PROFILE_SCOPE("GpuSceneRenderer::draw");
        return renderMeshes(cmd, draws, vp, camera.position, camera.view, lighting, colorTarget,
                            depthTarget, width, height, projectRoot, options);
    }
}

u32 GpuSceneRenderer::render(RHI::CommandBuffer* cmd, ECS::World& world,
                              const Editor::EditorContext& ctx, RHI::Texture* colorTarget,
                              RHI::Texture* depthTarget, u32 width, u32 height,
                              const std::string& projectRoot, const GpuSceneRenderOptions& options) {
    if (!m_ready || !cmd || !colorTarget || !depthTarget || width < 1 || height < 1) return 0;
    if (ctx.viewMode != Editor::EditorContext::ViewMode::Mode3D) return 0;

    const Vec3 cameraPos =
        Editor::editorCameraPosition(ctx.camYaw, ctx.camPitch, ctx.camDistance, ctx.camFocus);
    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(std::max(height, 1u));
    const f32 farPlane = ctx.cameraFarPlane();

    GpuSceneCamera camera;
    camera.position = cameraPos;
    camera.focus = ctx.camFocus;
    camera.view = Mat4::lookAt(cameraPos, ctx.camFocus, Vec3(0, 1, 0));
    camera.proj = Mat4::perspective(60.0f * 3.14159265f / 180.0f, aspect, 0.1f, farPlane);
    camera.fovRad = 60.0f * 3.14159265f / 180.0f;
    camera.nearClip = 0.1f;
    camera.farClip = farPlane;

    return renderWithCamera(cmd, world, camera, colorTarget, depthTarget, width, height,
                            projectRoot, options);
}

}  // namespace Caffeine::Render

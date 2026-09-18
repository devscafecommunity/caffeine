#include "render/GpuSceneRenderer.hpp"
#include "render/ShaderBytecode.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "editor/EditorContext.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/ComponentQuery.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/SceneComponents.hpp"
#include "scene/CpuDirectionalShadowMap.hpp"
#include "assets/MeshCache.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainCache.hpp"
#include "terrain/TerrainGpuTextures.hpp"
#include "terrain/TerrainLodSystem.hpp"
#include "spatial/Octree.hpp"
#include "math/Quat.hpp"
#include "ecs/Components.hpp"

#include <algorithm>
#include <cmath>
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
    int dirCount;
    int pointCount;
    float dirData[16];
    float dirColor[16];
    float pointData[16];
    float pointColor[16];
    float pointShadow[16];
    float dirShadow[16];
    float dirShadowVP[32];
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
    m_terrainPipeline = nullptr;
    m_shadowPipeline = nullptr;
    m_sampler = nullptr;
    m_repeatSampler = nullptr;
    m_pointShadows.shutdown();
    m_directionalShadows.shutdown();
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
                                          RHI::ShaderStage::Fragment, 1, 4);
    m_terrainFrag = createShaderFromBuiltin(m_device, BuiltinShader::TerrainLitFragment,
                                            RHI::ShaderStage::Fragment, 2, 10);
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
    m_terrainPipeline = m_device->createGraphicsPipeline(m_sceneVert, m_terrainFrag, sceneDesc);

    // Color-only shadow pass (no depth attachment) for cubemap / 2D shadow maps.
    RHI::GraphicsPipelineDesc shadowDesc{};
    fillMeshPipelineDesc(shadowDesc, layout, attrs, RHI::TextureFormat::R16_FLOAT, false, false);
    m_shadowPipeline = m_device->createGraphicsPipeline(m_shadowVert, m_shadowFrag, shadowDesc);

    return m_scenePipeline && m_terrainPipeline && m_shadowPipeline;
}

void GpuSceneRenderer::pushShadowDraw(RHI::CommandBuffer* cmd, const MeshDraw& draw,
                                      const Mat4& mvp, const Vec3& lightPos, int mode) {
    if (!m_shadowPipeline || !draw.mesh) return;

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
    cmd->drawIndexed(static_cast<u32>(draw.mesh->indices.size()));
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
                                                const Vec3& focus) {
    (void)world;
    if (!m_shadowPipeline || draws.empty()) return;

    u32 shadowSlot = 0;
    for (const auto& dir : lighting.lights.directionals) {
        if (!dir.castShadows || shadowSlot >= GpuDirectionalShadowMap::kMaxDirectionalShadowLights) {
            continue;
        }

        RHI::Texture* map = m_directionalShadows.texture(shadowSlot);
        if (!map) continue;

        const Mat4 lightVP = buildDirectionalLightVP(dir.direction, focus, dir.shadowDistance);
        m_directionalShadows.setSlot(shadowSlot, lightVP, true);

        RHI::RenderPassDesc pass;
        pass.colorTarget = map;
        pass.clearColor[0] = 1.0f;
        pass.clearColor[1] = 1.0f;
        pass.clearColor[2] = 1.0f;
        pass.clearColor[3] = 1.0f;
        cmd->beginRenderPass(pass);
        cmd->setViewport(0, 0, static_cast<f32>(GpuDirectionalShadowMap::kResolution),
                         static_cast<f32>(GpuDirectionalShadowMap::kResolution));

        for (const auto& draw : draws) {
            if (!draw.castShadows || !draw.mesh || !ensureMeshUploaded(draw.mesh)) continue;
            pushShadowDraw(cmd, draw, lightVP * draw.worldMatrix, focus, 1);
        }
        cmd->endRenderPass();
        ++shadowSlot;
    }

    for (; shadowSlot < GpuDirectionalShadowMap::kMaxDirectionalShadowLights; ++shadowSlot) {
        m_directionalShadows.setSlot(shadowSlot, Mat4::identity(), false);
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
                pushShadowDraw(cmd, draw, proj * view * draw.worldMatrix, point.position, 0);
            }
            cmd->endRenderPass();
        }
        ++shadowSlot;
    }
}

u32 GpuSceneRenderer::renderMeshes(RHI::CommandBuffer* cmd, const std::vector<MeshDraw>& draws,
                                   const Mat4& vp, const Vec3& cameraPos,
                                   const Scene::SceneLighting& lighting,
                                   RHI::Texture* colorTarget, RHI::Texture* depthTarget,
                                   u32 width, u32 height) {
    if (!m_scenePipeline || draws.empty()) return 0;

    u32 drawnMeshes = 0;

    RHI::RenderPassDesc pass;
    pass.colorTarget = colorTarget;
    pass.depthTarget = depthTarget;
    pass.clearColor[0] = 0.0f;
    pass.clearColor[1] = 0.0f;
    pass.clearColor[2] = 0.0f;
    pass.clearColor[3] = 0.0f;
    pass.clearDepth = true;
    cmd->beginRenderPass(pass);
    cmd->bindPipeline(m_scenePipeline);
    cmd->setViewport(0, 0, static_cast<f32>(width), static_cast<f32>(height));

    u32 dirShadowSlot = 0;
    for (u32 i = 0; i < GpuDirectionalShadowMap::kMaxDirectionalShadowLights; ++i) {
        if (m_directionalShadows.valid(i)) {
            RHI::Texture* tex = m_directionalShadows.texture(i);
            if (tex) {
                cmd->bindTexture(tex, 2 + i, m_sampler);
            }
        }
    }

    for (const auto& draw : draws) {
        if (!draw.mesh || !ensureMeshUploaded(draw.mesh)) continue;

        if (draw.isTerrain && m_terrainPipeline) {
            cmd->bindPipeline(m_terrainPipeline);
        } else {
            cmd->bindPipeline(m_scenePipeline);
        }

        LightingUBO lights{};
        lights.cameraPos[0] = cameraPos.x;
        lights.cameraPos[1] = cameraPos.y;
        lights.cameraPos[2] = cameraPos.z;
        lights.ambient[0] = 0.18f;
        lights.ambient[1] = 0.18f;
        lights.ambient[2] = 0.18f;
        lights.albedo[0] = draw.albedo.x;
        lights.albedo[1] = draw.albedo.y;
        lights.albedo[2] = draw.albedo.z;
        lights.metallic = draw.metallic;
        lights.roughness = draw.roughness;

        lights.dirCount = static_cast<int>(std::min(lighting.lights.directionals.size(), size_t{4}));
        dirShadowSlot = 0;
        for (int i = 0; i < lights.dirCount; ++i) {
            const auto& d = lighting.lights.directionals[i];
            lights.dirData[i * 4 + 0] = d.direction.x;
            lights.dirData[i * 4 + 1] = d.direction.y;
            lights.dirData[i * 4 + 2] = d.direction.z;
            lights.dirData[i * 4 + 3] = d.intensity;
            lights.dirColor[i * 4 + 0] = d.color.x;
            lights.dirColor[i * 4 + 1] = d.color.y;
            lights.dirColor[i * 4 + 2] = d.color.z;

            if (draw.receiveShadows && d.castShadows &&
                dirShadowSlot < GpuDirectionalShadowMap::kMaxDirectionalShadowLights &&
                m_directionalShadows.valid(dirShadowSlot)) {
                lights.dirShadow[i * 4 + 0] = 1.0f;
                lights.dirShadow[i * 4 + 1] = static_cast<float>(dirShadowSlot);
                std::memcpy(lights.dirShadowVP + dirShadowSlot * 16,
                            m_directionalShadows.lightVP(dirShadowSlot).data(), sizeof(float) * 16);
                ++dirShadowSlot;
            }
        }

        lights.pointCount = static_cast<int>(std::min(lighting.lights.points.size(), size_t{4}));
        u32 pointShadowSlot = 0;
        for (int i = 0; i < lights.pointCount; ++i) {
            const auto& p = lighting.lights.points[i];
            lights.pointData[i * 4 + 0] = p.position.x;
            lights.pointData[i * 4 + 1] = p.position.y;
            lights.pointData[i * 4 + 2] = p.position.z;
            lights.pointData[i * 4 + 3] = p.radius;
            lights.pointColor[i * 4 + 0] = p.color.x;
            lights.pointColor[i * 4 + 1] = p.color.y;
            lights.pointColor[i * 4 + 2] = p.color.z;

            if (draw.receiveShadows && p.castShadows &&
                pointShadowSlot < GpuPointShadowMap::kMaxPointShadowLights) {
                lights.pointShadow[i * 4 + 0] = 1.0f;
                lights.pointShadow[i * 4 + 1] = static_cast<float>(pointShadowSlot);
                RHI::Texture* cube = m_pointShadows.cubemap(pointShadowSlot);
                if (cube) {
                    cmd->bindTexture(cube, pointShadowSlot, m_sampler);
                }
                ++pointShadowSlot;
            }
        }

        if (draw.isTerrain) {
            TerrainMaterialUBO mat{};
            if (draw.terrainSettings) {
                mat.worldSize[0] = draw.terrainSettings->worldSizeX;
                mat.worldSize[1] = draw.terrainSettings->worldSizeZ;
                const Mat4 inv = draw.worldMatrix.inverted();
                std::memcpy(mat.modelInv, inv.data(), sizeof(mat.modelInv));
            }
            const Terrain::TerrainGpuTextures* gpu =
                Terrain::TerrainCache::instance().gpuTexturesFor(draw.entity);
            RHI::Texture* whiteTex =
                Terrain::TerrainGpuTextureCache::instance().whiteTexture(m_device);
            // SDL_GPU requires every sampler declared by the shader to be bound.
            for (u32 slot = 0; slot < 10; ++slot) {
                if (whiteTex) cmd->bindTexture(whiteTex, slot, m_repeatSampler);
            }
            if (gpu && gpu->useSplatmap) {
                mat.flags[0] = 1.0f;
                if (gpu->splatMap) {
                    cmd->bindTexture(gpu->splatMap, 4, m_sampler);
                }
                for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
                    RHI::Texture* layerTex = gpu->layers[i] ? gpu->layers[i] : whiteTex;
                    if (layerTex) {
                        cmd->bindTexture(layerTex, 5 + i, m_repeatSampler);
                    }
                }
            } else if (gpu) {
                RHI::Texture* albedoTex = gpu->albedo ? gpu->albedo : whiteTex;
                if (albedoTex) {
                    mat.flags[1] = 1.0f;
                    cmd->bindTexture(albedoTex, 9, m_repeatSampler);
                }
            }
            cmd->pushUniformData(RHI::ShaderStage::Fragment, 1, &mat, sizeof(mat));
        }

        VertexUBO vubo{};
        const Mat4 mvp = vp * draw.worldMatrix;
        std::memcpy(vubo.mvp, mvp.data(), sizeof(vubo.mvp));
        std::memcpy(vubo.model, draw.worldMatrix.data(), sizeof(vubo.model));
        cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &vubo, sizeof(vubo));
        cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &lights, sizeof(lights));

        cmd->bindVertexBuffer(draw.mesh->vertexBuffer);
        cmd->bindIndexBuffer(draw.mesh->indexBuffer);
        cmd->drawIndexed(static_cast<u32>(draw.mesh->indices.size()));
        ++drawnMeshes;
    }

    cmd->endRenderPass();
    return drawnMeshes;
}

u32 GpuSceneRenderer::render(RHI::CommandBuffer* cmd, ECS::World& world,
                              const Editor::EditorContext& ctx, RHI::Texture* colorTarget,
                              RHI::Texture* depthTarget, u32 width, u32 height,
                              const std::string& projectRoot) {
    if (!m_ready || !cmd || !colorTarget || !depthTarget || width < 1 || height < 1) return 0;
    if (ctx.viewMode != Editor::EditorContext::ViewMode::Mode3D) return 0;

    const f32 sinY = std::sin(ctx.camYaw), cosY = std::cos(ctx.camYaw);
    const f32 sinP = std::sin(ctx.camPitch), cosP = std::cos(ctx.camPitch);
    const Vec3 cameraPos = ctx.camFocus + Vec3(sinY * cosP, -sinP, -cosY * cosP) * ctx.camDistance;
    const Mat4 view = Mat4::lookAt(cameraPos, ctx.camFocus, Vec3(0, 1, 0));
    const f32 aspect = static_cast<f32>(width) / static_cast<f32>(std::max(height, 1u));
    const Mat4 proj = Mat4::perspective(60.0f * 3.14159265f / 180.0f, aspect, 0.1f, 10000.0f);
    const Mat4 vp = proj * view;

    std::vector<MeshDraw> draws;
    const Spatial::Frustum frustum = Spatial::Frustum::fromCamera(
        cameraPos, ctx.camFocus, Vec3(0.0f, 1.0f, 0.0f),
        60.0f * 3.14159265f / 180.0f, aspect, 0.1f, 10000.0f);

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
                                   terrainChunks);
            for (const Terrain::TerrainDrawChunk& chunk : terrainChunks) {
                if (!chunk.mesh || chunk.mesh->vertices.empty()) continue;
                MeshDraw draw;
                draw.entity = entity;
                draw.mesh = chunk.mesh;
                draw.worldMatrix = worldMatrix;
                draw.terrainSettings = terrain;
                draw.isTerrain = true;
                draw.castShadows = false;
                draw.receiveShadows = Scene::meshReceivesShadows(world, entity);
                if (auto* renderer = world.get<ECS::MeshRendererComponent>(entity)) {
                    draw.receiveShadows = renderer->receiveShadows;
                }
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
        draws.push_back(draw);
    });

    if (draws.empty()) return 0;

    Scene::SceneLighting lighting;
    lighting.clear();
    Scene::collectSceneLights(world, lighting.lights);

    const bool wantsDirShadows = std::any_of(
        lighting.lights.directionals.begin(), lighting.lights.directionals.end(),
        [](const Scene::DirectionalLightData& l) { return l.castShadows; });
    const bool wantsPointShadows = std::any_of(
        lighting.lights.points.begin(), lighting.lights.points.end(),
        [](const Scene::PointLightData& l) { return l.castShadows; });

    if (wantsDirShadows) {
        renderDirectionalShadows(cmd, world, lighting, draws, ctx.camFocus);
    }
    if (wantsPointShadows) {
        renderPointShadows(cmd, world, lighting, draws);
    }
    return renderMeshes(cmd, draws, vp, cameraPos, lighting, colorTarget, depthTarget, width, height);
}

}  // namespace Caffeine::Render

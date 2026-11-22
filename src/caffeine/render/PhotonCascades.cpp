// ============================================================================
// @file    PhotonCascades.cpp
// @brief   Photon compute path implementation (P3: cascades K-1..0)
// ============================================================================
#include "render/PhotonCascades.hpp"

#include "caffeine/photon/PhotonFarField.hpp"
#include "render/GpuProfile.hpp"
#include "render/ShaderBytecode.hpp"
#include "rhi/CommandBuffer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

namespace Caffeine::Render {
namespace {

/// std140 layout shared by photon_cascade.comp and photon_c0.comp.
/// Keep all three in sync; the static_assert catches CPU-side drift.
struct PhotonCascadeUBO {
    Vec4 proj;      // P00, P11, P22, P23
    Vec4 extra;     // P32, sky depth, K, k (cascade) / hasUpper (C0)
    Vec4 texel;     // 1/W, 1/H, W, H
    Mat4 view;      // world -> view
    Mat4 invView;   // view -> world (far-field endpoint)
    Vec4 params;    // s0, reach, steps, thicknessRel
    Vec4 params2;   // kappa, uMin, skyIntensity, tile side
    Vec4 sky;       // rgb sky radiance
    Vec4 jitter;    // Halton(2,3) - 0.5
    i32  grid[4];   // G_k.x, G_k.y, atlasW, atlasH
    Vec4 farLevels[4]; // per level: xyz probe origin, w probe spacing
    Vec4 farParams;    // atlas width, atlas height, enabled
    Mat4 prevViewProj; // P5 reprojection into the previous frame
    Vec4 sParams;      // x S_cap, y valid
};
static_assert(sizeof(PhotonCascadeUBO) == 416,
              "PhotonCascadeUBO must match the GLSL std140 layout");

/// std140 layout of TriUBO in photon_voxelize_tri.comp.
struct TriangleVoxelizeUBO {
    Vec4 clip[4];  // per level: xyz clipmap origin, w cell size
    u32  params[4];  // triangle count, level mask, mode (0 voxelise, 1 clear, 2 clear box, 3 voxelise box), cells
    u32  atlas[4];   // tiles per row, level stride, 0, 0
    Vec4 regionLo;   // world box of a region update
    Vec4 regionHi;
};
static_assert(sizeof(TriangleVoxelizeUBO) == 128,
              "TriangleVoxelizeUBO must match the GLSL std140 layout");

/// The scene's triangle count the voxeliser accepts (64 bytes each): a bigger scene falls back to the
/// screened fill alone rather than allocating without bound.
constexpr u32 kMaxVoxelTriangles = 1u << 20;

constexpr f32 kKappa = 1.0f;  // spec §4.2 default
constexpr f32 kSkyR = ECS::kPhotonSkyRadiance[0];
constexpr f32 kSkyG = ECS::kPhotonSkyRadiance[1];
constexpr f32 kSkyB = ECS::kPhotonSkyRadiance[2];
constexpr f32 kSkyDepth = 1.0e30f;

/// Halton(index, base) in [0, 1). Per-frame direction jitter.
f32 halton(u32 index, u32 base) {
    f32 f = 1.0f;
    f32 result = 0.0f;
    u32 i = index;
    while (i > 0) {
        f /= static_cast<f32>(base);
        result += f * static_cast<f32>(i % base);
        i /= base;
    }
    return result;
}

u32 ceilDiv(u32 a, u32 b) { return (a + b - 1) / b; }

u32 roundUp(u32 value, u32 multiple) {
    if (multiple == 0) return value;
    return ceilDiv(value, multiple) * multiple;
}

}  // namespace

bool PhotonCascades::init(RHI::RenderDevice* device) {
    if (!device || !device->isInitialized()) {
        return false;
    }
    if (m_device && m_device != device) {
        shutdown(m_device);
    }
    m_device = device;

    const RHI::ShaderBytecodeFormat format = detectShaderFormat(device);
    if (!m_cascadePipeline) {
        const ShaderBytecodeView bytecode =
            getBuiltinShaderBytecode(BuiltinShader::PhotonCascade, format);
        if (bytecode.data && bytecode.size > 0) {
            RHI::ComputePipelineDesc desc;
            desc.code = bytecode.data;
            desc.codeSize = bytecode.size;
            desc.entryPoint = bytecode.entryPoint;
            desc.format = format;
            desc.threadCountX = 8;
            desc.threadCountY = 8;
            desc.threadCountZ = 1;
            desc.numSamplers = 9;
            desc.numReadwriteStorageTextures = 1;
            desc.numUniformBuffers = 1;
            m_cascadePipeline = device->createComputePipeline(desc);
        }
    }
    if (!m_c0Pipeline) {
        const ShaderBytecodeView bytecode =
            getBuiltinShaderBytecode(BuiltinShader::PhotonC0, format);
        if (bytecode.data && bytecode.size > 0) {
            RHI::ComputePipelineDesc desc;
            desc.code = bytecode.data;
            desc.codeSize = bytecode.size;
            desc.entryPoint = bytecode.entryPoint;
            desc.format = format;
            desc.threadCountX = 8;
            desc.threadCountY = 8;
            desc.threadCountZ = 1;
            desc.numSamplers = 9;
            desc.numReadwriteStorageTextures = 1;
            desc.numUniformBuffers = 1;
            m_c0Pipeline = device->createComputePipeline(desc);
        }
    }
    if (!m_voxelizePipeline) {
        const ShaderBytecodeView bytecode =
            getBuiltinShaderBytecode(BuiltinShader::PhotonVoxelize, format);
        if (bytecode.data && bytecode.size > 0) {
            RHI::ComputePipelineDesc desc;
            desc.code = bytecode.data;
            desc.codeSize = bytecode.size;
            desc.entryPoint = bytecode.entryPoint;
            desc.format = format;
            desc.threadCountX = 64;
            desc.threadCountY = 1;
            desc.threadCountZ = 1;
            desc.numSamplers = 2;
            desc.numReadwriteStorageTextures = 1;
            desc.numUniformBuffers = 1;
            m_voxelizePipeline = device->createComputePipeline(desc);
        }
    }
    if (!m_triPipeline) {
        const ShaderBytecodeView bytecode =
            getBuiltinShaderBytecode(BuiltinShader::PhotonVoxelizeTri, format);
        if (bytecode.data && bytecode.size > 0) {
            RHI::ComputePipelineDesc desc;
            desc.code = bytecode.data;
            desc.codeSize = bytecode.size;
            desc.entryPoint = bytecode.entryPoint;
            desc.format = format;
            desc.threadCountX = 64;
            desc.threadCountY = 1;
            desc.threadCountZ = 1;
            desc.numReadonlyStorageBuffers = 1;
            desc.numReadwriteStorageTextures = 1;
            desc.numUniformBuffers = 1;
            m_triPipeline = device->createComputePipeline(desc);
        }
    }
    if (!m_probePipeline) {
        const ShaderBytecodeView bytecode =
            getBuiltinShaderBytecode(BuiltinShader::PhotonProbe, format);
        if (bytecode.data && bytecode.size > 0) {
            RHI::ComputePipelineDesc desc;
            desc.code = bytecode.data;
            desc.codeSize = bytecode.size;
            desc.entryPoint = bytecode.entryPoint;
            desc.format = format;
            desc.threadCountX = 64;
            desc.threadCountY = 1;
            desc.threadCountZ = 1;
            desc.numSamplers = 6;
            desc.numReadwriteStorageTextures = 3;
            desc.numUniformBuffers = 1;
            m_probePipeline = device->createComputePipeline(desc);
        }
    }
    if (!m_injectPipeline) {
        const ShaderBytecodeView bytecode =
            getBuiltinShaderBytecode(BuiltinShader::PhotonInject, format);
        if (bytecode.data && bytecode.size > 0) {
            RHI::ComputePipelineDesc desc;
            desc.code = bytecode.data;
            desc.codeSize = bytecode.size;
            desc.entryPoint = bytecode.entryPoint;
            desc.format = format;
            desc.threadCountX = 8;
            desc.threadCountY = 8;
            desc.threadCountZ = 1;
            desc.numSamplers = 2;
            desc.numReadwriteStorageTextures = 3;
            desc.numUniformBuffers = 1;
            m_injectPipeline = device->createComputePipeline(desc);
        }
    }
    if (!m_point) {
        RHI::SamplerDesc pointDesc;
        pointDesc.linearFilter = false;
        pointDesc.clampToEdge = true;
        m_point = device->createSampler(pointDesc);
    }
    if (!m_linear) {
        RHI::SamplerDesc linearDesc;
        linearDesc.linearFilter = true;
        linearDesc.clampToEdge = true;
        m_linear = device->createSampler(linearDesc);
    }
    return ready();
}

bool PhotonCascades::ready() const {
    return m_device && m_cascadePipeline && m_c0Pipeline && m_point;
}

void PhotonCascades::releaseTargets(RHI::RenderDevice* device) {
    if (!device) return;
    if (m_shAtlas) {
        device->destroyTexture(m_shAtlas);
        m_shAtlas = nullptr;
    }
    for (RHI::Texture*& atlas : m_atlases) {
        if (atlas) device->destroyTexture(atlas);
    }
    m_atlases.clear();
    if (m_dummyUpper) {
        device->destroyTexture(m_dummyUpper);
        m_dummyUpper = nullptr;
    }
    m_minZ.shutdown(device);
    m_width = 0;
    m_height = 0;
    m_s0 = 0;
    m_cascadeCount = 0;
    m_gridW = 0;
    m_gridH = 0;
    m_atlasW = 0;
    m_atlasH = 0;
}

void PhotonCascades::shutdown(RHI::RenderDevice* device) {
    RHI::RenderDevice* target = device ? device : m_device;
    if (!target) return;
    releaseFarTargets(target);
    releaseTargets(target);
    if (m_cascadePipeline) {
        target->destroyComputePipeline(m_cascadePipeline);
        m_cascadePipeline = nullptr;
    }
    if (m_c0Pipeline) {
        target->destroyComputePipeline(m_c0Pipeline);
        m_c0Pipeline = nullptr;
    }
    if (m_voxelizePipeline) {
        target->destroyComputePipeline(m_voxelizePipeline);
        m_voxelizePipeline = nullptr;
    }
    if (m_triPipeline) {
        target->destroyComputePipeline(m_triPipeline);
        m_triPipeline = nullptr;
    }
    if (m_triBuffer) {
        target->destroyBuffer(m_triBuffer);
        m_triBuffer = nullptr;
    }
    m_triCapacity = 0;
    m_triCount = 0;
    m_geometryValid = false;
    if (m_probePipeline) {
        target->destroyComputePipeline(m_probePipeline);
        m_probePipeline = nullptr;
    }
    if (m_injectPipeline) {
        target->destroyComputePipeline(m_injectPipeline);
        m_injectPipeline = nullptr;
    }
    if (m_point) {
        target->destroySampler(m_point);
        m_point = nullptr;
    }
    if (m_linear) {
        target->destroySampler(m_linear);
        m_linear = nullptr;
    }
    m_device = nullptr;
}

void PhotonCascades::releaseFarTargets(RHI::RenderDevice* device) {
    if (!device) return;
    if (m_voxelAlbedo) {
        device->destroyTexture(m_voxelAlbedo);
        m_voxelAlbedo = nullptr;
    }
    if (m_probeRadiance) {
        device->destroyTexture(m_probeRadiance);
        m_probeRadiance = nullptr;
    }
    if (m_probeMoments) {
        device->destroyTexture(m_probeMoments);
        m_probeMoments = nullptr;
    }
    if (m_probeSH) {
        device->destroyTexture(m_probeSH);
        m_probeSH = nullptr;
    }
    if (m_probeRadiancePrev) {
        device->destroyTexture(m_probeRadiancePrev);
        m_probeRadiancePrev = nullptr;
    }
    if (m_probeMomentsPrev) {
        device->destroyTexture(m_probeMomentsPrev);
        m_probeMomentsPrev = nullptr;
    }
    for (RHI::Texture*& radiance : m_voxelRadiance) {
        if (radiance) device->destroyTexture(radiance);
        radiance = nullptr;
    }
    m_farCleared = false;
    for (bool& built : m_levelBuilt) built = false;
}

bool PhotonCascades::ensureTargets(RHI::RenderDevice* device, u32 width, u32 height, u32 s0,
                                   u32 cascadeCount) {
    if (!device || width == 0 || height == 0 || s0 == 0 || cascadeCount == 0) {
        return false;
    }
    if (m_shAtlas && m_width == width && m_height == height && m_s0 == s0 &&
        m_cascadeCount == cascadeCount && m_minZ.isReady()) {
        return true;
    }

    releaseTargets(device);
    m_atlasesValid.fill(false);
    ++m_stats.targetRebuilds;

    m_width = width;
    m_height = height;
    m_s0 = s0;
    m_cascadeCount = cascadeCount;

    // W' is rounded up to a multiple of s0·2^(K-1) so every G_k is exact.
    const u32 alignment = s0 << (cascadeCount - 1);
    const u32 widthPrime = roundUp(width, alignment);
    const u32 heightPrime = roundUp(height, alignment);
    m_gridW = widthPrime / s0;
    m_gridH = heightPrime / s0;
    m_atlasW = m_gridW * 2;
    m_atlasH = m_gridH * 2;

    RHI::TextureDesc atlasDesc;
    atlasDesc.width = m_atlasW;
    atlasDesc.height = m_atlasH;
    atlasDesc.format = RHI::TextureFormat::R16G16B16A16_FLOAT;
    atlasDesc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ComputeWrite;
    atlasDesc.mipLevels = 1;
    atlasDesc.type = RHI::TextureType::Texture2D;

    m_shAtlas = device->createTexture(atlasDesc);
    m_atlases.assign(cascadeCount, nullptr);
    for (u32 k = 1; k < cascadeCount; ++k) {
        m_atlases[k] = device->createTexture(atlasDesc);
    }

    RHI::TextureDesc dummyDesc;
    dummyDesc.width = 1;
    dummyDesc.height = 1;
    dummyDesc.format = RHI::TextureFormat::R16G16B16A16_FLOAT;
    dummyDesc.usage = RHI::TextureUsage::Sampler;
    dummyDesc.mipLevels = 1;
    dummyDesc.type = RHI::TextureType::Texture2D;
    m_dummyUpper = device->createTexture(dummyDesc);

    if (!m_minZ.init(device, width, height)) {
        return false;
    }
    if (!m_shAtlas || !m_dummyUpper) {
        return false;
    }
    for (u32 k = 1; k < cascadeCount; ++k) {
        if (!m_atlases[k]) return false;
    }
    return true;}

bool PhotonCascades::dispatchCascade(RHI::CommandBuffer* cmd, u32 cascade, RHI::Texture* upper,
                                     RHI::Texture* target, const Mat4& view, const Mat4& proj,
                                     u32 width, u32 height,
                                     const ECS::PhotonGIComponent& settings, f32 uMin,
                                     const Vec4& jitter, f32 skyIntensity) {
    if (!cmd || !upper || !target || !m_cascadePipeline) {
        return false;
    }
    const u32 k = cascade;
    const u32 gx = std::max(1u, m_gridW >> k);
    const u32 gy = std::max(1u, m_gridH >> k);
    const f32 tile = static_cast<f32>(1u << (k + 1u));

    PhotonCascadeUBO ubo{};
    ubo.proj = Vec4(proj(0, 0), proj(1, 1), proj(2, 2), proj(2, 3));
    ubo.extra = Vec4(proj(3, 2), kSkyDepth, static_cast<f32>(m_cascadeCount),
                     static_cast<f32>(k));
    ubo.texel = Vec4(1.0f / static_cast<f32>(width), 1.0f / static_cast<f32>(height),
                     static_cast<f32>(width), static_cast<f32>(height));
    ubo.view = view;
    ubo.invView = view.inverted();
    ubo.params = Vec4(static_cast<f32>(m_s0), std::max(settings.radius, 0.1f),
                      static_cast<f32>(std::clamp<i32>(settings.computeSteps, 1, 32)),
                      std::max(settings.thickness, 0.001f));
    ubo.params2 = Vec4(kKappa, uMin, skyIntensity, tile);
    ubo.sky = Vec4(kSkyR * m_skyTint.x * skyIntensity, kSkyG * m_skyTint.y * skyIntensity,
                       kSkyB * m_skyTint.z * skyIntensity, 1.0f);
    ubo.jitter = jitter;
    ubo.grid[0] = static_cast<i32>(gx);
    ubo.grid[1] = static_cast<i32>(gy);
    ubo.grid[2] = static_cast<i32>(m_atlasW);
    ubo.grid[3] = static_cast<i32>(m_atlasH);
    for (u32 level = 0; level < 4; ++level) {
        ubo.farLevels[level] = Vec4(m_farProbeOrigin[level].x, m_farProbeOrigin[level].y,
                                    m_farProbeOrigin[level].z, m_farProbeSpacing[level]);
    }
    ubo.farParams = Vec4(640.0f, 2560.0f, m_farEnabled ? 1.0f : 0.0f, 0.0f);
    ubo.prevViewProj = m_prevViewProj;
    ubo.sParams = Vec4(4.0f, (m_sValid && m_sPrev) ? 1.0f : 0.0f, 0.0f, 0.0f);

    cmd->beginComputePass(target);
    cmd->bindComputePipeline(m_cascadePipeline);
    RHI::Texture* feedback = m_sPrev ? m_sPrev : m_direct;
    RHI::Texture* textures[9] = {m_direct, m_depth, m_albedo, m_normalMaterial, upper,
                                 m_minZ.texture(), m_probeRadiance, m_probeMoments, feedback};
    RHI::Sampler* samplers[9] = {m_point, m_point, m_point, m_point, m_point, m_point,
                                 m_linear, m_linear, m_linear};
    cmd->bindComputeTextures(textures, samplers, 0, 9);
    cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
    cmd->dispatch(ceilDiv(m_atlasW, 8u), ceilDiv(m_atlasH, 8u), 1u);
    cmd->endComputePass();
    return true;
}

bool PhotonCascades::dispatchC0(RHI::CommandBuffer* cmd, RHI::Texture* upper, const Mat4& view,
                                const Mat4& proj, u32 width, u32 height,
                                const ECS::PhotonGIComponent& settings, f32 uMin,
                                const Vec4& jitter, f32 skyIntensity) {
    if (!cmd || !upper || !m_shAtlas || !m_c0Pipeline) {
        return false;
    }
    PhotonCascadeUBO ubo{};
    ubo.proj = Vec4(proj(0, 0), proj(1, 1), proj(2, 2), proj(2, 3));
    ubo.extra = Vec4(proj(3, 2), kSkyDepth, static_cast<f32>(m_cascadeCount),
                     m_cascadeCount > 1 ? 1.0f : 0.0f);
    ubo.texel = Vec4(1.0f / static_cast<f32>(width), 1.0f / static_cast<f32>(height),
                     static_cast<f32>(width), static_cast<f32>(height));
    ubo.view = view;
    ubo.invView = view.inverted();
    ubo.params = Vec4(static_cast<f32>(m_s0), std::max(settings.radius, 0.1f),
                      static_cast<f32>(std::clamp<i32>(settings.computeSteps, 1, 32)),
                      std::max(settings.thickness, 0.001f));
    // C0 continues in M̄1, whose atlas tile side is 4.
    ubo.params2 = Vec4(kKappa, uMin, skyIntensity, 4.0f);
    ubo.sky = Vec4(kSkyR * m_skyTint.x * skyIntensity, kSkyG * m_skyTint.y * skyIntensity,
                       kSkyB * m_skyTint.z * skyIntensity, 1.0f);
    ubo.jitter = jitter;
    ubo.grid[0] = static_cast<i32>(m_gridW);
    ubo.grid[1] = static_cast<i32>(m_gridH);
    ubo.grid[2] = static_cast<i32>(m_atlasW);
    ubo.grid[3] = static_cast<i32>(m_atlasH);
    for (u32 level = 0; level < 4; ++level) {
        ubo.farLevels[level] = Vec4(m_farProbeOrigin[level].x, m_farProbeOrigin[level].y,
                                    m_farProbeOrigin[level].z, m_farProbeSpacing[level]);
    }
    ubo.farParams = Vec4(640.0f, 2560.0f, m_farEnabled ? 1.0f : 0.0f, 0.0f);
    ubo.prevViewProj = m_prevViewProj;
    ubo.sParams = Vec4(4.0f, (m_sValid && m_sPrev) ? 1.0f : 0.0f, 0.0f, 0.0f);

    cmd->beginComputePass(m_shAtlas);
    cmd->bindComputePipeline(m_c0Pipeline);
    RHI::Texture* feedback = m_sPrev ? m_sPrev : m_direct;
    RHI::Texture* textures[9] = {m_direct, m_depth, m_albedo, m_normalMaterial, upper,
                                 m_minZ.texture(), m_probeRadiance, m_probeMoments, feedback};
    RHI::Sampler* samplers[9] = {m_point, m_point, m_point, m_point, m_point, m_point,
                                 m_linear, m_linear, m_linear};
    cmd->bindComputeTextures(textures, samplers, 0, 9);
    cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
    cmd->dispatch(ceilDiv(m_gridW, 8u), ceilDiv(m_gridH, 8u), 1u);
    cmd->endComputePass();
    return true;
}

namespace {

struct VoxelizeUBO {
    Vec4 proj;
    Vec4 projExtra;
    Vec4 texel;
    Mat4 invView;
    Vec4 clip[4];
    Vec4 atlas;
};
static_assert(sizeof(VoxelizeUBO) == 192, "VoxelizeUBO must match the GLSL std140 layout");

struct ProbeUBO {
    Vec4 clip;
    Vec4 probe;
    Vec4 params;
    Vec4 sky;
    Vec4 jitter;
    i32  dims[4];
    i32  offsets[4];
    Vec4 planes[4];  ///< the camera frustum's side planes, normalised, inside positive
};
static_assert(sizeof(ProbeUBO) == 176, "ProbeUBO must match the GLSL std140 layout");

struct InjectUBO {
    Vec4 clip[4];
    Vec4 probe[4];
    Vec4 params;
    Vec4 ambient;
    Vec4 atlas;
    Vec4 counts;  // directional, point, spot light counts
    Vec4 dirDir[PhotonLights::kMaxDirectional];
    Vec4 dirColor[PhotonLights::kMaxDirectional];
    Vec4 pointPos[PhotonLights::kMaxPoint];
    Vec4 pointColor[PhotonLights::kMaxPoint];
    Vec4 spotPos[PhotonLights::kMaxSpot];
    Vec4 spotDir[PhotonLights::kMaxSpot];
    Vec4 spotColor[PhotonLights::kMaxSpot];
    Vec4 cloudShadow[3];  // the clouds' shadow on the first directional light
};
static_assert(sizeof(InjectUBO) == 176 + 16 * (1 + 2 + 2 + 8 + 8 + 4 + 4 + 4 + 3),
              "InjectUBO must match the GLSL std140 layout");

}  // namespace

bool PhotonCascades::ensureFarTargets(RHI::RenderDevice* device) {
    if (!device) return false;
    if (m_voxelAlbedo && m_probeRadiance && m_probeMoments && m_probeSH &&
        m_probeRadiancePrev && m_probeMomentsPrev &&
        m_voxelRadiance[0] && m_voxelRadiance[1] && m_voxelRadiance[2]) {
        return true;
    }
    releaseFarTargets(device);

    RHI::TextureDesc desc;
    desc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget |
                 RHI::TextureUsage::ComputeWrite;
    desc.mipLevels = 1;
    desc.type = RHI::TextureType::Texture2D;

    desc.width = 2048;  // 4 levels × 8×8 tiles of 64² voxels
    desc.height = 512;
    desc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    m_voxelAlbedo = device->createTexture(desc);

    desc.width = 640;   // 4 levels × 16 probes × 10
    desc.height = 2560; // 16 z-slices × 16 probe rows × 10
    desc.format = RHI::TextureFormat::R16G16B16A16_FLOAT;
    m_probeRadiance = device->createTexture(desc);
    m_probeRadiancePrev = device->createTexture(desc);
    // RGBA16F: RG16F is not a guaranteed compute-storage format in SDL_GPU.
    desc.format = RHI::TextureFormat::R16G16B16A16_FLOAT;
    m_probeMoments = device->createTexture(desc);
    m_probeMomentsPrev = device->createTexture(desc);

    desc.width = 128;   // 4 levels × 16 probes × 2 SH texels
    desc.height = 512;  // 16 probe rows × 16 z-slices × 2
    m_probeSH = device->createTexture(desc);

    desc.width = 4096;  // 4 levels × 2 texels per voxel along the axis
    desc.height = 512;
    for (RHI::Texture*& radiance : m_voxelRadiance) {
        radiance = device->createTexture(desc);
    }

    return m_voxelAlbedo && m_probeRadiance && m_probeMoments && m_probeSH &&
           m_probeRadiancePrev && m_probeMomentsPrev &&
           m_voxelRadiance[0] && m_voxelRadiance[1] && m_voxelRadiance[2];
}

bool PhotonCascades::dispatchVoxelize(RHI::CommandBuffer* cmd, RHI::Texture* depth,
                                      RHI::Texture* albedo, const Mat4& view, const Mat4& proj,
                                      u32 width, u32 height) {
    if (!cmd || !depth || !albedo || !m_voxelizePipeline || !m_voxelAlbedo) return false;
    VoxelizeUBO ubo{};
    ubo.proj = Vec4(proj(0, 0), proj(1, 1), proj(2, 2), proj(2, 3));
    ubo.projExtra = Vec4(proj(3, 2), 0.0f, 0.0f, 0.0f);
    ubo.texel = Vec4(1.0f / static_cast<f32>(width), 1.0f / static_cast<f32>(height),
                     static_cast<f32>(width), static_cast<f32>(height));
    ubo.invView = view.inverted();
    for (u32 level = 0; level < 4; ++level) {
        ubo.clip[level] = Vec4(m_farClipOrigin[level].x, m_farClipOrigin[level].y,
                               m_farClipOrigin[level].z, m_farCellSize[level]);
    }
    ubo.atlas = Vec4(64.0f, 8.0f, 512.0f, 0.0f);

    cmd->beginComputePass(m_voxelAlbedo, 0, 0, false);
    cmd->bindComputePipeline(m_voxelizePipeline);
    RHI::Texture* textures[2] = {depth, albedo};
    RHI::Sampler* samplers[2] = {m_point, m_point};
    cmd->bindComputeTextures(textures, samplers, 0, 2);
    cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
    cmd->dispatch(ceilDiv(width * height, 64u), 1u, 1u);
    cmd->endComputePass();
    return true;
}

bool PhotonCascades::setGeometry(u64 hash, const std::vector<PhotonTriangle>& triangles) {
    PhotonGeometry geometry;
    PhotonObject object;
    object.key = 1;
    object.hash = hash;
    object.count = static_cast<u32>(std::min<usize>(triangles.size(), kMaxVoxelTriangles));
    geometry.objects.push_back(object);
    geometry.triangles = triangles;
    return setGeometry(geometry, {}, true);
}

bool PhotonCascades::setGeometry(const PhotonGeometry& geometry, const std::vector<PhotonBox>& dirty,
                                 bool full) {
    if (!m_device || !m_triPipeline) return false;
    const u32 count = geometry.overBudget
                          ? 0u
                          : static_cast<u32>(std::min<usize>(geometry.triangles.size(), kMaxVoxelTriangles));
    if (count > 0) {
        if (!m_triBuffer || count > m_triCapacity) {
            if (m_triBuffer) {
                m_device->destroyBuffer(m_triBuffer);
                m_triBuffer = nullptr;
            }
            u32 capacity = 1024;
            while (capacity < count) capacity *= 2;
            RHI::BufferDesc desc;
            desc.size = static_cast<u64>(capacity) * sizeof(PhotonTriangle);
            m_triBuffer = m_device->createBuffer(desc, RHI::BufferUsage::StorageRead);
            m_triCapacity = m_triBuffer ? capacity : 0;
            if (!m_triBuffer) return false;
            full = true;  // a new buffer holds nothing the volume was built from
        }
        if (!m_device->uploadBuffer(m_triBuffer, geometry.triangles.data(),
                                    static_cast<u64>(count) * sizeof(PhotonTriangle))) {
            return false;
        }
    }
    m_triCount = count;
    m_stats.objects = static_cast<u32>(geometry.objects.size());
    m_stats.triangles = count;
    m_stats.overBudget = geometry.overBudget ? 1u : 0u;
    // More than a handful of boxes is cheaper as a whole-level rebuild than as a pass per box.
    constexpr usize kMaxRegionBoxes = 8;
    if (full || !m_geometryValid || m_pendingBoxes.size() + dirty.size() > kMaxRegionBoxes) {
        ++m_geometryRevision;
        m_pendingBoxes.clear();
    } else {
        m_pendingBoxes.insert(m_pendingBoxes.end(), dirty.begin(), dirty.end());
    }
    m_geometryValid = true;
    return true;
}

bool PhotonCascades::updateGeometryVoxels(RHI::CommandBuffer* cmd, const Vec3& cameraPos) {
    if (!cmd || !m_triPipeline || !m_geometryValid || !ensureFarTargets(m_device)) return false;
    const auto started = std::chrono::steady_clock::now();

    u32 mask = 0;
    for (u32 level = 0; level < 4; ++level) {
        const Photon::FarFieldLevel l = Photon::computeFarFieldLevel(level, cameraPos);
        m_farClipOrigin[level] = l.clipmapOrigin;
        m_farCellSize[level] = l.cellSize;
        m_farProbeOrigin[level] = l.probeOrigin;
        m_farProbeSpacing[level] = l.probeSpacing;
        const bool moved = m_levelOrigin[level].x != l.clipmapOrigin.x ||
                           m_levelOrigin[level].y != l.clipmapOrigin.y ||
                           m_levelOrigin[level].z != l.clipmapOrigin.z;
        if (!m_levelBuilt[level] || moved || m_levelRevision[level] != m_geometryRevision) {
            mask |= 1u << level;
            m_levelOrigin[level] = l.clipmapOrigin;
            m_levelRevision[level] = m_geometryRevision;
            m_levelBuilt[level] = true;
        }
    }
    const std::vector<PhotonBox> boxes = std::move(m_pendingBoxes);
    m_pendingBoxes.clear();
    m_stats.lastLevelMask = mask;
    m_stats.lastRegions = 0;
    if (mask == 0 && boxes.empty()) {
        m_stats.lastUpdateMs = 0.0f;
        return true;
    }

    TriangleVoxelizeUBO ubo{};
    for (u32 level = 0; level < 4; ++level) {
        ubo.clip[level] = Vec4(m_farClipOrigin[level].x, m_farClipOrigin[level].y,
                               m_farClipOrigin[level].z, m_farCellSize[level]);
    }
    ubo.params[3] = 64;
    ubo.atlas[0] = 8;
    ubo.atlas[1] = 512;

    // The buffer has to exist for the pipeline's binding even when the scene has no triangles.
    if (!m_triBuffer) {
        RHI::BufferDesc desc;
        desc.size = 1024ull * sizeof(PhotonTriangle);
        m_triBuffer = m_device->createBuffer(desc, RHI::BufferUsage::StorageRead);
        m_triCapacity = m_triBuffer ? 1024u : 0u;
        if (!m_triBuffer) return false;
    }
    RHI::Buffer* readOnly[1] = {m_triBuffer};
    ubo.params[0] = m_triCount;

    auto pass = [&](u32 mode, u32 levels, u32 groups) {
        ubo.params[1] = levels;
        ubo.params[2] = mode;
        cmd->beginComputePass(m_voxelAlbedo, 0, 0, false);
        cmd->bindComputePipeline(m_triPipeline);
        cmd->bindComputeStorageBuffers(readOnly, 0, 1);
        cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
        cmd->dispatch(groups, 1u, 1u);
        cmd->endComputePass();
    };
    const u32 clearGroups = ceilDiv(4u * 64u * 64u * 64u, 64u);

    // 1) Levels rebuilt whole: clear them, then voxelise every triangle into them (a separate pass, so it
    //    sees the cleared texels).
    if (mask != 0) {
        pass(1, mask, clearGroups);
        if (m_triCount > 0) pass(0, mask, ceilDiv(m_triCount, 64u));
        m_stats.fullRebuilds += static_cast<u32>(__builtin_popcount(mask));
    }

    // 2) The rest of the levels: only the boxes around what changed, one clear and one voxelise each. A box
    //    is grown by a cell of its level so a triangle ending on the border still marks the voxels beyond it.
    const u32 regionLevels = ~mask & 0xFu;
    for (const PhotonBox& box : boxes) {
        if (regionLevels == 0) break;
        for (u32 level = 0; level < 4; ++level) {
            if ((regionLevels & (1u << level)) == 0) continue;
            const f32 pad = m_farCellSize[level];
            ubo.regionLo = Vec4(box.lo.x - pad, box.lo.y - pad, box.lo.z - pad, 0.0f);
            ubo.regionHi = Vec4(box.hi.x + pad, box.hi.y + pad, box.hi.z + pad, 0.0f);
            pass(2, 1u << level, clearGroups);
            if (m_triCount > 0) pass(3, 1u << level, ceilDiv(m_triCount, 64u));
        }
        ++m_stats.regionRebuilds;
        ++m_stats.lastRegions;
    }

    m_stats.lastUpdateMs = std::chrono::duration<f32, std::milli>(std::chrono::steady_clock::now() - started).count();
    return true;
}

void PhotonCascades::clearFarTargets(RHI::CommandBuffer* cmd) {
    if (!cmd || m_farCleared) return;
    auto clearTarget = [&](RHI::Texture* target) {
        if (!target) return;
        RHI::RenderPassDesc pass;
        pass.colorTarget = target;
        pass.clearColor[0] = 0.0f;
        pass.clearColor[1] = 0.0f;
        pass.clearColor[2] = 0.0f;
        pass.clearColor[3] = 0.0f;
        cmd->beginRenderPass(pass);
        cmd->endRenderPass();
    };
    clearTarget(m_voxelAlbedo);
    clearTarget(m_probeRadiance);
    clearTarget(m_probeMoments);
    clearTarget(m_probeRadiancePrev);
    clearTarget(m_probeMomentsPrev);
    clearTarget(m_probeSH);
    clearTarget(m_voxelRadiance[0]);
    clearTarget(m_voxelRadiance[1]);
    clearTarget(m_voxelRadiance[2]);
    m_farCleared = true;
    // The material volume was just emptied: every level has to be voxelised again.
    for (bool& built : m_levelBuilt) built = false;
}

bool PhotonCascades::dispatchProbes(RHI::CommandBuffer* cmd, u32 frameIndex, f32 skyIntensity) {
    if (!cmd || !m_probePipeline || !m_probeRadiance || !m_probeMoments) return false;

    RHI::Texture* storage[3] = {m_probeRadiance, m_probeMoments, m_probeSH};
    cmd->beginComputePass(storage, 3);
    cmd->bindComputePipeline(m_probePipeline);
    RHI::Texture* textures[6] = {m_voxelAlbedo, m_voxelRadiance[0], m_voxelRadiance[1],
                                 m_voxelRadiance[2], m_probeRadiancePrev, m_probeMomentsPrev};
    RHI::Sampler* samplers[6] = {m_point, m_point, m_point, m_point, m_point, m_point};
    cmd->bindComputeTextures(textures, samplers, 0, 6);

    const u32 jitterIndex = frameIndex % 64u + 1u;
    // z: the weight of the previous frame, lower while the scene changes so a moved object leaves no ghost.
    const f32 hysteresis = 1.0f - std::clamp(m_temporalWeight, 0.15f, 0.6f);
    const Vec4 jitter(halton(jitterIndex, 2) - 0.5f, halton(jitterIndex, 3) - 0.5f, hysteresis, 0.0f);
    const u32 probes = Photon::FarFieldConfig::kProbesPerAxis *
                       Photon::FarFieldConfig::kProbesPerAxis *
                       Photon::FarFieldConfig::kProbesPerAxis;

    // The camera frustum's side planes from the view-projection (Gribb-Hartmann): probes far outside it have no
    // reader on screen and are not traced.
    Vec4 planes[4];
    {
        const Mat4& m = m_viewProj;
        const f32 rows[4][4] = {{m(0, 0), m(0, 1), m(0, 2), m(0, 3)},
                                {m(1, 0), m(1, 1), m(1, 2), m(1, 3)},
                                {m(2, 0), m(2, 1), m(2, 2), m(2, 3)},
                                {m(3, 0), m(3, 1), m(3, 2), m(3, 3)}};
        const f32 sign[4] = {1.0f, -1.0f, 1.0f, -1.0f};
        const u32 axis[4] = {0, 0, 1, 1};
        for (u32 i = 0; i < 4; ++i) {
            f32 a = rows[3][0] + sign[i] * rows[axis[i]][0];
            f32 b = rows[3][1] + sign[i] * rows[axis[i]][1];
            f32 c = rows[3][2] + sign[i] * rows[axis[i]][2];
            f32 d = rows[3][3] + sign[i] * rows[axis[i]][3];
            const f32 length = std::sqrt(a * a + b * b + c * c);
            if (length > 1e-6f) {
                a /= length;
                b /= length;
                c /= length;
                d /= length;
            }
            planes[i] = Vec4(a, b, c, d);
        }
    }
    // Coarse levels change slowly: they are traced every few frames (the others carry their tiles over). While the
    // scene changes every level follows every frame, and the previous frame's weight is raised to the period so a
    // sliced level converges as fast per second as a level traced every frame.
    const bool calm = m_temporalWeight < 0.2f;
    static constexpr u32 kLevelPeriod[4] = {1u, 2u, 2u, 4u};
    for (u32 level = 0; level < 4; ++level) {
        const u32 period = calm ? kLevelPeriod[level] : 1u;
        const bool traced = ((frameIndex + level) % period) == 0u;
        ProbeUBO ubo{};
        ubo.clip = Vec4(m_farClipOrigin[level].x, m_farClipOrigin[level].y,
                        m_farClipOrigin[level].z, m_farCellSize[level]);
        ubo.probe = Vec4(m_farProbeOrigin[level].x, m_farProbeOrigin[level].y,
                         m_farProbeOrigin[level].z, m_farProbeSpacing[level]);
        const f32 extent = 16.0f * m_farProbeSpacing[level];
        ubo.params = Vec4(extent, 192.0f, static_cast<f32>(frameIndex), 64.0f);
        ubo.sky = Vec4(kSkyR * m_skyTint.x * skyIntensity, kSkyG * m_skyTint.y * skyIntensity,
                       kSkyB * m_skyTint.z * skyIntensity, 1.0f);
        ubo.jitter = jitter;
        ubo.jitter.z = std::pow(jitter.z, static_cast<f32>(period));
        // Frames a probe may go untraced and still keep its history: long enough that switching Photon off for a
        // moment (the voxels rebuild from the history the probes hold) does not reset them, short enough that a
        // probe that was out of view for seconds starts from a fresh trace.
        ubo.jitter.w = 120.0f;
        ubo.dims[0] = 16;
        ubo.dims[1] = 10;
        ubo.dims[2] = traced ? 0 : 1;
        ubo.dims[3] = m_probeCulling ? 1 : 0;
        for (u32 i = 0; i < 4; ++i) ubo.planes[i] = planes[i];
        ubo.offsets[0] = static_cast<i32>(level * 512u);   // material columns
        ubo.offsets[1] = static_cast<i32>(level * 1024u);  // radiance columns
        ubo.offsets[2] = static_cast<i32>(level * 160u);   // probe columns
        ubo.offsets[3] = static_cast<i32>(level * 32u);    // SH columns
        cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
        cmd->dispatch(ceilDiv(probes, 64u), 1u, 1u);
    }

    cmd->endComputePass();
    std::swap(m_probeRadiance, m_probeRadiancePrev);
    std::swap(m_probeMoments, m_probeMomentsPrev);
    return true;
}

bool PhotonCascades::dispatchInject(RHI::CommandBuffer* cmd, f32 skyIntensity) {
    if (!cmd || !m_injectPipeline || !m_voxelAlbedo || !m_probeSH ||
        !m_voxelRadiance[0] || !m_voxelRadiance[1] || !m_voxelRadiance[2]) {
        return false;
    }
    InjectUBO ubo{};
    for (u32 level = 0; level < 4; ++level) {
        ubo.clip[level] = Vec4(m_farClipOrigin[level].x, m_farClipOrigin[level].y,
                               m_farClipOrigin[level].z, m_farCellSize[level]);
        ubo.probe[level] = Vec4(m_farProbeOrigin[level].x, m_farProbeOrigin[level].y,
                                m_farProbeOrigin[level].z, m_farProbeSpacing[level]);
    }
    ubo.params = Vec4(64.0f, 512.0f, 4.0f, 0.0f);
    // The bounce gain scales what the voxels are lit with, so it scales the light that bounces and nothing else.
    const f32 gain = m_bounceGain;
    const f32 ambient = 3.14159265f * 0.5f * skyIntensity * gain;
    ubo.ambient = Vec4(ambient * m_skyTint.x, ambient * m_skyTint.y, ambient * m_skyTint.z, 1.0f);
    ubo.atlas = Vec4(640.0f, 2560.0f, 32.0f, 1024.0f);
    ubo.counts = Vec4(static_cast<f32>(m_lights.directionalCount),
                      static_cast<f32>(m_lights.pointCount), static_cast<f32>(m_lights.spotCount),
                      0.0f);
    for (u32 i = 0; i < PhotonLights::kMaxDirectional; ++i) {
        ubo.dirDir[i] = m_lights.directionalDir[i];
        ubo.dirColor[i] = m_lights.directionalColor[i] * gain;
    }
    for (u32 i = 0; i < PhotonLights::kMaxPoint; ++i) {
        ubo.pointPos[i] = m_lights.pointPosition[i];
        ubo.pointColor[i] = m_lights.pointColor[i] * gain;
    }
    for (u32 i = 0; i < 3; ++i) ubo.cloudShadow[i] = m_lights.cloudShadow[i];
    for (u32 i = 0; i < PhotonLights::kMaxSpot; ++i) {
        ubo.spotPos[i] = m_lights.spotPosition[i];
        ubo.spotDir[i] = m_lights.spotDirection[i];
        ubo.spotColor[i] = m_lights.spotColor[i] * gain;
    }

    RHI::Texture* storage[3] = {m_voxelRadiance[0], m_voxelRadiance[1], m_voxelRadiance[2]};
    cmd->beginComputePass(storage, 3);
    cmd->bindComputePipeline(m_injectPipeline);
    RHI::Texture* textures[2] = {m_voxelAlbedo, m_probeSH};
    RHI::Sampler* samplers[2] = {m_point, m_point};
    cmd->bindComputeTextures(textures, samplers, 0, 2);
    cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
    cmd->dispatch(ceilDiv(2048u, 8u), ceilDiv(512u, 8u), 1u);
    cmd->endComputePass();
    return true;
}

bool PhotonCascades::execute(RHI::CommandBuffer* cmd, RHI::Texture* depth, RHI::Texture* albedo,
                             RHI::Texture* normalMaterial, RHI::Texture* direct,
                             const Mat4& view, const Mat4& proj, u32 width, u32 height,
                             const ECS::PhotonGIComponent& settings, u32 frameIndex,
                             RHI::Texture* sPrev, const Mat4& prevViewProj, bool sValid) {
    if (!cmd || !depth || !depth->handle || !albedo || !normalMaterial || !direct) {
        return false;
    }
    if (!ready()) {
        return false;
    }

    const u32 s0 = std::clamp<u32>(settings.s0 == 0 ? 4u : settings.s0, 2u, 32u);
    const u32 cascadeCount = std::clamp<u32>(settings.cascades, 1u, 6u);
    if (!ensureTargets(m_device, width, height, s0, cascadeCount)) {
        return false;
    }

    m_direct = direct;
    m_depth = depth;
    m_albedo = albedo;
    m_normalMaterial = normalMaterial;
    m_sPrev = sPrev;
    m_prevViewProj = prevViewProj;
    m_viewProj = proj * view;
    {
        // The atlases are anchored to the screen: when the camera moves they are rebuilt every frame (the view
        // matrix, not the projection, so TAA's sub-pixel jitter does not count as motion).
        f32 moved = 0.0f;
        for (u32 i = 0; i < 16; ++i) moved = std::max(moved, std::abs(view.data()[i] - m_lastView.data()[i]));
        m_cameraMoved = moved > 1e-3f;
        m_lastView = view;
    }
    m_sValid = sValid;
    if (!sValid) m_atlasesValid.fill(false);  // after a cut or a resize every cascade is rebuilt

    // 1) Min-Z pyramid (render passes; leaves no pass active).
    {
        CF_GPU_SCOPE(cmd, "Photon::minZ");
        m_minZ.generate(cmd, depth, MinZParams::fromProjection(proj));
    }

    // 1b) Far field (level 0): screened voxel fill + probe trace.
    m_farEnabled = false;
    if (m_voxelizePipeline && m_probePipeline && m_linear && ensureFarTargets(m_device)) {
        const Mat4 invView = view.inverted();
        const Vec3 cameraPos(invView(0, 3), invView(1, 3), invView(2, 3));
        for (u32 level = 0; level < 4; ++level) {
            const Photon::FarFieldLevel l = Photon::computeFarFieldLevel(level, cameraPos);
            m_farClipOrigin[level] = l.clipmapOrigin;
            m_farCellSize[level] = l.cellSize;
            m_farProbeOrigin[level] = l.probeOrigin;
            m_farProbeSpacing[level] = l.probeSpacing;
        }

        clearFarTargets(cmd);
        m_bounceGain = std::clamp(settings.bounceGain, 0.0f, 8.0f);

        // The scene's own triangles first (what the screen cannot see), then the screened fill adds
        // what it can (terrain, skinned meshes).
        {
            CF_GPU_SCOPE(cmd, "Photon::geometryVoxels");
            updateGeometryVoxels(cmd, cameraPos);
        }
        const f32 farSky = std::max(settings.skyIntensity, 0.0f);
        // Everything the far field is a function of: the camera (its view, not the jittered projection), the lights,
        // the sky and what the voxels were built from. While all of it is the same frame after frame, and the
        // temporal averages have settled, tracing the probes again only reproduces the picture they already hold.
        {
            auto mixInto = [](u64& hash, const void* data, usize bytes) {
                const u8* p = static_cast<const u8*>(data);
                for (usize i = 0; i < bytes; ++i) {
                    hash ^= p[i];
                    hash *= 1099511628211ull;
                }
            };
            // What the far field depends on besides where the camera looks: the lights, the sky, the voxels' source.
            u64 inputs = 1469598103934665603ull;
            const u32 shape[3] = {s0, cascadeCount, m_geometryRevision};
            mixInto(inputs, shape, sizeof(shape));
            const u32 lightCounts[3] = {m_lights.directionalCount, m_lights.pointCount, m_lights.spotCount};
            mixInto(inputs, lightCounts, sizeof(lightCounts));
            for (u32 i = 0; i < m_lights.directionalCount; ++i) {
                mixInto(inputs, &m_lights.directionalDir[i], sizeof(Vec4));
                mixInto(inputs, &m_lights.directionalColor[i], sizeof(Vec4));
            }
            for (u32 i = 0; i < m_lights.pointCount; ++i) {
                mixInto(inputs, &m_lights.pointPosition[i], sizeof(Vec4));
                mixInto(inputs, &m_lights.pointColor[i], sizeof(Vec4));
            }
            for (u32 i = 0; i < m_lights.spotCount; ++i) {
                mixInto(inputs, &m_lights.spotPosition[i], sizeof(Vec4));
                mixInto(inputs, &m_lights.spotDirection[i], sizeof(Vec4));
                mixInto(inputs, &m_lights.spotColor[i], sizeof(Vec4));
            }
            mixInto(inputs, m_lights.cloudShadow, sizeof(m_lights.cloudShadow));
            mixInto(inputs, &m_skyTint, sizeof(Vec3));
            mixInto(inputs, &farSky, sizeof(f32));
            mixInto(inputs, &m_bounceGain, sizeof(f32));
            // Where the clipmaps sit and what was revoxelised: when these move the far field has real work to do.
            u64 placement = 1469598103934665603ull;
            mixInto(placement, m_farClipOrigin, sizeof(m_farClipOrigin));
            const u32 rebuilds = m_stats.fullRebuilds + m_stats.regionRebuilds;
            mixInto(placement, &rebuilds, sizeof(rebuilds));
            const bool placementHeld = placement == m_farPlacementSignature;
            m_farPlacementSignature = placement;
            // The picture too: the camera's view (not the jittered projection) and the target's size.
            u64 signature = inputs;
            mixInto(signature, &placement, sizeof(placement));
            mixInto(signature, view.data(), 16 * sizeof(f32));
            const u32 size[2] = {width, height};
            mixInto(signature, size, sizeof(size));

            const bool calm = m_temporalWeight < 0.1f;
            m_farInputStill = (inputs == m_farInputSignature && m_farFieldValid) ? m_farInputStill + 1u : 0u;
            m_farInputSignature = inputs;
            // A camera that moves without carrying the clipmaps along (they snap) in a scene whose lights and sky have
            // held for a while (the temporal averages are settled): the far field is traced every other frame; a probe
            // coming into view is one frame late.
            m_farHalfRate = (m_farInputStill >= 24u && placementHeld && m_temporalWeight < 0.2f) ? 1u : 0u;
            if (signature == m_farSignature && calm && m_farFieldValid) {
                ++m_farStill;
            } else {
                m_farStill = 0;
            }
            m_farSignature = signature;
            m_farRebuildsSeen = rebuilds;
        }
        // A real trace now and then keeps the history honest; in between a still far field costs nothing.
        constexpr u32 kFarSettleFrames = 48u;
        constexpr u32 kFarRefreshFrames = 240u;
        // A camera that only turns (the clipmaps stay put) in a calm scene needs the far field every other frame: a
        // probe that comes into view is traced one frame late, and no picture shows the difference.
        const bool halfRateSkip = m_farHalfRate != 0u && m_farTracedLast;
        if (m_farStill >= kFarSettleFrames && (m_farStill % kFarRefreshFrames) != 0u) {
            m_farEnabled = true;
            ++m_stats.farSkipped;
        } else if (halfRateSkip) {
            m_farEnabled = true;
            m_farTracedLast = false;
            ++m_stats.farHalfRate;
        } else {
            m_farTracedLast = true;
            bool voxelized = false;
            {
                CF_GPU_SCOPE(cmd, "Photon::voxelizeScreen");
                voxelized = dispatchVoxelize(cmd, depth, albedo, view, proj, width, height);
            }
            bool injected = false;
            if (voxelized) {
                CF_GPU_SCOPE(cmd, "Photon::inject");
                injected = dispatchInject(cmd, farSky);
            }
            if (injected) {
                CF_GPU_SCOPE(cmd, "Photon::probes");
                m_farEnabled = dispatchProbes(cmd, frameIndex, farSky);
            }
            m_farFieldValid = m_farEnabled;
        }
    }
    m_stats.farEnabled = m_farEnabled ? 1u : 0u;

    // 2) Per-frame constants shared by every cascade.
    const f32 skyIntensity = std::max(settings.skyIntensity, 0.0f);
    const f32 reach = std::max(settings.radius, 0.1f);
    const f32 series = (std::pow(4.0f, static_cast<f32>(cascadeCount)) - 1.0f) / 3.0f;
    // Spec §4.2 reach floor: guarantees t_K ~= reach regardless of depth.
    const f32 uMin = reach / std::max(kKappa * static_cast<f32>(s0) * series, 1e-4f);
    const u32 jitterIndex = frameIndex % 64u + 1u;
    const Vec4 jitter(halton(jitterIndex, 2) - 0.5f, halton(jitterIndex, 3) - 0.5f, 0.0f,
                      0.0f);

    // 3) Cascades top-down: each one consumes M̄_{k+1}.
    CF_GPU_SCOPE(cmd, "Photon::nearField");
    for (i32 k = static_cast<i32>(cascadeCount) - 1; k >= 1; --k) {
        RHI::Texture* upper = m_dummyUpper;
        if (static_cast<u32>(k) + 1u < cascadeCount) {
            upper = m_atlases[static_cast<size_t>(k + 1)];
        }
        // The coarse cascades (2 and up: few probes, wide angular tiles) change slowly: while the scene is calm they are
        // traced on alternate frames and keep their atlas in between (a skipped dispatch does not touch it).
        const bool coarse = static_cast<u32>(k) >= 2u;
        if (coarse && !m_cameraMoved && m_temporalWeight < 0.2f &&
            ((frameIndex + static_cast<u32>(k)) & 1u) != 0u && m_atlasesValid[static_cast<size_t>(k)]) {
            continue;
        }
        static constexpr const char* kCascadeScope[6] = {"Photon::cascade 0", "Photon::cascade 1",
                                                         "Photon::cascade 2", "Photon::cascade 3",
                                                         "Photon::cascade 4", "Photon::cascade 5"};
        CF_GPU_SCOPE(cmd, kCascadeScope[std::min(k, 5)]);
        if (!dispatchCascade(cmd, static_cast<u32>(k), upper, m_atlases[k], view, proj, width,
                             height, settings, uMin, jitter, skyIntensity)) {
            return false;
        }
        m_atlasesValid[static_cast<size_t>(k)] = true;
    }

    // 4) Cascade 0 consumes M̄1 (or the far field when K == 1).
    RHI::Texture* upper0 = cascadeCount > 1 ? m_atlases[1] : m_dummyUpper;
    CF_GPU_SCOPE(cmd, "Photon::cascade 0 (SH)");
    return dispatchC0(cmd, upper0, view, proj, width, height, settings, uMin, jitter,
                      skyIntensity);
}

}  // namespace Caffeine::Render

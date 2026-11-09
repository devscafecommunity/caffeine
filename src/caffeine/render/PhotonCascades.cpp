// ============================================================================
// @file    PhotonCascades.cpp
// @brief   Photon compute path implementation (P3: cascades K-1..0)
// ============================================================================
#include "render/PhotonCascades.hpp"

#include "caffeine/photon/PhotonFarField.hpp"
#include "render/ShaderBytecode.hpp"
#include "rhi/CommandBuffer.hpp"

#include <algorithm>
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

constexpr f32 kKappa = 1.0f;  // spec §4.2 default
constexpr f32 kSkyR = 0.48f;
constexpr f32 kSkyG = 0.60f;
constexpr f32 kSkyB = 0.82f;
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
    ubo.sky = Vec4(kSkyR * skyIntensity, kSkyG * skyIntensity, kSkyB * skyIntensity, 1.0f);
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
    ubo.sky = Vec4(kSkyR * skyIntensity, kSkyG * skyIntensity, kSkyB * skyIntensity, 1.0f);
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
};
static_assert(sizeof(ProbeUBO) == 112, "ProbeUBO must match the GLSL std140 layout");

struct InjectUBO {
    Vec4 clip[4];
    Vec4 probe[4];
    Vec4 params;
    Vec4 ambient;
    Vec4 atlas;
};
static_assert(sizeof(InjectUBO) == 176, "InjectUBO must match the GLSL std140 layout");

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

    cmd->beginComputePass(m_voxelAlbedo);
    cmd->bindComputePipeline(m_voxelizePipeline);
    RHI::Texture* textures[2] = {depth, albedo};
    RHI::Sampler* samplers[2] = {m_point, m_point};
    cmd->bindComputeTextures(textures, samplers, 0, 2);
    cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
    cmd->dispatch(ceilDiv(width * height, 64u), 1u, 1u);
    cmd->endComputePass();
    return true;
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
    const Vec4 jitter(halton(jitterIndex, 2) - 0.5f, halton(jitterIndex, 3) - 0.5f, 0.0f, 0.0f);
    const u32 probes = Photon::FarFieldConfig::kProbesPerAxis *
                       Photon::FarFieldConfig::kProbesPerAxis *
                       Photon::FarFieldConfig::kProbesPerAxis;

    for (u32 level = 0; level < 4; ++level) {
        ProbeUBO ubo{};
        ubo.clip = Vec4(m_farClipOrigin[level].x, m_farClipOrigin[level].y,
                        m_farClipOrigin[level].z, m_farCellSize[level]);
        ubo.probe = Vec4(m_farProbeOrigin[level].x, m_farProbeOrigin[level].y,
                         m_farProbeOrigin[level].z, m_farProbeSpacing[level]);
        const f32 extent = 16.0f * m_farProbeSpacing[level];
        ubo.params = Vec4(extent, 192.0f, static_cast<f32>(frameIndex), 64.0f);
        ubo.sky = Vec4(kSkyR * skyIntensity, kSkyG * skyIntensity, kSkyB * skyIntensity, 1.0f);
        ubo.jitter = jitter;
        ubo.dims[0] = 16;
        ubo.dims[1] = 10;
        ubo.dims[2] = 0;
        ubo.dims[3] = 0;
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
    const f32 ambient = 3.14159265f * 0.5f * skyIntensity;
    ubo.ambient = Vec4(ambient, ambient, ambient, 1.0f);
    ubo.atlas = Vec4(640.0f, 2560.0f, 32.0f, 1024.0f);

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
    m_sValid = sValid;

    // 1) Min-Z pyramid (render passes; leaves no pass active).
    m_minZ.generate(cmd, depth, MinZParams::fromProjection(proj));

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

        if (!m_farCleared) {
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
        }

        const f32 farSky = std::max(settings.skyIntensity, 0.0f);
        if (dispatchVoxelize(cmd, depth, albedo, view, proj, width, height) &&
            dispatchInject(cmd, farSky) &&
            dispatchProbes(cmd, frameIndex, farSky)) {
            m_farEnabled = true;
        }
    }

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
    for (i32 k = static_cast<i32>(cascadeCount) - 1; k >= 1; --k) {
        RHI::Texture* upper = m_dummyUpper;
        if (static_cast<u32>(k) + 1u < cascadeCount) {
            upper = m_atlases[static_cast<size_t>(k + 1)];
        }
        if (!dispatchCascade(cmd, static_cast<u32>(k), upper, m_atlases[k], view, proj, width,
                             height, settings, uMin, jitter, skyIntensity)) {
            return false;
        }
    }

    // 4) Cascade 0 consumes M̄1 (or the far field when K == 1).
    RHI::Texture* upper0 = cascadeCount > 1 ? m_atlases[1] : m_dummyUpper;
    return dispatchC0(cmd, upper0, view, proj, width, height, settings, uMin, jitter,
                      skyIntensity);
}

}  // namespace Caffeine::Render

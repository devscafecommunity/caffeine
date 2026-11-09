// ============================================================================
// @file    PhotonCascades.cpp
// @brief   Photon compute path implementation (P3: cascades K-1..0)
// ============================================================================
#include "render/PhotonCascades.hpp"

#include "render/ShaderBytecode.hpp"
#include "rhi/CommandBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Caffeine::Render {
namespace {

/// std140 layout shared by photon_cascade.comp and photon_c0.comp.
/// Keep all three in sync; the static_assert catches CPU-side drift.
struct PhotonCascadeUBO {
    Vec4 proj;      // P00, P11, P22, P23
    Vec4 extra;     // P32, sky depth, K, k (cascade) / hasUpper (C0)
    Vec4 texel;     // 1/W, 1/H, W, H
    Mat4 view;      // world -> view
    Vec4 params;    // s0, reach, steps, thicknessRel
    Vec4 params2;   // kappa, uMin, skyIntensity, tile side
    Vec4 sky;       // rgb sky radiance
    Vec4 jitter;    // Halton(2,3) - 0.5
    i32  grid[4];   // G_k.x, G_k.y, atlasW, atlasH
};
static_assert(sizeof(PhotonCascadeUBO) == 192,
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
            desc.numSamplers = 6;
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
            desc.numSamplers = 6;
            desc.numReadwriteStorageTextures = 1;
            desc.numUniformBuffers = 1;
            m_c0Pipeline = device->createComputePipeline(desc);
        }
    }
    if (!m_point) {
        RHI::SamplerDesc pointDesc;
        pointDesc.linearFilter = false;
        pointDesc.clampToEdge = true;
        m_point = device->createSampler(pointDesc);
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
    releaseTargets(target);
    if (m_cascadePipeline) {
        target->destroyComputePipeline(m_cascadePipeline);
        m_cascadePipeline = nullptr;
    }
    if (m_c0Pipeline) {
        target->destroyComputePipeline(m_c0Pipeline);
        m_c0Pipeline = nullptr;
    }
    if (m_point) {
        target->destroySampler(m_point);
        m_point = nullptr;
    }
    m_device = nullptr;
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

    cmd->beginComputePass(target);
    cmd->bindComputePipeline(m_cascadePipeline);
    RHI::Texture* textures[6] = {m_direct, m_depth, m_albedo, m_normalMaterial, upper,
                                 m_minZ.texture()};
    RHI::Sampler* samplers[6] = {m_point, m_point, m_point, m_point, m_point, m_point};
    cmd->bindComputeTextures(textures, samplers, 0, 6);
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

    cmd->beginComputePass(m_shAtlas);
    cmd->bindComputePipeline(m_c0Pipeline);
    RHI::Texture* textures[6] = {m_direct, m_depth, m_albedo, m_normalMaterial, upper,
                                 m_minZ.texture()};
    RHI::Sampler* samplers[6] = {m_point, m_point, m_point, m_point, m_point, m_point};
    cmd->bindComputeTextures(textures, samplers, 0, 6);
    cmd->pushComputeUniformData(0, &ubo, sizeof(ubo));
    cmd->dispatch(ceilDiv(m_gridW, 8u), ceilDiv(m_gridH, 8u), 1u);
    cmd->endComputePass();
    return true;
}

bool PhotonCascades::execute(RHI::CommandBuffer* cmd, RHI::Texture* depth, RHI::Texture* albedo,
                             RHI::Texture* normalMaterial, RHI::Texture* direct,
                             const Mat4& view, const Mat4& proj, u32 width, u32 height,
                             const ECS::PhotonGIComponent& settings, u32 frameIndex) {
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

    // 1) Min-Z pyramid (render passes; leaves no pass active).
    m_minZ.generate(cmd, depth, MinZParams::fromProjection(proj));

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

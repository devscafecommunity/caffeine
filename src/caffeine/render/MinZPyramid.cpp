// ============================================================================
// @file    MinZPyramid.cpp
// @brief   Photon near-field Min-Z pyramid implementation
// ============================================================================
#include "render/MinZPyramid.hpp"

#include "render/ShaderBytecode.hpp"
#include "rhi/CommandBuffer.hpp"

#include <algorithm>

namespace Caffeine::Render {
namespace {

// std140: three vec4s. Must match MinZUBO in minz_downsample.frag.
struct MinZUBO {
    f32 proj[4];
    f32 projExtra[4];
    f32 params[4];
};

constexpr f32 kDepthMode  = 0.0f;
constexpr f32 kReduceMode = 1.0f;

}  // namespace

MinZParams MinZParams::fromProjection(const Mat4& projection) {
    MinZParams params;
    params.proj[0] = projection(0, 0);
    params.proj[1] = projection(1, 1);
    params.proj[2] = projection(2, 2);
    params.proj[3] = projection(2, 3);
    params.proj32  = projection(3, 2);
    return params;
}

u32 MinZPyramid::mipCountFor(u32 width, u32 height) {
    if (width == 0 || height == 0) {
        return 0;
    }
    const u32 largest = std::max(width, height);
    u32 levels = 1;
    for (u32 size = largest; size > 1; size >>= 1) {
        ++levels;
    }
    return levels;
}

bool MinZPyramid::init(RHI::RenderDevice* device, u32 width, u32 height) {
    if (!device || !device->isInitialized() || width == 0 || height == 0) {
        return false;
    }
    if (m_device == device && m_width == width && m_height == height && isReady()) {
        return true;
    }

    if (m_device && m_device != device) {
        releaseResources(m_device);
    } else if (m_device) {
        releaseResources(m_device);
    }
    m_device = device;

    m_width  = width;
    m_height = height;
    m_mips   = mipCountFor(width, height);

    RHI::TextureDesc minZDesc;
    minZDesc.width     = width;
    minZDesc.height    = height;
    minZDesc.format    = RHI::TextureFormat::R32_FLOAT;
    minZDesc.usage     = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    minZDesc.mipLevels = m_mips;
    minZDesc.type      = RHI::TextureType::Texture2D;
    m_minZ = device->createTexture(minZDesc);

    RHI::TextureDesc scratchDesc;
    scratchDesc.width     = width;
    scratchDesc.height    = height;
    scratchDesc.format    = RHI::TextureFormat::R32_FLOAT;
    scratchDesc.usage     = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    scratchDesc.mipLevels = 1;
    scratchDesc.type      = RHI::TextureType::Texture2D;
    m_scratch = device->createTexture(scratchDesc);

    RHI::SamplerDesc samplerDesc;
    samplerDesc.linearFilter = false;  // exact 2×2 taps
    samplerDesc.clampToEdge  = true;
    m_sampler = device->createSampler(samplerDesc);

    RHI::Shader* vertex =
        createBuiltinShader(device, BuiltinShader::FullscreenVertex, RHI::ShaderStage::Vertex, 0);
    RHI::Shader* fragment =
        createBuiltinShader(device, BuiltinShader::MinZDownsampleFragment,
                            RHI::ShaderStage::Fragment, 1, 1);

    if (m_minZ && m_scratch && m_sampler && vertex && fragment) {
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.colorFormat  = RHI::TextureFormat::R32_FLOAT;
        pipelineDesc.depthTest    = false;
        pipelineDesc.depthWrite   = false;
        pipelineDesc.blendMode    = RHI::BlendMode::None;
        m_pipeline = device->createGraphicsPipeline(vertex, fragment, pipelineDesc);
    }

    if (vertex) {
        device->destroyShader(vertex);
    }
    if (fragment) {
        device->destroyShader(fragment);
    }

    if (!isReady()) {
        releaseResources(device);
        m_width = 0;
        m_height = 0;
        m_mips = 0;
        return false;
    }
    return true;
}

void MinZPyramid::releaseResources(RHI::RenderDevice* device) {
    if (!device) {
        return;
    }
    if (m_pipeline) {
        device->destroyPipeline(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_sampler) {
        device->destroySampler(m_sampler);
        m_sampler = nullptr;
    }
    if (m_minZ) {
        device->destroyTexture(m_minZ);
        m_minZ = nullptr;
    }
    if (m_scratch) {
        device->destroyTexture(m_scratch);
        m_scratch = nullptr;
    }
}

void MinZPyramid::shutdown(RHI::RenderDevice* device) {
    releaseResources(device ? device : m_device);
    m_device = nullptr;
    m_width = 0;
    m_height = 0;
    m_mips = 0;
}

void MinZPyramid::generate(RHI::CommandBuffer* cmd, RHI::Texture* sceneDepth,
                           const MinZParams& params) {
    if (!cmd || !sceneDepth || !sceneDepth->handle || !isReady() || m_mips == 0) {
        return;
    }

    MinZUBO ubo{};
    for (u32 i = 0; i < 4; ++i) {
        ubo.proj[i] = params.proj[i];
        ubo.projExtra[i] = 0.0f;
    }
    ubo.projExtra[0] = params.proj32;

    // Level 0: scene depth -> linear view depth.
    {
        RHI::RenderPassDesc pass;
        pass.colorTarget    = m_minZ;
        pass.colorMipLevel  = 0;
        pass.clearColor[0]  = params.skyDepth;
        pass.clearColor[1]  = 0.0f;
        pass.clearColor[2]  = 0.0f;
        pass.clearColor[3]  = 1.0f;

        ubo.params[0] = kDepthMode;
        ubo.params[1] = params.skyDepth;

        cmd->beginRenderPass(pass);
        cmd->bindPipeline(m_pipeline);
        cmd->bindTexture(sceneDepth, 0, m_sampler);
        cmd->setViewport(0.0f, 0.0f, static_cast<f32>(m_width), static_cast<f32>(m_height));
        cmd->setScissor(0, 0, m_width, m_height);
        cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &ubo, sizeof(ubo));
        cmd->draw(3);
        cmd->endRenderPass();
    }

    // Levels 1..N-1: stage the previous level in the scratch texture, then take
    // the exact 2×2 minimum. The copy keeps the two subresources disjoint.
    ubo.params[0] = kReduceMode;
    for (u32 mip = 1; mip < m_mips; ++mip) {
        const u32 srcW = std::max(1u, m_width >> (mip - 1));
        const u32 srcH = std::max(1u, m_height >> (mip - 1));
        const u32 dstW = std::max(1u, m_width >> mip);
        const u32 dstH = std::max(1u, m_height >> mip);

        cmd->copyTextureRegion(m_minZ, mip - 1, m_scratch, 0, srcW, srcH);

        RHI::RenderPassDesc pass;
        pass.colorTarget   = m_minZ;
        pass.colorMipLevel = mip;
        pass.clearColor[0] = 1.0e30f;
        pass.clearColor[1] = 0.0f;
        pass.clearColor[2] = 0.0f;
        pass.clearColor[3] = 1.0f;

        cmd->beginRenderPass(pass);
        cmd->bindPipeline(m_pipeline);
        cmd->bindTexture(m_scratch, 0, m_sampler);
        cmd->setViewport(0.0f, 0.0f, static_cast<f32>(dstW), static_cast<f32>(dstH));
        cmd->setScissor(0, 0, dstW, dstH);
        cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &ubo, sizeof(ubo));
        cmd->draw(3);
        cmd->endRenderPass();
    }
}

}  // namespace Caffeine::Render

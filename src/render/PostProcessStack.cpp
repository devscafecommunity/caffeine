#include "render/PostProcessStack.hpp"

#include "debug/Profiler.hpp"
#include "render/ShaderBytecode.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/RenderDevice.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Caffeine::Render {
namespace {

constexpr u32 kLuminanceSize = 256;
constexpr u32 kMaxBloomLevels = 6;
constexpr u32 kMaxViews = 4;

struct Vec4f {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

struct AdaptUBO {
    Vec4f params;
};

struct BloomUBO {
    Vec4f texel;
    Vec4f params;
};

struct SsaoUBO {
    Vec4f proj;
    Vec4f texel;
    Vec4f params;
};

struct BlurUBO {
    Vec4f texel;
};

struct DofUBO {
    Vec4f proj;
    Vec4f texel;
    Vec4f params;
};

struct MotionUBO {
    float invViewProj[16];
    float prevViewProj[16];
    Vec4f params;
};

struct TaaUBO {
    float invViewProj[16];
    float prevViewProj[16];
    Vec4f texel;
    Vec4f params;
};

struct CompositeUBO {
    float invViewProj[16];
    Vec4f camera;
    Vec4f proj;
    Vec4f texel;
    Vec4f exposure;
    Vec4f exposureRange;
    Vec4f grading;
    Vec4f bloom;
    Vec4f fog;
    Vec4f fogColor;
    Vec4f ao;
    Vec4f lens;
    Vec4f grain;
    Vec4f flags;
};

struct FxaaUBO {
    Vec4f texel;
    Vec4f params;
};

f32 halton(u32 index, u32 base) {
    f32 f = 1.0f;
    f32 r = 0.0f;
    while (index > 0) {
        f /= static_cast<f32>(base);
        r += f * static_cast<f32>(index % base);
        index /= base;
    }
    return r;
}

f32 srgbToLinear(f32 c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

Vec4f projParams(const Mat4& proj) {
    return {proj(0, 0), proj(1, 1), proj(2, 2), proj(2, 3)};
}

}  // namespace

PostProcessStack::~PostProcessStack() {
    shutdown();
}

ECS::PostProcessComponent PostProcessStack::defaults() {
    ECS::PostProcessComponent settings;
    settings.colorGrading.enabled = true;
    settings.antiAliasing.enabled = true;
    settings.antiAliasing.mode = 1;
    settings.ambientOcclusion.enabled = true;
    settings.ambientOcclusion.intensity = 0.85f;
    settings.ambientOcclusion.radius = 0.45f;
    settings.ambientOcclusion.bias = 0.02f;
    return settings;
}

Vec2 PostProcessStack::taaJitter(u32 frameIndex) {
    const u32 i = (frameIndex % 8u) + 1u;
    return Vec2(halton(i, 2) - 0.5f, halton(i, 3) - 0.5f);
}

bool PostProcessStack::usesTaa(const ECS::PostProcessComponent& settings) {
    return settings.enabled && settings.antiAliasing.enabled && settings.antiAliasing.mode == 1;
}

bool PostProcessStack::isTemporal(const ECS::PostProcessComponent& settings) {
    if (!settings.enabled) return false;
    return usesTaa(settings) || settings.autoExposure.enabled ||
           (settings.grain.enabled && settings.grain.intensity > 0.0f);
}

bool PostProcessStack::init(RHI::RenderDevice* device) {
    shutdown();
    if (!device || !device->isInitialized()) return false;
    m_device = device;

    m_vertex = createBuiltinShader(device, BuiltinShader::FullscreenVertex, RHI::ShaderStage::Vertex, 0);
    if (!m_vertex) {
        shutdown();
        return false;
    }
    auto frag = [&](BuiltinShader shader, u32 samplers) {
        RHI::Shader* s =
            createBuiltinShader(device, shader, RHI::ShaderStage::Fragment, 1, samplers);
        if (s) m_shaders.push_back(s);
        return s;
    };
    constexpr u32 kR16F = static_cast<u32>(RHI::TextureFormat::R16_FLOAT);
    constexpr u32 kHdr = static_cast<u32>(RHI::TextureFormat::R16G16B16A16_FLOAT);
    constexpr u32 kLdr = static_cast<u32>(RHI::TextureFormat::R8G8B8A8_UNORM);

    m_luminance = createPass(frag(BuiltinShader::PostLuminanceFragment, 1), kR16F, false);
    m_adapt = createPass(frag(BuiltinShader::PostAdaptFragment, 2), kR16F, false);
    m_bloomPrefilter = createPass(frag(BuiltinShader::PostBloomPrefilterFragment, 1), kHdr, false);
    RHI::Shader* down = frag(BuiltinShader::PostBloomDownFragment, 1);
    m_bloomDown = createPass(down, kHdr, false);
    m_bloomUp = createPass(frag(BuiltinShader::PostBloomUpFragment, 1), kHdr, true);
    m_ssao = createPass(frag(BuiltinShader::PostSsaoFragment, 1), kHdr, false);
    m_ssaoBlur = createPass(frag(BuiltinShader::PostSsaoBlurFragment, 1), kHdr, false);
    m_dof = createPass(frag(BuiltinShader::PostDofFragment, 2), kHdr, false);
    m_motionBlur = createPass(frag(BuiltinShader::PostMotionBlurFragment, 2), kHdr, false);
    m_taa = createPass(frag(BuiltinShader::PostTaaFragment, 3), kHdr, false);
    m_composite = createPass(frag(BuiltinShader::PostCompositeFragment, 5), kLdr, false);
    m_fxaa = createPass(frag(BuiltinShader::PostFxaaFragment, 1), kLdr, false);

    RHI::SamplerDesc linear;
    linear.linearFilter = true;
    linear.clampToEdge = true;
    m_linear = device->createSampler(linear);
    RHI::SamplerDesc point;
    point.linearFilter = false;
    point.clampToEdge = true;
    m_point = device->createSampler(point);

    m_ready = m_composite && m_linear && m_point;
    return m_ready;
}

void PostProcessStack::shutdown() {
    if (m_device) {
        for (ViewResources& view : m_views) releaseView(view);
        RHI::Pipeline* pipelines[] = {m_luminance, m_adapt, m_bloomPrefilter, m_bloomDown,
                                      m_bloomUp, m_ssao, m_ssaoBlur, m_dof, m_motionBlur,
                                      m_taa, m_composite, m_fxaa};
        for (RHI::Pipeline* p : pipelines) {
            if (p) m_device->destroyPipeline(p);
        }
        for (RHI::Shader* s : m_shaders) m_device->destroyShader(s);
        if (m_vertex) m_device->destroyShader(m_vertex);
        if (m_linear) m_device->destroySampler(m_linear);
        if (m_point) m_device->destroySampler(m_point);
    }
    m_views.clear();
    m_shaders.clear();
    m_vertex = nullptr;
    m_luminance = m_adapt = m_bloomPrefilter = m_bloomDown = m_bloomUp = nullptr;
    m_ssao = m_ssaoBlur = m_dof = m_motionBlur = m_taa = m_composite = m_fxaa = nullptr;
    m_linear = nullptr;
    m_point = nullptr;
    m_device = nullptr;
    m_ready = false;
}

RHI::Pipeline* PostProcessStack::createPass(RHI::Shader* fragment, u32 colorFormat, bool additive) {
    if (!m_device || !m_vertex || !fragment) return nullptr;
    RHI::GraphicsPipelineDesc desc{};
    desc.colorFormat = static_cast<RHI::TextureFormat>(colorFormat);
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.blendMode = additive ? RHI::BlendMode::Additive : RHI::BlendMode::None;
    return m_device->createGraphicsPipeline(m_vertex, fragment, desc);
}

RHI::Texture* PostProcessStack::createTarget(u32 width, u32 height, u32 format, u32 mipLevels) {
    RHI::TextureDesc desc;
    desc.width = std::max(width, 1u);
    desc.height = std::max(height, 1u);
    desc.format = static_cast<RHI::TextureFormat>(format);
    desc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    desc.mipLevels = std::max(mipLevels, 1u);
    return m_device->createTexture(desc);
}

void PostProcessStack::releaseView(ViewResources& view) {
    if (!m_device) return;
    auto drop = [&](RHI::Texture*& t) {
        if (t) m_device->destroyTexture(t);
        t = nullptr;
    };
    drop(view.history[0]);
    drop(view.history[1]);
    drop(view.temp[0]);
    drop(view.temp[1]);
    drop(view.ao);
    drop(view.aoBlur);
    drop(view.luminance);
    drop(view.adapted[0]);
    drop(view.adapted[1]);
    drop(view.ldr);
    for (RHI::Texture*& t : view.bloom) drop(t);
    view.bloom.clear();
    view.bloomWidths.clear();
    view.bloomHeights.clear();
    view.historyValid = false;
    view.adaptedValid = false;
}

PostProcessStack::ViewResources* PostProcessStack::acquireView(const PostProcessInputs& inputs) {
    ++m_frameCounter;
    for (ViewResources& view : m_views) {
        if (view.viewId != inputs.viewId) continue;
        if (view.width != inputs.sceneWidth || view.height != inputs.sceneHeight ||
            view.outWidth != inputs.outputWidth || view.outHeight != inputs.outputHeight) {
            releaseView(view);
            view.width = inputs.sceneWidth;
            view.height = inputs.sceneHeight;
            view.outWidth = inputs.outputWidth;
            view.outHeight = inputs.outputHeight;
        }
        view.lastUsed = m_frameCounter;
        return &view;
    }
    if (m_views.size() >= kMaxViews) {
        auto oldest = std::min_element(m_views.begin(), m_views.end(),
                                       [](const ViewResources& a, const ViewResources& b) {
                                           return a.lastUsed < b.lastUsed;
                                       });
        releaseView(*oldest);
        m_views.erase(oldest);
    }
    ViewResources view;
    view.viewId = inputs.viewId;
    view.width = inputs.sceneWidth;
    view.height = inputs.sceneHeight;
    view.outWidth = inputs.outputWidth;
    view.outHeight = inputs.outputHeight;
    view.lastUsed = m_frameCounter;
    m_views.push_back(view);
    return &m_views.back();
}

void PostProcessStack::runPass(RHI::CommandBuffer* cmd, RHI::Pipeline* pipeline,
                               RHI::Texture* target, u32 width, u32 height,
                               const PassTexture* textures, u32 textureCount, const void* ubo,
                               u32 uboSize, bool loadColor) {
    if (!pipeline || !target) return;
    RHI::RenderPassDesc pass;
    pass.colorTarget = target;
    pass.loadColor = loadColor;
    pass.cycle = !loadColor;
    pass.clearColor[0] = pass.clearColor[1] = pass.clearColor[2] = 0.0f;
    pass.clearColor[3] = 1.0f;
    cmd->beginRenderPass(pass);
    cmd->bindPipeline(pipeline);
    cmd->setViewport(0.0f, 0.0f, static_cast<f32>(width), static_cast<f32>(height));
    for (u32 i = 0; i < textureCount; ++i) {
        cmd->bindTexture(textures[i].texture, i, textures[i].point ? m_point : m_linear);
    }
    if (ubo && uboSize > 0) {
        cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, ubo, uboSize);
    }
    cmd->draw(3);
    cmd->endRenderPass();
}

void PostProcessStack::execute(RHI::CommandBuffer* cmd, const ECS::PostProcessComponent& in,
                               const PostProcessInputs& inputs) {
    if (!m_ready || !cmd || !inputs.sceneColor || !inputs.sceneDepth || !inputs.output ||
        inputs.sceneWidth == 0 || inputs.sceneHeight == 0 || inputs.outputWidth == 0 ||
        inputs.outputHeight == 0) {
        return;
    }
    CF_PROFILE_SCOPE("PostProcessStack::execute");
    ViewResources* view = acquireView(inputs);
    if (!view) return;

    const bool on = in.enabled;
    const u32 w = inputs.sceneWidth;
    const u32 h = inputs.sceneHeight;
    constexpr u32 kHdr = static_cast<u32>(RHI::TextureFormat::R16G16B16A16_FLOAT);
    constexpr u32 kR16F = static_cast<u32>(RHI::TextureFormat::R16_FLOAT);
    constexpr u32 kLdr = static_cast<u32>(RHI::TextureFormat::R8G8B8A8_UNORM);
    const Vec4f texel{1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h),
                      static_cast<f32>(inputs.frameIndex % 64u), 0.0f};
    const Mat4 invViewProj = inputs.viewProj.inverted();

    RHI::Texture* source = inputs.sceneColor;
    auto ensure = [&](RHI::Texture*& t, u32 tw, u32 th, u32 format) {
        if (!t) t = createTarget(tw, th, format);
        return t != nullptr;
    };

    // Temporal AA resolves the jittered scene against last frame's history.
    if (on && usesTaa(in) && m_taa && ensure(view->history[0], w, h, kHdr) &&
        ensure(view->history[1], w, h, kHdr)) {
        CF_PROFILE_SCOPE("Post::taa");
        const u32 writeIndex = view->historyIndex ^ 1u;
        TaaUBO ubo{};
        std::memcpy(ubo.invViewProj, invViewProj.data(), sizeof(ubo.invViewProj));
        std::memcpy(ubo.prevViewProj, inputs.prevViewProj.data(), sizeof(ubo.prevViewProj));
        ubo.texel = texel;
        ubo.params = {(view->historyValid && inputs.historyValid) ? 1.0f : 0.0f, 0.9f, 0.0f, 0.0f};
        const PassTexture tex[3] = {{source, false}, {view->history[view->historyIndex], false},
                                    {inputs.sceneDepth, true}};
        runPass(cmd, m_taa, view->history[writeIndex], w, h, tex, 3, &ubo, sizeof(ubo));
        view->historyIndex = writeIndex;
        view->historyValid = true;
        source = view->history[writeIndex];
    } else {
        view->historyValid = false;
    }

    u32 tempIndex = 0;
    auto nextTemp = [&]() -> RHI::Texture* {
        RHI::Texture*& t = view->temp[tempIndex];
        tempIndex ^= 1u;
        ensure(t, w, h, kHdr);
        return t;
    };

    if (on && in.motionBlur.enabled && in.motionBlur.intensity > 0.0f && m_motionBlur &&
        inputs.historyValid) {
        CF_PROFILE_SCOPE("Post::motionBlur");
        RHI::Texture* target = nextTemp();
        MotionUBO ubo{};
        std::memcpy(ubo.invViewProj, invViewProj.data(), sizeof(ubo.invViewProj));
        std::memcpy(ubo.prevViewProj, inputs.prevViewProj.data(), sizeof(ubo.prevViewProj));
        ubo.params = {std::clamp(in.motionBlur.intensity, 0.0f, 1.0f), 12.0f,
                      std::clamp(in.motionBlur.maxVelocity, 0.0f, 4.0f) * 0.05f, 0.0f};
        const PassTexture tex[2] = {{source, false}, {inputs.sceneDepth, true}};
        runPass(cmd, m_motionBlur, target, w, h, tex, 2, &ubo, sizeof(ubo));
        source = target;
    }

    if (on && in.depthOfField.enabled && in.depthOfField.maxBlur > 0.0f && m_dof) {
        CF_PROFILE_SCOPE("Post::depthOfField");
        RHI::Texture* target = nextTemp();
        DofUBO ubo{};
        ubo.proj = projParams(inputs.proj);
        ubo.texel = texel;
        ubo.params = {in.depthOfField.focusDistance, in.depthOfField.aperture,
                      in.depthOfField.focalLength,
                      std::clamp(in.depthOfField.maxBlur, 0.0f, 2.0f) * 12.0f *
                          (static_cast<f32>(h) / 1080.0f)};
        const PassTexture tex[2] = {{source, false}, {inputs.sceneDepth, true}};
        runPass(cmd, m_dof, target, w, h, tex, 2, &ubo, sizeof(ubo));
        source = target;
    }

    RHI::Texture* aoTexture = nullptr;
    if (on && in.ambientOcclusion.enabled && in.ambientOcclusion.intensity > 0.0f && m_ssao &&
        m_ssaoBlur && ensure(view->ao, w, h, kHdr) && ensure(view->aoBlur, w, h, kHdr)) {
        CF_PROFILE_SCOPE("Post::ssao");
        SsaoUBO ubo{};
        ubo.proj = projParams(inputs.proj);
        ubo.texel = texel;
        ubo.params = {std::max(in.ambientOcclusion.radius, 0.05f),
                      std::max(in.ambientOcclusion.bias, 0.0f),
                      std::clamp(in.ambientOcclusion.intensity, 0.0f, 4.0f), 16.0f};
        const PassTexture depthTex[1] = {{inputs.sceneDepth, true}};
        runPass(cmd, m_ssao, view->ao, w, h, depthTex, 1, &ubo, sizeof(ubo));
        BlurUBO blur{};
        blur.texel = texel;
        const PassTexture aoTex[1] = {{view->ao, true}};
        runPass(cmd, m_ssaoBlur, view->aoBlur, w, h, aoTex, 1, &blur, sizeof(blur));
        aoTexture = view->aoBlur;
    }

    RHI::Texture* adaptedTexture = nullptr;
    if (on && in.autoExposure.enabled && m_luminance && m_adapt) {
        CF_PROFILE_SCOPE("Post::autoExposure");
        if (!view->luminance) {
            view->luminanceMips = 1;
            for (u32 s = kLuminanceSize; s > 1; s >>= 1) ++view->luminanceMips;
            view->luminance = createTarget(kLuminanceSize, kLuminanceSize, kR16F, view->luminanceMips);
        }
        ensure(view->adapted[0], 1, 1, kR16F);
        ensure(view->adapted[1], 1, 1, kR16F);
        if (view->luminance && view->adapted[0] && view->adapted[1]) {
            const PassTexture lumIn[1] = {{source, false}};
            runPass(cmd, m_luminance, view->luminance, kLuminanceSize, kLuminanceSize, lumIn, 1,
                    nullptr, 0);
            cmd->generateMipmaps(view->luminance);
            const f32 speed = std::max(in.autoExposure.adaptationSpeed, 0.01f);
            const f32 blend = view->adaptedValid
                ? 1.0f - std::exp(-std::clamp(inputs.deltaTime, 0.0f, 0.25f) * speed)
                : 1.0f;
            AdaptUBO ubo{};
            ubo.params = {blend, static_cast<f32>(view->luminanceMips - 1), -12.0f, 12.0f};
            const u32 writeIndex = view->adaptedIndex ^ 1u;
            const PassTexture adaptIn[2] = {{view->luminance, false},
                                            {view->adapted[view->adaptedIndex], true}};
            runPass(cmd, m_adapt, view->adapted[writeIndex], 1, 1, adaptIn, 2, &ubo, sizeof(ubo));
            view->adaptedIndex = writeIndex;
            view->adaptedValid = true;
            adaptedTexture = view->adapted[writeIndex];
        }
    } else {
        view->adaptedValid = false;
    }

    RHI::Texture* bloomTexture = nullptr;
    if (on && in.bloom.enabled && in.bloom.intensity > 0.0f && m_bloomPrefilter && m_bloomDown &&
        m_bloomUp) {
        CF_PROFILE_SCOPE("Post::bloom");
        if (view->bloom.empty()) {
            u32 bw = std::max(w / 2, 1u);
            u32 bh = std::max(h / 2, 1u);
            for (u32 level = 0; level < kMaxBloomLevels; ++level) {
                RHI::Texture* t = createTarget(bw, bh, kHdr);
                if (!t) break;
                view->bloom.push_back(t);
                view->bloomWidths.push_back(bw);
                view->bloomHeights.push_back(bh);
                if (bw <= 8 || bh <= 8) break;
                bw = std::max(bw / 2, 1u);
                bh = std::max(bh / 2, 1u);
            }
        }
        if (!view->bloom.empty()) {
            BloomUBO ubo{};
            ubo.texel = {1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h), 0.0f, 0.0f};
            ubo.params = {std::max(in.bloom.threshold, 0.0f), 0.5f, 64.0f, 0.0f};
            const PassTexture first[1] = {{source, false}};
            runPass(cmd, m_bloomPrefilter, view->bloom[0], view->bloomWidths[0],
                    view->bloomHeights[0], first, 1, &ubo, sizeof(ubo));
            for (size_t i = 1; i < view->bloom.size(); ++i) {
                ubo.texel = {1.0f / static_cast<f32>(view->bloomWidths[i - 1]),
                             1.0f / static_cast<f32>(view->bloomHeights[i - 1]), 0.0f, 0.0f};
                const PassTexture down[1] = {{view->bloom[i - 1], false}};
                runPass(cmd, m_bloomDown, view->bloom[i], view->bloomWidths[i],
                        view->bloomHeights[i], down, 1, &ubo, sizeof(ubo));
            }
            const f32 scatter = std::clamp(in.bloom.scatter, 0.0f, 1.0f);
            for (size_t i = view->bloom.size() - 1; i > 0; --i) {
                ubo.texel = {1.0f / static_cast<f32>(view->bloomWidths[i]),
                             1.0f / static_cast<f32>(view->bloomHeights[i]), 0.0f, 0.0f};
                ubo.params = {1.0f, scatter, 0.0f, 0.0f};
                const PassTexture up[1] = {{view->bloom[i], false}};
                runPass(cmd, m_bloomUp, view->bloom[i - 1], view->bloomWidths[i - 1],
                        view->bloomHeights[i - 1], up, 1, &ubo, sizeof(ubo), true);
            }
            bloomTexture = view->bloom[0];
        }
    }

    const bool fxaa = on && in.antiAliasing.enabled && in.antiAliasing.mode == 0 && m_fxaa &&
                      ensure(view->ldr, inputs.outputWidth, inputs.outputHeight, kLdr);
    RHI::Texture* compositeTarget = fxaa ? view->ldr : inputs.output;

    {
        CF_PROFILE_SCOPE("Post::composite");
        CompositeUBO ubo{};
        std::memcpy(ubo.invViewProj, invViewProj.data(), sizeof(ubo.invViewProj));
        ubo.camera = {inputs.cameraPos.x, inputs.cameraPos.y, inputs.cameraPos.z, inputs.time};
        ubo.proj = projParams(inputs.proj);
        ubo.texel = {1.0f / static_cast<f32>(w), 1.0f / static_cast<f32>(h),
                     1.0f / static_cast<f32>(inputs.outputWidth),
                     1.0f / static_cast<f32>(inputs.outputHeight)};
        const bool grading = on && in.colorGrading.enabled;
        ubo.exposure = {grading ? std::max(in.colorGrading.exposure, 0.0f) : 1.0f,
                        adaptedTexture ? 1.0f : 0.0f,
                        adaptedTexture ? in.autoExposure.compensation : 0.0f,
                        static_cast<f32>(inputs.frameIndex)};
        ubo.exposureRange = {std::max(in.autoExposure.minExposure, 0.001f),
                             std::max(in.autoExposure.maxExposure, in.autoExposure.minExposure), 0.0f,
                             0.0f};
        ubo.grading = {in.colorGrading.contrast, in.colorGrading.saturation,
                       in.colorGrading.temperature, in.colorGrading.tint};
        ubo.bloom = {bloomTexture ? in.bloom.intensity : 0.0f, 0.0f, 0.0f, 0.0f};
        const bool fog = on && in.deferredFog.enabled;
        ubo.fog = {std::max(in.deferredFog.density, 0.0f), std::max(in.deferredFog.start, 0.0f),
                   std::max(in.deferredFog.end, in.deferredFog.start + 0.01f), fog ? 1.0f : 0.0f};
        ubo.fogColor = {srgbToLinear(in.deferredFog.colorR), srgbToLinear(in.deferredFog.colorG),
                        srgbToLinear(in.deferredFog.colorB), 1.0f};
        ubo.ao = {aoTexture ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        ubo.lens = {(on && in.chromaticAberration.enabled) ? in.chromaticAberration.intensity : 0.0f,
                    (on && in.lensDistortion.enabled) ? in.lensDistortion.intensity : 0.0f,
                    (on && in.vignette.enabled) ? in.vignette.intensity : 0.0f,
                    in.vignette.smoothness};
        ubo.grain = {(on && in.grain.enabled) ? in.grain.intensity : 0.0f,
                     std::max(in.grain.size, 0.5f), 0.0f, 0.0f};
        const f32 sharpen = (on && usesTaa(in)) ? std::clamp(in.antiAliasing.sharpness, 0.0f, 1.0f) : 0.0f;
        ubo.flags = {fxaa ? 1.0f : 0.0f, grading ? 1.0f : 0.0f, sharpen, 0.0f};

        const PassTexture tex[5] = {{source, false},
                                    {bloomTexture ? bloomTexture : source, false},
                                    {inputs.sceneDepth, true},
                                    {aoTexture ? aoTexture : source, false},
                                    {adaptedTexture ? adaptedTexture : source, true}};
        runPass(cmd, m_composite, compositeTarget, inputs.outputWidth, inputs.outputHeight, tex, 5,
                &ubo, sizeof(ubo));
    }

    if (fxaa) {
        CF_PROFILE_SCOPE("Post::fxaa");
        FxaaUBO ubo{};
        ubo.texel = {1.0f / static_cast<f32>(inputs.outputWidth),
                     1.0f / static_cast<f32>(inputs.outputHeight), 0.0f, 0.0f};
        ubo.params = {0.75f, 0.125f, 0.0312f, 0.0f};
        const PassTexture tex[1] = {{view->ldr, false}};
        runPass(cmd, m_fxaa, inputs.output, inputs.outputWidth, inputs.outputHeight, tex, 1, &ubo,
                sizeof(ubo));
    }
}

}  // namespace Caffeine::Render

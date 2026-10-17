#pragma once

#include "core/Types.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "math/Mat4.hpp"
#include "math/Vec2.hpp"
#include "math/Vec3.hpp"

#include <vector>

namespace Caffeine::RHI {
class RenderDevice;
class CommandBuffer;
struct Texture;
struct Pipeline;
struct Sampler;
struct Shader;
}  // namespace Caffeine::RHI

namespace Caffeine::Render {

/// What the scene pass produced for one view, plus where the display image goes.
struct PostProcessInputs {
    RHI::Texture* sceneColor = nullptr;  ///< RGBA16F linear HDR
    RHI::Texture* sceneDepth = nullptr;  ///< D32, created with Sampler usage
    u32 sceneWidth = 0;
    u32 sceneHeight = 0;
    RHI::Texture* output = nullptr;      ///< RGBA8 display target
    u32 outputWidth = 0;
    u32 outputHeight = 0;
    Mat4 proj;                           ///< projection used for the scene (P22/P23 linearise depth)
    Mat4 viewProj;                       ///< as rendered (jittered when TAA is on)
    Mat4 prevViewProj;                   ///< last frame of the same view, unjittered
    Vec3 cameraPos;
    f32 deltaTime = 1.0f / 60.0f;
    f32 time = 0.0f;
    u32 frameIndex = 0;
    bool historyValid = false;           ///< false after a camera cut or resize
    Vec2 jitterPixels;                   ///< TAA sub-pixel offset applied to `proj`
    u64 viewId = 0;                      ///< separates TAA / eye-adaptation history per view
};

/// GPU post-processing driven by ECS::PostProcessComponent: TAA, motion blur, depth of field,
/// SSAO, auto exposure, bloom, fog, colour grading, ACES tone mapping, lens effects, grain, FXAA.
/// Screen-space reflections run in the forward scene shader (it needs material data); the
/// stack only reports whether they are requested.
class PostProcessStack {
public:
    PostProcessStack() = default;
    ~PostProcessStack();
    PostProcessStack(const PostProcessStack&) = delete;
    PostProcessStack& operator=(const PostProcessStack&) = delete;

    bool init(RHI::RenderDevice* device);
    void shutdown();
    bool ready() const { return m_ready; }

    /// Tone maps `inputs.sceneColor` into `inputs.output`. Call outside a render pass.
    void execute(RHI::CommandBuffer* cmd, const ECS::PostProcessComponent& settings,
                 const PostProcessInputs& inputs);

    /// Settings used when the camera has no PostProcessComponent: ACES + TAA.
    static ECS::PostProcessComponent defaults();
    /// Halton(2,3) sub-pixel offset in [-0.5, 0.5] pixels.
    static Vec2 taaJitter(u32 frameIndex);
    /// Effects whose result depends on previous frames (keep rendering while active).
    static bool isTemporal(const ECS::PostProcessComponent& settings);
    static bool usesTaa(const ECS::PostProcessComponent& settings);

private:
    struct ViewResources {
        u64 viewId = 0;
        u32 width = 0;
        u32 height = 0;
        u32 outWidth = 0;
        u32 outHeight = 0;
        u64 lastUsed = 0;
        RHI::Texture* history[2] = {nullptr, nullptr};
        u32 historyIndex = 0;
        bool historyValid = false;
        RHI::Texture* temp[2] = {nullptr, nullptr};
        RHI::Texture* ao = nullptr;
        RHI::Texture* aoBlur = nullptr;
        RHI::Texture* luminance = nullptr;
        u32 luminanceMips = 0;
        RHI::Texture* adapted[2] = {nullptr, nullptr};
        u32 adaptedIndex = 0;
        bool adaptedValid = false;
        std::vector<RHI::Texture*> bloom;
        std::vector<u32> bloomWidths;
        std::vector<u32> bloomHeights;
        RHI::Texture* ldr = nullptr;
    };

    ViewResources* acquireView(const PostProcessInputs& inputs);
    void releaseView(ViewResources& view);
    RHI::Texture* createTarget(u32 width, u32 height, u32 format, u32 mipLevels = 1);
    RHI::Pipeline* createPass(RHI::Shader* fragment, u32 colorFormat, bool additive);

    struct PassTexture {
        RHI::Texture* texture = nullptr;
        bool point = false;
    };
    void runPass(RHI::CommandBuffer* cmd, RHI::Pipeline* pipeline, RHI::Texture* target,
                 u32 width, u32 height, const PassTexture* textures, u32 textureCount,
                 const void* ubo, u32 uboSize, bool loadColor = false);

    RHI::RenderDevice* m_device = nullptr;
    bool m_ready = false;
    u64 m_frameCounter = 0;

    RHI::Shader* m_vertex = nullptr;
    std::vector<RHI::Shader*> m_shaders;
    RHI::Pipeline* m_luminance = nullptr;
    RHI::Pipeline* m_adapt = nullptr;
    RHI::Pipeline* m_bloomPrefilter = nullptr;
    RHI::Pipeline* m_bloomDown = nullptr;
    RHI::Pipeline* m_bloomUp = nullptr;
    RHI::Pipeline* m_ssao = nullptr;
    RHI::Pipeline* m_ssaoBlur = nullptr;
    RHI::Pipeline* m_dof = nullptr;
    RHI::Pipeline* m_motionBlur = nullptr;
    RHI::Pipeline* m_taa = nullptr;
    RHI::Pipeline* m_composite = nullptr;
    RHI::Pipeline* m_fxaa = nullptr;
    RHI::Sampler* m_linear = nullptr;
    RHI::Sampler* m_point = nullptr;

    std::vector<ViewResources> m_views;
};

}  // namespace Caffeine::Render

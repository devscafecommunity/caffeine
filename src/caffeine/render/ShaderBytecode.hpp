#pragma once

#include "../core/Types.hpp"
#include "../rhi/RenderDevice.hpp"

namespace Caffeine::Render {

enum class BuiltinShader : u8 {
    SphereLitVertex,
    SphereLitFragment,
    MeshLitVertex,
    SceneLitVertex,
    SceneInstancedVertex,
    SceneLitFragment,
    TerrainLitFragment,
    ShadowDepthVertex,
    SceneDisplaceVertex,
    ShadowDisplaceVertex,
    ShadowDepthFragment,
    ShadowCutoutFragment,
    SceneFoliageVertex,
    SceneFoliageInstancedVertex,
    ShadowFoliageVertex,
    WaterVertex,
    FullscreenVertex,
    SkyFragment,
    GridFragment,
    EffectBillboardVertex,
    EffectBillboardFragment,
    PostLuminanceFragment,
    PostAdaptFragment,
    PostBloomPrefilterFragment,
    PostBloomDownFragment,
    PostBloomUpFragment,
    PostSsaoFragment,
    PostSsaoBlurFragment,
    PostDofFragment,
    PostMotionBlurFragment,
    PostCompositeFragment,
    PostFxaaFragment,
    PostTaaFragment,
    PostPhotonFragment,
    PostPhotonResolveFragment,
    MinZDownsampleFragment,
    Batch2DVertex,
    Batch2DFragment,
    PhotonCascade,
    PhotonC0,
    PhotonVoxelize,
    PhotonVoxelizeTri,
    PhotonInject,
    PhotonProbe,
    PhotonBakeAO,
    MicromeshCull,
    SceneMicromeshVertex,
    ShadowMicromeshVertex,
    MicromeshCompact,
    MicromeshReset,
    MicromeshHzb,
};

struct ShaderBytecodeView {
    const u8* data = nullptr;
    usize     size = 0;
    const char* entryPoint = "main";
};

ShaderBytecodeView getBuiltinShaderBytecode(BuiltinShader shader,
                                            RHI::ShaderBytecodeFormat format);
RHI::ShaderBytecodeFormat detectShaderFormat(RHI::RenderDevice* device);

/// Creates a GPU shader from the embedded bytecode; nullptr when the format isn't bundled.
RHI::Shader* createBuiltinShader(RHI::RenderDevice* device, BuiltinShader shader,
                                 RHI::ShaderStage stage, u32 numUniformBuffers,
                                 u32 numSamplers = 0, u32 numStorageBuffers = 0);

}  // namespace Caffeine::Render

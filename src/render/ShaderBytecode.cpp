#include "render/ShaderBytecode.hpp"

#include <SDL3/SDL_gpu.h>
#include <cstring>

#ifdef CF_HAS_GENERATED_SHADERS
#include "BuiltinShaders_spirv.hpp"
#endif
#ifdef CF_HAS_GENERATED_SHADERS_DXBC
#include "BuiltinShaders_dxbc.hpp"
#endif
#ifdef CF_HAS_GENERATED_SHADERS_MSL
#include "BuiltinShaders_msl.hpp"
#endif

namespace Caffeine::Render {
namespace {

ShaderBytecodeView lookupSpirv(BuiltinShader shader) {
#ifdef CF_HAS_GENERATED_SHADERS
    switch (shader) {
        case BuiltinShader::SphereLitVertex:     return {Shaders::sphere_lit_vert_spirv, Shaders::sphere_lit_vert_spirvSize};
        case BuiltinShader::SphereLitFragment:   return {Shaders::sphere_lit_frag_spirv, Shaders::sphere_lit_frag_spirvSize};
        case BuiltinShader::MeshLitVertex:       return {Shaders::mesh_lit_vert_spirv, Shaders::mesh_lit_vert_spirvSize};
        case BuiltinShader::SceneLitVertex:      return {Shaders::scene_lit_vert_spirv, Shaders::scene_lit_vert_spirvSize};
        case BuiltinShader::SceneInstancedVertex: return {Shaders::scene_instanced_vert_spirv, Shaders::scene_instanced_vert_spirvSize};
        case BuiltinShader::SceneLitFragment:    return {Shaders::scene_lit_frag_spirv, Shaders::scene_lit_frag_spirvSize};
        case BuiltinShader::TerrainLitFragment:  return {Shaders::terrain_lit_frag_spirv, Shaders::terrain_lit_frag_spirvSize};
        case BuiltinShader::ShadowDepthVertex:   return {Shaders::shadow_depth_vert_spirv, Shaders::shadow_depth_vert_spirvSize};
        case BuiltinShader::ShadowDepthFragment: return {Shaders::shadow_depth_frag_spirv, Shaders::shadow_depth_frag_spirvSize};
        case BuiltinShader::FullscreenVertex:    return {Shaders::fullscreen_vert_spirv, Shaders::fullscreen_vert_spirvSize};
        case BuiltinShader::SkyFragment:         return {Shaders::sky_frag_spirv, Shaders::sky_frag_spirvSize};
        case BuiltinShader::GridFragment:        return {Shaders::grid_frag_spirv, Shaders::grid_frag_spirvSize};
        case BuiltinShader::EffectBillboardVertex: return {Shaders::effect_billboard_vert_spirv, Shaders::effect_billboard_vert_spirvSize};
        case BuiltinShader::EffectBillboardFragment: return {Shaders::effect_billboard_frag_spirv, Shaders::effect_billboard_frag_spirvSize};
        case BuiltinShader::PostLuminanceFragment: return {Shaders::post_luminance_frag_spirv, Shaders::post_luminance_frag_spirvSize};
        case BuiltinShader::PostAdaptFragment:   return {Shaders::post_adapt_frag_spirv, Shaders::post_adapt_frag_spirvSize};
        case BuiltinShader::PostBloomPrefilterFragment: return {Shaders::post_bloom_prefilter_frag_spirv, Shaders::post_bloom_prefilter_frag_spirvSize};
        case BuiltinShader::PostBloomDownFragment: return {Shaders::post_bloom_down_frag_spirv, Shaders::post_bloom_down_frag_spirvSize};
        case BuiltinShader::PostBloomUpFragment: return {Shaders::post_bloom_up_frag_spirv, Shaders::post_bloom_up_frag_spirvSize};
        case BuiltinShader::PostSsaoFragment:    return {Shaders::post_ssao_frag_spirv, Shaders::post_ssao_frag_spirvSize};
        case BuiltinShader::PostSsaoBlurFragment: return {Shaders::post_ssao_blur_frag_spirv, Shaders::post_ssao_blur_frag_spirvSize};
        case BuiltinShader::PostDofFragment:     return {Shaders::post_dof_frag_spirv, Shaders::post_dof_frag_spirvSize};
        case BuiltinShader::PostMotionBlurFragment: return {Shaders::post_motion_blur_frag_spirv, Shaders::post_motion_blur_frag_spirvSize};
        case BuiltinShader::PostCompositeFragment: return {Shaders::post_composite_frag_spirv, Shaders::post_composite_frag_spirvSize};
        case BuiltinShader::PostFxaaFragment:    return {Shaders::post_fxaa_frag_spirv, Shaders::post_fxaa_frag_spirvSize};
        case BuiltinShader::PostTaaFragment:     return {Shaders::post_taa_frag_spirv, Shaders::post_taa_frag_spirvSize};
    }
#endif
    (void)shader;
    return {};
}

ShaderBytecodeView lookupDxbc(BuiltinShader shader) {
#ifdef CF_HAS_GENERATED_SHADERS_DXBC
    switch (shader) {
        case BuiltinShader::SphereLitVertex:     return {Shaders::sphere_lit_vert_dxbc, Shaders::sphere_lit_vert_dxbcSize};
        case BuiltinShader::SphereLitFragment:   return {Shaders::sphere_lit_frag_dxbc, Shaders::sphere_lit_frag_dxbcSize};
        case BuiltinShader::MeshLitVertex:       return {Shaders::mesh_lit_vert_dxbc, Shaders::mesh_lit_vert_dxbcSize};
        case BuiltinShader::SceneLitVertex:      return {Shaders::scene_lit_vert_dxbc, Shaders::scene_lit_vert_dxbcSize};
        case BuiltinShader::SceneInstancedVertex: return {Shaders::scene_instanced_vert_dxbc, Shaders::scene_instanced_vert_dxbcSize};
        case BuiltinShader::SceneLitFragment:    return {Shaders::scene_lit_frag_dxbc, Shaders::scene_lit_frag_dxbcSize};
        case BuiltinShader::TerrainLitFragment:  return {Shaders::terrain_lit_frag_dxbc, Shaders::terrain_lit_frag_dxbcSize};
        case BuiltinShader::ShadowDepthVertex:   return {Shaders::shadow_depth_vert_dxbc, Shaders::shadow_depth_vert_dxbcSize};
        case BuiltinShader::ShadowDepthFragment: return {Shaders::shadow_depth_frag_dxbc, Shaders::shadow_depth_frag_dxbcSize};
        default: break;
    }
#endif
    (void)shader;
    return {};
}

ShaderBytecodeView lookupMsl(BuiltinShader shader) {
#ifdef CF_HAS_GENERATED_SHADERS_MSL
    switch (shader) {
        case BuiltinShader::SphereLitVertex:     return {Shaders::sphere_lit_vert_msl, Shaders::sphere_lit_vert_mslSize, "main0"};
        case BuiltinShader::SphereLitFragment:   return {Shaders::sphere_lit_frag_msl, Shaders::sphere_lit_frag_mslSize, "main0"};
        case BuiltinShader::MeshLitVertex:       return {Shaders::mesh_lit_vert_msl, Shaders::mesh_lit_vert_mslSize, "main0"};
        case BuiltinShader::SceneLitVertex:      return {Shaders::scene_lit_vert_msl, Shaders::scene_lit_vert_mslSize, "main0"};
        case BuiltinShader::SceneInstancedVertex: return {Shaders::scene_instanced_vert_msl, Shaders::scene_instanced_vert_mslSize, "main0"};
        case BuiltinShader::SceneLitFragment:    return {Shaders::scene_lit_frag_msl, Shaders::scene_lit_frag_mslSize, "main0"};
        case BuiltinShader::TerrainLitFragment:  return {Shaders::terrain_lit_frag_msl, Shaders::terrain_lit_frag_mslSize, "main0"};
        case BuiltinShader::ShadowDepthVertex:   return {Shaders::shadow_depth_vert_msl, Shaders::shadow_depth_vert_mslSize, "main0"};
        case BuiltinShader::ShadowDepthFragment: return {Shaders::shadow_depth_frag_msl, Shaders::shadow_depth_frag_mslSize, "main0"};
        default: break;
    }
#endif
    (void)shader;
    return {};
}

}  // namespace

ShaderBytecodeView getBuiltinShaderBytecode(BuiltinShader shader, RHI::ShaderBytecodeFormat format) {
    switch (format) {
        case RHI::ShaderBytecodeFormat::DXBC:     return lookupDxbc(shader);
        case RHI::ShaderBytecodeFormat::MSL:
        case RHI::ShaderBytecodeFormat::Metallib: return lookupMsl(shader);
        case RHI::ShaderBytecodeFormat::SPIRV:
        default:                                  return lookupSpirv(shader);
    }
}

RHI::Shader* createBuiltinShader(RHI::RenderDevice* device, BuiltinShader shader,
                                 RHI::ShaderStage stage, u32 numUniformBuffers, u32 numSamplers) {
    if (!device) return nullptr;
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

RHI::ShaderBytecodeFormat detectShaderFormat(RHI::RenderDevice* device) {
    if (!device || !device->isInitialized()) {
        return RHI::ShaderBytecodeFormat::SPIRV;
    }

    const char* driver = SDL_GetGPUDeviceDriver(device->nativeDevice());
    if (driver && std::strcmp(driver, "direct3d12") == 0) {
        return RHI::ShaderBytecodeFormat::DXBC;
    }
#ifdef __APPLE__
    SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device->nativeDevice());
    if (supported & SDL_GPU_SHADERFORMAT_METALLIB) {
        return RHI::ShaderBytecodeFormat::Metallib;
    }
    if (supported & SDL_GPU_SHADERFORMAT_MSL) {
        return RHI::ShaderBytecodeFormat::MSL;
    }
#endif
    return RHI::ShaderBytecodeFormat::SPIRV;
}

}  // namespace Caffeine::Render

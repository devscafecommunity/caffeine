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
        case BuiltinShader::SceneLitFragment:    return {Shaders::scene_lit_frag_spirv, Shaders::scene_lit_frag_spirvSize};
        case BuiltinShader::TerrainLitFragment:  return {Shaders::terrain_lit_frag_spirv, Shaders::terrain_lit_frag_spirvSize};
        case BuiltinShader::ShadowDepthVertex:   return {Shaders::shadow_depth_vert_spirv, Shaders::shadow_depth_vert_spirvSize};
        case BuiltinShader::ShadowDepthFragment: return {Shaders::shadow_depth_frag_spirv, Shaders::shadow_depth_frag_spirvSize};
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
        case BuiltinShader::SceneLitFragment:    return {Shaders::scene_lit_frag_dxbc, Shaders::scene_lit_frag_dxbcSize};
        case BuiltinShader::TerrainLitFragment:  return {Shaders::terrain_lit_frag_dxbc, Shaders::terrain_lit_frag_dxbcSize};
        case BuiltinShader::ShadowDepthVertex:   return {Shaders::shadow_depth_vert_dxbc, Shaders::shadow_depth_vert_dxbcSize};
        case BuiltinShader::ShadowDepthFragment: return {Shaders::shadow_depth_frag_dxbc, Shaders::shadow_depth_frag_dxbcSize};
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
        case BuiltinShader::SceneLitFragment:    return {Shaders::scene_lit_frag_msl, Shaders::scene_lit_frag_mslSize, "main0"};
        case BuiltinShader::TerrainLitFragment:  return {Shaders::terrain_lit_frag_msl, Shaders::terrain_lit_frag_mslSize, "main0"};
        case BuiltinShader::ShadowDepthVertex:   return {Shaders::shadow_depth_vert_msl, Shaders::shadow_depth_vert_mslSize, "main0"};
        case BuiltinShader::ShadowDepthFragment: return {Shaders::shadow_depth_frag_msl, Shaders::shadow_depth_frag_mslSize, "main0"};
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

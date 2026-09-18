#pragma once

#include "../core/Types.hpp"
#include "../rhi/RenderDevice.hpp"

namespace Caffeine::Render {

enum class BuiltinShader : u8 {
    SphereLitVertex,
    SphereLitFragment,
    MeshLitVertex,
    SceneLitVertex,
    SceneLitFragment,
    TerrainLitFragment,
    ShadowDepthVertex,
    ShadowDepthFragment,
};

struct ShaderBytecodeView {
    const u8* data = nullptr;
    usize     size = 0;
    const char* entryPoint = "main";
};

ShaderBytecodeView getBuiltinShaderBytecode(BuiltinShader shader,
                                            RHI::ShaderBytecodeFormat format);
RHI::ShaderBytecodeFormat detectShaderFormat(RHI::RenderDevice* device);

}  // namespace Caffeine::Render

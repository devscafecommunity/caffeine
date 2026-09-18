#pragma once

#include "../core/Types.hpp"
#include "../rhi/RenderDevice.hpp"

#include <string>
#include <vector>

namespace Caffeine::Render {

struct CompiledShader {
    std::vector<u8> bytecode;
    std::string     errorLog;
    bool            success = false;
};

class ShaderCompiler {
public:
    ShaderCompiler() = default;
    ~ShaderCompiler();

    ShaderCompiler(const ShaderCompiler&) = delete;
    ShaderCompiler& operator=(const ShaderCompiler&) = delete;

    CompiledShader compileGlsl(const std::string& source, RHI::ShaderStage stage,
                                 const char* entryPoint = "main");

    CompiledShader compileCached(const std::string& source, RHI::ShaderStage stage,
                                 const char* entryPoint = "main");

    void clearCache();

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}  // namespace Caffeine::Render

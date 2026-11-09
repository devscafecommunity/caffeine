#include "render/ShaderCompiler.hpp"

#ifdef CF_HAS_SHADERC
#include <shaderc/shaderc.hpp>
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>

namespace Caffeine::Render {

struct ShaderCompiler::Impl {
#ifdef CF_HAS_SHADERC
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
#endif
    std::unordered_map<std::string, CompiledShader> cache;

    Impl() {
#ifdef CF_HAS_SHADERC
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
#endif
    }
};

ShaderCompiler::~ShaderCompiler() {
    delete m_impl;
}

#ifdef CF_HAS_SHADERC
static shaderc_shader_kind toShadercKind(RHI::ShaderStage stage) {
    switch (stage) {
        case RHI::ShaderStage::Vertex:   return shaderc_vertex_shader;
        case RHI::ShaderStage::Fragment: return shaderc_fragment_shader;
        default:                         return shaderc_glsl_default_fragment_shader;
    }
}
#endif

static CompiledShader compileWithGlslc(const std::string& source, RHI::ShaderStage stage) {
    CompiledShader result;
    const char* stageName = (stage == RHI::ShaderStage::Vertex) ? "vertex" : "fragment";
    const auto tmpDir = std::filesystem::temp_directory_path();
    const auto srcPath = tmpDir / "caffeine_shader_tmp.glsl";
    const auto outPath = tmpDir / "caffeine_shader_tmp.spv";

    {
        std::ofstream out(srcPath);
        if (!out) {
            result.errorLog = "Failed to write temporary shader source";
            return result;
        }
        out << source;
    }

    const std::string cmd = std::string("glslc -fshader-stage=") + stageName + " \"" +
                            srcPath.string() + "\" -o \"" + outPath.string() + "\" 2>&1";
    if (std::system(cmd.c_str()) != 0) {
        result.errorLog = "glslc compilation failed";
        return result;
    }

    std::ifstream in(outPath, std::ios::binary);
    if (!in) {
        result.errorLog = "Failed to read SPIR-V output";
        return result;
    }
    result.bytecode.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    result.success = !result.bytecode.empty();
    return result;
}

CompiledShader ShaderCompiler::compileGlsl(const std::string& source, RHI::ShaderStage stage,
                                           const char* entryPoint) {
    CompiledShader result;
    if (source.empty()) {
        result.errorLog = "Shader source is empty";
        return result;
    }

    if (!m_impl) {
        m_impl = new Impl();
    }

#ifdef CF_HAS_SHADERC
    const shaderc_shader_kind kind = toShadercKind(stage);
    const shaderc::SpvCompilationResult compilation =
        m_impl->compiler.CompileGlslToSpv(source.c_str(), source.size(), kind,
                                          entryPoint ? entryPoint : "main",
                                          m_impl->options);

    if (compilation.GetCompilationStatus() != shaderc_compilation_status_success) {
        result.errorLog = compilation.GetErrorMessage();
        return result;
    }

    const auto begin = compilation.cbegin();
    const auto end = compilation.cend();
    result.bytecode.assign(begin, end);
    result.success = !result.bytecode.empty();
    if (!result.success) {
        result.errorLog = "SPIR-V output is empty";
    }
    return result;
#else
    (void)entryPoint;
    return compileWithGlslc(source, stage);
#endif
}

CompiledShader ShaderCompiler::compileCached(const std::string& source, RHI::ShaderStage stage,
                                             const char* entryPoint) {
    if (!m_impl) {
        m_impl = new Impl();
    }

    std::hash<std::string> hasher;
    const std::string key = std::to_string(static_cast<u8>(stage)) + ":" +
                            (entryPoint ? entryPoint : "main") + ":" +
                            std::to_string(hasher(source));

    auto it = m_impl->cache.find(key);
    if (it != m_impl->cache.end()) {
        return it->second;
    }

    CompiledShader compiled = compileGlsl(source, stage, entryPoint);
    m_impl->cache.emplace(key, compiled);
    return compiled;
}

void ShaderCompiler::clearCache() {
    if (m_impl) {
        m_impl->cache.clear();
    }
}

}  // namespace Caffeine::Render

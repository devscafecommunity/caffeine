#pragma once
#include "core/Types.hpp"
#include "math/Vec4.hpp"
#include "render/ShaderCompiler.hpp"
#include <memory>
#include <string>
#include <vector>

#ifdef CF_HAS_IMGUI
struct ImTextureData;
#endif

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#include "assets/MeshTypes.hpp"
#endif

namespace Caffeine::Editor {

class PreviewRenderer {
public:
    PreviewRenderer() = default;
    ~PreviewRenderer();

    bool init(RHI::RenderDevice* device);
    void shutdown();

    bool gpuReady() const { return m_gpuReady; }

    void render(RHI::CommandBuffer* cmd, const std::string& shaderCode);
    void renderGpu(RHI::CommandBuffer* cmd, const Vec4& albedo, f32 metallic, f32 roughness,
                   float rotationDeg);

    void renderFallback(float rotationDeg, float width, float height);
    void renderMaterial(const Vec4& albedo, f32 metallic, f32 roughness,
                        float rotationDeg, float width, float height);
    void displayGpuPreview(float width, float height);

private:
    struct SphereUniforms {
        float mvp[16];
        float albedo[4];
        float metallic;
        float roughness;
        float padding[2];
    };

    bool createSphereMesh();
    bool uploadSphereMesh();
    bool ensureGpuResources();
    bool createBuiltinPipeline(const std::string& shaderCode);
    bool createGraphPipeline(const std::string& fragmentGlsl);
    void rebuildMaterialTexture(const Vec4& albedo, f32 metallic, f32 roughness, float rotationDeg);
    void buildSphereUniforms(SphereUniforms& out, const Vec4& albedo, f32 metallic, f32 roughness,
                             float rotationDeg) const;

    RHI::RenderDevice* m_device = nullptr;
    RHI::Shader* m_vertexShader = nullptr;
    RHI::Shader* m_fragmentShader = nullptr;
    RHI::Pipeline* m_pipeline = nullptr;
    RHI::Sampler* m_sampler = nullptr;
    RHI::Texture* m_colorTarget = nullptr;
    RHI::Texture* m_depthTarget = nullptr;
    RHI::Buffer* m_sphereVertices = nullptr;
    RHI::Buffer* m_sphereIndices = nullptr;
    std::vector<float> m_sphereVertexData;
    std::vector<uint32_t> m_sphereIndexData;
    u32 m_indexCount = 0;
    bool m_initialized = false;
    bool m_hasRHI = false;
    bool m_gpuReady = false;
    bool m_usingGraphShader = false;
    std::string m_activeFragmentSource;
    Render::ShaderCompiler m_shaderCompiler;
    static constexpr u32 kPreviewSize = 96;

#ifdef CF_HAS_IMGUI
    std::unique_ptr<ImTextureData> m_previewTexture;
    Vec4 m_cachedAlbedo{-1.0f, -1.0f, -1.0f, -1.0f};
    f32 m_cachedMetallic = -1.0f;
    f32 m_cachedRoughness = -1.0f;
    float m_cachedRotation = -10000.0f;
    Vec4 m_lastGpuAlbedo{-1.0f, -1.0f, -1.0f, -1.0f};
    f32 m_lastGpuMetallic = -1.0f;
    f32 m_lastGpuRoughness = -1.0f;
    float m_lastGpuRotation = -10000.0f;
#endif
};

} // namespace Caffeine::Editor

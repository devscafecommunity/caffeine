#pragma once
#include "core/Types.hpp"
#include <string>

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

    void render(RHI::CommandBuffer* cmd, const std::string& shaderCode);

    void renderFallback(float rotationDeg, float width, float height);

private:
    bool createSphereMesh();
    bool createShaderPipeline(const std::string& shaderCode);

    RHI::RenderDevice* m_device = nullptr;
    RHI::Shader* m_vertexShader = nullptr;
    RHI::Shader* m_fragmentShader = nullptr;
    void* m_pipeline = nullptr;
    RHI::Buffer* m_sphereVertices = nullptr;
    RHI::Buffer* m_sphereIndices = nullptr;
    u32 m_indexCount = 0;
    bool m_initialized = false;
    bool m_hasRHI = false;
};

} // namespace Caffeine::Editor

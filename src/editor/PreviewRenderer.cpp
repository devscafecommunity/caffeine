#include "editor/PreviewRenderer.hpp"
#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif
#include <cmath>

namespace Caffeine::Editor {

PreviewRenderer::~PreviewRenderer() {
    shutdown();
}

bool PreviewRenderer::init(RHI::RenderDevice* device) {
    m_device = device;
    m_hasRHI = m_device && m_device->isInitialized();
    if (m_hasRHI) {
        m_initialized = createSphereMesh();
        return m_initialized;
    }
    m_initialized = false;
    return false;
}

void PreviewRenderer::shutdown() {
    if (m_hasRHI && m_device) {
        if (m_vertexShader)   m_device->destroyShader(m_vertexShader);
        if (m_fragmentShader) m_device->destroyShader(m_fragmentShader);
        if (m_sphereVertices) m_device->destroyBuffer(m_sphereVertices);
        if (m_sphereIndices)  m_device->destroyBuffer(m_sphereIndices);
    }
    m_vertexShader = nullptr;
    m_fragmentShader = nullptr;
    m_pipeline = nullptr;
    m_sphereVertices = nullptr;
    m_sphereIndices = nullptr;
    m_initialized = false;
}

bool PreviewRenderer::createSphereMesh() {
    const int segs = 32;
    std::vector<float> verts;
    std::vector<uint32_t> indices;

    for (int lat = 0; lat <= segs; lat++) {
        float theta = static_cast<float>(lat) * 3.14159f / static_cast<float>(segs);
        for (int lon = 0; lon <= segs; lon++) {
            float phi = static_cast<float>(lon) * 2.0f * 3.14159f / static_cast<float>(segs);
            float x = sinf(theta) * cosf(phi);
            float y = cosf(theta);
            float z = sinf(theta) * sinf(phi);
            verts.push_back(x); verts.push_back(y); verts.push_back(z);
            verts.push_back(x); verts.push_back(y); verts.push_back(z);
            verts.push_back(static_cast<float>(lon) / static_cast<float>(segs));
            verts.push_back(static_cast<float>(lat) / static_cast<float>(segs));
        }
    }

    for (int lat = 0; lat < segs; lat++) {
        for (int lon = 0; lon < segs; lon++) {
            int first = lat * (segs + 1) + lon;
            int second = first + segs + 1;
            indices.push_back(static_cast<uint32_t>(first));
            indices.push_back(static_cast<uint32_t>(second));
            indices.push_back(static_cast<uint32_t>(first + 1));
            indices.push_back(static_cast<uint32_t>(second));
            indices.push_back(static_cast<uint32_t>(second + 1));
            indices.push_back(static_cast<uint32_t>(first + 1));
        }
    }

    m_indexCount = static_cast<uint32_t>(indices.size());

    if (m_hasRHI && m_device) {
        RHI::BufferDesc desc;
        desc.size = verts.size() * sizeof(float);
        desc.name = "SphereVerts";
        m_sphereVertices = m_device->createBuffer(desc, RHI::BufferUsage::Vertex);

        desc.size = indices.size() * sizeof(uint32_t);
        desc.name = "SphereIndices";
        m_sphereIndices = m_device->createBuffer(desc, RHI::BufferUsage::Index);
    }

    return true;
}

bool PreviewRenderer::createShaderPipeline(const std::string& shaderCode) {
    (void)shaderCode;
    if (!m_hasRHI || !m_device) return false;

    // TODO: Parse shader code, create RHI shaders and pipeline
    // For now, just a placeholder

    return false;
}

void PreviewRenderer::render(RHI::CommandBuffer* cmd, const std::string& shaderCode) {
    (void)cmd;
    if (m_hasRHI && !shaderCode.empty()) {
        createShaderPipeline(shaderCode);
        // TODO: Full RHI pipeline render with sphere mesh
    }
}

void PreviewRenderer::renderFallback(float rotationDeg, float width, float height) {
#ifdef CF_HAS_IMGUI
    (void)rotationDeg;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImVec2 center;
    center.x = cursorPos.x + width * 0.5f;
    center.y = cursorPos.y + height * 0.5f;
    float radius = (width < height ? width : height) * 0.4f;

    ImU32 col = ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
    ImU32 outline = ImGui::GetColorU32(ImVec4(0.1f, 0.2f, 0.4f, 1.0f));

    dl->AddCircleFilled(center, radius, col, 32);
    dl->AddCircle(center, radius, outline, 32);

    float rad = rotationDeg * 3.14159f / 180.0f;
    dl->AddCircleFilled(
        ImVec2(center.x + cosf(rad) * radius * 0.3f,
               center.y + sinf(rad) * radius * 0.3f),
        radius * 0.12f, IM_COL32(255, 255, 255, 60), 16);
#else
    (void)rotationDeg;
    (void)width;
    (void)height;
#endif
}

} // namespace Caffeine::Editor

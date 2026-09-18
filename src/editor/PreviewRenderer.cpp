#include "editor/PreviewRenderer.hpp"
#include "render/ShaderBytecode.hpp"
#include "math/Mat4.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int kMaterialPreviewTexSize = 96;
}

namespace Caffeine::Editor {

constexpr u32 kSphereStride = 8u * sizeof(float);

PreviewRenderer::~PreviewRenderer() {
    shutdown();
}

bool PreviewRenderer::init(RHI::RenderDevice* device) {
    m_device = device;
    m_hasRHI = m_device && m_device->isInitialized();
    if (!m_hasRHI) {
        m_initialized = false;
        return false;
    }

    m_initialized = createSphereMesh();
    if (!m_initialized) {
        return false;
    }

    m_gpuReady = ensureGpuResources();
    return m_initialized;
}

void PreviewRenderer::shutdown() {
#ifdef CF_HAS_IMGUI
    m_previewTexture.reset();
    m_cachedAlbedo = Vec4(-1.0f, -1.0f, -1.0f, -1.0f);
    m_cachedMetallic = -1.0f;
    m_cachedRoughness = -1.0f;
    m_cachedRotation = -10000.0f;
    m_lastGpuAlbedo = Vec4(-1.0f, -1.0f, -1.0f, -1.0f);
    m_lastGpuMetallic = -1.0f;
    m_lastGpuRoughness = -1.0f;
    m_lastGpuRotation = -10000.0f;
#endif
    if (m_hasRHI && m_device) {
        if (m_vertexShader)   m_device->destroyShader(m_vertexShader);
        if (m_fragmentShader) m_device->destroyShader(m_fragmentShader);
        if (m_pipeline)       m_device->destroyPipeline(m_pipeline);
        if (m_sampler)        m_device->destroySampler(m_sampler);
        if (m_colorTarget)    m_device->destroyTexture(m_colorTarget);
        if (m_depthTarget)    m_device->destroyTexture(m_depthTarget);
        if (m_sphereVertices) m_device->destroyBuffer(m_sphereVertices);
        if (m_sphereIndices)  m_device->destroyBuffer(m_sphereIndices);
    }
    m_vertexShader = nullptr;
    m_fragmentShader = nullptr;
    m_pipeline = nullptr;
    m_sampler = nullptr;
    m_colorTarget = nullptr;
    m_depthTarget = nullptr;
    m_sphereVertices = nullptr;
    m_sphereIndices = nullptr;
    m_sphereVertexData.clear();
    m_sphereIndexData.clear();
    m_gpuReady = false;
    m_initialized = false;
    m_hasRHI = false;
}

bool PreviewRenderer::createSphereMesh() {
    const int segs = 32;
    m_sphereVertexData.clear();
    m_sphereIndexData.clear();

    for (int lat = 0; lat <= segs; lat++) {
        float theta = static_cast<float>(lat) * 3.14159f / static_cast<float>(segs);
        for (int lon = 0; lon <= segs; lon++) {
            float phi = static_cast<float>(lon) * 2.0f * 3.14159f / static_cast<float>(segs);
            float x = sinf(theta) * cosf(phi);
            float y = cosf(theta);
            float z = sinf(theta) * sinf(phi);
            m_sphereVertexData.push_back(x);
            m_sphereVertexData.push_back(y);
            m_sphereVertexData.push_back(z);
            m_sphereVertexData.push_back(x);
            m_sphereVertexData.push_back(y);
            m_sphereVertexData.push_back(z);
            m_sphereVertexData.push_back(static_cast<float>(lon) / static_cast<float>(segs));
            m_sphereVertexData.push_back(static_cast<float>(lat) / static_cast<float>(segs));
        }
    }

    for (int lat = 0; lat < segs; lat++) {
        for (int lon = 0; lon < segs; lon++) {
            int first = lat * (segs + 1) + lon;
            int second = first + segs + 1;
            m_sphereIndexData.push_back(static_cast<uint32_t>(first));
            m_sphereIndexData.push_back(static_cast<uint32_t>(second));
            m_sphereIndexData.push_back(static_cast<uint32_t>(first + 1));
            m_sphereIndexData.push_back(static_cast<uint32_t>(second));
            m_sphereIndexData.push_back(static_cast<uint32_t>(second + 1));
            m_sphereIndexData.push_back(static_cast<uint32_t>(first + 1));
        }
    }

    m_indexCount = static_cast<u32>(m_sphereIndexData.size());
    return m_indexCount > 0;
}

bool PreviewRenderer::uploadSphereMesh() {
    if (!m_hasRHI || !m_device || m_sphereVertexData.empty() || m_sphereIndexData.empty()) {
        return false;
    }

    RHI::BufferDesc vertDesc;
    vertDesc.size = m_sphereVertexData.size() * sizeof(float);
    vertDesc.name = "SphereVerts";
    m_sphereVertices = m_device->createBuffer(vertDesc, RHI::BufferUsage::Vertex);
    if (!m_sphereVertices) {
        return false;
    }

    RHI::BufferDesc indexDesc;
    indexDesc.size = m_sphereIndexData.size() * sizeof(uint32_t);
    indexDesc.name = "SphereIndices";
    m_sphereIndices = m_device->createBuffer(indexDesc, RHI::BufferUsage::Index);
    if (!m_sphereIndices) {
        return false;
    }

    if (!m_device->uploadBuffer(m_sphereVertices, m_sphereVertexData.data(), vertDesc.size)) {
        return false;
    }
    return m_device->uploadBuffer(m_sphereIndices, m_sphereIndexData.data(), indexDesc.size);
}

bool PreviewRenderer::createGraphPipeline(const std::string& fragmentGlsl) {
    if (!m_hasRHI || !m_device || fragmentGlsl.empty()) return false;

    const auto format = Render::detectShaderFormat(m_device);
    if (format != RHI::ShaderBytecodeFormat::SPIRV) {
        return false;
    }

#ifdef CF_HAS_SHADERC
    const auto compiled = m_shaderCompiler.compileCached(fragmentGlsl, RHI::ShaderStage::Fragment);
    if (!compiled.success) return false;

    if (m_vertexShader) m_device->destroyShader(m_vertexShader);
    if (m_fragmentShader) m_device->destroyShader(m_fragmentShader);
    if (m_pipeline) m_device->destroyPipeline(m_pipeline);
    m_vertexShader = nullptr;
    m_fragmentShader = nullptr;
    m_pipeline = nullptr;

    const auto vertBytecode = Render::getBuiltinShaderBytecode(Render::BuiltinShader::MeshLitVertex, format);
    if (!vertBytecode.data) return false;

    RHI::ShaderDesc vertDesc;
    vertDesc.code = vertBytecode.data;
    vertDesc.codeSize = vertBytecode.size;
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.format = format;
    vertDesc.entryPoint = vertBytecode.entryPoint;
    vertDesc.numUniformBuffers = 1;
    m_vertexShader = m_device->createShader(vertDesc);

    RHI::ShaderDesc fragDesc;
    fragDesc.code = compiled.bytecode.data();
    fragDesc.codeSize = compiled.bytecode.size();
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.format = format;
    fragDesc.numUniformBuffers = 1;
    fragDesc.numSamplers = 1;
    m_fragmentShader = m_device->createShader(fragDesc);

    RHI::VertexBufferLayoutDesc layout{0, kSphereStride, false};
    RHI::VertexAttributeDesc attributes[3] = {
        {0, 0, RHI::VertexFormat::Float3, 0},
        {1, 0, RHI::VertexFormat::Float3, 12},
        {2, 0, RHI::VertexFormat::Float2, 24},
    };
    RHI::GraphicsPipelineDesc pipeDesc;
    pipeDesc.vertexBuffers = &layout;
    pipeDesc.numVertexBuffers = 1;
    pipeDesc.attributes = attributes;
    pipeDesc.numAttributes = 3;
    pipeDesc.colorFormat = RHI::TextureFormat::R8G8B8A8_UNORM;
    pipeDesc.depthFormat = RHI::TextureFormat::D32_FLOAT;
    m_pipeline = m_device->createGraphicsPipeline(m_vertexShader, m_fragmentShader, pipeDesc);
    m_usingGraphShader = m_pipeline != nullptr;
    m_activeFragmentSource = fragmentGlsl;
    return m_usingGraphShader;
#else
    (void)fragmentGlsl;
    return false;
#endif
}

bool PreviewRenderer::createBuiltinPipeline(const std::string& shaderCode) {
    if (!shaderCode.empty() && shaderCode.find("void main()") != std::string::npos) {
        if (createGraphPipeline(shaderCode)) {
            return true;
        }
    }

    const auto format = Render::detectShaderFormat(m_device);
    const auto vertBytecode = Render::getBuiltinShaderBytecode(Render::BuiltinShader::SphereLitVertex, format);
    const auto fragBytecode = Render::getBuiltinShaderBytecode(Render::BuiltinShader::SphereLitFragment, format);
    if (!vertBytecode.data || !fragBytecode.data || vertBytecode.size == 0 || fragBytecode.size == 0) {
        return false;
    }

    RHI::ShaderDesc vertDesc;
    vertDesc.code = vertBytecode.data;
    vertDesc.codeSize = vertBytecode.size;
    vertDesc.stage = RHI::ShaderStage::Vertex;
    vertDesc.format = format;
    vertDesc.entryPoint = vertBytecode.entryPoint;
    vertDesc.numUniformBuffers = 1;

    RHI::ShaderDesc fragDesc;
    fragDesc.code = fragBytecode.data;
    fragDesc.codeSize = fragBytecode.size;
    fragDesc.stage = RHI::ShaderStage::Fragment;
    fragDesc.format = format;
    fragDesc.entryPoint = fragBytecode.entryPoint;
    fragDesc.numUniformBuffers = 1;

    if (m_vertexShader) m_device->destroyShader(m_vertexShader);
    if (m_fragmentShader) m_device->destroyShader(m_fragmentShader);
    if (m_pipeline) m_device->destroyPipeline(m_pipeline);

    m_vertexShader = m_device->createShader(vertDesc);
    m_fragmentShader = m_device->createShader(fragDesc);
    if (!m_vertexShader || !m_fragmentShader) {
        return false;
    }

    RHI::VertexBufferLayoutDesc layout{};
    layout.slot = 0;
    layout.stride = kSphereStride;

    RHI::VertexAttributeDesc attributes[3] = {
        {0, 0, RHI::VertexFormat::Float3, 0},
        {1, 0, RHI::VertexFormat::Float3, 12},
        {2, 0, RHI::VertexFormat::Float2, 24},
    };

    RHI::GraphicsPipelineDesc pipeDesc;
    pipeDesc.vertexBuffers = &layout;
    pipeDesc.numVertexBuffers = 1;
    pipeDesc.attributes = attributes;
    pipeDesc.numAttributes = 3;
    pipeDesc.colorFormat = RHI::TextureFormat::R8G8B8A8_UNORM;
    pipeDesc.depthFormat = RHI::TextureFormat::D32_FLOAT;
    pipeDesc.depthTest = true;
    pipeDesc.depthWrite = true;
    pipeDesc.enableBlend = false;

    m_pipeline = m_device->createGraphicsPipeline(m_vertexShader, m_fragmentShader, pipeDesc);
    m_usingGraphShader = false;
    m_activeFragmentSource.clear();
    return m_pipeline != nullptr;
}

bool PreviewRenderer::ensureGpuResources() {
    if (!m_hasRHI || !m_device) {
        return false;
    }

    if (!uploadSphereMesh()) {
        return false;
    }

    RHI::TextureDesc colorDesc;
    colorDesc.width = kPreviewSize;
    colorDesc.height = kPreviewSize;
    colorDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    colorDesc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    m_colorTarget = m_device->createTexture(colorDesc);

    RHI::TextureDesc depthDesc;
    depthDesc.width = kPreviewSize;
    depthDesc.height = kPreviewSize;
    depthDesc.format = RHI::TextureFormat::D32_FLOAT;
    depthDesc.usage = RHI::TextureUsage::DepthStencil;
    m_depthTarget = m_device->createTexture(depthDesc);

    m_sampler = m_device->createSampler();
    if (!m_colorTarget || !m_depthTarget || !m_sampler) {
        return false;
    }

    return createBuiltinPipeline({});
}

void PreviewRenderer::buildSphereUniforms(SphereUniforms& out, const Vec4& albedo, f32 metallic,
                                        f32 roughness, float rotationDeg) const {
    const float rot = rotationDeg * 3.14159f / 180.0f;
    const Mat4 model = Mat4::rotationY(rot);
    const Mat4 view = Mat4::lookAt(Vec3(0.0f, 0.0f, 2.5f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 proj = Mat4::perspective(45.0f * 3.14159f / 180.0f, 1.0f, 0.1f, 10.0f);
    const Mat4 mvp = proj * view * model;
    std::memcpy(out.mvp, mvp.data(), sizeof(out.mvp));
    out.albedo[0] = albedo.x;
    out.albedo[1] = albedo.y;
    out.albedo[2] = albedo.z;
    out.albedo[3] = albedo.w;
    out.metallic = metallic;
    out.roughness = roughness;
    out.padding[0] = 0.0f;
    out.padding[1] = 0.0f;
}

void PreviewRenderer::renderGpu(RHI::CommandBuffer* cmd, const Vec4& albedo, f32 metallic,
                                f32 roughness, float rotationDeg) {
    if (!m_gpuReady || !cmd || !m_pipeline || !m_colorTarget || !m_depthTarget) {
        return;
    }

    SphereUniforms uniforms{};
    buildSphereUniforms(uniforms, albedo, metallic, roughness, rotationDeg);

    cmd->pushUniformData(RHI::ShaderStage::Vertex, 0, &uniforms, sizeof(uniforms));
    cmd->pushUniformData(RHI::ShaderStage::Fragment, 0, &uniforms, sizeof(uniforms));

    RHI::RenderPassDesc pass;
    pass.colorTarget = m_colorTarget;
    pass.depthTarget = m_depthTarget;
    pass.clearColor[0] = 0.09f;
    pass.clearColor[1] = 0.09f;
    pass.clearColor[2] = 0.11f;
    pass.clearColor[3] = 1.0f;
    pass.clearDepth = true;
    pass.depthValue = 1.0f;

    cmd->beginRenderPass(pass);
    cmd->bindPipeline(m_pipeline);
    cmd->bindVertexBuffer(m_sphereVertices, 0);
    cmd->bindIndexBuffer(m_sphereIndices);
    cmd->setViewport(0.0f, 0.0f, static_cast<f32>(kPreviewSize), static_cast<f32>(kPreviewSize));
    cmd->setScissor(0, 0, kPreviewSize, kPreviewSize);
    cmd->drawIndexed(m_indexCount);
    cmd->endRenderPass();

#ifdef CF_HAS_IMGUI
    m_lastGpuAlbedo = albedo;
    m_lastGpuMetallic = metallic;
    m_lastGpuRoughness = roughness;
    m_lastGpuRotation = rotationDeg;
#endif
}

void PreviewRenderer::displayGpuPreview(float width, float height) {
#ifdef CF_HAS_IMGUI
    if (!m_gpuReady || !m_colorTarget || !m_colorTarget->handle) {
        ImGui::Dummy(ImVec2(width, height));
        return;
    }

    const float side = std::min(width, height);
    const ImVec2 drawSize(side, side);
    const ImVec2 offset((width - side) * 0.5f, (height - side) * 0.5f);
    const ImVec2 start = ImGui::GetCursorPos();
    ImGui::SetCursorPos(start + offset);
    ImGui::Image(reinterpret_cast<ImTextureID>(m_colorTarget->handle), drawSize);
    ImGui::SetCursorPos(start);
    ImGui::Dummy(ImVec2(width, height));
#else
    (void)width;
    (void)height;
#endif
}

void PreviewRenderer::render(RHI::CommandBuffer* cmd, const std::string& shaderCode) {
    if (!m_hasRHI || shaderCode.empty() || !cmd) {
        return;
    }

    if (!m_gpuReady) {
        m_gpuReady = ensureGpuResources();
    }
    if (!m_gpuReady) {
        return;
    }

    if (!m_pipeline || (m_usingGraphShader && shaderCode != m_activeFragmentSource)) {
        createBuiltinPipeline(shaderCode);
    }
}

void PreviewRenderer::rebuildMaterialTexture(const Vec4& albedo, f32 metallic, f32 roughness,
                                             float rotationDeg) {
#ifdef CF_HAS_IMGUI
    if (!m_previewTexture) {
        m_previewTexture = std::make_unique<ImTextureData>();
        m_previewTexture->Create(ImTextureFormat_RGBA32, kMaterialPreviewTexSize, kMaterialPreviewTexSize);
    }

    u8* pixels = static_cast<u8*>(m_previewTexture->GetPixels());
    const int renderS = kMaterialPreviewTexSize;
    const Vec3 lightDir = Vec3(0.35f, 0.75f, 0.55f).normalized();
    const float rot = rotationDeg * 3.14159f / 180.0f;
    const float specPower = std::max(4.0f, (1.0f - roughness) * 96.0f + 8.0f);

    for (int y = 0; y < renderS; ++y) {
        for (int x = 0; x < renderS; ++x) {
            const float u = (static_cast<float>(x) / static_cast<float>(renderS - 1)) * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) / static_cast<float>(renderS - 1)) * 2.0f - 1.0f;
            const float lenSq = u * u + v * v;
            const size_t idx = static_cast<size_t>(y * renderS + x) * 4;
            if (lenSq > 1.0f) {
                pixels[idx + 0] = 24;
                pixels[idx + 1] = 24;
                pixels[idx + 2] = 28;
                pixels[idx + 3] = 255;
                continue;
            }

            const float z = std::sqrt(std::max(0.0f, 1.0f - lenSq));
            Vec3 normal(u, v, z);
            normal = Vec3(normal.x * std::cos(rot) - normal.z * std::sin(rot),
                          normal.y,
                          normal.x * std::sin(rot) + normal.z * std::cos(rot)).normalized();

            const float ndotl = std::max(0.0f, normal.dot(lightDir));
            const Vec3 viewDir(0.0f, 0.0f, 1.0f);
            const Vec3 halfVec = (lightDir + viewDir).normalized();
            const float spec = std::pow(std::max(0.0f, normal.dot(halfVec)), specPower) * metallic;

            const float diffuse = 0.25f + ndotl * 0.75f;
            pixels[idx + 0] = static_cast<u8>(std::clamp((albedo.x * diffuse + spec) * 255.0f, 0.0f, 255.0f));
            pixels[idx + 1] = static_cast<u8>(std::clamp((albedo.y * diffuse + spec) * 255.0f, 0.0f, 255.0f));
            pixels[idx + 2] = static_cast<u8>(std::clamp((albedo.z * diffuse + spec) * 255.0f, 0.0f, 255.0f));
            pixels[idx + 3] = 255;
        }
    }

    if (m_previewTexture->Status == ImTextureStatus_WantCreate) {
        ImGui_ImplSDLGPU3_UpdateTexture(m_previewTexture.get());
    } else {
        m_previewTexture->UpdateRect.x = 0;
        m_previewTexture->UpdateRect.y = 0;
        m_previewTexture->UpdateRect.w = static_cast<unsigned short>(renderS);
        m_previewTexture->UpdateRect.h = static_cast<unsigned short>(renderS);
        m_previewTexture->SetStatus(ImTextureStatus_WantUpdates);
        ImGui_ImplSDLGPU3_UpdateTexture(m_previewTexture.get());
    }

    m_cachedAlbedo = albedo;
    m_cachedMetallic = metallic;
    m_cachedRoughness = roughness;
    m_cachedRotation = rotationDeg;
#endif
}

void PreviewRenderer::renderMaterial(const Vec4& albedo, f32 metallic, f32 roughness,
                                   float rotationDeg, float width, float height) {
#ifdef CF_HAS_IMGUI
    if (m_gpuReady && m_colorTarget && m_colorTarget->handle) {
        displayGpuPreview(width, height);
        return;
    }

    const bool cacheHit =
        m_previewTexture &&
        m_cachedAlbedo.x == albedo.x && m_cachedAlbedo.y == albedo.y &&
        m_cachedAlbedo.z == albedo.z && m_cachedAlbedo.w == albedo.w &&
        m_cachedMetallic == metallic && m_cachedRoughness == roughness &&
        m_cachedRotation == rotationDeg;

    if (!cacheHit) {
        rebuildMaterialTexture(albedo, metallic, roughness, rotationDeg);
    } else if (m_previewTexture->Status == ImTextureStatus_WantCreate) {
        ImGui_ImplSDLGPU3_UpdateTexture(m_previewTexture.get());
    }

    const float side = std::min(width, height);
    const ImVec2 drawSize(side, side);
    const ImVec2 offset((width - side) * 0.5f, (height - side) * 0.5f);
    const ImVec2 start = ImGui::GetCursorPos();
    ImGui::SetCursorPos(start + offset);

    if (m_previewTexture && m_previewTexture->GetTexID() != ImTextureID_Invalid) {
        ImGui::Image(m_previewTexture->GetTexRef(), drawSize);
    } else {
        ImGui::Dummy(drawSize);
    }

    ImGui::SetCursorPos(start);
    ImGui::Dummy(ImVec2(width, height));
#else
    (void)albedo;
    (void)metallic;
    (void)roughness;
    (void)rotationDeg;
    (void)width;
    (void)height;
#endif
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
    ImGui::Dummy(ImVec2(width, height));
#else
    (void)rotationDeg;
    (void)width;
    (void)height;
#endif
}

} // namespace Caffeine::Editor

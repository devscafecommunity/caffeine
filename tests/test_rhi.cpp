#include "catch.hpp"
#include <rhi/RenderDevice.hpp>
#include <rhi/CommandBuffer.hpp>
#include <render/MinZPyramid.hpp>

using namespace Caffeine;
using namespace Caffeine::RHI;

// ============================================================================
// RenderConfig defaults
// ============================================================================

TEST_CASE("RenderConfig - Default values", "[rhi][config]") {
    RenderConfig config;
    REQUIRE(config.width == 1280);
    REQUIRE(config.height == 720);
    REQUIRE(config.vsync == true);
    REQUIRE(config.tripleBuffering == true);
}

TEST_CASE("RenderConfig - Custom values", "[rhi][config]") {
    RenderConfig config;
    config.width = 1920;
    config.height = 1080;
    config.vsync = false;
    config.tripleBuffering = false;
    config.windowTitle = "Test Window";

    REQUIRE(config.width == 1920);
    REQUIRE(config.height == 1080);
    REQUIRE(config.vsync == false);
    REQUIRE(config.tripleBuffering == false);
}

// ============================================================================
// TextureFormat enum
// ============================================================================

TEST_CASE("TextureFormat - Enum values are distinct", "[rhi][texture]") {
    REQUIRE(static_cast<u32>(TextureFormat::Invalid) != static_cast<u32>(TextureFormat::R8G8B8A8_UNORM));
    REQUIRE(static_cast<u32>(TextureFormat::R8G8B8A8_UNORM) != static_cast<u32>(TextureFormat::B8G8R8A8_UNORM));
    REQUIRE(static_cast<u32>(TextureFormat::D16_UNORM) != static_cast<u32>(TextureFormat::D32_FLOAT));
}

TEST_CASE("TextureFormat - Maps to SDL values", "[rhi][texture]") {
    REQUIRE(static_cast<u32>(TextureFormat::R8G8B8A8_UNORM) == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    REQUIRE(static_cast<u32>(TextureFormat::B8G8R8A8_UNORM) == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
    REQUIRE(static_cast<u32>(TextureFormat::D16_UNORM) == SDL_GPU_TEXTUREFORMAT_D16_UNORM);
    REQUIRE(static_cast<u32>(TextureFormat::D32_FLOAT) == SDL_GPU_TEXTUREFORMAT_D32_FLOAT);
}

// ============================================================================
// TextureUsage flags
// ============================================================================

TEST_CASE("TextureUsage - Bitwise OR combines flags", "[rhi][texture]") {
    auto combined = TextureUsage::Sampler | TextureUsage::ColorTarget;
    u32 expected = static_cast<u32>(TextureUsage::Sampler) | static_cast<u32>(TextureUsage::ColorTarget);
    REQUIRE(static_cast<u32>(combined) == expected);
}

TEST_CASE("TextureUsage - Bitwise AND tests individual flags", "[rhi][texture]") {
    auto combined = TextureUsage::Sampler | TextureUsage::ColorTarget;
    REQUIRE((combined & TextureUsage::Sampler) != 0);
    REQUIRE((combined & TextureUsage::ColorTarget) != 0);
    REQUIRE((combined & TextureUsage::DepthStencil) == 0);
}

// ============================================================================
// TextureDesc defaults
// ============================================================================

TEST_CASE("TextureDesc - Default values", "[rhi][texture]") {
    TextureDesc desc;
    REQUIRE(desc.width == 1);
    REQUIRE(desc.height == 1);
    REQUIRE(desc.format == TextureFormat::R8G8B8A8_UNORM);
    REQUIRE(desc.usage == TextureUsage::Sampler);
    REQUIRE(desc.mipLevels == 1);
}

TEST_CASE("TextureDesc - Custom values", "[rhi][texture]") {
    TextureDesc desc;
    desc.width = 512;
    desc.height = 256;
    desc.format = TextureFormat::R16G16B16A16_FLOAT;
    desc.usage = TextureUsage::ColorTarget;
    desc.mipLevels = 4;

    REQUIRE(desc.width == 512);
    REQUIRE(desc.height == 256);
    REQUIRE(desc.format == TextureFormat::R16G16B16A16_FLOAT);
    REQUIRE(desc.usage == TextureUsage::ColorTarget);
    REQUIRE(desc.mipLevels == 4);
}

// ============================================================================
// BufferUsage enum
// ============================================================================

TEST_CASE("BufferUsage - All values are distinct", "[rhi][buffer]") {
    REQUIRE(static_cast<u8>(BufferUsage::Vertex) != static_cast<u8>(BufferUsage::Index));
    REQUIRE(static_cast<u8>(BufferUsage::Index) != static_cast<u8>(BufferUsage::Uniform));
    REQUIRE(static_cast<u8>(BufferUsage::Uniform) != static_cast<u8>(BufferUsage::Storage));
    REQUIRE(static_cast<u8>(BufferUsage::Storage) != static_cast<u8>(BufferUsage::TransferSrc));
    REQUIRE(static_cast<u8>(BufferUsage::TransferSrc) != static_cast<u8>(BufferUsage::TransferDst));
}

// ============================================================================
// BufferDesc defaults
// ============================================================================

TEST_CASE("BufferDesc - Default values", "[rhi][buffer]") {
    BufferDesc desc;
    REQUIRE(desc.size == 0);
    REQUIRE(desc.name == nullptr);
}

TEST_CASE("BufferDesc - Custom values", "[rhi][buffer]") {
    BufferDesc desc;
    desc.size = 1024 * 1024;
    desc.name = "VertexBuffer";

    REQUIRE(desc.size == 1024 * 1024);
    REQUIRE(desc.name != nullptr);
}

// ============================================================================
// ShaderStage enum
// ============================================================================

TEST_CASE("ShaderStage - Maps to SDL values", "[rhi][shader]") {
    REQUIRE(static_cast<u8>(ShaderStage::Vertex) == SDL_GPU_SHADERSTAGE_VERTEX);
    REQUIRE(static_cast<u8>(ShaderStage::Fragment) == SDL_GPU_SHADERSTAGE_FRAGMENT);
}

// ============================================================================
// ShaderDesc defaults
// ============================================================================

TEST_CASE("ShaderDesc - Default values", "[rhi][shader]") {
    ShaderDesc desc;
    REQUIRE(desc.code == nullptr);
    REQUIRE(desc.codeSize == 0);
    REQUIRE(desc.stage == ShaderStage::Vertex);
    REQUIRE(desc.numSamplers == 0);
    REQUIRE(desc.numStorageTextures == 0);
    REQUIRE(desc.numStorageBuffers == 0);
    REQUIRE(desc.numUniformBuffers == 0);
}

// ============================================================================
// DrawCommand defaults
// ============================================================================

TEST_CASE("DrawCommand - Default values", "[rhi][draw]") {
    DrawCommand cmd;
    REQUIRE(cmd.pipeline == nullptr);
    REQUIRE(cmd.vertices == nullptr);
    REQUIRE(cmd.indices == nullptr);
    REQUIRE(cmd.texture == nullptr);
    REQUIRE(cmd.indexCount == 0);
    REQUIRE(cmd.firstIndex == 0);
    REQUIRE(cmd.instanceCount == 1);
    REQUIRE(cmd.sortKey == 0);
}

TEST_CASE("DrawCommand - Tint default is white", "[rhi][draw]") {
    DrawCommand cmd;
    REQUIRE(cmd.tint.x == 1.0f);
    REQUIRE(cmd.tint.y == 1.0f);
    REQUIRE(cmd.tint.z == 1.0f);
    REQUIRE(cmd.tint.w == 1.0f);
}

// ============================================================================
// RenderPassDesc defaults
// ============================================================================

TEST_CASE("RenderPassDesc - Default values", "[rhi][renderpass]") {
    RenderPassDesc desc;
    REQUIRE(desc.clearColor[0] == 0.0f);
    REQUIRE(desc.clearColor[1] == 0.0f);
    REQUIRE(desc.clearColor[2] == 0.0f);
    REQUIRE(desc.clearColor[3] == 1.0f);
    REQUIRE(desc.clearDepth == false);
    REQUIRE(desc.depthValue == 1.0f);
}

TEST_CASE("RenderPassDesc - Custom clear color", "[rhi][renderpass]") {
    RenderPassDesc desc;
    desc.clearColor[0] = 0.2f;
    desc.clearColor[1] = 0.3f;
    desc.clearColor[2] = 0.4f;
    desc.clearColor[3] = 1.0f;
    desc.clearDepth = true;
    desc.depthValue = 0.0f;

    REQUIRE(desc.clearColor[0] == Approx(0.2f));
    REQUIRE(desc.clearColor[1] == Approx(0.3f));
    REQUIRE(desc.clearColor[2] == Approx(0.4f));
    REQUIRE(desc.clearDepth == true);
    REQUIRE(desc.depthValue == Approx(0.0f));
}

// ============================================================================
// Opaque resource structs
// ============================================================================

TEST_CASE("Texture struct - Default state", "[rhi][texture]") {
    Texture tex;
    REQUIRE(tex.handle == nullptr);
    REQUIRE(tex.width == 0);
    REQUIRE(tex.height == 0);
    REQUIRE(tex.format == TextureFormat::Invalid);
}

TEST_CASE("Buffer struct - Default state", "[rhi][buffer]") {
    Buffer buf;
    REQUIRE(buf.handle == nullptr);
    REQUIRE(buf.size == 0);
    REQUIRE(buf.usage == BufferUsage::Vertex);
}

TEST_CASE("Shader struct - Default state", "[rhi][shader]") {
    Shader shader;
    REQUIRE(shader.handle == nullptr);
    REQUIRE(shader.stage == ShaderStage::Vertex);
}

TEST_CASE("Pipeline struct - Default state", "[rhi][pipeline]") {
    Pipeline pipeline;
    REQUIRE(pipeline.handle == nullptr);
}

// ============================================================================
// RenderDevice (no GPU context — API contract tests)
// ============================================================================

TEST_CASE("RenderDevice - Default state before init", "[rhi][device]") {
    RenderDevice device;
    REQUIRE(device.isInitialized() == false);
    REQUIRE(device.nativeDevice() == nullptr);
    REQUIRE(device.nativeWindow() == nullptr);
}

TEST_CASE("RenderDevice - Config queries before init use defaults", "[rhi][device]") {
    RenderDevice device;
    REQUIRE(device.backbufferWidth() == 1280);
    REQUIRE(device.backbufferHeight() == 720);
    REQUIRE(device.isVSync() == true);
}

TEST_CASE("RenderDevice - Init with null window fails", "[rhi][device]") {
    RenderDevice device;
    bool result = device.init(nullptr);
    REQUIRE(result == false);
    REQUIRE(device.isInitialized() == false);
}

TEST_CASE("RenderDevice - beginFrame without init returns null", "[rhi][device]") {
    RenderDevice device;
    auto* cmd = device.beginFrame();
    REQUIRE(cmd == nullptr);
}

TEST_CASE("RenderDevice - endFrame with null cmd is safe", "[rhi][device]") {
    RenderDevice device;
    device.endFrame(nullptr);
}

TEST_CASE("RenderDevice - shutdown without init is safe", "[rhi][device]") {
    RenderDevice device;
    device.shutdown();
    REQUIRE(device.isInitialized() == false);
}

TEST_CASE("RenderDevice - createTexture without init returns null", "[rhi][device]") {
    RenderDevice device;
    TextureDesc desc;
    desc.width = 64;
    desc.height = 64;
    REQUIRE(device.createTexture(desc) == nullptr);
}

TEST_CASE("RenderDevice - createBuffer without init returns null", "[rhi][device]") {
    RenderDevice device;
    BufferDesc desc;
    desc.size = 1024;
    REQUIRE(device.createBuffer(desc, BufferUsage::Vertex) == nullptr);
}

TEST_CASE("RenderDevice - createShader without init returns null", "[rhi][device]") {
    RenderDevice device;
    u8 fakeCode[] = {0x03, 0x02, 0x23, 0x07};
    ShaderDesc desc;
    desc.code = fakeCode;
    desc.codeSize = sizeof(fakeCode);
    REQUIRE(device.createShader(desc) == nullptr);
}

TEST_CASE("RenderDevice - createGraphicsPipeline without init returns null", "[rhi][device]") {
    RenderDevice device;
    Shader vs;
    Shader fs;
    VertexBufferLayoutDesc layout{0, 32, false};
    VertexAttributeDesc attr{0, 0, VertexFormat::Float3, 0};
    GraphicsPipelineDesc pipeDesc;
    pipeDesc.vertexBuffers = &layout;
    pipeDesc.numVertexBuffers = 1;
    pipeDesc.attributes = &attr;
    pipeDesc.numAttributes = 1;
    REQUIRE(device.createGraphicsPipeline(&vs, &fs, pipeDesc) == nullptr);
}

TEST_CASE("RenderDevice - uploadBuffer without init returns false", "[rhi][device]") {
    RenderDevice device;
    Buffer buf;
    u8 data[4] = {0};
    REQUIRE(device.uploadBuffer(&buf, data, sizeof(data)) == false);
}

TEST_CASE("RenderDevice - destroyPipeline with null is safe", "[rhi][device]") {
    RenderDevice device;
    device.destroyPipeline(nullptr);
}

TEST_CASE("RenderDevice - destroySampler with null is safe", "[rhi][device]") {
    RenderDevice device;
    device.destroySampler(nullptr);
}

TEST_CASE("GraphicsPipelineDesc - Default depth enabled", "[rhi][pipeline]") {
    GraphicsPipelineDesc desc;
    REQUIRE(desc.depthTest == true);
    REQUIRE(desc.depthWrite == true);
    REQUIRE(desc.enableBlend == false);
}

TEST_CASE("RenderPassDesc - Offscreen targets default null", "[rhi][renderpass]") {
    RenderPassDesc desc;
    REQUIRE(desc.colorTarget == nullptr);
    REQUIRE(desc.depthTarget == nullptr);
}

TEST_CASE("RenderDevice - destroyTexture with null is safe", "[rhi][device]") {
    RenderDevice device;
    device.destroyTexture(nullptr);
}

TEST_CASE("RenderDevice - destroyShader with null is safe", "[rhi][device]") {
    RenderDevice device;
    device.destroyShader(nullptr);
}

TEST_CASE("RenderDevice - destroyBuffer with null is safe", "[rhi][device]") {
    RenderDevice device;
    device.destroyBuffer(nullptr);
}

// ============================================================================
// CommandBuffer (no GPU context)
// ============================================================================

TEST_CASE("CommandBuffer - Default state", "[rhi][cmdbuffer]") {
    CommandBuffer cmd;
    REQUIRE(cmd.isInRenderPass() == false);
    REQUIRE(cmd.nativeHandle() == nullptr);
    REQUIRE(cmd.nativeRenderPass() == nullptr);
}

TEST_CASE("CommandBuffer - beginRenderPass without acquire is no-op", "[rhi][cmdbuffer]") {
    CommandBuffer cmd;
    RenderPassDesc desc;
    cmd.beginRenderPass(desc);
    REQUIRE(cmd.isInRenderPass() == false);
}

TEST_CASE("CommandBuffer - endRenderPass without begin is safe", "[rhi][cmdbuffer]") {
    CommandBuffer cmd;
    cmd.endRenderPass();
    REQUIRE(cmd.isInRenderPass() == false);
}

TEST_CASE("CommandBuffer - draw without render pass is safe", "[rhi][cmdbuffer]") {
    CommandBuffer cmd;
    cmd.draw(3, 0);
    cmd.drawIndexed(6, 0, 0);
    cmd.drawInstanced(3, 10, 0, 0);
}

TEST_CASE("CommandBuffer - bind commands without render pass are safe", "[rhi][cmdbuffer]") {
    CommandBuffer cmd;
    cmd.bindPipeline(nullptr);
    cmd.bindVertexBuffer(nullptr, 0);
    cmd.bindIndexBuffer(nullptr);
    cmd.bindTexture(nullptr, 0);
    cmd.setViewport(0, 0, 800, 600);
    cmd.setScissor(0, 0, 800, 600);
}

TEST_CASE("CommandBuffer - pushUniformData without acquire is safe", "[rhi][cmdbuffer]") {
    CommandBuffer cmd;
    float data[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    cmd.pushUniformData(ShaderStage::Vertex, 0, data, sizeof(data));
}

TEST_CASE("CommandBuffer - Move constructor transfers state", "[rhi][cmdbuffer]") {
    CommandBuffer a;
    CommandBuffer b(std::move(a));
    REQUIRE(b.isInRenderPass() == false);
    REQUIRE(b.nativeHandle() == nullptr);
}

TEST_CASE("CommandBuffer - Move assignment transfers state", "[rhi][cmdbuffer]") {
    CommandBuffer a;
    CommandBuffer b;
    b = std::move(a);
    REQUIRE(b.isInRenderPass() == false);
    REQUIRE(b.nativeHandle() == nullptr);
}

// ============================================================================
// P0 / Photon: multi-render-target G-buffer
// ============================================================================

TEST_CASE("MRT - Maximum attachment count is a valid common denominator", "[rhi][mrt]") {
    // The Photon G-buffer needs albedo + octahedral normal (+ material); four
    // attachments is the floor guaranteed by every SDL_GPU backend.
    REQUIRE(kMaxColorTargets == 4);
}

TEST_CASE("GraphicsPipelineDesc - MRT defaults to a single target", "[rhi][mrt][pipeline]") {
    GraphicsPipelineDesc desc;
    REQUIRE(desc.numColorTargets == 1);
    REQUIRE(desc.colorFormat == TextureFormat::R8G8B8A8_UNORM);
    for (u32 i = 0; i < kMaxColorTargets - 1; ++i) {
        REQUIRE(desc.extraColorFormats[i] == TextureFormat::Invalid);
    }
}

TEST_CASE("GraphicsPipelineDesc - MRT stores the G-buffer formats", "[rhi][mrt][pipeline]") {
    GraphicsPipelineDesc desc;
    desc.numColorTargets = 3;
    desc.colorFormat = TextureFormat::R16G16B16A16_FLOAT;
    desc.extraColorFormats[0] = TextureFormat::R8G8B8A8_UNORM;  // gAlbedo
    desc.extraColorFormats[1] = TextureFormat::R8G8_UNORM;      // gNormal

    REQUIRE(desc.numColorTargets == 3);
    REQUIRE(desc.extraColorFormats[0] == TextureFormat::R8G8B8A8_UNORM);
    REQUIRE(desc.extraColorFormats[1] == TextureFormat::R8G8_UNORM);
}

TEST_CASE("RenderPassDesc - MRT defaults to a single target", "[rhi][mrt][renderpass]") {
    RenderPassDesc desc;
    REQUIRE(desc.numColorTargets == 1);
    REQUIRE(desc.usePerTargetClear == false);
    for (u32 i = 0; i < kMaxColorTargets - 1; ++i) {
        REQUIRE(desc.extraColorTargets[i] == nullptr);
        for (u32 c = 0; c < 4; ++c) {
            REQUIRE(desc.extraClearColors[i][c] == 0.0f);
        }
    }
}

TEST_CASE("RenderPassDesc - MRT accepts up to three extra targets", "[rhi][mrt][renderpass]") {
    Texture albedo;
    Texture normal;
    RenderPassDesc desc;
    desc.numColorTargets = 3;
    desc.colorTarget = &albedo;
    desc.extraColorTargets[0] = &normal;
    desc.usePerTargetClear = true;
    desc.extraClearColors[0][2] = 0.5f;

    REQUIRE(desc.numColorTargets == 3);
    REQUIRE(desc.extraColorTargets[0] == &normal);
    REQUIRE(desc.extraColorTargets[1] == nullptr);
    REQUIRE(desc.extraClearColors[0][2] == Approx(0.5f));
}

// ============================================================================
// P0 / Photon: 3D + 2D-array textures
// ============================================================================

TEST_CASE("TextureType - 3D and array types are distinct", "[rhi][texture]") {
    REQUIRE(static_cast<u8>(TextureType::Texture3D) != static_cast<u8>(TextureType::Texture2D));
    REQUIRE(static_cast<u8>(TextureType::Texture2DArray) !=
            static_cast<u8>(TextureType::Texture3D));
    REQUIRE(static_cast<u8>(TextureType::Cube) != static_cast<u8>(TextureType::Texture3D));
}

TEST_CASE("TextureDesc - Volume defaults", "[rhi][texture]") {
    TextureDesc desc;
    REQUIRE(desc.type == TextureType::Texture2D);
    REQUIRE(desc.depth == 1);
}

TEST_CASE("Texture struct - Volume defaults", "[rhi][texture]") {
    Texture tex;
    REQUIRE(tex.depth == 1);
    REQUIRE(tex.type == TextureType::Texture2D);
}

TEST_CASE("TextureFormat - Photon formats map to SDL values", "[rhi][texture]") {
    REQUIRE(static_cast<u32>(TextureFormat::R8G8_UNORM) ==
            SDL_GPU_TEXTUREFORMAT_R8G8_UNORM);
    REQUIRE(static_cast<u32>(TextureFormat::R16G16_FLOAT) ==
            SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT);
    REQUIRE(static_cast<u32>(TextureFormat::R16G16_UNORM) ==
            SDL_GPU_TEXTUREFORMAT_R16G16_UNORM);
    REQUIRE(static_cast<u32>(TextureFormat::R11G11B10_UFLOAT) ==
            SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT);
}

TEST_CASE("RenderDevice - create 3D texture without init returns null", "[rhi][texture]") {
    RenderDevice device;
    TextureDesc desc;
    desc.type = TextureType::Texture3D;
    desc.width = 64;
    desc.height = 64;
    desc.depth = 64;
    REQUIRE(device.createTexture(desc) == nullptr);
}

TEST_CASE("RenderDevice - uploadTexture3D without init returns false", "[rhi][device]") {
    RenderDevice device;
    Texture tex;
    u8 data[8] = {0};
    REQUIRE(device.uploadTexture3D(&tex, data, 1, 1, 1) == false);
    REQUIRE(device.uploadTexture3D(&tex, data, 1, 1, 0) == false);
}

// ============================================================================
// P0 / Photon: compute sampled textures
// ============================================================================

TEST_CASE("CommandBuffer - bindComputeTexture without compute pass is safe", "[rhi][compute]") {
    CommandBuffer cmd;
    Texture tex;
    Sampler sampler;
    cmd.bindComputeTexture(nullptr);
    cmd.bindComputeTexture(&tex, 0, &sampler);
    cmd.bindComputeTextures(nullptr, nullptr, 0, 1);
    REQUIRE(cmd.isInComputePass() == false);
}

TEST_CASE("CommandBuffer - beginComputePass with mip/layer without acquire is no-op",
          "[rhi][compute]") {
    CommandBuffer cmd;
    Texture storage;
    cmd.beginComputePass(&storage, 3, 2);
    REQUIRE(cmd.isInComputePass() == false);
    cmd.endComputePass();
    REQUIRE(cmd.isInComputePass() == false);
}

TEST_CASE("ComputePipelineDesc - sampler count defaults to zero", "[rhi][compute]") {
    ComputePipelineDesc desc;
    REQUIRE(desc.numSamplers == 0);
    REQUIRE(desc.numReadonlyStorageTextures == 0);
    REQUIRE(desc.numReadwriteStorageTextures == 0);
}

// ============================================================================
// P0 / Photon: Min-Z pyramid
// ============================================================================

TEST_CASE("MinZParams - Defaults describe a perspective camera", "[photon][minz]") {
    Render::MinZParams params;
    REQUIRE(params.proj[0] == 1.0f);
    REQUIRE(params.proj[1] == 1.0f);
    REQUIRE(params.proj[2] == 0.0f);
    REQUIRE(params.proj[3] == 0.0f);
    REQUIRE(params.proj32 == -1.0f);
    REQUIRE(params.skyDepth > 1.0e29f);
}

TEST_CASE("MinZParams - Extracts the projection elements used by the shader",
          "[photon][minz]") {
    const Mat4 projection = Mat4::perspective(1.0472f, 16.0f / 9.0f, 0.1f, 500.0f);
    const Render::MinZParams params = Render::MinZParams::fromProjection(projection);

    REQUIRE(params.proj[0] == Approx(projection(0, 0)));
    REQUIRE(params.proj[1] == Approx(projection(1, 1)));
    REQUIRE(params.proj[2] == Approx(projection(2, 2)));
    REQUIRE(params.proj[3] == Approx(projection(2, 3)));
    REQUIRE(params.proj32 == Approx(projection(3, 2)));
    REQUIRE(params.proj32 == Approx(-1.0f));
}

TEST_CASE("MinZParams - Orthographic projection reports P32 = 0", "[photon][minz]") {
    const Mat4 projection = Mat4::ortho(-10.0f, 10.0f, -6.0f, 6.0f, 0.1f, 100.0f);
    const Render::MinZParams params = Render::MinZParams::fromProjection(projection);
    REQUIRE(params.proj32 == Approx(0.0f));
}

TEST_CASE("MinZPyramid - Mip count covers the full chain", "[photon][minz]") {
    REQUIRE(Render::MinZPyramid::mipCountFor(0, 1080) == 0);
    REQUIRE(Render::MinZPyramid::mipCountFor(1920, 0) == 0);
    REQUIRE(Render::MinZPyramid::mipCountFor(1, 1) == 1);
    REQUIRE(Render::MinZPyramid::mipCountFor(2, 2) == 2);
    REQUIRE(Render::MinZPyramid::mipCountFor(1920, 1080) == 11);  // 1920 -> 1
    REQUIRE(Render::MinZPyramid::mipCountFor(256, 256) == 9);     // 256 -> 1
}

TEST_CASE("MinZPyramid - Default state is not ready", "[photon][minz]") {
    Render::MinZPyramid pyramid;
    REQUIRE(pyramid.isReady() == false);
    REQUIRE(pyramid.texture() == nullptr);
    REQUIRE(pyramid.mipCount() == 0);
    REQUIRE(pyramid.width() == 0);
    REQUIRE(pyramid.height() == 0);
}

TEST_CASE("MinZPyramid - init without a device fails cleanly", "[photon][minz]") {
    Render::MinZPyramid pyramid;
    REQUIRE(pyramid.init(nullptr, 320, 180) == false);
    REQUIRE(pyramid.init(nullptr, 0, 0) == false);

    RenderDevice device;  // not initialised
    REQUIRE(pyramid.init(&device, 320, 180) == false);
    REQUIRE(pyramid.isReady() == false);
}

TEST_CASE("MinZPyramid - generate and shutdown without resources are safe",
          "[photon][minz]") {
    Render::MinZPyramid pyramid;
    CommandBuffer cmd;
    pyramid.generate(nullptr, nullptr, {});
    pyramid.generate(&cmd, nullptr, {});
    pyramid.shutdown(nullptr);
    REQUIRE(pyramid.isReady() == false);
}

TEST_CASE("CommandBuffer - Multi-storage compute pass is safe without acquire",
          "[rhi][compute]") {
    CommandBuffer cmd;
    Texture a;
    Texture b;
    Texture* storage[2] = {&a, &b};
    cmd.beginComputePass(storage, 2);
    REQUIRE(cmd.isInComputePass() == false);
    Texture* missing[2] = {&a, nullptr};
    cmd.beginComputePass(missing, 2);
    REQUIRE(cmd.isInComputePass() == false);
}

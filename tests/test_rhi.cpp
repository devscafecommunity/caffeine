#include "catch.hpp"
#include <rhi/RenderDevice.hpp>
#include <rhi/CommandBuffer.hpp>

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

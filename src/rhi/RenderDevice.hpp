// ============================================================================
// @file    RenderDevice.hpp
// @brief   Rendering Hardware Interface - Device abstraction over SDL_GPU
// @note    Part of rhi/ module - wraps SDL_GPU for portability
// ============================================================================
#pragma once

#include "../core/Types.hpp"
#include "../math/Mat4.hpp"
#include "../math/Vec4.hpp"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

namespace Caffeine::RHI {

class CommandBuffer;

struct RenderConfig {
    u32  width           = 1280;
    u32  height          = 720;
    bool vsync           = true;
    bool tripleBuffering = true;
    const char* windowTitle = "Caffeine Engine";
};

enum class TextureFormat : u32 {
    Invalid            = SDL_GPU_TEXTUREFORMAT_INVALID,
    R8G8B8A8_UNORM     = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
    B8G8R8A8_UNORM     = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
    R8_UNORM           = SDL_GPU_TEXTUREFORMAT_R8_UNORM,
    R16_FLOAT          = SDL_GPU_TEXTUREFORMAT_R16_FLOAT,
    R16G16B16A16_FLOAT = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
    R32G32B32A32_FLOAT = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
    D16_UNORM          = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
    D24_UNORM          = SDL_GPU_TEXTUREFORMAT_D24_UNORM,
    D32_FLOAT          = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
    R8G8B8A8_UNORM_SRGB = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
    B8G8R8A8_UNORM_SRGB = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB,
};

enum class TextureUsage : u32 {
    Sampler      = SDL_GPU_TEXTUREUSAGE_SAMPLER,
    ColorTarget  = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
    DepthStencil = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
    StorageRead  = SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ,
    ComputeRead  = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ,
    ComputeWrite = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}

inline u32 operator&(TextureUsage a, TextureUsage b) {
    return static_cast<u32>(a) & static_cast<u32>(b);
}

enum class BufferUsage : u8 {
    Vertex,
    Index,
    Uniform,
    Storage,
    TransferSrc,
    TransferDst
};

enum class ShaderStage : u8 {
    Vertex   = SDL_GPU_SHADERSTAGE_VERTEX,
    Fragment = SDL_GPU_SHADERSTAGE_FRAGMENT
};

struct Texture {
    SDL_GPUTexture* handle = nullptr;
    u32           width    = 0;
    u32           height   = 0;
    TextureFormat format   = TextureFormat::Invalid;
};

struct Buffer {
    SDL_GPUBuffer* handle = nullptr;
    u64          size     = 0;
    BufferUsage  usage    = BufferUsage::Vertex;
};

struct Shader {
    SDL_GPUShader* handle = nullptr;
    ShaderStage    stage  = ShaderStage::Vertex;
};

struct Pipeline {
    SDL_GPUGraphicsPipeline* handle = nullptr;
};

struct TextureDesc {
    u32           width     = 1;
    u32           height    = 1;
    TextureFormat format    = TextureFormat::R8G8B8A8_UNORM;
    TextureUsage  usage     = TextureUsage::Sampler;
    u32           mipLevels = 1;
};

struct BufferDesc {
    u64         size = 0;
    const char* name = nullptr;
};

struct ShaderDesc {
    const u8*   code       = nullptr;
    usize       codeSize   = 0;
    const char* entryPoint = "main";
    ShaderStage stage      = ShaderStage::Vertex;
    u32 numSamplers        = 0;
    u32 numStorageTextures = 0;
    u32 numStorageBuffers  = 0;
    u32 numUniformBuffers  = 0;
};

struct DrawCommand {
    Pipeline* pipeline      = nullptr;
    Buffer*   vertices      = nullptr;
    Buffer*   indices       = nullptr;
    Texture*  texture       = nullptr;
    u32       indexCount     = 0;
    u32       firstIndex    = 0;
    u32       instanceCount = 1;
    Mat4      transform     = Mat4::identity();
    Vec4      tint          = {1.0f, 1.0f, 1.0f, 1.0f};
    i32       sortKey       = 0;
};

struct RenderPassDesc {
    f32  clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool clearDepth    = false;
    f32  depthValue    = 1.0f;
};

// ============================================================================
// @brief  Main render device — abstracts SDL_GPUDevice, swapchain, and
//         resource management. Game code uses this instead of SDL_GPU directly.
// ============================================================================
class RenderDevice {
public:
    RenderDevice() = default;
    ~RenderDevice();

    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;
    RenderDevice(RenderDevice&&) = delete;
    RenderDevice& operator=(RenderDevice&&) = delete;

    bool init(SDL_Window* window, const RenderConfig& config = {});
    void shutdown();

    CommandBuffer* beginFrame();
    void           endFrame(CommandBuffer* cmd);

    Texture* createTexture(const TextureDesc& desc);
    Shader*  createShader(const ShaderDesc& desc);
    Buffer*  createBuffer(const BufferDesc& desc, BufferUsage usage);

    void destroyTexture(Texture* tex);
    void destroyShader(Shader* shader);
    void destroyBuffer(Buffer* buf);

    u32  backbufferWidth()  const { return m_config.width; }
    u32  backbufferHeight() const { return m_config.height; }
    bool isVSync()          const { return m_config.vsync; }
    bool isInitialized()    const { return m_device != nullptr; }

    SDL_GPUDevice* nativeDevice() const { return m_device; }
    SDL_Window*    nativeWindow() const { return m_window; }
    const RenderConfig& config() const { return m_config; }

private:
    SDL_GPUDevice* m_device     = nullptr;
    SDL_Window*    m_window     = nullptr;
    RenderConfig   m_config;
    u32            m_frameIndex = 0;

    static constexpr u32 MAX_FRAMES_IN_FLIGHT = 3;
};

}  // namespace Caffeine::RHI

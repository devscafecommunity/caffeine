#include "render/SkyboxRenderer.hpp"

#ifdef CF_HAS_IMGUI

#include "math/Vec3.hpp"
#include <stb/stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef CF_HAS_SDL3
#include <imgui_impl_sdlgpu3.h>
#endif

namespace Caffeine::Render {

bool SkyboxCamera::nearlyEqual(const SkyboxCamera& other, f32 epsilon) const {
    auto close = [&](const Vec3& a, const Vec3& b) {
        return std::abs(a.x - b.x) <= epsilon
            && std::abs(a.y - b.y) <= epsilon
            && std::abs(a.z - b.z) <= epsilon;
    };
    return close(forward, other.forward)
        && close(right, other.right)
        && close(up, other.up)
        && std::abs(fovY - other.fovY) <= epsilon
        && std::abs(aspect - other.aspect) <= epsilon;
}

namespace {

constexpr int kSkyboxRasterMaxDim = 320;
constexpr f32 kTwoPi = 6.2831853f;

void sampleEquirectNearest(const u8* pixels, u32 width, u32 height, float u, float v, u8 outRgba[4]) {
    if (!pixels || width == 0 || height == 0) {
        outRgba[0] = 26; outRgba[1] = 26; outRgba[2] = 31; outRgba[3] = 255;
        return;
    }

    while (u < 0.f) u += 1.f;
    while (u >= 1.f) u -= 1.f;
    v = std::clamp(v, 0.f, 1.f);

    const int x = static_cast<int>(u * static_cast<float>(width - 1));
    const int y = static_cast<int>(v * static_cast<float>(height - 1));
    const size_t idx = static_cast<size_t>(y * width + x) * 4;
    outRgba[0] = pixels[idx + 0];
    outRgba[1] = pixels[idx + 1];
    outRgba[2] = pixels[idx + 2];
    outRgba[3] = 255;
}

void directionToEquirectUVFast(f32 dx, f32 dy, f32 dz, f32& u, f32& v) {
    const f32 theta = std::atan2(dx, dz);
    const f32 xz = std::sqrt(dx * dx + dz * dz);
    const f32 phi = std::atan2(dy, xz);
    u = theta / kTwoPi + 0.5f;
    v = 0.5f - phi / 3.14159265f;
}

}  // namespace

bool SkyboxRenderer::loadSource(const std::string& path, SourceImage& out) {
    if (path.empty()) return false;
    if (out.loaded) return true;

    int width = 0;
    int height = 0;
    int channels = 0;
    u8* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) return false;

    out.pixels.assign(data, data + static_cast<size_t>(width * height * 4));
    out.width = static_cast<u32>(width);
    out.height = static_cast<u32>(height);
    out.loaded = true;
    stbi_image_free(data);
    return true;
}

void SkyboxRenderer::renderPixels(SourceImage& source, FrameCache& frame, ImVec2 origin,
                                  ImVec2 panelSize, const SkyboxCamera& camera) {
    const int panelW = std::max(1, static_cast<int>(panelSize.x));
    const int panelH = std::max(1, static_cast<int>(panelSize.y));
    int renderW = panelW;
    int renderH = panelH;
    if (renderW > kSkyboxRasterMaxDim || renderH > kSkyboxRasterMaxDim) {
        const f32 scale = static_cast<f32>(kSkyboxRasterMaxDim) /
                          static_cast<f32>(std::max(renderW, renderH));
        renderW = std::max(1, static_cast<int>(static_cast<f32>(renderW) * scale));
        renderH = std::max(1, static_cast<int>(static_cast<f32>(renderH) * scale));
    }

    frame.origin = origin;
    frame.panelW = panelW;
    frame.panelH = panelH;
    frame.renderW = renderW;
    frame.renderH = renderH;
    frame.camera = camera;
    frame.pixels.resize(static_cast<size_t>(renderW) * static_cast<size_t>(renderH) * 4);

    const Vec3 forward = camera.forward.normalized();
    const Vec3 right = camera.right.normalized();
    const Vec3 up = camera.up.normalized();
    const f32 fx = forward.x, fy = forward.y, fz = forward.z;
    const f32 rightX = right.x, rightY = right.y, rightZ = right.z;
    const f32 upX = up.x, upY = up.y, upZ = up.z;

    const f32 aspect = std::max(camera.aspect, 0.01f);
    const f32 tanHalfFov = std::tan(camera.fovY * 0.5f);
    const f32 invRenderW = 1.f / static_cast<f32>(renderW);
    const f32 invRenderH = 1.f / static_cast<f32>(renderH);

    std::vector<f32> ndcX(renderW);
    for (int x = 0; x < renderW; ++x) {
        ndcX[x] = (2.f * static_cast<f32>(x) * invRenderW - 1.f) * aspect * tanHalfFov;
    }

    for (int y = 0; y < renderH; ++y) {
        const f32 ndcY = (1.f - 2.f * static_cast<f32>(y) * invRenderH) * tanHalfFov;
        const f32 rowDy = fy + upY * ndcY;
        const f32 rowDz = fz + upZ * ndcY;
        const f32 rowDxBase = fx + upX * ndcY;
        u8* row = frame.pixels.data() + static_cast<size_t>(y * renderW) * 4;

        for (int x = 0; x < renderW; ++x) {
            const f32 dx = rowDxBase + rightX * ndcX[x];
            const f32 dy = rowDy + rightY * ndcX[x];
            const f32 dz = rowDz + rightZ * ndcX[x];
            f32 u = 0.f;
            f32 v = 0.f;
            directionToEquirectUVFast(dx, dy, dz, u, v);
            sampleEquirectNearest(source.pixels.data(), source.width, source.height, u, v, row + x * 4);
        }
    }

    frame.valid = true;
    frame.gpuDirty = true;
}

void SkyboxRenderer::destroyGpuTexture(GpuTexture& gpu) {
#ifdef CF_HAS_SDL3
    if (gpu.texture && gpu.texture->GetTexID() != ImTextureID_Invalid) {
        gpu.texture->UnusedFrames = 1;
        gpu.texture->SetStatus(ImTextureStatus_WantDestroy);
        ImGui_ImplSDLGPU3_UpdateTexture(gpu.texture.get());
    }
#endif
    gpu.texture.reset();
    gpu.width = 0;
    gpu.height = 0;
    gpu.sourcePath.clear();
}

void SkyboxRenderer::blitFrame(ImDrawList* dl, FrameCache& frame, GpuTexture& gpu) {
    if (!dl || !frame.valid) return;

#ifdef CF_HAS_SDL3
    const bool sourceChanged = gpu.sourcePath != frame.texturePath;
    const bool sizeChanged = !gpu.texture || gpu.width != frame.renderW || gpu.height != frame.renderH;
    if (sourceChanged || sizeChanged) {
        destroyGpuTexture(gpu);
        gpu.sourcePath = frame.texturePath;
        gpu.width = frame.renderW;
        gpu.height = frame.renderH;
        gpu.texture = std::make_unique<ImTextureData>();
        gpu.texture->Create(ImTextureFormat_RGBA32, frame.renderW, frame.renderH);
        frame.gpuDirty = true;
    }

    if (frame.gpuDirty && gpu.texture) {
        std::memcpy(gpu.texture->GetPixels(), frame.pixels.data(), frame.pixels.size());
        if (gpu.texture->Status == ImTextureStatus_WantCreate) {
            ImGui_ImplSDLGPU3_UpdateTexture(gpu.texture.get());
        } else {
            gpu.texture->UpdateRect.x = 0;
            gpu.texture->UpdateRect.y = 0;
            gpu.texture->UpdateRect.w = static_cast<unsigned short>(frame.renderW);
            gpu.texture->UpdateRect.h = static_cast<unsigned short>(frame.renderH);
            gpu.texture->SetStatus(ImTextureStatus_WantUpdates);
            ImGui_ImplSDLGPU3_UpdateTexture(gpu.texture.get());
        }
        frame.gpuDirty = false;
    }

    if (gpu.texture && gpu.texture->GetTexID() != ImTextureID_Invalid) {
        dl->AddImage(gpu.texture->GetTexRef(), frame.origin,
                     ImVec2(frame.origin.x + static_cast<f32>(frame.panelW),
                            frame.origin.y + static_cast<f32>(frame.panelH)));
    }
#endif
}

void SkyboxRenderer::releaseGpuTextures() {
    destroyGpuTexture(m_gpu);
    m_frame.valid = false;
    m_frame.gpuDirty = false;
}

bool SkyboxRenderer::draw(ImDrawList* drawList, ImVec2 origin, ImVec2 panelSize,
                          const SkyboxCamera& camera, const std::string& texturePath) {
    if (!drawList || texturePath.empty()) return false;

    auto& source = m_sources[texturePath];
    if (!loadSource(texturePath, source)) return false;

    const int panelW = std::max(1, static_cast<int>(panelSize.x));
    const int panelH = std::max(1, static_cast<int>(panelSize.y));
    const bool needsRender = !m_frame.valid
        || m_frame.texturePath != texturePath
        || m_frame.panelW != panelW
        || m_frame.panelH != panelH
        || !m_frame.camera.nearlyEqual(camera);

    if (needsRender) {
        m_frame.texturePath = texturePath;
        renderPixels(source, m_frame, origin, panelSize, camera);
    } else {
        m_frame.origin = origin;
    }

    blitFrame(drawList, m_frame, m_gpu);
    return m_frame.valid;
}

#endif

}  // namespace Caffeine::Render

#include "editor/AssetPreviewRenderer.hpp"
#include "editor/EditorIcons.hpp"
#include "editor/ImGuiGpuTexture.hpp"
#include "assets/MaterialFile.hpp"
#include "assets/MeshCache.hpp"
#include "assets/MeshImportValidator.hpp"
#include "math/Vec3.hpp"

#include <stb/stb_image.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace Caffeine::Editor {

#ifdef CF_HAS_IMGUI

namespace {

constexpr f32 kPi = 3.14159265f;

Vec3 rotatePreviewPoint(const Vec3& p, f32 yaw, f32 pitch) {
    const f32 cy = std::cos(yaw);
    const f32 sy = std::sin(yaw);
    const f32 cp = std::cos(pitch);
    const f32 sp = std::sin(pitch);

    Vec3 yRot(p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy);
    return Vec3(yRot.x, yRot.y * cp - yRot.z * sp, yRot.y * sp + yRot.z * cp);
}

std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool isImagePath(const std::filesystem::path& path) {
    const std::string ext = lowerExtension(path);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
           ext == ".webp" || ext == ".gif";
}

bool isMaterialPath(const std::filesystem::path& path) {
    const std::string ext = lowerExtension(path);
    return ext == ".mat" || ext == ".material";
}

bool isMeshPath(const std::filesystem::path& path, AssetType type) {
    if (type == AssetType::Mesh) return true;
    const std::string ext = lowerExtension(path);
    return ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".dae";
}

bool loadWavPeaks(const std::filesystem::path& path, std::vector<std::pair<float, float>>& peaks,
                  float& durationSec) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    char riff[4]{};
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) return false;

    file.seekg(4, std::ios::cur);
    char wave[4]{};
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) return false;

    u16 audioFormat = 0;
    u16 channels = 0;
    u32 sampleRate = 0;
    u16 bitsPerSample = 0;
    std::vector<u8> pcm;

    while (file && !file.eof()) {
        char chunkId[4]{};
        u32 chunkSize = 0;
        file.read(chunkId, 4);
        file.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (!file) break;

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            file.read(reinterpret_cast<char*>(&audioFormat), 2);
            file.read(reinterpret_cast<char*>(&channels), 2);
            file.read(reinterpret_cast<char*>(&sampleRate), 4);
            file.seekg(6, std::ios::cur);
            file.read(reinterpret_cast<char*>(&bitsPerSample), 2);
            if (chunkSize > 16) {
                file.seekg(static_cast<std::streamoff>(chunkSize - 16), std::ios::cur);
            }
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            pcm.resize(chunkSize);
            file.read(reinterpret_cast<char*>(pcm.data()), chunkSize);
        } else {
            file.seekg(chunkSize, std::ios::cur);
        }
    }

    if (pcm.empty() || audioFormat != 1 || channels == 0 || bitsPerSample == 0 || sampleRate == 0) {
        return false;
    }

    const u32 bytesPerSample = static_cast<u32>(bitsPerSample / 8);
    const u32 frameSize = bytesPerSample * channels;
    if (frameSize == 0) return false;

    const u32 totalFrames = static_cast<u32>(pcm.size() / frameSize);
    durationSec = static_cast<float>(totalFrames) / static_cast<float>(sampleRate);

    constexpr int kPeaks = 120;
    peaks.resize(kPeaks);
    const u32 framesPerPeak = std::max(1u, totalFrames / static_cast<u32>(kPeaks));

    for (int i = 0; i < kPeaks; ++i) {
        float minVal = 0.0f;
        float maxVal = 0.0f;
        const u32 start = static_cast<u32>(i) * framesPerPeak;
        const u32 end = std::min(start + framesPerPeak, totalFrames);
        for (u32 frame = start; frame < end; ++frame) {
            const u64 offset = static_cast<u64>(frame) * frameSize;
            if (offset + bytesPerSample > pcm.size()) break;
            float sample = 0.0f;
            if (bitsPerSample == 16) {
                i16 val = 0;
                std::memcpy(&val, pcm.data() + offset, sizeof(i16));
                sample = static_cast<float>(val) / 32768.0f;
            } else if (bitsPerSample == 8) {
                sample = (static_cast<float>(pcm[offset]) - 128.0f) / 128.0f;
            }
            minVal = std::min(minVal, sample);
            maxVal = std::max(maxVal, sample);
        }
        peaks[static_cast<size_t>(i)] = {minVal, maxVal};
    }
    return true;
}

}  // namespace

AssetPreviewRenderer::~AssetPreviewRenderer() {
    shutdownGpu();
}

void AssetPreviewRenderer::invalidate() {
    m_cachedKey.clear();
    destroyImGuiTexture(m_image.texture);
    m_image.width = 0;
    m_image.height = 0;
    m_image.failed = false;
    m_waveform = {};
    m_material = {};
}

void AssetPreviewRenderer::shutdownGpu() {
    invalidate();
#ifdef CF_HAS_SDL3
    m_pane.shutdown();
    m_thumbs.shutdown();
    if (m_device) {
        for (auto& entry : m_gpuThumbs) {
            if (entry.second.texture) m_device->destroyTexture(entry.second.texture);
        }
    }
    m_gpuThumbs.clear();
    for (auto& entry : m_imageThumbs) {
        destroyImGuiTexture(entry.second.texture);
    }
    m_imageThumbs.clear();
    m_thumbItems.clear();
    m_activeThumbKey.clear();
    m_device = nullptr;
    m_frameCmd = nullptr;
#endif
}

#ifdef CF_HAS_SDL3

namespace {

constexpr u32 kThumbnailPixels = 128;

void downsampleNearest(const u8* src, int sw, int sh, std::vector<u8>& dst, int& dw, int& dh) {
    dw = sw;
    dh = sh;
    if (sw <= 160 && sh <= 160) return;
    if (sw >= sh) {
        dw = 128;
        dh = std::max(1, sh * 128 / sw);
    } else {
        dh = 128;
        dw = std::max(1, sw * 128 / sh);
    }
    dst.resize(static_cast<size_t>(dw * dh * 4));
    for (int y = 0; y < dh; ++y) {
        const int sy = std::min(sh - 1, y * sh / dh);
        for (int x = 0; x < dw; ++x) {
            const int sx = std::min(sw - 1, x * sw / dw);
            const u8* p = src + (static_cast<size_t>(sy) * sw + sx) * 4;
            u8* o = dst.data() + (static_cast<size_t>(y) * dw + x) * 4;
            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
            o[3] = p[3];
        }
    }
}

}  // namespace

void AssetPreviewRenderer::initGpu(RHI::RenderDevice* device) {
    shutdownGpu();
    m_device = device;
    m_pane.setMaterialKeys("__caffeine_asset_preview__.mat", "__caffeine_asset_preview_floor__.mat");
    m_thumbs.setMaterialKeys("__caffeine_asset_thumb__.mat", "__caffeine_asset_thumb_floor__.mat");
    if (!device) return;
    m_pane.init(device);
    m_thumbs.init(device);
}

void AssetPreviewRenderer::setEnvironment(std::string path, f32 exposure) {
    m_envPath = std::move(path);
    m_envExposure = exposure;
}

Render::MaterialPreviewSettings AssetPreviewRenderer::previewSettings(u32 pixelSize) const {
    Render::MaterialPreviewSettings settings;
    settings.yawDegrees = 32.0f;
    settings.pitchDegrees = 14.0f;
    settings.showFloor = true;
    settings.environmentPath = m_envPath;
    settings.environmentExposure = m_envExposure;
    settings.probeResolution = pixelSize >= 256 ? 128u : 64u;
    return settings;
}

bool AssetPreviewRenderer::drawGpuTexture(RHI::Texture* texture, ImVec2 size) {
    if (!texture || !texture->handle || size.x < 1.0f || size.y < 1.0f) return false;
    const float side = std::min(size.x, size.y);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 p0(origin.x + (size.x - side) * 0.5f, origin.y + (size.y - side) * 0.5f);
    ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(texture->handle), p0,
                                         ImVec2(p0.x + side, p0.y + side));
    ImGui::Dummy(size);
    return true;
}

bool AssetPreviewRenderer::thumbnailReady(const std::string& key) const {
    const auto image = m_imageThumbs.find(key);
    if (image != m_imageThumbs.end() && image->second.ready) return true;
    const auto gpu = m_gpuThumbs.find(key);
    return gpu != m_gpuThumbs.end() && gpu->second.ready;
}

void AssetPreviewRenderer::prepareThumbnails(
    const std::vector<std::pair<std::filesystem::path, AssetType>>& items, const std::string& projectRoot) {
    m_thumbItems = items;
    if (!m_activeThumbKey.empty()) {
        bool stillVisible = false;
        for (const auto& item : m_thumbItems) {
            if (item.first.string() == m_activeThumbKey) {
                stillVisible = true;
                break;
            }
        }
        if (!stillVisible) m_activeThumbKey.clear();
    }
    pumpThumbnail(projectRoot);
}

void AssetPreviewRenderer::pumpThumbnail(const std::string& projectRoot) {
    if (m_activeThumbKey.empty()) {
        for (const auto& item : m_thumbItems) {
            const bool image = isImagePath(item.first) || item.second == AssetType::Texture;
            const bool mesh = isMeshPath(item.first, item.second);
            const bool material = isMaterialPath(item.first);
            if (!image && !mesh && !material) continue;
            const std::string key = item.first.string();
            if (image) {
                const auto it = m_imageThumbs.find(key);
                if (it != m_imageThumbs.end() && (it->second.ready || it->second.failed)) continue;
            } else {
                const auto it = m_gpuThumbs.find(key);
                if (it != m_gpuThumbs.end() && (it->second.ready || it->second.failed)) continue;
            }
            m_activeThumbKey = key;
            m_activeThumbIsImage = image;
            m_activeThumbIsMesh = mesh;
            break;
        }
    }
    if (m_activeThumbKey.empty()) return;

    if (m_activeThumbIsImage) {
        ImageThumb& thumb = m_imageThumbs[m_activeThumbKey];
        if (!thumb.texture && !thumb.failed) {
            int width = 0;
            int height = 0;
            int channels = 0;
            u8* pixels = stbi_load(m_activeThumbKey.c_str(), &width, &height, &channels, 4);
            if (!pixels || width <= 0 || height <= 0) {
                thumb.failed = true;
                if (pixels) stbi_image_free(pixels);
            } else {
                std::vector<u8> scaled;
                int dw = width;
                int dh = height;
                downsampleNearest(pixels, width, height, scaled, dw, dh);
                const u8* src = scaled.empty() ? pixels : scaled.data();
                thumb.texture = std::make_unique<ImTextureData>();
                thumb.texture->Create(ImTextureFormat_RGBA32, dw, dh);
                std::memcpy(thumb.texture->GetPixels(), src, static_cast<size_t>(dw * dh * 4));
                thumb.texture->SetStatus(ImTextureStatus_WantCreate);
                thumb.width = dw;
                thumb.height = dh;
                stbi_image_free(pixels);
            }
        }
        if (thumb.texture && thumb.texture->Status == ImTextureStatus_WantCreate) {
            ImGui_ImplSDLGPU3_UpdateTexture(thumb.texture.get());
        }
        if (thumb.failed || (thumb.texture && thumb.texture->Status == ImTextureStatus_OK)) {
            thumb.ready = thumb.texture && thumb.texture->Status == ImTextureStatus_OK;
            m_activeThumbKey.clear();
        }
        return;
    }

    if (!m_frameCmd || !m_thumbs.isReady() || !m_device) return;

    const auto settings = previewSettings(kThumbnailPixels);
    bool rendered = false;
    if (m_activeThumbIsMesh) {
        rendered = m_thumbs.renderMesh(m_frameCmd, m_activeThumbKey, kThumbnailPixels, settings, projectRoot);
    } else {
        Assets::MaterialSurface surface;
        if (Assets::loadMaterialFile(m_activeThumbKey, surface)) {
            rendered = m_thumbs.render(m_frameCmd, surface, kThumbnailPixels, settings, projectRoot);
        }
    }
    if (!rendered) {
        m_gpuThumbs[m_activeThumbKey].failed = true;
        m_activeThumbKey.clear();
        return;
    }

    RHI::Texture* source = m_thumbs.colorTexture();
    GpuThumb& thumb = m_gpuThumbs[m_activeThumbKey];
    if (source && source->handle) {
        if (!thumb.texture || thumb.texture->width != kThumbnailPixels) {
            if (thumb.texture) m_device->destroyTexture(thumb.texture);
            RHI::TextureDesc desc;
            desc.width = kThumbnailPixels;
            desc.height = kThumbnailPixels;
            desc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
            desc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
            thumb.texture = m_device->createTexture(desc);
        }
        if (thumb.texture && thumb.texture->handle) {
            m_frameCmd->copyTexture(source, thumb.texture, kThumbnailPixels, kThumbnailPixels);
            thumb.ready = true;
        }
    }
    if (!m_thumbs.wantsMoreFrames()) m_activeThumbKey.clear();
}

bool AssetPreviewRenderer::drawThumbnail(const std::filesystem::path& path, AssetType type, float size) {
    if (size < 1.0f) return false;
    const std::string key = path.string();
    if (isImagePath(path) || type == AssetType::Texture) {
        const auto it = m_imageThumbs.find(key);
        if (it == m_imageThumbs.end() || !it->second.texture) return false;
        if (it->second.texture->Status == ImTextureStatus_WantCreate) {
            ImGui_ImplSDLGPU3_UpdateTexture(it->second.texture.get());
        }
        const ImTextureRef texRef = it->second.texture->GetTexRef();
        if (it->second.texture->Status != ImTextureStatus_OK ||
            (texRef._TexData == nullptr && texRef._TexID == ImTextureID_Invalid)) {
            return false;
        }
        ImGui::Image(texRef, ImVec2(size, size));
        return true;
    }
    if (!isMaterialPath(path) && !isMeshPath(path, type)) return false;
    const auto it = m_gpuThumbs.find(key);
    if (it == m_gpuThumbs.end() || !it->second.ready || !it->second.texture || !it->second.texture->handle) {
        return false;
    }
    ImGui::Image(reinterpret_cast<ImTextureID>(it->second.texture->handle), ImVec2(size, size));
    return true;
}

#endif

void AssetPreviewRenderer::drawPreviewFrame(ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 rectMax(origin.x + size.x, origin.y + size.y);
    dl->AddRectFilled(origin, rectMax, IM_COL32(18, 20, 26, 255), 6.0f);
    dl->AddRect(origin, rectMax, IM_COL32(70, 76, 92, 255), 6.0f);
    ImGui::Dummy(size);
}

void AssetPreviewRenderer::renderVisual(const std::filesystem::path& path, AssetType type,
                                        const std::string& projectRoot, ImVec2 size) {
    size.x = std::max(80.0f, size.x);
    size.y = std::max(120.0f, size.y);

    drawPreviewFrame(size);
    const ImVec2 origin = ImGui::GetItemRectMin();
    const ImVec2 inner(origin.x + 8.0f, origin.y + 8.0f);
    const ImVec2 innerSize(size.x - 16.0f, size.y - 16.0f);

    ImGui::SetCursorScreenPos(inner);

    if (isImagePath(path) || type == AssetType::Texture) {
        renderImagePreview(path, innerSize);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + size.y + 6.0f));
        return;
    }

    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".mat" || ext == ".material") {
            renderMaterialPreview(path, projectRoot, innerSize);
            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + size.y + 6.0f));
            return;
        }
    }

    switch (type) {
        case AssetType::Mesh:
            renderMeshPreview(path, projectRoot, innerSize);
            break;
        case AssetType::Audio:
            renderAudioPreview(path, innerSize);
            break;
        case AssetType::Scene:
            renderScenePreview(path, innerSize);
            break;
        default:
            renderFallbackIcon(type, path, innerSize);
            break;
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + size.y + 6.0f));
}

bool AssetPreviewRenderer::ensureImageLoaded(const std::filesystem::path& path) {
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    const std::string key = ec ? path.string() : canonical.string();
    if (m_cachedKey == key && (m_image.texture || m_image.failed)) {
        return m_image.texture != nullptr;
    }

    invalidate();
    m_cachedKey = key;

    int width = 0;
    int height = 0;
    int channels = 0;
    u8* pixels = stbi_load(key.c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        m_image.failed = true;
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    m_image.width = width;
    m_image.height = height;
    m_image.texture = std::make_unique<ImTextureData>();
    m_image.texture->Create(ImTextureFormat_RGBA32, width, height);
    std::memcpy(m_image.texture->GetPixels(), pixels, static_cast<size_t>(width * height * 4));
    m_image.texture->SetStatus(ImTextureStatus_WantCreate);
    ImGui_ImplSDLGPU3_UpdateTexture(m_image.texture.get());
    stbi_image_free(pixels);
    return true;
}

void AssetPreviewRenderer::renderImagePreview(const std::filesystem::path& path, ImVec2 size) {
    if (!ensureImageLoaded(path)) {
        renderFallbackIcon(AssetType::Texture, path, size);
        return;
    }

    if (m_image.texture->Status == ImTextureStatus_WantCreate) {
        ImGui_ImplSDLGPU3_UpdateTexture(m_image.texture.get());
    }

    const ImTextureRef texRef = m_image.texture->GetTexRef();
    if (m_image.texture->Status != ImTextureStatus_OK ||
        (texRef._TexData == nullptr && texRef._TexID == ImTextureID_Invalid)) {
        renderFallbackIcon(AssetType::Texture, path, size);
        return;
    }

    const float aspect = static_cast<float>(m_image.width) / static_cast<float>(m_image.height);
    ImVec2 drawSize = size;
    if (aspect > size.x / size.y) {
        drawSize.y = size.x / aspect;
    } else {
        drawSize.x = size.y * aspect;
    }
    drawSize.x = std::max(1.0f, drawSize.x);
    drawSize.y = std::max(1.0f, drawSize.y);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 offset((size.x - drawSize.x) * 0.5f, (size.y - drawSize.y) * 0.5f);
    const ImVec2 p0(origin.x + offset.x, origin.y + offset.y);
    ImGui::GetWindowDrawList()->AddImage(texRef, p0, ImVec2(p0.x + drawSize.x, p0.y + drawSize.y));
    ImGui::Dummy(size);
}

void AssetPreviewRenderer::renderMeshPreview(const std::filesystem::path& path,
                                             const std::string& projectRoot, ImVec2 size) {
    auto& meshCache = Assets::MeshCache::getInstance();
    Assets::Mesh3D* mesh = meshCache.getMesh(path.string(), projectRoot);
    if (!mesh || mesh->vertices.empty()) {
        renderFallbackIcon(AssetType::Mesh, path, size);
        return;
    }

#ifdef CF_HAS_SDL3
    if (m_frameCmd && m_pane.isReady()) {
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        const float dpi = std::max(1.0f, std::max(scale.x, scale.y));
        u32 pixels = std::clamp(static_cast<u32>(std::ceil(std::min(size.x, size.y) * dpi)), 128u, 512u);
        pixels = (pixels + 3u) & ~3u;
        if (m_pane.renderMesh(m_frameCmd, path.string(), pixels, previewSettings(pixels), projectRoot) &&
            drawGpuTexture(m_pane.colorTexture(), size)) {
            return;
        }
    }
#endif

    if (!mesh->baseColorTexture.empty() && mesh->textureWidth > 0 && mesh->textureHeight > 0) {
        const std::string key = path.string() + "#embedded";
        if (m_cachedKey != key) {
            invalidate();
            m_cachedKey = key;
            m_image.width = static_cast<int>(mesh->textureWidth);
            m_image.height = static_cast<int>(mesh->textureHeight);
            m_image.texture = std::make_unique<ImTextureData>();
            m_image.texture->Create(ImTextureFormat_RGBA32, m_image.width, m_image.height);

            const int channels = mesh->textureChannels > 0 ? mesh->textureChannels : 4;
            u8* dst = static_cast<u8*>(m_image.texture->GetPixels());
            const size_t pixelCount = static_cast<size_t>(m_image.width * m_image.height);
            if (channels == 4) {
                std::memcpy(dst, mesh->baseColorTexture.data(), pixelCount * 4);
            } else if (channels == 3) {
                for (size_t i = 0; i < pixelCount; ++i) {
                    dst[i * 4 + 0] = mesh->baseColorTexture[i * 3 + 0];
                    dst[i * 4 + 1] = mesh->baseColorTexture[i * 3 + 1];
                    dst[i * 4 + 2] = mesh->baseColorTexture[i * 3 + 2];
                    dst[i * 4 + 3] = 255;
                }
            } else {
                m_image.failed = true;
            }

            if (!m_image.failed) {
                m_image.texture->SetStatus(ImTextureStatus_WantCreate);
                ImGui_ImplSDLGPU3_UpdateTexture(m_image.texture.get());
            }
        }

        if (m_image.texture) {
            if (m_image.texture->Status == ImTextureStatus_WantCreate) {
                ImGui_ImplSDLGPU3_UpdateTexture(m_image.texture.get());
            }
            const ImTextureRef texRef = m_image.texture->GetTexRef();
            if (m_image.texture->Status == ImTextureStatus_OK &&
                (texRef._TexData != nullptr || texRef._TexID != ImTextureID_Invalid)) {
                const float aspect =
                    static_cast<float>(m_image.width) / static_cast<float>(m_image.height);
                ImVec2 drawSize = size;
                if (aspect > size.x / size.y) {
                    drawSize.y = size.x / aspect;
                } else {
                    drawSize.x = size.y * aspect;
                }
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const ImVec2 offset((size.x - drawSize.x) * 0.5f, (size.y - drawSize.y) * 0.5f);
                const ImVec2 p0(origin.x + offset.x, origin.y + offset.y);
                ImGui::GetWindowDrawList()->AddImage(texRef, p0, ImVec2(p0.x + drawSize.x, p0.y + drawSize.y));
                ImGui::Dummy(size);
                return;
            }
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.55f);

    const Vec3 center3 = mesh->bounds.center();
    const Vec3 extents = mesh->bounds.extents();
    const f32 radius = std::max({extents.x, extents.y, extents.z, 0.01f});
    const f32 scale = std::min(size.x, size.y) * 0.38f / radius;

    const f32 yaw = static_cast<f32>(ImGui::GetTime()) * 0.7f;
    const f32 pitch = 0.35f;

    auto project = [&](const Vec3& p) -> ImVec2 {
        Vec3 local = p - center3;
        Vec3 rotated = rotatePreviewPoint(local, yaw, pitch);
        return ImVec2(center.x + rotated.x * scale, center.y - rotated.y * scale);
    };

    const size_t triCount = mesh->indices.size() / 3;
    const size_t step = triCount > 2500 ? std::max<size_t>(1, triCount / 2500) : 1;
    const ImU32 fillCol = IM_COL32(90, 120, 170, 35);
    const ImU32 lineCol = IM_COL32(170, 190, 220, 220);

    for (size_t t = 0; t < triCount; t += step) {
        const u32 i0 = mesh->indices[t * 3 + 0];
        const u32 i1 = mesh->indices[t * 3 + 1];
        const u32 i2 = mesh->indices[t * 3 + 2];
        if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) {
            continue;
        }

        const ImVec2 a = project(mesh->vertices[i0].position);
        const ImVec2 b = project(mesh->vertices[i1].position);
        const ImVec2 c = project(mesh->vertices[i2].position);
        dl->AddTriangleFilled(a, b, c, fillCol);
        dl->AddLine(a, b, lineCol, 1.0f);
        dl->AddLine(b, c, lineCol, 1.0f);
        dl->AddLine(c, a, lineCol, 1.0f);
    }

    dl->AddText(ImVec2(origin.x + 8.0f, origin.y + size.y - 20.0f), IM_COL32(150, 160, 180, 255),
                "3D preview");
    ImGui::Dummy(size);
}

bool AssetPreviewRenderer::ensureWaveformLoaded(const std::filesystem::path& path) {
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    const std::string key = ec ? path.string() : canonical.string();
    if (m_cachedKey == key && (!m_waveform.peaks.empty() || m_waveform.failed)) {
        return !m_waveform.peaks.empty();
    }

    invalidate();
    m_cachedKey = key;

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".wav") {
        m_waveform.failed = true;
        return false;
    }

    if (!loadWavPeaks(path, m_waveform.peaks, m_waveform.durationSec)) {
        m_waveform.failed = true;
        return false;
    }
    return true;
}

void AssetPreviewRenderer::renderAudioPreview(const std::filesystem::path& path, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    if (!ensureWaveformLoaded(path)) {
        if (EditorIcons::hasIcon("bell")) {
            const float iconSize = std::min(size.x, size.y) * 0.35f;
            ImGui::SetCursorScreenPos(
                ImVec2(origin.x + (size.x - iconSize) * 0.5f, origin.y + (size.y - iconSize) * 0.35f));
            EditorIcons::image("bell", iconSize);
            dl->AddText(ImVec2(origin.x + 8.0f, origin.y + size.y - 36.0f), IM_COL32(150, 160, 180, 255),
                        "Waveform preview for .wav");
        } else {
            renderFallbackIcon(AssetType::Audio, path, size);
        }
        ImGui::Dummy(size);
        return;
    }

    const float midY = origin.y + size.y * 0.55f;
    const float halfH = size.y * 0.35f;
    const float stepX = size.x / static_cast<float>(m_waveform.peaks.size());

    dl->AddLine(ImVec2(origin.x, midY), ImVec2(origin.x + size.x, midY), IM_COL32(70, 76, 92, 255), 1.0f);

    for (size_t i = 0; i < m_waveform.peaks.size(); ++i) {
        const float x = origin.x + static_cast<float>(i) * stepX;
        const float y0 = midY - m_waveform.peaks[i].second * halfH;
        const float y1 = midY - m_waveform.peaks[i].first * halfH;
        dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), IM_COL32(120, 190, 255, 255), 1.5f);
    }

    char durationBuf[32];
    std::snprintf(durationBuf, sizeof(durationBuf), "%.1fs", m_waveform.durationSec);
    dl->AddText(ImVec2(origin.x + 8.0f, origin.y + size.y - 20.0f), IM_COL32(150, 160, 180, 255),
                durationBuf);
    ImGui::Dummy(size);
}

void AssetPreviewRenderer::renderScenePreview(const std::filesystem::path& path, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    std::ifstream in(path, std::ios::binary);
    CafHeader header{};
    if (in) {
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
    }

    const bool valid = in.good() && header.magic == CafHeader::kMagic;
    const char* icon = "account";
    if (valid) {
        switch (header.type) {
            case AssetType::Texture: icon = "beer"; break;
            case AssetType::Audio: icon = "bell"; break;
            case AssetType::Mesh: icon = "arrows-diagonal"; break;
            default: break;
        }
    }

    const float iconSize = std::min(size.x, size.y) * 0.32f;
    ImGui::SetCursorScreenPos(
        ImVec2(origin.x + (size.x - iconSize) * 0.5f, origin.y + (size.y - iconSize) * 0.35f));
    if (EditorIcons::hasIcon(icon)) {
        EditorIcons::image(icon, iconSize);
    }

    const char* label = valid ? "CAF scene asset" : "Scene file";
    dl->AddText(ImVec2(origin.x + 8.0f, origin.y + size.y - 20.0f), IM_COL32(150, 160, 180, 255), label);
    ImGui::Dummy(size);
}

bool AssetPreviewRenderer::ensureMaterialLoaded(const std::filesystem::path& path) {
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    const std::string key = ec ? path.string() : canonical.string();
    if (m_cachedKey == key && (m_material.loaded || m_material.failed)) {
        return m_material.loaded;
    }

    invalidate();
    m_cachedKey = key;
    m_material.loaded = Assets::loadMaterialFile(path, m_material.surface);
    m_material.failed = !m_material.loaded;
    return m_material.loaded;
}

std::filesystem::path resolveProjectRelative(const std::string& projectRoot,
                                             const std::string& relative) {
    if (relative.empty()) return {};
    std::filesystem::path p(relative);
    if (p.is_absolute()) return p;
    if (projectRoot.empty()) return p;
    return std::filesystem::path(projectRoot) / p;
}

void AssetPreviewRenderer::renderMaterialPreview(const std::filesystem::path& path,
                                                 const std::string& projectRoot, ImVec2 size) {
    if (!ensureMaterialLoaded(path)) {
        renderFallbackIcon(AssetType::Unknown, path, size);
        return;
    }

    const Assets::MaterialSurface& surface = m_material.surface;
#ifdef CF_HAS_SDL3
    if (m_frameCmd && m_pane.isReady()) {
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        const float dpi = std::max(1.0f, std::max(scale.x, scale.y));
        u32 pixels = std::clamp(static_cast<u32>(std::ceil(std::min(size.x, size.y) * dpi)), 128u, 512u);
        pixels = (pixels + 3u) & ~3u;
        if (m_pane.render(m_frameCmd, surface, pixels, previewSettings(pixels), projectRoot) &&
            drawGpuTexture(m_pane.colorTexture(), size)) {
            return;
        }
    }
#endif
    if (!surface.albedoMap.empty()) {
        const std::filesystem::path texPath = resolveProjectRelative(projectRoot, surface.albedoMap);
        std::error_code ec;
        if (std::filesystem::exists(texPath, ec)) {
            renderImagePreview(texPath, size);
            return;
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.52f);
    const float radius = std::min(size.x, size.y) * 0.38f;

    const auto toByte = [](f32 v) {
        return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
    };
    const ImU32 base = IM_COL32(toByte(surface.albedo.x), toByte(surface.albedo.y),
                                toByte(surface.albedo.z), 255);
    const ImU32 shade = IM_COL32(toByte(surface.albedo.x * 0.55f), toByte(surface.albedo.y * 0.55f),
                                 toByte(surface.albedo.z * 0.55f), 255);
    const ImU32 highlight =
        IM_COL32(toByte(std::min(surface.albedo.x * 1.25f, 1.0f)),
                 toByte(std::min(surface.albedo.y * 1.25f, 1.0f)),
                 toByte(std::min(surface.albedo.z * 1.25f, 1.0f)), 255);

    dl->AddCircleFilled(center, radius, shade);
    dl->AddCircleFilled(ImVec2(center.x - radius * 0.28f, center.y - radius * 0.32f), radius * 0.55f,
                        highlight);
    dl->AddCircle(center, radius, base, 0, 2.0f);

    if (surface.metallic > 0.05f) {
        dl->AddCircle(center, radius * 0.72f, IM_COL32(220, 230, 255, 90), 0, 1.5f);
    }
    if (surface.emissionStrength > 0.01f) {
        const ImU32 glow = IM_COL32(toByte(surface.emission.x), toByte(surface.emission.y),
                                    toByte(surface.emission.z), 80);
        dl->AddCircleFilled(center, radius * 1.05f, glow);
    }

    char caption[96];
    std::snprintf(caption, sizeof(caption), "%s  M%.2f R%.2f", surface.name.c_str(),
                  surface.metallic, surface.roughness);
    dl->AddText(ImVec2(origin.x + 6.0f, origin.y + size.y - 34.0f), IM_COL32(150, 160, 180, 255),
                caption);
    ImGui::Dummy(size);
}

void AssetPreviewRenderer::renderFallbackIcon(AssetType type, const std::filesystem::path& path,
                                              ImVec2 size) {
    const char* icon = "alert-circle";
    switch (type) {
        case AssetType::Texture: icon = "beer"; break;
        case AssetType::Audio: icon = "bell"; break;
        case AssetType::Mesh: icon = "arrows-diagonal"; break;
        case AssetType::Scene: icon = "account"; break;
        default:
            if (path.extension() == ".lua") icon = "at";
            else if (path.extension() == ".mat" || path.extension() == ".material") icon = "beer-alt";
            break;
    }

    const float iconSize = std::min(size.x, size.y) * 0.34f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(
        ImVec2(origin.x + (size.x - iconSize) * 0.5f, origin.y + (size.y - iconSize) * 0.38f));
    if (EditorIcons::hasIcon(icon)) {
        EditorIcons::image(icon, iconSize);
    } else {
        ImGui::TextUnformatted("[asset]");
    }
    ImGui::Dummy(size);
}

#endif

}  // namespace Caffeine::Editor

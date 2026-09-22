#include "editor/AssetPreviewRenderer.hpp"
#include "editor/EditorIcons.hpp"
#include "editor/ImGuiGpuTexture.hpp"
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

bool isImagePath(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
           ext == ".webp" || ext == ".gif";
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

void AssetPreviewRenderer::invalidate() {
    m_cachedKey.clear();
    destroyImGuiTexture(m_image.texture);
    m_image.width = 0;
    m_image.height = 0;
    m_image.failed = false;
    m_waveform = {};
}

void AssetPreviewRenderer::shutdownGpu() {
    invalidate();
}

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
        return;
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
    if (texRef._TexData == nullptr && texRef._TexID == ImTextureID_Invalid) {
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

    const ImVec2 offset((size.x - drawSize.x) * 0.5f, (size.y - drawSize.y) * 0.5f);
    ImGui::SetCursorPos(ImGui::GetCursorPos() + offset);
    ImGui::Image(texRef, drawSize);
}

void AssetPreviewRenderer::renderMeshPreview(const std::filesystem::path& path,
                                             const std::string& projectRoot, ImVec2 size) {
    auto& meshCache = Assets::MeshCache::getInstance();
    Assets::Mesh3D* mesh = meshCache.getMesh(path.string(), projectRoot);
    if (!mesh || mesh->vertices.empty()) {
        renderFallbackIcon(AssetType::Mesh, path, size);
        return;
    }

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
            if (texRef._TexData != nullptr || texRef._TexID != ImTextureID_Invalid) {
                const float aspect =
                    static_cast<float>(m_image.width) / static_cast<float>(m_image.height);
                ImVec2 drawSize = size;
                if (aspect > size.x / size.y) {
                    drawSize.y = size.x / aspect;
                } else {
                    drawSize.x = size.y * aspect;
                }
                const ImVec2 offset((size.x - drawSize.x) * 0.5f, (size.y - drawSize.y) * 0.5f);
                ImGui::SetCursorPos(ImGui::GetCursorPos() + offset);
                ImGui::Image(texRef, drawSize);
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

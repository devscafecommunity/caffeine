#include "editor/ImageManagerPanel.hpp"

#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#ifdef CF_HAS_SDL3
#include "editor/ImGuiGpuTexture.hpp"
#endif
#endif

#include <algorithm>
#include <cstring>

namespace Caffeine::Editor {

ImageManagerPanel::~ImageManagerPanel() {
#ifdef CF_HAS_SDL3
    destroyImGuiTexture(m_texture);
#endif
}

namespace {

bool isImageExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
           ext == ".gif";
}

}  // namespace

void ImageManagerPanel::openPath(const std::filesystem::path& path) {
    m_open = true;
    m_path = path;
    m_pixels.clear();
    m_width = 0;
    m_height = 0;
    m_status.clear();
    m_zoom = 1.0f;
    m_panX = 0.0f;
    m_panY = 0.0f;

    int channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &m_width, &m_height, &channels, 4);
    if (!pixels || m_width <= 0 || m_height <= 0) {
        m_status = "Could not read image.";
        if (pixels) stbi_image_free(pixels);
        m_width = 0;
        m_height = 0;
        return;
    }
    const size_t bytes = static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4;
    m_pixels.assign(pixels, pixels + bytes);
    stbi_image_free(pixels);
#ifdef CF_HAS_SDL3
    m_gpuDirty = true;
#endif
}

void ImageManagerPanel::rebuildGpu() {
#ifdef CF_HAS_SDL3
    if (!m_gpuDirty || m_pixels.empty() || m_width <= 0 || m_height <= 0) return;
    destroyImGuiTexture(m_texture);
    m_texture = std::make_unique<ImTextureData>();
    m_texture->Create(ImTextureFormat_RGBA32, m_width, m_height);
    auto* dst = static_cast<unsigned char*>(m_texture->GetPixels());
    const size_t count = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    for (size_t i = 0; i < count; ++i) {
        dst[i * 4 + 0] = m_showR ? m_pixels[i * 4 + 0] : 0;
        dst[i * 4 + 1] = m_showG ? m_pixels[i * 4 + 1] : 0;
        dst[i * 4 + 2] = m_showB ? m_pixels[i * 4 + 2] : 0;
        dst[i * 4 + 3] = m_showA ? m_pixels[i * 4 + 3] : 255;
    }
    m_texture->SetStatus(ImTextureStatus_WantCreate);
    ImGui_ImplSDLGPU3_UpdateTexture(m_texture.get());
    m_gpuDirty = false;
#else
    (void)this;
#endif
}

void ImageManagerPanel::flipHorizontal() {
    if (m_width <= 1 || m_height <= 0) return;
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width / 2; ++x) {
            const size_t a = static_cast<size_t>(y * m_width + x) * 4;
            const size_t b = static_cast<size_t>(y * m_width + (m_width - 1 - x)) * 4;
            for (int c = 0; c < 4; ++c) std::swap(m_pixels[a + c], m_pixels[b + c]);
        }
    }
#ifdef CF_HAS_SDL3
    m_gpuDirty = true;
#endif
}

void ImageManagerPanel::flipVertical() {
    if (m_height <= 1 || m_width <= 0) return;
    const size_t stride = static_cast<size_t>(m_width) * 4;
    for (int y = 0; y < m_height / 2; ++y) {
        unsigned char* rowA = m_pixels.data() + static_cast<size_t>(y) * stride;
        unsigned char* rowB = m_pixels.data() + static_cast<size_t>(m_height - 1 - y) * stride;
        for (size_t i = 0; i < stride; ++i) std::swap(rowA[i], rowB[i]);
    }
#ifdef CF_HAS_SDL3
    m_gpuDirty = true;
#endif
}

bool ImageManagerPanel::savePng(const std::filesystem::path& path) const {
    if (m_pixels.empty() || m_width <= 0 || m_height <= 0) return false;
    std::filesystem::path target = path;
    if (target.extension() != ".png") target += ".png";
    std::error_code ec;
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
    return stbi_write_png(target.string().c_str(), m_width, m_height, 4, m_pixels.data(), m_width * 4) != 0;
}

void ImageManagerPanel::onImGuiRender(EditorContext& ctx) {
#ifdef CF_HAS_IMGUI
    if (!ctx.imageToOpen.empty()) {
        openPath(ctx.imageToOpen);
        ctx.imageToOpen.clear();
    }
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(860.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Image Manager", &m_open)) {
        ImGui::End();
        return;
    }

    const std::filesystem::path root =
        !ctx.assetRootPath.empty() ? ctx.assetRootPath : ctx.projectRootPath;
    if (ImGui::Button("Refresh library") && !root.empty()) {
        m_library.clear();
        std::error_code ec;
        if (std::filesystem::exists(root, ec)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
                if (ec || m_library.size() >= 400) break;
                if (!entry.is_regular_file()) continue;
                if (isImageExtension(entry.path())) m_library.push_back(entry.path());
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu images", m_library.size());

    const float listW = 220.0f;
    ImGui::BeginChild("image_library", ImVec2(listW, 0), true);
    for (size_t i = 0; i < m_library.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool selected = m_library[i] == m_path;
        if (ImGui::Selectable(m_library[i].filename().string().c_str(), selected)) {
            openPath(m_library[i]);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("image_canvas_tools", ImVec2(0, 0), false);
    if (m_width > 0) {
        ImGui::Text("%s", m_path.filename().string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%d x %d", m_width, m_height);
        if (ImGui::Checkbox("R", &m_showR)) {
#ifdef CF_HAS_SDL3
            m_gpuDirty = true;
#endif
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("G", &m_showG)) {
#ifdef CF_HAS_SDL3
            m_gpuDirty = true;
#endif
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("B", &m_showB)) {
#ifdef CF_HAS_SDL3
            m_gpuDirty = true;
#endif
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("A", &m_showA)) {
#ifdef CF_HAS_SDL3
            m_gpuDirty = true;
#endif
        }
        ImGui::SameLine();
        if (ImGui::Button("Flip H")) flipHorizontal();
        ImGui::SameLine();
        if (ImGui::Button("Flip V")) flipVertical();
        ImGui::SameLine();
        if (ImGui::Button("Save PNG")) {
            std::filesystem::path target = m_path;
            target.replace_extension(".png");
            m_status = savePng(target) ? "Saved " + target.filename().string() : "Save failed.";
            ctx.assetBrowserDirty = true;
        }
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Zoom", &m_zoom, 0.1f, 8.0f, "%.2f");
    } else {
        ImGui::TextDisabled("Open an image from the library or double-click one in the asset browser.");
    }
    if (!m_status.empty()) ImGui::TextWrapped("%s", m_status.c_str());

#ifdef CF_HAS_SDL3
    rebuildGpu();
#endif

    const ImVec2 canvas = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("image_canvas", canvas);
    if (ImGui::IsItemHovered()) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) m_zoom = std::clamp(m_zoom + wheel * 0.1f, 0.1f, 8.0f);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            m_panX += ImGui::GetIO().MouseDelta.x;
            m_panY += ImGui::GetIO().MouseDelta.y;
        }
    }
    const ImVec2 origin = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y), IM_COL32(22, 24, 30, 255));
    draw->PushClipRect(origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y), true);
#ifdef CF_HAS_SDL3
    if (m_texture && m_texture->Status == ImTextureStatus_OK && m_width > 0) {
        const ImTextureRef tex = m_texture->GetTexRef();
        const float drawW = static_cast<float>(m_width) * m_zoom;
        const float drawH = static_cast<float>(m_height) * m_zoom;
        const ImVec2 p0(origin.x + (canvas.x - drawW) * 0.5f + m_panX,
                        origin.y + (canvas.y - drawH) * 0.5f + m_panY);
        draw->AddImage(tex, p0, ImVec2(p0.x + drawW, p0.y + drawH));
    }
#endif
    draw->PopClipRect();
    ImGui::EndChild();
    ImGui::End();
#else
    (void)ctx;
#endif
}

}  // namespace Caffeine::Editor

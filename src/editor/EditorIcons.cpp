#include "editor/EditorIcons.hpp"
#include "editor/ImGuiGpuTexture.hpp"
#include "editor/EditorPaths.hpp"

#include <stb/stb_image.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef CF_HAS_SDL3
#include <imgui_impl_sdlgpu3.h>
#endif

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

namespace Caffeine::Editor {

#ifdef CF_HAS_IMGUI
std::unordered_map<std::string, EditorIcons::IconEntry> EditorIcons::s_cache;
#endif

void EditorIcons::init() {
    EditorPaths::init();
}

void EditorIcons::shutdown() {
#ifdef CF_HAS_IMGUI
    for (auto& [_, entry] : s_cache) {
        destroyImGuiTexture(entry.texture);
    }
    s_cache.clear();
#endif
}

#ifdef CF_HAS_IMGUI

std::string EditorIcons::preprocessSvg(std::string svg) {
    while (true) {
        const size_t start = svg.find("<animate");
        if (start == std::string::npos) break;
        const size_t end = svg.find("/>", start);
        if (end == std::string::npos) break;
        svg.erase(start, end - start + 2);
    }
    while (true) {
        const size_t start = svg.find("<animateTransform");
        if (start == std::string::npos) break;
        const size_t end = svg.find("/>", start);
        if (end == std::string::npos) break;
        svg.erase(start, end - start + 2);
    }

    auto replaceAll = [&](const char* from, const char* to) {
        std::string needle(from);
        size_t pos = 0;
        while ((pos = svg.find(needle, pos)) != std::string::npos) {
            svg.replace(pos, needle.size(), to);
            pos += std::strlen(to);
        }
    };
    replaceAll("currentColor", "#E8E4E0");
    replaceAll("stroke-dasharray", "data-dash");
    replaceAll("stroke-dashoffset", "data-dashoff");
    return svg;
}

bool EditorIcons::rasterizePng(const std::filesystem::path& path, IconEntry& out) {
    int width = 0;
    int height = 0;
    int channels = 0;
    u8* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    out.width = width;
    out.height = height;
    out.texture = std::make_unique<ImTextureData>();
    out.texture->Create(ImTextureFormat_RGBA32, width, height);
    std::memcpy(out.texture->GetPixels(), pixels, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    stbi_image_free(pixels);
    out.texture->SetStatus(ImTextureStatus_WantCreate);
#ifdef CF_HAS_SDL3
    ImGui_ImplSDLGPU3_UpdateTexture(out.texture.get());
#endif
    out.failed = out.texture->GetTexID() == ImTextureID_Invalid;
    return !out.failed;
}

bool EditorIcons::rasterizeSvg(const std::string& path, IconEntry& out, int sizePx) {
    std::ifstream file(path);
    if (!file) return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string svg = preprocessSvg(buffer.str());

    NSVGimage* image = nsvgParse(const_cast<char*>(svg.c_str()), "px", 96.0f);
    if (!image) return false;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        return false;
    }

    std::vector<u8> pixels(static_cast<size_t>(sizePx) * static_cast<size_t>(sizePx) * 4, 0);
    const f32 scale = static_cast<f32>(sizePx) / std::max(image->width, image->height);
    nsvgRasterize(rast, image, 0, 0, scale, pixels.data(), sizePx, sizePx, sizePx * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    out.width = sizePx;
    out.height = sizePx;
    out.texture = std::make_unique<ImTextureData>();
    out.texture->Create(ImTextureFormat_RGBA32, sizePx, sizePx);
    std::memcpy(out.texture->GetPixels(), pixels.data(), pixels.size());
    out.texture->SetStatus(ImTextureStatus_WantCreate);
#ifdef CF_HAS_SDL3
    ImGui_ImplSDLGPU3_UpdateTexture(out.texture.get());
#endif
    out.failed = out.texture->GetTexID() == ImTextureID_Invalid;
    return !out.failed;
}

EditorIcons::IconEntry& EditorIcons::loadPng(const std::string& cacheKey, const std::filesystem::path& path) {
    auto it = s_cache.find(cacheKey);
    if (it != s_cache.end()) return it->second;

    IconEntry entry;
    if (!std::filesystem::exists(path) || !rasterizePng(path, entry)) {
        entry.failed = true;
    }

    auto [inserted, _] = s_cache.emplace(cacheKey, std::move(entry));
    return inserted->second;
}

EditorIcons::IconEntry& EditorIcons::load(const std::string& name) {
    auto it = s_cache.find(name);
    if (it != s_cache.end()) return it->second;

    IconEntry entry;
    std::filesystem::path path;
    if (name.rfind("spinners/", 0) == 0) {
        path = EditorPaths::spinnerPath(name.substr(9));
    } else {
        path = EditorPaths::iconPath(name);
    }
    if (!std::filesystem::exists(path) || !rasterizeSvg(path.string(), entry, 24)) {
        entry.failed = true;
    }

    auto [inserted, _] = s_cache.emplace(name, std::move(entry));
    return inserted->second;
}

bool EditorIcons::hasIcon(const std::string& name) {
    IconEntry& entry = load(name);
    return !entry.failed && entry.texture;
}

ImTextureRef EditorIcons::get(const std::string& name) {
    IconEntry& entry = load(name);
    if (entry.failed || !entry.texture) return ImTextureRef();
    if (entry.texture->Status == ImTextureStatus_WantCreate) {
#ifdef CF_HAS_SDL3
        ImGui_ImplSDLGPU3_UpdateTexture(entry.texture.get());
#endif
    }
    return entry.texture->GetTexRef();
}

void EditorIcons::image(const std::string& name, f32 size, ImU32 tint) {
    ImTextureRef tex = get(name);
    if (tex._TexData == nullptr && tex._TexID == ImTextureID_Invalid) return;
    const ImVec4 tintCol(
        ((tint >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
        ((tint >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
        ((tint >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
        ((tint >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
    ImGui::Image(tex, ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), tintCol, ImVec4(0, 0, 0, 0));
}

bool EditorIcons::iconButton(const std::string& name, const char* id, f32 size) {
    ImTextureRef tex = get(name);
    if (tex._TexData == nullptr && tex._TexID == ImTextureID_Invalid) {
        return ImGui::Button(id ? id : name.c_str());
    }
    return ImGui::ImageButton(id ? id : name.c_str(), tex, ImVec2(size, size));
}

void EditorIcons::iconLabel(const std::string& name, const char* text, f32 size) {
    image(name, size);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(text);
}

bool EditorIcons::hasBrandLogo() {
    IconEntry& entry = loadPng("__brand_logo__", EditorPaths::brandLogoPath());
    return !entry.failed && entry.texture;
}

ImTextureRef EditorIcons::brandLogoTexture() {
    IconEntry& entry = loadPng("__brand_logo__", EditorPaths::brandLogoPath());
    if (entry.failed || !entry.texture) return ImTextureRef();
    if (entry.texture->Status == ImTextureStatus_WantCreate) {
#ifdef CF_HAS_SDL3
        ImGui_ImplSDLGPU3_UpdateTexture(entry.texture.get());
#endif
    }
    return entry.texture->GetTexRef();
}

void EditorIcons::brandLogo(f32 height) {
    ImTextureRef tex = brandLogoTexture();
    if (tex._TexData == nullptr && tex._TexID == ImTextureID_Invalid) return;

    IconEntry& entry = loadPng("__brand_logo__", EditorPaths::brandLogoPath());
    const f32 aspect = entry.height > 0 ? static_cast<f32>(entry.width) / static_cast<f32>(entry.height) : 1.0f;
    ImGui::Image(tex, ImVec2(height * aspect, height), ImVec2(0, 0), ImVec2(1, 1),
                 ImVec4(1, 1, 1, 1), ImVec4(0, 0, 0, 0));
}

bool EditorIcons::menuItem(const char* icon, const char* label, const char* shortcut, bool selected,
                           bool enabled) {
    const f32 iconSize = ImGui::GetFontSize();
    if (icon && hasIcon(icon)) {
        image(icon, iconSize);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    return ImGui::MenuItem(label, shortcut, selected, enabled);
}

bool EditorIcons::menuItem(const char* icon, const char* label, const char* shortcut, bool* open,
                           bool enabled) {
    const f32 iconSize = ImGui::GetFontSize();
    if (icon && hasIcon(icon)) {
        image(icon, iconSize);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    return ImGui::MenuItem(label, shortcut, open, enabled);
}

#endif

} // namespace Caffeine::Editor

#pragma once

#ifdef CF_HAS_IMGUI
#ifdef CF_HAS_SDL3

#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>
#include <memory>
#include <vector>

namespace Caffeine::Editor {

/// Logical ImGui size → framebuffer pixels (HiDPI / DisplayFramebufferScale).
/// maxDim caps the longest edge to avoid 4K+ offscreen targets in editor panels.
inline ImVec2 imguiFramebufferSize(ImVec2 logicalSize, int maxDim = 1920) {
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    float w = logicalSize.x * scale.x;
    float h = logicalSize.y * scale.y;
    if (w < 1.0f) w = 1.0f;
    if (h < 1.0f) h = 1.0f;
    const float longest = w > h ? w : h;
    if (maxDim > 0 && longest > static_cast<float>(maxDim)) {
        const float shrink = static_cast<float>(maxDim) / longest;
        w *= shrink;
        h *= shrink;
    }
    return ImVec2(w, h);
}

/// Releases GPU backing for a user-owned ImTextureData (sprites, icons, previews).
inline void destroyImGuiTexture(std::unique_ptr<ImTextureData>& texture) {
    if (!texture) return;
    if (texture->GetTexID() != ImTextureID_Invalid) {
        texture->UnusedFrames = 1;
        texture->SetStatus(ImTextureStatus_WantDestroy);
        ImGui_ImplSDLGPU3_UpdateTexture(texture.get());
    }
    texture.reset();
}

inline void clearRetiredImGuiTextures(std::vector<std::unique_ptr<ImTextureData>>& retired) {
    for (auto& texture : retired) {
        destroyImGuiTexture(texture);
    }
    retired.clear();
}

}  // namespace Caffeine::Editor

#endif  // CF_HAS_SDL3
#endif  // CF_HAS_IMGUI

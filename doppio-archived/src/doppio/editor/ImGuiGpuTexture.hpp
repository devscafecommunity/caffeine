#pragma once

#ifdef CF_HAS_IMGUI
#ifdef CF_HAS_SDL3

#include "core/Types.hpp"

#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>
#include <algorithm>
#include <cmath>
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
    // Even pixels keep TAA jitter symmetric and stop 1px HiDPI flicker from reallocating.
    w = std::max(8.0f, std::floor(w * 0.5f) * 2.0f);
    h = std::max(8.0f, std::floor(h * 0.5f) * 2.0f);
    return ImVec2(w, h);
}

/// Holds the GPU canvas at the last settled size so a live window drag does not
/// rebuild HDR / TAA / probe targets every pixel.
struct GpuCanvasLatch {
    u32 width = 0;
    u32 height = 0;
    u32 pendingW = 0;
    u32 pendingH = 0;
    u32 stable = 0;

    static u32 snap(u32 value) { return value < 8u ? 8u : (value & ~1u); }

    bool commit(u32 wantedW, u32 wantedH, u32 settleFrames = 8) {
        wantedW = snap(wantedW);
        wantedH = snap(wantedH);
        if (width == 0 || height == 0) {
            width = wantedW;
            height = wantedH;
            pendingW = pendingH = 0;
            stable = 0;
            return true;
        }
        if (wantedW == width && wantedH == height) {
            pendingW = pendingH = 0;
            stable = 0;
            return false;
        }
        if (wantedW == pendingW && wantedH == pendingH) {
            ++stable;
        } else {
            pendingW = wantedW;
            pendingH = wantedH;
            stable = 1;
        }
        if (stable < settleFrames) return false;
        width = wantedW;
        height = wantedH;
        pendingW = pendingH = 0;
        stable = 0;
        return true;
    }
};

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

#pragma once
#include "core/Types.hpp"

#ifdef CF_HAS_SDL3
#ifdef CF_HAS_IMGUI

#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace Caffeine::Editor {
using namespace Caffeine;

class ImGuiIntegration {
public:
    ImGuiIntegration() = default;
    ~ImGuiIntegration() = default;

    bool init(SDL_Window* window, RHI::RenderDevice* device) {
        ImGui::CreateContext();
        ImGui_ImplSDL3_InitForSDLGPU(window, nullptr);
        ImGui_ImplSDLGPU3_Init(device->nativeDevice());
        m_initialized = true;
        return true;
    }

    void shutdown() {
        if (!m_initialized) return;
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_initialized = false;
    }

    void beginFrame() {
        if (!m_initialized) return;
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    void endFrame(RHI::CommandBuffer* cmd) {
        if (!m_initialized) return;
        ImGui::Render();
        ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), cmd);
    }

    bool processEvent(const SDL_Event& event) {
        if (!m_initialized) return false;
        ImGui_ImplSDL3_ProcessEvent(&event);
        ImGuiIO& io = ImGui::GetIO();
        return io.WantCaptureMouse || io.WantCaptureKeyboard;
    }

    bool wantsKeyboard() const {
        if (!m_initialized) return false;
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    bool wantsMouse() const {
        if (!m_initialized) return false;
        return ImGui::GetIO().WantCaptureMouse;
    }

private:
    bool m_initialized = false;
};

}  // namespace Caffeine::Editor

#endif  // CF_HAS_IMGUI
#endif  // CF_HAS_SDL3

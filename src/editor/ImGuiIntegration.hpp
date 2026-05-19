#pragma once
#include "core/Types.hpp"
#include <cstdio>

#ifdef CF_HAS_SDL3
#ifdef CF_HAS_IMGUI

#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace Caffeine::Editor {

class ImGuiIntegration {
public:
    ImGuiIntegration() = default;
    ~ImGuiIntegration() = default;

    bool init(SDL_Window* window, RHI::RenderDevice* device) {
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        ImGui_ImplSDL3_InitForSDLGPU(window);

        ImGui_ImplSDLGPU3_InitInfo initInfo{};
        initInfo.Device            = device->nativeDevice();
        initInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(
            device->nativeDevice(), window);

        ImGui_ImplSDLGPU3_Init(&initInfo);
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

    void prepareRender(RHI::CommandBuffer* cmd) {
        if (!m_initialized) return;
        ImGui::Render();
        ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), cmd->nativeHandle());
    }

    void endFrame(RHI::CommandBuffer* cmd) {
        if (!m_initialized) return;
        ImGui_ImplSDLGPU3_RenderDrawData(
            ImGui::GetDrawData(),
            cmd->nativeHandle(),
            cmd->nativeRenderPass());
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

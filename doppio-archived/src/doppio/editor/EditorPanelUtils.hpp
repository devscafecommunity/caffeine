#pragma once

#include "assets/MaterialCache.hpp"
#include "core/Types.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/ForwardRenderComponents.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "animation/SkinLibrary.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/SkyboxComponents.hpp"
#include "ecs/EnvironmentEffectsComponents.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "scene/SceneComponents.hpp"

#include <cstring>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#include <imgui_internal.h>
#endif

namespace Caffeine::Editor {

// Changes when entities move, terrain is edited, or the scene membership changes.
// Used so cached GPU frames redraw without waiting for a camera orbit.
inline u64 editorSceneContentStamp(ECS::World& world) {
    u64 hash = 1469598103934665603ull;
    auto mix = [&](u64 value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    mix(world.entityCount());
    mix(Assets::MaterialCache::instance().revision());
    mix(Animation::skinRevision());

    ECS::ComponentQuery meshQuery;
    meshQuery.with<ECS::MeshFilterComponent>();
    world.forEach<ECS::MeshFilterComponent>(meshQuery, [&](ECS::Entity entity, ECS::MeshFilterComponent& filter) {
        mix(entity.id());
        auto mixText = [&](const std::string& text) {
            mix(text.size());
            for (unsigned char c : text) mix(c);
        };
        mixText(filter.customMaterialPath);
        mixText(filter.customTexturePath);
        mixText(filter.customNormalPath);
        u32 shininessBits = 0;
        std::memcpy(&shininessBits, &filter.shininess, sizeof(shininessBits));
        mix(shininessBits);
    });

    ECS::ComponentQuery transformQuery;
    transformQuery.with<Scene::WorldTransform>();
    world.forEach<Scene::WorldTransform>(transformQuery, [&](ECS::Entity entity, Scene::WorldTransform& wt) {
        mix(entity.id());
        const f32* matrix = wt.matrix.data();
        for (int i = 0; i < 16; ++i) {
            u32 bits = 0;
            std::memcpy(&bits, &matrix[i], sizeof(bits));
            mix(bits);
        }
    });

    ECS::ComponentQuery terrainQuery;
    terrainQuery.with<ECS::TerrainComponent>();
    world.forEach<ECS::TerrainComponent>(terrainQuery, [&](ECS::Entity, ECS::TerrainComponent& terrain) {
        mix(terrain.dataRevision);
        mix(terrain.meshRevision);
        mix(terrain.splatRevision);
    });

    ECS::ComponentQuery forwardQuery;
    forwardQuery.with<ECS::ForwardRenderFeaturesComponent>();
    world.forEach<ECS::ForwardRenderFeaturesComponent>(
        forwardQuery, [&](ECS::Entity entity, ECS::ForwardRenderFeaturesComponent& fx) {
            mix(entity.id());
            mix(fx.enabled ? 1u : 0u);
            mix(static_cast<u32>(fx.reflections.mode));
            mix(static_cast<u32>(fx.volumetrics.quality));
            mix(static_cast<u32>(fx.occlusion.enabled));
        });

    ECS::ComponentQuery skyQuery;
    skyQuery.with<ECS::SkyboxComponent>();
    world.forEach<ECS::SkyboxComponent>(skyQuery, [&](ECS::Entity entity, ECS::SkyboxComponent& sky) {
        mix(entity.id());
        mix(sky.enabled ? 1u : 0u);
        mix(static_cast<u32>(sky.presetIndex));
        u32 exposureBits = 0;
        std::memcpy(&exposureBits, &sky.exposure, sizeof(exposureBits));
        mix(exposureBits);
        for (const char* c = sky.customTexturePath; *c; ++c) mix(static_cast<unsigned char>(*c));
    });

    ECS::ComponentQuery envQuery;
    envQuery.with<ECS::EnvironmentEffectsComponent>();
    world.forEach<ECS::EnvironmentEffectsComponent>(
        envQuery, [&](ECS::Entity entity, ECS::EnvironmentEffectsComponent& fx) {
            mix(entity.id());
            mix(fx.enabled);
            mix(fx.mode);
            u32 bits = 0;
            std::memcpy(&bits, &fx.timeOfDay, sizeof(bits));
            mix(bits);
            std::memcpy(&bits, &fx.drivenAmbient.x, sizeof(bits));
            mix(bits);
            std::memcpy(&bits, &fx.drivenAmbient2D, sizeof(bits));
            mix(bits);
        });

    ECS::ComponentQuery lightQuery;
    lightQuery.with<ECS::LightComponent>();
    world.forEach<ECS::LightComponent>(lightQuery, [&](ECS::Entity entity, ECS::LightComponent& light) {
        mix(entity.id());
        u32 bits = 0;
        std::memcpy(&bits, &light.intensity, sizeof(bits));
        mix(bits);
        std::memcpy(&bits, &light.color.x, sizeof(bits));
        mix(bits);
        std::memcpy(&bits, &light.color.y, sizeof(bits));
        mix(bits);
        std::memcpy(&bits, &light.color.z, sizeof(bits));
        mix(bits);
        std::memcpy(&bits, &light.color.w, sizeof(bits));
        mix(bits);
    });

    ECS::ComponentQuery postQuery;
    postQuery.with<ECS::PostProcessComponent>();
    world.forEach<ECS::PostProcessComponent>(postQuery, [&](ECS::Entity entity, ECS::PostProcessComponent& fx) {
        mix(entity.id());
        const auto* bytes = reinterpret_cast<const unsigned char*>(&fx);
        for (size_t i = 0; i < sizeof(fx); ++i) mix(bytes[i]);
    });

    return hash;
}

#ifdef CF_HAS_IMGUI

inline ImGuiID detachedPanelClassId() {
    static const ImGuiID id = ImHashStr("CaffeineDetachedPanel");
    return id;
}

inline int& detachedPanelPlaceFrame() {
    static int frame = -1;
    return frame;
}

inline void editorPanelApplyDetach(bool detached, ImVec2 detachedSize = ImVec2(0, 0)) {
    if (!detached) return;

    ImGuiWindowClass windowClass;
    windowClass.ClassId = detachedPanelClassId();
    windowClass.DockingAllowUnclassed = true;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    windowClass.ViewportFlagsOverrideClear =
        ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);

    if (detachedSize.x > 0.0f && detachedSize.y > 0.0f &&
        ImGui::GetFrameCount() == detachedPanelPlaceFrame()) {
        ImGui::SetNextWindowSize(detachedSize, ImGuiCond_Always);
        const ImGuiViewport* mainVp = ImGui::GetMainViewport();
        if (mainVp) {
            // Keep the new window on the current monitor. Wayland drops a toplevel
            // whose first position is past the last output.
            ImGui::SetNextWindowPos(ImVec2(mainVp->Pos.x + 72.0f, mainVp->Pos.y + 72.0f),
                                    ImGuiCond_Always);
        }
    }
}

// Small button on the tab bar (right side) to pop the panel into its own OS window.
inline void editorPanelDetachTabButton(bool& detached) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window) return;

    const float tabH = ImGui::GetFrameHeight();
    const float btnW = tabH + 2.0f;
    const float btnH = tabH - 2.0f;

    float x = window->Pos.x + window->SizeFull.x - btnW - 4.0f;
    float y = window->Pos.y + 2.0f;
    if (window->DockIsActive && window->DockNode) {
        x = window->Pos.x + window->SizeFull.x - btnW - 4.0f;
        y = window->Pos.y + 2.0f;
    }

    ImGui::PushID("##panel_detach_tab");
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
    const char* label = detached ? "Dock" : "Pop";
    if (ImGui::Button(label, ImVec2(btnW, btnH))) {
        detached = !detached;
        if (detached) detachedPanelPlaceFrame() = ImGui::GetFrameCount() + 1;
    }
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(detached ? "Dock back into the main window"
                                   : "Open a system window you can drag to another monitor");
    }
    ImGui::PopID();
}

// Skip GPU work for collapsed/hidden/throttled dock panels; still safe to blit the last target.
inline bool editorPanelWorthGpuRender(ImVec2 origin, ImVec2 size, u32 frameInterval = 1) {
    if (size.x < 32.0f || size.y < 32.0f) return false;
    if (ImGui::IsWindowCollapsed()) return false;
    const ImVec2 max(origin.x + size.x, origin.y + size.y);
    if (!ImGui::IsRectVisible(origin, max)) return false;
    if (frameInterval > 1u && (ImGui::GetFrameCount() % frameInterval) != 0u) return false;
    return true;
}

#endif

}  // namespace Caffeine::Editor

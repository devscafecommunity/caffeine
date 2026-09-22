#include "caffeine/plugin/PluginAPI.hpp"
#include "caffeine/postprocess/PostProcessEditorUI.hpp"
#include "caffeine/postprocess/PostProcessPresets.hpp"
#include "editor/EditorContext.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "ecs/CameraComponents.hpp"

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace {

class PostProcessingPlugin : public Caffeine::Editor::IPlugin {
public:
    explicit PostProcessingPlugin(const Caffeine::Editor::PluginHostApi* host) : m_host(host) {}

    void OnLoad() override {
        if (!m_host) return;
        if (m_host->registerPanel) {
            m_host->registerPanel(m_host->editorContext, GetName(), "Post Processing",
                                  &PostProcessingPlugin::renderPanel, this);
        }
        if (m_host->registerMenuAction) {
            m_host->registerMenuAction(m_host->editorContext, GetName(),
                                     "Effects/Add Post Process to Camera",
                                     &PostProcessingPlugin::addToSelectedCamera, this);
        }
        if (m_host->registerComponentDrawer && m_host->getComponentTypeId) {
            const Caffeine::u32 typeId =
                m_host->getComponentTypeId(m_host->editorContext, "PostProcess");
            if (typeId != Caffeine::u32_max) {
                m_host->registerComponentDrawer(m_host->editorContext, GetName(), typeId,
                                                &PostProcessingPlugin::drawComponent, this);
            }
        }
    }

    void OnUnload() override {}
    void OnUpdate(float) override {}

    const char* GetName() const override { return "Post Processing"; }
    const char* GetVersion() const override { return "0.2.0"; }
    const char* GetDescription() const override {
        return "Modular post-processing stack: per-effect modules, Lua control, optional benchmarks.";
    }

private:
    static Caffeine::Editor::EditorContext* editorCtx(const Caffeine::Editor::PluginHostApi* host) {
        return host ? static_cast<Caffeine::Editor::EditorContext*>(host->editorContext) : nullptr;
    }

    static void renderPanel(void* userData) {
#ifdef CF_HAS_IMGUI
        auto* self = static_cast<PostProcessingPlugin*>(userData);
        auto* ctx = editorCtx(self->m_host);
        if (!ctx || !ctx->activeWorld) {
            ImGui::TextDisabled("No active scene.");
            return;
        }
        Caffeine::ECS::World& world = *ctx->activeWorld;
        if (!ctx->selectedEntity.isValid()) {
            ImGui::TextWrapped(
                "Select a camera entity. Combine effect modules below — Cinematic / Horror / Arcade "
                "are optional benchmark looks, not game types.");
            return;
        }

        Caffeine::ECS::Entity e = ctx->selectedEntity;
        ImGui::Text("Target: %s (entity %u)", Caffeine::Editor::getEntityName(world, e), e.id());

        if (!world.has<Caffeine::ECS::PostProcessComponent>(e)) {
            if (ImGui::Button("Add Post Process Stack")) {
                world.add<Caffeine::ECS::PostProcessComponent>(e);
                ctx->isDirty = true;
            }
            return;
        }

        auto* fx = world.get<Caffeine::ECS::PostProcessComponent>(e);
        if (!fx) return;

        ImGui::TextWrapped(
            "Modular effect stack — enable and tune each module independently. GPU passes are "
            "planned; editor preview uses approximate overlays.");

        if (ImGui::CollapsingHeader("Benchmark looks (optional)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (Caffeine::PostProcess::EditorUI::drawBenchmarkPresets(*fx)) ctx->isDirty = true;
        }

        ImGui::Separator();
        if (Caffeine::PostProcess::EditorUI::drawEffectStack(*fx)) ctx->isDirty = true;
#else
        (void)userData;
#endif
    }

    static void addToSelectedCamera(void* userData) {
        auto* self = static_cast<PostProcessingPlugin*>(userData);
        auto* ctx = editorCtx(self->m_host);
        if (!ctx || !ctx->activeWorld || !ctx->selectedEntity.isValid()) return;
        Caffeine::ECS::World& world = *ctx->activeWorld;
        Caffeine::ECS::Entity e = ctx->selectedEntity;
        if (!world.has<Caffeine::ECS::Camera2DComponent>(e) &&
            !world.has<Caffeine::ECS::Camera3DComponent>(e)) {
            if (self->m_host->logError) {
                self->m_host->logError(self->m_host->editorContext,
                                       "Select a Camera2D or Camera3D entity first.");
            }
            return;
        }
        if (!world.has<Caffeine::ECS::PostProcessComponent>(e)) {
            world.add<Caffeine::ECS::PostProcessComponent>(e);
            ctx->isDirty = true;
        }
        if (self->m_host->logInfo) {
            self->m_host->logInfo(self->m_host->editorContext, "Post-process stack added to camera.");
        }
    }

    static void drawComponent(void* componentData, void* userData) {
#ifdef CF_HAS_IMGUI
        (void)userData;
        auto* fx = static_cast<Caffeine::ECS::PostProcessComponent*>(componentData);
        if (!fx) return;
        Caffeine::PostProcess::EditorUI::drawEffectStack(*fx);
#else
        (void)componentData;
        (void)userData;
#endif
    }

    const Caffeine::Editor::PluginHostApi* m_host = nullptr;
};

PostProcessingPlugin* g_plugin = nullptr;

}  // namespace

extern "C" Caffeine::Editor::IPlugin* CreatePlugin(const Caffeine::Editor::PluginHostApi* host) {
    if (!g_plugin) g_plugin = new PostProcessingPlugin(host);
    return g_plugin;
}

extern "C" void DestroyPlugin(Caffeine::Editor::IPlugin* plugin) {
    delete plugin;
    g_plugin = nullptr;
}

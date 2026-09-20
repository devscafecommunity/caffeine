#include "editor/PluginAPI.hpp"
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
    const char* GetVersion() const override { return "0.1.0"; }
    const char* GetDescription() const override {
        return "Camera post-processing stack: bloom, vignette, color grading and more.";
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
            ImGui::TextWrapped("Select a camera entity, then use the presets below or add the "
                               "Post Process component from the Inspector.");
            return;
        }

        Caffeine::ECS::Entity e = ctx->selectedEntity;
        ImGui::Text("Target: %s (entity %u)", Caffeine::Editor::getEntityName(world, e), e.id());

        if (!world.has<Caffeine::ECS::PostProcessComponent>(e)) {
            if (ImGui::Button("Add Post Process Component")) {
                world.add<Caffeine::ECS::PostProcessComponent>(e);
                ctx->isDirty = true;
            }
            return;
        }

        auto* fx = world.get<Caffeine::ECS::PostProcessComponent>(e);
        if (!fx) return;

        ImGui::Separator();
        ImGui::TextUnformatted("Presets");
        if (ImGui::Button("Cinematic")) applyPreset(*fx, 1.05f, 1.1f, 1.05f, 0.35f, 0.15f, 0.02f, 0.04f);
        ImGui::SameLine();
        if (ImGui::Button("Horror")) applyPreset(*fx, 0.75f, 1.25f, 0.7f, 0.55f, 0.0f, 0.08f, 0.12f);
        ImGui::SameLine();
        if (ImGui::Button("Arcade")) applyPreset(*fx, 1.2f, 1.15f, 1.3f, 0.1f, 0.25f, 0.0f, 0.0f);

        ImGui::Separator();
        if (ImGui::Checkbox("Enabled", &fx->enabled)) ctx->isDirty = true;
        if (ImGui::SliderFloat("Exposure", &fx->exposure, 0.2f, 3.0f)) ctx->isDirty = true;
        if (ImGui::SliderFloat("Vignette", &fx->vignette, 0.0f, 1.0f)) ctx->isDirty = true;
        if (ImGui::SliderFloat("Bloom", &fx->bloom, 0.0f, 1.0f)) ctx->isDirty = true;
        if (ImGui::SliderFloat("Chromatic", &fx->chromaticAberration, 0.0f, 1.0f)) {
            ctx->isDirty = true;
        }
        if (ImGui::SliderFloat("Film Grain", &fx->filmGrain, 0.0f, 1.0f)) ctx->isDirty = true;
#else
        (void)userData;
#endif
    }

    static void applyPreset(Caffeine::ECS::PostProcessComponent& fx, float exposure, float contrast,
                            float saturation, float vignette, float bloom, float chroma, float grain) {
        fx.enabled = true;
        fx.exposure = exposure;
        fx.contrast = contrast;
        fx.saturation = saturation;
        fx.vignette = vignette;
        fx.bloom = bloom;
        fx.chromaticAberration = chroma;
        fx.filmGrain = grain;
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
            self->m_host->logInfo(self->m_host->editorContext, "Post Process added to camera.");
        }
    }

    static void drawComponent(void* componentData, void* userData) {
#ifdef CF_HAS_IMGUI
        (void)userData;
        auto* fx = static_cast<Caffeine::ECS::PostProcessComponent*>(componentData);
        if (!fx) return;
        ImGui::Checkbox("Enabled", &fx->enabled);
        ImGui::SliderFloat("Exposure", &fx->exposure, 0.2f, 3.0f);
        ImGui::SliderFloat("Vignette", &fx->vignette, 0.0f, 1.0f);
        ImGui::SliderFloat("Bloom", &fx->bloom, 0.0f, 1.0f);
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

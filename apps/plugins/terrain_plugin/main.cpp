#include "caffeine/plugin/PluginAPI.hpp"
#include "editor/EditorContext.hpp"
#include "ecs/TerrainComponents.hpp"
#include "TerrainBridge.hpp"

extern "C" void caffeine_terrain_plugin_register_services(
    const Caffeine::Editor::PluginHostApi* host);

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <cstring>

namespace {

class TerrainGeneratorPlugin : public Caffeine::Editor::IPlugin {
public:
    explicit TerrainGeneratorPlugin(const Caffeine::Editor::PluginHostApi* host) : m_host(host) {}

    void OnLoad() override {
        if (!m_host) return;
        caffeine_terrain_plugin_register_services(m_host);
        if (m_host->registerPanel) {
            m_host->registerPanel(m_host->editorContext, GetName(), "Terrain Generator",
                                  &TerrainGeneratorPlugin::renderPanel, this);
        }
    }

    void OnUnload() override {}
    void OnUpdate(float) override {}

    const char* GetName() const override { return "Terrain Generator"; }
    const char* GetVersion() const override { return "0.1.0"; }
    const char* GetDescription() const override {
        return "Ultra-realistic procedural terrain via domain warping + geomorphological PDE.";
    }

private:
    static Caffeine::Editor::EditorContext* editorCtx(
        const Caffeine::Editor::PluginHostApi* host) {
        return host ? static_cast<Caffeine::Editor::EditorContext*>(host->editorContext) : nullptr;
    }

    static void renderPanel(void* userData) {
#ifdef CF_HAS_IMGUI
        auto* self = static_cast<TerrainGeneratorPlugin*>(userData);
        auto* ctx = editorCtx(self->m_host);
        if (!ctx || !ctx->activeWorld) {
            ImGui::TextDisabled("No active scene.");
            return;
        }

        Caffeine::ECS::World& world = *ctx->activeWorld;
        if (!ctx->selectedEntity.isValid() || !world.has<Caffeine::ECS::TerrainComponent>(ctx->selectedEntity)) {
            ImGui::TextWrapped(
                "Select a Terrain entity, then choose a preset and click Generate. Sculpt and paint "
                "in the Terrain Editor panel.");
            return;
        }

        auto* terrain = world.get<Caffeine::ECS::TerrainComponent>(ctx->selectedEntity);
        if (!terrain) return;

        ImGui::Text("Entity %u — %ux%u heightmap", ctx->selectedEntity.id(), terrain->resolutionX,
                    terrain->resolutionZ);

        static const char* kPresets[] = {"default", "alpine", "mesa", "volcanic", "glacial", "blend"};
        ImGui::Combo("Preset", &self->m_presetIndex, kPresets, IM_ARRAYSIZE(kPresets));

        ImGui::DragFloat("World Size (m)", &self->m_worldSize, 4.0f, 32.0f, 4096.0f, "%.0f");
        ImGui::DragInt("Grid Size", &self->m_gridSize, 8, 64, 1024);
        ImGui::DragInt("Seed", &self->m_seed, 1, 0, 999999);

        ImGui::Separator();
        if (self->m_busy) {
            ImGui::TextDisabled("Generating… (Node.js)");
        } else if (ImGui::Button("Generate Terrain")) {
            self->m_busy = true;
            std::string error;
            const bool ok = Caffeine::Editor::TerrainBridge::generateUltraRealistic(
                world, ctx->selectedEntity, kPresets[self->m_presetIndex],
                static_cast<Caffeine::u32>(self->m_seed), self->m_worldSize,
                static_cast<Caffeine::u32>(self->m_gridSize), error);
            self->m_busy = false;
            if (ok) {
                ctx->isDirty = true;
                if (self->m_host->logInfo) {
                    self->m_host->logInfo(self->m_host->editorContext, "Terrain generated.");
                }
            } else if (self->m_host->logError) {
                self->m_host->logError(self->m_host->editorContext, error.c_str());
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Import");
        ImGui::InputText("Heightmap path", self->m_importPath, sizeof(self->m_importPath));
        if (ImGui::Button("Import .txt heightmap")) {
            std::string error;
            if (Caffeine::Editor::TerrainBridge::importHeightmap(
                    world, ctx->selectedEntity, self->m_importPath, terrain->worldSizeX,
                    terrain->worldSizeZ, error)) {
                ctx->isDirty = true;
            } else if (self->m_host->logError) {
                self->m_host->logError(self->m_host->editorContext, error.c_str());
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled(
            "Algorithm: assets/general-ultra-realistic-terrain-algorithm (npm install on first run).");
#else
        (void)userData;
#endif
    }

    const Caffeine::Editor::PluginHostApi* m_host = nullptr;
#ifdef CF_HAS_IMGUI
    int m_presetIndex = 0;
    float m_worldSize = 256.0f;
    int m_gridSize = 257;
    int m_seed = 42;
    bool m_busy = false;
    char m_importPath[512] = {};
#endif
};

TerrainGeneratorPlugin* g_plugin = nullptr;

}  // namespace

extern "C" {

CAFFEINE_PLUGIN_API Caffeine::Editor::IPlugin* CreatePlugin(
    const Caffeine::Editor::PluginHostApi* host) {
    g_plugin = new TerrainGeneratorPlugin(host);
    return g_plugin;
}

CAFFEINE_PLUGIN_API void DestroyPlugin(Caffeine::Editor::IPlugin* plugin) {
    delete plugin;
    g_plugin = nullptr;
}

}  // extern "C"

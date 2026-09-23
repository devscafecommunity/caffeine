#include "caffeine/plugin/PluginAPI.hpp"
#include "editor/EditorContext.hpp"
#include "ProceduralBridge.hpp"

extern "C" void caffeine_procedural_plugin_register_services(
    const Caffeine::Editor::PluginHostApi* host);

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include <cstring>
#include <filesystem>
#include <string>

namespace {

#ifdef CF_HAS_IMGUI
static const char* kTerrainProfiles[] = {"flat", "hills", "fbm"};
static const char* kTerrainProfileLabels[] = {"Flat", "Hills (FBM)", "FBM (generic)"};
static const char* kStructureProfiles[] = {"none", "markers", "rooms", "track"};
static const char* kStructureProfileLabels[] = {"None", "Random markers", "Room grid",
                                                "Race track walls"};
static const char* kBenchmarks[] = {"exploration", "backrooms", "endless_race"};
static const char* kBenchmarkLabels[] = {"Exploration (hills + markers)",
                                         "Backrooms (flat + rooms)",
                                         "Endless race (flat + track)"};
#endif

class ProceduralToolsPlugin : public Caffeine::Editor::IPlugin {
public:
    explicit ProceduralToolsPlugin(const Caffeine::Editor::PluginHostApi* host) : m_host(host) {}

    void OnLoad() override {
        if (!m_host) return;
        caffeine_procedural_plugin_register_services(m_host);
        if (!m_host->registerPanel) return;
        m_host->registerPanel(m_host->editorContext, GetName(), "Procedural Tools",
                              &ProceduralToolsPlugin::renderPanel, this);
        if (m_host->registerMenuAction) {
            m_host->registerMenuAction(
                m_host->editorContext, GetName(), "Game/Install Procedural Scripts",
                &ProceduralToolsPlugin::installScriptsMenu, this);
        }
    }

    void OnUnload() override {}
    void OnUpdate(float) override {}

    const char* GetName() const override { return "Procedural Tools"; }
    const char* GetVersion() const override { return "0.2.0"; }
    const char* GetDescription() const override {
        return "Modular procedural streaming tools — terrain, structures, and Lua directors.";
    }

private:
    static Caffeine::Editor::EditorContext* editorCtx(
        const Caffeine::Editor::PluginHostApi* host) {
        return host ? static_cast<Caffeine::Editor::EditorContext*>(host->editorContext) : nullptr;
    }

    static std::filesystem::path projectRoot(Caffeine::Editor::EditorContext* ctx) {
        if (!ctx || ctx->currentScenePath.empty()) return {};
        return std::filesystem::path(ctx->currentScenePath).parent_path().parent_path();
    }

    static void installScriptsMenu(void* userData) {
        auto* self = static_cast<ProceduralToolsPlugin*>(userData);
        auto* ctx = editorCtx(self->m_host);
        if (!ctx) return;
        std::string error;
        if (!Caffeine::Editor::ProceduralBridge::installScriptTemplates(projectRoot(ctx), error)) {
            if (self->m_host->logError) self->m_host->logError(self->m_host->editorContext, error.c_str());
        }
    }

#ifdef CF_HAS_IMGUI
    bool invokeStreamingSetup(Caffeine::Editor::EditorContext* ctx) const {
        if (!ctx || !ctx->activeWorld) return false;
        Caffeine::Editor::ProceduralStreamingSetup setup;
        setup.seed = static_cast<Caffeine::u32>(m_seed);
        setup.chunkSize = m_chunkSize;
        setup.viewRadius = static_cast<Caffeine::u32>(m_viewRadius);
        setup.scriptPath = m_scriptPath;
        setup.terrainProfile = kTerrainProfiles[m_terrainProfileIndex];
        setup.structureProfile = kStructureProfiles[m_structureProfileIndex];

        Caffeine::ECS::Entity director;
        Caffeine::ECS::Entity terrain;
        std::string error;
        if (!Caffeine::Editor::ProceduralBridge::createStreamingSetup(*ctx->activeWorld, setup,
                                                                      director, terrain, error)) {
            if (m_host->logError) m_host->logError(m_host->editorContext, error.c_str());
            return false;
        }
        ctx->selectedEntity = director;
        ctx->isDirty = true;
        return true;
    }

    bool invokeBenchmark(Caffeine::Editor::EditorContext* ctx, int index) const {
        if (!ctx || !ctx->activeWorld || index < 0 || index >= 3) return false;
        Caffeine::ECS::Entity director;
        Caffeine::ECS::Entity terrain;
        std::string error;
        if (!Caffeine::Editor::ProceduralBridge::loadBenchmark(
                *ctx->activeWorld, kBenchmarks[index], static_cast<Caffeine::u32>(m_seed),
                m_chunkSize, static_cast<Caffeine::u32>(m_viewRadius), director, terrain, error)) {
            if (m_host->logError) m_host->logError(m_host->editorContext, error.c_str());
            return false;
        }
        ctx->selectedEntity = director;
        ctx->isDirty = true;
        return true;
    }
#endif

    static void renderPanel(void* userData) {
#ifdef CF_HAS_IMGUI
        auto* self = static_cast<ProceduralToolsPlugin*>(userData);
        auto* ctx = editorCtx(self->m_host);
        if (!ctx || !ctx->activeWorld) {
            ImGui::TextDisabled("No active scene.");
            return;
        }

        ImGui::TextWrapped(
            "Modular tools for procedural streaming: combine terrain generators, structure "
            "spawners, and Lua directors to build any game. Press Play to stream chunks.");

        if (ImGui::CollapsingHeader("Streaming", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragInt("Seed", &self->m_seed, 1, 0, 999999);
            ImGui::DragFloat("Chunk size (m)", &self->m_chunkSize, 4.0f, 32.0f, 256.0f, "%.0f");
            ImGui::DragInt("View radius (chunks)", &self->m_viewRadius, 1, 2, 12);
        }

        if (ImGui::CollapsingHeader("Built-in generators", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Combo("Terrain height", &self->m_terrainProfileIndex, kTerrainProfileLabels,
                         IM_ARRAYSIZE(kTerrainProfileLabels));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Height profile baked per chunk. Override in Lua with "
                                  "generateTerrainChunk / fillTerrainNoise.");
            }
            ImGui::Combo("Structure spawner", &self->m_structureProfileIndex,
                         kStructureProfileLabels, IM_ARRAYSIZE(kStructureProfileLabels));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Optional cubes spawned per chunk. Use none and spawn via Lua for full control.");
            }
        }

        if (ImGui::CollapsingHeader("Director script", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputText("Script path", self->m_scriptPath, sizeof(self->m_scriptPath));
            if (ImGui::Button("Install Lua scripts to project")) {
                installScriptsMenu(self);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Copies templates to scripts/procedural/");
            }
        }

        if (ImGui::CollapsingHeader("Scene setup", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Create Streaming Setup")) {
                if (self->invokeStreamingSetup(ctx) && self->m_host->logInfo) {
                    self->m_host->logInfo(self->m_host->editorContext,
                                          "Streaming setup created. Press Play to stream.");
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Creates ProceduralTerrain + ProceduralDirector with your settings.");
            }
        }

        if (ImGui::CollapsingHeader("Lua API reference")) {
            ImGui::BulletText("caffeine.procedural.stream(director, terrain, cx, cz)");
            ImGui::BulletText("caffeine.procedural.generateTerrainChunk(...)");
            ImGui::BulletText("caffeine.procedural.spawnCube(...)");
            ImGui::BulletText("caffeine.procedural.fbm2d / noise2d / hash");
            ImGui::BulletText("caffeine.procedural.getFocusChunk / isChunkLoaded");
        }

        if (ImGui::CollapsingHeader("Benchmarks (examples)")) {
            ImGui::TextWrapped(
                "Reference setups — not game types. Study or duplicate these scripts, then mix "
                "tools for your own design.");
            for (int i = 0; i < 3; ++i) {
                if (ImGui::Button(kBenchmarkLabels[i])) {
                    if (self->invokeBenchmark(ctx, i) && self->m_host->logInfo) {
                        self->m_host->logInfo(self->m_host->editorContext,
                                              "Benchmark scene loaded. Press Play to test.");
                    }
                }
                if (i < 2) ImGui::SameLine();
            }
            ImGui::Separator();
            ImGui::TextDisabled("Scripts: exploration_director.lua, backrooms_director.lua, "
                                "endless_race_director.lua, custom_director.lua");
        }
#else
        (void)userData;
#endif
    }

    const Caffeine::Editor::PluginHostApi* m_host = nullptr;
#ifdef CF_HAS_IMGUI
    int m_seed = 42;
    float m_chunkSize = 64.0f;
    int m_viewRadius = 4;
    int m_terrainProfileIndex = 1;
    int m_structureProfileIndex = 0;
    char m_scriptPath[256] = "scripts/procedural/custom_director.lua";
#endif
};

}  // namespace

extern "C" {

CAFFEINE_PLUGIN_API Caffeine::Editor::IPlugin* CreatePlugin(
    const Caffeine::Editor::PluginHostApi* host) {
    return new ProceduralToolsPlugin(host);
}

CAFFEINE_PLUGIN_API void DestroyPlugin(Caffeine::Editor::IPlugin* plugin) { delete plugin; }

}  // extern "C"

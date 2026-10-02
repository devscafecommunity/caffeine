#include "caffeine/navigation/NavigationApi.hpp"
#include "editor/EditorContext.hpp"
#include "editor/PluginAPI.hpp"

#include <filesystem>
#include <fstream>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace {

const char* kIdleScript = R"(function onCreate(entity)
  caffeine.navigation.setMode(entity, "idle")
end

function onUpdate(entity, dt)
end
)";

const char* kPatrolScript = R"(function onCreate(entity)
  caffeine.navigation.setMode(entity, "patrol")
end

function onUpdate(entity, dt)
end
)";

const char* kFollowScript = R"(-- otherEntityId: id of the entity this NPC should follow.
function onCreate(entity)
  caffeine.navigation.setMode(entity, "follow")
end

function onUpdate(entity, dt)
end
)";

class AiDevelopmentPlugin : public Caffeine::Editor::IPlugin {
public:
    explicit AiDevelopmentPlugin(const Caffeine::Editor::PluginHostApi* host) : m_host(host) {}

    void OnLoad() override {
        if (!m_host || !m_host->registerPanel) return;
        m_host->registerPanel(m_host->editorContext, GetName(), "AI Development",
                              &AiDevelopmentPlugin::renderPanel, this);
    }

    void OnUnload() override {}
    void OnUpdate(float) override {}

    const char* GetName() const override { return "AI Development"; }
    const char* GetVersion() const override { return "0.1.0"; }
    const char* GetDescription() const override {
        return "Author NPC behaviour: idle, patrol, follow, or a Lua script. Not limited to combat.";
    }

private:
    static Caffeine::Editor::EditorContext* editorCtx(const Caffeine::Editor::PluginHostApi* host) {
        return host ? static_cast<Caffeine::Editor::EditorContext*>(host->editorContext) : nullptr;
    }

    static bool writeTemplate(const std::filesystem::path& path, const char* source) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path);
        if (!out) return false;
        out << source;
        return static_cast<bool>(out);
    }

    static void renderPanel(void* userData) {
#ifdef CF_HAS_IMGUI
        auto* self = static_cast<AiDevelopmentPlugin*>(userData);
        auto* ctx = editorCtx(self->m_host);
        if (!ctx || !ctx->activeWorld) {
            ImGui::TextDisabled("No active scene.");
            return;
        }
        Caffeine::ECS::World& world = *ctx->activeWorld;
        ImGui::TextWrapped(
            "Program any NPC: a villager, a guide, a creature, or an opponent. The grid pathfinder "
            "lives in the engine; this panel only authors behaviour.");

        if (!ctx->selectedEntity.isValid()) {
            ImGui::TextDisabled("Select an entity to add a nav volume or an agent.");
            return;
        }
        Caffeine::ECS::Entity entity = ctx->selectedEntity;
        ImGui::Text("Target: %s (%u)", Caffeine::Editor::getEntityName(world, entity), entity.id());

        if (!world.has<Caffeine::Navigation::NavVolume>(entity)) {
            if (ImGui::Button("Add Nav Volume")) {
                Caffeine::Navigation::NavVolume volume;
                volume.resize(32, 32);
                world.add<Caffeine::Navigation::NavVolume>(entity, volume);
                ctx->isDirty = true;
            }
        } else if (auto* volume = world.get<Caffeine::Navigation::NavVolume>(entity)) {
            ImGui::SeparatorText("Nav volume");
            int size[2] = {volume->width, volume->height};
            if (ImGui::DragInt2("Cells", size, 1.0f, 2, 256)) {
                volume->resize(size[0], size[1]);
                ctx->isDirty = true;
            }
            if (ImGui::DragFloat("Cell size", &volume->cellSize, 0.05f, 0.1f, 100.0f)) ctx->isDirty = true;
            if (ImGui::DragFloat3("Origin", &volume->origin.x, 0.1f)) ctx->isDirty = true;
        }

        ImGui::Separator();
        if (!world.has<Caffeine::Navigation::NavAgent>(entity)) {
            if (ImGui::Button("Add Nav Agent")) {
                world.add<Caffeine::Navigation::NavAgent>(entity);
                ctx->isDirty = true;
            }
        } else if (auto* agent = world.get<Caffeine::Navigation::NavAgent>(entity)) {
            ImGui::SeparatorText("Agent");
            int mode = static_cast<int>(agent->mode);
            const char* modes[] = {"Idle", "Patrol", "Follow", "Scripted"};
            if (ImGui::Combo("Behaviour", &mode, modes, 4)) {
                agent->mode = static_cast<Caffeine::Navigation::NavMode>(mode);
                agent->planDirty = true;
                ctx->isDirty = true;
            }
            if (ImGui::DragFloat("Speed", &agent->speed, 0.05f, 0.0f, 100.0f)) ctx->isDirty = true;
            if (ImGui::DragFloat3("Destination", &agent->destination.x, 0.1f)) {
                agent->hasDestination = true;
                agent->planDirty = true;
                ctx->isDirty = true;
            }
            if (ImGui::Button("Add patrol point from destination")) {
                Caffeine::Navigation::addPatrolPoint(world, entity, agent->destination);
                ctx->isDirty = true;
            }
            ImGui::TextDisabled("Patrol points: %u", agent->patrolCount);
            if (ImGui::Button("Clear patrol")) {
                agent->patrolCount = 0;
                agent->patrolIndex = 0;
                ctx->isDirty = true;
            }

            ImGui::SeparatorText("Behaviour scripts");
            const std::filesystem::path root = ctx->projectRootPath.empty()
                                                   ? std::filesystem::path("scripts/ai")
                                                   : ctx->projectRootPath / "scripts" / "ai";
            auto save = [&](const char* file, const char* source) {
                const std::filesystem::path path = root / file;
                if (!writeTemplate(path, source)) return;
                Caffeine::Navigation::setBehaviorScript(world, entity, path.generic_string());
                ctx->assetBrowserDirty = true;
                ctx->isDirty = true;
            };
            if (ImGui::Button("Write idle.lua")) save("idle.lua", kIdleScript);
            ImGui::SameLine();
            if (ImGui::Button("Write patrol.lua")) save("patrol.lua", kPatrolScript);
            ImGui::SameLine();
            if (ImGui::Button("Write follow.lua")) save("follow.lua", kFollowScript);
            if (agent->behaviorScript[0] != '\0') {
                ImGui::TextWrapped("Script: %s", agent->behaviorScript);
            }
        }
#else
        (void)userData;
#endif
    }

    const Caffeine::Editor::PluginHostApi* m_host = nullptr;
};

AiDevelopmentPlugin* g_plugin = nullptr;

}  // namespace

extern "C" Caffeine::Editor::IPlugin* CreatePlugin(const Caffeine::Editor::PluginHostApi* host) {
    if (!g_plugin) g_plugin = new AiDevelopmentPlugin(host);
    return g_plugin;
}

extern "C" void DestroyPlugin(Caffeine::Editor::IPlugin* plugin) {
    delete plugin;
    g_plugin = nullptr;
}

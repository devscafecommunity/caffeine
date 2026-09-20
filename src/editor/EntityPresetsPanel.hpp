#pragma once

#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"
#include "editor/EntityPresetTypes.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace Caffeine::Editor {

class EntityPresetsPanel {
public:
    void setProjectRoot(const std::filesystem::path& root) { m_projectRoot = root; }
    void setOnScriptOpen(std::function<void(const std::string&)> callback) { m_onScriptOpen = std::move(callback); }

    void render(ECS::World& world, EditorContext& ctx);

    bool isOpen() const { return m_open; }
    void open() { m_open = true; }
    void close() { m_open = false; }

private:
    void renderCategoryList();
    void renderPresetList();
    void renderWizard();
    void selectPreset(const std::string& presetId);
    void createSelectedPreset(ECS::World& world, EditorContext& ctx);

    bool m_open = false;
    bool m_detached = false;
    std::filesystem::path m_projectRoot;
    std::function<void(const std::string&)> m_onScriptOpen;

    std::string m_selectedCategory = "playable_2d";
    std::string m_selectedPresetId;
    EntityPresetWizardState m_wizard;
    std::string m_statusMessage;
    bool m_statusIsError = false;
};

}  // namespace Caffeine::Editor

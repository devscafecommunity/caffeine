#include "editor/EntityPresetRegistry.hpp"
#include "editor/EntityPresetFactories.hpp"
#include "editor/EntityPresetPackages.hpp"
#include "editor/EntityPresetUtils.hpp"

namespace Caffeine::Editor {

EntityPresetRegistry& EntityPresetRegistry::instance() {
    static EntityPresetRegistry registry;
    return registry;
}

void EntityPresetRegistry::registerBuiltIns() {
    m_categories = {
        {"playable", "Playable", "Player rigs for 2D, FPP, third-person and vehicles."},
        {"npcs", "NPCs", "Non-hostile characters with behavior scripts."},
        {"enemies", "Enemies", "Hostile actors with combat-oriented scripts."},
        {"items", "Items", "Collectibles, pickups and interactables."},
        {"objects", "Objects", "World objects like destructibles and props."},
        {"ui", "UI", "HUDs, menus and interface scaffolding."},
    };

    m_presets.clear();
    m_presetIndex.clear();
    registerBuiltInEntityPresets(*this);
}

void EntityPresetRegistry::clearExternalPresets() {
    std::vector<EntityPresetDescriptor> kept;
    kept.reserve(m_presets.size());
    for (auto& preset : m_presets) {
        if (preset.builtIn) kept.push_back(std::move(preset));
    }
    m_presets = std::move(kept);
    m_presetIndex.clear();
    for (usize i = 0; i < m_presets.size(); ++i) {
        m_presetIndex[m_presets[i].id] = i;
    }
}

bool EntityPresetRegistry::registerPreset(EntityPresetDescriptor descriptor) {
    if (descriptor.id.empty()) return false;
    if (auto it = m_presetIndex.find(descriptor.id); it != m_presetIndex.end()) {
        m_presets[it->second] = std::move(descriptor);
        return true;
    }
    m_presetIndex[descriptor.id] = m_presets.size();
    m_presets.push_back(std::move(descriptor));
    return true;
}

bool EntityPresetRegistry::unregisterPreset(const std::string& id) {
    auto it = m_presetIndex.find(id);
    if (it == m_presetIndex.end()) return false;
    m_presets.erase(m_presets.begin() + static_cast<long>(it->second));
    m_presetIndex.clear();
    for (usize i = 0; i < m_presets.size(); ++i) {
        m_presetIndex[m_presets[i].id] = i;
    }
    return true;
}

const EntityPresetDescriptor* EntityPresetRegistry::findPreset(const std::string& id) const {
    auto it = m_presetIndex.find(id);
    if (it == m_presetIndex.end()) return nullptr;
    return &m_presets[it->second];
}

std::vector<const EntityPresetDescriptor*> EntityPresetRegistry::presetsInCategory(
    const std::string& categoryId) const {
    std::vector<const EntityPresetDescriptor*> out;
    for (const auto& preset : m_presets) {
        if (preset.category == categoryId) out.push_back(&preset);
    }
    return out;
}

EntityPresetWizardState EntityPresetRegistry::makeDefaultWizardState(
    const EntityPresetDescriptor& preset) const {
    EntityPresetWizardState state;
    state.entityName = preset.defaultEntityName;
    state.fields.push_back(EntityPresetUtils::makeEnumField(
        "script_type", "Script Type", {"Lua", "C++ Native"}, 0,
        "Lua runs in Play immediately. C++ Native generates a .hpp you compile with the project."));
    state.fields.insert(state.fields.end(), preset.defaultFields.begin(), preset.defaultFields.end());
    return state;
}

void EntityPresetRegistry::loadPackagesFromDirectory(const std::filesystem::path& directory,
                                                     const std::string& sourceLabel) {
    scanEntityPresetPackages(directory, sourceLabel, *this);
}

void EntityPresetRegistry::scanProject(const std::filesystem::path& projectRoot) {
    clearExternalPresets();
    if (projectRoot.empty()) return;

    loadPackagesFromDirectory(projectRoot / "packages", "package");
    loadPackagesFromDirectory(projectRoot / "plugins", "plugin");

    const auto bundled = projectRoot / "assets" / "presets" / "packages";
    loadPackagesFromDirectory(bundled, "bundled");
}

}  // namespace Caffeine::Editor

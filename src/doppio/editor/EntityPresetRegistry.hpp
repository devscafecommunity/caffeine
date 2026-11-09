#pragma once

#include "editor/EntityPresetTypes.hpp"

#include <filesystem>
#include <unordered_map>
#include <vector>

namespace Caffeine::Editor {

class EntityPresetRegistry {
public:
    static EntityPresetRegistry& instance();

    void registerBuiltIns();
    void loadPackagesFromDirectory(const std::filesystem::path& directory, const std::string& sourceLabel);
    void scanProject(const std::filesystem::path& projectRoot);
    void clearExternalPresets();

    bool registerPreset(EntityPresetDescriptor descriptor);
    bool unregisterPreset(const std::string& id);

    const std::vector<EntityPresetCategory>& categories() const { return m_categories; }
    const std::vector<EntityPresetDescriptor>& presets() const { return m_presets; }

    const EntityPresetDescriptor* findPreset(const std::string& id) const;
    std::vector<const EntityPresetDescriptor*> presetsInCategory(const std::string& categoryId) const;

    EntityPresetWizardState makeDefaultWizardState(const EntityPresetDescriptor& preset) const;

private:
    EntityPresetRegistry() = default;

    std::vector<EntityPresetCategory> m_categories;
    std::vector<EntityPresetDescriptor> m_presets;
    std::unordered_map<std::string, usize> m_presetIndex;
};

}  // namespace Caffeine::Editor

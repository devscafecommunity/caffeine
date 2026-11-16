#pragma once

#include "editor/EntityPresetTypes.hpp"

#include <filesystem>
#include <vector>

namespace Caffeine::Editor {

class EntityPresetRegistry;

struct EntityPresetPackageLoadResult {
    std::vector<EntityPresetDescriptor> presets;
    std::string packageName;
    std::string error;
};

EntityPresetPackageLoadResult loadEntityPresetPackage(const std::filesystem::path& manifestPath);

void scanEntityPresetPackages(const std::filesystem::path& rootDirectory,
                              const std::string& sourceLabel,
                              EntityPresetRegistry& registry);

}  // namespace Caffeine::Editor

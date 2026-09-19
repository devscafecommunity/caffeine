#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Caffeine::Editor {

class EditorContext;

struct EntityPresetWizardField {
    enum class Type : u8 { Bool, Float, Int, String, Enum };

    std::string id;
    std::string label;
    std::string hint;
    Type type = Type::Bool;

    bool boolValue = false;
    float floatValue = 0.0f;
    float floatMin = 0.0f;
    float floatMax = 100.0f;
    int intValue = 0;
    int intMin = 0;
    int intMax = 100;
    std::string stringValue;
    std::vector<std::string> enumOptions;
    int enumIndex = 0;
};

struct EntityPresetWizardState {
    std::string entityName = "New Entity";
    std::vector<EntityPresetWizardField> fields;

    bool getBool(const std::string& id, bool fallback = false) const;
    float getFloat(const std::string& id, float fallback = 0.0f) const;
    int getInt(const std::string& id, int fallback = 0) const;
    std::string getString(const std::string& id, const std::string& fallback = {}) const;
    std::string getEnumLabel(const std::string& id, const std::string& fallback = {}) const;
    bool usesCppScript() const;
};

struct EntityPresetSpawnResult {
    ECS::Entity root = ECS::Entity::INVALID;
    std::vector<std::string> createdScriptPaths;
    std::vector<std::string> infoMessages;
    std::string error;
    bool needsRebuild = false;
    bool ok() const { return root.isValid() && error.empty(); }
};

using EntityPresetSpawnFn = std::function<EntityPresetSpawnResult(
    ECS::World& world,
    EditorContext& ctx,
    const std::filesystem::path& projectRoot,
    const EntityPresetWizardState& wizard)>;

struct EntityPresetDescriptor {
    std::string id;
    std::string displayName;
    std::string description;
    std::string category;
    std::string defaultEntityName = "New Entity";
    std::string source = "builtin";
    bool builtIn = true;

    std::filesystem::path packageRoot;
    std::filesystem::path prefabPath;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> scriptCopies;

    std::vector<EntityPresetWizardField> defaultFields;
    EntityPresetSpawnFn spawn;
};

struct EntityPresetCategory {
    std::string id;
    std::string displayName;
    std::string description;
};

}  // namespace Caffeine::Editor

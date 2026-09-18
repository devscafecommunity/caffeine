#pragma once
#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include "ecs/PrefabComponents.hpp"
#include "math/Vec3.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace Caffeine::Editor {

class PrefabSystem {
public:
    static bool CreateFromEntity(ECS::World& world, ECS::Entity entity,
                                 const std::filesystem::path& path);

    static ECS::Entity Instantiate(ECS::World& world, const std::string& prefabPath,
                                   const Vec3& positionOffset = Vec3(0, 0, 0));

    static ECS::Entity FindInstanceRoot(ECS::World& world, ECS::Entity entity);

    static u32 EntityIndex(ECS::World& world, ECS::Entity entity);

    static void RecordOverride(ECS::World& world, ECS::Entity entity,
                               const std::string& componentName, const std::string& propertyName,
                               const std::vector<u8>& value);

    static bool IsOverridden(ECS::World& world, ECS::Entity entity,
                             const std::string& componentName, const std::string& propertyName);

    static void RevertOverrides(ECS::World& world, ECS::Entity instanceRoot);

    static bool ApplyOverridesToPrefab(ECS::World& world, ECS::Entity instanceRoot);

    static void PropagateChanges(ECS::World& world, const std::string& prefabPath);

    static std::vector<u8> SerializeVec3(const Vec3& v);
    static Vec3 DeserializeVec3(const std::vector<u8>& data);
};

}  // namespace Caffeine::Editor

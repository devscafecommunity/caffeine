#pragma once
#include "assets/PrefabAsset.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "math/Vec3.hpp"
#include <string>
#include <unordered_map>

namespace Caffeine::Assets {

using namespace Caffeine;

class PrefabSerializer {
public:
    PrefabSerializer(ECS::World& world) : m_world(world) {}
    
    bool save(const std::string& filePath, ECS::Entity rootEntity);
    ECS::Entity load(const std::string& filePath, const Vec3& positionOffset = {0, 0, 0});

private:
    ECS::World& m_world;
    
    static constexpr u32 kTypeName          = 0;
    static constexpr u32 kTypeTransform     = 1;
    static constexpr u32 kTypeAcceleration2D = 3;
    static constexpr u32 kTypeSprite        = 6;
    static constexpr u32 kTypeTag           = 8;
    static constexpr u32 kTypeAudioEmitter  = 9;
    static constexpr u32 kTypeParent        = 10;
    static constexpr u32 kTypeLight         = 11;
    static constexpr u32 kTypeDirLight      = 12;
    static constexpr u32 kTypePointLight    = 13;
    static constexpr u32 kTypeSpotLight     = 14;
    static constexpr u32 kTypePosition3D    = 15;
    static constexpr u32 kTypeRotation3D    = 16;
    static constexpr u32 kTypeScale3D       = 17;
    static constexpr u32 kTypeMeshFilter    = 18;
    static constexpr u32 kTypeMeshRenderer  = 19;
    
    PrefabAsset::EntityData serializeEntity(ECS::Entity entity, std::unordered_map<u32, u32>& entityIdMap);
    ECS::Entity deserializeEntity(const PrefabAsset::EntityData& data, std::vector<ECS::Entity>& outCreatedEntities);
    void applyComponentData(ECS::Entity entity, u32 typeId, const u8* data, u32 size);
};

}

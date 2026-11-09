#pragma once
#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include <vector>
#include <string>

namespace Caffeine::Assets {

struct PrefabAsset {
    struct ComponentEntry {
        u32 typeId;
        std::vector<u8> data;
    };
    
    struct EntityData {
        std::string name;
        std::vector<ComponentEntry> components;
        std::vector<u32> childEntityIds;
    };
    
    std::vector<EntityData> entities;
    
    static constexpr u32 MAGIC = 0xCAF0B01F;
    static constexpr u32 VERSION = 1;
};

}

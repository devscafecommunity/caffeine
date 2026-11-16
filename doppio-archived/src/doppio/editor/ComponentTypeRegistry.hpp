#pragma once

#include "ecs/ComponentID.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"
#include "core/Types.hpp"

#include <functional>
#include <string>
#include <unordered_map>

namespace Caffeine::Editor {

class ComponentTypeRegistry {
public:
    static ComponentTypeRegistry& instance() {
        static ComponentTypeRegistry registry;
        return registry;
    }

    template<typename T>
    void registerType(const char* name) {
        const u32 id = ECS::ComponentID::get<T>();
        m_ids[name] = id;
        m_has[id] = [](ECS::World& world, ECS::Entity entity) {
            return world.has<T>(entity);
        };
    }

    u32 lookup(const char* name) const {
        if (!name) return u32_max;
        auto it = m_ids.find(name);
        return it != m_ids.end() ? it->second : u32_max;
    }

    bool hasComponent(ECS::World& world, ECS::Entity entity, u32 typeId) const {
        auto it = m_has.find(typeId);
        return it != m_has.end() && it->second(world, entity);
    }

private:
    std::unordered_map<std::string, u32> m_ids;
    std::unordered_map<u32, std::function<bool(ECS::World&, ECS::Entity)>> m_has;
};

}  // namespace Caffeine::Editor

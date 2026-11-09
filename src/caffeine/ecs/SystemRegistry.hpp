#pragma once

#include "core/Types.hpp"
#include "ecs/ISystem.hpp"
#include <memory>
#include <vector>

namespace Caffeine::ECS {

class World;

class SystemRegistry {
public:
    SystemRegistry() = default;
    ~SystemRegistry() = default;
    
    void add(std::unique_ptr<ISystem> system) {
        m_systems.push_back(std::move(system));
    }
    
    void updateAll(World& world, f32 deltaTime) {
        for (auto& system : m_systems) {
            if (system) {
                system->onUpdate(world, deltaTime);
            }
        }
    }
    
    usize count() const {
        return m_systems.size();
    }
    
private:
    std::vector<std::unique_ptr<ISystem>> m_systems;
};

}

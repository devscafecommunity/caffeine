#pragma once
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "core/Types.hpp"
#include <functional>
#include <vector>
#include <string>

namespace Caffeine::Editor {

struct ComponentEntry {
    std::string category;
    std::string name;
    std::function<bool(ECS::World&, ECS::Entity)> has;
    std::function<void(ECS::World&, ECS::Entity)> add;
};

class ComponentRegistry {
public:
    static ComponentRegistry& instance();
    void registerComponent(ComponentEntry entry);
    const std::vector<ComponentEntry>& entries() const { return m_entries; }
private:
    std::vector<ComponentEntry> m_entries;
};

void registerAllComponents(ComponentRegistry& reg);

} // namespace Caffeine::Editor

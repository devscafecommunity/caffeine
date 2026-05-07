#pragma once

#include "core/Types.hpp"
#include "ecs/ComponentSet.hpp"
#include "ecs/ComponentID.hpp"
#include "ecs/Archetype.hpp"
#include "containers/Vector.hpp"

namespace Caffeine {
namespace ECS {

class ComponentQuery {
public:
    ComponentQuery() = default;
    
    template<typename T, typename... Ts>
    ComponentQuery& with() {
        addRequired<T>();
        if constexpr (sizeof...(Ts) > 0) {
            with<Ts...>();
        }
        return *this;
    }
    
    template<typename T, typename... Ts>
    ComponentQuery& without() {
        addExcluded<T>();
        if constexpr (sizeof...(Ts) > 0) {
            without<Ts...>();
        }
        return *this;
    }
    
    template<typename T, typename... Ts>
    ComponentQuery& any() {
        ComponentSet anySet;
        addToAnySet<T, Ts...>(anySet);
        m_any.pushBack(anySet);
        return *this;
    }
    
    bool matches(const Archetype* archetype) const {
        if (!archetype) {
            return false;
        }
        
        const ComponentSet& archetypeComponents = archetype->getComponentSet();
        
        if (!archetypeComponents.matches(m_required)) {
            return false;
        }
        
        for (u32 i = 0; i < 64; ++i) {
            if (m_excluded.has(i) && archetypeComponents.has(i)) {
                return false;
            }
        }
        
        for (usize i = 0; i < m_any.size(); ++i) {
            bool hasAny = false;
            const ComponentSet& anySet = m_any[i];
            for (u32 j = 0; j < 64; ++j) {
                if (anySet.has(j) && archetypeComponents.has(j)) {
                    hasAny = true;
                    break;
                }
            }
            if (!hasAny) {
                return false;
            }
        }
        
        return true;
    }
    
private:
    template<typename T>
    void addRequired() {
        m_required.add(ComponentID::get<T>());
    }
    
    template<typename T>
    void addExcluded() {
        m_excluded.add(ComponentID::get<T>());
    }
    
    template<typename T, typename... Ts>
    void addToAnySet(ComponentSet& anySet) {
        anySet.add(ComponentID::get<T>());
        if constexpr (sizeof...(Ts) > 0) {
            addToAnySet<Ts...>(anySet);
        }
    }
    
    ComponentSet m_required;
    ComponentSet m_excluded;
    Vector<ComponentSet> m_any;
};

} // namespace ECS
} // namespace Caffeine

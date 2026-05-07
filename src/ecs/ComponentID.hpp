// ============================================================================
// @file    ComponentID.hpp
// @brief   Component type ID registration system for ECS
// @note    Thread-safe unique ID generation per component type
// ============================================================================
#pragma once

#include "core/Types.hpp"
#include "core/Assertions.hpp"
#include <atomic>

namespace Caffeine::ECS {

class ComponentID {
public:
    static constexpr u32 MAX_COMPONENTS = 256;
    
    template<typename T>
    static u32 get() {
        static const u32 id = nextID();
        return id;
    }
    
private:
    static inline std::atomic<u32> s_nextID{0};
    
    static u32 nextID() {
        u32 id = s_nextID.fetch_add(1, std::memory_order_relaxed);
        CF_ASSERT(id < MAX_COMPONENTS, "Exceeded maximum number of component types");
        return id;
    }
};

}

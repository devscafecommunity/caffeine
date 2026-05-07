// ============================================================================
// @file    ComponentSet.hpp
// @brief   Bitset-based component type storage for archetypes
// @note    Uses 64-bit bitfield, max 64 component types
// ============================================================================
#pragma once

#include "core/Types.hpp"
#include "core/Compiler.hpp"
#include "core/Assertions.hpp"

#if defined(CF_COMPILER_MSVC)
    #include <intrin.h>
#endif

#include <functional>

namespace Caffeine::ECS {

class ComponentSet {
public:
    ComponentSet() = default;
    
    ComponentSet& add(u32 componentID) {
        CF_ASSERT(componentID < 64, "Component ID out of range");
        m_bits |= (1ULL << componentID);
        return *this;
    }
    
    ComponentSet& remove(u32 componentID) {
        CF_ASSERT(componentID < 64, "Component ID out of range");
        m_bits &= ~(1ULL << componentID);
        return *this;
    }
    
    bool has(u32 componentID) const {
        CF_ASSERT(componentID < 64, "Component ID out of range");
        return (m_bits & (1ULL << componentID)) != 0;
    }
    
    u32 count() const {
#if defined(CF_COMPILER_MSVC)
        return static_cast<u32>(__popcnt64(m_bits));
#else
        return static_cast<u32>(__builtin_popcountll(m_bits));
#endif
    }
    
    bool isEmpty() const {
        return m_bits == 0;
    }
    
    bool matches(const ComponentSet& other) const {
        return (m_bits & other.m_bits) == other.m_bits;
    }
    
    u64 hash() const {
        return m_bits;
    }
    
    bool operator==(const ComponentSet& other) const {
        return m_bits == other.m_bits;
    }
    
    bool operator!=(const ComponentSet& other) const {
        return m_bits != other.m_bits;
    }
    
private:
    u64 m_bits = 0;
};

}

namespace std {
    template<>
    struct hash<Caffeine::ECS::ComponentSet> {
        std::size_t operator()(const Caffeine::ECS::ComponentSet& set) const {
            return static_cast<std::size_t>(set.hash());
        }
    };
}

// ============================================================================
// @file    ComponentSet.hpp
// @brief   Bitset-based component type storage for archetypes
// ============================================================================
#pragma once

#include "core/Types.hpp"
#include "core/Compiler.hpp"
#include "core/Assertions.hpp"

#if defined(CF_COMPILER_MSVC)
    #include <intrin.h>
#endif

#include <array>
#include <functional>

namespace Caffeine::ECS {

class ComponentSet {
public:
    static constexpr u32 kMaxComponents = 256;
    static constexpr u32 kWordCount = kMaxComponents / 64;

    ComponentSet() = default;

    ComponentSet& add(u32 componentID) {
        CF_ASSERT(componentID < kMaxComponents, "Component ID out of range");
        if (componentID >= kMaxComponents) return *this;
        m_bits[componentID / 64] |= (1ULL << (componentID % 64));
        return *this;
    }

    ComponentSet& remove(u32 componentID) {
        CF_ASSERT(componentID < kMaxComponents, "Component ID out of range");
        if (componentID >= kMaxComponents) return *this;
        m_bits[componentID / 64] &= ~(1ULL << (componentID % 64));
        return *this;
    }

    bool has(u32 componentID) const {
        if (componentID >= kMaxComponents) return false;
        return (m_bits[componentID / 64] & (1ULL << (componentID % 64))) != 0;
    }

    u32 count() const {
        u32 n = 0;
        for (u64 word : m_bits) {
#if defined(CF_COMPILER_MSVC)
            n += static_cast<u32>(__popcnt64(word));
#else
            n += static_cast<u32>(__builtin_popcountll(word));
#endif
        }
        return n;
    }

    bool isEmpty() const {
        for (u64 word : m_bits) {
            if (word != 0) return false;
        }
        return true;
    }

    bool matches(const ComponentSet& other) const {
        for (u32 i = 0; i < kWordCount; ++i) {
            if ((m_bits[i] & other.m_bits[i]) != other.m_bits[i]) return false;
        }
        return true;
    }

    u64 hash() const {
        u64 h = 14695981039346656037ULL;
        for (u64 word : m_bits) {
            h ^= word;
            h *= 1099511628211ULL;
        }
        return h;
    }

    template<typename Fn>
    void forEachComponentId(Fn&& fn) const {
        for (u32 i = 0; i < kMaxComponents; ++i) {
            if (has(i)) fn(i);
        }
    }

    bool operator==(const ComponentSet& other) const { return m_bits == other.m_bits; }
    bool operator!=(const ComponentSet& other) const { return m_bits != other.m_bits; }

private:
    std::array<u64, kWordCount> m_bits{};
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

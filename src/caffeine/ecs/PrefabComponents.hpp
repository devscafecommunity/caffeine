#pragma once
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace Caffeine::ECS {
using namespace Caffeine;

struct PrefabOverride {
    u32 entityIndex = 0;
    std::string componentName;
    std::string propertyName;
    std::vector<u8> value;

    bool matches(u32 index, const std::string& comp, const std::string& prop) const {
        return entityIndex == index && componentName == comp && propertyName == prop;
    }
};

struct PrefabInstance {
    std::string prefabPath;
    u32 rootEntityId = 0;
    std::unordered_map<u32, u32> entityIndexMap;

    std::vector<PrefabOverride> overrides;

    bool isOverridden(u32 entityIndex, const std::string& comp, const std::string& prop) const {
        for (const auto& o : overrides) {
            if (o.matches(entityIndex, comp, prop)) return true;
        }
        return false;
    }

    void setOverride(u32 entityIndex, const std::string& comp, const std::string& prop,
                     const std::vector<u8>& value) {
        for (auto& o : overrides) {
            if (o.matches(entityIndex, comp, prop)) {
                o.value = value;
                return;
            }
        }
        overrides.push_back({entityIndex, comp, prop, value});
    }

    void clearOverride(u32 entityIndex, const std::string& comp, const std::string& prop) {
        overrides.erase(
            std::remove_if(overrides.begin(), overrides.end(),
                           [&](const PrefabOverride& o) { return o.matches(entityIndex, comp, prop); }),
            overrides.end());
    }

    void clearAllOverrides() { overrides.clear(); }

    u32 indexForEntity(u32 runtimeEntityId) const {
        for (const auto& [index, eid] : entityIndexMap) {
            if (eid == runtimeEntityId) return index;
        }
        return u32_max;
    }
};

}  // namespace Caffeine::ECS

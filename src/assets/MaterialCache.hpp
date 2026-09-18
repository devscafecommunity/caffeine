#pragma once

#include "assets/MaterialTypes.hpp"
#include <string>
#include <unordered_map>

namespace Caffeine::Assets {

class MaterialCache {
public:
    static MaterialCache& instance();

    MaterialSurface resolve(const std::string& materialPath,
                            const std::string& projectRoot = "");
    void invalidate(const std::string& materialPath = {});

private:
    MaterialCache() = default;

    std::unordered_map<std::string, MaterialSurface> m_cache;
};

}  // namespace Caffeine::Assets

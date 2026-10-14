#pragma once

#include "core/Types.hpp"

#include <string>
#include <unordered_map>

namespace Caffeine::Script {

/// Load text assets (JSON, CSV, etc.) relative to project search root.
class AssetTextLoader {
public:
    void setSearchRoot(const std::string& root);
    void clearCache();

    bool load(const std::string& path, std::string& outText, std::string* outError = nullptr);
    bool loadCached(const std::string& path, std::string& outText, std::string* outError = nullptr);

private:
    std::string resolvePath(const std::string& path) const;

    std::string m_searchRoot;
    std::unordered_map<std::string, std::string> m_cache;
};

}  // namespace Caffeine::Script

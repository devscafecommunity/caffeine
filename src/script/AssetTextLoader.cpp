#include "script/AssetTextLoader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Caffeine::Script {

void AssetTextLoader::setSearchRoot(const std::string& root) {
    m_searchRoot = root;
    clearCache();
}

void AssetTextLoader::clearCache() {
    m_cache.clear();
}

std::string AssetTextLoader::resolvePath(const std::string& path) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(path, ec)) return path;
    if (m_searchRoot.empty()) return path;

    const fs::path root(m_searchRoot);
    const fs::path p(path);
    const fs::path candidates[] = {
        root / p,
        root / "assets" / p,
        root / "data" / p,
        root / "data" / "assets" / p,
        root / p.filename(),
    };
    for (const auto& c : candidates) {
        if (fs::exists(c, ec)) return c.string();
    }
    return path;
}

bool AssetTextLoader::load(const std::string& path, std::string& outText, std::string* outError) {
    const std::string file = resolvePath(path);
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (outError) *outError = "AssetText: cannot open " + file;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    outText = ss.str();
    return true;
}

bool AssetTextLoader::loadCached(const std::string& path, std::string& outText,
                                 std::string* outError) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        outText = it->second;
        return true;
    }
    if (!load(path, outText, outError)) return false;
    m_cache[path] = outText;
    return true;
}

}  // namespace Caffeine::Script

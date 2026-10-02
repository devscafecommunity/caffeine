#include "assets/MaterialCache.hpp"

#include "assets/MaterialFile.hpp"

#include <filesystem>

namespace Caffeine::Assets {

MaterialCache& MaterialCache::instance() {
    static MaterialCache cache;
    return cache;
}

std::string MaterialCache::canonicalKey(const std::string& materialPath,
                                        const std::string& projectRoot) const {
    if (materialPath.empty()) return {};
    std::filesystem::path resolved(materialPath);
    if (!resolved.is_absolute() && !projectRoot.empty()) {
        resolved = std::filesystem::path(projectRoot) / materialPath;
    }
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(resolved, ec);
    if (ec) return resolved.string();
    return canonical.string();
}

MaterialSurface MaterialCache::resolve(const std::string& materialPath,
                                       const std::string& projectRoot) {
    MaterialSurface empty;
    const std::string key = canonicalKey(materialPath, projectRoot);
    if (key.empty()) return empty;
    auto it = m_cache.find(key);
    if (!std::filesystem::exists(key)) {
        // Published-only materials (e.g. the editor preview) have no file behind them.
        if (it != m_cache.end() && !it->second.hasStamp) return it->second.surface;
        return empty;
    }

    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(key, ec);
    if (it != m_cache.end() && !ec && it->second.hasStamp && it->second.stamp == stamp) {
        return it->second.surface;
    }

    MaterialSurface surface;
    if (!loadMaterialFile(key, surface)) return empty;
    Entry entry;
    entry.surface = surface;
    entry.stamp = stamp;
    entry.hasStamp = !ec;
    if (it != m_cache.end() && it->second.hasStamp && entry.hasStamp && it->second.stamp != stamp) {
        ++m_revision;
    }
    m_cache[key] = entry;
    return surface;
}

void MaterialCache::publish(const std::string& materialPath, const std::string& projectRoot,
                            const MaterialSurface& surface) {
    const std::string key = canonicalKey(materialPath, projectRoot);
    if (key.empty()) return;
    Entry entry;
    entry.surface = surface;
    entry.surface.valid = true;
    std::error_code ec;
    if (std::filesystem::exists(key)) {
        entry.stamp = std::filesystem::last_write_time(key, ec);
        entry.hasStamp = !ec;
    }
    m_cache[key] = entry;
    ++m_revision;
}

void MaterialCache::invalidate(const std::string& materialPath) {
    ++m_revision;
    if (materialPath.empty()) {
        m_cache.clear();
        return;
    }
    m_cache.erase(materialPath);
    m_cache.erase(canonicalKey(materialPath, ""));
}

}  // namespace Caffeine::Assets

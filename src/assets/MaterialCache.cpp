#include "assets/MaterialCache.hpp"

#include "assets/MaterialFile.hpp"

#include <filesystem>

namespace Caffeine::Assets {
namespace {

bool sameSurface(const MaterialSurface& a, const MaterialSurface& b) {
    return a.name == b.name && a.albedo.x == b.albedo.x && a.albedo.y == b.albedo.y &&
           a.albedo.z == b.albedo.z && a.albedo.w == b.albedo.w && a.metallic == b.metallic &&
           a.roughness == b.roughness && a.reflectance == b.reflectance &&
           a.emission.x == b.emission.x && a.emission.y == b.emission.y &&
           a.emission.z == b.emission.z && a.emissionStrength == b.emissionStrength &&
           a.albedoMap == b.albedoMap && a.normalMap == b.normalMap && a.ormMap == b.ormMap &&
           a.emissionMap == b.emissionMap && a.uvTiling.x == b.uvTiling.x &&
           a.uvTiling.y == b.uvTiling.y && a.uvOffset.x == b.uvOffset.x &&
           a.uvOffset.y == b.uvOffset.y && a.normalStrength == b.normalStrength &&
           a.aoStrength == b.aoStrength && a.alphaMode == b.alphaMode &&
           a.alphaCutoff == b.alphaCutoff && a.transmission == b.transmission && a.ior == b.ior &&
           a.clearcoat == b.clearcoat && a.clearcoatRoughness == b.clearcoatRoughness &&
           a.sheenColor.x == b.sheenColor.x && a.sheenColor.y == b.sheenColor.y &&
           a.sheenColor.z == b.sheenColor.z && a.sheenRoughness == b.sheenRoughness &&
           a.iridescence == b.iridescence && a.iridescenceThickness == b.iridescenceThickness &&
           a.iridescenceIor == b.iridescenceIor && a.reflection == b.reflection &&
           a.reflectionBudget == b.reflectionBudget &&
           a.reflectionPerformance == b.reflectionPerformance &&
           a.reflectionQuality == b.reflectionQuality && a.reflectionPlanar == b.reflectionPlanar &&
           a.ssrResolution == b.ssrResolution && a.ssrMaxSteps == b.ssrMaxSteps &&
           a.ssrTemporalFrames == b.ssrTemporalFrames && a.ssrDistance == b.ssrDistance &&
           a.ssrSamples == b.ssrSamples && a.ssrDenoise == b.ssrDenoise &&
           a.ssrProbeBlend == b.ssrProbeBlend && a.ssrBounces == b.ssrBounces;
}

}  // namespace

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
    const auto it = m_cache.find(key);
    const bool changed = it == m_cache.end() || !it->second.hasStamp != !entry.hasStamp ||
                         (entry.hasStamp && it->second.stamp != entry.stamp) ||
                         !sameSurface(it->second.surface, entry.surface);
    m_cache[key] = entry;
    if (changed) ++m_revision;
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

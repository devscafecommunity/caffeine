#include "assets/MaterialCache.hpp"

#include "editor/MaterialSerializer.hpp"

#include <filesystem>

namespace Caffeine::Assets {

MaterialCache& MaterialCache::instance() {
    static MaterialCache cache;
    return cache;
}

MaterialSurface MaterialCache::resolve(const std::string& materialPath,
                                       const std::string& projectRoot) {
    MaterialSurface empty;
    if (materialPath.empty()) return empty;

    std::filesystem::path resolved(materialPath);
    if (!resolved.is_absolute() && !projectRoot.empty()) {
        resolved = std::filesystem::path(projectRoot) / materialPath;
    }
    std::error_code ec;
    resolved = std::filesystem::weakly_canonical(resolved, ec);
    if (ec || !std::filesystem::exists(resolved)) return empty;

    const std::string key = resolved.string();
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    Editor::MaterialDocument doc;
    Editor::ShaderGraph graph;
    if (!Editor::MaterialSerializer::load(resolved, doc, graph)) return empty;

    const Editor::EvaluatedMaterial evaluated = graph.evaluateMaterial(0.0f);
    MaterialSurface surface;
    if (evaluated.valid) {
        surface.albedo = evaluated.albedo;
        surface.metallic = evaluated.metallic;
        surface.roughness = evaluated.roughness;
        surface.valid = true;
    } else {
        surface.albedo = Vec4(doc.properties.albedoColor.r, doc.properties.albedoColor.g,
                              doc.properties.albedoColor.b, doc.properties.albedoColor.a);
        surface.metallic = doc.properties.metallic;
        surface.roughness = doc.properties.roughness;
        surface.valid = true;
    }

    m_cache[key] = surface;
    return surface;
}

void MaterialCache::invalidate(const std::string& materialPath) {
    if (materialPath.empty()) {
        m_cache.clear();
        return;
    }
    m_cache.erase(materialPath);
}

}  // namespace Caffeine::Assets

#pragma once
#include "assets/MeshTypes.hpp"
#include <unordered_map>
#include <string>
#include <vector>

namespace Caffeine::Assets {

class MeshCache {
public:
    static MeshCache& getInstance();

    /// Load mesh, trying multiple path candidates when projectRoot is provided.
    Mesh3D* getMesh(const std::string& path, const std::string& projectRoot = "");

    const std::string& getLastError() const { return m_lastError; }
    const std::string& getResolvedPath() const { return m_lastResolvedPath; }

    static std::vector<std::string> buildCandidatePaths(const std::string& path,
                                                        const std::string& projectRoot);

    void clear();
    void remove(const std::string& path);

private:
    MeshCache() = default;
    ~MeshCache();

    Mesh3D* loadFromResolvedPath(const std::string& path);

    std::unordered_map<std::string, Mesh3D*> m_cache;
    std::string m_lastError;
    std::string m_lastResolvedPath;
};

}  // namespace Caffeine::Assets

#pragma once
#include "assets/MeshTypes.hpp"
#include <unordered_map>
#include <string>

namespace Caffeine::Assets {

class MeshCache {
public:
    static MeshCache& getInstance();
    
    // Get or load mesh from cache
    Mesh3D* getMesh(const std::string& path);
    
    // Clear cache
    void clear();
    
    // Remove single entry
    void remove(const std::string& path);

private:
    MeshCache() = default;
    ~MeshCache();
    
    std::unordered_map<std::string, Mesh3D*> m_cache;
};

}  // namespace Caffeine::Assets

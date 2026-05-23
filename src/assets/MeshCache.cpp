#include "assets/MeshCache.hpp"
#include "assets/MeshLoader.hpp"
#include <cstdio>

namespace Caffeine::Assets {

MeshCache& MeshCache::getInstance() {
    static MeshCache instance;
    return instance;
}

Mesh3D* MeshCache::getMesh(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second;
    }
    
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        return nullptr;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        return nullptr;
    }
    
    std::vector<u8> buffer(size);
    fread(buffer.data(), 1, size, f);
    fclose(f);
    
    Mesh3D* mesh = MeshLoader::parseGLTF(buffer.data(), buffer.size(), path.c_str());
    if (mesh) {
        m_cache[path] = mesh;
    }
    
    return mesh;
}

void MeshCache::clear() {
    for (auto& pair : m_cache) {
        delete pair.second;
    }
    m_cache.clear();
}

void MeshCache::remove(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        delete it->second;
        m_cache.erase(it);
    }
}

MeshCache::~MeshCache() {
    clear();
}

}  // namespace Caffeine::Assets

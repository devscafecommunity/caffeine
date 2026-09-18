#include "assets/MeshCache.hpp"
#include "assets/MeshLoader.hpp"
#include "assets/MeshImportValidator.hpp"
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace Caffeine::Assets {

namespace {

std::string extensionLower(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

void appendUnique(std::vector<std::string>& out, const std::filesystem::path& candidate) {
    if (candidate.empty()) return;
    std::error_code ec;
    const std::string normalized = std::filesystem::weakly_canonical(candidate, ec).string();
    const std::string& use = ec ? candidate.string() : normalized;
    if (std::find(out.begin(), out.end(), use) == out.end()) {
        out.push_back(use);
    }
}

Mesh3D* loadMeshFromBuffer(const std::string& path, const std::vector<u8>& buffer,
                           std::string& outError) {
    if (buffer.empty()) {
        outError = "Ficheiro vazio";
        return nullptr;
    }

    const std::string ext = extensionLower(path);
    if (ext == ".obj") {
        Mesh3D* mesh = MeshLoader::parseOBJ(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        if (!mesh) outError = "Falha ao interpretar OBJ";
        return mesh;
    }
    if (ext == ".gltf" || ext == ".glb") {
        const MeshImportReport report = MeshImportValidator::analyze(path);
        if (!report.readyToLoad) {
            outError = report.errorSummary;
            if (!report.suggestion.empty()) {
                outError += " — " + report.suggestion;
            }
            return nullptr;
        }

        std::string parseError;
        Mesh3D* mesh = MeshLoader::parseGLTF(buffer.data(), buffer.size(), path.c_str(), &parseError);
        if (!mesh) {
            outError = parseError.empty() ? "Falha ao interpretar glTF" : parseError;
        }
        return mesh;
    }
    if (ext == ".fbx") {
        std::filesystem::path objPath(path);
        objPath.replace_extension(".obj");
        if (objPath.string() != path && std::filesystem::exists(objPath)) {
            return MeshCache::getInstance().getMesh(objPath.string());
        }
        outError = "FBX nao suportado — exporte como OBJ ou glTF";
        return nullptr;
    }

    outError = "Formato de mesh nao suportado";
    return nullptr;
}

bool readFileBytes(const std::string& path, std::vector<u8>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return false;
    }

    out.resize(static_cast<size_t>(size));
    const size_t read = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return read == out.size();
}

} // namespace

MeshCache& MeshCache::getInstance() {
    static MeshCache instance;
    return instance;
}

std::vector<std::string> MeshCache::buildCandidatePaths(const std::string& path,
                                                         const std::string& projectRoot) {
    std::vector<std::string> candidates;
    const std::filesystem::path input(path);

    appendUnique(candidates, input);
    if (input.is_absolute()) {
        return candidates;
    }

    appendUnique(candidates, std::filesystem::current_path() / input);
    appendUnique(candidates, std::filesystem::path("assets/raw") / input.filename());
    appendUnique(candidates, std::filesystem::path("assets/raw") / input);

    if (!projectRoot.empty()) {
        const std::filesystem::path root(projectRoot);
        appendUnique(candidates, root / input);
        appendUnique(candidates, root / "assets/raw" / input.filename());
        appendUnique(candidates, root / "assets/raw" / input);
        // Packaged game layout (build output): data/assets/raw/...
        appendUnique(candidates, root / "data" / input);
        appendUnique(candidates, root / "data/assets/raw" / input.filename());
        appendUnique(candidates, root / "data/assets/raw" / input);
        appendUnique(candidates, root / "data/assets/processed" / input.filename());
        appendUnique(candidates, root / "data/assets/processed" / input);
    }

    return candidates;
}

Mesh3D* MeshCache::getMesh(const std::string& path, const std::string& projectRoot) {
    m_lastError.clear();
    m_lastResolvedPath.clear();

    if (path.empty()) {
        m_lastError = "Caminho do mesh vazio";
        return nullptr;
    }

    for (const std::string& candidate : buildCandidatePaths(path, projectRoot)) {
        auto it = m_cache.find(candidate);
        if (it != m_cache.end()) {
            m_lastResolvedPath = candidate;
            const std::string ext = extensionLower(candidate);
            if (ext == ".gltf" || ext == ".glb") {
                it->second->flipTextureV = false;
            } else if (ext == ".obj") {
                it->second->flipTextureV = true;
            }
            return it->second;
        }

        Mesh3D* mesh = loadFromResolvedPath(candidate);
        if (mesh) {
            m_lastResolvedPath = candidate;
            const std::string ext = extensionLower(candidate);
            if (ext == ".gltf" || ext == ".glb") {
                mesh->flipTextureV = false;
            } else if (ext == ".obj") {
                mesh->flipTextureV = true;
            }
            return mesh;
        }
    }

    if (m_lastError.empty()) {
        m_lastError = "Ficheiro nao encontrado: " + path;
    }
    return nullptr;
}

Mesh3D* MeshCache::loadFromResolvedPath(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        return it->second;
    }

    std::vector<u8> buffer;
    if (!readFileBytes(path, buffer)) {
        return nullptr;
    }

    std::string error;
    Mesh3D* mesh = loadMeshFromBuffer(path, buffer, error);
    if (mesh) {
        m_cache[path] = mesh;
        return mesh;
    }

    if (!error.empty()) {
        m_lastError = error;
    }
    return nullptr;
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

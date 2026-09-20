#include "assets/MeshCache.hpp"
#include "assets/MeshLoader.hpp"
#include "assets/MeshImportValidator.hpp"
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>

#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#endif

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

std::filesystem::path findEngineAssetsRoot() {
    std::vector<std::filesystem::path> roots;
#ifdef CAFFEINE_SOURCE_DIR
    roots.push_back(std::filesystem::path(CAFFEINE_SOURCE_DIR) / "assets");
#endif
    roots.push_back(std::filesystem::current_path() / "assets");
    roots.push_back(std::filesystem::current_path() / ".." / "assets");

#ifdef __linux__
    char exePath[PATH_MAX] = {};
    const ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        roots.push_back(exeDir / "assets");
        roots.push_back(exeDir / "data" / "assets");
        roots.push_back(exeDir / ".." / "assets");
    }
#endif
    roots.push_back(std::filesystem::current_path() / "data" / "assets");

    for (const auto& root : roots) {
        std::error_code ec;
        if ((std::filesystem::exists(root / "kenney_prototype-textures", ec) && !ec) ||
            (std::filesystem::exists(root / "kenney_skyboxes", ec) && !ec) ||
            (std::filesystem::exists(root / "raw", ec) && !ec)) {
            return std::filesystem::weakly_canonical(root, ec);
        }
    }
    return {};
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

std::string MeshCache::normalizeTexturePath(const std::string& path) {
    if (path.empty()) return path;

    std::string normalized = path;
    for (size_t pos = 0;
         (pos = normalized.find("kenney-prototype", pos)) != std::string::npos;) {
        normalized.replace(pos, 16, "kenney_prototype");
        pos += 17;
    }

    while (!normalized.empty() && (normalized.back() == '/' || normalized.back() == '\\')) {
        normalized.pop_back();
    }

    const std::filesystem::path p(normalized);
    const std::string ext = p.extension().string();
    if (ext.empty() || ext == ".") {
        if (normalized.find("kenney_prototype-textures") != std::string::npos) {
            normalized += "/Light/texture_07.png";
        } else if (!normalized.empty()) {
            normalized += "/texture_07.png";
        }
    }

    return normalized;
}

std::string MeshCache::resolveTexturePath(const std::string& path, const std::string& projectRoot) {
    if (path.empty()) return {};

    std::vector<std::string> candidates;
    auto addCandidates = [&](const std::string& candidatePath) {
        if (candidatePath.empty()) return;
        for (const std::string& candidate : buildCandidatePaths(candidatePath, projectRoot)) {
            appendUnique(candidates, candidate);
        }
    };

    addCandidates(path);
    const std::string fixed = normalizeTexturePath(path);
    if (fixed != path) {
        addCandidates(fixed);
    }

    for (const std::string& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::string> MeshCache::buildCandidatePaths(const std::string& path,
                                                         const std::string& projectRoot) {
    std::vector<std::string> candidates;
    const std::filesystem::path input(path);

    appendUnique(candidates, input);
    if (input.is_absolute()) {
        appendUnique(candidates, input.filename());
        if (!projectRoot.empty()) {
            const std::filesystem::path root(projectRoot);
            appendUnique(candidates, root / "assets" / "raw" / input.filename());
            appendUnique(candidates, root / "data" / "assets" / "raw" / input.filename());
            appendUnique(candidates, root / "data" / "assets" / "processed" / input.filename());
            const std::string generic = input.generic_string();
            auto assetsPos = generic.find("/assets/");
            if (assetsPos == std::string::npos) assetsPos = generic.find("\\assets\\");
            if (assetsPos != std::string::npos) {
                appendUnique(candidates, root / generic.substr(assetsPos + 1));
                appendUnique(candidates, root / "data" / generic.substr(assetsPos + 1));
            }
        }
    }

    if (!input.is_absolute()) {

    appendUnique(candidates, std::filesystem::current_path() / input);
    appendUnique(candidates, std::filesystem::path("assets/raw") / input.filename());
    appendUnique(candidates, std::filesystem::path("assets/raw") / input);

    if (!projectRoot.empty()) {
        const std::filesystem::path root(projectRoot);
        appendUnique(candidates, root / input);
        appendUnique(candidates, root / "assets" / input);
        appendUnique(candidates, root / "assets/raw" / input.filename());
        appendUnique(candidates, root / "assets/raw" / input);
        // Packaged game layout (build output): data/assets/raw/...
        appendUnique(candidates, root / "data" / input);
        appendUnique(candidates, root / "data/assets/raw" / input.filename());
        appendUnique(candidates, root / "data/assets/raw" / input);
        appendUnique(candidates, root / "data/assets/processed" / input.filename());
        appendUnique(candidates, root / "data/assets/processed" / input);
    }
    }

    const std::filesystem::path engineAssets = findEngineAssetsRoot();
    if (!engineAssets.empty()) {
        appendUnique(candidates, engineAssets / input);
        appendUnique(candidates, engineAssets / input.filename());
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

#ifdef CF_HAS_SDL3
void MeshCache::releaseGpuResources(RHI::RenderDevice* device) {
    if (!device) return;
    for (auto& pair : m_cache) {
        if (!pair.second) continue;
        Mesh3D& mesh = *pair.second;
        if (mesh.vertexBuffer) {
            device->destroyBuffer(mesh.vertexBuffer);
            mesh.vertexBuffer = nullptr;
        }
        if (mesh.indexBuffer) {
            device->destroyBuffer(mesh.indexBuffer);
            mesh.indexBuffer = nullptr;
        }
    }
}
#endif

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

#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Caffeine::Assets {

struct MeshImportReport {
    bool meshFileExists = false;
    bool readyToLoad = false;
    std::string format;
    std::vector<std::string> dependencies;
    std::vector<std::string> missingDependencies;
    std::vector<std::string> presentDependencies;
    std::string errorSummary;
    std::string suggestion;
};

class MeshImportValidator {
public:
    static MeshImportReport analyze(const std::filesystem::path& meshPath);

    /// External URI strings referenced by a .gltf file (excludes data: URIs).
    static std::vector<std::string> listGltfExternalUris(const std::filesystem::path& gltfPath);

    /// Files to copy when importing a mesh (mesh + existing external deps).
    static std::vector<std::filesystem::path> collectImportBundle(
        const std::filesystem::path& sourceMeshPath);
};

}  // namespace Caffeine::Assets

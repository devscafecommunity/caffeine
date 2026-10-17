#pragma once

#include "assets/MeshTypes.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Caffeine::Assets {

/// Writes one `.mat` per glTF/OBJ surface next to `meshFile`.
/// Existing files are kept unless `overwrite` is set. Returned paths are project-relative
/// when `projectRoot` is set.
std::vector<std::string> exportMeshMaterials(const Mesh3D& mesh,
                                             const std::filesystem::path& meshFile,
                                             const std::filesystem::path& projectRoot,
                                             bool overwrite = false);

}  // namespace Caffeine::Assets

#pragma once

#include "assets/MaterialTypes.hpp"

#include <filesystem>

namespace Caffeine::Assets {

bool loadMaterialFile(const std::filesystem::path& path, MaterialSurface& out);
bool saveMaterialFile(const std::filesystem::path& path, const MaterialSurface& material);
/// Clamps every field to the range the shader expects.
void sanitizeMaterialSurface(MaterialSurface& surface);

}  // namespace Caffeine::Assets

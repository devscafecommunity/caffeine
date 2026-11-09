#pragma once

#include "editor/ShaderGraph.hpp"
#include "assets/MeshTypes.hpp"
#include <filesystem>
#include <string>

namespace Caffeine::Editor {

struct MaterialDocument {
    std::string name;
    Assets::Material3D properties;
};

class MaterialSerializer {
public:
    static bool save(const std::filesystem::path& path, const MaterialDocument& doc,
                     const ShaderGraph& graph);
    static bool load(const std::filesystem::path& path, MaterialDocument& out, ShaderGraph& graph);
};

}  // namespace Caffeine::Editor

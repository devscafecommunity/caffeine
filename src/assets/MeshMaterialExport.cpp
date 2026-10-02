#include "assets/MeshMaterialExport.hpp"

#include "assets/MaterialFile.hpp"

#include <cctype>

namespace Caffeine::Assets {
namespace {

std::string sanitizeStem(std::string name) {
    if (name.empty()) name = "Material";
    for (char& c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '_' || c == '-') continue;
        c = '_';
    }
    return name;
}

std::string relativeToProject(const std::filesystem::path& projectRoot,
                              const std::filesystem::path& absolute) {
    if (projectRoot.empty()) return absolute.generic_string();
    std::error_code ec;
    const auto rel = std::filesystem::relative(absolute, projectRoot, ec);
    if (ec || rel.empty()) return absolute.generic_string();
    return rel.generic_string();
}

std::string relativeTexture(const std::filesystem::path& projectRoot, const std::string& path) {
    if (path.empty()) return {};
    std::filesystem::path file(path);
    if (!file.is_absolute() || projectRoot.empty()) return file.generic_string();
    return relativeToProject(projectRoot, file);
}

}  // namespace

std::vector<std::string> exportMeshMaterials(const Mesh3D& mesh,
                                             const std::filesystem::path& meshFile,
                                             const std::filesystem::path& projectRoot,
                                             bool overwrite) {
    std::vector<std::string> written;
    if (mesh.materials.empty() || meshFile.empty()) return written;

    const std::filesystem::path dir = meshFile.parent_path().empty()
                                          ? std::filesystem::path(".")
                                          : meshFile.parent_path();
    const std::string stem = sanitizeStem(meshFile.stem().string());

    for (size_t i = 0; i < mesh.materials.size(); ++i) {
        const MeshSurfaceMaterial& src = mesh.materials[i];
        MaterialSurface surface;
        surface.valid = true;
        surface.name = mesh.materials.size() == 1 ? stem : stem + "_" + std::to_string(i);
        surface.albedo = Vec4(src.albedoColor.r, src.albedoColor.g, src.albedoColor.b, src.albedoColor.a);
        surface.metallic = src.metallic;
        surface.roughness = src.roughness;
        surface.albedoMap = relativeTexture(projectRoot, src.albedoPath);

        const std::string fileStem =
            mesh.materials.size() == 1 ? stem : stem + "_" + std::to_string(i);
        const std::filesystem::path out = dir / (fileStem + ".mat");
        std::error_code ec;
        if (overwrite || !std::filesystem::exists(out, ec)) {
            if (!saveMaterialFile(out, surface)) continue;
        }
        written.push_back(relativeToProject(projectRoot, out));
    }
    return written;
}

}  // namespace Caffeine::Assets

#include "assets/MaterialFile.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Caffeine::Assets {
namespace {

std::string restOfLine(std::istringstream& ss) {
    std::string value;
    std::getline(ss, value);
    if (!value.empty() && value[0] == ' ') value.erase(0, 1);
    return value;
}

f32 clamp01(f32 v) { return std::clamp(v, 0.0f, 1.0f); }

const char* alphaModeName(MaterialAlphaMode mode) {
    switch (mode) {
        case MaterialAlphaMode::Cutout: return "cutout";
        case MaterialAlphaMode::Blend:  return "blend";
        case MaterialAlphaMode::Opaque:
        default:                        return "opaque";
    }
}

MaterialAlphaMode parseAlphaMode(const std::string& text) {
    if (text == "cutout") return MaterialAlphaMode::Cutout;
    if (text == "blend") return MaterialAlphaMode::Blend;
    return MaterialAlphaMode::Opaque;
}

}  // namespace

bool loadMaterialFile(const std::filesystem::path& path, MaterialSurface& out) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header)) return false;
    if (header != "CAFMAT3" && header != "CAFMAT2" && header != "CAFMAT1") return false;

    MaterialSurface surface;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "name") {
            surface.name = restOfLine(ss);
        } else if (tag == "albedo") {
            ss >> surface.albedo.x >> surface.albedo.y >> surface.albedo.z >> surface.albedo.w;
        } else if (tag == "roughness") {
            ss >> surface.roughness;
        } else if (tag == "metallic") {
            ss >> surface.metallic;
        } else if (tag == "reflectance") {
            ss >> surface.reflectance;
        } else if (tag == "emission") {
            ss >> surface.emission.x >> surface.emission.y >> surface.emission.z;
        } else if (tag == "emission_strength") {
            ss >> surface.emissionStrength;
        } else if (tag == "albedo_map") {
            surface.albedoMap = restOfLine(ss);
        } else if (tag == "normal_map") {
            surface.normalMap = restOfLine(ss);
        } else if (tag == "orm_map") {
            surface.ormMap = restOfLine(ss);
        } else if (tag == "emission_map") {
            surface.emissionMap = restOfLine(ss);
        } else if (tag == "uv_tiling") {
            ss >> surface.uvTiling.x >> surface.uvTiling.y;
        } else if (tag == "uv_offset") {
            ss >> surface.uvOffset.x >> surface.uvOffset.y;
        } else if (tag == "normal_strength") {
            ss >> surface.normalStrength;
        } else if (tag == "ao_strength") {
            ss >> surface.aoStrength;
        } else if (tag == "alpha_mode") {
            std::string mode;
            ss >> mode;
            surface.alphaMode = parseAlphaMode(mode);
        } else if (tag == "alpha_cutoff") {
            ss >> surface.alphaCutoff;
        } else if (tag == "transmission") {
            ss >> surface.transmission;
        } else if (tag == "ior") {
            ss >> surface.ior;
        } else if (tag == "clearcoat") {
            ss >> surface.clearcoat;
        } else if (tag == "clearcoat_roughness") {
            ss >> surface.clearcoatRoughness;
        } else if (tag == "sheen") {
            ss >> surface.sheenColor.x >> surface.sheenColor.y >> surface.sheenColor.z;
        } else if (tag == "sheen_roughness") {
            ss >> surface.sheenRoughness;
        } else if (tag == "iridescence") {
            ss >> surface.iridescence;
        } else if (tag == "iridescence_thickness") {
            ss >> surface.iridescenceThickness;
        } else if (tag == "iridescence_ior") {
            ss >> surface.iridescenceIor;
        }
    }

    sanitizeMaterialSurface(surface);
    surface.valid = true;
    out = std::move(surface);
    return true;
}

void sanitizeMaterialSurface(MaterialSurface& surface) {
    surface.albedo.w = clamp01(surface.albedo.w);
    surface.roughness = clamp01(surface.roughness);
    surface.metallic = clamp01(surface.metallic);
    surface.reflectance = clamp01(surface.reflectance);
    surface.emissionStrength = std::max(surface.emissionStrength, 0.0f);
    surface.normalStrength = std::clamp(surface.normalStrength, 0.0f, 4.0f);
    surface.aoStrength = clamp01(surface.aoStrength);
    surface.alphaCutoff = clamp01(surface.alphaCutoff);
    surface.transmission = clamp01(surface.transmission);
    surface.ior = std::clamp(surface.ior, 1.0f, 3.0f);
    surface.clearcoat = clamp01(surface.clearcoat);
    surface.clearcoatRoughness = clamp01(surface.clearcoatRoughness);
    surface.sheenColor = Vec3(clamp01(surface.sheenColor.x), clamp01(surface.sheenColor.y),
                              clamp01(surface.sheenColor.z));
    surface.sheenRoughness = clamp01(surface.sheenRoughness);
    surface.iridescence = clamp01(surface.iridescence);
    surface.iridescenceThickness = std::clamp(surface.iridescenceThickness, 50.0f, 1500.0f);
    surface.iridescenceIor = std::clamp(surface.iridescenceIor, 1.0f, 2.5f);
}

bool saveMaterialFile(const std::filesystem::path& path, const MaterialSurface& material) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "CAFMAT3\n";
    out << "name " << material.name << '\n';
    out << "albedo " << material.albedo.x << ' ' << material.albedo.y << ' ' << material.albedo.z << ' '
        << material.albedo.w << '\n';
    out << "metallic " << material.metallic << '\n';
    out << "roughness " << material.roughness << '\n';
    out << "reflectance " << material.reflectance << '\n';
    out << "emission " << material.emission.x << ' ' << material.emission.y << ' ' << material.emission.z << '\n';
    out << "emission_strength " << material.emissionStrength << '\n';
    out << "albedo_map " << material.albedoMap << '\n';
    out << "normal_map " << material.normalMap << '\n';
    out << "orm_map " << material.ormMap << '\n';
    out << "emission_map " << material.emissionMap << '\n';
    out << "uv_tiling " << material.uvTiling.x << ' ' << material.uvTiling.y << '\n';
    out << "uv_offset " << material.uvOffset.x << ' ' << material.uvOffset.y << '\n';
    out << "normal_strength " << material.normalStrength << '\n';
    out << "ao_strength " << material.aoStrength << '\n';
    out << "alpha_mode " << alphaModeName(material.alphaMode) << '\n';
    out << "alpha_cutoff " << material.alphaCutoff << '\n';
    out << "transmission " << material.transmission << '\n';
    out << "ior " << material.ior << '\n';
    out << "clearcoat " << material.clearcoat << '\n';
    out << "clearcoat_roughness " << material.clearcoatRoughness << '\n';
    out << "sheen " << material.sheenColor.x << ' ' << material.sheenColor.y << ' '
        << material.sheenColor.z << '\n';
    out << "sheen_roughness " << material.sheenRoughness << '\n';
    out << "iridescence " << material.iridescence << '\n';
    out << "iridescence_thickness " << material.iridescenceThickness << '\n';
    out << "iridescence_ior " << material.iridescenceIor << '\n';
    return static_cast<bool>(out);
}

}  // namespace Caffeine::Assets

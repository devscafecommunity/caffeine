#pragma once

#include "assets/MaterialTypes.hpp"

#include <string_view>
#include <vector>

namespace Caffeine::Assets {

struct MaterialPreset {
    std::string_view category;
    std::string_view name;
    MaterialSurface surface;
};

/// Starting points for common real-world surfaces. Colours are sRGB as shown in the editor;
/// metal colours are measured F0 values. Maps are left empty so presets keep existing textures
/// when applied with `applyMaterialPreset`.
inline const std::vector<MaterialPreset>& materialPresets() {
    static const std::vector<MaterialPreset> presets = [] {
        std::vector<MaterialPreset> list;
        auto add = [&list](std::string_view category, std::string_view name, auto&& configure) {
            MaterialSurface s;
            s.name = std::string(name);
            configure(s);
            s.valid = true;
            list.push_back({category, name, s});
        };
        add("Metal", "Mirror", [](MaterialSurface& s) {
            s.albedo = Vec4(0.98f, 0.98f, 0.98f, 1.0f);
            s.metallic = 1.0f;
            s.roughness = 0.0f;
        });
        add("Metal", "Chrome", [](MaterialSurface& s) {
            s.albedo = Vec4(0.96f, 0.96f, 0.97f, 1.0f);
            s.metallic = 1.0f;
            s.roughness = 0.04f;
        });
        add("Metal", "Brushed Steel", [](MaterialSurface& s) {
            s.albedo = Vec4(0.86f, 0.86f, 0.87f, 1.0f);
            s.metallic = 1.0f;
            s.roughness = 0.38f;
        });
        add("Metal", "Gold", [](MaterialSurface& s) {
            s.albedo = Vec4(1.0f, 0.89f, 0.62f, 1.0f);
            s.metallic = 1.0f;
            s.roughness = 0.12f;
        });
        add("Metal", "Copper", [](MaterialSurface& s) {
            s.albedo = Vec4(0.98f, 0.82f, 0.76f, 1.0f);
            s.metallic = 1.0f;
            s.roughness = 0.18f;
        });
        add("Metal", "Aluminium", [](MaterialSurface& s) {
            s.albedo = Vec4(0.96f, 0.96f, 0.97f, 1.0f);
            s.metallic = 1.0f;
            s.roughness = 0.28f;
        });
        add("Metal", "Rust", [](MaterialSurface& s) {
            s.albedo = Vec4(0.47f, 0.22f, 0.11f, 1.0f);
            s.metallic = 0.25f;
            s.roughness = 0.88f;
        });
        add("Metal", "Holographic Foil", [](MaterialSurface& s) {
            s.albedo = Vec4(0.92f, 0.92f, 0.94f, 1.0f);
            s.metallic = 1.0f;
            s.roughness = 0.1f;
            s.iridescence = 1.0f;
            s.iridescenceThickness = 560.0f;
            s.iridescenceIor = 1.6f;
        });
        add("Glass", "Clear Glass", [](MaterialSurface& s) {
            s.albedo = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
            s.roughness = 0.0f;
            s.transmission = 1.0f;
            s.ior = 1.5f;
        });
        add("Glass", "Frosted Glass", [](MaterialSurface& s) {
            s.albedo = Vec4(0.95f, 0.97f, 1.0f, 1.0f);
            s.roughness = 0.35f;
            s.transmission = 1.0f;
            s.ior = 1.5f;
        });
        add("Glass", "Ice", [](MaterialSurface& s) {
            s.albedo = Vec4(0.82f, 0.93f, 1.0f, 1.0f);
            s.roughness = 0.1f;
            s.transmission = 0.85f;
            s.ior = 1.31f;
            s.clearcoat = 0.5f;
            s.clearcoatRoughness = 0.02f;
        });
        add("Glass", "Water", [](MaterialSurface& s) {
            s.albedo = Vec4(0.8f, 0.93f, 0.95f, 1.0f);
            s.roughness = 0.02f;
            s.transmission = 1.0f;
            s.ior = 1.33f;
        });
        add("Glass", "Soap Bubble", [](MaterialSurface& s) {
            s.albedo = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
            s.roughness = 0.0f;
            s.transmission = 1.0f;
            s.ior = 1.05f;
            s.iridescence = 1.0f;
            s.iridescenceThickness = 380.0f;
            s.iridescenceIor = 1.33f;
        });
        add("Dielectric", "Glossy Plastic", [](MaterialSurface& s) {
            s.albedo = Vec4(0.8f, 0.12f, 0.1f, 1.0f);
            s.roughness = 0.22f;
            s.reflectance = 0.045f;
        });
        add("Dielectric", "Matte Plastic", [](MaterialSurface& s) {
            s.albedo = Vec4(0.75f, 0.75f, 0.72f, 1.0f);
            s.roughness = 0.7f;
        });
        add("Dielectric", "Rubber", [](MaterialSurface& s) {
            s.albedo = Vec4(0.08f, 0.08f, 0.08f, 1.0f);
            s.roughness = 0.95f;
            s.reflectance = 0.03f;
        });
        add("Dielectric", "Car Paint", [](MaterialSurface& s) {
            s.albedo = Vec4(0.62f, 0.03f, 0.04f, 1.0f);
            s.metallic = 0.45f;
            s.roughness = 0.4f;
            s.clearcoat = 1.0f;
            s.clearcoatRoughness = 0.03f;
        });
        add("Dielectric", "Ceramic", [](MaterialSurface& s) {
            s.albedo = Vec4(0.92f, 0.92f, 0.9f, 1.0f);
            s.roughness = 0.3f;
            s.clearcoat = 0.8f;
            s.clearcoatRoughness = 0.05f;
        });
        add("Fabric", "Velvet", [](MaterialSurface& s) {
            s.albedo = Vec4(0.36f, 0.03f, 0.12f, 1.0f);
            s.roughness = 0.9f;
            s.sheenColor = Vec3(0.9f, 0.55f, 0.65f);
            s.sheenRoughness = 0.35f;
        });
        add("Fabric", "Cotton", [](MaterialSurface& s) {
            s.albedo = Vec4(0.7f, 0.72f, 0.78f, 1.0f);
            s.roughness = 0.95f;
            s.sheenColor = Vec3(0.4f, 0.4f, 0.45f);
            s.sheenRoughness = 0.6f;
        });
        add("Emissive", "Neon", [](MaterialSurface& s) {
            s.albedo = Vec4(0.05f, 0.05f, 0.05f, 1.0f);
            s.roughness = 0.3f;
            s.emission = Vec3(1.0f, 0.2f, 0.8f);
            s.emissionStrength = 6.0f;
        });
        return list;
    }();
    return presets;
}

/// Copies the look of `preset` onto `target`, keeping its name, texture maps and UV layout.
inline void applyMaterialPreset(const MaterialSurface& preset, MaterialSurface& target) {
    MaterialSurface result = preset;
    result.name = target.name;
    result.albedoMap = target.albedoMap;
    result.normalMap = target.normalMap;
    result.ormMap = target.ormMap;
    result.emissionMap = target.emissionMap;
    result.uvTiling = target.uvTiling;
    result.uvOffset = target.uvOffset;
    result.normalStrength = target.normalStrength;
    result.aoStrength = target.aoStrength;
    result.valid = true;
    target = std::move(result);
}

}  // namespace Caffeine::Assets

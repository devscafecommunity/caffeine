#pragma once

#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"

#include <string>

namespace Caffeine::Assets {

/// Opaque writes depth; Cutout discards below `alphaCutoff`; Blend is sorted and drawn after
/// the opaque pass.
enum class MaterialAlphaMode : u8 { Opaque = 0, Cutout = 1, Blend = 2 };

/// Surface the forward shader consumes. A `.mat` file is this struct, not a generated shader.
struct MaterialSurface {
    std::string name;
    /// Base colour in sRGB as picked in the editor (the renderer linearises it); `w` is opacity.
    Vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
    f32  metallic = 0.0f;
    f32  roughness = 0.5f;
    /// Dielectric Fresnel reflectance (F0). Metals replace this with albedo. 0.04 matches plastic.
    f32  reflectance = 0.04f;
    Vec3 emission{0.0f, 0.0f, 0.0f};
    f32  emissionStrength = 0.0f;
    std::string albedoMap;
    std::string normalMap;
    std::string ormMap;
    std::string emissionMap;

    Vec2 uvTiling{1.0f, 1.0f};
    Vec2 uvOffset{0.0f, 0.0f};
    /// 1 = UVs follow the object's scale: a mesh scaled 2x keeps the texture's density (it
    /// tiles twice) instead of stretching it. The factor is the world length of the mesh's
    /// tangent / bitangent per object-space unit, so non-uniform scale is handled per face.
    /// 0 = the authored 1:1 UVs.
    u8   uvObjectScale = 1;
    f32  normalStrength = 1.0f;
    /// The normal map stores DirectX-convention green (Y down) data.
    bool normalDirectX = false;
    f32  aoStrength = 1.0f;

    /// Parallax occlusion mapping depth, in UV units of the (tiled) texture. 0 turns it off.
    /// The height is read from the ALPHA channel of the normal map (1 = surface, 0 = deepest),
    /// so no extra sampler is needed: bake or author normal+height in one texture.
    f32  heightScale = 0.0f;
    /// Maximum parallax ray-march steps (the shader uses fewer when looking straight on).
    f32  parallaxSteps = 32.0f;
    /// 0 = parallax occlusion (texture space), 1 = geometric: the mesh is subdivided and its
    /// vertices are displaced along their normals by the same height, so the silhouette changes
    /// and the relief casts real shadows.
    u8   displacementMode = 0;
    /// Geometric depth in metres: the surface level stays put, valleys sink by up to this much.
    f32  displacementScale = 0.05f;
    /// Subdivision levels applied to the mesh in geometric mode (each level doubles the
    /// segments per edge: a quad becomes 2^n x 2^n quads).
    u8   meshSubdivisions = 7;

    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    f32  alphaCutoff = 0.5f;

    /// Light passing through the surface (glass, water, ice). Refracts the opaque scene.
    f32  transmission = 0.0f;
    f32  ior = 1.5f;

    /// Second specular layer on top of the base (car paint, varnish, lacquer).
    f32  clearcoat = 0.0f;
    f32  clearcoatRoughness = 0.03f;

    /// Soft retro-reflective rim for cloth and velvet.
    Vec3 sheenColor{0.0f, 0.0f, 0.0f};
    f32  sheenRoughness = 0.5f;

    /// Thin-film interference (soap bubbles, oil, holographic foil). Thickness in nanometres.
    f32  iridescence = 0.0f;
    f32  iridescenceThickness = 400.0f;
    f32  iridescenceIor = 1.3f;

    /// How strongly this surface asks for reflections. 0 leaves the base roughness in charge.
    f32  reflection = 0.0f;
    /// 0 inherit, 1 performance, 2 both, 3 quality. Kept so older .mat files still select a module.
    u8   reflectionBudget = 0;
    u8   reflectionPerformance = 0;
    u8   reflectionQuality = 0;
    u8   reflectionPlanar = 0;
    f32  ssrResolution = 0.5f;
    f32  ssrMaxSteps = 32.0f;
    f32  ssrTemporalFrames = 4.0f;
    f32  ssrDistance = 15.0f;
    f32  ssrSamples = 64.0f;
    f32  ssrDenoise = 0.7f;
    f32  ssrProbeBlend = 8.0f;
    f32  ssrBounces = 1.0f;

    bool valid = false;
};

}  // namespace Caffeine::Assets

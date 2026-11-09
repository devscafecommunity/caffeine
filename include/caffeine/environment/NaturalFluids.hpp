#pragma once

#include "core/Types.hpp"

namespace Caffeine::ECS {

/// How the Natural Fluids entity is rendered.
enum class NaturalFluidMode : u8 {
    /// Level 1: perfectly flat water.
    FlatWater = 0,
    /// Level 2: analytic wind waves + conservative shallow-water physics.
    WindWater = 1,
    /// Level 3: broad Gerstner spectrum, choppiness, foam and depth attenuation.
    RealisticWater = 2,
    /// Level 4: a real Eulerian fluid volume (water, fire, fog, smoke).
    FluidVolume = 3,
};

enum class NaturalFluidPreset : u8 {
    Water = 0,
    Fire = 1,
    Fog = 2,
    Smoke = 3,
};

/// Authoring data for an animated water surface or a real fluid volume.
///
/// The component is a plain POD so the scene serializer can store it directly.
/// Simulation state (height fields, velocity grids, meshes) lives in
/// Scene::NaturalFluidSystem and is rebuilt from these parameters.
struct NaturalFluidComponent {
    u8 enabled = 1;
    u8 mode = static_cast<u8>(NaturalFluidMode::RealisticWater);
    u8 preset = static_cast<u8>(NaturalFluidPreset::Water);
    /// Adds the shallow-water solver on top of the analytic waves.
    u8 shallowPhysics = 0;
    /// Runs a 2D fluid slice in addition to the surface (preview / authoring).
    u8 simulateFluid2D = 0;
    /// Advances the simulation in the editor viewport while not playing.
    u8 previewInEditor = 1;
    /// Draws the 3D fluid volume as density voxels until a raymarch pass lands.
    u8 renderVolume = 1;
    u8 castShadows = 0;
    u8 receiveShadows = 1;
    u8 _pad0[2] = {};

    u32 seed = 1337u;
    u32 surfaceResolution = 96u;
    u32 simulationResolution = 64u;
    u32 fluidResolution = 28u;

    // Surface / water shape.
    f32 sizeX = 96.0f;
    f32 sizeY = 16.0f;
    f32 sizeZ = 96.0f;
    f32 waterLevel = 0.0f;
    f32 depth = 200.0f;
    f32 shallowDepth = 2.5f;

    // Waves.
    f32 windSpeed = 12.0f;
    f32 windDirectionDeg = 35.0f;
    f32 amplitudeScale = 1.0f;
    f32 choppiness = 0.72f;
    f32 wavelengthScale = 1.0f;
    f32 foamThreshold = 0.72f;
    f32 timeScale = 1.0f;

    // Shading.
    f32 albedoR = 0.03f;
    f32 albedoG = 0.16f;
    f32 albedoB = 0.30f;
    f32 albedoA = 0.78f;
    f32 roughness = 0.05f;
    f32 metallic = 0.0f;

    // Fluid volume / 2D slice.
    f32 fluidWindSpeed = 0.0f;
    f32 fluidWindDirectionDeg = 0.0f;
    f32 fluidSourceRate = 0.45f;
    f32 fluidSourceRadius = 2.0f;
    f32 fluidSourceTemperature = 1.4f;
    f32 fluidEmission = 0.0f;
};

}  // namespace Caffeine::ECS

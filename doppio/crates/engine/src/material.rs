use std::ffi::{c_char, CStr};

pub const MATERIAL_NAME_SIZE: usize = 128;
pub const MATERIAL_PATH_SIZE: usize = 512;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MaterialAlphaMode {
    Opaque,
    Cutout,
    Blend,
}

impl MaterialAlphaMode {
    pub fn label(self) -> &'static str {
        match self {
            MaterialAlphaMode::Opaque => "Opaque",
            MaterialAlphaMode::Cutout => "Cutout",
            MaterialAlphaMode::Blend => "Blend",
        }
    }

    pub(crate) fn from_raw(value: i32) -> Self {
        match value {
            1 => MaterialAlphaMode::Cutout,
            2 => MaterialAlphaMode::Blend,
            _ => MaterialAlphaMode::Opaque,
        }
    }

    pub(crate) fn raw(self) -> i32 {
        match self {
            MaterialAlphaMode::Opaque => 0,
            MaterialAlphaMode::Cutout => 1,
            MaterialAlphaMode::Blend => 2,
        }
    }
}

/// Editable PBR material, mirroring the engine's CAFMAT3 surface.
#[derive(Debug, Clone, PartialEq)]
pub struct Material {
    pub name: String,
    pub albedo: [f32; 4],
    pub metallic: f32,
    pub roughness: f32,
    pub reflectance: f32,
    pub emission: [f32; 3],
    pub emission_strength: f32,
    pub albedo_map: String,
    pub normal_map: String,
    pub orm_map: String,
    pub emission_map: String,
    pub uv_tiling: [f32; 2],
    pub uv_offset: [f32; 2],
    pub normal_strength: f32,
    pub normal_directx: bool,
    pub ao_strength: f32,
    pub alpha_mode: MaterialAlphaMode,
    pub alpha_cutoff: f32,
    pub transmission: f32,
    pub ior: f32,
    pub clearcoat: f32,
    pub clearcoat_roughness: f32,
    pub sheen_color: [f32; 3],
    pub sheen_roughness: f32,
    pub wind_strength: f32,
    pub wind_speed: f32,
    pub wind_flutter: f32,
    pub translucency: f32,
    /// Run-time modifiers: albedo tint, wetness, snow cover, age (0..1).
    pub mod_tint: [f32; 3],
    pub mod_wetness: f32,
    pub mod_snow: f32,
    pub mod_age: f32,
    pub iridescence: f32,
    pub iridescence_thickness: f32,
    pub iridescence_ior: f32,
    pub reflection: f32,
    pub reflection_budget: i32,
    pub reflection_performance: bool,
    pub reflection_quality: bool,
    pub reflection_planar: bool,
    pub ssr_resolution: f32,
    pub ssr_max_steps: f32,
    pub ssr_temporal: f32,
    pub ssr_distance: f32,
    pub ssr_samples: f32,
    pub ssr_denoise: f32,
    pub ssr_probe_blend: f32,
    pub ssr_bounces: f32,
    /// Parallax depth in UV units (0 = off); the height is the normal map's alpha channel.
    pub height_scale: f32,
    /// Maximum parallax ray-march steps.
    pub parallax_steps: f32,
    /// 0 = parallax occlusion, 1 = geometric (subdivided mesh + vertex displacement).
    pub displacement_mode: i32,
    /// Geometric depth in metres.
    pub displacement_scale: f32,
    /// Subdivision levels (1..9) for geometric mode.
    pub mesh_subdivisions: i32,
    /// UVs follow the object's scale (texture density is kept when the mesh is scaled).
    pub uv_object_scale: bool,
}

impl Default for Material {
    fn default() -> Self {
        Self {
            name: "New Material".to_owned(),
            albedo: [1.0, 1.0, 1.0, 1.0],
            metallic: 0.0,
            roughness: 0.5,
            reflectance: 0.04,
            emission: [0.0; 3],
            emission_strength: 0.0,
            albedo_map: String::new(),
            normal_map: String::new(),
            orm_map: String::new(),
            emission_map: String::new(),
            uv_tiling: [1.0, 1.0],
            uv_offset: [0.0, 0.0],
            normal_strength: 1.0,
            normal_directx: false,
            ao_strength: 1.0,
            alpha_mode: MaterialAlphaMode::Opaque,
            alpha_cutoff: 0.5,
            transmission: 0.0,
            ior: 1.5,
            clearcoat: 0.0,
            clearcoat_roughness: 0.03,
            sheen_color: [0.0; 3],
            sheen_roughness: 0.5,
            wind_strength: 0.0,
            wind_speed: 1.0,
            wind_flutter: 0.0,
            translucency: 0.0,
            mod_tint: [1.0; 3],
            mod_wetness: 0.0,
            mod_snow: 0.0,
            mod_age: 0.0,
            iridescence: 0.0,
            iridescence_thickness: 400.0,
            iridescence_ior: 1.3,
            reflection: 0.0,
            reflection_budget: 0,
            reflection_performance: false,
            reflection_quality: false,
            reflection_planar: false,
            ssr_resolution: 0.5,
            ssr_max_steps: 32.0,
            ssr_temporal: 4.0,
            ssr_distance: 15.0,
            ssr_samples: 64.0,
            ssr_denoise: 0.7,
            ssr_probe_blend: 8.0,
            ssr_bounces: 1.0,
            height_scale: 0.0,
            parallax_steps: 32.0,
            displacement_mode: 0,
            displacement_scale: 0.05,
            mesh_subdivisions: 7,
            uv_object_scale: true,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct MaterialPreset {
    pub category: String,
    pub name: String,
    pub material: Material,
}

#[repr(C)]
pub(crate) struct RawMaterial {
    pub name: [c_char; MATERIAL_NAME_SIZE],
    pub albedo: [f32; 4],
    pub metallic: f32,
    pub roughness: f32,
    pub reflectance: f32,
    pub emission: [f32; 3],
    pub emission_strength: f32,
    pub albedo_map: [c_char; MATERIAL_PATH_SIZE],
    pub normal_map: [c_char; MATERIAL_PATH_SIZE],
    pub orm_map: [c_char; MATERIAL_PATH_SIZE],
    pub emission_map: [c_char; MATERIAL_PATH_SIZE],
    pub uv_tiling: [f32; 2],
    pub uv_offset: [f32; 2],
    pub normal_strength: f32,
    pub normal_directx: i32,
    pub ao_strength: f32,
    pub alpha_mode: i32,
    pub alpha_cutoff: f32,
    pub transmission: f32,
    pub ior: f32,
    pub clearcoat: f32,
    pub clearcoat_roughness: f32,
    pub sheen_color: [f32; 3],
    pub sheen_roughness: f32,
    pub iridescence: f32,
    pub iridescence_thickness: f32,
    pub iridescence_ior: f32,
    pub reflection: f32,
    pub reflection_budget: i32,
    pub reflection_performance: i32,
    pub reflection_quality: i32,
    pub reflection_planar: i32,
    pub ssr_resolution: f32,
    pub ssr_max_steps: f32,
    pub ssr_temporal: f32,
    pub ssr_distance: f32,
    pub ssr_samples: f32,
    pub ssr_denoise: f32,
    pub ssr_probe_blend: f32,
    pub ssr_bounces: f32,
    pub height_scale: f32,
    pub parallax_steps: f32,
    pub displacement_mode: i32,
    pub displacement_scale: f32,
    pub mesh_subdivisions: i32,
    pub uv_object_scale: i32,
    pub wind_strength: f32,
    pub wind_speed: f32,
    pub wind_flutter: f32,
    pub translucency: f32,
    pub mod_tint: [f32; 3],
    pub mod_wetness: f32,
    pub mod_snow: f32,
    pub mod_age: f32,
}

impl Default for RawMaterial {
    fn default() -> Self {
        Self {
            name: [0; MATERIAL_NAME_SIZE],
            albedo: [0.0; 4],
            metallic: 0.0,
            roughness: 0.0,
            reflectance: 0.0,
            emission: [0.0; 3],
            emission_strength: 0.0,
            albedo_map: [0; MATERIAL_PATH_SIZE],
            normal_map: [0; MATERIAL_PATH_SIZE],
            orm_map: [0; MATERIAL_PATH_SIZE],
            emission_map: [0; MATERIAL_PATH_SIZE],
            uv_tiling: [0.0; 2],
            uv_offset: [0.0; 2],
            normal_strength: 0.0,
            normal_directx: 0,
            ao_strength: 0.0,
            alpha_mode: 0,
            alpha_cutoff: 0.0,
            transmission: 0.0,
            ior: 0.0,
            clearcoat: 0.0,
            clearcoat_roughness: 0.0,
            sheen_color: [0.0; 3],
            sheen_roughness: 0.0,
            wind_strength: 0.0,
            wind_speed: 0.0,
            wind_flutter: 0.0,
            translucency: 0.0,
            mod_tint: [0.0; 3],
            mod_wetness: 0.0,
            mod_snow: 0.0,
            mod_age: 0.0,
            iridescence: 0.0,
            iridescence_thickness: 0.0,
            iridescence_ior: 0.0,
            reflection: 0.0,
            reflection_budget: 0,
            reflection_performance: 0,
            reflection_quality: 0,
            reflection_planar: 0,
            ssr_resolution: 0.0,
            ssr_max_steps: 0.0,
            ssr_temporal: 0.0,
            ssr_distance: 0.0,
            ssr_samples: 0.0,
            ssr_denoise: 0.0,
            ssr_probe_blend: 0.0,
            ssr_bounces: 0.0,
            height_scale: 0.0,
            parallax_steps: 0.0,
            displacement_mode: 0,
            displacement_scale: 0.0,
            mesh_subdivisions: 0,
            uv_object_scale: 0,
        }
    }
}

#[repr(C)]
pub(crate) struct RawMaterialPresetInfo {
    pub category: [c_char; 64],
    pub name: [c_char; 64],
}

impl Default for RawMaterialPresetInfo {
    fn default() -> Self {
        Self {
            category: [0; 64],
            name: [0; 64],
        }
    }
}

pub(crate) fn string_from_chars(chars: &[c_char]) -> String {
    if chars.is_empty() {
        return String::new();
    }
    unsafe { CStr::from_ptr(chars.as_ptr()) }
        .to_string_lossy()
        .into_owned()
}

pub(crate) fn write_chars(destination: &mut [c_char], value: &str) {
    if destination.is_empty() {
        return;
    }
    let bytes = value.as_bytes();
    let count = bytes.len().min(destination.len() - 1);
    for (target, byte) in destination.iter_mut().zip(bytes.iter()).take(count) {
        *target = *byte as c_char;
    }
    destination[count] = 0;
}

impl From<&RawMaterial> for Material {
    fn from(raw: &RawMaterial) -> Self {
        Self {
            name: string_from_chars(&raw.name),
            albedo: raw.albedo,
            metallic: raw.metallic,
            roughness: raw.roughness,
            reflectance: raw.reflectance,
            emission: raw.emission,
            emission_strength: raw.emission_strength,
            albedo_map: string_from_chars(&raw.albedo_map),
            normal_map: string_from_chars(&raw.normal_map),
            orm_map: string_from_chars(&raw.orm_map),
            emission_map: string_from_chars(&raw.emission_map),
            uv_tiling: raw.uv_tiling,
            uv_offset: raw.uv_offset,
            normal_strength: raw.normal_strength,
            normal_directx: raw.normal_directx != 0,
            ao_strength: raw.ao_strength,
            alpha_mode: MaterialAlphaMode::from_raw(raw.alpha_mode),
            alpha_cutoff: raw.alpha_cutoff,
            transmission: raw.transmission,
            ior: raw.ior,
            clearcoat: raw.clearcoat,
            clearcoat_roughness: raw.clearcoat_roughness,
            sheen_color: raw.sheen_color,
            sheen_roughness: raw.sheen_roughness,
            wind_strength: raw.wind_strength,
            wind_speed: raw.wind_speed,
            wind_flutter: raw.wind_flutter,
            translucency: raw.translucency,
            mod_tint: raw.mod_tint,
            mod_wetness: raw.mod_wetness,
            mod_snow: raw.mod_snow,
            mod_age: raw.mod_age,
            iridescence: raw.iridescence,
            iridescence_thickness: raw.iridescence_thickness,
            iridescence_ior: raw.iridescence_ior,
            reflection: raw.reflection,
            reflection_budget: raw.reflection_budget,
            reflection_performance: raw.reflection_performance != 0,
            reflection_quality: raw.reflection_quality != 0,
            reflection_planar: raw.reflection_planar != 0,
            ssr_resolution: raw.ssr_resolution,
            ssr_max_steps: raw.ssr_max_steps,
            ssr_temporal: raw.ssr_temporal,
            ssr_distance: raw.ssr_distance,
            ssr_samples: raw.ssr_samples,
            ssr_denoise: raw.ssr_denoise,
            ssr_probe_blend: raw.ssr_probe_blend,
            ssr_bounces: raw.ssr_bounces,
            height_scale: raw.height_scale,
            parallax_steps: raw.parallax_steps,
            displacement_mode: raw.displacement_mode,
            displacement_scale: raw.displacement_scale,
            mesh_subdivisions: raw.mesh_subdivisions,
            uv_object_scale: raw.uv_object_scale != 0,
        }
    }
}

impl From<&Material> for RawMaterial {
    fn from(material: &Material) -> Self {
        let mut raw = RawMaterial::default();
        write_chars(&mut raw.name, &material.name);
        raw.albedo = material.albedo;
        raw.metallic = material.metallic;
        raw.roughness = material.roughness;
        raw.reflectance = material.reflectance;
        raw.emission = material.emission;
        raw.emission_strength = material.emission_strength;
        write_chars(&mut raw.albedo_map, &material.albedo_map);
        write_chars(&mut raw.normal_map, &material.normal_map);
        write_chars(&mut raw.orm_map, &material.orm_map);
        write_chars(&mut raw.emission_map, &material.emission_map);
        raw.uv_tiling = material.uv_tiling;
        raw.uv_offset = material.uv_offset;
        raw.normal_strength = material.normal_strength;
        raw.normal_directx = i32::from(material.normal_directx);
        raw.ao_strength = material.ao_strength;
        raw.alpha_mode = material.alpha_mode.raw();
        raw.alpha_cutoff = material.alpha_cutoff;
        raw.transmission = material.transmission;
        raw.ior = material.ior;
        raw.clearcoat = material.clearcoat;
        raw.clearcoat_roughness = material.clearcoat_roughness;
        raw.sheen_color = material.sheen_color;
        raw.sheen_roughness = material.sheen_roughness;
        raw.wind_strength = material.wind_strength;
        raw.wind_speed = material.wind_speed;
        raw.wind_flutter = material.wind_flutter;
        raw.translucency = material.translucency;
        raw.mod_tint = material.mod_tint;
        raw.mod_wetness = material.mod_wetness;
        raw.mod_snow = material.mod_snow;
        raw.mod_age = material.mod_age;
        raw.iridescence = material.iridescence;
        raw.iridescence_thickness = material.iridescence_thickness;
        raw.iridescence_ior = material.iridescence_ior;
        raw.reflection = material.reflection;
        raw.reflection_budget = material.reflection_budget;
        raw.reflection_performance = i32::from(material.reflection_performance);
        raw.reflection_quality = i32::from(material.reflection_quality);
        raw.reflection_planar = i32::from(material.reflection_planar);
        raw.ssr_resolution = material.ssr_resolution;
        raw.ssr_max_steps = material.ssr_max_steps;
        raw.ssr_temporal = material.ssr_temporal;
        raw.ssr_distance = material.ssr_distance;
        raw.ssr_samples = material.ssr_samples;
        raw.ssr_denoise = material.ssr_denoise;
        raw.ssr_probe_blend = material.ssr_probe_blend;
        raw.ssr_bounces = material.ssr_bounces;
        raw.height_scale = material.height_scale;
        raw.parallax_steps = material.parallax_steps;
        raw.displacement_mode = material.displacement_mode;
        raw.displacement_scale = material.displacement_scale;
        raw.mesh_subdivisions = material.mesh_subdivisions;
        raw.uv_object_scale = i32::from(material.uv_object_scale);
        raw
    }
}

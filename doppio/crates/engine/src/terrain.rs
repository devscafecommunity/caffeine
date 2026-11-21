//! Terrain from generated data (C ABI v8), in plain Rust types.

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Terrain {
    /// Grid vertices along X and Z.
    pub resolution: [u32; 2],
    /// World size in metres along X and Z.
    pub size: [f32; 2],
    /// Height of a normalised 1.0, in metres.
    pub max_height: f32,
    pub cast_shadows: bool,
}

impl Default for Terrain {
    fn default() -> Self {
        Self {
            resolution: [257, 257],
            size: [512.0, 512.0],
            max_height: 120.0,
            cast_shadows: true,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub(crate) struct RawTerrain {
    resolution_x: u32,
    resolution_z: u32,
    size_x: f32,
    size_z: f32,
    max_height: f32,
    cast_shadows: i32,
}

impl Terrain {
    pub(crate) fn to_raw(self) -> RawTerrain {
        RawTerrain {
            resolution_x: self.resolution[0],
            resolution_z: self.resolution[1],
            size_x: self.size[0],
            size_z: self.size[1],
            max_height: self.max_height,
            cast_shadows: i32::from(self.cast_shadows),
        }
    }

    pub(crate) fn from_raw(raw: &RawTerrain) -> Self {
        Terrain {
            resolution: [raw.resolution_x, raw.resolution_z],
            size: [raw.size_x, raw.size_z],
            max_height: raw.max_height,
            cast_shadows: raw.cast_shadows != 0,
        }
    }
}

/// One terrain mask: a named 0..1 field over the terrain (C ABI v15). The built-in masks (`height`, `slope`,
/// `curvature`) are derived from the heightfield and have no grid; stored masks have their own.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TerrainMaskInfo {
    pub name: String,
    /// Grid of a stored mask (0 x 0 for a built-in).
    pub resolution: [u32; 2],
    pub builtin: bool,
}

/// One kind of thing to scatter on a terrain (C ABI v16): spacing, exclusion radius, density, scale range and how it
/// sits on the ground.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ScatterClass {
    pub id: u32,
    /// Higher wins a conflict (rocks, then trees, then grass).
    pub priority: i32,
    /// Candidate grid spacing in metres.
    pub cell_size: f32,
    /// Exclusion radius in metres: two points closer than the sum of their radii conflict.
    pub radius: f32,
    /// 0..1 chance, multiplied by the biome table's density.
    pub density: f32,
    pub min_scale: f32,
    pub max_scale: f32,
    /// 0 keeps up, 1 follows the terrain normal.
    pub tilt_to_normal: f32,
    /// Metres the base sinks below the ground, per unit of scale.
    pub sink: f32,
    /// The tint is 1 +- this, per channel.
    pub tint_jitter: f32,
    pub variants: u32,
}

impl Default for ScatterClass {
    fn default() -> Self {
        Self {
            id: 0,
            priority: 0,
            cell_size: 8.0,
            radius: 2.0,
            density: 1.0,
            min_scale: 0.8,
            max_scale: 1.2,
            tilt_to_normal: 0.15,
            sink: 0.0,
            tint_jitter: 0.06,
            variants: 1,
        }
    }
}

/// One placed instance, in scene space.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ScatterInstance {
    pub class_id: u32,
    pub variant: u32,
    pub position: [f32; 3],
    pub up: [f32; 3],
    /// The orientation (the yaw included) as Euler degrees, for `set_rotation`.
    pub rotation_degrees: [f32; 3],
    pub scale: f32,
    pub tint: [f32; 3],
    pub seed: u64,
}

/// What a scatter produced: the instances that fit the buffer and how many the region holds in all.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct ScatterResult {
    pub instances: Vec<ScatterInstance>,
    pub total: u32,
}

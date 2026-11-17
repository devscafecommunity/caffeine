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

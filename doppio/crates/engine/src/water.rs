//! Water surfaces (C ABI v7): authored Gerstner waves plus shading, in plain Rust types.

pub const MAX_WATER_WAVES: usize = 16;

#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct WaterWave {
    /// Direction on the water plane (normalised by the engine).
    pub direction: [f32; 2],
    pub wavelength: f32,
    pub amplitude: f32,
    pub steepness: f32,
    pub phase: f32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Water {
    pub enabled: bool,
    pub size: [f32; 2],
    pub level: f32,
    pub depth: f32,
    pub resolution: u32,
    pub time_scale: f32,
    pub albedo: [f32; 4],
    pub roughness: f32,
    pub foam_threshold: f32,
    pub seed: u32,
    /// With no waves the engine uses its wind spectrum.
    pub waves: Vec<WaterWave>,
}

impl Default for Water {
    fn default() -> Self {
        Self {
            enabled: true,
            size: [64.0, 64.0],
            level: 0.0,
            depth: 30.0,
            resolution: 96,
            time_scale: 1.0,
            albedo: [0.03, 0.16, 0.30, 0.78],
            roughness: 0.05,
            foam_threshold: 0.72,
            seed: 1,
            waves: Vec::new(),
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub(crate) struct RawWave {
    dir_x: f32,
    dir_z: f32,
    wavelength: f32,
    amplitude: f32,
    steepness: f32,
    phase: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub(crate) struct RawWater {
    enabled: i32,
    size_x: f32,
    size_z: f32,
    water_level: f32,
    depth: f32,
    resolution: u32,
    time_scale: f32,
    albedo: [f32; 4],
    roughness: f32,
    foam_threshold: f32,
    seed: u32,
    wave_count: u32,
    waves: [RawWave; MAX_WATER_WAVES],
}

impl Water {
    pub(crate) fn to_raw(&self) -> RawWater {
        let mut raw = RawWater {
            enabled: i32::from(self.enabled),
            size_x: self.size[0],
            size_z: self.size[1],
            water_level: self.level,
            depth: self.depth,
            resolution: self.resolution,
            time_scale: self.time_scale,
            albedo: self.albedo,
            roughness: self.roughness,
            foam_threshold: self.foam_threshold,
            seed: self.seed,
            wave_count: self.waves.len().min(MAX_WATER_WAVES) as u32,
            ..RawWater::default()
        };
        for (slot, wave) in raw.waves.iter_mut().zip(&self.waves) {
            *slot = RawWave {
                dir_x: wave.direction[0],
                dir_z: wave.direction[1],
                wavelength: wave.wavelength,
                amplitude: wave.amplitude,
                steepness: wave.steepness,
                phase: wave.phase,
            };
        }
        raw
    }

    pub(crate) fn from_raw(raw: &RawWater) -> Self {
        Water {
            enabled: raw.enabled != 0,
            size: [raw.size_x, raw.size_z],
            level: raw.water_level,
            depth: raw.depth,
            resolution: raw.resolution,
            time_scale: raw.time_scale,
            albedo: raw.albedo,
            roughness: raw.roughness,
            foam_threshold: raw.foam_threshold,
            seed: raw.seed,
            waves: raw
                .waves
                .iter()
                .take((raw.wave_count as usize).min(MAX_WATER_WAVES))
                .map(|w| WaterWave {
                    direction: [w.dir_x, w.dir_z],
                    wavelength: w.wavelength,
                    amplitude: w.amplitude,
                    steepness: w.steepness,
                    phase: w.phase,
                })
                .collect(),
        }
    }
}

/// `caffeine_session_set_water_surface` (heights, coverage, flow over the water's grid).
pub(crate) type SetWaterSurfaceFn = unsafe extern "C" fn(
    *mut std::ffi::c_void,
    u32,
    u32,
    u32,
    *const f32,
    *const u8,
    *const f32,
) -> i32;

impl super::Engine {
    /// True when the engine library can shape a water surface (`caffeine_session_set_water_surface`).
    pub fn has_water_surface(&self) -> bool {
        self.functions.set_water_surface.is_some()
    }

    /// Gives a water entity (it must already have water, `set_water`) a shaped surface: `heights` in metres relative
    /// to the entity over its `size`, `resolution[0] * resolution[1]` values, row 0 at `-size_z / 2`; `coverage` 0 dry
    /// to 255 wet (`None`: wet everywhere); `flow` two floats per vertex in m/s along x and z (`None`: still). Empty
    /// `heights` clears the shape (a flat plane again).
    pub fn set_water_surface(
        &mut self,
        entity: u32,
        resolution: [u32; 2],
        heights: &[f32],
        coverage: Option<&[u8]>,
        flow: Option<&[f32]>,
    ) -> super::EngineResult<()> {
        let set = self
            .functions
            .set_water_surface
            .ok_or(super::EngineError::Error)?;
        let cells = resolution[0] as usize * resolution[1] as usize;
        if heights.is_empty() {
            return super::result(unsafe {
                set(
                    self.session,
                    entity,
                    0,
                    0,
                    std::ptr::null(),
                    std::ptr::null(),
                    std::ptr::null(),
                )
            });
        }
        if resolution[0] < 2
            || resolution[1] < 2
            || heights.len() != cells
            || coverage.is_some_and(|c| c.len() != cells)
            || flow.is_some_and(|f| f.len() != cells * 2)
        {
            return Err(super::EngineError::InvalidArgument);
        }
        super::result(unsafe {
            set(
                self.session,
                entity,
                resolution[0],
                resolution[1],
                heights.as_ptr(),
                coverage.map_or(std::ptr::null(), <[u8]>::as_ptr),
                flow.map_or(std::ptr::null(), <[f32]>::as_ptr),
            )
        })
    }
}

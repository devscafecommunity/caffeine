//! World Composer (C ABI v28), in plain Rust types: build a `.generator.world` composition over a baked terrain.
//!
//! The engine does the whole build (references, water, scatter, edit layer, ground context) and returns data; the
//! UI decides what to do with it (entities for a small world, an instance bake for a large one).

use std::ffi::{c_char, c_void, CStr, CString};
use std::path::Path;

use libloading::Library;

use super::{load_symbol, Engine};

/// Which generator a referenced recipe belongs to (`CaffeineRecipeKind`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WorldRecipeKind {
    Terrain = 0,
    Nature = 1,
    Water = 2,
    Material = 3,
    Texture = 4,
}

/// What the project knows about one referenced recipe.
#[derive(Debug, Clone, PartialEq)]
pub struct WorldRecipe {
    pub reference: String,
    pub kind: WorldRecipeKind,
    pub generator_version: u32,
    pub content_hash: u64,
}

/// A stored terrain mask (`flow`, `wetness`, ...), one byte per vertex.
#[derive(Debug, Clone, PartialEq)]
pub struct WorldMask {
    pub name: String,
    pub resolution: [u32; 2],
    pub data: Vec<u8>,
}

/// Everything a build reads.
#[derive(Debug, Clone, PartialEq)]
pub struct WorldBuildDesc {
    pub graph: String,
    pub generator_version: u32,
    /// `None`: references are not checked (a composition outside a project).
    pub recipes: Option<Vec<WorldRecipe>>,
    /// Normalised heights, `resolution[0] * resolution[1]`, row-major.
    pub heights: Vec<f32>,
    pub resolution: [u32; 2],
    pub size: [f32; 2],
    pub max_height: f32,
    pub masks: Vec<WorldMask>,
    /// `[x0, z0, x1, z1]` metres from the corner; `None` is the whole terrain.
    pub region: Option<[f32; 4]>,
    /// 0: no limit.
    pub max_instances: u32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct WorldClass {
    pub id: String,
    pub reference: String,
    pub salt: u32,
    pub first_instance: u32,
    pub instance_count: u32,
    pub unique: bool,
    /// Nature recipe parameters the ground context drives (`adapt param.<name>`), at most 4.
    pub params: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct WorldInstance {
    pub class_index: u32,
    pub variant: u32,
    pub cell: [i32; 2],
    /// Metres from the terrain's corner.
    pub position: [f32; 3],
    pub up: [f32; 3],
    /// Radians about `up`.
    pub yaw: f32,
    pub scale: f32,
    pub tint: [f32; 3],
    pub seed: u64,
    pub slope: f32,
    pub height: f32,
    pub wetness: f32,
    pub water_distance: f32,
    pub neighbours: f32,
    pub wetness_modifier: f32,
    pub snow_modifier: f32,
    /// Values of the class's `params`, in order.
    pub params: [f32; 4],
}

#[derive(Debug, Clone, PartialEq)]
pub struct WorldWaterBody {
    pub reference: String,
    pub lake: bool,
    pub cells: u32,
    pub coverage: Vec<u8>,
    pub surface: Vec<f32>,
    /// The body as a shaped water surface over its own bounds (`caffeine_world_water_surface`); `None` on a library
    /// without it.
    pub surface_grid: Option<WaterSurfaceGrid>,
}

/// A water body's surface on its own grid, ready for `Engine::set_water_surface`.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct WaterSurfaceGrid {
    /// `[x0, z0, x1, z1]`, metres from the terrain's corner.
    pub bounds: [f32; 4],
    /// Vertices along x and z.
    pub resolution: [u32; 2],
    /// Surface height per vertex, metres (the terrain's frame), row 0 at `z0`.
    pub heights: Vec<f32>,
    /// 0 dry to 255 wet, per vertex.
    pub coverage: Vec<u8>,
    /// Two floats per vertex: the flow along x and z in m/s.
    pub flow: Vec<f32>,
}

/// The result of a build. `errors` is empty exactly when `ok`.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct WorldBuild {
    pub ok: bool,
    pub errors: Vec<String>,
    pub classes: Vec<WorldClass>,
    pub instances: Vec<WorldInstance>,
    pub total: u32,
    pub truncated: bool,
    pub cache_key: u64,
    pub instance_hash: u64,
    pub water: Vec<WorldWaterBody>,
    pub water_distance: Vec<u8>,
    /// The terrain after carving (only when `carved`).
    pub carved: bool,
    pub heights: Vec<f32>,
    pub resolution: [u32; 2],
}

impl WorldBuild {
    /// The instances of one class.
    pub fn instances_of(&self, class_index: usize) -> &[WorldInstance] {
        let Some(class) = self.classes.get(class_index) else {
            return &[];
        };
        let first = class.first_instance as usize;
        &self.instances[first..first + class.instance_count as usize]
    }
}

#[repr(C)]
struct RawRecipe {
    reference: *const c_char,
    kind: u32,
    generator_version: u32,
    content_hash: u64,
}

#[repr(C)]
struct RawMask {
    name: *const c_char,
    resolution_x: u32,
    resolution_z: u32,
    data: *const u8,
}

#[repr(C)]
struct RawDesc {
    graph_text: *const c_char,
    generator_version: u32,
    recipes: *const RawRecipe,
    recipe_count: u32,
    check_refs: i32,
    heights: *const f32,
    resolution_x: u32,
    resolution_z: u32,
    size_x: f32,
    size_z: f32,
    max_height: f32,
    masks: *const RawMask,
    mask_count: u32,
    region: [f32; 4],
    max_instances: u32,
}

#[repr(C)]
#[derive(Default)]
struct RawInfo {
    ok: i32,
    error_count: u32,
    class_count: u32,
    instance_count: u32,
    total: u32,
    truncated: i32,
    cache_key: u64,
    instance_hash: u64,
    water_body_count: u32,
    carved: i32,
    resolution_x: u32,
    resolution_z: u32,
}

#[repr(C)]
struct RawClass {
    id: [c_char; 64],
    reference: [c_char; 256],
    salt: u32,
    first_instance: u32,
    instance_count: u32,
    unique: i32,
    param_count: u32,
    params: [[c_char; 32]; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct RawInstance {
    class_index: u32,
    variant: u32,
    cell: [i32; 2],
    position: [f32; 3],
    up: [f32; 3],
    yaw: f32,
    scale: f32,
    tint: [f32; 3],
    seed: u64,
    slope: f32,
    height: f32,
    wetness: f32,
    water_distance: f32,
    neighbours: f32,
    wetness_modifier: f32,
    snow_modifier: f32,
    params: [f32; 4],
}

#[repr(C)]
struct RawWaterBody {
    reference: [c_char; 256],
    lake: i32,
    cells: u32,
}

type BuildFn = unsafe extern "C" fn(*const RawDesc, *mut *mut c_void) -> i32;
type FreeFn = unsafe extern "C" fn(*mut c_void);
type InfoFn = unsafe extern "C" fn(*const c_void, *mut RawInfo);
type ErrorFn = unsafe extern "C" fn(*const c_void, u32, *mut c_char, u32) -> i32;
type ClassFn = unsafe extern "C" fn(*const c_void, u32, *mut RawClass) -> i32;
type InstancesFn = unsafe extern "C" fn(*const c_void, u32, *mut RawInstance, u32) -> u32;
type WaterBodyFn =
    unsafe extern "C" fn(*const c_void, u32, *mut RawWaterBody, *mut u8, *mut f32) -> i32;
type BytesFn = unsafe extern "C" fn(*const c_void, *mut u8) -> i32;
type HeightsFn = unsafe extern "C" fn(*const c_void, *mut f32) -> i32;
type CheckFn = unsafe extern "C" fn(*const c_char, *mut c_char, u32) -> i32;
type WriteCinstFn = unsafe extern "C" fn(*const c_void, *const c_char, f32) -> i32;
type ReadCinstFn = unsafe extern "C" fn(*const c_char, *mut *mut c_void, *mut c_char, u32) -> i32;
type CacheKeyFn = unsafe extern "C" fn(*const RawDesc, *mut u64) -> i32;
type BuildCachedFn =
    unsafe extern "C" fn(*const RawDesc, *const c_char, *mut *mut c_void, *mut i32) -> i32;
type WaterSurfaceFn = unsafe extern "C" fn(
    *const c_void,
    u32,
    *mut f32,
    *mut u32,
    *mut u32,
    *mut f32,
    *mut u8,
    *mut f32,
) -> i32;

#[derive(Clone, Copy)]
pub(crate) struct WorldFunctions {
    build: BuildFn,
    free: FreeFn,
    info: InfoFn,
    error: ErrorFn,
    class: ClassFn,
    instances: InstancesFn,
    water_body: WaterBodyFn,
    water_distance: BytesFn,
    heights: HeightsFn,
    check: CheckFn,
    write_cinst: WriteCinstFn,
    read_cinst: ReadCinstFn,
    cache_key: CacheKeyFn,
    build_cached: BuildCachedFn,
    /// Optional: a library without it builds worlds whose water has no surface grid.
    water_surface: Option<WaterSurfaceFn>,
}

impl WorldFunctions {
    /// All of them, or `None` on a library older than v28.
    pub(crate) fn load(library: &Library) -> Option<Self> {
        unsafe {
            Some(Self {
                build: load_symbol(library, b"caffeine_world_build\0")?,
                free: load_symbol(library, b"caffeine_world_free\0")?,
                info: load_symbol(library, b"caffeine_world_info\0")?,
                error: load_symbol(library, b"caffeine_world_error\0")?,
                class: load_symbol(library, b"caffeine_world_class\0")?,
                instances: load_symbol(library, b"caffeine_world_instances\0")?,
                water_body: load_symbol(library, b"caffeine_world_water_body\0")?,
                water_distance: load_symbol(library, b"caffeine_world_water_distance\0")?,
                heights: load_symbol(library, b"caffeine_world_heights\0")?,
                check: load_symbol(library, b"caffeine_world_check\0")?,
                write_cinst: load_symbol(library, b"caffeine_world_write_cinst\0")?,
                read_cinst: load_symbol(library, b"caffeine_world_read_cinst\0")?,
                cache_key: load_symbol(library, b"caffeine_world_cache_key\0")?,
                build_cached: load_symbol(library, b"caffeine_world_build_cached\0")?,
                water_surface: load_symbol(library, b"caffeine_world_water_surface\0"),
            })
        }
    }
}

fn text(chars: &[c_char]) -> String {
    unsafe { CStr::from_ptr(chars.as_ptr()) }
        .to_string_lossy()
        .into_owned()
}

const NEEDS_V28: &str = "the engine library needs C ABI v28 (World Composer)";

/// The session-free half of the World Composer: copies of the world functions, nothing else. The C ABI library stays
/// loaded for the process lifetime (`shared_library`), so a builder can be moved to a worker thread and outlive the
/// `Engine` it came from. Every call builds from scratch; nothing is shared between calls.
#[derive(Clone, Copy)]
pub struct WorldBuilder {
    f: WorldFunctions,
}

// The function pointers are plain code addresses into a library that is never unloaded.
const _: fn() = || {
    fn send_sync<T: Send + Sync>() {}
    send_sync::<WorldBuilder>();
};

impl Engine {
    /// A `WorldBuilder` for worker threads, or `None` on a library older than v28.
    pub fn world_builder(&self) -> Option<WorldBuilder> {
        self.functions.world.map(|f| WorldBuilder { f })
    }

    fn builder(&self) -> Result<WorldBuilder, String> {
        self.world_builder().ok_or_else(|| NEEDS_V28.to_owned())
    }

    /// Builds a composition. `Err` only for a desc the engine refuses outright (a NUL in a string, mismatched
    /// sizes, an engine without v28); a composition that does not build is `Ok` with `ok == false` and its errors.
    pub fn world_build(&self, desc: &WorldBuildDesc) -> Result<WorldBuild, String> {
        self.builder()?.build(desc)
    }

    /// `world_build`, then, when it built and `bake` is set, writes the instance bake (`.cinst`) there in chunks of
    /// `chunk_size` metres (0: 64).
    pub fn world_build_baked(
        &self,
        desc: &WorldBuildDesc,
        bake: Option<&Path>,
        chunk_size: f32,
    ) -> Result<WorldBuild, String> {
        self.builder()?.build_baked(desc, bake, chunk_size)
    }

    /// `world_build` through the persistent bake cache in `cache_dir`. The flag says whether the cache answered (a
    /// cached world has classes and instances, no water and no carved heights).
    pub fn world_build_cached(
        &self,
        desc: &WorldBuildDesc,
        cache_dir: &Path,
    ) -> Result<(WorldBuild, bool), String> {
        self.builder()?.build_cached(desc, cache_dir)
    }

    /// The cache key a build of `desc` would have (0 when a reference or the terrain is missing).
    pub fn world_cache_key(&self, desc: &WorldBuildDesc) -> Result<u64, String> {
        let f = self.builder()?.f;
        with_raw_desc(desc, |raw| {
            let mut key = 0u64;
            if unsafe { (f.cache_key)(raw, &mut key) } != 0 {
                return Err("the engine refused the world description".to_owned());
            }
            Ok(key)
        })?
    }

    /// Opens an instance bake (`.cinst`) as a world: classes and instances.
    pub fn world_read_cinst(&self, path: &Path) -> Result<WorldBuild, String> {
        let f = self.builder()?.f;
        let path = CString::new(path.to_string_lossy().as_bytes())
            .map_err(|_| "the path contains a NUL".to_owned())?;
        let mut handle: *mut c_void = std::ptr::null_mut();
        let mut error = vec![0 as c_char; 512];
        if unsafe {
            (f.read_cinst)(
                path.as_ptr(),
                &mut handle,
                error.as_mut_ptr(),
                error.len() as u32,
            )
        } != 0
            || handle.is_null()
        {
            return Err(text(&error));
        }
        let out = unsafe { read_world(&f, handle) };
        unsafe { (f.free)(handle) };
        Ok(out)
    }

    /// Parses and validates a composition's graph text: `Ok`, or every problem, one per line.
    pub fn world_check(&self, graph: &str) -> Result<(), String> {
        self.builder()?.check(graph)
    }
}

impl WorldBuilder {
    /// True when builds fill `WorldWaterBody::surface_grid` (`caffeine_world_water_surface`).
    pub fn has_water_surface(&self) -> bool {
        self.f.water_surface.is_some()
    }

    /// See `Engine::world_build`.
    pub fn build(&self, desc: &WorldBuildDesc) -> Result<WorldBuild, String> {
        self.build_baked(desc, None, 0.0)
    }

    /// See `Engine::world_build_baked`.
    pub fn build_baked(
        &self,
        desc: &WorldBuildDesc,
        bake: Option<&Path>,
        chunk_size: f32,
    ) -> Result<WorldBuild, String> {
        let f = self.f;
        let bake = bake
            .map(|p| CString::new(p.to_string_lossy().as_bytes()))
            .transpose()
            .map_err(|_| "the bake path contains a NUL".to_owned())?;
        with_raw_desc(desc, |raw| {
            let mut handle: *mut c_void = std::ptr::null_mut();
            if unsafe { (f.build)(raw, &mut handle) } != 0 || handle.is_null() {
                return Err("the engine refused the world description".to_owned());
            }
            let mut out = unsafe { read_world(&f, handle) };
            if let (Some(path), true) = (&bake, out.ok) {
                if unsafe { (f.write_cinst)(handle, path.as_ptr(), chunk_size) } != 0 {
                    out.errors
                        .push(format!("cannot write the bake {}", path.to_string_lossy()));
                }
            }
            unsafe { (f.free)(handle) };
            Ok(out)
        })?
    }

    /// See `Engine::world_build_cached`.
    pub fn build_cached(
        &self,
        desc: &WorldBuildDesc,
        cache_dir: &Path,
    ) -> Result<(WorldBuild, bool), String> {
        let f = self.f;
        let dir = CString::new(cache_dir.to_string_lossy().as_bytes())
            .map_err(|_| "the cache path contains a NUL".to_owned())?;
        with_raw_desc(desc, |raw| {
            let mut handle: *mut c_void = std::ptr::null_mut();
            let mut hit = 0i32;
            if unsafe { (f.build_cached)(raw, dir.as_ptr(), &mut handle, &mut hit) } != 0
                || handle.is_null()
            {
                return Err("the engine refused the world description".to_owned());
            }
            let out = unsafe { read_world(&f, handle) };
            unsafe { (f.free)(handle) };
            Ok((out, hit != 0))
        })?
    }

    /// See `Engine::world_check`.
    pub fn check(&self, graph: &str) -> Result<(), String> {
        let graph = CString::new(graph).map_err(|_| "the text contains a NUL".to_owned())?;
        let mut error = vec![0 as c_char; 4096];
        if unsafe { (self.f.check)(graph.as_ptr(), error.as_mut_ptr(), error.len() as u32) } == 0 {
            return Ok(());
        }
        Err(text(&error))
    }
}

/// Lays `desc` out as the C struct (with the buffers it points into) for the duration of `body`.
fn with_raw_desc<R>(desc: &WorldBuildDesc, body: impl FnOnce(&RawDesc) -> R) -> Result<R, String> {
    let cells = desc.resolution[0] as usize * desc.resolution[1] as usize;
    if desc.heights.len() != cells {
        return Err(format!(
            "{} heights for a {}x{} grid",
            desc.heights.len(),
            desc.resolution[0],
            desc.resolution[1]
        ));
    }
    let nul = |_| "a string contains a NUL".to_owned();
    let graph = CString::new(desc.graph.as_str()).map_err(nul)?;
    let recipe_names = desc
        .recipes
        .iter()
        .flatten()
        .map(|r| CString::new(r.reference.as_str()))
        .collect::<Result<Vec<_>, _>>()
        .map_err(nul)?;
    let recipes: Vec<RawRecipe> = desc
        .recipes
        .iter()
        .flatten()
        .zip(&recipe_names)
        .map(|(r, name)| RawRecipe {
            reference: name.as_ptr(),
            kind: r.kind as u32,
            generator_version: r.generator_version,
            content_hash: r.content_hash,
        })
        .collect();
    let mask_names = desc
        .masks
        .iter()
        .map(|m| CString::new(m.name.as_str()))
        .collect::<Result<Vec<_>, _>>()
        .map_err(nul)?;
    let mut masks = Vec::with_capacity(desc.masks.len());
    for (mask, name) in desc.masks.iter().zip(&mask_names) {
        if mask.data.len() != mask.resolution[0] as usize * mask.resolution[1] as usize {
            return Err(format!("the mask `{}` has the wrong size", mask.name));
        }
        masks.push(RawMask {
            name: name.as_ptr(),
            resolution_x: mask.resolution[0],
            resolution_z: mask.resolution[1],
            data: mask.data.as_ptr(),
        });
    }
    let raw = RawDesc {
        graph_text: graph.as_ptr(),
        generator_version: desc.generator_version,
        recipes: recipes.as_ptr(),
        recipe_count: recipes.len() as u32,
        check_refs: i32::from(desc.recipes.is_some()),
        heights: desc.heights.as_ptr(),
        resolution_x: desc.resolution[0],
        resolution_z: desc.resolution[1],
        size_x: desc.size[0],
        size_z: desc.size[1],
        max_height: desc.max_height,
        masks: masks.as_ptr(),
        mask_count: masks.len() as u32,
        region: desc.region.unwrap_or([0.0; 4]),
        max_instances: desc.max_instances,
    };
    Ok(body(&raw))
}

/// Copies everything out of a world handle.
///
/// # Safety
/// `handle` must be a live `CaffeineWorld*` from `f.build`.
unsafe fn read_world(f: &WorldFunctions, handle: *mut c_void) -> WorldBuild {
    let mut info = RawInfo::default();
    (f.info)(handle, &mut info);
    let mut out = WorldBuild {
        ok: info.ok != 0,
        total: info.total,
        truncated: info.truncated != 0,
        cache_key: info.cache_key,
        instance_hash: info.instance_hash,
        carved: info.carved != 0,
        resolution: [info.resolution_x, info.resolution_z],
        ..WorldBuild::default()
    };
    let mut buffer = vec![0 as c_char; 1024];
    for i in 0..info.error_count {
        if (f.error)(handle, i, buffer.as_mut_ptr(), buffer.len() as u32) == 0 {
            out.errors.push(text(&buffer));
        }
    }
    for i in 0..info.class_count {
        let mut raw = RawClass {
            id: [0; 64],
            reference: [0; 256],
            salt: 0,
            first_instance: 0,
            instance_count: 0,
            unique: 0,
            param_count: 0,
            params: [[0; 32]; 4],
        };
        if (f.class)(handle, i, &mut raw) == 0 {
            out.classes.push(WorldClass {
                id: text(&raw.id),
                reference: text(&raw.reference),
                salt: raw.salt,
                first_instance: raw.first_instance,
                instance_count: raw.instance_count,
                unique: raw.unique != 0,
                params: raw.params[..raw.param_count.min(4) as usize]
                    .iter()
                    .map(|p| text(p))
                    .collect(),
            });
        }
    }
    let mut raw = vec![RawInstance::default(); info.instance_count as usize];
    let written = (f.instances)(handle, 0, raw.as_mut_ptr(), info.instance_count);
    raw.truncate(written as usize);
    out.instances = raw
        .iter()
        .map(|r| WorldInstance {
            class_index: r.class_index,
            variant: r.variant,
            cell: r.cell,
            position: r.position,
            up: r.up,
            yaw: r.yaw,
            scale: r.scale,
            tint: r.tint,
            seed: r.seed,
            slope: r.slope,
            height: r.height,
            wetness: r.wetness,
            water_distance: r.water_distance,
            neighbours: r.neighbours,
            wetness_modifier: r.wetness_modifier,
            snow_modifier: r.snow_modifier,
            params: r.params,
        })
        .collect();
    let cells = info.resolution_x as usize * info.resolution_z as usize;
    for i in 0..info.water_body_count {
        let mut body = RawWaterBody {
            reference: [0; 256],
            lake: 0,
            cells: 0,
        };
        let mut coverage = vec![0u8; cells];
        let mut surface = vec![0f32; cells];
        if (f.water_body)(
            handle,
            i,
            &mut body,
            coverage.as_mut_ptr(),
            surface.as_mut_ptr(),
        ) == 0
        {
            out.water.push(WorldWaterBody {
                reference: text(&body.reference),
                lake: body.lake != 0,
                cells: body.cells,
                coverage,
                surface,
                surface_grid: f
                    .water_surface
                    .and_then(|get| read_water_surface(get, handle, i)),
            });
        }
    }
    if out.ok && cells > 0 {
        let mut distance = vec![0u8; cells];
        if (f.water_distance)(handle, distance.as_mut_ptr()) == 0 {
            out.water_distance = distance;
        }
        if out.carved {
            let mut heights = vec![0f32; cells];
            if (f.heights)(handle, heights.as_mut_ptr()) == 0 {
                out.heights = heights;
            }
        }
    }
    out
}

/// The surface grid of water body `index`: its resolution first (NULL arrays), then the arrays.
///
/// # Safety
/// `handle` must be a live `CaffeineWorld*`.
unsafe fn read_water_surface(
    get: WaterSurfaceFn,
    handle: *mut c_void,
    index: u32,
) -> Option<WaterSurfaceGrid> {
    let mut bounds = [0f32; 4];
    let (mut rx, mut rz) = (0u32, 0u32);
    if get(
        handle,
        index,
        bounds.as_mut_ptr(),
        &mut rx,
        &mut rz,
        std::ptr::null_mut(),
        std::ptr::null_mut(),
        std::ptr::null_mut(),
    ) != 0
        || rx < 2
        || rz < 2
    {
        return None;
    }
    let cells = rx as usize * rz as usize;
    let mut grid = WaterSurfaceGrid {
        bounds,
        resolution: [rx, rz],
        heights: vec![0.0; cells],
        coverage: vec![0; cells],
        flow: vec![0.0; cells * 2],
    };
    let (mut again_x, mut again_z) = (rx, rz);
    if get(
        handle,
        index,
        grid.bounds.as_mut_ptr(),
        &mut again_x,
        &mut again_z,
        grid.heights.as_mut_ptr(),
        grid.coverage.as_mut_ptr(),
        grid.flow.as_mut_ptr(),
    ) != 0
        || (again_x, again_z) != (rx, rz)
    {
        return None;
    }
    Some(grid)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_lock;

    fn bowl() -> WorldBuildDesc {
        const N: u32 = 65;
        let mut heights = Vec::new();
        let mut wetness = Vec::new();
        for z in 0..N {
            for x in 0..N {
                let (fx, fz) = (x as f32 - 20.0, z as f32 - 40.0);
                let h = (0.1 + 0.0002 * (fx * fx + fz * fz)).min(1.0);
                heights.push(h);
                wetness.push((255.0 * (1.0 - h)) as u8);
            }
        }
        let recipe = |reference: &str, kind| WorldRecipe {
            reference: reference.to_owned(),
            kind,
            generator_version: 2,
            content_hash: 7,
        };
        WorldBuildDesc {
            graph: "seed 9\nterrain Terrain/Hill.generator.terrain\n\
                    water Water/Lake.generator.water fill basins level 0.3\n\
                    class rock ref Nature/Rock.generator.nature priority 30 cell 6 radius 1.5 variants 3\n\
                    class tree ref Nature/Tree.generator.nature priority 20 cell 4 radius 1.2\n  wetness 0.1 0.2 1 1\n\
                    \x20 adapt size wetness\n"
                .to_owned(),
            generator_version: 2,
            recipes: Some(vec![
                recipe("Terrain/Hill.generator.terrain", WorldRecipeKind::Terrain),
                recipe("Water/Lake.generator.water", WorldRecipeKind::Water),
                recipe("Nature/Rock.generator.nature", WorldRecipeKind::Nature),
                recipe("Nature/Tree.generator.nature", WorldRecipeKind::Nature),
            ]),
            heights,
            resolution: [N, N],
            size: [128.0, 128.0],
            max_height: 20.0,
            masks: vec![WorldMask {
                name: "wetness".to_owned(),
                resolution: [N, N],
                data: wetness,
            }],
            region: None,
            max_instances: 0,
        }
    }

    #[test]
    fn a_world_builds_the_same_every_time_and_reports_its_errors() {
        let _guard = test_lock();
        let Some(engine) = Engine::load_default() else {
            return;
        };
        if engine.functions.world.is_none() {
            return; // a libcaffeine-capi from before C ABI 28
        }
        let desc = bowl();
        let world = engine.world_build(&desc).expect("build");
        assert!(world.ok, "{:?}", world.errors);
        assert_eq!(world.classes.len(), 2);
        assert_eq!(world.classes[0].id, "rock");
        assert!(!world.instances_of(1).is_empty());
        assert_eq!(world.instances.len() as u32, world.total);
        assert_eq!(world.water.len(), 1);
        assert!(world.water[0].lake && world.water[0].cells > 0);
        assert_eq!(world.water_distance.len(), 65 * 65);
        let again = engine.world_build(&desc).expect("build");
        assert_eq!(again.instance_hash, world.instance_hash);
        assert_eq!(again.cache_key, world.cache_key);

        // Halves add up to the whole.
        let mut left = desc.clone();
        left.region = Some([0.0, 0.0, 60.0, 128.0]);
        let mut right = desc.clone();
        right.region = Some([60.0, 0.0, 128.0, 128.0]);
        let (l, r) = (
            engine.world_build(&left).unwrap(),
            engine.world_build(&right).unwrap(),
        );
        assert_eq!(l.instances.len() + r.instances.len(), world.instances.len());

        let mut wrong = desc.clone();
        wrong.recipes.as_mut().unwrap()[3].kind = WorldRecipeKind::Material;
        let failed = engine.world_build(&wrong).expect("build");
        assert!(!failed.ok);
        assert!(failed.errors[0].contains("Tree.generator.nature"));
        assert!(failed.instances.is_empty());

        // The bake and the cache.
        let dir = std::env::temp_dir().join(format!("doppio-world-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let bake = dir.join("bowl.cinst");
        let baked = engine.world_build_baked(&desc, Some(&bake), 32.0).unwrap();
        assert!(baked.ok && bake.exists());
        let read = engine.world_read_cinst(&bake).unwrap();
        assert_eq!(read.instances.len(), world.instances.len());
        assert_eq!(read.cache_key, world.cache_key);
        assert!(engine.world_read_cinst(&dir.join("none.cinst")).is_err());
        assert_eq!(engine.world_cache_key(&desc).unwrap(), world.cache_key);
        let cache = dir.join("cache");
        let (first, hit) = engine.world_build_cached(&desc, &cache).unwrap();
        assert!(first.ok && !hit);
        let (second, hit) = engine.world_build_cached(&desc, &cache).unwrap();
        assert!(hit);
        assert_eq!(second.instance_hash, world.instance_hash);
        let _ = std::fs::remove_dir_all(&dir);

        assert!(engine.world_check(&desc.graph).is_ok());
        assert!(engine
            .world_check("class oak\n")
            .unwrap_err()
            .contains("no `terrain`"));
        let mut bad = desc;
        bad.heights.pop();
        assert!(engine.world_build(&bad).is_err());
    }

    #[test]
    fn a_world_builder_builds_on_another_thread_like_the_engine() {
        let _guard = test_lock();
        let Some(engine) = Engine::load_default() else {
            return;
        };
        let Some(builder) = engine.world_builder() else {
            return; // a libcaffeine-capi from before C ABI 28
        };
        let desc = bowl();
        let here = engine.world_build(&desc).expect("build");
        let there = std::thread::spawn(move || builder.build(&desc).expect("build"))
            .join()
            .expect("the worker");
        assert_eq!(there.instance_hash, here.instance_hash);
        assert_eq!(there.instances.len(), here.instances.len());
        assert!(builder.check(&bowl().graph).is_ok());
        assert!(builder.check("class oak\n").is_err());
    }

    #[test]
    fn water_bodies_carry_their_surface_grid_when_the_engine_has_it() {
        let _guard = test_lock();
        let Some(engine) = Engine::load_default() else {
            return;
        };
        let Some(builder) = engine.world_builder() else {
            return;
        };
        let world = builder.build(&bowl()).expect("build");
        assert!(world.ok, "{:?}", world.errors);
        if !builder.has_water_surface() {
            // A library from before `caffeine_world_water_surface`: no grid, nothing else changes.
            assert!(world.water.iter().all(|b| b.surface_grid.is_none()));
            return;
        }
        let grid = world.water[0]
            .surface_grid
            .as_ref()
            .expect("a surface grid");
        let cells = grid.resolution[0] as usize * grid.resolution[1] as usize;
        assert!(grid.resolution[0] >= 2 && grid.resolution[1] >= 2);
        assert_eq!(grid.heights.len(), cells);
        assert_eq!(grid.coverage.len(), cells);
        assert_eq!(grid.flow.len(), cells * 2);
        assert!(grid.bounds[2] > grid.bounds[0] && grid.bounds[3] > grid.bounds[1]);
        assert!(grid.coverage.iter().any(|c| *c > 0));
    }
}

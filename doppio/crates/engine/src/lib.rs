//! Safe Rust bindings to the Caffeine C ABI.
//!
//! This crate is the only place that knows about `libcaffeine-capi`. The UI
//! crates consume a typed session and never touch function pointers. When the
//! shared library is not built, callers can fall back to fixtures.

use std::ffi::{c_char, c_void, CStr, CString};
use std::path::{Path, PathBuf};

use libloading::Library;

mod material;
pub use material::{Material, MaterialAlphaMode, MaterialPreset};
use material::{RawMaterial, RawMaterialPresetInfo};

pub const ENTITY_NONE: u32 = u32::MAX;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EngineError {
    Error,
    InvalidArgument,
    NotFound,
}

pub type EngineResult<T> = Result<T, EngineError>;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EntityInfo {
    pub id: u32,
    pub name: String,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Camera {
    pub yaw: f32,
    pub pitch: f32,
    pub distance: f32,
    pub focus: [f32; 3],
}

impl Default for Camera {
    fn default() -> Self {
        Self {
            yaw: 0.65,
            pitch: 0.35,
            distance: 10.0,
            focus: [0.0; 3],
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AntiAliasing {
    Off,
    Fxaa,
    Taa,
}

impl AntiAliasing {
    fn from_raw(value: i32) -> Self {
        match value {
            0 => Self::Off,
            2 => Self::Taa,
            _ => Self::Fxaa,
        }
    }

    fn raw(self) -> i32 {
        match self {
            Self::Off => 0,
            Self::Fxaa => 1,
            Self::Taa => 2,
        }
    }
}

/// Offscreen viewport quality and overlays.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ViewportSettings {
    pub show_grid: bool,
    pub anti_aliasing: AntiAliasing,
    /// Internal supersampling factor, clamped by the engine to [0.5, 2.0].
    pub render_scale: f32,
}

impl Default for ViewportSettings {
    fn default() -> Self {
        Self {
            show_grid: true,
            anti_aliasing: AntiAliasing::Fxaa,
            render_scale: 1.5,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AssetInfo {
    pub name: String,
    pub relative_path: String,
    pub kind: String,
    pub size_bytes: u64,
    pub is_directory: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TextureMapKind {
    Normal,
    AmbientOcclusion,
    Roughness,
    Metallic,
    Height,
}

impl TextureMapKind {
    fn raw(self) -> i32 {
        match self {
            TextureMapKind::Normal => 0,
            TextureMapKind::AmbientOcclusion => 1,
            TextureMapKind::Roughness => 2,
            TextureMapKind::Metallic => 3,
            TextureMapKind::Height => 4,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PreviewShape {
    Sphere,
    Cube,
    Plane,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MeshMapKind {
    Normal,
    Position,
    ObjectId,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TexturePackKind {
    /// R = AO, G = roughness, B = metallic.
    Orm,
    /// Unity HDRP: R = metallic, G = AO, B = detail, A = smoothness.
    UnityHdrpMask,
    /// Unity Standard: R = metallic, A = smoothness.
    UnityMetallicSmoothness,
}

impl TexturePackKind {
    fn raw(self) -> i32 {
        match self {
            TexturePackKind::Orm => 0,
            TexturePackKind::UnityHdrpMask => 1,
            TexturePackKind::UnityMetallicSmoothness => 2,
        }
    }
}

#[derive(Debug, Clone, Copy, Default)]
pub struct TextureMapOptions {
    pub strength: f32,
    pub directx: bool,
    pub wrap: bool,
    pub min_value: f32,
    pub max_value: f32,
    pub invert: bool,
    pub radius: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FieldKind {
    F32,
    Bool,
    I32,
    U32,
    Vec2,
    Vec3,
    Vec4,
    String,
    Enum,
    Color,
}

#[derive(Debug, Clone, PartialEq)]
pub enum FieldValue {
    F32(f32),
    Bool(bool),
    I32(i32),
    U32(u32),
    Vec2([f32; 2]),
    Vec3([f32; 3]),
    Vec4([f32; 4]),
    String(String),
    Enum(i32),
    Color([f32; 4]),
}

#[derive(Debug, Clone)]
pub struct FieldInfo {
    pub name: String,
    pub kind: FieldKind,
    pub min: f32,
    pub max: f32,
    pub step: f32,
    pub options: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct ComponentType {
    pub id: u32,
    pub name: String,
    pub fields: Vec<FieldInfo>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ComponentInfo {
    pub type_id: u32,
    pub name: String,
    pub field_count: u32,
}

#[derive(Debug, Clone)]
pub struct ComponentData {
    pub info: ComponentInfo,
    pub values: Vec<FieldValue>,
}

#[derive(Debug, Clone)]
pub struct Snapshot {
    pub data: Vec<u8>,
    pub dirty: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RenderState {
    Idle,
    Pending,
    Ready,
    Failed,
}

#[derive(Debug, Clone, Copy, Default)]
pub struct FrameStats {
    pub delta_time: f32,
    pub frame_index: u64,
    pub entity_count: u32,
}

#[repr(C)]
struct RawEntityInfo {
    id: u32,
    name: [c_char; 64],
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
struct RawVec3 {
    x: f32,
    y: f32,
    z: f32,
}

#[repr(C)]
struct RawFieldInfo {
    name: [c_char; 64],
    kind: i32,
    min_value: f32,
    max_value: f32,
    step: f32,
    enum_options: [c_char; 256],
}

#[repr(C)]
struct RawFieldValue {
    kind: i32,
    vec: [f32; 4],
    integer: i32,
    boolean: i32,
    text: [c_char; 256],
}

#[repr(C)]
struct RawComponentTypeInfo {
    id: u32,
    name: [c_char; 64],
    field_count: u32,
}

#[repr(C)]
struct RawComponentInfo {
    type_id: u32,
    name: [c_char; 64],
    field_count: u32,
}

#[repr(C)]
struct RawAssetInfo {
    name: [c_char; 128],
    relative_path: [c_char; 512],
    kind: [c_char; 32],
    size_bytes: u64,
    is_directory: i32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
struct RawCamera {
    yaw: f32,
    pitch: f32,
    distance: f32,
    focus: RawVec3,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
struct RawViewportSettings {
    show_grid: i32,
    anti_aliasing: i32,
    render_scale: f32,
}

impl From<RawViewportSettings> for ViewportSettings {
    fn from(raw: RawViewportSettings) -> Self {
        Self {
            show_grid: raw.show_grid != 0,
            anti_aliasing: AntiAliasing::from_raw(raw.anti_aliasing),
            render_scale: raw.render_scale,
        }
    }
}

impl From<ViewportSettings> for RawViewportSettings {
    fn from(settings: ViewportSettings) -> Self {
        Self {
            show_grid: i32::from(settings.show_grid),
            anti_aliasing: settings.anti_aliasing.raw(),
            render_scale: settings.render_scale,
        }
    }
}

#[repr(C)]
struct RawFrameStats {
    delta_time: f32,
    frame_index: u64,
    entity_count: u32,
}

type CapiVersionFn = unsafe extern "C" fn() -> i32;
type CreateFn = unsafe extern "C" fn() -> *mut c_void;
type DestroyFn = unsafe extern "C" fn(*mut c_void);
type LoadSceneFn = unsafe extern "C" fn(*mut c_void, *const c_char) -> i32;
type SaveSceneFn = unsafe extern "C" fn(*mut c_void, *const c_char) -> i32;
type ScenePathFn = unsafe extern "C" fn(*const c_void) -> *const c_char;
type IsDirtyFn = unsafe extern "C" fn(*const c_void) -> i32;
type EntityCountFn = unsafe extern "C" fn(*mut c_void) -> u32;
type EntityAtFn = unsafe extern "C" fn(*mut c_void, u32, *mut RawEntityInfo) -> i32;
type EntityNameFn = unsafe extern "C" fn(*mut c_void, u32, *mut c_char, usize) -> i32;
type CreateEntityFn = unsafe extern "C" fn(*mut c_void, *const c_char) -> u32;
type DestroyEntityFn = unsafe extern "C" fn(*mut c_void, u32) -> i32;
type SetNameFn = unsafe extern "C" fn(*mut c_void, u32, *const c_char) -> i32;
type ParentFn = unsafe extern "C" fn(*const c_void, u32, *mut u32) -> i32;
type SetParentFn = unsafe extern "C" fn(*mut c_void, u32, u32) -> i32;
type SelectFn = unsafe extern "C" fn(*mut c_void, u32) -> i32;
type SelectedFn = unsafe extern "C" fn(*mut c_void) -> u32;
type PositionFn = unsafe extern "C" fn(*mut c_void, u32, *mut RawVec3) -> i32;
type SetPositionFn = unsafe extern "C" fn(*mut c_void, u32, RawVec3) -> i32;
type RotationFn = unsafe extern "C" fn(*mut c_void, u32, *mut RawVec3) -> i32;
type SetRotationFn = unsafe extern "C" fn(*mut c_void, u32, RawVec3) -> i32;
type ScaleFn = unsafe extern "C" fn(*mut c_void, u32, *mut RawVec3) -> i32;
type SetScaleFn = unsafe extern "C" fn(*mut c_void, u32, RawVec3) -> i32;
type WorldPositionFn = unsafe extern "C" fn(*const c_void, u32, *mut RawVec3) -> i32;
type SetWorldPositionFn = unsafe extern "C" fn(*mut c_void, u32, RawVec3) -> i32;
type RenderFn = unsafe extern "C" fn(*mut c_void, u32, u32, *mut u8, usize) -> i32;
type RenderBeginFn = unsafe extern "C" fn(*mut c_void, u32, u32) -> i32;
type RenderPollFn = unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32;
type SnapshotFn = unsafe extern "C" fn(*const c_void, *mut *mut u8, *mut usize, *mut i32) -> i32;
type FreeBytesFn = unsafe extern "C" fn(*mut c_void);
type RestoreFn = unsafe extern "C" fn(*mut c_void, *const u8, usize, i32) -> i32;
type ComponentTypeCountFn = unsafe extern "C" fn() -> u32;
type ComponentTypeAtFn = unsafe extern "C" fn(u32, *mut RawComponentTypeInfo) -> i32;
type ComponentTypeFieldAtFn = unsafe extern "C" fn(u32, u32, *mut RawFieldInfo) -> i32;
type ComponentCountFn = unsafe extern "C" fn(*const c_void, u32) -> u32;
type ComponentAtFn = unsafe extern "C" fn(*const c_void, u32, u32, *mut RawComponentInfo) -> i32;
type ComponentFieldGetFn =
    unsafe extern "C" fn(*const c_void, u32, u32, u32, *mut RawFieldValue) -> i32;
type ComponentFieldSetFn =
    unsafe extern "C" fn(*mut c_void, u32, u32, u32, *const RawFieldValue) -> i32;
type AddComponentFn = unsafe extern "C" fn(*mut c_void, u32, u32) -> i32;
type RemoveComponentFn = unsafe extern "C" fn(*mut c_void, u32, u32) -> i32;
type SetCameraFn = unsafe extern "C" fn(*mut c_void, RawCamera) -> i32;
type GetCameraFn = unsafe extern "C" fn(*mut c_void, *mut RawCamera) -> i32;
type SetViewportSettingsFn = unsafe extern "C" fn(*mut c_void, RawViewportSettings) -> i32;
type GetViewportSettingsFn = unsafe extern "C" fn(*const c_void, *mut RawViewportSettings) -> i32;
type SetAssetRootFn = unsafe extern "C" fn(*mut c_void, *const c_char) -> i32;
type RefreshAssetsFn = unsafe extern "C" fn(*mut c_void) -> i32;
type AssetCountFn = unsafe extern "C" fn(*const c_void) -> u32;
type AssetAtFn = unsafe extern "C" fn(*const c_void, u32, *mut RawAssetInfo) -> i32;
type TickFn = unsafe extern "C" fn(*mut c_void, f32);
type FrameStatsFn = unsafe extern "C" fn(*mut c_void, *mut RawFrameStats);
type MaterialDefaultFn = unsafe extern "C" fn(*mut RawMaterial) -> i32;
type MaterialLoadFn = unsafe extern "C" fn(*const c_char, *mut RawMaterial) -> i32;
type MaterialSaveFn = unsafe extern "C" fn(*const c_char, *const RawMaterial) -> i32;
type MaterialPresetCountFn = unsafe extern "C" fn() -> u32;
type MaterialPresetAtFn =
    unsafe extern "C" fn(u32, *mut RawMaterialPresetInfo, *mut RawMaterial) -> i32;
type ApplyMaterialFn = unsafe extern "C" fn(*mut c_void, u32, *const c_char) -> i32;
type EntityMaterialFn = unsafe extern "C" fn(*const c_void, u32, *mut c_char, usize) -> i32;
type MaterialPreviewFn =
    unsafe extern "C" fn(*mut c_void, *const RawMaterial, u32, f32, f32, *mut u8, usize) -> i32;
type AssetPathFn = unsafe extern "C" fn(*const c_void, *const c_char, *mut c_char, usize) -> i32;
type GenerateMapFn = unsafe extern "C" fn(
    *const c_char,
    *const c_char,
    i32,
    f32,
    i32,
    i32,
    f32,
    f32,
    i32,
    u32,
) -> i32;
type PackMapsFn =
    unsafe extern "C" fn(*const c_char, *const c_char, *const c_char, *const c_char, i32) -> i32;
type SaveRgbaFn = unsafe extern "C" fn(*const c_char, u32, u32, *const u8, usize) -> i32;
type SaveRgba16Fn = unsafe extern "C" fn(*const c_char, u32, u32, *const u16, usize) -> i32;
type LoadThumbnailFn =
    unsafe extern "C" fn(*const c_char, u32, *mut u32, *mut u32, *mut *mut u8, *mut usize) -> i32;
type LoadRgbaFn =
    unsafe extern "C" fn(*const c_char, *mut u32, *mut u32, *mut *mut u8, *mut usize) -> i32;
type BakeMeshMapFn = unsafe extern "C" fn(*mut c_void, u32, i32, u32, *const c_char) -> i32;
type PreviewExFn = unsafe extern "C" fn(
    *mut c_void,
    *const RawMaterial,
    u32,
    f32,
    f32,
    i32,
    *const c_char,
    f32,
    *mut u8,
    usize,
) -> i32;
type PreviewBeginFn = unsafe extern "C" fn(
    *mut c_void,
    *const RawMaterial,
    u32,
    f32,
    f32,
    i32,
    *const c_char,
    f32,
) -> i32;
type PreviewLightFn = unsafe extern "C" fn(*mut c_void, f32, f32, f32, i32) -> i32;
type PreviewPollFn = unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32;
type DryadAvailableFn = unsafe extern "C" fn() -> i32;
type RunDryadFn = unsafe extern "C" fn(*mut c_void, u32, *const c_char, f32) -> i32;
type PickUvFn =
    unsafe extern "C" fn(*mut c_void, u32, f32, f32, f32, f32, *mut f32, *mut f32) -> i32;
type BakeAoFn = unsafe extern "C" fn(
    *mut c_void,
    u32,
    u32,
    u32,
    f32,
    *const c_char,
    *mut RawAOBakeResult,
) -> i32;

#[repr(C)]
#[derive(Clone, Copy)]
struct RawAOBakeResult {
    ok: i32,
    used_gpu: i32,
    covered_texels: u32,
    triangles: u32,
    orm_path: [c_char; 512],
    material_path: [c_char; 512],
    error: [c_char; 256],
}

/// Result of a scene-geometry AO bake.
pub struct AOBakeResult {
    pub ok: bool,
    pub used_gpu: bool,
    pub covered_texels: u32,
    pub triangles: u32,
    pub orm_path: String,
    pub material_path: String,
    pub error: String,
}

struct Functions {
    capi_version: CapiVersionFn,
    destroy: DestroyFn,
    load_scene: LoadSceneFn,
    save_scene: SaveSceneFn,
    scene_path: ScenePathFn,
    is_dirty: IsDirtyFn,
    entity_count: EntityCountFn,
    entity_at: EntityAtFn,
    entity_name: EntityNameFn,
    create_entity: CreateEntityFn,
    destroy_entity: DestroyEntityFn,
    set_name: SetNameFn,
    parent: ParentFn,
    set_parent: SetParentFn,
    select: SelectFn,
    selected: SelectedFn,
    position: PositionFn,
    set_position: SetPositionFn,
    rotation: RotationFn,
    set_rotation: SetRotationFn,
    scale: ScaleFn,
    set_scale: SetScaleFn,
    world_position: WorldPositionFn,
    set_world_position: SetWorldPositionFn,
    render: RenderFn,
    render_begin: RenderBeginFn,
    render_poll: RenderPollFn,
    snapshot: SnapshotFn,
    free_bytes: FreeBytesFn,
    restore: RestoreFn,
    component_type_count: ComponentTypeCountFn,
    component_type_at: ComponentTypeAtFn,
    component_type_field_at: ComponentTypeFieldAtFn,
    component_count: ComponentCountFn,
    component_at: ComponentAtFn,
    component_field_get: ComponentFieldGetFn,
    component_field_set: ComponentFieldSetFn,
    add_component: AddComponentFn,
    remove_component: RemoveComponentFn,
    set_camera: SetCameraFn,
    get_camera: GetCameraFn,
    set_viewport_settings: SetViewportSettingsFn,
    get_viewport_settings: GetViewportSettingsFn,
    set_asset_root: SetAssetRootFn,
    refresh_assets: RefreshAssetsFn,
    asset_count: AssetCountFn,
    asset_at: AssetAtFn,
    tick: TickFn,
    frame_stats: FrameStatsFn,
    material_default: MaterialDefaultFn,
    material_load: MaterialLoadFn,
    material_save: MaterialSaveFn,
    material_preset_count: MaterialPresetCountFn,
    material_preset_at: MaterialPresetAtFn,
    apply_material: ApplyMaterialFn,
    entity_material: EntityMaterialFn,
    material_preview: MaterialPreviewFn,
    asset_path: AssetPathFn,
    asset_project_path: AssetPathFn,
    resolve_path: AssetPathFn,
    generate_map: GenerateMapFn,
    pack_maps: PackMapsFn,
    save_rgba: SaveRgbaFn,
    save_rgba16: SaveRgba16Fn,
    load_rgba: LoadRgbaFn,
    load_thumbnail: LoadThumbnailFn,
    bake_ao: BakeAoFn,
    pick_uv: PickUvFn,
    bake_mesh_map: BakeMeshMapFn,
    material_preview_ex: PreviewExFn,
    material_preview_begin: PreviewBeginFn,
    material_preview_poll: PreviewPollFn,
    preview_light: Option<PreviewLightFn>,
    dryad_available: Option<DryadAvailableFn>,
    run_dryad: Option<RunDryadFn>,
}

pub struct Engine {
    session: *mut c_void,
    functions: Functions,
}

unsafe fn load_symbol<T: Copy>(library: &Library, name: &[u8]) -> Option<T> {
    library.get::<T>(name).ok().map(|symbol| *symbol)
}

/// The C ABI library stays loaded for the process lifetime.
///
/// Unloading it (`dlclose`) while the editor still holds SDL/GPU state crashes
/// the process, and there is no benefit in reloading it. Sessions are still
/// created and destroyed per `Engine`.
fn shared_library(path: &Path) -> Option<&'static Library> {
    use std::sync::OnceLock;

    static LIBRARY: OnceLock<Library> = OnceLock::new();
    if let Some(library) = LIBRARY.get() {
        return Some(library);
    }
    let loaded = unsafe { Library::new(path) }.ok()?;
    let _ = LIBRARY.set(loaded);
    LIBRARY.get()
}

fn default_library_path() -> PathBuf {
    #[cfg(target_os = "windows")]
    const FILE: &str = "caffeine-capi.dll";
    #[cfg(target_os = "macos")]
    const FILE: &str = "libcaffeine-capi.dylib";
    #[cfg(not(any(target_os = "windows", target_os = "macos")))]
    const FILE: &str = "libcaffeine-capi.so";

    if let Some(path) = std::env::var_os("CAFFEINE_CAPI_LIB") {
        return PathBuf::from(path);
    }
    // Packaged builds ship the shared library next to the executable.
    if let Ok(executable) = std::env::current_exe() {
        if let Some(directory) = executable.parent() {
            let candidate = directory.join(FILE);
            if candidate.exists() {
                return candidate;
            }
        }
    }
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("..")
        .join("build")
        .join(FILE)
}

impl Engine {
    pub fn load_default() -> Option<Self> {
        Self::load(&default_library_path())
    }

    pub fn load(path: &Path) -> Option<Self> {
        let library = shared_library(path)?;
        let create: CreateFn = unsafe { load_symbol(library, b"caffeine_session_create\0") }?;
        let functions = Functions {
            capi_version: unsafe { load_symbol(library, b"caffeine_capi_version\0") }?,
            destroy: unsafe { load_symbol(library, b"caffeine_session_destroy\0") }?,
            load_scene: unsafe { load_symbol(library, b"caffeine_session_load_scene\0") }?,
            save_scene: unsafe { load_symbol(library, b"caffeine_session_save_scene\0") }?,
            scene_path: unsafe { load_symbol(library, b"caffeine_session_scene_path\0") }?,
            is_dirty: unsafe { load_symbol(library, b"caffeine_session_is_dirty\0") }?,
            entity_count: unsafe { load_symbol(library, b"caffeine_session_entity_count\0") }?,
            entity_at: unsafe { load_symbol(library, b"caffeine_session_entity_at\0") }?,
            entity_name: unsafe { load_symbol(library, b"caffeine_session_entity_name\0") }?,
            create_entity: unsafe { load_symbol(library, b"caffeine_session_create_entity\0") }?,
            destroy_entity: unsafe { load_symbol(library, b"caffeine_session_destroy_entity\0") }?,
            set_name: unsafe { load_symbol(library, b"caffeine_session_set_entity_name\0") }?,
            parent: unsafe { load_symbol(library, b"caffeine_session_entity_parent\0") }?,
            set_parent: unsafe { load_symbol(library, b"caffeine_session_set_entity_parent\0") }?,
            select: unsafe { load_symbol(library, b"caffeine_session_select_entity\0") }?,
            selected: unsafe { load_symbol(library, b"caffeine_session_selected_entity\0") }?,
            position: unsafe { load_symbol(library, b"caffeine_session_entity_position\0") }?,
            set_position: unsafe {
                load_symbol(library, b"caffeine_session_set_entity_position\0")
            }?,
            rotation: unsafe { load_symbol(library, b"caffeine_session_entity_rotation\0") }?,
            set_rotation: unsafe {
                load_symbol(library, b"caffeine_session_set_entity_rotation\0")
            }?,
            scale: unsafe { load_symbol(library, b"caffeine_session_entity_scale\0") }?,
            set_scale: unsafe { load_symbol(library, b"caffeine_session_set_entity_scale\0") }?,
            world_position: unsafe {
                load_symbol(library, b"caffeine_session_entity_world_position\0")
            }?,
            set_world_position: unsafe {
                load_symbol(library, b"caffeine_session_set_entity_world_position\0")
            }?,
            render: unsafe { load_symbol(library, b"caffeine_session_render\0") }?,
            render_begin: unsafe { load_symbol(library, b"caffeine_session_render_begin\0") }?,
            render_poll: unsafe { load_symbol(library, b"caffeine_session_render_poll\0") }?,
            snapshot: unsafe { load_symbol(library, b"caffeine_session_snapshot\0") }?,
            free_bytes: unsafe { load_symbol(library, b"caffeine_session_free\0") }?,
            restore: unsafe { load_symbol(library, b"caffeine_session_restore\0") }?,
            component_type_count: unsafe {
                load_symbol(library, b"caffeine_component_type_count\0")
            }?,
            component_type_at: unsafe { load_symbol(library, b"caffeine_component_type_at\0") }?,
            component_type_field_at: unsafe {
                load_symbol(library, b"caffeine_component_type_field_at\0")
            }?,
            component_count: unsafe {
                load_symbol(library, b"caffeine_session_entity_component_count\0")
            }?,
            component_at: unsafe {
                load_symbol(library, b"caffeine_session_entity_component_at\0")
            }?,
            component_field_get: unsafe {
                load_symbol(library, b"caffeine_session_entity_component_field_get\0")
            }?,
            component_field_set: unsafe {
                load_symbol(library, b"caffeine_session_entity_component_field_set\0")
            }?,
            add_component: unsafe { load_symbol(library, b"caffeine_session_add_component\0") }?,
            remove_component: unsafe {
                load_symbol(library, b"caffeine_session_remove_component\0")
            }?,
            set_camera: unsafe { load_symbol(library, b"caffeine_session_set_camera\0") }?,
            get_camera: unsafe { load_symbol(library, b"caffeine_session_get_camera\0") }?,
            set_viewport_settings: unsafe {
                load_symbol(library, b"caffeine_session_set_viewport_settings\0")
            }?,
            get_viewport_settings: unsafe {
                load_symbol(library, b"caffeine_session_get_viewport_settings\0")
            }?,
            set_asset_root: unsafe { load_symbol(library, b"caffeine_session_set_asset_root\0") }?,
            refresh_assets: unsafe { load_symbol(library, b"caffeine_session_refresh_assets\0") }?,
            asset_count: unsafe { load_symbol(library, b"caffeine_session_asset_count\0") }?,
            asset_at: unsafe { load_symbol(library, b"caffeine_session_asset_at\0") }?,
            tick: unsafe { load_symbol(library, b"caffeine_session_tick\0") }?,
            frame_stats: unsafe { load_symbol(library, b"caffeine_session_frame_stats\0") }?,
            material_default: unsafe { load_symbol(library, b"caffeine_material_default\0") }?,
            material_load: unsafe { load_symbol(library, b"caffeine_material_load\0") }?,
            material_save: unsafe { load_symbol(library, b"caffeine_material_save\0") }?,
            material_preset_count: unsafe {
                load_symbol(library, b"caffeine_material_preset_count\0")
            }?,
            material_preset_at: unsafe { load_symbol(library, b"caffeine_material_preset_at\0") }?,
            apply_material: unsafe { load_symbol(library, b"caffeine_session_apply_material\0") }?,
            entity_material: unsafe {
                load_symbol(library, b"caffeine_session_entity_material\0")
            }?,
            material_preview: unsafe {
                load_symbol(library, b"caffeine_session_render_material_preview\0")
            }?,
            asset_path: unsafe { load_symbol(library, b"caffeine_session_asset_path\0") }?,
            asset_project_path: unsafe {
                load_symbol(library, b"caffeine_session_asset_project_path\0")
            }?,
            resolve_path: unsafe { load_symbol(library, b"caffeine_session_resolve_path\0") }?,
            generate_map: unsafe { load_symbol(library, b"caffeine_texture_generate_map\0") }?,
            pack_maps: unsafe { load_symbol(library, b"caffeine_texture_pack_maps\0") }?,
            save_rgba: unsafe { load_symbol(library, b"caffeine_texture_save_rgba\0") }?,
            save_rgba16: unsafe { load_symbol(library, b"caffeine_texture_save_rgba16\0") }?,
            load_rgba: unsafe { load_symbol(library, b"caffeine_texture_load_rgba\0") }?,
            load_thumbnail: unsafe { load_symbol(library, b"caffeine_texture_load_thumbnail\0") }?,
            bake_ao: unsafe { load_symbol(library, b"caffeine_session_bake_ao\0") }?,
            pick_uv: unsafe { load_symbol(library, b"caffeine_session_pick_uv\0") }?,
            bake_mesh_map: unsafe { load_symbol(library, b"caffeine_session_bake_mesh_map\0") }?,
            material_preview_ex: unsafe {
                load_symbol(library, b"caffeine_session_render_material_preview_ex\0")
            }?,
            material_preview_begin: unsafe {
                load_symbol(library, b"caffeine_session_render_material_preview_begin\0")
            }?,
            material_preview_poll: unsafe {
                load_symbol(library, b"caffeine_session_render_material_preview_poll\0")
            }?,
            preview_light: unsafe {
                load_symbol(library, b"caffeine_session_set_material_preview_light\0")
            },
            dryad_available: unsafe { load_symbol(library, b"caffeine_dryad_available\0") },
            run_dryad: unsafe { load_symbol(library, b"caffeine_session_run_dryad\0") },
        };
        let session = unsafe { create() };
        if session.is_null() {
            return None;
        }
        Some(Self { session, functions })
    }

    pub fn component_types(&self) -> Vec<ComponentType> {
        let count = unsafe { (self.functions.component_type_count)() };
        let mut types = Vec::new();
        for index in 0..count {
            let mut raw = RawComponentTypeInfo {
                id: 0,
                name: [0; 64],
                field_count: 0,
            };
            if result(unsafe { (self.functions.component_type_at)(index, &mut raw) }).is_err() {
                continue;
            }
            let mut fields = Vec::new();
            for field in 0..raw.field_count {
                let mut info = RawFieldInfo {
                    name: [0; 64],
                    kind: 0,
                    min_value: 0.0,
                    max_value: 0.0,
                    step: 0.0,
                    enum_options: [0; 256],
                };
                if result(unsafe {
                    (self.functions.component_type_field_at)(raw.id, field, &mut info)
                })
                .is_err()
                {
                    continue;
                }
                fields.push(FieldInfo {
                    name: c_char_array(&info.name),
                    kind: field_kind(info.kind),
                    min: info.min_value,
                    max: info.max_value,
                    step: info.step,
                    options: c_char_array(&info.enum_options)
                        .split('|')
                        .filter(|option| !option.is_empty())
                        .map(str::to_owned)
                        .collect(),
                });
            }
            types.push(ComponentType {
                id: raw.id,
                name: c_char_array(&raw.name),
                fields,
            });
        }
        types
    }

    pub fn entity_components(&self, entity: u32) -> Vec<ComponentInfo> {
        let count = unsafe { (self.functions.component_count)(self.session, entity) };
        let mut components = Vec::new();
        for index in 0..count {
            let mut raw = RawComponentInfo {
                type_id: 0,
                name: [0; 64],
                field_count: 0,
            };
            if result(unsafe {
                (self.functions.component_at)(self.session, entity, index, &mut raw)
            })
            .is_ok()
            {
                components.push(ComponentInfo {
                    type_id: raw.type_id,
                    name: c_char_array(&raw.name),
                    field_count: raw.field_count,
                });
            }
        }
        components
    }

    pub fn component_values(&self, entity: u32, component: u32) -> Vec<FieldValue> {
        let mut values = Vec::new();
        let Some(info) = self
            .entity_components(entity)
            .get(component as usize)
            .cloned()
        else {
            return values;
        };
        for field in 0..info.field_count {
            let mut raw = RawFieldValue {
                kind: 0,
                vec: [0.0; 4],
                integer: 0,
                boolean: 0,
                text: [0; 256],
            };
            if result(unsafe {
                (self.functions.component_field_get)(
                    self.session,
                    entity,
                    component,
                    field,
                    &mut raw,
                )
            })
            .is_ok()
            {
                values.push(field_value(&raw));
            }
        }
        values
    }

    pub fn entity_component_data(&self, entity: u32) -> Vec<ComponentData> {
        self.entity_components(entity)
            .into_iter()
            .enumerate()
            .map(|(index, info)| {
                let values = self.component_values(entity, index as u32);
                ComponentData { info, values }
            })
            .collect()
    }

    pub fn set_component_field(
        &mut self,
        entity: u32,
        component: u32,
        field: u32,
        value: &FieldValue,
    ) -> EngineResult<()> {
        let raw = raw_field_value(value);
        result(unsafe {
            (self.functions.component_field_set)(self.session, entity, component, field, &raw)
        })
    }

    pub fn add_component(&mut self, entity: u32, type_id: u32) -> EngineResult<()> {
        result(unsafe { (self.functions.add_component)(self.session, entity, type_id) })
    }

    pub fn remove_component(&mut self, entity: u32, type_id: u32) -> EngineResult<()> {
        result(unsafe { (self.functions.remove_component)(self.session, entity, type_id) })
    }

    pub fn snapshot(&self) -> EngineResult<Snapshot> {
        let mut data: *mut u8 = std::ptr::null_mut();
        let mut size = 0usize;
        let mut dirty = 0i32;
        result(unsafe {
            (self.functions.snapshot)(self.session, &mut data, &mut size, &mut dirty)
        })?;
        let bytes = if data.is_null() || size == 0 {
            Vec::new()
        } else {
            unsafe { std::slice::from_raw_parts(data, size) }.to_vec()
        };
        if !data.is_null() {
            unsafe { (self.functions.free_bytes)(data.cast()) };
        }
        Ok(Snapshot {
            data: bytes,
            dirty: dirty != 0,
        })
    }

    pub fn restore(&mut self, snapshot: &Snapshot) -> EngineResult<()> {
        result(unsafe {
            (self.functions.restore)(
                self.session,
                snapshot.data.as_ptr(),
                snapshot.data.len(),
                i32::from(snapshot.dirty),
            )
        })
    }

    pub fn capi_version(&self) -> i32 {
        unsafe { (self.functions.capi_version)() }
    }

    pub fn dryad_available(&self) -> bool {
        self.functions
            .dryad_available
            .map(|available| unsafe { available() != 0 })
            .unwrap_or(false)
    }

    /// Runs `onCreate` plus one `onUpdate` for a Dryad script on an entity.
    pub fn run_dryad(&mut self, entity: u32, path: &str, dt: f32) -> EngineResult<()> {
        let run = self.functions.run_dryad.ok_or(EngineError::Error)?;
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { run(self.session, entity, path.as_ptr(), dt) })
    }

    pub fn load_scene(&mut self, path: &str) -> EngineResult<()> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { (self.functions.load_scene)(self.session, path.as_ptr()) })
    }

    pub fn save_scene(&mut self, path: &str) -> EngineResult<()> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { (self.functions.save_scene)(self.session, path.as_ptr()) })
    }

    pub fn scene_path(&self) -> Option<String> {
        let path = unsafe { (self.functions.scene_path)(self.session) };
        if path.is_null() {
            return None;
        }
        let path = unsafe { CStr::from_ptr(path) }
            .to_string_lossy()
            .into_owned();
        if path.is_empty() {
            None
        } else {
            Some(path)
        }
    }

    pub fn is_dirty(&self) -> bool {
        unsafe { (self.functions.is_dirty)(self.session) != 0 }
    }

    pub fn entity_count(&self) -> u32 {
        unsafe { (self.functions.entity_count)(self.session) }
    }

    pub fn entity_at(&self, index: u32) -> EngineResult<EntityInfo> {
        let mut info = RawEntityInfo {
            id: ENTITY_NONE,
            name: [0; 64],
        };
        result(unsafe { (self.functions.entity_at)(self.session, index, &mut info) })?;
        let name = unsafe { CStr::from_ptr(info.name.as_ptr()) }
            .to_string_lossy()
            .into_owned();
        Ok(EntityInfo { id: info.id, name })
    }

    pub fn entity_name(&self, entity: u32) -> EngineResult<String> {
        let mut buffer = [0 as c_char; 64];
        result(unsafe {
            (self.functions.entity_name)(self.session, entity, buffer.as_mut_ptr(), buffer.len())
        })?;
        Ok(unsafe { CStr::from_ptr(buffer.as_ptr()) }
            .to_string_lossy()
            .into_owned())
    }

    pub fn create_entity(&mut self, name: &str) -> EngineResult<u32> {
        let name = CString::new(name).map_err(|_| EngineError::InvalidArgument)?;
        let id = unsafe { (self.functions.create_entity)(self.session, name.as_ptr()) };
        if id == ENTITY_NONE {
            Err(EngineError::Error)
        } else {
            Ok(id)
        }
    }

    pub fn destroy_entity(&mut self, entity: u32) -> EngineResult<()> {
        result(unsafe { (self.functions.destroy_entity)(self.session, entity) })
    }

    pub fn set_entity_name(&mut self, entity: u32, name: &str) -> EngineResult<()> {
        let name = CString::new(name).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { (self.functions.set_name)(self.session, entity, name.as_ptr()) })
    }

    pub fn entity_parent(&self, entity: u32) -> EngineResult<Option<u32>> {
        let mut parent = ENTITY_NONE;
        result(unsafe { (self.functions.parent)(self.session, entity, &mut parent) })?;
        Ok((parent != ENTITY_NONE).then_some(parent))
    }

    pub fn set_entity_parent(&mut self, entity: u32, parent: Option<u32>) -> EngineResult<()> {
        result(unsafe {
            (self.functions.set_parent)(self.session, entity, parent.unwrap_or(ENTITY_NONE))
        })
    }

    pub fn select_entity(&mut self, entity: u32) -> EngineResult<()> {
        result(unsafe { (self.functions.select)(self.session, entity) })
    }

    pub fn selected_entity(&self) -> u32 {
        unsafe { (self.functions.selected)(self.session) }
    }

    pub fn position(&self, entity: u32) -> EngineResult<[f32; 3]> {
        read_vec3(|out| unsafe { (self.functions.position)(self.session, entity, out) })
    }

    pub fn set_position(&mut self, entity: u32, position: [f32; 3]) -> EngineResult<()> {
        result(unsafe { (self.functions.set_position)(self.session, entity, to_raw(position)) })
    }

    pub fn rotation(&self, entity: u32) -> EngineResult<[f32; 3]> {
        read_vec3(|out| unsafe { (self.functions.rotation)(self.session, entity, out) })
    }

    pub fn set_rotation(&mut self, entity: u32, euler_degrees: [f32; 3]) -> EngineResult<()> {
        result(unsafe {
            (self.functions.set_rotation)(self.session, entity, to_raw(euler_degrees))
        })
    }

    pub fn scale(&self, entity: u32) -> EngineResult<[f32; 3]> {
        read_vec3(|out| unsafe { (self.functions.scale)(self.session, entity, out) })
    }

    pub fn set_scale(&mut self, entity: u32, scale: [f32; 3]) -> EngineResult<()> {
        result(unsafe { (self.functions.set_scale)(self.session, entity, to_raw(scale)) })
    }

    /// Resolved position after every parent transform in the hierarchy.
    pub fn world_position(&self, entity: u32) -> EngineResult<[f32; 3]> {
        read_vec3(|out| unsafe { (self.functions.world_position)(self.session, entity, out) })
    }

    /// Sets a scene-space position and lets the engine derive the local transform.
    pub fn set_world_position(&mut self, entity: u32, position: [f32; 3]) -> EngineResult<()> {
        result(unsafe {
            (self.functions.set_world_position)(self.session, entity, to_raw(position))
        })
    }

    pub fn camera(&self) -> EngineResult<Camera> {
        let mut camera = RawCamera::default();
        result(unsafe { (self.functions.get_camera)(self.session, &mut camera) })?;
        Ok(Camera {
            yaw: camera.yaw,
            pitch: camera.pitch,
            distance: camera.distance,
            focus: [camera.focus.x, camera.focus.y, camera.focus.z],
        })
    }

    pub fn set_camera(&mut self, camera: Camera) -> EngineResult<()> {
        result(unsafe {
            (self.functions.set_camera)(
                self.session,
                RawCamera {
                    yaw: camera.yaw,
                    pitch: camera.pitch,
                    distance: camera.distance,
                    focus: RawVec3 {
                        x: camera.focus[0],
                        y: camera.focus[1],
                        z: camera.focus[2],
                    },
                },
            )
        })
    }

    pub fn viewport_settings(&self) -> EngineResult<ViewportSettings> {
        let mut raw = RawViewportSettings::default();
        result(unsafe { (self.functions.get_viewport_settings)(self.session, &mut raw) })?;
        Ok(raw.into())
    }

    pub fn set_viewport_settings(&mut self, settings: ViewportSettings) -> EngineResult<()> {
        result(unsafe { (self.functions.set_viewport_settings)(self.session, settings.into()) })
    }

    pub fn render_begin(&mut self, width: u32, height: u32) -> EngineResult<()> {
        result(unsafe { (self.functions.render_begin)(self.session, width, height) })
    }

    pub fn render_poll(&mut self, pixels: &mut [u8]) -> EngineResult<RenderState> {
        let state = unsafe {
            (self.functions.render_poll)(self.session, pixels.as_mut_ptr(), pixels.len())
        };
        match state {
            0 => Ok(RenderState::Idle),
            1 => Ok(RenderState::Pending),
            2 => Ok(RenderState::Ready),
            _ => Ok(RenderState::Failed),
        }
    }

    pub fn render(&mut self, width: u32, height: u32) -> EngineResult<Vec<u8>> {
        let size = width as usize * height as usize * 4;
        let mut pixels = vec![0u8; size];
        result(unsafe {
            (self.functions.render)(
                self.session,
                width,
                height,
                pixels.as_mut_ptr(),
                pixels.len(),
            )
        })?;
        Ok(pixels)
    }

    pub fn set_asset_root(&mut self, path: &str) -> EngineResult<()> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { (self.functions.set_asset_root)(self.session, path.as_ptr()) })
    }

    pub fn refresh_assets(&mut self) -> EngineResult<()> {
        result(unsafe { (self.functions.refresh_assets)(self.session) })
    }

    pub fn asset_count(&self) -> u32 {
        unsafe { (self.functions.asset_count)(self.session) }
    }

    pub fn asset_at(&self, index: u32) -> EngineResult<AssetInfo> {
        let mut info = RawAssetInfo {
            name: [0; 128],
            relative_path: [0; 512],
            kind: [0; 32],
            size_bytes: 0,
            is_directory: 0,
        };
        result(unsafe { (self.functions.asset_at)(self.session, index, &mut info) })?;
        Ok(AssetInfo {
            name: c_char_array(&info.name),
            relative_path: c_char_array(&info.relative_path),
            kind: c_char_array(&info.kind),
            size_bytes: info.size_bytes,
            is_directory: info.is_directory != 0,
        })
    }

    pub fn assets(&self) -> Vec<AssetInfo> {
        (0..self.asset_count())
            .filter_map(|index| self.asset_at(index).ok())
            .collect()
    }

    pub fn asset_path(&self, relative: &str) -> EngineResult<String> {
        let relative = CString::new(relative).map_err(|_| EngineError::InvalidArgument)?;
        read_string(512, |buffer, size| unsafe {
            (self.functions.asset_path)(self.session, relative.as_ptr(), buffer, size)
        })
    }

    pub fn asset_project_path(&self, relative: &str) -> EngineResult<String> {
        let relative = CString::new(relative).map_err(|_| EngineError::InvalidArgument)?;
        read_string(512, |buffer, size| unsafe {
            (self.functions.asset_project_path)(self.session, relative.as_ptr(), buffer, size)
        })
    }

    pub fn resolve_path(&self, path: &str) -> EngineResult<String> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        read_string(512, |buffer, size| unsafe {
            (self.functions.resolve_path)(self.session, path.as_ptr(), buffer, size)
        })
    }

    pub fn new_material(&self) -> EngineResult<Material> {
        let mut raw = RawMaterial::default();
        result(unsafe { (self.functions.material_default)(&mut raw) })?;
        Ok(Material::from(&raw))
    }

    pub fn material_presets(&self) -> Vec<MaterialPreset> {
        (0..unsafe { (self.functions.material_preset_count)() })
            .filter_map(|index| self.material_preset_at(index).ok())
            .collect()
    }

    pub fn material_preset_at(&self, index: u32) -> EngineResult<MaterialPreset> {
        let mut info = RawMaterialPresetInfo::default();
        let mut raw = RawMaterial::default();
        result(unsafe { (self.functions.material_preset_at)(index, &mut info, &mut raw) })?;
        Ok(MaterialPreset {
            category: material::string_from_chars(&info.category),
            name: material::string_from_chars(&info.name),
            material: Material::from(&raw),
        })
    }

    pub fn load_material(&self, path: &str) -> EngineResult<Material> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        let mut raw = RawMaterial::default();
        result(unsafe { (self.functions.material_load)(path.as_ptr(), &mut raw) })?;
        Ok(Material::from(&raw))
    }

    pub fn save_material(&self, path: &str, material: &Material) -> EngineResult<()> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        let raw = RawMaterial::from(material);
        result(unsafe { (self.functions.material_save)(path.as_ptr(), &raw) })
    }

    pub fn apply_material(&mut self, entity: u32, path: &str) -> EngineResult<()> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { (self.functions.apply_material)(self.session, entity, path.as_ptr()) })
    }

    pub fn entity_material(&self, entity: u32) -> EngineResult<String> {
        read_string(512, |buffer, size| unsafe {
            (self.functions.entity_material)(self.session, entity, buffer, size)
        })
    }

    pub fn render_material_preview(
        &mut self,
        material: &Material,
        size: u32,
        yaw_degrees: f32,
        pitch_degrees: f32,
    ) -> EngineResult<Vec<u8>> {
        let size = size.clamp(32, 1024);
        let mut pixels = vec![0u8; size as usize * size as usize * 4];
        let raw = RawMaterial::from(material);
        result(unsafe {
            (self.functions.material_preview)(
                self.session,
                &raw,
                size,
                yaw_degrees,
                pitch_degrees,
                pixels.as_mut_ptr(),
                pixels.len(),
            )
        })?;
        Ok(pixels)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn render_material_preview_ex(
        &mut self,
        material: &Material,
        size: u32,
        yaw_degrees: f32,
        pitch_degrees: f32,
        shape: PreviewShape,
        environment: &str,
        exposure: f32,
    ) -> EngineResult<Vec<u8>> {
        let size = size.clamp(32, 1024);
        let mut pixels = vec![0u8; size as usize * size as usize * 4];
        let raw = RawMaterial::from(material);
        let environment = CString::new(environment).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe {
            (self.functions.material_preview_ex)(
                self.session,
                &raw,
                size,
                yaw_degrees,
                pitch_degrees,
                shape as i32,
                environment.as_ptr(),
                exposure,
                pixels.as_mut_ptr(),
                pixels.len(),
            )
        })?;
        Ok(pixels)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn begin_material_preview(
        &mut self,
        material: &Material,
        size: u32,
        yaw_degrees: f32,
        pitch_degrees: f32,
        shape: PreviewShape,
        environment: &str,
        exposure: f32,
    ) -> EngineResult<()> {
        let size = size.clamp(32, 1024);
        let raw = RawMaterial::from(material);
        let environment = CString::new(environment).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe {
            (self.functions.material_preview_begin)(
                self.session,
                &raw,
                size,
                yaw_degrees,
                pitch_degrees,
                shape as i32,
                environment.as_ptr(),
                exposure,
            )
        })
    }

    /// Sun used by later material previews (older libraries without the symbol ignore it).
    pub fn set_material_preview_light(
        &mut self,
        yaw_degrees: f32,
        elevation_degrees: f32,
        intensity: f32,
        shadows: bool,
    ) {
        if let Some(set) = self.functions.preview_light {
            unsafe {
                set(
                    self.session,
                    yaw_degrees,
                    elevation_degrees,
                    intensity,
                    i32::from(shadows),
                );
            }
        }
    }

    pub fn poll_material_preview(&mut self, pixels: &mut [u8]) -> EngineResult<RenderState> {
        let state = unsafe {
            (self.functions.material_preview_poll)(self.session, pixels.as_mut_ptr(), pixels.len())
        };
        Ok(match state {
            0 => RenderState::Idle,
            1 => RenderState::Pending,
            2 => RenderState::Ready,
            _ => RenderState::Failed,
        })
    }

    pub fn generate_texture_map(
        &self,
        source: &str,
        output: &str,
        kind: TextureMapKind,
        options: TextureMapOptions,
    ) -> EngineResult<()> {
        let source = CString::new(source).map_err(|_| EngineError::InvalidArgument)?;
        let output = CString::new(output).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe {
            (self.functions.generate_map)(
                source.as_ptr(),
                output.as_ptr(),
                kind.raw(),
                options.strength,
                i32::from(options.directx),
                i32::from(options.wrap),
                options.min_value,
                options.max_value,
                i32::from(options.invert),
                options.radius,
            )
        })
    }

    pub fn pack_texture_maps(
        &self,
        ao: Option<&str>,
        roughness: Option<&str>,
        metallic: Option<&str>,
        output: &str,
        kind: TexturePackKind,
    ) -> EngineResult<()> {
        let ao = ao
            .map(CString::new)
            .transpose()
            .map_err(|_| EngineError::InvalidArgument)?;
        let roughness = roughness
            .map(CString::new)
            .transpose()
            .map_err(|_| EngineError::InvalidArgument)?;
        let metallic = metallic
            .map(CString::new)
            .transpose()
            .map_err(|_| EngineError::InvalidArgument)?;
        let output = CString::new(output).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe {
            (self.functions.pack_maps)(
                ao.as_ref().map_or(std::ptr::null(), |path| path.as_ptr()),
                roughness
                    .as_ref()
                    .map_or(std::ptr::null(), |path| path.as_ptr()),
                metallic
                    .as_ref()
                    .map_or(std::ptr::null(), |path| path.as_ptr()),
                output.as_ptr(),
                kind.raw(),
            )
        })
    }

    pub fn save_texture_rgba(
        &self,
        path: &str,
        width: u32,
        height: u32,
        pixels: &[u8],
    ) -> EngineResult<()> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe {
            (self.functions.save_rgba)(path.as_ptr(), width, height, pixels.as_ptr(), pixels.len())
        })
    }

    pub fn save_texture_rgba16(
        &self,
        path: &str,
        width: u32,
        height: u32,
        pixels: &[u16],
    ) -> EngineResult<()> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe {
            (self.functions.save_rgba16)(
                path.as_ptr(),
                width,
                height,
                pixels.as_ptr(),
                pixels.len() * 2,
            )
        })
    }

    pub fn bake_mesh_map(
        &self,
        entity: u32,
        kind: MeshMapKind,
        resolution: u32,
        output_path: &str,
    ) -> EngineResult<()> {
        let output_path = CString::new(output_path).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe {
            (self.functions.bake_mesh_map)(
                self.session,
                entity,
                kind as i32,
                resolution,
                output_path.as_ptr(),
            )
        })
    }

    pub fn pick_uv(
        &self,
        entity: u32,
        x: f32,
        y: f32,
        width: f32,
        height: f32,
    ) -> EngineResult<Option<(f32, f32)>> {
        let mut u = 0.0f32;
        let mut v = 0.0f32;
        let status = unsafe {
            (self.functions.pick_uv)(self.session, entity, x, y, width, height, &mut u, &mut v)
        };
        match status {
            0 => Ok(Some((u, v))),
            3 => Ok(None),
            2 => Err(EngineError::InvalidArgument),
            _ => Err(EngineError::Error),
        }
    }

    pub fn bake_ao(
        &self,
        entity: u32,
        resolution: u32,
        rays: u32,
        max_distance: f32,
        output_directory: &str,
    ) -> EngineResult<AOBakeResult> {
        let output_directory =
            CString::new(output_directory).map_err(|_| EngineError::InvalidArgument)?;
        let mut raw = RawAOBakeResult {
            ok: 0,
            used_gpu: 0,
            covered_texels: 0,
            triangles: 0,
            orm_path: [0; 512],
            material_path: [0; 512],
            error: [0; 256],
        };
        result(unsafe {
            (self.functions.bake_ao)(
                self.session,
                entity,
                resolution,
                rays,
                max_distance,
                output_directory.as_ptr(),
                &mut raw,
            )
        })?;
        Ok(AOBakeResult {
            ok: raw.ok != 0,
            used_gpu: raw.used_gpu != 0,
            covered_texels: raw.covered_texels,
            triangles: raw.triangles,
            orm_path: material::string_from_chars(&raw.orm_path),
            material_path: material::string_from_chars(&raw.material_path),
            error: material::string_from_chars(&raw.error),
        })
    }

    pub fn load_texture_thumbnail(
        &self,
        path: &str,
        max_size: u32,
    ) -> EngineResult<(u32, u32, Vec<u8>)> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        let mut width = 0u32;
        let mut height = 0u32;
        let mut pixels: *mut u8 = std::ptr::null_mut();
        let mut bytes = 0usize;
        result(unsafe {
            (self.functions.load_thumbnail)(
                path.as_ptr(),
                max_size,
                &mut width,
                &mut height,
                &mut pixels,
                &mut bytes,
            )
        })?;
        let data = if pixels.is_null() || bytes == 0 {
            Vec::new()
        } else {
            unsafe { std::slice::from_raw_parts(pixels, bytes) }.to_vec()
        };
        if !pixels.is_null() {
            unsafe { (self.functions.free_bytes)(pixels.cast()) };
        }
        Ok((width, height, data))
    }

    pub fn load_texture_rgba(&self, path: &str) -> EngineResult<(u32, u32, Vec<u8>)> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        let mut width = 0u32;
        let mut height = 0u32;
        let mut pixels: *mut u8 = std::ptr::null_mut();
        let mut bytes = 0usize;
        result(unsafe {
            (self.functions.load_rgba)(
                path.as_ptr(),
                &mut width,
                &mut height,
                &mut pixels,
                &mut bytes,
            )
        })?;
        let data = if pixels.is_null() || bytes == 0 {
            Vec::new()
        } else {
            unsafe { std::slice::from_raw_parts(pixels, bytes) }.to_vec()
        };
        if !pixels.is_null() {
            unsafe { (self.functions.free_bytes)(pixels.cast()) };
        }
        Ok((width, height, data))
    }

    pub fn tick(&mut self, delta_time: f32) {
        unsafe { (self.functions.tick)(self.session, delta_time) }
    }

    pub fn frame_stats(&self) -> FrameStats {
        let mut stats = RawFrameStats {
            delta_time: 0.0,
            frame_index: 0,
            entity_count: 0,
        };
        unsafe { (self.functions.frame_stats)(self.session, &mut stats) };
        FrameStats {
            delta_time: stats.delta_time,
            frame_index: stats.frame_index,
            entity_count: stats.entity_count,
        }
    }
}

impl Drop for Engine {
    fn drop(&mut self) {
        unsafe { (self.functions.destroy)(self.session) };
    }
}

fn c_char_array<const N: usize>(bytes: &[c_char; N]) -> String {
    unsafe { CStr::from_ptr(bytes.as_ptr()) }
        .to_string_lossy()
        .into_owned()
}

fn field_kind(kind: i32) -> FieldKind {
    match kind {
        0 => FieldKind::F32,
        1 => FieldKind::Bool,
        2 => FieldKind::I32,
        3 => FieldKind::U32,
        4 => FieldKind::Vec2,
        5 => FieldKind::Vec3,
        6 => FieldKind::Vec4,
        7 => FieldKind::String,
        8 => FieldKind::Enum,
        _ => FieldKind::Color,
    }
}

fn field_value(raw: &RawFieldValue) -> FieldValue {
    match raw.kind {
        0 => FieldValue::F32(raw.vec[0]),
        1 => FieldValue::Bool(raw.boolean != 0),
        2 => FieldValue::I32(raw.integer),
        3 => FieldValue::U32(raw.integer as u32),
        4 => FieldValue::Vec2([raw.vec[0], raw.vec[1]]),
        5 => FieldValue::Vec3([raw.vec[0], raw.vec[1], raw.vec[2]]),
        6 => FieldValue::Vec4(raw.vec),
        7 => FieldValue::String(c_char_array(&raw.text)),
        8 => FieldValue::Enum(raw.integer),
        _ => FieldValue::Color(raw.vec),
    }
}

fn raw_field_value(value: &FieldValue) -> RawFieldValue {
    let mut raw = RawFieldValue {
        kind: 0,
        vec: [0.0; 4],
        integer: 0,
        boolean: 0,
        text: [0; 256],
    };
    match value {
        FieldValue::F32(number) => {
            raw.kind = 0;
            raw.vec[0] = *number;
        }
        FieldValue::Bool(flag) => {
            raw.kind = 1;
            raw.boolean = i32::from(*flag);
        }
        FieldValue::I32(number) => {
            raw.kind = 2;
            raw.integer = *number;
        }
        FieldValue::U32(number) => {
            raw.kind = 3;
            raw.integer = *number as i32;
        }
        FieldValue::Vec2(vector) => {
            raw.kind = 4;
            raw.vec[..2].copy_from_slice(vector);
        }
        FieldValue::Vec3(vector) => {
            raw.kind = 5;
            raw.vec[..3].copy_from_slice(vector);
        }
        FieldValue::Vec4(vector) => {
            raw.kind = 6;
            raw.vec = *vector;
        }
        FieldValue::String(text) => {
            raw.kind = 7;
            let bytes = text.as_bytes();
            let length = bytes.len().min(255);
            for (index, byte) in bytes[..length].iter().enumerate() {
                raw.text[index] = *byte as c_char;
            }
        }
        FieldValue::Enum(index) => {
            raw.kind = 8;
            raw.integer = *index;
        }
        FieldValue::Color(color) => {
            raw.kind = 9;
            raw.vec = *color;
        }
    }
    raw
}

fn to_raw(value: [f32; 3]) -> RawVec3 {
    RawVec3 {
        x: value[0],
        y: value[1],
        z: value[2],
    }
}

fn read_vec3(call: impl FnOnce(*mut RawVec3) -> i32) -> EngineResult<[f32; 3]> {
    let mut value = RawVec3::default();
    result(call(&mut value))?;
    Ok([value.x, value.y, value.z])
}

fn read_string(
    capacity: usize,
    call: impl FnOnce(*mut c_char, usize) -> i32,
) -> EngineResult<String> {
    let mut buffer = vec![0 as c_char; capacity];
    result(call(buffer.as_mut_ptr(), buffer.len()))?;
    let text = unsafe { CStr::from_ptr(buffer.as_ptr()) }
        .to_string_lossy()
        .into_owned();
    Ok(text)
}

fn result(value: i32) -> EngineResult<()> {
    match value {
        0 => Ok(()),
        2 => Err(EngineError::InvalidArgument),
        3 => Err(EngineError::NotFound),
        _ => Err(EngineError::Error),
    }
}

#[cfg(test)]
pub(crate) fn test_lock() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    LOCK.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dense_scene_snapshot_restores_position() {
        let _guard = test_lock();
        let Some(mut engine) = Engine::load_default() else {
            return;
        };
        let scene = std::path::Path::new("../assets/benchmarks/dense_outdoor.caf");
        if engine.load_scene(scene.to_str().unwrap()).is_err() {
            return;
        }
        let mut target = None;
        for index in 0..engine.entity_count() {
            let info = engine.entity_at(index).unwrap();
            let components = engine.entity_component_data(info.id);
            if let Some(component) = components
                .iter()
                .position(|component| component.info.name == "Position 3D")
            {
                target = Some((info.id, component as u32));
                break;
            }
        }
        let Some((entity, component)) = target else {
            return;
        };
        let old = engine.component_values(entity, component)[0].clone();
        let snapshot = engine.snapshot().expect("snapshot");
        engine
            .set_component_field(entity, component, 0, &FieldValue::Vec3([7.0, 8.0, 9.0]))
            .expect("set");
        engine.restore(&snapshot).expect("restore");
        let components = engine.entity_component_data(entity);
        let restored = &components[component as usize].values[0];
        assert_eq!(*restored, old);
    }

    #[test]
    fn bridge_roundtrip_when_library_is_built() {
        let _guard = test_lock();
        let Some(mut engine) = Engine::load_default() else {
            eprintln!(
                "skipping C ABI bridge test: {} is not built",
                default_library_path().display()
            );
            return;
        };

        let player = engine.create_entity("Player").expect("create entity");
        let enemy = engine.create_entity("Enemy").expect("create enemy");
        assert_eq!(engine.entity_count(), 2);

        assert_eq!(engine.entity_name(player).unwrap(), "Player");
        engine.set_entity_name(player, "Hero").unwrap();
        assert_eq!(engine.entity_name(player).unwrap(), "Hero");
        engine.select_entity(enemy).unwrap();
        assert_eq!(engine.selected_entity(), enemy);

        assert_eq!(engine.position(player).unwrap_err(), EngineError::NotFound);
        engine.set_position(player, [1.0, 2.0, 3.0]).unwrap();
        assert_eq!(engine.position(player).unwrap(), [1.0, 2.0, 3.0]);

        engine.set_rotation(player, [10.0, 20.0, 30.0]).unwrap();
        let rotation = engine.rotation(player).unwrap();
        assert!((rotation[0] - 10.0).abs() < 0.01);
        assert!((rotation[1] - 20.0).abs() < 0.01);
        assert!((rotation[2] - 30.0).abs() < 0.01);

        engine.set_scale(player, [2.0, 3.0, 4.0]).unwrap();
        assert_eq!(engine.scale(player).unwrap(), [2.0, 3.0, 4.0]);
        assert!(engine.is_dirty());

        let camera = Camera {
            yaw: 1.0,
            pitch: 0.25,
            distance: 12.0,
            focus: [1.0, 2.0, 3.0],
        };
        engine.set_camera(camera).unwrap();
        let readback = engine.camera().unwrap();
        assert!((readback.yaw - 1.0).abs() < 0.001);
        assert!((readback.pitch - 0.25).abs() < 0.001);
        assert!((readback.distance - 12.0).abs() < 0.001);
        assert_eq!(readback.focus, [1.0, 2.0, 3.0]);

        let defaults = engine.viewport_settings().unwrap();
        assert!(defaults.show_grid);
        assert_eq!(defaults.anti_aliasing, AntiAliasing::Fxaa);
        engine
            .set_viewport_settings(ViewportSettings {
                show_grid: false,
                anti_aliasing: AntiAliasing::Taa,
                render_scale: 9.0,
            })
            .unwrap();
        let clamped = engine.viewport_settings().unwrap();
        assert!(!clamped.show_grid);
        assert_eq!(clamped.anti_aliasing, AntiAliasing::Taa);
        assert!(clamped.render_scale <= 2.0);
        engine.set_viewport_settings(defaults).unwrap();

        let path = std::env::temp_dir().join("caffeine_capi_rust_roundtrip.caf");
        engine.save_scene(path.to_str().unwrap()).unwrap();
        assert!(!engine.is_dirty());
        assert_eq!(engine.scene_path().as_deref(), path.to_str());

        let first = engine.entity_at(0).unwrap();
        assert!(first.id == player || first.id == enemy);
        assert_eq!(engine.entity_at(99).unwrap_err(), EngineError::NotFound);

        if let Ok(pixels) = engine.render(320, 180) {
            assert_eq!(pixels.len(), 320 * 180 * 4);
        }

        let types = engine.component_types();
        let camera = types
            .iter()
            .find(|component| component.name == "Camera 3D")
            .cloned()
            .expect("Camera 3D schema");
        let fov_field = camera
            .fields
            .iter()
            .position(|field| field.name == "Field of view")
            .expect("fov field");
        engine.add_component(player, camera.id).expect("add camera");
        let components = engine.entity_components(player);
        let camera_index = components
            .iter()
            .position(|component| component.type_id == camera.id)
            .expect("camera component");
        let values = engine.component_values(player, camera_index as u32);
        assert!(matches!(values[fov_field], FieldValue::F32(_)));
        engine
            .set_component_field(
                player,
                camera_index as u32,
                fov_field as u32,
                &FieldValue::F32(90.0),
            )
            .expect("set fov");
        let values = engine.component_values(player, camera_index as u32);
        assert_eq!(values[fov_field], FieldValue::F32(90.0));
        engine
            .remove_component(player, camera.id)
            .expect("remove camera");
        assert!(engine
            .entity_components(player)
            .iter()
            .all(|component| component.type_id != camera.id));

        let snapshot = engine.snapshot().expect("snapshot");
        let count_before_restore = engine.entity_count();
        engine.set_entity_name(enemy, "Changed").unwrap();
        engine.create_entity("Temporary").unwrap();
        engine.restore(&snapshot).expect("restore");
        assert_eq!(engine.entity_count(), count_before_restore);
        assert!(engine
            .entity_at(0)
            .map(|info| info.name != "Temporary")
            .unwrap_or(true));
        let enemy_after_restore = engine
            .entity_count()
            .gt(&0)
            .then(|| engine.entity_at(0).unwrap().name)
            .unwrap();
        assert_ne!(enemy_after_restore, "Temporary");
        let _ = enemy;

        let mut async_pixels = vec![0u8; 160 * 90 * 4];
        if engine.render_begin(160, 90).is_ok() {
            let mut ready = false;
            for _ in 0..200 {
                match engine.render_poll(&mut async_pixels) {
                    Ok(RenderState::Ready) => {
                        ready = true;
                        break;
                    }
                    Ok(RenderState::Pending) => {
                        std::thread::sleep(std::time::Duration::from_millis(5));
                    }
                    _ => break,
                }
            }
            assert!(ready, "async render should complete");
            assert!(async_pixels.iter().any(|byte| *byte != 0));
        }
        engine.set_entity_parent(enemy, Some(player)).unwrap();
        assert_eq!(engine.entity_parent(enemy).unwrap(), Some(player));
        assert_eq!(engine.entity_parent(player).unwrap(), None);
        assert!(engine.set_entity_parent(player, Some(enemy)).is_err());
        engine.set_entity_parent(enemy, None).unwrap();
        assert_eq!(engine.entity_parent(enemy).unwrap(), None);

        drop(engine);

        let mut engine = Engine::load_default().expect("reload");
        assert_eq!(engine.entity_count(), 0);
        let player = engine.create_entity("Player").expect("create player");
        engine.destroy_entity(player).unwrap();
        assert_eq!(engine.entity_count(), 0);
        assert_eq!(
            engine.entity_name(player).unwrap_err(),
            EngineError::NotFound
        );
        let _ = std::fs::remove_file(path);
    }
}

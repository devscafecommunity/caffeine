//! Safe Rust bindings to the Caffeine C ABI.
//!
//! This crate is the only place that knows about `libcaffeine-capi`. The UI
//! crates consume a typed session and never touch function pointers. When the
//! shared library is not built, callers can fall back to fixtures.

use std::ffi::{c_char, c_void, CStr, CString};
use std::path::{Path, PathBuf};

use libloading::Library;

mod animation;
mod material;
mod terrain;
mod water;
mod world;
pub use animation::{
    ConditionOp, PhaseShiftCondition, PhaseShiftKind, PhaseShiftParameter, PhaseShiftSnapshot,
    PhaseShiftState, PhaseShiftTransition, Rig2dBinding, Rig2dBone, Rig2dParameter, Rig2dSnapshot,
    Rig2dVertex, RigChannel,
};
pub use material::{Material, MaterialAlphaMode, MaterialPreset};
use material::{RawMaterial, RawMaterialPresetInfo};
use terrain::RawTerrain;
pub use terrain::{ScatterClass, ScatterInstance, ScatterResult, Terrain, TerrainMaskInfo};
use water::RawWater;
pub use water::{Water, WaterWave, MAX_WATER_WAVES};
pub use world::{
    WorldBuild, WorldBuildDesc, WorldClass, WorldInstance, WorldMask, WorldRecipe, WorldRecipeKind,
    WorldWaterBody,
};

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
    /// Render the lights' shadow maps in the viewport (off by default, as in the editor; C ABI v14).
    pub shadows: bool,
}

impl Default for ViewportSettings {
    fn default() -> Self {
        Self {
            show_grid: true,
            anti_aliasing: AntiAliasing::Fxaa,
            render_scale: 1.5,
            shadows: false,
        }
    }
}

/// The Micromesh hierarchy of a mesh as bytes (the `.micromesh` section) with a summary.
#[derive(Debug, Clone, PartialEq)]
pub struct MicromeshBake {
    pub bytes: Vec<u8>,
    pub source_triangles: u32,
    pub clusters: u32,
    pub levels: u32,
    pub root_clusters: u32,
    pub max_cluster_triangles: u32,
    pub max_error: f32,
}

#[repr(C)]
#[derive(Default)]
struct RawMicromeshStats {
    source_triangles: u32,
    clusters: u32,
    levels: u32,
    root_clusters: u32,
    max_cluster_triangles: u32,
    max_error: f32,
}

/// What the last rendered frame drew and cost (times are CPU-side), and GPU capabilities.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct RenderStats {
    pub triangles: u64,
    pub meshes: u32,
    pub frustum_culled: u32,
    pub draw_calls: u32,
    pub instanced_draw_calls: u32,
    pub gather_ms: f32,
    pub shadows_ms: f32,
    pub opaque_ms: f32,
    pub translucent_ms: f32,
    pub total_ms: f32,
    pub indirect_draw: bool,
    pub graphics_storage_buffers: bool,
    pub compute_storage_write: bool,
    pub mesh_shaders: bool,
    /// Water surfaces whose waves run in the vertex shader (C ABI v13).
    pub gpu_water_draws: u32,
    /// Meshes drawn as a GPU-selected Micromesh cut (C ABI v14).
    pub micromesh_draws: u32,
    /// Caster submissions into the sun's shadow cascades (a mesh counts once per cascade) and the casters
    /// skipped because they lie outside a cascade (C ABI v19).
    pub shadow_draws: u32,
    pub shadow_draws_culled: u32,
}

/// What Photon's voxel far field holds and what its latest update cost (C ABI v17).
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct PhotonStats {
    /// Solid objects in the triangle volume.
    pub objects: u32,
    /// Triangles voxelised (0 while over budget).
    pub triangles: u32,
    /// The scene passed the 1M-triangle cap: only the screen fill remains.
    pub over_budget: bool,
    /// The far field ran in the latest frame.
    pub far_enabled: bool,
    /// Clipmap levels rebuilt whole since the session began.
    pub full_rebuilds: u32,
    /// Dirty boxes revoxelised since the session began.
    pub region_rebuilds: u32,
    /// Levels rebuilt whole in the latest update (one bit per level).
    pub last_level_mask: u32,
    /// Dirty boxes revoxelised in the latest update.
    pub last_regions: u32,
    /// CPU time to encode the latest voxel update.
    pub last_update_ms: f32,
    /// Times the per-size targets were created again: a second view of another size makes it climb.
    pub target_rebuilds: u32,
    /// Frames the far field was left as it was because nothing it depends on changed.
    pub far_skipped: u32,
    /// Frames it was skipped because the camera only turned in a calm scene.
    pub far_half_rate: u32,
}

#[repr(C)]
#[derive(Default)]
struct RawPhotonStats {
    objects: u32,
    triangles: u32,
    over_budget: u32,
    far_enabled: u32,
    full_rebuilds: u32,
    region_rebuilds: u32,
    last_level_mask: u32,
    last_regions: u32,
    last_update_ms: f32,
    target_rebuilds: u32,
    far_skipped: u32,
    far_half_rate: u32,
}

/// The most logical CPU cores [`SystemMetrics`] reports.
pub const MAX_CPU_CORES: usize = 64;

/// The machine as the profiler's graphs show it (C ABI v19). Percentages cover the time since the previous call.
#[derive(Debug, Clone, PartialEq)]
pub struct SystemMetrics {
    /// Busy share of each logical core, 0..100 (empty where the platform cannot say).
    pub cpu_cores: Vec<f32>,
    /// The whole machine, 0..100.
    pub cpu_total: f32,
    /// This process, in percent of one core (can exceed 100).
    pub process_cpu: f32,
    /// Resident memory of this process, MiB.
    pub ram_process_mib: Option<f32>,
    /// Memory in use on the whole machine, and its size, MiB.
    pub ram_used_mib: Option<f32>,
    pub ram_total_mib: Option<f32>,
    /// GPU busy share, 0..100, and video memory in use and its size (MiB); `None` when unavailable.
    pub gpu_busy: Option<f32>,
    pub vram_used_mib: Option<f32>,
    pub vram_total_mib: Option<f32>,
}

#[repr(C)]
struct RawSystemMetrics {
    cpu_core_count: u32,
    cpu_core_percent: [f32; MAX_CPU_CORES],
    cpu_total_percent: f32,
    process_cpu_percent: f32,
    ram_process_mib: f32,
    ram_used_mib: f32,
    ram_total_mib: f32,
    gpu_busy_percent: f32,
    vram_used_mib: f32,
    vram_total_mib: f32,
}

/// The weather's moving parts at the latest frame (C ABI v19): the lightning lighting the scene and the wind's surge.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct WeatherState {
    /// 0 none, about 1 at a full return stroke (more for a close strike).
    pub flash: f32,
    /// 0..1: how visible the bolt in the sky is.
    pub bolt: f32,
    /// The wind's strength relative to its mean (1 = mean).
    pub gust: f32,
    /// The clock the strikes and the thunder are timed on, seconds.
    pub clock_s: f32,
    /// Whether the scene has lightning running.
    pub lightning_active: bool,
    /// Of the strike being shown: metres and direction around +Y.
    pub strike_distance_m: f32,
    pub strike_azimuth_rad: f32,
}

#[repr(C)]
#[derive(Default)]
struct RawGameState {
    running: i32,
    scripts_running: i32,
    frames: u64,
    time_s: f32,
}

/// What the running game reports (C ABI v23).
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct GameState {
    pub running: bool,
    /// The script engine started: Lua and Dryad scripts run.
    pub scripts_running: bool,
    pub frames: u64,
    pub time_s: f32,
}

/// A scene view: the scene through one of its cameras instead of the editor's.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SceneView {
    /// The active camera: what the compiled game shows.
    Game,
    /// A chosen camera (see `set_preview_camera`), else the active one.
    Camera,
}

impl SceneView {
    fn raw(self) -> i32 {
        match self {
            SceneView::Game => 0,
            SceneView::Camera => 1,
        }
    }
}

#[repr(C)]
struct RawWeatherState {
    flash: f32,
    bolt: f32,
    gust: f32,
    clock_s: f32,
    lightning_active: i32,
    strike_distance_m: f32,
    strike_azimuth_rad: f32,
}

/// What the weather's sound is doing (C ABI v19). The session makes the sound itself, from noise, when the scene's
/// `Weather` component has `Audio Enabled`; without an audio device the levels still follow the weather.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct AudioState {
    /// The scene asks for the weather's sound.
    pub enabled: bool,
    /// An audio device is open.
    pub device_ready: bool,
    pub volume: f32,
    /// The loudness (0..1) each bed is heading for.
    pub rain_level: f32,
    pub wind_level: f32,
    /// The bed is running on the device.
    pub rain_playing: bool,
    pub wind_playing: bool,
    /// Claps handed to the player since the session began, and the ones that started on a device.
    pub thunder_heard: u32,
    pub thunder_played: u32,
}

#[repr(C)]
struct RawAudioState {
    enabled: i32,
    device_ready: i32,
    volume: f32,
    rain_level: f32,
    wind_level: f32,
    rain_playing: i32,
    wind_playing: i32,
    thunder_heard: u32,
    thunder_played: u32,
}

/// The scene's volumetric audio (C ABI v20): emitters travelling through the scene's acoustic obstacles.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct VolumetricState {
    /// An enabled `Volumetric Audio` component is in the scene.
    pub enabled: bool,
    /// The mixer plays on a sound device (otherwise it mixes silently so this state keeps moving).
    pub device_open: bool,
    pub voices: u32,
    pub obstacles: u32,
    pub clips_missing: u32,
    /// The room round the listener: 0 shut in .. 1 open; 0 under a roof .. 1 open sky; the reverb tail; its level.
    pub openness: f32,
    pub sky_exposure: f32,
    pub rt60_s: f32,
    pub wet: f32,
    pub echoes: u32,
    pub time_s: f32,
}

#[repr(C)]
struct RawVolumetricState {
    enabled: i32,
    device_open: i32,
    voices: u32,
    obstacles: u32,
    clips_missing: u32,
    openness: f32,
    sky_exposure: f32,
    rt60_s: f32,
    wet: f32,
    echoes: u32,
    time_s: f32,
}

/// What the mixer decided for one emitter (C ABI v20).
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct VolumetricVoice {
    pub entity: u32,
    /// 0 World, 1 Ambient, 2 Music, 3 Ui.
    pub layer: i32,
    pub loaded: bool,
    pub playing: bool,
    pub distance_m: f32,
    pub delay_s: f32,
    /// The Doppler shift: above 1 while it approaches.
    pub pitch_ratio: f32,
    pub gain_db: f32,
    pub cutoff_hz: f32,
    pub blocked: bool,
    pub diffracted: bool,
    /// Where it seems to come from, in the world.
    pub direction: [f32; 3],
}

#[repr(C)]
struct RawVolumetricVoice {
    entity: u32,
    layer: i32,
    loaded: i32,
    playing: i32,
    distance_m: f32,
    delay_s: f32,
    pitch_ratio: f32,
    gain_db: f32,
    cutoff_hz: f32,
    blocked: i32,
    diffracted: i32,
    dir_x: f32,
    dir_y: f32,
    dir_z: f32,
}

/// A clap of thunder that has reached the listener (C ABI v19). The engine makes no sound: a player does.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
#[repr(C)]
pub struct ThunderEvent {
    /// Of the strike, metres (the sound took distance / 343 s to arrive).
    pub distance_m: f32,
    /// Direction of the strike around +Y.
    pub azimuth_rad: f32,
    /// 0..1, falling with the distance.
    pub volume: f32,
    /// On the clock [`WeatherState::clock_s`] reports.
    pub arrival_s: f32,
}

/// One scope of the engine's profiler (C ABI v18), in depth-first order.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct ProfileScope {
    pub name: String,
    /// 0 for the top scopes.
    pub depth: u32,
    pub calls: u32,
    /// Milliseconds: the most recent call, the average and the worst since the last reset, and the average
    /// time not spent in child scopes.
    pub last_ms: f32,
    pub avg_ms: f32,
    pub max_ms: f32,
    pub self_ms: f32,
    /// Mean and worst of the latest 120 calls: what the scene does now (C ABI v19).
    pub recent_avg_ms: f32,
    pub recent_max_ms: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct RawProfileScope {
    name: [u8; 48],
    depth: u32,
    calls: u32,
    last_ms: f32,
    avg_ms: f32,
    max_ms: f32,
    self_ms: f32,
    recent_avg_ms: f32,
    recent_max_ms: f32,
}

/// What the last frame's Micromesh draws kept (C ABI v14). Reading it waits for the GPU.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct MicromeshFrameStats {
    pub draws: u32,
    pub clusters: u32,
    /// Triangles of the camera's cut.
    pub triangles: u64,
    pub source_triangles: u64,
    /// Views cut this frame: the camera and each shadow cascade.
    pub views: u32,
    /// Indices every view's cut took from the shared index arena, and its size.
    pub arena_used: u32,
    pub arena_capacity: u32,
    /// Cuts that did not fit in the arena and were not drawn this frame.
    pub overflowed: u32,
    /// The camera's cut was tested against last frame's depth (occlusion culling).
    pub occlusion: bool,
}

#[repr(C)]
#[derive(Default)]
struct RawMicromeshFrameStats {
    draws: u32,
    clusters: u32,
    triangles: u64,
    source_triangles: u64,
    views: u32,
    arena_used: u32,
    arena_capacity: u32,
    overflowed: u32,
    occlusion: u32,
    reserved: u32,
}

#[repr(C)]
#[derive(Default)]
struct RawRenderStats {
    triangles: u64,
    meshes: u32,
    frustum_culled: u32,
    draw_calls: u32,
    instanced_draw_calls: u32,
    gather_ms: f32,
    shadows_ms: f32,
    opaque_ms: f32,
    translucent_ms: f32,
    total_ms: f32,
    indirect_draw: i32,
    graphics_storage_buffers: i32,
    compute_storage_write: i32,
    mesh_shaders: i32,
    gpu_water_draws: u32,
    micromesh_draws: u32,
    shadow_draws: u32,
    shadow_draws_culled: u32,
}

/// What the project's asset manager made of a path (see `Engine::asset_reference`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ReferenceStatus {
    /// A file inside the project (or an engine built-in).
    Ok,
    Empty,
    /// No such file in the project, its assets folder or the engine's assets.
    Missing,
    /// A real file, but not inside the project: it has to be imported first.
    OutsideProject,
    NotAFile,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AssetReference {
    pub status: ReferenceStatus,
    /// An engine built-in rather than a project file.
    pub engine_asset: bool,
    /// The call copied the file into the project.
    pub imported: bool,
    /// Relative to the project root, `/`-separated: what scenes and `.mat` files store.
    pub project_path: String,
    /// Relative to the assets folder when the file is in it (what the picker lists and components store),
    /// else the same as `project_path`.
    pub asset_path: String,
    pub absolute_path: String,
    pub kind: String,
}

impl AssetReference {
    pub fn is_ok(&self) -> bool {
        self.status == ReferenceStatus::Ok
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
#[derive(Clone, Copy)]
struct RawTerrainBrush {
    mode: i32,
    radius_m: f32,
    strength: f32,
    spacing_m: f32,
    falloff: i32,
    rotation_degrees: f32,
    target_height_m: f32,
    pressure_to_strength: f32,
    seed: u64,
}

#[repr(C)]
#[derive(Default)]
struct RawStrokeResult {
    record: u32,
    stamps: u32,
    tiles_changed: u32,
    bytes: u64,
    has_region: i32,
    region_min_x: u32,
    region_min_z: u32,
    region_max_x: u32,
    region_max_z: u32,
}

/// What a terrain brush does under a stamp.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BrushMode {
    Raise = 0,
    Lower = 1,
    Smooth = 2,
    Flatten = 3,
    Noise = 4,
}

/// How a stamp's weight falls from its centre to its edge.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Falloff {
    Smooth = 0,
    Linear = 1,
    Sharp = 2,
    Constant = 3,
}

/// A terrain brush in real units (metres), see `caffeine_session_terrain_stroke_begin`.
#[derive(Debug, Clone, PartialEq)]
pub struct TerrainBrush {
    pub mode: BrushMode,
    pub radius_m: f32,
    /// Raise, lower, noise: metres per stamp. Smooth, flatten: 0..1 per stamp.
    pub strength: f32,
    /// Metres between stamps along the path; 0 is a quarter of the radius.
    pub spacing_m: f32,
    pub falloff: Falloff,
    pub rotation_degrees: f32,
    /// Flatten: metres; negative takes the height under the first stamp.
    pub target_height_m: f32,
    /// 0 ignores a pen's pressure, 1 follows it.
    pub pressure_to_strength: f32,
    pub seed: u64,
    /// A square grey-scale image (`size` squared values, 0..1) that shapes the stamp, if any.
    pub stamp: Option<(u32, Vec<f32>)>,
}

impl Default for TerrainBrush {
    fn default() -> Self {
        TerrainBrush {
            mode: BrushMode::Raise,
            radius_m: 6.0,
            strength: 0.25,
            spacing_m: 0.0,
            falloff: Falloff::Smooth,
            rotation_degrees: 0.0,
            target_height_m: -1.0,
            pressure_to_strength: 0.0,
            seed: 0,
            stamp: None,
        }
    }
}

/// What a finished stroke changed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct StrokeSummary {
    /// Id for `terrain_stroke_revert` / `terrain_stroke_reapply`; 0 when the stroke changed nothing.
    pub record: u32,
    pub stamps: u32,
    pub tiles_changed: u32,
    /// Memory the undo record holds.
    pub bytes: u64,
    /// Height-grid vertices to refresh: `(min_x, min_z, max_x, max_z)`.
    pub region: Option<(u32, u32, u32, u32)>,
}

#[repr(C)]
struct RawAssetReference {
    status: i32,
    engine_asset: i32,
    imported: i32,
    project_path: [c_char; 512],
    asset_path: [c_char; 512],
    absolute_path: [c_char; 512],
    kind: [c_char; 32],
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
    shadows: i32,
}

impl From<RawViewportSettings> for ViewportSettings {
    fn from(raw: RawViewportSettings) -> Self {
        Self {
            show_grid: raw.show_grid != 0,
            anti_aliasing: AntiAliasing::from_raw(raw.anti_aliasing),
            render_scale: raw.render_scale,
            shadows: raw.shadows != 0,
        }
    }
}

impl From<ViewportSettings> for RawViewportSettings {
    fn from(settings: ViewportSettings) -> Self {
        Self {
            show_grid: i32::from(settings.show_grid),
            anti_aliasing: settings.anti_aliasing.raw(),
            render_scale: settings.render_scale,
            shadows: i32::from(settings.shadows),
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
type AssetReferenceFn =
    unsafe extern "C" fn(*mut c_void, *const c_char, i32, *mut RawAssetReference) -> i32;
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
type SetTerrainFn =
    unsafe extern "C" fn(*mut c_void, u32, *const RawTerrain, *const f32, *const u8) -> i32;
type GetTerrainFn = unsafe extern "C" fn(*const c_void, u32, *mut RawTerrain) -> i32;
type StrokeBeginFn =
    unsafe extern "C" fn(*mut c_void, u32, *const RawTerrainBrush, *const f32, u32) -> i32;
type StrokePointFn = unsafe extern "C" fn(*mut c_void, f64, f64, f64, f32, *mut u32) -> i32;
type StrokeEndFn = unsafe extern "C" fn(*mut c_void, *mut RawStrokeResult) -> i32;
type StrokeRecordFn = unsafe extern "C" fn(*mut c_void, u32) -> i32;
type TerrainRaycastFn =
    unsafe extern "C" fn(*const c_void, u32, *const f64, *const f64, *mut f64) -> i32;
type TerrainOutlineFn =
    unsafe extern "C" fn(*const c_void, u32, *const f64, f32, u32, *mut f32) -> i32;
type TerrainDataLegacyFn = unsafe extern "C" fn(*const c_void, u32, *mut i32) -> i32;
type TerrainConvertNativeFn = unsafe extern "C" fn(*mut c_void, u32, *mut c_char, usize) -> i32;
type TerrainHeightFn = unsafe extern "C" fn(*const c_void, u32, f32, f32, *mut f32) -> i32;
type SetTerrainMaskFn =
    unsafe extern "C" fn(*mut c_void, u32, *const c_char, u32, u32, *const u8) -> i32;
type TerrainMaskCountFn = unsafe extern "C" fn(*const c_void, u32, *mut u32) -> i32;
type TerrainMaskAtFn =
    unsafe extern "C" fn(*const c_void, u32, u32, *mut RawTerrainMaskInfo) -> i32;
type TerrainMaskFn =
    unsafe extern "C" fn(*const c_void, u32, *const c_char, f32, f32, *mut f32) -> i32;
type TerrainMaskRectFn = unsafe extern "C" fn(
    *const c_void,
    u32,
    *const c_char,
    f32,
    f32,
    f32,
    f32,
    u32,
    u32,
    *mut u8,
) -> i32;
type TerrainNormalFn = unsafe extern "C" fn(*const c_void, u32, f32, f32, *mut f32) -> i32;
type ScatterTerrainFn = unsafe extern "C" fn(
    *const c_void,
    u32,
    u64,
    *const RawScatterClass,
    u32,
    *const c_char,
    *const f32,
    *mut RawScatterInstance,
    u32,
    *mut u32,
) -> i32;
type BiomeCheckFn = unsafe extern "C" fn(*const c_char, *mut c_char, u32) -> i32;

#[repr(C)]
#[derive(Clone, Copy)]
struct RawScatterClass {
    id: u32,
    priority: i32,
    cell_size: f32,
    radius: f32,
    density: f32,
    min_scale: f32,
    max_scale: f32,
    tilt_to_normal: f32,
    sink: f32,
    tint_jitter: f32,
    variants: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct RawScatterInstance {
    class_id: u32,
    variant: u32,
    position: [f32; 3],
    up: [f32; 3],
    rotation_degrees: [f32; 3],
    scale: f32,
    tint: [f32; 3],
    seed: u64,
}

#[repr(C)]
struct RawTerrainMaskInfo {
    name: [c_char; 32],
    resolution_x: u32,
    resolution_z: u32,
    builtin: i32,
}
type SceneCameraFn = unsafe extern "C" fn(*const c_void, i32, *mut u32, *mut f32) -> i32;
type SetPreviewCameraFn = unsafe extern "C" fn(*mut c_void, u32) -> i32;
type ViewRenderBeginFn = unsafe extern "C" fn(*mut c_void, i32, u32, u32) -> i32;
type ViewRenderPollFn = unsafe extern "C" fn(*mut c_void, i32, *mut u8, usize) -> i32;
type GameStartFn = unsafe extern "C" fn(*mut c_void) -> i32;
type GameStopFn = unsafe extern "C" fn(*mut c_void);
type GameRunningFn = unsafe extern "C" fn(*const c_void) -> i32;
type GameTickFn = unsafe extern "C" fn(*mut c_void, f32) -> i32;
type GameStateFn = unsafe extern "C" fn(*const c_void, *mut RawGameState) -> i32;
type GameKeyFn = unsafe extern "C" fn(*mut c_void, u32, i32);
type GameReleaseKeysFn = unsafe extern "C" fn(*mut c_void);
type GameMouseFn = unsafe extern "C" fn(*mut c_void, f32, f32, u32, i32);
type SetSimulatingFn = unsafe extern "C" fn(*mut c_void, i32);
type SimulatingFn = unsafe extern "C" fn(*const c_void) -> i32;
type SetWaterFn = unsafe extern "C" fn(*mut c_void, u32, *const RawWater) -> i32;
type GetWaterFn = unsafe extern "C" fn(*const c_void, u32, *mut RawWater) -> i32;
type WaterHeightFn = unsafe extern "C" fn(*const c_void, u32, f32, f32, *mut f32) -> i32;
type RenderStatsFn = unsafe extern "C" fn(*const c_void, *mut RawRenderStats) -> i32;
type PhotonStatsFn = unsafe extern "C" fn(*const c_void, *mut RawPhotonStats) -> i32;
type PhotonRebuildFn = unsafe extern "C" fn(*mut c_void) -> i32;
type ProfilerReportFn = unsafe extern "C" fn(*mut RawProfileScope, u32, *mut u32) -> i32;
type ProfilerResetFn = unsafe extern "C" fn() -> i32;
type ProfilerSetGpuFn = unsafe extern "C" fn(i32) -> i32;
type ProfilerGpuFn = unsafe extern "C" fn(*mut i32) -> i32;
type SystemMetricsFn = unsafe extern "C" fn(*mut RawSystemMetrics) -> i32;
type WeatherStateFn = unsafe extern "C" fn(*const c_void, *mut RawWeatherState) -> i32;
type VolumetricStateFn = unsafe extern "C" fn(*const c_void, *mut RawVolumetricState) -> i32;
type VolumetricVoiceFn = unsafe extern "C" fn(*const c_void, u32, *mut RawVolumetricVoice) -> i32;
type SetAudioOutputFn = unsafe extern "C" fn(*mut c_void, i32) -> i32;
type AudioDeviceCountFn = unsafe extern "C" fn() -> i32;
type AudioDeviceNameFn = unsafe extern "C" fn(i32, *mut u8, usize) -> i32;
type SetAudioDeviceFn = unsafe extern "C" fn(*mut c_void, i32) -> i32;
type VolumetricRenderFn = unsafe extern "C" fn(*mut c_void, *mut f32, u32, *mut u32) -> i32;
type AudioStateFn = unsafe extern "C" fn(*const c_void, *mut RawAudioState) -> i32;
type PollThunderFn = unsafe extern "C" fn(*mut c_void, *mut ThunderEvent, u32, *mut u32) -> i32;
type MicromeshStatsFn = unsafe extern "C" fn(*const c_void, *mut RawMicromeshFrameStats) -> i32;
type MicromeshBuildFn = unsafe extern "C" fn(
    *const f32,
    u32,
    *const u32,
    u32,
    *mut RawMicromeshStats,
    *mut *mut u8,
    *mut usize,
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
    asset_reference: Option<AssetReferenceFn>,
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
    scene_camera: Option<SceneCameraFn>,
    set_preview_camera: Option<SetPreviewCameraFn>,
    view_render_begin: Option<ViewRenderBeginFn>,
    view_render_poll: Option<ViewRenderPollFn>,
    game_start: Option<GameStartFn>,
    game_stop: Option<GameStopFn>,
    game_running: Option<GameRunningFn>,
    game_tick: Option<GameTickFn>,
    game_state: Option<GameStateFn>,
    game_key: Option<GameKeyFn>,
    game_release_keys: Option<GameReleaseKeysFn>,
    game_mouse: Option<GameMouseFn>,
    set_simulating: Option<SetSimulatingFn>,
    simulating: Option<SimulatingFn>,
    set_water: Option<SetWaterFn>,
    set_terrain: Option<SetTerrainFn>,
    terrain_raycast: Option<TerrainRaycastFn>,
    terrain_outline: Option<TerrainOutlineFn>,
    terrain_data_legacy: Option<TerrainDataLegacyFn>,
    terrain_convert_native: Option<TerrainConvertNativeFn>,
    stroke_begin: Option<StrokeBeginFn>,
    stroke_point: Option<StrokePointFn>,
    stroke_end: Option<StrokeEndFn>,
    stroke_revert: Option<StrokeRecordFn>,
    stroke_reapply: Option<StrokeRecordFn>,
    get_terrain: Option<GetTerrainFn>,
    terrain_height: Option<TerrainHeightFn>,
    set_terrain_mask: Option<SetTerrainMaskFn>,
    terrain_mask_count: Option<TerrainMaskCountFn>,
    terrain_mask_at: Option<TerrainMaskAtFn>,
    terrain_mask: Option<TerrainMaskFn>,
    terrain_mask_rect: Option<TerrainMaskRectFn>,
    terrain_normal: Option<TerrainNormalFn>,
    scatter_terrain: Option<ScatterTerrainFn>,
    biome_check: Option<BiomeCheckFn>,
    world: Option<world::WorldFunctions>,
    get_water: Option<GetWaterFn>,
    water_height: Option<WaterHeightFn>,
    phase_shift_counts: Option<animation::PhaseShiftCountsFn>,
    phase_shift_parameter: Option<animation::PhaseShiftParameterFn>,
    phase_shift_state: Option<animation::PhaseShiftStateFn>,
    phase_shift_transition: Option<animation::PhaseShiftTransitionFn>,
    phase_shift_condition: Option<animation::PhaseShiftConditionFn>,
    phase_shift_set_float: Option<animation::PhaseShiftSetFloatFn>,
    phase_shift_set_smoothing: Option<animation::PhaseShiftSetSmoothingFn>,
    rig2d_attach: Option<animation::Rig2dAttachFn>,
    rig2d_counts: Option<animation::Rig2dCountsFn>,
    rig2d_bone: Option<animation::Rig2dBoneFn>,
    rig2d_parameter: Option<animation::Rig2dParameterFn>,
    rig2d_vertex: Option<animation::Rig2dVertexFn>,
    rig2d_binding: Option<animation::Rig2dBindingFn>,
    rig2d_set_parameter: Option<animation::Rig2dSetParameterFn>,
    phase_shift_add: Option<animation::PhaseShiftAddFn>,
    phase_shift_add_parameter: Option<animation::PhaseShiftAddParameterFn>,
    phase_shift_remove_parameter: Option<animation::PhaseShiftRemoveParameterFn>,
    render_stats: Option<RenderStatsFn>,
    micromesh_stats: Option<MicromeshStatsFn>,
    photon_stats: Option<PhotonStatsFn>,
    photon_rebuild: Option<PhotonRebuildFn>,
    profiler_report: Option<ProfilerReportFn>,
    profiler_reset: Option<ProfilerResetFn>,
    profiler_set_gpu_timing: Option<ProfilerSetGpuFn>,
    profiler_gpu_timing: Option<ProfilerGpuFn>,
    system_metrics: Option<SystemMetricsFn>,
    weather_state: Option<WeatherStateFn>,
    poll_thunder: Option<PollThunderFn>,
    audio_state: Option<AudioStateFn>,
    volumetric_state: Option<VolumetricStateFn>,
    volumetric_voice: Option<VolumetricVoiceFn>,
    set_audio_output: Option<SetAudioOutputFn>,
    volumetric_render: Option<VolumetricRenderFn>,
    audio_device_count: Option<AudioDeviceCountFn>,
    audio_device_name: Option<AudioDeviceNameFn>,
    set_audio_device: Option<SetAudioDeviceFn>,
    micromesh_build: Option<MicromeshBuildFn>,
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
            // v24: an older libcaffeine-capi has no asset manager references; the editor then keeps the path as typed.
            asset_reference: unsafe { load_symbol(library, b"caffeine_session_asset_reference\0") },
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
            set_terrain: unsafe { load_symbol(library, b"caffeine_session_set_terrain\0") },
            terrain_raycast: unsafe { load_symbol(library, b"caffeine_session_terrain_raycast\0") },
            terrain_outline: unsafe {
                load_symbol(library, b"caffeine_session_terrain_brush_outline\0")
            },
            terrain_data_legacy: unsafe {
                load_symbol(library, b"caffeine_session_terrain_data_legacy\0")
            },
            terrain_convert_native: unsafe {
                load_symbol(library, b"caffeine_session_terrain_convert_native\0")
            },
            stroke_begin: unsafe {
                load_symbol(library, b"caffeine_session_terrain_stroke_begin\0")
            },
            stroke_point: unsafe {
                load_symbol(library, b"caffeine_session_terrain_stroke_point\0")
            },
            stroke_end: unsafe { load_symbol(library, b"caffeine_session_terrain_stroke_end\0") },
            stroke_revert: unsafe {
                load_symbol(library, b"caffeine_session_terrain_stroke_revert\0")
            },
            stroke_reapply: unsafe {
                load_symbol(library, b"caffeine_session_terrain_stroke_reapply\0")
            },
            get_terrain: unsafe { load_symbol(library, b"caffeine_session_terrain\0") },
            terrain_height: unsafe { load_symbol(library, b"caffeine_session_terrain_height\0") },
            set_terrain_mask: unsafe {
                load_symbol(library, b"caffeine_session_set_terrain_mask\0")
            },
            terrain_mask_count: unsafe {
                load_symbol(library, b"caffeine_session_terrain_mask_count\0")
            },
            terrain_mask_at: unsafe { load_symbol(library, b"caffeine_session_terrain_mask_at\0") },
            terrain_mask: unsafe { load_symbol(library, b"caffeine_session_terrain_mask\0") },
            terrain_mask_rect: unsafe {
                load_symbol(library, b"caffeine_session_terrain_mask_rect\0")
            },
            terrain_normal: unsafe { load_symbol(library, b"caffeine_session_terrain_normal\0") },
            scatter_terrain: unsafe { load_symbol(library, b"caffeine_session_scatter_terrain\0") },
            biome_check: unsafe { load_symbol(library, b"caffeine_biome_check\0") },
            world: world::WorldFunctions::load(library),
            scene_camera: unsafe { load_symbol(library, b"caffeine_session_scene_camera\0") },
            set_preview_camera: unsafe {
                load_symbol(library, b"caffeine_session_set_preview_camera\0")
            },
            view_render_begin: unsafe {
                load_symbol(library, b"caffeine_session_view_render_begin\0")
            },
            view_render_poll: unsafe {
                load_symbol(library, b"caffeine_session_view_render_poll\0")
            },
            game_start: unsafe { load_symbol(library, b"caffeine_session_game_start\0") },
            game_stop: unsafe { load_symbol(library, b"caffeine_session_game_stop\0") },
            game_running: unsafe { load_symbol(library, b"caffeine_session_game_running\0") },
            game_tick: unsafe { load_symbol(library, b"caffeine_session_game_tick\0") },
            game_state: unsafe { load_symbol(library, b"caffeine_session_game_state\0") },
            game_key: unsafe { load_symbol(library, b"caffeine_session_game_key\0") },
            game_release_keys: unsafe {
                load_symbol(library, b"caffeine_session_game_release_keys\0")
            },
            game_mouse: unsafe { load_symbol(library, b"caffeine_session_game_mouse\0") },
            set_simulating: unsafe { load_symbol(library, b"caffeine_session_set_simulating\0") },
            simulating: unsafe { load_symbol(library, b"caffeine_session_simulating\0") },
            set_water: unsafe { load_symbol(library, b"caffeine_session_set_water\0") },
            get_water: unsafe { load_symbol(library, b"caffeine_session_water\0") },
            water_height: unsafe { load_symbol(library, b"caffeine_session_water_height\0") },
            phase_shift_counts: unsafe {
                load_symbol(library, b"caffeine_session_phase_shift_counts\0")
            },
            phase_shift_parameter: unsafe {
                load_symbol(library, b"caffeine_session_phase_shift_parameter\0")
            },
            phase_shift_state: unsafe {
                load_symbol(library, b"caffeine_session_phase_shift_state\0")
            },
            phase_shift_transition: unsafe {
                load_symbol(library, b"caffeine_session_phase_shift_transition\0")
            },
            phase_shift_condition: unsafe {
                load_symbol(library, b"caffeine_session_phase_shift_condition\0")
            },
            phase_shift_set_float: unsafe {
                load_symbol(library, b"caffeine_session_phase_shift_set_float\0")
            },
            phase_shift_set_smoothing: unsafe {
                load_symbol(library, b"caffeine_session_phase_shift_set_smoothing\0")
            },
            rig2d_attach: unsafe { load_symbol(library, b"caffeine_session_rig2d_attach\0") },
            rig2d_counts: unsafe { load_symbol(library, b"caffeine_session_rig2d_counts\0") },
            rig2d_bone: unsafe { load_symbol(library, b"caffeine_session_rig2d_bone\0") },
            rig2d_parameter: unsafe { load_symbol(library, b"caffeine_session_rig2d_parameter\0") },
            rig2d_vertex: unsafe { load_symbol(library, b"caffeine_session_rig2d_vertex\0") },
            rig2d_binding: unsafe { load_symbol(library, b"caffeine_session_rig2d_binding\0") },
            rig2d_set_parameter: unsafe {
                load_symbol(library, b"caffeine_session_rig2d_set_parameter\0")
            },
            phase_shift_add: unsafe { load_symbol(library, b"caffeine_session_phase_shift_add\0") },
            phase_shift_add_parameter: unsafe {
                load_symbol(library, b"caffeine_session_phase_shift_add_parameter\0")
            },
            phase_shift_remove_parameter: unsafe {
                load_symbol(library, b"caffeine_session_phase_shift_remove_parameter\0")
            },
            render_stats: unsafe { load_symbol(library, b"caffeine_session_render_stats\0") },
            micromesh_stats: unsafe { load_symbol(library, b"caffeine_session_micromesh_stats\0") },
            photon_stats: unsafe { load_symbol(library, b"caffeine_session_photon_stats\0") },
            photon_rebuild: unsafe { load_symbol(library, b"caffeine_session_photon_rebuild\0") },
            profiler_report: unsafe { load_symbol(library, b"caffeine_profiler_report\0") },
            profiler_reset: unsafe { load_symbol(library, b"caffeine_profiler_reset\0") },
            profiler_set_gpu_timing: unsafe {
                load_symbol(library, b"caffeine_profiler_set_gpu_timing\0")
            },
            profiler_gpu_timing: unsafe { load_symbol(library, b"caffeine_profiler_gpu_timing\0") },
            system_metrics: unsafe { load_symbol(library, b"caffeine_system_metrics\0") },
            weather_state: unsafe { load_symbol(library, b"caffeine_session_weather_state\0") },
            poll_thunder: unsafe { load_symbol(library, b"caffeine_session_poll_thunder\0") },
            audio_state: unsafe { load_symbol(library, b"caffeine_session_audio_state\0") },
            volumetric_state: unsafe {
                load_symbol(library, b"caffeine_session_volumetric_state\0")
            },
            volumetric_voice: unsafe {
                load_symbol(library, b"caffeine_session_volumetric_voice\0")
            },
            set_audio_output: unsafe {
                load_symbol(library, b"caffeine_session_set_audio_output\0")
            },
            volumetric_render: unsafe {
                load_symbol(library, b"caffeine_session_volumetric_render\0")
            },
            audio_device_count: unsafe {
                load_symbol(library, b"caffeine_audio_output_device_count\0")
            },
            audio_device_name: unsafe {
                load_symbol(library, b"caffeine_audio_output_device_name\0")
            },
            set_audio_device: unsafe {
                load_symbol(library, b"caffeine_session_set_audio_output_device\0")
            },
            micromesh_build: unsafe { load_symbol(library, b"caffeine_micromesh_build\0") },
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

    /// Asks the project's asset manager what `path` refers to. With `import`, a real file outside the project is
    /// copied into the assets folder first and the reference names the copy. `None` when the engine library is
    /// older than the asset manager (C ABI 24).
    pub fn asset_reference(&self, path: &str, import: bool) -> Option<AssetReference> {
        let function = self.functions.asset_reference?;
        let path = CString::new(path).ok()?;
        let mut raw = RawAssetReference {
            status: 0,
            engine_asset: 0,
            imported: 0,
            project_path: [0; 512],
            asset_path: [0; 512],
            absolute_path: [0; 512],
            kind: [0; 32],
        };
        if unsafe { function(self.session, path.as_ptr(), i32::from(import), &mut raw) } != 0 {
            return None;
        }
        let status = match raw.status {
            0 => ReferenceStatus::Ok,
            1 => ReferenceStatus::Empty,
            2 => ReferenceStatus::Missing,
            3 => ReferenceStatus::OutsideProject,
            _ => ReferenceStatus::NotAFile,
        };
        Some(AssetReference {
            status,
            engine_asset: raw.engine_asset != 0,
            imported: raw.imported != 0,
            project_path: c_char_array(&raw.project_path),
            asset_path: c_char_array(&raw.asset_path),
            absolute_path: c_char_array(&raw.absolute_path),
            kind: c_char_array(&raw.kind),
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

    /// Creates or updates a terrain on an entity from generated heights (0..1, row-major) and
    /// optional splat weights (RGBA8 per texel, same grid). Needs a C ABI v8 library.
    pub fn set_terrain(
        &mut self,
        entity: u32,
        terrain: &Terrain,
        heights: &[f32],
        splat_rgba: Option<&[u8]>,
    ) -> EngineResult<()> {
        let set = self.functions.set_terrain.ok_or(EngineError::Error)?;
        let cells = terrain.resolution[0] as usize * terrain.resolution[1] as usize;
        if heights.len() != cells || splat_rgba.is_some_and(|s| s.len() != cells * 4) {
            return Err(EngineError::InvalidArgument);
        }
        let raw = terrain.to_raw();
        result(unsafe {
            set(
                self.session,
                entity,
                &raw,
                heights.as_ptr(),
                splat_rgba.map_or(std::ptr::null(), <[u8]>::as_ptr),
            )
        })
    }

    /// Where a world-space ray meets a terrain, in world space (C ABI 27). `None` when it misses, for an entity that is
    /// not a terrain, or from an older engine library.
    pub fn terrain_raycast(
        &self,
        entity: u32,
        origin: [f64; 3],
        direction: [f64; 3],
    ) -> Option<[f64; 3]> {
        let cast = self.functions.terrain_raycast?;
        let mut hit = [0.0f64; 3];
        (unsafe {
            cast(
                self.session,
                entity,
                origin.as_ptr(),
                direction.as_ptr(),
                hit.as_mut_ptr(),
            )
        } == 0)
            .then_some(hit)
    }

    /// The ring a brush of `radius_m` metres makes on the terrain around a world-space `center`: `segments` points on
    /// the relief, a few centimetres above it (C ABI 27).
    pub fn terrain_brush_outline(
        &self,
        entity: u32,
        center: [f64; 3],
        radius_m: f32,
        segments: u32,
    ) -> Option<Vec<[f32; 3]>> {
        let outline = self.functions.terrain_outline?;
        let mut out = vec![0.0f32; segments as usize * 3];
        (unsafe {
            outline(
                self.session,
                entity,
                center.as_ptr(),
                radius_m,
                segments,
                out.as_mut_ptr(),
            )
        } == 0)
            .then(|| out.as_chunks::<3>().0.to_vec())
    }

    /// Whether a terrain's data is still in the old `.cterrain` format (C ABI 26): it opens and edits as it is, and
    /// `terrain_convert_native` saves it as the native `.terrain`. `None` for an entity that is not a terrain, or
    /// from an older engine library.
    pub fn terrain_data_is_legacy(&self, entity: u32) -> Option<bool> {
        let query = self.functions.terrain_data_legacy?;
        let mut legacy = 0i32;
        (unsafe { query(self.session, entity, &mut legacy) } == 0).then_some(legacy != 0)
    }

    /// Saves a terrain as the native `.terrain` next to its legacy file, checked by reading it back bit for bit, and
    /// points the scene at it. Returns the new project-relative path.
    pub fn terrain_convert_native(&mut self, entity: u32) -> EngineResult<String> {
        let convert = self
            .functions
            .terrain_convert_native
            .ok_or(EngineError::Error)?;
        read_string(256, |buffer, size| unsafe {
            convert(self.session, entity, buffer, size)
        })
    }

    /// Whether the engine library can place a brush cursor on a terrain (C ABI 27).
    pub fn has_terrain_cursor(&self) -> bool {
        self.functions.terrain_raycast.is_some() && self.functions.terrain_outline.is_some()
    }

    /// Whether the engine library has terrain strokes (C ABI 25).
    pub fn has_terrain_strokes(&self) -> bool {
        self.functions.stroke_begin.is_some()
    }

    /// Starts a brush stroke on a terrain (C ABI 25). A stroke that is still open is ended first.
    pub fn terrain_stroke_begin(&mut self, entity: u32, brush: &TerrainBrush) -> EngineResult<()> {
        let begin = self.functions.stroke_begin.ok_or(EngineError::Error)?;
        let raw = RawTerrainBrush {
            mode: brush.mode as i32,
            radius_m: brush.radius_m,
            strength: brush.strength,
            spacing_m: brush.spacing_m,
            falloff: brush.falloff as i32,
            rotation_degrees: brush.rotation_degrees,
            target_height_m: brush.target_height_m,
            pressure_to_strength: brush.pressure_to_strength,
            seed: brush.seed,
        };
        let (alpha, size) = match &brush.stamp {
            Some((size, alpha)) if alpha.len() == (*size as usize) * (*size as usize) => {
                (alpha.as_ptr(), *size)
            }
            Some(_) => return Err(EngineError::InvalidArgument),
            None => (std::ptr::null(), 0),
        };
        result(unsafe { begin(self.session, entity, &raw, alpha, size) })
    }

    /// Extends the open stroke to a world-space point, in metres. Returns how many stamps it laid.
    pub fn terrain_stroke_point(&mut self, world: [f64; 3], pressure: f32) -> EngineResult<u32> {
        let point = self.functions.stroke_point.ok_or(EngineError::Error)?;
        let mut stamps = 0u32;
        result(unsafe {
            point(
                self.session,
                world[0],
                world[1],
                world[2],
                pressure,
                &mut stamps,
            )
        })?;
        Ok(stamps)
    }

    /// Ends the open stroke and keeps its record for undo.
    pub fn terrain_stroke_end(&mut self) -> EngineResult<StrokeSummary> {
        let end = self.functions.stroke_end.ok_or(EngineError::Error)?;
        let mut raw = RawStrokeResult::default();
        result(unsafe { end(self.session, &mut raw) })?;
        Ok(StrokeSummary {
            record: raw.record,
            stamps: raw.stamps,
            tiles_changed: raw.tiles_changed,
            bytes: raw.bytes,
            region: (raw.has_region != 0).then_some((
                raw.region_min_x,
                raw.region_min_z,
                raw.region_max_x,
                raw.region_max_z,
            )),
        })
    }

    /// Undoes a finished stroke exactly, by the id `terrain_stroke_end` gave.
    pub fn terrain_stroke_revert(&mut self, record: u32) -> EngineResult<()> {
        let revert = self.functions.stroke_revert.ok_or(EngineError::Error)?;
        result(unsafe { revert(self.session, record) })
    }

    /// Redoes an undone stroke.
    pub fn terrain_stroke_reapply(&mut self, record: u32) -> EngineResult<()> {
        let reapply = self.functions.stroke_reapply.ok_or(EngineError::Error)?;
        result(unsafe { reapply(self.session, record) })
    }

    pub fn terrain(&self, entity: u32) -> EngineResult<Terrain> {
        let get = self.functions.get_terrain.ok_or(EngineError::Error)?;
        let mut raw = RawTerrain::default();
        result(unsafe { get(self.session, entity, &mut raw) })?;
        Ok(Terrain::from_raw(&raw))
    }

    /// World-space height of a terrain at local (x, z) metres from its corner.
    /// Stores or replaces a terrain mask: `data` is `resolution[0] * resolution[1]` bytes, row-major (z rows),
    /// 0..255 for 0..1, on a grid of its own (C ABI v15). Names are 1 to 31 letters, digits, `_` or `-`; the built-in
    /// names (`height`, `slope`, `curvature`) are taken.
    pub fn set_terrain_mask(
        &mut self,
        entity: u32,
        name: &str,
        resolution: [u32; 2],
        data: &[u8],
    ) -> EngineResult<()> {
        let set = self.functions.set_terrain_mask.ok_or(EngineError::Error)?;
        if data.len() != resolution[0] as usize * resolution[1] as usize {
            return Err(EngineError::InvalidArgument);
        }
        let name = CString::new(name).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe {
            set(
                self.session,
                entity,
                name.as_ptr(),
                resolution[0],
                resolution[1],
                data.as_ptr(),
            )
        })
    }

    /// Removes a stored mask.
    pub fn remove_terrain_mask(&mut self, entity: u32, name: &str) -> EngineResult<()> {
        let set = self.functions.set_terrain_mask.ok_or(EngineError::Error)?;
        let name = CString::new(name).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { set(self.session, entity, name.as_ptr(), 0, 0, std::ptr::null()) })
    }

    /// The masks of a terrain: the three built-ins first, then the stored ones.
    pub fn terrain_masks(&self, entity: u32) -> EngineResult<Vec<TerrainMaskInfo>> {
        let count = self
            .functions
            .terrain_mask_count
            .ok_or(EngineError::Error)?;
        let at = self.functions.terrain_mask_at.ok_or(EngineError::Error)?;
        let mut n = 0u32;
        result(unsafe { count(self.session, entity, &mut n) })?;
        let mut masks = Vec::with_capacity(n as usize);
        for index in 0..n {
            let mut raw = RawTerrainMaskInfo {
                name: [0; 32],
                resolution_x: 0,
                resolution_z: 0,
                builtin: 0,
            };
            result(unsafe { at(self.session, entity, index, &mut raw) })?;
            let name = unsafe { CStr::from_ptr(raw.name.as_ptr()) }
                .to_string_lossy()
                .into_owned();
            masks.push(TerrainMaskInfo {
                name,
                resolution: [raw.resolution_x, raw.resolution_z],
                builtin: raw.builtin != 0,
            });
        }
        Ok(masks)
    }

    /// One mask at (x, z) metres from the terrain's corner, 0..1.
    pub fn terrain_mask(&self, entity: u32, name: &str, x: f32, z: f32) -> EngineResult<f32> {
        let sample = self.functions.terrain_mask.ok_or(EngineError::Error)?;
        let name = CString::new(name).map_err(|_| EngineError::InvalidArgument)?;
        let mut value = 0.0f32;
        result(unsafe { sample(self.session, entity, name.as_ptr(), x, z, &mut value) })?;
        Ok(value)
    }

    /// A rectangle `[x0, z0, x1, z1]` (metres from the corner) resampled to `size[0] x size[1]` bytes, row-major,
    /// row 0 at z0.
    pub fn terrain_mask_rect(
        &self,
        entity: u32,
        name: &str,
        rect: [f32; 4],
        size: [u32; 2],
    ) -> EngineResult<Vec<u8>> {
        let sample = self.functions.terrain_mask_rect.ok_or(EngineError::Error)?;
        let name = CString::new(name).map_err(|_| EngineError::InvalidArgument)?;
        let mut out = vec![0u8; size[0] as usize * size[1] as usize];
        result(unsafe {
            sample(
                self.session,
                entity,
                name.as_ptr(),
                rect[0],
                rect[1],
                rect[2],
                rect[3],
                size[0],
                size[1],
                out.as_mut_ptr(),
            )
        })?;
        Ok(out)
    }

    /// Scatters `classes` over the terrain of `entity` (C ABI v16): where to put trees, rocks, grass, by the terrain's
    /// height, normal and masks and the biome table `biome_text` (empty for none; see `biome_check`). `region` is
    /// `[x0, z0, x1, z1]` in metres from the corner (`None` for all of it); at most `max` instances are returned,
    /// `total` says how many the region holds. Deterministic: the same seed gives the same instances.
    pub fn scatter_terrain(
        &self,
        entity: u32,
        seed: u64,
        classes: &[ScatterClass],
        biome_text: &str,
        region: Option<[f32; 4]>,
        max: u32,
    ) -> EngineResult<ScatterResult> {
        let scatter = self.functions.scatter_terrain.ok_or(EngineError::Error)?;
        let raw_classes: Vec<RawScatterClass> = classes
            .iter()
            .map(|c| RawScatterClass {
                id: c.id,
                priority: c.priority,
                cell_size: c.cell_size,
                radius: c.radius,
                density: c.density,
                min_scale: c.min_scale,
                max_scale: c.max_scale,
                tilt_to_normal: c.tilt_to_normal,
                sink: c.sink,
                tint_jitter: c.tint_jitter,
                variants: c.variants,
            })
            .collect();
        let biome = CString::new(biome_text).map_err(|_| EngineError::InvalidArgument)?;
        let region_ptr = region.as_ref().map_or(std::ptr::null(), |r| r.as_ptr());
        let mut total = 0u32;
        // First count, then fill exactly (the count is cheap next to the entities made from it).
        result(unsafe {
            scatter(
                self.session,
                entity,
                seed,
                raw_classes.as_ptr(),
                raw_classes.len() as u32,
                biome.as_ptr(),
                region_ptr,
                std::ptr::null_mut(),
                0,
                &mut total,
            )
        })?;
        let want = total.min(max);
        let mut raw = vec![RawScatterInstance::default(); want as usize];
        let mut again = 0u32;
        result(unsafe {
            scatter(
                self.session,
                entity,
                seed,
                raw_classes.as_ptr(),
                raw_classes.len() as u32,
                biome.as_ptr(),
                region_ptr,
                raw.as_mut_ptr(),
                want,
                &mut again,
            )
        })?;
        Ok(ScatterResult {
            instances: raw
                .iter()
                .map(|r| ScatterInstance {
                    class_id: r.class_id,
                    variant: r.variant,
                    position: r.position,
                    up: r.up,
                    rotation_degrees: r.rotation_degrees,
                    scale: r.scale,
                    tint: r.tint,
                    seed: r.seed,
                })
                .collect(),
            total,
        })
    }

    /// Checks a biome table: `Ok`, or the reason (with the line number).
    pub fn biome_check(&self, text: &str) -> Result<(), String> {
        let check = self
            .functions
            .biome_check
            .ok_or_else(|| "the engine library needs C ABI v16".to_owned())?;
        let text = CString::new(text).map_err(|_| "the text contains a NUL".to_owned())?;
        let mut error = [0 as c_char; 256];
        let code = unsafe { check(text.as_ptr(), error.as_mut_ptr(), error.len() as u32) };
        if code == 0 {
            return Ok(());
        }
        Err(unsafe { CStr::from_ptr(error.as_ptr()) }
            .to_string_lossy()
            .into_owned())
    }

    /// The unit surface normal of a terrain at (x, z) metres from its corner.
    pub fn terrain_normal(&self, entity: u32, x: f32, z: f32) -> EngineResult<[f32; 3]> {
        let sample = self.functions.terrain_normal.ok_or(EngineError::Error)?;
        let mut normal = [0.0f32; 3];
        result(unsafe { sample(self.session, entity, x, z, normal.as_mut_ptr()) })?;
        Ok(normal)
    }

    pub fn terrain_height(&self, entity: u32, x: f32, z: f32) -> EngineResult<f32> {
        let sample = self.functions.terrain_height.ok_or(EngineError::Error)?;
        let mut height = 0.0f32;
        result(unsafe { sample(self.session, entity, x, z, &mut height) })?;
        Ok(height)
    }

    /// Adds or updates a water surface on an entity (needs a C ABI v7 library).
    pub fn set_water(&mut self, entity: u32, water: &Water) -> EngineResult<()> {
        let set = self.functions.set_water.ok_or(EngineError::Error)?;
        let raw = water.to_raw();
        result(unsafe { set(self.session, entity, &raw) })
    }

    pub fn water(&self, entity: u32) -> EngineResult<Water> {
        let get = self.functions.get_water.ok_or(EngineError::Error)?;
        let mut raw = RawWater::default();
        result(unsafe { get(self.session, entity, &mut raw) })?;
        Ok(Water::from_raw(&raw))
    }

    /// Surface height of a water entity at a rest position, as the engine simulates it.
    /// Builds the Micromesh cluster hierarchy of an indexed triangle mesh (C ABI v12): clusters of at
    /// most 128 triangles, simplified levels, crack-free cuts. Deterministic and offline work: call it
    /// from a bake, not per frame.
    pub fn micromesh_build(
        &self,
        positions: &[[f32; 3]],
        indices: &[u32],
    ) -> EngineResult<MicromeshBake> {
        let build = self.functions.micromesh_build.ok_or(EngineError::Error)?;
        let mut stats = RawMicromeshStats::default();
        let mut bytes: *mut u8 = std::ptr::null_mut();
        let mut size = 0usize;
        result(unsafe {
            build(
                positions.as_ptr().cast(),
                positions.len() as u32,
                indices.as_ptr(),
                indices.len() as u32,
                &mut stats,
                &mut bytes,
                &mut size,
            )
        })?;
        let data = if bytes.is_null() || size == 0 {
            Vec::new()
        } else {
            unsafe { std::slice::from_raw_parts(bytes, size) }.to_vec()
        };
        if !bytes.is_null() {
            unsafe { (self.functions.free_bytes)(bytes.cast()) };
        }
        Ok(MicromeshBake {
            bytes: data,
            source_triangles: stats.source_triangles,
            clusters: stats.clusters,
            levels: stats.levels,
            root_clusters: stats.root_clusters,
            max_cluster_triangles: stats.max_cluster_triangles,
            max_error: stats.max_error,
        })
    }

    /// What the last rendered frame drew and cost, and what the GPU supports (C ABI v11).
    pub fn render_stats(&self) -> EngineResult<RenderStats> {
        let query = self.functions.render_stats.ok_or(EngineError::Error)?;
        let mut raw = RawRenderStats::default();
        result(unsafe { query(self.session, &mut raw) })?;
        Ok(RenderStats {
            triangles: raw.triangles,
            meshes: raw.meshes,
            frustum_culled: raw.frustum_culled,
            draw_calls: raw.draw_calls,
            instanced_draw_calls: raw.instanced_draw_calls,
            gather_ms: raw.gather_ms,
            shadows_ms: raw.shadows_ms,
            opaque_ms: raw.opaque_ms,
            translucent_ms: raw.translucent_ms,
            total_ms: raw.total_ms,
            indirect_draw: raw.indirect_draw != 0,
            graphics_storage_buffers: raw.graphics_storage_buffers != 0,
            compute_storage_write: raw.compute_storage_write != 0,
            mesh_shaders: raw.mesh_shaders != 0,
            gpu_water_draws: raw.gpu_water_draws,
            micromesh_draws: raw.micromesh_draws,
            shadow_draws: raw.shadow_draws,
            shadow_draws_culled: raw.shadow_draws_culled,
        })
    }

    /// What the last frame's Micromesh draws kept (C ABI v14). Blocks until the GPU is done: for tools and
    /// tests, not for every frame.
    pub fn micromesh_frame_stats(&self) -> EngineResult<MicromeshFrameStats> {
        let query = self.functions.micromesh_stats.ok_or(EngineError::Error)?;
        let mut raw = RawMicromeshFrameStats::default();
        result(unsafe { query(self.session, &mut raw) })?;
        Ok(MicromeshFrameStats {
            draws: raw.draws,
            clusters: raw.clusters,
            triangles: raw.triangles,
            source_triangles: raw.source_triangles,
            views: raw.views,
            arena_used: raw.arena_used,
            arena_capacity: raw.arena_capacity,
            overflowed: raw.overflowed,
            occlusion: raw.occlusion != 0,
        })
    }

    /// What Photon's voxel far field holds and what its latest update cost (C ABI v17).
    pub fn photon_stats(&self) -> EngineResult<PhotonStats> {
        let query = self.functions.photon_stats.ok_or(EngineError::Error)?;
        let mut raw = RawPhotonStats::default();
        result(unsafe { query(self.session, &mut raw) })?;
        Ok(PhotonStats {
            objects: raw.objects,
            triangles: raw.triangles,
            over_budget: raw.over_budget != 0,
            far_enabled: raw.far_enabled != 0,
            full_rebuilds: raw.full_rebuilds,
            region_rebuilds: raw.region_rebuilds,
            last_level_mask: raw.last_level_mask,
            last_regions: raw.last_regions,
            last_update_ms: raw.last_update_ms,
            target_rebuilds: raw.target_rebuilds,
            far_skipped: raw.far_skipped,
            far_half_rate: raw.far_half_rate,
        })
    }

    /// Throws Photon's triangle volume away; the next frame voxelises the scene again (C ABI v17).
    pub fn photon_rebuild(&mut self) -> EngineResult<()> {
        let rebuild = self.functions.photon_rebuild.ok_or(EngineError::Error)?;
        result(unsafe { rebuild(self.session) })
    }

    /// The profiler's scopes in depth-first order (C ABI v18). The profiler is process-wide.
    pub fn profiler_scopes(&self) -> EngineResult<Vec<ProfileScope>> {
        let report = self.functions.profiler_report.ok_or(EngineError::Error)?;
        let mut count = 0u32;
        result(unsafe { report(std::ptr::null_mut(), 0, &mut count) })?;
        let mut raw = vec![
            RawProfileScope {
                name: [0; 48],
                depth: 0,
                calls: 0,
                last_ms: 0.0,
                avg_ms: 0.0,
                max_ms: 0.0,
                self_ms: 0.0,
                recent_avg_ms: 0.0,
                recent_max_ms: 0.0,
            };
            count as usize
        ];
        if count == 0 {
            return Ok(Vec::new());
        }
        let mut total = 0u32;
        result(unsafe { report(raw.as_mut_ptr(), count, &mut total) })?;
        raw.truncate((total as usize).min(raw.len()));
        Ok(raw
            .into_iter()
            .map(|scope| {
                let end = scope.name.iter().position(|b| *b == 0).unwrap_or(48);
                ProfileScope {
                    name: String::from_utf8_lossy(&scope.name[..end]).into_owned(),
                    depth: scope.depth,
                    calls: scope.calls,
                    last_ms: scope.last_ms,
                    avg_ms: scope.avg_ms,
                    max_ms: scope.max_ms,
                    self_ms: scope.self_ms,
                    recent_avg_ms: scope.recent_avg_ms,
                    recent_max_ms: scope.recent_max_ms,
                }
            })
            .collect())
    }

    /// The machine's CPU cores, memory and GPU (C ABI v19). Call it at a steady rate: the percentages cover the
    /// time since the previous call, and the first call reports zeros.
    pub fn system_metrics(&self) -> EngineResult<SystemMetrics> {
        let sample = self.functions.system_metrics.ok_or(EngineError::Error)?;
        let mut raw = RawSystemMetrics {
            cpu_core_count: 0,
            cpu_core_percent: [0.0; MAX_CPU_CORES],
            cpu_total_percent: 0.0,
            process_cpu_percent: 0.0,
            ram_process_mib: -1.0,
            ram_used_mib: -1.0,
            ram_total_mib: -1.0,
            gpu_busy_percent: -1.0,
            vram_used_mib: -1.0,
            vram_total_mib: -1.0,
        };
        result(unsafe { sample(&mut raw) })?;
        let known = |value: f32| (value >= 0.0).then_some(value);
        let cores = (raw.cpu_core_count as usize).min(MAX_CPU_CORES);
        Ok(SystemMetrics {
            cpu_cores: raw.cpu_core_percent[..cores].to_vec(),
            cpu_total: raw.cpu_total_percent,
            process_cpu: raw.process_cpu_percent,
            ram_process_mib: known(raw.ram_process_mib),
            ram_used_mib: known(raw.ram_used_mib),
            ram_total_mib: known(raw.ram_total_mib),
            gpu_busy: known(raw.gpu_busy_percent),
            vram_used_mib: known(raw.vram_used_mib),
            vram_total_mib: known(raw.vram_total_mib),
        })
    }

    /// The lightning and the wind's surge at the latest frame (C ABI v19).
    pub fn weather_state(&self) -> EngineResult<WeatherState> {
        let get = self.functions.weather_state.ok_or(EngineError::Error)?;
        let mut raw = RawWeatherState {
            flash: 0.0,
            bolt: 0.0,
            gust: 1.0,
            clock_s: 0.0,
            lightning_active: 0,
            strike_distance_m: 0.0,
            strike_azimuth_rad: 0.0,
        };
        result(unsafe { get(self.session, &mut raw) })?;
        Ok(WeatherState {
            flash: raw.flash,
            bolt: raw.bolt,
            gust: raw.gust,
            clock_s: raw.clock_s,
            lightning_active: raw.lightning_active != 0,
            strike_distance_m: raw.strike_distance_m,
            strike_azimuth_rad: raw.strike_azimuth_rad,
        })
    }

    /// The scene's volumetric audio (C ABI v20).
    pub fn volumetric_state(&self) -> EngineResult<VolumetricState> {
        let get = self.functions.volumetric_state.ok_or(EngineError::Error)?;
        let mut raw = RawVolumetricState {
            enabled: 0,
            device_open: 0,
            voices: 0,
            obstacles: 0,
            clips_missing: 0,
            openness: 0.0,
            sky_exposure: 0.0,
            rt60_s: 0.0,
            wet: 0.0,
            echoes: 0,
            time_s: 0.0,
        };
        result(unsafe { get(self.session, &mut raw) })?;
        Ok(VolumetricState {
            enabled: raw.enabled != 0,
            device_open: raw.device_open != 0,
            voices: raw.voices,
            obstacles: raw.obstacles,
            clips_missing: raw.clips_missing,
            openness: raw.openness,
            sky_exposure: raw.sky_exposure,
            rt60_s: raw.rt60_s,
            wet: raw.wet,
            echoes: raw.echoes,
            time_s: raw.time_s,
        })
    }

    /// One emitter's state, `index` below [`VolumetricState::voices`] (C ABI v20).
    pub fn volumetric_voice(&self, index: u32) -> EngineResult<VolumetricVoice> {
        let get = self.functions.volumetric_voice.ok_or(EngineError::Error)?;
        let mut raw = RawVolumetricVoice {
            entity: 0,
            layer: 0,
            loaded: 0,
            playing: 0,
            distance_m: 0.0,
            delay_s: 0.0,
            pitch_ratio: 1.0,
            gain_db: 0.0,
            cutoff_hz: 0.0,
            blocked: 0,
            diffracted: 0,
            dir_x: 0.0,
            dir_y: 0.0,
            dir_z: 0.0,
        };
        result(unsafe { get(self.session, index, &mut raw) })?;
        Ok(VolumetricVoice {
            entity: raw.entity,
            layer: raw.layer,
            loaded: raw.loaded != 0,
            playing: raw.playing != 0,
            distance_m: raw.distance_m,
            delay_s: raw.delay_s,
            pitch_ratio: raw.pitch_ratio,
            gain_db: raw.gain_db,
            cutoff_hz: raw.cutoff_hz,
            blocked: raw.blocked != 0,
            diffracted: raw.diffracted != 0,
            direction: [raw.dir_x, raw.dir_y, raw.dir_z],
        })
    }

    /// The names of the sound devices that can play (C ABI v20).
    pub fn audio_output_devices(&self) -> Vec<String> {
        let (Some(count), Some(name)) = (
            self.functions.audio_device_count,
            self.functions.audio_device_name,
        ) else {
            return Vec::new();
        };
        let n = unsafe { count() };
        (0..n.max(0))
            .filter_map(|index| {
                let mut buffer = [0u8; 256];
                if unsafe { name(index, buffer.as_mut_ptr(), buffer.len()) } != 0 {
                    return None;
                }
                let end = buffer.iter().position(|b| *b == 0).unwrap_or(buffer.len());
                Some(String::from_utf8_lossy(&buffer[..end]).into_owned())
            })
            .collect()
    }

    /// Chooses the device the volumetric mixer plays on: an index of [`Engine::audio_output_devices`], or `None` for the
    /// system default (C ABI v20).
    pub fn set_audio_output_device(&mut self, index: Option<usize>) -> EngineResult<()> {
        let set = self.functions.set_audio_device.ok_or(EngineError::Error)?;
        result(unsafe { set(self.session, index.map_or(-1, |i| i as i32)) })
    }

    /// Opens or closes the sound device the volumetric mixer plays on (C ABI v20). An error when there is no device.
    pub fn set_audio_output(&mut self, on: bool) -> EngineResult<()> {
        let set = self.functions.set_audio_output.ok_or(EngineError::Error)?;
        result(unsafe { set(self.session, i32::from(on)) })
    }

    /// Mixes `frames` stereo frames (interleaved) for tests and captures; only while no device is open (C ABI v20).
    pub fn volumetric_render(&mut self, frames: u32) -> EngineResult<Vec<f32>> {
        let render = self.functions.volumetric_render.ok_or(EngineError::Error)?;
        let mut out = vec![0.0f32; frames as usize * 2];
        let mut written = 0u32;
        result(unsafe { render(self.session, out.as_mut_ptr(), frames, &mut written) })?;
        out.truncate(written as usize * 2);
        Ok(out)
    }

    /// What the weather's sound is doing (C ABI v19).
    pub fn audio_state(&self) -> EngineResult<AudioState> {
        let get = self.functions.audio_state.ok_or(EngineError::Error)?;
        let mut raw = RawAudioState {
            enabled: 0,
            device_ready: 0,
            volume: 0.0,
            rain_level: 0.0,
            wind_level: 0.0,
            rain_playing: 0,
            wind_playing: 0,
            thunder_heard: 0,
            thunder_played: 0,
        };
        result(unsafe { get(self.session, &mut raw) })?;
        Ok(AudioState {
            enabled: raw.enabled != 0,
            device_ready: raw.device_ready != 0,
            volume: raw.volume,
            rain_level: raw.rain_level,
            wind_level: raw.wind_level,
            rain_playing: raw.rain_playing != 0,
            wind_playing: raw.wind_playing != 0,
            thunder_heard: raw.thunder_heard,
            thunder_played: raw.thunder_played,
        })
    }

    /// The thunder that has reached the listener since the last call (C ABI v19).
    pub fn poll_thunder(&mut self) -> EngineResult<Vec<ThunderEvent>> {
        let poll = self.functions.poll_thunder.ok_or(EngineError::Error)?;
        let mut events = [ThunderEvent::default(); 16];
        let mut count = 0u32;
        result(unsafe { poll(self.session, events.as_mut_ptr(), 16, &mut count) })?;
        Ok(events[..(count as usize).min(16)].to_vec())
    }

    /// Forgets every profiler scope and sample (C ABI v18).
    pub fn profiler_reset(&self) -> EngineResult<()> {
        let reset = self.functions.profiler_reset.ok_or(EngineError::Error)?;
        result(unsafe { reset() })
    }

    /// Turns the profiler's GPU timing on or off: the GPU passes then wait for the GPU, so frames are slower
    /// while it is on (C ABI v18).
    pub fn set_profiler_gpu_timing(&self, on: bool) -> EngineResult<()> {
        let set = self
            .functions
            .profiler_set_gpu_timing
            .ok_or(EngineError::Error)?;
        result(unsafe { set(i32::from(on)) })
    }

    /// Whether the profiler's GPU timing is on (C ABI v18).
    pub fn profiler_gpu_timing(&self) -> EngineResult<bool> {
        let get = self
            .functions
            .profiler_gpu_timing
            .ok_or(EngineError::Error)?;
        let mut on = 0i32;
        result(unsafe { get(&mut on) })?;
        Ok(on != 0)
    }

    pub fn water_height(&self, entity: u32, x: f32, z: f32) -> EngineResult<f32> {
        let sample = self.functions.water_height.ok_or(EngineError::Error)?;
        let mut height = 0.0f32;
        result(unsafe { sample(self.session, entity, x, z, &mut height) })?;
        Ok(height)
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

    /// The camera a scene view would use and its field of view in degrees; `None` when the scene has no
    /// Camera 3D (or the library predates C ABI v23).
    pub fn scene_camera(&self, view: SceneView) -> Option<(u32, f32)> {
        let get = self.functions.scene_camera?;
        let mut entity = 0u32;
        let mut fov = 0.0f32;
        let code = unsafe { get(self.session, view.raw(), &mut entity, &mut fov) };
        (code == 0).then_some((entity, fov))
    }

    /// Chooses the camera of the camera preview; `None` follows the active camera.
    pub fn set_preview_camera(&mut self, entity: Option<u32>) {
        if let Some(set) = self.functions.set_preview_camera {
            unsafe { set(self.session, entity.unwrap_or(u32::MAX)) };
        }
    }

    pub fn view_render_begin(
        &mut self,
        view: SceneView,
        width: u32,
        height: u32,
    ) -> EngineResult<()> {
        let begin = self.functions.view_render_begin.ok_or(EngineError::Error)?;
        result(unsafe { begin(self.session, view.raw(), width, height) })
    }

    pub fn view_render_poll(
        &mut self,
        view: SceneView,
        pixels: &mut [u8],
    ) -> EngineResult<RenderState> {
        let poll = self.functions.view_render_poll.ok_or(EngineError::Error)?;
        let state = unsafe { poll(self.session, view.raw(), pixels.as_mut_ptr(), pixels.len()) };
        match state {
            0 => Ok(RenderState::Idle),
            1 => Ok(RenderState::Pending),
            2 => Ok(RenderState::Ready),
            _ => Ok(RenderState::Failed),
        }
    }

    /// Begins the game: the scene's simulation as the compiled game runs it. False when the library cannot.
    pub fn game_start(&mut self) -> bool {
        match self.functions.game_start {
            Some(start) => unsafe { start(self.session) == 0 },
            None => false,
        }
    }

    pub fn game_stop(&mut self) {
        if let Some(stop) = self.functions.game_stop {
            unsafe { stop(self.session) };
        }
    }

    pub fn game_running(&self) -> bool {
        self.functions
            .game_running
            .is_some_and(|running| unsafe { running(self.session) != 0 })
    }

    /// One frame of the running game.
    pub fn game_tick(&mut self, delta_time: f32) -> bool {
        match self.functions.game_tick {
            Some(tick) => unsafe { tick(self.session, delta_time) == 0 },
            None => false,
        }
    }

    pub fn game_state(&self) -> GameState {
        let Some(get) = self.functions.game_state else {
            return GameState::default();
        };
        let mut raw = RawGameState::default();
        if unsafe { get(self.session, &mut raw) } != 0 {
            return GameState::default();
        }
        GameState {
            running: raw.running != 0,
            scripts_running: raw.scripts_running != 0,
            frames: raw.frames,
            time_s: raw.time_s,
        }
    }

    /// A key the game's scripts read; `scancode` is the USB HID / SDL scancode.
    pub fn game_key(&mut self, scancode: u32, down: bool) {
        if let Some(key) = self.functions.game_key {
            unsafe { key(self.session, scancode, i32::from(down)) };
        }
    }

    pub fn game_release_keys(&mut self) {
        if let Some(release) = self.functions.game_release_keys {
            unsafe { release(self.session) };
        }
    }

    /// The pointer over the game view; `button` 1 left, 2 middle, 3 right, 0 for a plain move.
    pub fn game_mouse(&mut self, x: f32, y: f32, button: u32, down: bool) {
        if let Some(mouse) = self.functions.game_mouse {
            unsafe { mouse(self.session, x, y, button, i32::from(down)) };
        }
    }

    /// Whether the scene advances while it renders (clouds, weather, wind, water, sound). Off, a frame stands
    /// still. A library older than C ABI v21 cannot pause: it keeps simulating and this reports it.
    pub fn set_simulating(&mut self, simulating: bool) -> bool {
        match self.functions.set_simulating {
            Some(set) => {
                unsafe { set(self.session, i32::from(simulating)) };
                true
            }
            None => false,
        }
    }

    /// Whether the scene is simulating (true for a library that cannot pause).
    pub fn simulating(&self) -> bool {
        match self.functions.simulating {
            Some(get) => unsafe { get(self.session) != 0 },
            None => true,
        }
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
    fn a_terrain_stroke_edits_locally_and_undoes_exactly() {
        let _guard = test_lock();
        let Some(mut engine) = Engine::load_default() else {
            return;
        };
        if !engine.has_terrain_strokes() {
            return; // a libcaffeine-capi from before C ABI 25
        }
        let entity = engine.create_entity("Ground").expect("entity");
        let terrain = Terrain {
            resolution: [257, 257],
            size: [512.0, 512.0],
            max_height: 80.0,
            ..Terrain::default()
        };
        engine
            .set_terrain(entity, &terrain, &vec![0.5; 257 * 257], None)
            .expect("terrain");
        let height = |engine: &Engine, x: f32, z: f32| {
            engine.terrain_height(entity, x + 256.0, z + 256.0).unwrap()
        };
        let before = height(&engine, 0.0, 0.0);

        engine
            .terrain_stroke_begin(
                entity,
                &TerrainBrush {
                    radius_m: 10.0,
                    strength: 2.0,
                    spacing_m: 5.0,
                    ..TerrainBrush::default()
                },
            )
            .expect("begin");
        assert_eq!(engine.terrain_stroke_point([0.0, 0.0, 0.0], 1.0), Ok(1));
        assert_eq!(engine.terrain_stroke_point([50.0, 0.0, 0.0], 1.0), Ok(10));
        assert!(height(&engine, 25.0, 0.0) > before + 1.5);
        assert!((height(&engine, 0.0, 100.0) - before).abs() < 0.001);
        let summary = engine.terrain_stroke_end().expect("end");
        assert_ne!(summary.record, 0);
        assert_eq!(summary.stamps, 11);
        assert!(summary.tiles_changed >= 1 && summary.tiles_changed < 16);
        assert!(summary.region.is_some());
        let edited = height(&engine, 25.0, 0.0);

        engine
            .terrain_stroke_revert(summary.record)
            .expect("revert");
        assert!((height(&engine, 25.0, 0.0) - before).abs() < 1e-4);
        engine
            .terrain_stroke_reapply(summary.record)
            .expect("reapply");
        assert!((height(&engine, 25.0, 0.0) - edited).abs() < 1e-4);
        assert_eq!(
            engine.terrain_stroke_revert(9999),
            Err(EngineError::NotFound)
        );
    }

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
                shadows: true,
            })
            .unwrap();
        let clamped = engine.viewport_settings().unwrap();
        assert!(!clamped.show_grid);
        assert_eq!(clamped.anti_aliasing, AntiAliasing::Taa);
        assert!(clamped.render_scale <= 2.0);
        assert!(clamped.shadows);
        assert!(!defaults.shadows);
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

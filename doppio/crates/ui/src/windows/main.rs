use crate::history::{self, History};
use crate::uikit::asset_files;
use crate::uikit::asset_folders;
use crate::uikit::asset_wizard;
use crate::uikit::behaviour;
use crate::uikit::components::{
    behaviour_picker, button, dock_overlay, gizmo, menu, panel, prefab_palette, scene_markers,
    status, tabs, toolbar, viewport_shader,
};
use crate::uikit::dryad;
use crate::uikit::generators::{self, MaterialGraph};
use crate::uikit::height_pack;
use crate::uikit::id;
use crate::uikit::material_library;
use crate::uikit::panels;
use crate::uikit::panels::dryad_nodes;
use crate::uikit::panels::material;
use crate::uikit::panels::material_generator;
use crate::uikit::panels::nature_generator as nature_generator_panel;
use crate::uikit::panels::project_manager;
use crate::uikit::panels::script as script_panel;
use crate::uikit::panels::terrain_generator as terrain_generator_panel;
use crate::uikit::panels::texture_generator as texture_generator_panel;
use crate::uikit::panels::texture_set as texture_set_panel;
use crate::uikit::panels::water_generator as water_generator_panel;
use crate::uikit::prefabs;
use crate::uikit::project;
use crate::uikit::script;
use crate::uikit::texture_set;
use crate::uikit::theme;
use crate::uikit::EntityView;
use crate::windows::log::{LogEntry, LogLevel};
use crate::windows::projectloader;

use iced::widget::{column, container, pane_grid, row, text};
use iced::{mouse, Element, Length, Point, Size};
use std::collections::{HashMap, HashSet};
use std::time::Instant;

const DEFAULT_VIEWPORT_WIDTH: u32 = 960;
const DEFAULT_VIEWPORT_HEIGHT: u32 = 540;
/// Render the viewport above its logical size so it stays sharp on HiDPI and
/// after Iced scales the image back down.
const VIEWPORT_PIXEL_RATIO: f32 = 2.0;
/// Upper bound for the CPU readback path (RGBA, three bytes per logical pixel
/// at 2×); keeps a 4K pane from uploading tens of megabytes per frame.
const VIEWPORT_PIXEL_BUDGET: f32 = 3_000_000.0;
/// CPU readback plus a full image upload is expensive; 30 fps is plenty for
/// an editor preview and keeps the UI thread responsive.
const VIEWPORT_FRAME_INTERVAL: std::time::Duration = std::time::Duration::from_millis(33);
/// Stop just short of the pole so `lookAt` never degenerates, but let the
/// editor look almost straight up at clouds. Focus/height is never clamped.
const PITCH_LIMIT: f32 = 1.5533;

#[cfg(feature = "agent")]
pub fn run() -> iced::Result {
    agent::run()
}

#[cfg(not(feature = "agent"))]
pub fn run() -> iced::Result {
    iced::application(App::new, App::update, App::view)
        .subscription(App::subscription)
        .theme(|_: &App| theme::app_theme())
        .settings(theme::settings())
        .window(theme::window())
        .run()
}

/// Shared result slot of a background caf-pack run.
type PackSlot = std::sync::Arc<std::sync::Mutex<Option<Result<String, String>>>>;

/// Pointer and window events the editor reacts to regardless of the widget
/// under the cursor. Shared by the live subscription and the headless driver
/// so both see exactly the same mapping.
fn global_event_message(event: &iced::Event) -> Option<Message> {
    match event {
        iced::Event::Mouse(mouse::Event::ButtonReleased(mouse::Button::Left)) => {
            Some(Message::PointerReleased)
        }
        iced::Event::Window(iced::window::Event::FileDropped(path)) => {
            Some(Message::FileDropped(path.clone()))
        }
        _ => None,
    }
}

/// Keyboard shortcuts and fly-camera keys (events no widget captured).
fn keyboard_event_message(event: iced::keyboard::Event) -> Option<Message> {
    match event {
        iced::keyboard::Event::KeyPressed { key, modifiers, .. } => {
            if matches!(
                key.as_ref(),
                iced::keyboard::Key::Named(iced::keyboard::key::Named::Escape)
            ) {
                return Some(Message::CloseMenu);
            }
            if !modifiers.control() {
                if let iced::keyboard::Key::Character(character) = key.as_ref() {
                    if let Some(key) = match character {
                        "1" => Some(1),
                        "2" => Some(2),
                        "3" => Some(3),
                        _ => None,
                    } {
                        return Some(Message::GizmoShortcut(key));
                    }
                }
                if matches!(
                    key.as_ref(),
                    iced::keyboard::Key::Character(character)
                        if character.eq_ignore_ascii_case("g")
                ) {
                    return Some(Message::ViewportGridShortcut);
                }
                if matches!(
                    key.as_ref(),
                    iced::keyboard::Key::Named(iced::keyboard::key::Named::Shift)
                ) {
                    return Some(Message::ViewportBoost(true));
                }
                return fly_key(&key).map(|key| Message::ViewportKey(key, true));
            }
            match key.as_ref() {
                iced::keyboard::Key::Character(character)
                    if character.eq_ignore_ascii_case("e")
                        && modifiers.control()
                        && modifiers.shift() =>
                {
                    Some(Message::OpenScriptEditor)
                }
                iced::keyboard::Key::Character(character)
                    if character.eq_ignore_ascii_case("d")
                        && modifiers.control()
                        && modifiers.shift() =>
                {
                    Some(Message::OpenDryadNodes)
                }
                iced::keyboard::Key::Character(character)
                    if character.eq_ignore_ascii_case("o") && modifiers.control() =>
                {
                    Some(Message::Project(project_manager::Edit::Open))
                }
                iced::keyboard::Key::Character(character)
                    if character.eq_ignore_ascii_case("z") =>
                {
                    Some(if modifiers.shift() {
                        Message::Redo
                    } else {
                        Message::Undo
                    })
                }
                iced::keyboard::Key::Character(character)
                    if character.eq_ignore_ascii_case("y") =>
                {
                    Some(Message::Redo)
                }
                iced::keyboard::Key::Character(character)
                    if character.eq_ignore_ascii_case("n") =>
                {
                    Some(if modifiers.shift() {
                        Message::TogglePrefabPalette
                    } else {
                        Message::AddEntity
                    })
                }
                iced::keyboard::Key::Character(character)
                    if character.eq_ignore_ascii_case("m") && modifiers.shift() =>
                {
                    Some(Message::Material(material::Edit::New))
                }
                iced::keyboard::Key::Character(character)
                    if character.eq_ignore_ascii_case("s") =>
                {
                    Some(Message::SaveScene)
                }
                iced::keyboard::Key::Character(character)
                    if character.eq_ignore_ascii_case("f") =>
                {
                    Some(Message::FocusSelection)
                }
                iced::keyboard::Key::Character(character)
                    if character.eq_ignore_ascii_case("r") =>
                {
                    Some(Message::RenderViewport)
                }
                iced::keyboard::Key::Character("0") => Some(Message::ResetCamera),
                iced::keyboard::Key::Character("1") => Some(Message::Layout(LayoutPreset::Default)),
                iced::keyboard::Key::Character("2") => {
                    Some(Message::Layout(LayoutPreset::Viewport))
                }
                iced::keyboard::Key::Named(iced::keyboard::key::Named::Enter) => {
                    Some(Message::TogglePlay)
                }
                iced::keyboard::Key::Named(iced::keyboard::key::Named::F5) => {
                    Some(Message::AssetRefresh)
                }
                iced::keyboard::Key::Named(iced::keyboard::key::Named::Escape) => {
                    Some(Message::Project(project_manager::Edit::Close))
                }
                _ => None,
            }
        }
        iced::keyboard::Event::KeyReleased { key, .. } => {
            if matches!(
                key.as_ref(),
                iced::keyboard::Key::Named(iced::keyboard::key::Named::Shift)
            ) {
                Some(Message::ViewportBoost(false))
            } else {
                fly_key(&key).map(|key| Message::ViewportKey(key, false))
            }
        }
        _ => None,
    }
}

#[derive(Debug, Clone, Copy)]
struct ViewportDrag {
    button: mouse::Button,
    last: Option<Point>,
    distance: f32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum FlyKey {
    Forward,
    Back,
    Left,
    Right,
    Up,
    Down,
}

/// Cheap content fingerprint so an unchanged frame does not upload a new
/// multi-megabyte image texture every 16 ms.
fn viewport_sample_hash(pixels: &[u8]) -> u64 {
    let mut hash = 0xcbf2_9ce4_8422_2325u64;
    for byte in pixels.iter().step_by(16) {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    hash ^= pixels.len() as u64;
    hash
}

fn fly_key(key: &iced::keyboard::Key) -> Option<FlyKey> {
    let iced::keyboard::Key::Character(character) = key.as_ref() else {
        return None;
    };
    if character.eq_ignore_ascii_case("w") {
        Some(FlyKey::Forward)
    } else if character.eq_ignore_ascii_case("s") {
        Some(FlyKey::Back)
    } else if character.eq_ignore_ascii_case("a") {
        Some(FlyKey::Left)
    } else if character.eq_ignore_ascii_case("d") {
        Some(FlyKey::Right)
    } else if character.eq_ignore_ascii_case("q") {
        Some(FlyKey::Down)
    } else if character.eq_ignore_ascii_case("e") {
        Some(FlyKey::Up)
    } else {
        None
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum MenuId {
    Project,
    Scene,
    Edit,
    View,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum LayoutPreset {
    Default,
    Viewport,
}

#[derive(Debug, Clone)]
struct PaneGroup {
    pages: Vec<PaneKind>,
    active: usize,
}

impl PaneGroup {
    fn new(pages: &[PaneKind]) -> Self {
        Self {
            pages: pages.to_vec(),
            active: 0,
        }
    }

    fn active_kind(&self) -> PaneKind {
        self.pages
            .get(self.active)
            .copied()
            .unwrap_or(PaneKind::Hierarchy)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PaneKind {
    Hierarchy,
    Assets,
    Viewport,
    Generator,
    TextureGenerator,
    NatureGenerator,
    WaterGenerator,
    TerrainGenerator,
    Inspector,
    Material,
    TextureSet,
    Script,
    DryadNodes,
    Console,
}

#[derive(Debug, Clone)]
enum Message {
    PaneDragged(pane_grid::DragEvent),
    PaneResized(pane_grid::ResizeEvent),
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    SelectEntity(usize),
    HierarchyContext(usize),
    HierarchyToggle(usize),
    HierarchyRowPress(usize),
    HierarchyRowRelease(usize),
    HierarchyRowEnter(usize),
    HierarchyRowExit(usize),
    HierarchyRootEnter,
    HierarchyRootExit,
    HierarchyRootRelease,
    HierarchyHoverClear,
    HierarchyRootToggle,
    HierarchySearch(String),
    #[cfg_attr(not(test), allow(dead_code))]
    HierarchyDoubleClick(usize),
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    SelectEntityByName(String),
    TogglePlay,
    AddEntity,
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    AddEntityNamed(String),
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    SetCamera(doppio_engine::Camera),
    SetGizmoMode(gizmo::Mode),
    GizmoShortcut(u8),
    Gizmo(gizmo::Message),
    Inspector(panels::inspector::Edit),
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    SetComponentField {
        component: usize,
        field: usize,
        value: doppio_engine::FieldValue,
    },
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    SetParent {
        entity: u32,
        parent: Option<u32>,
    },
    SaveScene,
    RenderViewport,
    ViewportTick,
    ViewportPressed(mouse::Button),
    ViewportReleased(mouse::Button),
    ViewportMoved(Point),
    ViewportScrolled(mouse::ScrollDelta),
    ViewportDoubleClick,
    ViewportHover(bool),
    ViewportKey(FlyKey, bool),
    ViewportBoost(bool),
    ViewportResized(f32, f32),
    ViewportGridShortcut,
    ToggleGrid,
    ToggleMarkers,
    MarkerSelected(u32),
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    SetViewportSettings {
        show_grid: Option<bool>,
        anti_aliasing: Option<doppio_engine::AntiAliasing>,
        render_scale: Option<f32>,
    },
    FocusSelection,
    ResetCamera,
    Undo,
    Redo,
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    CloseScene,
    ToggleMenu(MenuId),
    CloseMenu,
    ToggleConsoleFilter(LogLevel),
    Layout(LayoutPreset),
    TabPressed(pane_grid::Pane, PaneKind),
    TabReleased(pane_grid::Pane, PaneKind),
    TabEnter(pane_grid::Pane, PaneKind),
    TabExit(pane_grid::Pane, PaneKind),
    PaneEnter(pane_grid::Pane, PaneKind),
    PaneExit(pane_grid::Pane, PaneKind),
    PaneReleased(pane_grid::Pane, PaneKind),
    SplitPage(pane_grid::Pane),
    ToggleMaximize(pane_grid::Pane),
    DockHover(pane_grid::Pane, Option<dock_overlay::Zone>),
    DockDrop(pane_grid::Pane, dock_overlay::Zone),
    PointerReleased,
    TogglePrefabPalette,
    ClosePrefabPalette,
    SelectPrefabCategory(usize),
    CreatePrefab(usize),
    AssetPressed(usize),
    AssetReleased(usize),
    AssetDoubleClick(usize),
    AssetSearch(String),
    AssetTreeToggle(String),
    AssetTreePanel,
    Wizard(panels::asset_wizard::Edit),
    OpenScriptEditor,
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    OpenScriptPath(String),
    OpenDryadNodes,
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    OpenDryadPath(String),
    DryadCanvas(dryad_nodes::CanvasEvent),
    DryadSearch(String),
    DryadAddNode(dryad::NodeKind),
    DryadSelectNode(Option<String>),
    DryadSelectLink(Option<dryad_nodes::LinkEndpoints>),
    DryadNumber {
        node: String,
        field: String,
        text: String,
    },
    DryadBool {
        node: String,
        field: String,
        value: bool,
    },
    DryadText {
        node: String,
        value: String,
    },
    DryadDelete,
    DryadSave,
    DryadSync,
    DryadAttach,
    DryadRun,
    DryadFit,
    DryadAddMotion,
    Noop,
    ScriptSelect(usize),
    ScriptTabClose(usize),
    ScriptAction(iced_code_editor::Message),
    ScriptSave,
    ScriptRevert,
    ScriptClose,
    ScriptAttach,
    ScriptDetach,
    AssetNavigate(String),
    AssetUp,
    /// The Assets pane's file bar: new folder, rename, delete.
    AssetFile(panels::assetmanager::FileAction),
    AssetRoot,
    AssetRefresh,
    AssetViewMode(panels::assetmanager::ViewMode),
    Project(project_manager::Edit),
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    OpenMaterial(String),
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    SetMaterialTexture {
        slot: material::TextureSlot,
        path: String,
    },
    Material(material::Edit),
    TextureSet(texture_set_panel::Edit),
    TextureGen(texture_generator_panel::Edit),
    ShowTextureGenerator,
    NatureGen(nature_generator_panel::Edit),
    ShowNatureGenerator,
    WaterGen(water_generator_panel::Edit),
    ShowWaterGenerator,
    TerrainGen(terrain_generator_panel::Edit),
    ShowTerrainGenerator,
    Generator(material_generator::Edit),
    GeneratorTextureSearch(String),
    GeneratorTexturePick(String),
    GeneratorTextureClose,
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    ImportMaterialcraft(String),
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    ExportMaterialcraft(String),
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    OpenTextureSet(String),
    OpenGenerator,
    MaterialPickerSearch(String),
    MaterialPickerPick(String),
    MaterialPickerClose,
    MaterialHeightSearch(String),
    MaterialHeightPick(String),
    MaterialHeightClose,
    MaterialEnvironmentSearch(String),
    MaterialEnvironmentPick(String),
    MaterialEnvironmentClose,
    BehaviourSearch(String),
    BehaviourPick(behaviour::Behaviour),
    BehaviourClose,
    BehaviourNew,
    MaterialAssetSearch(String),
    MaterialAssetPick(String),
    MaterialAssetClose,
    FileDropped(std::path::PathBuf),
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    ImportMeshMaterials(String),
}

struct MaterialDocument {
    surface: doppio_engine::Material,
    /// Asset-root-relative path, once saved.
    asset_path: Option<String>,
    /// Absolute path used to load and save the `.mat` file.
    file_path: Option<String>,
    dirty: bool,
    preview_dirty: bool,
    /// Texture used as the map-baker height source (asset-relative).
    source: String,
    generator: MaterialGraph,
    generator_history: generators::GraphHistory,
    generator_packing: generators::ChannelPacking,
    generator_bits: u8,
    thumbnails: [Option<iced::widget::image::Handle>; 4],
    source_thumbnail: Option<iced::widget::image::Handle>,
}

const GENERATOR_NODE_PREVIEW: u32 = 96;

enum GeneratorBakeEvent {
    Progress(f32, String),
    NodePreviews(Vec<(generators::NodeId, Vec<u8>)>),
    Done(Result<Box<generators::BakedMaterial>, String>),
}

struct GeneratorPreviewJob {
    rx: std::sync::mpsc::Receiver<GeneratorBakeEvent>,
    cancel: std::sync::Arc<std::sync::atomic::AtomicBool>,
    version: u64,
}

struct GeneratorBakeJob {
    rx: std::sync::mpsc::Receiver<GeneratorBakeEvent>,
    cancel: std::sync::Arc<std::sync::atomic::AtomicBool>,
    directory: std::path::PathBuf,
    asset_directory: String,
    stem: String,
    packing: generators::ChannelPacking,
    bits: u8,
    progress: f32,
    stage: String,
}

struct App {
    panes: pane_grid::State<PaneGroup>,
    engine: Option<doppio_engine::Engine>,
    entities: Vec<EntityView>,
    assets: Vec<doppio_engine::AssetInfo>,
    asset_paths: Vec<String>,
    asset_thumbnails: Vec<Option<iced::widget::image::Handle>>,
    asset_thumbnail_cache: HashMap<String, (u64, Option<iced::widget::image::Handle>)>,
    asset_tints: Vec<Option<iced::Color>>,
    asset_search: String,
    asset_drag: Option<usize>,
    asset_directory: String,
    asset_view_mode: panels::assetmanager::ViewMode,
    asset_tree_collapsed: std::collections::HashSet<String>,
    asset_tree_open: bool,
    wizard: panels::asset_wizard::State,
    /// Result slot of a running caf-pack job, polled on every tick.
    pack_job: Option<PackSlot>,
    /// Asset root used instead of the engine's (tests, offline fixtures).
    asset_root_override: Option<std::path::PathBuf>,
    asset_selected: Option<usize>,
    asset_edit: Option<panels::assetmanager::FileEdit>,
    project: project_manager::State,
    project_recent_file: std::path::PathBuf,
    script: script::State,
    dryad: dryad::State,
    selected: usize,
    name_input: String,
    log: Vec<LogEntry>,
    console_filters: std::collections::HashSet<LogLevel>,
    playing: bool,
    next_entity_id: u32,
    viewport_frame: Option<viewport_shader::Frame>,
    viewport_frame_camera: Option<doppio_engine::Camera>,
    viewport_pending_camera: Option<doppio_engine::Camera>,
    viewport_pixels: Vec<u8>,
    viewport_desired: (u32, u32),
    viewport_render_size: (u32, u32),
    viewport_pending: bool,
    viewport_started: Option<Instant>,
    viewport_last_request: Option<Instant>,
    viewport_image_hash: Option<u64>,
    viewport_uploads: u64,
    viewport_latency_ms: f32,
    viewport_hud_latency_ms: f32,
    viewport_fps: f32,
    viewport_fps_window: Instant,
    viewport_frames: u32,
    viewport_hovered: bool,
    /// The scene holds a water surface, so the editor keeps stepping the engine for it.
    has_water: bool,
    water_scan: u32,
    viewport_last_tick: Option<Instant>,
    fly_keys: HashSet<FlyKey>,
    fly_boost: bool,
    viewport_settings: doppio_engine::ViewportSettings,
    viewport_drag: Option<ViewportDrag>,
    open_menu: Option<MenuId>,
    hierarchy_menu: Option<usize>,
    collapsed_hierarchy: std::collections::HashSet<u32>,
    hierarchy_root_open: bool,
    hierarchy_search: String,
    hierarchy_drag: Option<u32>,
    hierarchy_hover: Option<u32>,
    hierarchy_root_hover: bool,
    tab_drag: Option<(pane_grid::Pane, PaneKind)>,
    tab_hover: Option<(pane_grid::Pane, PaneKind)>,
    tab_hover_pane: Option<pane_grid::Pane>,
    /// True once the pressed tab was dragged away from its own header; only
    /// then do the docking guides appear, so a plain click never flashes them.
    tab_dragging: bool,
    /// Docking guide under the cursor while a tab is being dragged.
    dock_hover: Option<(pane_grid::Pane, dock_overlay::Zone)>,
    prefab_open: bool,
    prefab_category: usize,
    gizmo_mode: gizmo::Mode,
    viewport_logical: (f32, f32),
    last_hierarchy_click: Option<(usize, Instant)>,
    scene_label: String,
    camera: doppio_engine::Camera,
    history: History,
    component_types: Vec<doppio_engine::ComponentType>,
    components: Vec<doppio_engine::ComponentData>,
    field_inputs: HashMap<(usize, usize, usize), String>,
    transform_inputs: HashMap<(usize, usize), String>,
    collapsed_components: HashSet<u32>,
    selected_material: Option<String>,
    selected_material_thumbnail: Option<iced::widget::image::Handle>,
    material: Option<MaterialDocument>,
    material_presets: Vec<doppio_engine::MaterialPreset>,
    material_preview: Option<iced::widget::image::Handle>,
    material_preview_yaw: f32,
    material_preview_pitch: f32,
    material_preview_dragging: bool,
    material_preview_last: Option<Point>,
    material_last_preview: Option<Instant>,
    material_collapsed: HashSet<material::Section>,
    material_baking: Option<String>,
    material_directx: bool,
    material_wrap: bool,
    material_bake_strength: f32,
    material_bake_radius: f32,
    material_packing: usize,
    material_picker: Option<material::TextureSlot>,
    material_picker_search: String,
    material_height_picker: bool,
    material_height_search: String,
    material_environment_picker: bool,
    material_environment_search: String,
    /// Script picker opened by the inspector's "Add Behaviour".
    behaviour_picker: bool,
    behaviour_search: String,
    material_asset_picker: bool,
    material_asset_search: String,
    material_preview_shape: usize,
    material_preview_environment: String,
    material_preview_exposure: f32,
    material_light_generation: u32,
    material_light_inflight: u32,
    material_sun_yaw: f32,
    material_sun_elevation: f32,
    material_sun_intensity: f32,
    material_preview_shadows: bool,
    material_preview_pending: bool,
    material_preview_pixels: Vec<u8>,
    material_preview_inflight: Option<(doppio_engine::Material, usize, String, f32, f32, f32)>,
    material_map_details: [String; 4],
    generator_selected: Option<generators::NodeId>,
    generator_link: Option<(generators::NodeId, usize)>,
    generator_pan: iced::Vector,
    generator_zoom: f32,
    generator_preview: Option<iced::widget::image::Handle>,
    generator_preview_maps: Vec<iced::widget::image::Handle>,
    generator_preview_channel: usize,
    generator_status: Option<String>,
    generator_snap: bool,
    generator_search: String,
    generator_palette_collapsed: HashSet<String>,
    generator_clipboard: Option<generators::GeneratorNode>,
    generator_reconnect: bool,
    generator_texture_target: Option<generators::NodeId>,
    generator_texture_search: String,
    generator_bake: Option<GeneratorBakeJob>,
    generator_live_job: Option<GeneratorPreviewJob>,
    generator_preview_dirty: bool,
    generator_preview_changed: Option<Instant>,
    generator_preview_version: u64,
    generator_preview_revision: u64,
    generator_node_previews: HashMap<generators::NodeId, iced::widget::image::Handle>,
    packing_user_presets: Vec<generators::NamedPackingPreset>,
    packing_preset_name: String,
    packing_store_path: Option<std::path::PathBuf>,
    texgen: texturegen::TextureGen,
    nature: naturegen::NatureGen,
    watergen: watergen::WaterGen,
    terraingen: terraingen::TerrainGen,
    /// Scene markers (lights, cameras, colliders, empties) drawn over the viewport.
    markers: Vec<scene_markers::Marker>,
    show_markers: bool,
    markers_dirty: bool,
    texture_set: Option<texture_set::TextureSet>,
    texture_set_history: texture_set::TileHistory,
    texture_set_path: Option<std::path::PathBuf>,
    texture_set_dirty: bool,
    texture_set_preview: Option<iced::widget::image::Handle>,
    texture_set_status: Option<String>,
    texture_set_paint_mode: bool,
    texture_set_painting: bool,
    texture_set_cursor: Option<[f32; 2]>,
    texture_set_export_preset: usize,
    texture_set_export_presets: Vec<texture_set::ExportPreset>,
    texture_set_stroke_snapshot: Option<texture_set::TileSnapshot>,
}

impl PaneKind {
    fn title(self) -> &'static str {
        match self {
            PaneKind::Hierarchy => "Hierarchy",
            PaneKind::Assets => "Assets",
            PaneKind::Viewport => "Viewport",
            PaneKind::Generator => "Material Generator",
            PaneKind::TextureGenerator => "Texture Generator",
            PaneKind::NatureGenerator => "Nature",
            PaneKind::WaterGenerator => "Water",
            PaneKind::TerrainGenerator => "Terrain",
            PaneKind::Inspector => "Inspector",
            PaneKind::Material => "Material",
            PaneKind::TextureSet => "Texture Set",
            PaneKind::Script => "Script",
            PaneKind::DryadNodes => "Dryad Nodes",
            PaneKind::Console => "Console",
        }
    }

    fn tab_id(self) -> id::UiId {
        match self {
            PaneKind::Hierarchy => id::PANE_TAB_HIERARCHY,
            PaneKind::Assets => id::PANE_TAB_ASSETS,
            PaneKind::Viewport => id::PANE_TAB_VIEWPORT,
            PaneKind::Generator => id::PANE_TAB_GENERATOR,
            PaneKind::TextureGenerator => id::PANE_TAB_TEXTURE_GENERATOR,
            PaneKind::NatureGenerator => id::PANE_TAB_NATURE_GENERATOR,
            PaneKind::WaterGenerator => id::PANE_TAB_WATER_GENERATOR,
            PaneKind::TerrainGenerator => id::PANE_TAB_TERRAIN_GENERATOR,
            PaneKind::Inspector => id::PANE_TAB_INSPECTOR,
            PaneKind::Material => id::PANE_TAB_MATERIAL,
            PaneKind::TextureSet => id::PANE_TAB_TEXTURE_SET,
            PaneKind::Script => id::PANE_TAB_SCRIPT,
            PaneKind::DryadNodes => id::PANE_TAB_DRYAD_NODES,
            PaneKind::Console => id::PANE_TAB_CONSOLE,
        }
    }
}

impl App {
    fn layout_configuration(preset: LayoutPreset) -> pane_grid::Configuration<PaneGroup> {
        use pane_grid::{Axis, Configuration};
        match preset {
            LayoutPreset::Default => Configuration::Split {
                axis: Axis::Horizontal,
                ratio: 0.68,
                a: Box::new(Configuration::Split {
                    axis: Axis::Vertical,
                    ratio: 0.19,
                    a: Box::new(Configuration::Pane(PaneGroup::new(&[PaneKind::Hierarchy]))),
                    b: Box::new(Configuration::Split {
                        axis: Axis::Vertical,
                        ratio: 0.74,
                        a: Box::new(Configuration::Pane(PaneGroup::new(&[
                            PaneKind::Viewport,
                            PaneKind::Generator,
                            PaneKind::TextureGenerator,
                            PaneKind::TextureSet,
                            PaneKind::Script,
                            PaneKind::DryadNodes,
                        ]))),
                        b: Box::new(Configuration::Pane(PaneGroup::new(&[
                            PaneKind::Inspector,
                            PaneKind::Material,
                            PaneKind::NatureGenerator,
                            PaneKind::WaterGenerator,
                            PaneKind::TerrainGenerator,
                        ]))),
                    }),
                }),
                // Project-style bottom dock: the asset browser next to the console.
                b: Box::new(Configuration::Pane(PaneGroup::new(&[
                    PaneKind::Assets,
                    PaneKind::Console,
                ]))),
            },
            LayoutPreset::Viewport => Configuration::Split {
                axis: Axis::Horizontal,
                ratio: 0.82,
                a: Box::new(Configuration::Split {
                    axis: Axis::Vertical,
                    ratio: 0.14,
                    a: Box::new(Configuration::Pane(PaneGroup::new(&[PaneKind::Hierarchy]))),
                    b: Box::new(Configuration::Split {
                        axis: Axis::Vertical,
                        ratio: 0.82,
                        a: Box::new(Configuration::Pane(PaneGroup::new(&[
                            PaneKind::Viewport,
                            PaneKind::Generator,
                            PaneKind::TextureGenerator,
                            PaneKind::TextureSet,
                            PaneKind::Script,
                            PaneKind::DryadNodes,
                        ]))),
                        b: Box::new(Configuration::Pane(PaneGroup::new(&[
                            PaneKind::Inspector,
                            PaneKind::Material,
                            PaneKind::NatureGenerator,
                            PaneKind::WaterGenerator,
                            PaneKind::TerrainGenerator,
                        ]))),
                    }),
                }),
                b: Box::new(Configuration::Pane(PaneGroup::new(&[
                    PaneKind::Assets,
                    PaneKind::Console,
                ]))),
            },
        }
    }

    fn new() -> Self {
        let project_arg = projectloader::project_from_args();
        let loaded = projectloader::load_from_args();
        let scene_from_args = projectloader::scene_from_args();
        let mut app = Self::with_project(loaded);
        if let Some(path) = project_arg {
            app.open_project_path(&path);
            if app.project.active.is_none() {
                app.project.open = true;
                app.refresh_recent_projects();
            }
        } else if !scene_from_args {
            app.project.open = true;
            app.refresh_recent_projects();
        }
        app
    }

    /// Editor with no scene and no engine session, used by agent fixtures.
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    fn empty() -> Self {
        Self::with_project(projectloader::LoadedProject {
            engine: None,
            entities: Vec::new(),
            assets: Vec::new(),
            log: vec![LogEntry::new(LogLevel::Ui, "Empty fixture")],
        })
    }

    fn with_project(loaded: projectloader::LoadedProject) -> Self {
        let panes =
            pane_grid::State::with_configuration(Self::layout_configuration(LayoutPreset::Default));

        let scene_label = loaded
            .engine
            .as_ref()
            .and_then(|engine| engine.scene_path())
            .and_then(|path| path.rsplit(['/', '\\']).next().map(str::to_owned))
            .unwrap_or_else(|| "Scene".to_owned());

        let next_entity_id = loaded
            .entities
            .iter()
            .map(|entity| entity.id.saturating_add(1))
            .max()
            .unwrap_or(0);
        let name_input = loaded
            .entities
            .first()
            .map(|entity| entity.name.clone())
            .unwrap_or_default();

        let camera = loaded
            .engine
            .as_ref()
            .and_then(|engine| engine.camera().ok())
            .unwrap_or_default();
        let viewport_settings = loaded
            .engine
            .as_ref()
            .and_then(|engine| engine.viewport_settings().ok())
            .unwrap_or_default();
        let material_presets = loaded
            .engine
            .as_ref()
            .map(|engine| engine.material_presets())
            .unwrap_or_default();

        let mut app = Self {
            panes,
            engine: loaded.engine,
            entities: loaded.entities,
            assets: loaded.assets,
            asset_paths: Vec::new(),
            asset_thumbnails: Vec::new(),
            asset_thumbnail_cache: HashMap::new(),
            asset_tints: Vec::new(),
            asset_search: String::new(),
            asset_drag: None,
            asset_directory: String::new(),
            asset_view_mode: panels::assetmanager::ViewMode::Grid,
            asset_tree_collapsed: std::collections::HashSet::new(),
            asset_tree_open: true,
            wizard: panels::asset_wizard::State::default(),
            pack_job: None,
            asset_root_override: None,
            asset_selected: None,
            asset_edit: None,
            project: project_manager::State::new(
                project::default_projects_directory().display().to_string(),
            ),
            project_recent_file: project::recent_projects_file(),
            script: script::State::new(),
            dryad: dryad::State::new(),
            selected: 0,
            name_input,
            log: loaded.log,
            console_filters: LogLevel::ALL.into_iter().collect(),
            playing: false,
            next_entity_id,
            viewport_frame: None,
            viewport_frame_camera: None,
            viewport_pending_camera: None,
            viewport_pixels: vec![
                0;
                (DEFAULT_VIEWPORT_WIDTH * DEFAULT_VIEWPORT_HEIGHT * 4) as usize
            ],
            viewport_desired: (DEFAULT_VIEWPORT_WIDTH, DEFAULT_VIEWPORT_HEIGHT),
            viewport_render_size: (DEFAULT_VIEWPORT_WIDTH, DEFAULT_VIEWPORT_HEIGHT),
            viewport_pending: false,
            viewport_started: None,
            viewport_last_request: None,
            viewport_image_hash: None,
            viewport_uploads: 0,
            viewport_latency_ms: 0.0,
            viewport_hud_latency_ms: 0.0,
            viewport_fps: 0.0,
            viewport_fps_window: Instant::now(),
            viewport_frames: 0,
            viewport_hovered: false,
            has_water: false,
            water_scan: 0,
            viewport_last_tick: None,
            fly_keys: HashSet::new(),
            fly_boost: false,
            viewport_settings,
            viewport_drag: None,
            open_menu: None,
            hierarchy_menu: None,
            collapsed_hierarchy: std::collections::HashSet::new(),
            hierarchy_root_open: true,
            hierarchy_search: String::new(),
            hierarchy_drag: None,
            hierarchy_hover: None,
            hierarchy_root_hover: false,
            tab_drag: None,
            tab_hover: None,
            tab_hover_pane: None,
            dock_hover: None,
            tab_dragging: false,
            prefab_open: false,
            prefab_category: 0,
            gizmo_mode: gizmo::Mode::Translate,
            viewport_logical: (
                DEFAULT_VIEWPORT_WIDTH as f32,
                DEFAULT_VIEWPORT_HEIGHT as f32,
            ),
            last_hierarchy_click: None,
            scene_label,
            camera,
            history: History::new(history::State::new(Vec::new(), None, false)),
            component_types: Vec::new(),
            components: Vec::new(),
            field_inputs: HashMap::new(),
            transform_inputs: HashMap::new(),
            collapsed_components: HashSet::new(),
            selected_material: None,
            selected_material_thumbnail: None,
            material: None,
            material_presets,
            material_preview: None,
            material_preview_yaw: 20.0,
            material_preview_pitch: 12.0,
            material_preview_dragging: false,
            material_preview_last: None,
            material_last_preview: None,
            material_collapsed: [material::Section::Advanced].into_iter().collect(),
            material_baking: None,
            material_directx: false,
            material_wrap: true,
            material_bake_strength: 1.0,
            material_bake_radius: 8.0,
            material_packing: 0,
            material_picker: None,
            material_picker_search: String::new(),
            material_height_picker: false,
            material_height_search: String::new(),
            material_environment_picker: false,
            material_environment_search: String::new(),
            behaviour_picker: false,
            behaviour_search: String::new(),
            material_asset_picker: false,
            material_asset_search: String::new(),
            material_preview_shape: 0,
            material_preview_environment: String::new(),
            material_preview_exposure: 1.0,
            material_light_generation: 0,
            material_light_inflight: 0,
            material_sun_yaw: 41.0,
            material_sun_elevation: 46.0,
            material_sun_intensity: 1.6,
            material_preview_shadows: true,
            material_preview_pending: false,
            material_preview_pixels: Vec::new(),
            material_preview_inflight: None,
            material_map_details: [String::new(), String::new(), String::new(), String::new()],
            generator_selected: None,
            generator_link: None,
            generator_pan: iced::Vector::new(0.0, 0.0),
            generator_zoom: 1.0,
            generator_preview: None,
            generator_preview_maps: Vec::new(),
            generator_preview_channel: 0,
            generator_status: None,
            generator_snap: true,
            generator_search: String::new(),
            generator_palette_collapsed: HashSet::new(),
            generator_clipboard: None,
            generator_reconnect: false,
            generator_texture_target: None,
            generator_texture_search: String::new(),
            generator_bake: None,
            generator_live_job: None,
            generator_preview_dirty: false,
            generator_preview_changed: None,
            generator_preview_version: 0,
            generator_preview_revision: 0,
            generator_node_previews: HashMap::new(),
            packing_user_presets: generators::user_packing_preset_path()
                .map(|path| generators::load_user_packing_presets(&path))
                .unwrap_or_default(),
            packing_preset_name: String::new(),
            packing_store_path: generators::user_packing_preset_path(),
            texgen: texturegen::TextureGen::new(),
            nature: naturegen::NatureGen::new(),
            watergen: watergen::WaterGen::new(),
            terraingen: terraingen::TerrainGen::new(),
            markers: Vec::new(),
            show_markers: true,
            markers_dirty: true,
            texture_set: None,
            texture_set_history: texture_set::TileHistory::default(),
            texture_set_path: None,
            texture_set_dirty: false,
            texture_set_preview: None,
            texture_set_status: None,
            texture_set_paint_mode: false,
            texture_set_painting: false,
            texture_set_cursor: None,
            texture_set_export_preset: 0,
            texture_set_export_presets: texture_set::export_presets(),
            texture_set_stroke_snapshot: None,
        };
        if let Some(engine) = app.engine.as_ref() {
            app.component_types = engine.component_types();
        }
        app.refresh_components();
        app.refresh_asset_cache();
        let initial = app.capture_state();
        app.history = History::new(initial);
        app
    }

    fn log(&mut self, level: LogLevel, text: impl Into<String>) {
        self.log.push(LogEntry::new(level, text));
    }

    fn refresh_components(&mut self) {
        self.field_inputs.clear();
        self.transform_inputs.clear();
        if let Some(entity) = self.entities.get(self.selected) {
            for (kind, values) in [
                (0, entity.position),
                (1, entity.rotation),
                (2, entity.scale),
            ] {
                for (axis, value) in values.iter().enumerate() {
                    self.transform_inputs
                        .insert((kind, axis), format_transform_number(*value));
                }
            }
        }
        let selected = self.entities.get(self.selected).map(|entity| entity.id);
        self.components = match (self.engine.as_ref(), selected) {
            (Some(engine), Some(id)) => engine.entity_component_data(id),
            _ => Vec::new(),
        };
        self.selected_material = match (self.engine.as_ref(), selected) {
            (Some(engine), Some(id)) => engine
                .entity_material(id)
                .ok()
                .filter(|path| !path.is_empty()),
            _ => None,
        };
        self.selected_material_thumbnail = self.selected_material.as_ref().and_then(|path| {
            let engine = self.engine.as_ref()?;
            let material = engine.load_material(path).ok()?;
            if material.albedo_map.is_empty() {
                return None;
            }
            self.load_thumbnail(&material.albedo_map)
        });
        if let (Some(engine), Some(entity)) =
            (self.engine.as_ref(), self.entities.get(self.selected))
        {
            let icon = projectloader::entity_icon(engine, entity.id, &entity.name);
            if let Some(view) = self.entities.get_mut(self.selected) {
                view.icon = icon;
            }
        }
    }

    fn handle_inspector(&mut self, edit: panels::inspector::Edit) {
        match edit {
            panels::inspector::Edit::Name(value) => self.rename_selected(value),
            panels::inspector::Edit::FieldText {
                component,
                field,
                axis,
                value,
            } => self.edit_component_text(component, field, axis, value),
            panels::inspector::Edit::FieldBool {
                component,
                field,
                value,
            } => self.edit_component_bool(component, field, value),
            panels::inspector::Edit::FieldEnum {
                component,
                field,
                index,
            } => self.edit_component_enum(component, field, index),
            panels::inspector::Edit::FieldNumber {
                component,
                field,
                value,
            } => {
                self.write_component_field(component, field, doppio_engine::FieldValue::F32(value))
            }
            panels::inspector::Edit::FieldColor {
                component,
                field,
                axis,
                value,
            } => self.edit_component_color(component, field, axis, value),
            panels::inspector::Edit::AddComponent(type_id) => self.add_component(type_id),
            panels::inspector::Edit::RemoveComponent(component) => self.remove_component(component),
            panels::inspector::Edit::ToggleComponent(type_id) => {
                if !self.collapsed_components.remove(&type_id) {
                    self.collapsed_components.insert(type_id);
                }
            }
            panels::inspector::Edit::TransformText { kind, axis, value } => {
                self.set_transform_axis(kind, axis, value);
            }
            panels::inspector::Edit::OpenMaterial(path) => {
                self.open_material_path(&path);
            }
            panels::inspector::Edit::OpenBehaviourPicker => {
                self.behaviour_picker = true;
                self.behaviour_search.clear();
                self.open_menu = None;
            }
            panels::inspector::Edit::OpenMaterialPicker => {
                self.material_asset_picker = true;
                self.material_asset_search.clear();
            }
            panels::inspector::Edit::DropMaterialAsset => {
                if let Some(asset_index) = self.asset_drag.take() {
                    if let Some(asset) = self.assets.get(asset_index).cloned() {
                        if asset.kind == "material" {
                            if let Some(entity) =
                                self.entities.get(self.selected).map(|entity| entity.id)
                            {
                                if let Some(engine) = self.engine.as_mut() {
                                    if engine.apply_material(entity, &asset.relative_path).is_ok() {
                                        self.refresh_components();
                                        self.commit("Assign Material", None);
                                        self.log(
                                            LogLevel::Action,
                                            format!("Assigned {}", asset.relative_path),
                                        );
                                    }
                                }
                            }
                        } else if asset.kind == "image" {
                            self.drop_asset_on_entity(asset_index, self.selected);
                        }
                    }
                }
            }
            panels::inspector::Edit::ClearMaterial => self.clear_selected_material(),
            panels::inspector::Edit::Delete => self.delete_selected(),
        }
    }

    fn clear_selected_material(&mut self) {
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        if let Some(engine) = self.engine.as_mut() {
            if engine.apply_material(entity, "").is_ok() {
                self.refresh_components();
                self.log(LogLevel::Action, "Cleared material");
                self.commit("Clear Material", None);
            }
        }
    }

    fn rename_selected(&mut self, value: String) {
        self.name_input = value;
        if let Some(entity) = self.entities.get_mut(self.selected) {
            entity.name = self.name_input.clone();
            if let Some(engine) = self.engine.as_mut() {
                let _ = engine.set_entity_name(entity.id, &self.name_input);
                if let Ok(confirmed) = engine.entity_name(entity.id) {
                    entity.name = confirmed;
                }
                entity.icon = projectloader::entity_icon(engine, entity.id, &entity.name);
            }
        }
        let name = format!("Rename {}", self.name_input);
        let key = Some(format!("rename:{}", self.selected));
        self.commit(name, key);
    }

    fn write_component_field(
        &mut self,
        component: usize,
        field: usize,
        value: doppio_engine::FieldValue,
    ) {
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.set_component_field(entity, component as u32, field as u32, &value);
        }
        let mut type_id = 0;
        let mut field_name = String::new();
        if let Some(data) = self.components.get_mut(component) {
            type_id = data.info.type_id;
            if let Some(slot) = data.values.get_mut(field) {
                *slot = value;
            }
            if let Some(meta) = self
                .component_types
                .iter()
                .find(|kind| kind.id == type_id)
                .and_then(|kind| kind.fields.get(field))
            {
                field_name = meta.name.clone();
            }
        }
        self.sync_selected_transform();
        let name = format!("Set {field_name}");
        let key = Some(format!("field:{}:{}:{}", self.selected, type_id, field));
        self.commit(name, key);
    }

    fn set_transform_axis(&mut self, kind: usize, axis: usize, text: String) {
        if axis >= 3 {
            return;
        }
        self.transform_inputs.insert((kind, axis), text.clone());
        let Ok(value) = text.trim().parse::<f32>() else {
            return;
        };
        let Some(id) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        let (name, key) = match kind {
            0 => {
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    entity.position[axis] = value;
                }
                let position = self.entities[self.selected].position;
                if let Some(engine) = self.engine.as_mut() {
                    let _ = engine.set_position(id, position);
                }
                (
                    "Position".to_owned(),
                    format!("transform:position:{id}:{axis}"),
                )
            }
            1 => {
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    entity.rotation[axis] = value;
                }
                let rotation = self.entities[self.selected].rotation;
                if let Some(engine) = self.engine.as_mut() {
                    let _ = engine.set_rotation(id, rotation);
                }
                (
                    "Rotation".to_owned(),
                    format!("transform:rotation:{id}:{axis}"),
                )
            }
            _ => {
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    entity.scale[axis] = value.max(0.001);
                }
                let scale = self.entities[self.selected].scale;
                if let Some(engine) = self.engine.as_mut() {
                    let _ = engine.set_scale(id, scale);
                }
                ("Scale".to_owned(), format!("transform:scale:{id}:{axis}"))
            }
        };
        self.commit(format!("Set {name} {}", ["X", "Y", "Z"][axis]), Some(key));
    }

    fn sync_selected_transform(&mut self) {
        let Some(id) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        let Some(engine) = self.engine.as_ref() else {
            return;
        };
        let position = engine.position(id).ok();
        let rotation = engine.rotation(id).ok();
        let scale = engine.scale(id).ok();
        if let Some(entity) = self.entities.get_mut(self.selected) {
            if let Some(position) = position {
                entity.position = position;
            }
            if let Some(rotation) = rotation {
                entity.rotation = rotation;
            }
            if let Some(scale) = scale {
                entity.scale = scale;
            }
        }
    }

    fn edit_component_text(&mut self, component: usize, field: usize, axis: usize, text: String) {
        self.field_inputs
            .insert((component, field, axis), text.clone());
        let Some(current) = self
            .components
            .get(component)
            .and_then(|data| data.values.get(field))
            .cloned()
        else {
            return;
        };
        let Some(value) =
            crate::uikit::components::field_editor::parse_value(&current, axis, &text)
        else {
            return;
        };
        self.write_component_field(component, field, value);
    }

    fn edit_component_bool(&mut self, component: usize, field: usize, flag: bool) {
        self.write_component_field(component, field, doppio_engine::FieldValue::Bool(flag));
    }

    fn edit_component_color(&mut self, component: usize, field: usize, axis: usize, value: f32) {
        let Some(current) = self
            .components
            .get(component)
            .and_then(|data| data.values.get(field))
            .cloned()
        else {
            return;
        };
        if let doppio_engine::FieldValue::Color(mut color) = current {
            color[axis.min(3)] = value;
            self.write_component_field(component, field, doppio_engine::FieldValue::Color(color));
        }
    }

    fn edit_component_enum(&mut self, component: usize, field: usize, index: i32) {
        self.write_component_field(component, field, doppio_engine::FieldValue::Enum(index));
    }

    fn add_component(&mut self, type_id: u32) {
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        let name = self
            .component_types
            .iter()
            .find(|kind| kind.id == type_id)
            .map(|kind| kind.name.clone())
            .unwrap_or_else(|| format!("Component {type_id}"));
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.add_component(entity, type_id);
        }
        self.refresh_components();
        self.commit(format!("Add {name}"), None);
    }

    fn remove_component(&mut self, component: usize) {
        let Some(type_id) = self.components.get(component).map(|data| data.info.type_id) else {
            return;
        };
        let name = self
            .components
            .get(component)
            .map(|data| data.info.name.clone())
            .unwrap_or_default();
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.remove_component(entity, type_id);
        }
        self.refresh_components();
        self.commit(format!("Remove {name}"), None);
    }

    fn delete_selected(&mut self) {
        if self.entities.is_empty() {
            return;
        }
        let entity = self.entities.remove(self.selected);
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.destroy_entity(entity.id);
        }
        if self.selected >= self.entities.len() {
            self.selected = self.entities.len().saturating_sub(1);
        }
        self.name_input = self
            .entities
            .get(self.selected)
            .map(|entity| entity.name.clone())
            .unwrap_or_default();
        self.refresh_components();
        self.log(LogLevel::Action, format!("Deleted {}", entity.name));
        self.commit(format!("Delete {}", entity.name), None);
    }

    fn capture_state(&self) -> history::State {
        let mut state = history::State::new(self.entities.clone(), None, false);
        state.selected = self.selected;
        if let Some(engine) = self.engine.as_ref() {
            if let Ok(snapshot) = engine.snapshot() {
                state.engine = Some(snapshot.data);
                state.dirty = snapshot.dirty;
            }
        }
        state
    }

    /// Records the current editor state as one undo step.
    fn commit(&mut self, name: impl Into<String>, key: Option<String>) {
        let after = self.capture_state();
        self.history.push(after, name, key);
    }

    fn amend_selection(&mut self) {
        let selected = self.selected;
        self.history
            .amend_current(|state| state.selected = selected);
    }

    fn apply_history_state(&mut self, state: history::State) {
        if let (Some(engine), Some(data)) = (self.engine.as_mut(), state.engine.as_ref()) {
            let snapshot = doppio_engine::Snapshot {
                data: data.clone(),
                dirty: state.dirty,
            };
            match engine.restore(&snapshot) {
                Ok(()) => {
                    self.entities = projectloader::read_entities(engine);
                }
                Err(error) => {
                    self.log(LogLevel::Error, format!("Restore failed: {error:?}"));
                    self.entities = state.entities.clone();
                }
            }
            // Do not cancel the pending readback here. The frame submitted
            // before the undo must still be polled; otherwise the transfer
            // fence is never drained and every later render_begin fails,
            // leaving a stale prefab on screen while the gizmo follows the
            // restored selection.
        } else {
            self.entities = state.entities.clone();
        }

        self.collapsed_hierarchy.clear();
        self.selected = state.selected.min(self.entities.len().saturating_sub(1));
        self.name_input = self
            .entities
            .get(self.selected)
            .map(|entity| entity.name.clone())
            .unwrap_or_default();
        if let (Some(engine), Some(entity)) =
            (self.engine.as_mut(), self.entities.get(self.selected))
        {
            let _ = engine.select_entity(entity.id);
        }
        self.refresh_components();
    }

    fn undo(&mut self) {
        if self.generator_is_active() {
            self.undo_generator();
            return;
        }
        if let Some((name, state)) = self.history.undo() {
            self.apply_history_state(state);
            self.log(LogLevel::Action, format!("Undo {name}"));
        }
    }

    fn redo(&mut self) {
        if self.generator_is_active() {
            self.redo_generator();
            return;
        }
        if let Some((name, state)) = self.history.redo() {
            self.apply_history_state(state);
            self.log(LogLevel::Action, format!("Redo {name}"));
        }
    }

    fn generator_is_active(&self) -> bool {
        self.material.is_some()
            && self
                .panes
                .iter()
                .any(|(_, group)| group.active_kind() == PaneKind::Generator)
    }

    fn undo_generator(&mut self) -> bool {
        let Some(document) = self.material.as_mut() else {
            return false;
        };
        let Some((name, graph)) = document.generator_history.undo() else {
            return false;
        };
        document.generator = graph;
        document.dirty = true;
        self.generator_link = None;
        if self
            .generator_selected
            .is_some_and(|id| document.generator.node(id).is_none())
        {
            self.generator_selected = None;
        }
        self.generator_status = Some(format!("Undo {name}"));
        self.log(LogLevel::Action, format!("Undo {name}"));
        self.mark_generator_preview_dirty();
        true
    }

    fn redo_generator(&mut self) -> bool {
        let Some(document) = self.material.as_mut() else {
            return false;
        };
        let Some((name, graph)) = document.generator_history.redo() else {
            return false;
        };
        document.generator = graph;
        document.dirty = true;
        self.generator_link = None;
        if self
            .generator_selected
            .is_some_and(|id| document.generator.node(id).is_none())
        {
            self.generator_selected = None;
        }
        self.generator_status = Some(format!("Redo {name}"));
        self.log(LogLevel::Action, format!("Redo {name}"));
        self.mark_generator_preview_dirty();
        true
    }

    fn select_index(&mut self, index: usize) {
        let selected = self
            .entities
            .get(index)
            .map(|entity| (entity.id, entity.name.clone()));
        if let Some((id, name)) = selected {
            self.selected = index;
            self.name_input = name.clone();
            if let Some(engine) = self.engine.as_mut() {
                let _ = engine.select_entity(id);
                if engine.selected_entity() != id {
                    self.log(LogLevel::Engine, "Engine selection mismatch");
                }
            }
            self.log(LogLevel::Action, format!("Selected {name}"));
        }
        self.refresh_components();
        self.amend_selection();
    }

    fn add_entity_named(&mut self, name: String) {
        let index = self.entities.len();
        let id = if let Some(engine) = self.engine.as_mut() {
            engine
                .create_entity(&name)
                .unwrap_or(doppio_engine::ENTITY_NONE)
        } else {
            let id = self.next_entity_id;
            self.next_entity_id += 1;
            id
        };
        if id != doppio_engine::ENTITY_NONE {
            self.entities.push(EntityView {
                id,
                name: name.clone(),
                position: [0.0; 3],
                rotation: [0.0; 3],
                scale: [1.0; 3],
                parent: None,
                icon: crate::uikit::icons::Icon::Generic,
            });
            self.selected = index;
            self.name_input = name.clone();
            self.log(LogLevel::Action, format!("Added {name}"));
        }
    }

    fn apply_camera(&mut self) {
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.set_camera(self.camera);
        }
    }

    /// Converts a logical pane size into the offscreen render target size.
    /// The aspect ratio is preserved even when the pixel budget is exceeded.
    fn viewport_target_size(width: f32, height: f32) -> (u32, u32) {
        let width = width.max(64.0) * VIEWPORT_PIXEL_RATIO;
        let height = height.max(36.0) * VIEWPORT_PIXEL_RATIO;
        let pixels = width * height;
        let scale = if pixels > VIEWPORT_PIXEL_BUDGET {
            (VIEWPORT_PIXEL_BUDGET / pixels).sqrt()
        } else {
            1.0
        };
        (
            (width * scale).round().max(64.0) as u32,
            (height * scale).round().max(36.0) as u32,
        )
    }

    fn toggle_grid(&mut self) {
        self.viewport_settings.show_grid = !self.viewport_settings.show_grid;
        self.apply_viewport_settings();
        let state = if self.viewport_settings.show_grid {
            "on"
        } else {
            "off"
        };
        self.log(LogLevel::Action, format!("Grid {state}"));
    }

    fn finish_tab_drag(&mut self) {
        let source = self.tab_drag.take();
        let tab_target = self.tab_hover.take();
        self.tab_hover_pane = None;
        self.dock_hover = None;
        self.tab_dragging = false;
        // Dropping onto another tab reorders/merges; dropping onto a pane body
        // is handled by the docking overlay (`DockDrop`).
        if let (Some(source), Some(target)) = (source, tab_target) {
            self.move_page(source, target);
        }
    }

    /// Docks the dragged page into `target` according to the guide it was
    /// dropped on: the centre joins the tab group, an edge splits the pane.
    fn dock_page(
        &mut self,
        source: (pane_grid::Pane, PaneKind),
        target: pane_grid::Pane,
        zone: dock_overlay::Zone,
    ) {
        use dock_overlay::Zone;
        let (source_pane, page) = source;
        self.panes.restore();
        let Some(source_group) = self.panes.get(source_pane) else {
            return;
        };
        let source_len = source_group.pages.len();
        let Some(source_index) = source_group.pages.iter().position(|item| *item == page) else {
            return;
        };
        if zone == Zone::Center {
            if source_pane == target {
                return;
            }
            let Some(anchor) = self.panes.get(target).map(|group| group.active_kind()) else {
                return;
            };
            self.move_page(source, (target, anchor));
            return;
        }
        // Splitting a pane that only holds this page would just recreate it.
        if source_pane == target && source_len < 2 {
            return;
        }
        let (axis, new_first) = match zone {
            Zone::Left => (pane_grid::Axis::Vertical, true),
            Zone::Right => (pane_grid::Axis::Vertical, false),
            Zone::Top => (pane_grid::Axis::Horizontal, true),
            _ => (pane_grid::Axis::Horizontal, false),
        };
        let Some((new_pane, split)) = self.panes.split(axis, target, PaneGroup::new(&[page]))
        else {
            return;
        };
        if new_first {
            self.panes.swap(target, new_pane);
        }
        self.panes
            .resize(split, if new_first { 0.35 } else { 0.65 });
        if let Some(group) = self.panes.get_mut(source_pane) {
            if let Some(index) = group.pages.iter().position(|item| *item == page) {
                group.pages.remove(index);
            } else if source_index < group.pages.len() {
                group.pages.remove(source_index);
            }
            if !group.pages.is_empty() {
                group.active = group.active.min(group.pages.len() - 1);
            }
        }
        if source_len == 1 {
            self.panes.close(source_pane);
        }
        self.log(LogLevel::Action, format!("Docked {page:?} ({zone:?})"));
    }

    fn move_page(
        &mut self,
        source: (pane_grid::Pane, PaneKind),
        target: (pane_grid::Pane, PaneKind),
    ) {
        let (source_pane, source_page) = source;
        let (target_pane, target_page) = target;
        let Some(source_pages) = self.panes.get(source_pane).map(|group| group.pages.clone())
        else {
            return;
        };
        let Some(source_index) = source_pages.iter().position(|page| *page == source_page) else {
            return;
        };
        if source_pane == target_pane {
            let Some(target_index) = source_pages.iter().position(|page| *page == target_page)
            else {
                return;
            };
            if source_index == target_index {
                return;
            }
            if let Some(group) = self.panes.get_mut(source_pane) {
                let page = group.pages.remove(source_index);
                let insert = if target_index > source_index {
                    target_index - 1
                } else {
                    target_index
                };
                group.pages.insert(insert.min(group.pages.len()), page);
                group.active = insert.min(group.pages.len().saturating_sub(1));
            }
            return;
        }

        if source_pages.len() == 1 && self.panes.len() == 1 {
            return;
        }
        let Some(target_group) = self.panes.get(target_pane) else {
            return;
        };
        if target_group.pages.contains(&source_page) {
            return;
        }
        let target_index = target_group
            .pages
            .iter()
            .position(|page| *page == target_page)
            .unwrap_or(target_group.pages.len());
        if let Some(group) = self.panes.get_mut(target_pane) {
            group
                .pages
                .insert(target_index.min(group.pages.len()), source_page);
            group.active = target_index.min(group.pages.len().saturating_sub(1));
        }
        if let Some(group) = self.panes.get_mut(source_pane) {
            if let Some(index) = group.pages.iter().position(|page| *page == source_page) {
                group.pages.remove(index);
            }
            if !group.pages.is_empty() {
                group.active = group.active.min(group.pages.len() - 1);
            }
        }
        if source_pages.len() == 1 {
            self.panes.close(source_pane);
        }
        self.log(
            LogLevel::Action,
            format!("Moved {source_page:?} to another pane"),
        );
    }

    fn split_active_page(&mut self, pane: pane_grid::Pane) {
        let Some(group) = self.panes.get(pane) else {
            return;
        };
        if group.pages.len() < 2 {
            return;
        }
        let page = group.active_kind();
        let index = group.active;
        if self
            .panes
            .split(pane_grid::Axis::Vertical, pane, PaneGroup::new(&[page]))
            .is_none()
        {
            return;
        }
        if let Some(group) = self.panes.get_mut(pane) {
            if index < group.pages.len() {
                group.pages.remove(index);
            }
            group.active = group.active.min(group.pages.len().saturating_sub(1));
        }
        self.log(LogLevel::Action, format!("Split {page:?} into a new pane"));
    }

    fn finish_hierarchy_drag(&mut self) {
        if let Some(dragged) = self.hierarchy_drag.take() {
            if self.hierarchy_root_hover {
                self.set_entity_parent(dragged, None);
            } else if let Some(target) = self.hierarchy_hover {
                if target != dragged {
                    self.set_entity_parent(dragged, Some(target));
                }
            }
        }
        self.hierarchy_hover = None;
        self.hierarchy_root_hover = false;
    }

    fn set_entity_parent(&mut self, entity: u32, parent: Option<u32>) {
        let result = self
            .engine
            .as_mut()
            .map(|engine| engine.set_entity_parent(entity, parent));
        match result {
            Some(Ok(())) => {
                if let Some(view) = self.entities.iter_mut().find(|view| view.id == entity) {
                    view.parent = parent;
                }
                self.log(LogLevel::Action, "Changed parent");
                self.commit("Change Parent", None);
            }
            Some(Err(error)) => {
                self.log(LogLevel::Error, format!("Parent change failed: {error:?}"));
            }
            None => {}
        }
    }

    fn select_entity_click(&mut self, index: usize) {
        let double_click = self.last_hierarchy_click.is_some_and(|(last, at)| {
            last == index && at.elapsed() < std::time::Duration::from_millis(400)
        });
        self.last_hierarchy_click = Some((index, Instant::now()));
        self.select_index(index);
        if double_click {
            self.focus_selection();
        }
    }

    fn create_prefab(&mut self, index: usize) {
        let Some(prefab) = prefabs::PREFABS.get(index).copied() else {
            return;
        };
        self.prefab_open = false;
        let offset = (self.entities.len() % 6) as f32 * 1.5;
        let spawn = [
            self.camera.focus[0] + offset,
            self.camera.focus[1],
            self.camera.focus[2],
        ];
        let created = if let Some(engine) = self.engine.as_mut() {
            let result = match prefab.kind {
                prefabs::Kind::DuplicateSelection => self
                    .entities
                    .get(self.selected)
                    .map(|entity| entity.id)
                    .ok_or_else(|| "no entity selected".to_owned())
                    .and_then(|source| duplicate_entity(engine, source, prefab.label)),
                _ => create_prefab_entity(engine, &prefab, spawn),
            };
            Some(result)
        } else {
            None
        };

        match created {
            Some(Ok(id)) => {
                if let Some(engine) = self.engine.as_ref() {
                    self.entities = projectloader::read_entities(engine);
                }
                if let Some(position) = self.entities.iter().position(|entity| entity.id == id) {
                    self.selected = position;
                }
                self.name_input = self
                    .entities
                    .get(self.selected)
                    .map(|entity| entity.name.clone())
                    .unwrap_or_default();
                self.refresh_components();
                self.log(LogLevel::Action, format!("Created {}", prefab.label));
                self.commit(format!("Create {}", prefab.label), None);
            }
            Some(Err(error)) => {
                self.log(LogLevel::Error, format!("Prefab failed: {error}"));
            }
            None => {
                let mut name = prefab.label.to_owned();
                let mut suffix = 2;
                while self.entities.iter().any(|entity| entity.name == name) {
                    name = format!("{} {suffix}", prefab.label);
                    suffix += 1;
                }
                let id = self.next_entity_id;
                self.next_entity_id += 1;
                self.entities.push(EntityView {
                    id,
                    name: name.clone(),
                    position: spawn,
                    rotation: [0.0; 3],
                    scale: prefab_scale(prefab.kind),
                    parent: None,
                    icon: prefab.icon,
                });
                self.selected = self.entities.len() - 1;
                self.name_input = name;
                self.refresh_components();
                self.log(LogLevel::Action, format!("Created {}", prefab.label));
                self.commit(format!("Create {}", prefab.label), None);
            }
        }
    }

    fn refresh_asset_cache(&mut self) {
        self.asset_paths.clear();
        self.asset_thumbnails.clear();
        self.asset_tints.clear();
        let Some(engine) = self.engine.as_ref() else {
            return;
        };
        for asset in &self.assets {
            let absolute = engine
                .asset_path(&asset.relative_path)
                .unwrap_or_else(|_| asset.relative_path.clone());
            let material = if asset.kind == "material" {
                engine.load_material(&absolute).ok()
            } else {
                None
            };
            let tint = material.as_ref().map(|material| {
                iced::Color::from_rgba(
                    material.albedo[0],
                    material.albedo[1],
                    material.albedo[2],
                    material.albedo[3],
                )
            });
            let thumbnail_path = match asset.kind.as_str() {
                "image" => Some(absolute.clone()),
                "material" => material
                    .as_ref()
                    .filter(|material| !material.albedo_map.is_empty())
                    .and_then(|material| engine.resolve_path(&material.albedo_map).ok()),
                _ => None,
            };
            let thumbnail = thumbnail_path
                .filter(|path| std::path::Path::new(path).exists())
                .and_then(|path| {
                    let stamp = std::fs::metadata(&path)
                        .ok()
                        .and_then(|metadata| metadata.modified().ok())
                        .and_then(|modified| modified.duration_since(std::time::UNIX_EPOCH).ok())
                        .map(|duration| duration.as_nanos() as u64)
                        .unwrap_or(0);
                    let key = format!("{path}:{stamp}");
                    if let Some((cached_stamp, handle)) = self.asset_thumbnail_cache.get(&key) {
                        if *cached_stamp == stamp {
                            return handle.clone();
                        }
                    }
                    let handle = engine.load_texture_thumbnail(&path, 96).ok().map(
                        |(width, height, rgba)| {
                            iced::widget::image::Handle::from_rgba(width, height, rgba)
                        },
                    );
                    self.asset_thumbnail_cache
                        .insert(key, (stamp, handle.clone()));
                    handle
                });
            self.asset_paths.push(absolute);
            self.asset_thumbnails.push(thumbnail);
            self.asset_tints.push(tint);
        }
    }

    /// Moves the dragged asset into the folder it was released on.
    fn move_asset_into_folder(&mut self, source: usize, folder: usize) {
        let (Some(from), Some(to)) = (self.assets.get(source), self.assets.get(folder)) else {
            return;
        };
        if !to.is_directory {
            return;
        }
        let (from, to) = (from.relative_path.clone(), to.relative_path.clone());
        let Some(root) = self.asset_root_dir() else {
            return;
        };
        match asset_files::move_into(&root, &from, &to) {
            Ok(path) => {
                self.log(
                    LogLevel::Action,
                    format!("Moved {from} to {path} (references to it are not rewritten)"),
                );
                self.asset_selected = None;
                self.refresh_assets();
            }
            Err(error) => self.log(LogLevel::Error, format!("Move failed: {error}")),
        }
    }

    /// The Assets pane's file bar: new folder, rename and delete (to the trash).
    fn asset_file_action(&mut self, action: panels::assetmanager::FileAction) {
        use panels::assetmanager::{FileAction, FileEdit};
        let selected = self
            .asset_selected
            .and_then(|index| self.assets.get(index))
            .map(|asset| (asset.relative_path.clone(), asset.name.clone()));
        match action {
            FileAction::NewFolder => self.asset_edit = Some(FileEdit::NewFolder(String::new())),
            FileAction::Rename => {
                if let Some((path, name)) = selected {
                    self.asset_edit = Some(FileEdit::Rename(path, name));
                }
            }
            FileAction::Delete => {
                if let Some((_, name)) = selected {
                    self.asset_edit = Some(FileEdit::ConfirmDelete(name));
                }
            }
            FileAction::Text(text) => {
                if let Some(FileEdit::NewFolder(value) | FileEdit::Rename(_, value)) =
                    self.asset_edit.as_mut()
                {
                    *value = text;
                }
            }
            FileAction::Cancel => self.asset_edit = None,
            FileAction::Commit | FileAction::ConfirmDelete => {
                let Some(root) = self.asset_root_dir() else {
                    self.log(LogLevel::Error, "Open a project first");
                    return;
                };
                let Some(edit) = self.asset_edit.clone() else {
                    return;
                };
                let directory = panels::assetmanager::normalize_directory(&self.asset_directory);
                let result = match (&edit, &action) {
                    (FileEdit::NewFolder(name), FileAction::Commit) => {
                        asset_files::create_folder(&root, &directory, name)
                            .map(|path| format!("Created folder {path}"))
                    }
                    (FileEdit::Rename(path, name), FileAction::Commit) => {
                        asset_files::rename(&root, path, name).map(|new| {
                            format!("Renamed {path} to {new} (references to it are not rewritten)")
                        })
                    }
                    (FileEdit::ConfirmDelete(_), FileAction::ConfirmDelete) => selected
                        .ok_or_else(|| "Nothing is selected".to_owned())
                        .and_then(|(path, _)| {
                            asset_files::trash(&root, &path)
                                .map(|moved| format!("Moved {path} to {moved}"))
                        }),
                    _ => return,
                };
                match result {
                    Ok(message) => {
                        self.log(LogLevel::Action, message);
                        self.asset_edit = None;
                        self.asset_selected = None;
                        self.refresh_assets();
                    }
                    // The editor stays open so the name can be fixed.
                    Err(error) => self.log(LogLevel::Error, error),
                }
            }
        }
    }

    fn refresh_assets(&mut self) {
        self.nature.thumb_cache.borrow_mut().clear();
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.refresh_assets();
            self.assets = engine.assets();
            // Deleted assets wait in the hidden trash folder: not part of the project listing.
            self.assets.retain(|asset| {
                asset.relative_path != asset_files::TRASH
                    && !asset.relative_path.starts_with(".trash/")
            });
        }
        if self
            .asset_selected
            .is_some_and(|index| index >= self.assets.len())
        {
            self.asset_selected = None;
        }
        self.refresh_asset_cache();
    }

    fn refresh_recent_projects(&mut self) {
        self.project.recent = project::recent_projects_at(&self.project_recent_file);
        if self
            .project
            .selected_recent
            .is_some_and(|index| index >= self.project.recent.len())
        {
            self.project.selected_recent = None;
        }
    }

    fn handle_project(&mut self, edit: project_manager::Edit) {
        use project_manager::Edit;
        match edit {
            Edit::Open => {
                self.project.open = true;
                self.open_menu = None;
                self.project.error = None;
                self.refresh_recent_projects();
                if self.project.tab == project_manager::Tab::Browse
                    && self.project.browse_results.is_empty()
                {
                    self.scan_project_browse();
                }
            }
            Edit::Close => {
                self.project.open = false;
                self.project.error = None;
            }
            Edit::Tab(tab) => {
                self.project.tab = tab;
                if tab == project_manager::Tab::Recent {
                    self.refresh_recent_projects();
                } else if tab == project_manager::Tab::Browse
                    && self.project.browse_results.is_empty()
                {
                    self.scan_project_browse();
                }
            }
            Edit::Name(value) => {
                self.project.name = value;
                self.project.error = None;
            }
            Edit::Location(value) => {
                self.project.location = value;
                self.project.error = None;
            }
            Edit::Template(index) => {
                self.project.template = project_manager::Template::from_index(index);
            }
            Edit::Create => self.create_project(),
            Edit::CreateAt {
                name,
                location,
                template,
            } => {
                self.project.name = name;
                self.project.location = location;
                if let Some(template) = project_manager::Template::from_name(&template) {
                    self.project.template = template;
                } else {
                    self.project.error = Some(format!("Unknown project template: {template}"));
                    return;
                }
                self.create_project();
            }
            Edit::DefaultLocation => {
                self.project.location = project::default_projects_directory().display().to_string();
                self.project.error = None;
            }
            Edit::DismissError => {
                self.project.error = None;
            }
            Edit::OpenPath(path) => {
                self.open_project_path(std::path::Path::new(&path));
            }
            Edit::RecentSearch(value) => {
                self.project.recent_search = value;
                self.project.selected_recent = None;
            }
            Edit::RecentSelect(index) => {
                if index < self.project.recent.len() {
                    self.project.selected_recent = Some(index);
                }
            }
            Edit::RecentOpen(index) => {
                if let Some(root) = self
                    .project
                    .recent
                    .get(index)
                    .map(|project| project.root.clone())
                {
                    self.open_project_path(&root);
                } else {
                    self.project.error = Some("Select a recent project first".to_owned());
                }
            }
            Edit::BrowsePath(value) => {
                self.project.browse_path = value;
                self.project.error = None;
            }
            Edit::BrowseScan => self.scan_project_browse(),
            Edit::BrowseDefault => {
                self.project.browse_path =
                    project::default_projects_directory().display().to_string();
                self.scan_project_browse();
            }
            Edit::BrowseSearch(value) => {
                self.project.browse_search = value;
                self.project.selected_browse = None;
            }
            Edit::BrowseSelect(index) => {
                if index < self.project.browse_results.len() {
                    self.project.selected_browse = Some(index);
                }
            }
            Edit::BrowseOpen(index) => {
                if let Some(root) = self
                    .project
                    .browse_results
                    .get(index)
                    .map(|project| project.root.clone())
                {
                    self.open_project_path(&root);
                } else {
                    self.project.error = Some("Select a scanned project first".to_owned());
                }
            }
        }
    }

    fn scan_project_browse(&mut self) {
        let raw = self.project.browse_path.trim();
        let path = if raw.is_empty() {
            project::default_projects_directory()
        } else {
            std::path::PathBuf::from(raw)
        };
        if !path.is_dir() {
            self.project.browse_results.clear();
            self.project.selected_browse = None;
            self.project.error = Some(format!("Folder not found: {}", path.display()));
            return;
        }
        self.project.browse_path = path.display().to_string();
        self.project.browse_results = project::scan_projects_recursive(&path);
        self.project.browse_search.clear();
        self.project.selected_browse = None;
        self.project.error = None;
        self.log(
            LogLevel::Action,
            format!(
                "Scanned {} project(s) in {}",
                self.project.browse_results.len(),
                path.display()
            ),
        );
    }

    fn create_project(&mut self) {
        let location_text = self.project.location.trim().to_owned();
        if location_text.is_empty() {
            self.project.error = Some("Choose a location for the project".to_owned());
            return;
        }
        let location = std::path::PathBuf::from(&location_text);
        let name = self.project.name.trim().to_owned();
        match project::create_project(&location, &name, self.project.template.manifest_name()) {
            Ok(info) => {
                project::record_recent_at(&self.project_recent_file, &info.root);
                self.log(LogLevel::Action, format!("Created project {}", info.name));
                self.apply_project(info);
            }
            Err(error) => {
                self.project.error = Some(error);
            }
        }
    }

    fn open_project_path(&mut self, path: &std::path::Path) {
        match project::load_project(path) {
            Ok(info) => {
                project::record_recent_at(&self.project_recent_file, &info.root);
                self.apply_project(info);
            }
            Err(error) => {
                self.project.error = Some(error);
            }
        }
    }

    fn apply_project(&mut self, mut info: project::ProjectInfo) {
        let mut logs: Vec<(LogLevel, String)> = Vec::new();
        let mut entities = Vec::new();
        let mut assets = Vec::new();
        if let Some(engine) = self.engine.as_mut() {
            if let Some(scene) = info.scene_path() {
                let scene_text = scene.to_string_lossy().to_string();
                match engine.load_scene(&scene_text) {
                    Ok(()) => logs.push((LogLevel::Engine, format!("Loaded {scene_text}"))),
                    Err(_) => logs.push((LogLevel::Error, format!("Could not load {scene_text}"))),
                }
            } else {
                let ids: Vec<u32> = (0..engine.entity_count())
                    .filter_map(|index| engine.entity_at(index).ok().map(|entity| entity.id))
                    .collect();
                for id in ids {
                    let _ = engine.destroy_entity(id);
                }
                let scene = info.root.join("scenes").join("main.caf");
                if let Some(parent) = scene.parent() {
                    let _ = std::fs::create_dir_all(parent);
                }
                let scene_text = scene.to_string_lossy().to_string();
                match engine.save_scene(&scene_text) {
                    Ok(()) => {
                        if let Ok(relative) = scene.strip_prefix(&info.root) {
                            let relative = relative.to_string_lossy().replace('\\', "/");
                            match project::update_last_scene(&info.root, &relative) {
                                Ok(()) => info.last_scene = relative,
                                Err(error) => logs.push((
                                    LogLevel::Error,
                                    format!("Could not update project.caffeine: {error}"),
                                )),
                            }
                        }
                        // Load the empty scene so the engine also learns the
                        // project root used by script import resolution.
                        let _ = engine.load_scene(&scene_text);
                        logs.push((LogLevel::Engine, format!("Created {scene_text}")));
                    }
                    Err(_) => {
                        logs.push((LogLevel::Error, format!("Could not create {scene_text}")))
                    }
                }
            }
            // Loading a scene derives `assets/raw` from the project root; set
            // the manifest's custom path afterwards so it always wins.
            let asset_root = info.asset_root();
            let asset_root_text = asset_root.to_string_lossy().to_string();
            if let Err(error) = engine.set_asset_root(&asset_root_text) {
                logs.push((
                    LogLevel::Error,
                    format!("Could not use asset root {asset_root_text}: {error:?}"),
                ));
            } else {
                logs.push((LogLevel::Engine, format!("Asset root {asset_root_text}")));
            }
            entities = projectloader::read_entities(engine);
            assets = engine.assets();
        } else {
            logs.push((
                LogLevel::Ui,
                format!("Opened {} without an engine session", info.name),
            ));
        }
        for (level, message) in logs {
            self.log(level, message);
        }

        self.entities = entities;
        self.assets = assets;
        self.selected = 0;
        self.name_input = self
            .entities
            .first()
            .map(|entity| entity.name.clone())
            .unwrap_or_default();
        self.scene_label = info
            .scene_path()
            .and_then(|path| {
                path.file_name()
                    .map(|name| name.to_string_lossy().into_owned())
            })
            .unwrap_or_else(|| "Scene".to_owned());
        self.asset_directory.clear();
        self.asset_search.clear();
        self.asset_selected = None;
        self.hierarchy_search.clear();
        self.hierarchy_menu = None;
        self.collapsed_hierarchy.clear();
        self.collapsed_components.clear();
        self.prefab_open = false;
        self.generator_selected = None;
        self.generator_link = None;
        self.generator_search.clear();
        self.material = None;
        self.selected_material = None;
        self.selected_material_thumbnail = None;
        self.material_preview = None;
        self.material_last_preview = None;
        self.texture_set = None;
        self.texture_set_path = None;
        self.texture_set_preview = None;
        self.generator_preview = None;
        self.generator_live_job = None;
        self.viewport_frame = None;
        self.viewport_frame_camera = None;
        self.viewport_pending_camera = None;
        self.camera = doppio_engine::Camera::default();
        self.project.active = Some(info.clone());
        self.project.open = false;
        self.project.error = None;
        self.refresh_recent_projects();
        self.refresh_asset_cache();
        self.refresh_components();
        let initial = self.capture_state();
        self.history = History::new(initial);
        self.log(LogLevel::Action, format!("Opened project {}", info.name));
    }

    fn update_project_scene(&mut self, scene: &str) {
        let Some(active) = self.project.active.as_ref() else {
            return;
        };
        let Ok(relative) = std::path::Path::new(scene).strip_prefix(&active.root) else {
            return;
        };
        let relative = relative.to_string_lossy().replace('\\', "/");
        let root = active.root.clone();
        match project::update_last_scene(&root, &relative) {
            Ok(()) => {
                if let Some(active) = self.project.active.as_mut() {
                    active.last_scene = relative;
                }
            }
            Err(error) => self.log(
                LogLevel::Error,
                format!("Could not update project.caffeine: {error}"),
            ),
        }
    }

    fn show_script_pane(&mut self) {
        self.panes.restore();
        let mut target = None;
        for (pane, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::Script)
            {
                group.active = index;
                target = Some(*pane);
            }
        }
        if target.is_none() {
            for (pane, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Viewport) {
                    group.pages.push(PaneKind::Script);
                    group.active = group.pages.len() - 1;
                    target = Some(*pane);
                    break;
                }
            }
        }
        if target.is_none() {
            if let Some((pane, group)) = self.panes.iter_mut().next() {
                group.pages.push(PaneKind::Script);
                group.active = group.pages.len() - 1;
                target = Some(*pane);
            }
        }
        if let Some(pane) = target {
            self.panes.maximize(pane);
        }
    }

    fn show_dryad_pane(&mut self) {
        self.panes.restore();
        let mut target = None;
        for (pane, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::DryadNodes)
            {
                group.active = index;
                target = Some(*pane);
            }
        }
        if target.is_none() {
            for (pane, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Viewport) {
                    group.pages.push(PaneKind::DryadNodes);
                    group.active = group.pages.len() - 1;
                    target = Some(*pane);
                    break;
                }
            }
        }
        if target.is_none() {
            if let Some((pane, group)) = self.panes.iter_mut().next() {
                group.pages.push(PaneKind::DryadNodes);
                group.active = group.pages.len() - 1;
                target = Some(*pane);
            }
        }
        if let Some(pane) = target {
            self.panes.maximize(pane);
        }
    }

    fn resolve_script_path(&self, path: &str) -> (String, String) {
        let Some(engine) = self.engine.as_ref() else {
            return (path.to_owned(), path.to_owned());
        };
        let absolute = engine
            .resolve_path(path)
            .ok()
            .filter(|resolved| std::path::Path::new(resolved).exists())
            .or_else(|| {
                let scene = engine.scene_path()?;
                let scene = std::path::Path::new(&scene);
                let parent = scene.parent()?;
                let root = if parent.file_name().and_then(|name| name.to_str()) == Some("scenes") {
                    parent.parent()?
                } else {
                    parent
                };
                let candidate = root.join(path);
                candidate
                    .exists()
                    .then(|| candidate.to_string_lossy().into_owned())
            })
            .or_else(|| engine.asset_path(path).ok())
            .unwrap_or_else(|| path.to_owned());
        let relative = engine
            .asset_project_path(path)
            .ok()
            .filter(|relative| !relative.is_empty())
            .unwrap_or_else(|| path.to_owned());
        (absolute, relative)
    }

    fn open_dryad_path(&mut self, path: &str) {
        let (absolute, relative) = self.resolve_script_path(path);
        match self
            .dryad
            .open(std::path::Path::new(&absolute), Some(relative.clone()))
        {
            Ok(()) => {
                let _ = self
                    .script
                    .open(std::path::Path::new(&absolute), Some(relative));
                self.show_dryad_pane();
                self.log(LogLevel::Action, format!("Opened Dryad graph {absolute}"));
            }
            Err(error) => self.log(LogLevel::Error, error),
        }
    }

    fn open_dryad_asset(&mut self, index: usize) {
        let Some(asset) = self.assets.get(index).cloned() else {
            return;
        };
        let absolute = self
            .asset_paths
            .get(index)
            .cloned()
            .or_else(|| {
                self.engine
                    .as_ref()
                    .and_then(|engine| engine.asset_path(&asset.relative_path).ok())
            })
            .unwrap_or_else(|| asset.relative_path.clone());
        let relative = self
            .engine
            .as_ref()
            .and_then(|engine| engine.asset_project_path(&asset.relative_path).ok())
            .filter(|relative| !relative.is_empty())
            .unwrap_or_else(|| asset.relative_path.clone());
        match self
            .dryad
            .open(std::path::Path::new(&absolute), Some(relative.clone()))
        {
            Ok(()) => {
                let _ = self
                    .script
                    .open(std::path::Path::new(&absolute), Some(relative));
                self.show_dryad_pane();
                self.log(
                    LogLevel::Action,
                    format!("Opened Dryad graph {}", asset.relative_path),
                );
            }
            Err(error) => self.log(LogLevel::Error, error),
        }
    }

    fn handle_dryad_canvas(&mut self, event: dryad_nodes::CanvasEvent) {
        use dryad_nodes::CanvasEvent;
        match event {
            CanvasEvent::Select(selected) => {
                self.dryad.selected = selected;
                self.dryad.selected_link = None;
            }
            CanvasEvent::SelectLink(link) => {
                self.dryad.selected_link = link;
                self.dryad.selected = None;
            }
            CanvasEvent::Detach { to } => {
                if let Some(graph) = self.dryad.graph.as_mut() {
                    graph.disconnect_input(&to);
                    self.dryad.dirty = true;
                    self.dryad.status = Some("Removed input wire.".to_owned());
                }
            }
            CanvasEvent::MoveNode { id, delta } => {
                self.dryad.move_node(&id, delta.x, delta.y);
            }
            CanvasEvent::Connect { from, to } => {
                if let Err(error) = self.dryad.connect(&from, &to) {
                    self.log(LogLevel::Error, error);
                }
            }
            CanvasEvent::Viewport { pan, zoom } => {
                self.dryad.pan = pan;
                self.dryad.zoom = zoom;
            }
        }
    }

    fn run_dryad_preview(&mut self) {
        let Some(path) = self.dryad.script_path.clone() else {
            self.dryad.status = Some("Open a Dryad script first.".to_owned());
            return;
        };
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            self.dryad.status = Some("Select an entity to run the script on.".to_owned());
            return;
        };
        let Some(engine) = self.engine.as_mut() else {
            self.dryad.status = Some("No engine session for a Dryad preview.".to_owned());
            return;
        };
        if !engine.dryad_available() {
            self.dryad.status = Some("This build has no Dryad interpreter.".to_owned());
            return;
        }
        let script = path.to_string_lossy().into_owned();
        match engine.run_dryad(entity, &script, 1.0 / 60.0) {
            Ok(()) => {
                self.dryad.status = Some("Ran one preview frame.".to_owned());
                self.refresh_components();
                self.commit("Run Dryad Preview", None);
                self.log(LogLevel::Action, "Ran Dryad preview");
            }
            Err(error) => {
                let message = format!("Dryad preview failed: {error:?}");
                self.dryad.status = Some(message.clone());
                self.log(LogLevel::Error, message);
            }
        }
    }

    fn open_script_path(&mut self, path: &str) {
        let (absolute, relative) = self.resolve_script_path(path);
        self.open_script_file(std::path::Path::new(&absolute), Some(relative));
    }

    fn open_script_file(&mut self, path: &std::path::Path, relative: Option<String>) {
        let title = path
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("script")
            .to_owned();
        match self.script.open(path, relative) {
            Ok(_) => {
                self.show_script_pane();
                self.log(LogLevel::Action, format!("Opened script {title}"));
            }
            Err(error) => self.log(LogLevel::Error, error),
        }
    }

    fn open_script_asset(&mut self, index: usize) {
        let Some(asset) = self.assets.get(index).cloned() else {
            return;
        };
        let absolute = self
            .asset_paths
            .get(index)
            .cloned()
            .or_else(|| {
                self.engine
                    .as_ref()
                    .and_then(|engine| engine.asset_path(&asset.relative_path).ok())
            })
            .unwrap_or_else(|| asset.relative_path.clone());
        let relative = self
            .engine
            .as_ref()
            .and_then(|engine| engine.asset_project_path(&asset.relative_path).ok())
            .filter(|relative| !relative.is_empty())
            .unwrap_or_else(|| asset.relative_path.clone());
        self.open_script_file(std::path::Path::new(&absolute), Some(relative));
    }

    fn attach_active_script(&mut self) {
        let Some(document) = self.script.current() else {
            self.log(LogLevel::Error, "Open a script before attaching it");
            return;
        };
        let relative = if document.relative_path.is_empty() {
            document.path.display().to_string()
        } else {
            document.relative_path.clone()
        };
        self.attach_script_path(&relative);
    }

    /// Attaches whatever the "Add Behaviour" picker chose to the selected entity.
    fn add_behaviour(&mut self, picked: behaviour::Behaviour) {
        match picked {
            behaviour::Behaviour::Component(type_id) => self.add_component(type_id),
            behaviour::Behaviour::Script(path) => self.attach_script_path(&path),
            behaviour::Behaviour::Mesh(path) => self.add_mesh_behaviour(&path),
            behaviour::Behaviour::Material(path) => self.add_material_behaviour(&path),
        }
    }

    fn add_mesh_behaviour(&mut self, relative: &str) {
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            self.log(LogLevel::Error, "Select an entity before adding a mesh");
            return;
        };
        let Some(engine) = self.engine.as_mut() else {
            self.log(LogLevel::Error, "No engine session to add a mesh");
            return;
        };
        let type_id = engine
            .entity_components(entity)
            .iter()
            .find(|component| component.name == "Mesh Renderer")
            .map(|component| component.type_id)
            .or_else(|| add_component(engine, entity, "Mesh Renderer").ok());
        let Some(type_id) = type_id else {
            self.log(LogLevel::Error, "Mesh Renderer is not available");
            return;
        };
        if let Err(error) = set_component_field(
            engine,
            entity,
            type_id,
            "Mesh",
            doppio_engine::FieldValue::String(relative.to_owned()),
        ) {
            self.log(LogLevel::Error, format!("Could not set the mesh: {error}"));
            return;
        }
        self.refresh_components();
        self.commit("Add Mesh", None);
        self.log(LogLevel::Action, format!("Added mesh {relative}"));
    }

    fn add_material_behaviour(&mut self, relative: &str) {
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            self.log(LogLevel::Error, "Select an entity before adding a material");
            return;
        };
        let Some(engine) = self.engine.as_mut() else {
            self.log(LogLevel::Error, "No engine session to assign a material");
            return;
        };
        match engine.apply_material(entity, relative) {
            Ok(()) => {
                self.refresh_components();
                self.commit("Assign Material", None);
                self.log(LogLevel::Action, format!("Assigned {relative}"));
            }
            Err(_) => self.log(LogLevel::Error, format!("Could not assign {relative}")),
        }
    }

    fn attach_script_path(&mut self, relative: &str) {
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            self.log(
                LogLevel::Error,
                "Select an entity before attaching a script",
            );
            return;
        };
        let Some(engine) = self.engine.as_mut() else {
            self.log(LogLevel::Error, "No engine session to attach a script");
            return;
        };
        let type_id = engine
            .entity_components(entity)
            .iter()
            .find(|component| component.name == "Script")
            .map(|component| component.type_id)
            .or_else(|| add_component(engine, entity, "Script").ok());
        let Some(type_id) = type_id else {
            self.log(LogLevel::Error, "Script component is not available");
            return;
        };
        if let Err(error) = set_component_field(
            engine,
            entity,
            type_id,
            "Script",
            doppio_engine::FieldValue::String(relative.to_owned()),
        ) {
            self.log(LogLevel::Error, format!("Could not attach script: {error}"));
            return;
        }
        self.refresh_components();
        self.commit("Attach Script", None);
        self.log(LogLevel::Action, format!("Attached script {relative}"));
    }

    fn detach_script(&mut self) {
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        let Some(engine) = self.engine.as_mut() else {
            return;
        };
        let Some(type_id) = engine
            .entity_components(entity)
            .iter()
            .find(|component| component.name == "Script")
            .map(|component| component.type_id)
        else {
            return;
        };
        if set_component_field(
            engine,
            entity,
            type_id,
            "Script",
            doppio_engine::FieldValue::String(String::new()),
        )
        .is_ok()
        {
            self.refresh_components();
            self.commit("Detach Script", None);
            self.log(LogLevel::Action, "Detached script");
        }
    }

    fn show_texture_set_pane(&mut self) {
        if self.texture_set.is_none() {
            self.new_texture_set();
        }
        self.panes.restore();
        let mut target = None;
        for (pane, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::TextureSet)
            {
                group.active = index;
                target = Some(*pane);
            }
        }
        if target.is_none() {
            for (pane, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Material) {
                    group.pages.push(PaneKind::TextureSet);
                    group.active = group.pages.len() - 1;
                    target = Some(*pane);
                    break;
                }
            }
        }
        if target.is_none() {
            if let Some((pane, group)) = self.panes.iter_mut().next() {
                group.pages.push(PaneKind::TextureSet);
                group.active = group.pages.len() - 1;
                target = Some(*pane);
            }
        }
        if let Some(pane) = target {
            self.panes.maximize(pane);
        }
    }

    fn new_texture_set(&mut self) {
        self.texture_set = Some(texture_set::TextureSet::default());
        self.texture_set_history = texture_set::TileHistory::default();
        self.texture_set_path = None;
        self.texture_set_dirty = true;
        self.texture_set_paint_mode = false;
        self.texture_set_painting = false;
        self.texture_set_cursor = None;
        self.texture_set_status = Some("New texture set".to_owned());
        self.refresh_texture_set_preview();
        self.show_texture_set_pane();
        self.log(LogLevel::Action, "New texture set");
    }

    fn refresh_texture_set_preview(&mut self) {
        let Some(set) = self.texture_set.as_ref() else {
            self.texture_set_preview = None;
            return;
        };
        let pixels = set.composite(set.active_tile, texture_set::CHANNEL_BASE);
        self.texture_set_preview = Some(iced::widget::image::Handle::from_rgba(
            set.tile_size,
            set.tile_size,
            pixels,
        ));
    }

    fn texture_set_tile_for_uv(&self, uv: [f32; 2]) -> (u32, [f32; 2]) {
        let mut tile = self
            .texture_set
            .as_ref()
            .map(|set| set.active_tile)
            .unwrap_or(texture_set::MIN_UDIM);
        let mut local = uv;
        if uv[0] >= 1.0 {
            tile = texture_set::MIN_UDIM + uv[0].floor() as u32;
            local[0] = uv[0].fract();
        } else if uv[0] < 0.0 {
            local[0] = uv[0].rem_euclid(1.0);
            tile = texture_set::MIN_UDIM;
        }
        local[1] = local[1].clamp(0.0, 1.0);
        (
            tile.clamp(texture_set::MIN_UDIM, texture_set::MAX_UDIM),
            local,
        )
    }

    fn paint_texture_set(&mut self, uv: [f32; 2], start: bool) -> bool {
        let (tile, local) = self.texture_set_tile_for_uv(uv);
        let Some(set) = self.texture_set.as_mut() else {
            return false;
        };
        if start || !self.texture_set_painting {
            self.texture_set_stroke_snapshot = Some(set.snapshot_tile(tile));
            self.texture_set_painting = true;
            set.active_tile = tile;
        }
        let changed = set.paint(tile, local);
        if changed {
            self.texture_set_dirty = true;
            self.refresh_texture_set_preview();
        }
        changed
    }

    fn pick_texture_uv(&self, point: Point) -> Option<(f32, f32)> {
        let engine = self.engine.as_ref()?;
        let entity = self.entities.get(self.selected)?.id;
        let (width, height) = self.viewport_logical;
        if width <= 0.0 || height <= 0.0 {
            return None;
        }
        engine
            .pick_uv(entity, point.x, point.y, width, height)
            .ok()
            .flatten()
    }

    fn bake_texture_set_mesh_map(&mut self, kind: doppio_engine::MeshMapKind, channel: &str) {
        let Some(engine) = self.engine.as_ref() else {
            self.texture_set_status = Some("Mesh-map bake: no engine".to_owned());
            return;
        };
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            self.texture_set_status = Some("Mesh-map bake: select a mesh entity first".to_owned());
            return;
        };
        let (tile, size) = match self.texture_set.as_ref() {
            Some(set) => (set.active_tile, set.tile_size),
            None => return,
        };
        let path = std::env::temp_dir().join(format!(
            "doppio-meshmap-{}-{}-{}.png",
            std::process::id(),
            channel,
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|duration| duration.as_nanos())
                .unwrap_or_default()
        ));
        if engine
            .bake_mesh_map(entity, kind, size, &path.to_string_lossy())
            .is_err()
        {
            self.texture_set_status = Some(format!("Could not bake {channel} mesh map"));
            return;
        }
        match generators::decode_texture(&path.to_string_lossy()) {
            Ok(data) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.import_rgba_channel(tile, channel, &data);
                    self.texture_set_dirty = true;
                    self.texture_set_status = Some(format!("Baked {channel} into tile {tile}"));
                }
                self.refresh_texture_set_preview();
            }
            Err(error) => {
                self.texture_set_status = Some(format!("Mesh map read: {error}"));
            }
        }
        let _ = std::fs::remove_file(path);
    }

    fn bake_texture_set_scene_ao(&mut self) {
        let Some(engine) = self.engine.as_ref() else {
            self.texture_set_status = Some("Scene AO: no engine".to_owned());
            return;
        };
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            self.texture_set_status = Some("Scene AO: select a mesh entity first".to_owned());
            return;
        };
        let (tile, size) = match self.texture_set.as_ref() {
            Some(set) => (set.active_tile, set.tile_size),
            None => return,
        };
        match engine.bake_ao(entity, size, 16, 2.5, "processed/ao") {
            Ok(result) if result.ok => {
                let resolved = engine
                    .resolve_path(&result.orm_path)
                    .unwrap_or_else(|_| result.orm_path.clone());
                match generators::decode_texture(&resolved) {
                    Ok(data) => {
                        if let Some(set) = self.texture_set.as_mut() {
                            set.import_channel(tile, texture_set::CHANNEL_AO, &data, 0);
                            self.texture_set_dirty = true;
                            self.texture_set_status =
                                Some(format!("Scene AO baked into tile {tile}"));
                        }
                        self.refresh_texture_set_preview();
                    }
                    Err(error) => {
                        self.texture_set_status = Some(format!("Scene AO read: {error}"));
                    }
                }
            }
            Ok(result) => {
                self.texture_set_status = Some(if result.error.is_empty() {
                    "Scene AO failed".to_owned()
                } else {
                    result.error
                });
            }
            Err(_) => self.texture_set_status = Some("Scene AO failed".to_owned()),
        }
    }

    fn finish_texture_set_stroke(&mut self) {
        if let Some(snapshot) = self.texture_set_stroke_snapshot.take() {
            self.texture_set_history.push(snapshot);
        }
        self.texture_set_painting = false;
        self.apply_texture_set_to_selected();
        self.refresh_texture_set_preview();
    }

    fn apply_texture_set_to_selected(&mut self) {
        let sets_folder = self
            .asset_root_dir()
            .map(|root| asset_folders::resolve(&root, asset_folders::AssetFolder::TextureSets));
        let Some(engine) = self.engine.as_mut() else {
            return;
        };
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        let Some(set) = self.texture_set.as_ref() else {
            return;
        };
        let stem = sanitize_material_name(&set.name);
        let directory = self
            .texture_set_path
            .as_ref()
            .and_then(|path| path.parent().map(std::path::Path::to_path_buf))
            .or(sets_folder)
            .unwrap_or_default();
        if std::fs::create_dir_all(&directory).is_err() {
            return;
        }
        let base_path = directory.join(format!("{stem}_baseColor.png"));
        let pixels = set.composite(set.active_tile, texture_set::CHANNEL_BASE);
        if engine
            .save_texture_rgba(
                &base_path.to_string_lossy(),
                set.tile_size,
                set.tile_size,
                &pixels,
            )
            .is_err()
        {
            return;
        }
        let material_path = directory.join(format!("{stem}_paint.mat"));
        let mut material = engine.new_material().unwrap_or_default();
        material.name = set.name.clone();
        material.albedo_map = engine
            .asset_project_path(&base_path.to_string_lossy())
            .unwrap_or_else(|_| base_path.to_string_lossy().into_owned());
        if engine
            .save_material(&material_path.to_string_lossy(), &material)
            .is_err()
        {
            return;
        }
        let _ = engine.apply_material(entity, &material_path.to_string_lossy());
    }

    fn open_texture_set(&mut self, path: &str) {
        let resolved = self
            .engine
            .as_ref()
            .and_then(|engine| {
                engine
                    .resolve_path(path)
                    .ok()
                    .filter(|resolved| std::path::Path::new(resolved).exists())
                    .or_else(|| engine.asset_path(path).ok())
            })
            .unwrap_or_else(|| path.to_owned());
        match texture_set::TextureSet::load(std::path::Path::new(&resolved)) {
            Ok(set) => {
                self.texture_set = Some(set);
                self.texture_set_history = texture_set::TileHistory::default();
                self.texture_set_path = Some(std::path::PathBuf::from(&resolved));
                self.texture_set_dirty = false;
                self.texture_set_status = Some(format!("Opened {resolved}"));
                self.texture_set_paint_mode = false;
                self.texture_set_painting = false;
                self.texture_set_cursor = None;
                self.refresh_texture_set_preview();
                self.show_texture_set_pane();
                self.log(LogLevel::Action, format!("Opened texture set {resolved}"));
            }
            Err(error) => {
                self.texture_set_status = Some(format!("Could not open texture set: {error}"));
                self.log(LogLevel::Error, format!("Texture set: {error}"));
            }
        }
    }

    fn save_texture_set(&mut self) {
        let Some(set) = self.texture_set.as_ref() else {
            return;
        };
        let stem = sanitize_material_name(&set.name);
        let path = if let Some(path) = self.texture_set_path.clone() {
            path
        } else {
            let Some(engine) = self.engine.as_ref() else {
                self.texture_set_status = Some("Save texture set: no engine".to_owned());
                return;
            };
            engine
                .asset_path(&format!("texture_sets/{stem}.texset"))
                .map(std::path::PathBuf::from)
                .unwrap_or_else(|_| std::path::PathBuf::from(format!("texture_sets/{stem}.texset")))
        };
        match set.save(&path) {
            Ok(()) => {
                self.texture_set_path = Some(path.clone());
                self.texture_set_dirty = false;
                self.texture_set_status = Some(format!("Saved {}", path.display()));
                self.log(
                    LogLevel::Action,
                    format!("Saved texture set {}", path.display()),
                );
            }
            Err(error) => {
                self.texture_set_status = Some(format!("Could not save texture set: {error}"));
                self.log(LogLevel::Error, format!("Texture set: {error}"));
            }
        }
    }

    fn edit_texture_set(&mut self, edit: texture_set_panel::Edit) {
        match edit {
            texture_set_panel::Edit::New => self.new_texture_set(),
            texture_set_panel::Edit::Save => self.save_texture_set(),
            texture_set_panel::Edit::SelectTile(tile) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.active_tile = tile;
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::AddTile => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.add_tile();
                    self.texture_set_dirty = true;
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::SelectLayer(index) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.active_layer = index;
                }
            }
            texture_set_panel::Edit::AddPaintLayer => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.add_paint_layer();
                    self.texture_set_dirty = true;
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::AddFillLayer => {
                if let Some(set) = self.texture_set.as_mut() {
                    let color = set.brush.color;
                    set.add_fill_layer(color);
                    self.texture_set_dirty = true;
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::DeleteLayer => {
                if let Some(set) = self.texture_set.as_mut() {
                    let index = set.active_layer;
                    set.remove_layer(index);
                    self.texture_set_dirty = true;
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::MoveLayer(delta) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.move_layer(set.active_layer, delta);
                    self.texture_set_dirty = true;
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::SetVisible(index, visible) => {
                if let Some(layer) = self
                    .texture_set
                    .as_mut()
                    .and_then(|set| set.layers.get_mut(index))
                {
                    layer.visible = visible;
                    self.texture_set_dirty = true;
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::SetOpacity(index, opacity) => {
                if let Some(layer) = self
                    .texture_set
                    .as_mut()
                    .and_then(|set| set.layers.get_mut(index))
                {
                    layer.opacity = opacity.clamp(0.0, 1.0);
                    self.texture_set_dirty = true;
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::SetBlend(index, blend) => {
                if let Some(layer) = self
                    .texture_set
                    .as_mut()
                    .and_then(|set| set.layers.get_mut(index))
                {
                    layer.blend = generators::BlendMode::from_index(blend);
                    self.texture_set_dirty = true;
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::SetChannel(index) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.active_channel = index;
                }
            }
            texture_set_panel::Edit::SetBrushRadius(value) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.brush.radius = value.clamp(1.0, 128.0);
                }
            }
            texture_set_panel::Edit::SetBrushHardness(value) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.brush.hardness = value.clamp(0.0, 0.99);
                }
            }
            texture_set_panel::Edit::SetBrushOpacity(value) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.brush.opacity = value.clamp(0.0, 1.0);
                }
            }
            texture_set_panel::Edit::SetBrushColor(axis, value) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.brush.color[axis.min(3)] = value.clamp(0.0, 1.0);
                }
            }
            texture_set_panel::Edit::SetPaintMask(value) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.paint_mask = value;
                }
            }
            texture_set_panel::Edit::TogglePaintMode => {
                self.texture_set_paint_mode = !self.texture_set_paint_mode;
                if self.texture_set_paint_mode {
                    self.panes.restore();
                }
                self.texture_set_status = Some(if self.texture_set_paint_mode {
                    "3D paint on: drag on the selected mesh".to_owned()
                } else {
                    "3D paint off".to_owned()
                });
            }
            texture_set_panel::Edit::SetName(name) => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.name = name;
                    self.texture_set_dirty = true;
                }
            }
            texture_set_panel::Edit::Undo => {
                let restored = self
                    .texture_set
                    .as_mut()
                    .is_some_and(|set| self.texture_set_history.undo(set));
                if restored {
                    self.refresh_texture_set_preview();
                }
            }
            texture_set_panel::Edit::Redo => {
                let restored = self
                    .texture_set
                    .as_mut()
                    .is_some_and(|set| self.texture_set_history.redo(set));
                if restored {
                    self.refresh_texture_set_preview();
                }
            }
            texture_set_panel::Edit::SetExportPreset(index) => {
                self.texture_set_export_preset = index;
            }
            texture_set_panel::Edit::Export => self.export_texture_set_to(None),
            texture_set_panel::Edit::ExportTo(path) => self.export_texture_set_to(Some(&path)),
            texture_set_panel::Edit::BakeHeightAo => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.bake_from_height(texture_set::CHANNEL_AO, false, 8.0, 1.0);
                    self.texture_set_dirty = true;
                    self.texture_set_status = Some("Baked AO from height".to_owned());
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::BakeCurvature => {
                if let Some(set) = self.texture_set.as_mut() {
                    set.bake_from_height(texture_set::CHANNEL_CURVATURE, true, 8.0, 1.0);
                    self.texture_set_dirty = true;
                    self.texture_set_status = Some("Baked curvature from height".to_owned());
                }
                self.refresh_texture_set_preview();
            }
            texture_set_panel::Edit::BakeSceneAo => self.bake_texture_set_scene_ao(),
            texture_set_panel::Edit::BakeMeshNormal => self.bake_texture_set_mesh_map(
                doppio_engine::MeshMapKind::Normal,
                texture_set::CHANNEL_NORMAL,
            ),
            texture_set_panel::Edit::BakeMeshPosition => self.bake_texture_set_mesh_map(
                doppio_engine::MeshMapKind::Position,
                texture_set::CHANNEL_POSITION,
            ),
            texture_set_panel::Edit::BakeMeshObjectId => self.bake_texture_set_mesh_map(
                doppio_engine::MeshMapKind::ObjectId,
                texture_set::CHANNEL_OBJECT_ID,
            ),
            texture_set_panel::Edit::PaintStart(uv) => {
                self.paint_texture_set(uv, true);
            }
            texture_set_panel::Edit::PaintMove(uv) => {
                self.paint_texture_set(uv, false);
            }
            texture_set_panel::Edit::PaintEnd => self.finish_texture_set_stroke(),
            texture_set_panel::Edit::PaintDab(tile, uv) => {
                if let Some(set) = self.texture_set.as_mut() {
                    let snapshot = set.snapshot_tile(tile);
                    set.active_tile = tile;
                    if set.paint(tile, uv) {
                        self.texture_set_history.push(snapshot);
                        self.texture_set_dirty = true;
                        self.refresh_texture_set_preview();
                    }
                }
            }
            texture_set_panel::Edit::Hover(uv) => {
                self.texture_set_cursor = uv;
            }
        }
    }

    fn export_texture_set_to(&mut self, override_directory: Option<&str>) {
        let Some(set) = self.texture_set.as_ref() else {
            return;
        };
        let presets = texture_set::export_presets();
        let preset = presets
            .get(
                self.texture_set_export_preset
                    .min(presets.len().saturating_sub(1)),
            )
            .cloned();
        let Some(preset) = preset else {
            return;
        };
        let directory = if let Some(directory) = override_directory {
            std::path::PathBuf::from(directory)
        } else {
            self.texture_set_path
                .as_ref()
                .and_then(|path| path.parent().map(std::path::Path::to_path_buf))
                .or_else(|| {
                    self.asset_root_dir().map(|root| {
                        asset_folders::resolve(&root, asset_folders::AssetFolder::TextureSets)
                    })
                })
                .unwrap_or_else(|| std::path::PathBuf::from("Textures/Sets"))
                .join("export")
        };
        let stem = sanitize_material_name(&set.name);
        match set.export(&directory, &preset, &stem) {
            Ok(files) => {
                self.texture_set_status = Some(format!(
                    "Exported {} map(s) to {}",
                    files.len(),
                    directory.display()
                ));
                self.log(
                    LogLevel::Action,
                    format!("Texture set export: {} file(s)", files.len()),
                );
            }
            Err(error) => {
                self.texture_set_status = Some(format!("Export failed: {error}"));
                self.log(LogLevel::Error, format!("Texture set export: {error}"));
            }
        }
    }

    fn show_material_pane(&mut self) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::Material)
            {
                group.active = index;
                found = true;
            }
        }
        if found {
            return;
        }
        for (_, group) in self.panes.iter_mut() {
            if group.active_kind() == PaneKind::Inspector {
                group.pages.push(PaneKind::Material);
                group.active = group.pages.len() - 1;
                return;
            }
        }
        if let Some((_, group)) = self.panes.iter_mut().next() {
            group.pages.push(PaneKind::Material);
            group.active = group.pages.len() - 1;
        }
    }

    fn new_material(&mut self) {
        let surface = self
            .engine
            .as_ref()
            .and_then(|engine| engine.new_material().ok())
            .unwrap_or_default();
        let generator = MaterialGraph::default();
        self.material = Some(MaterialDocument {
            surface,
            asset_path: None,
            file_path: None,
            dirty: true,
            preview_dirty: true,
            source: String::new(),
            generator_history: generators::GraphHistory::new(generator.clone()),
            generator_packing: generators::ChannelPacking::default(),
            generator_bits: 8,
            generator,
            thumbnails: [None, None, None, None],
            source_thumbnail: None,
        });
        self.material_preview = None;
        self.material_last_preview = None;
        self.material_map_details = Default::default();
        self.reset_generator_view();
        self.show_material_pane();
        self.log(LogLevel::Action, "New material");
    }

    fn open_material_asset(&mut self, index: usize) {
        let Some(asset) = self.assets.get(index).cloned() else {
            return;
        };
        let absolute = self
            .asset_paths
            .get(index)
            .cloned()
            .unwrap_or_else(|| asset.relative_path.clone());
        let loaded = self
            .engine
            .as_ref()
            .and_then(|engine| engine.load_material(&absolute).ok());
        let Some(surface) = loaded else {
            self.log(
                LogLevel::Error,
                format!("Could not load material {}", asset.relative_path),
            );
            return;
        };
        self.material_directx = surface.normal_directx;
        let generator =
            std::fs::read_to_string(std::path::Path::new(&absolute).with_extension("mgraph"))
                .ok()
                .and_then(|text| MaterialGraph::from_text(&text))
                .unwrap_or_default();
        self.material = Some(MaterialDocument {
            surface,
            asset_path: Some(asset.relative_path.clone()),
            file_path: Some(absolute),
            dirty: false,
            preview_dirty: true,
            source: String::new(),
            generator_history: generators::GraphHistory::new(generator.clone()),
            generator_packing: generators::ChannelPacking::default(),
            generator_bits: 8,
            generator,
            thumbnails: [None, None, None, None],
            source_thumbnail: None,
        });
        self.material_preview = None;
        self.material_last_preview = None;
        self.material_baking = None;
        self.reset_generator_view();
        self.refresh_material_thumbnails();
        self.show_material_pane();
        self.log(LogLevel::Action, format!("Opened material {}", asset.name));
    }

    fn asset_double_clicked(&mut self, index: usize) {
        let Some(asset) = self.assets.get(index) else {
            return;
        };
        if asset.is_directory {
            let path = asset.relative_path.clone();
            self.asset_directory = panels::assetmanager::normalize_directory(&path);
            self.asset_search.clear();
            self.asset_selected = None;
        } else if asset.kind == "script"
            && !asset.relative_path.to_ascii_lowercase().ends_with(".dyd")
        {
            self.open_script_asset(index);
        } else {
            // `.dyd` opens the Dryad node editor (and its source tab).
            self.asset_pressed(index);
        }
    }

    fn asset_pressed(&mut self, index: usize) {
        let Some(asset) = self.assets.get(index) else {
            return;
        };
        match asset.kind.as_str() {
            "material" => self.open_material_asset(index),
            "image" => self.new_material_from_texture(index),
            "script" => {
                if asset.relative_path.to_ascii_lowercase().ends_with(".dyd") {
                    self.open_dryad_asset(index);
                } else {
                    self.open_script_asset(index);
                }
            }
            _ => self.log(
                LogLevel::Ui,
                format!("{} is a {} asset", asset.name, asset.kind),
            ),
        }
    }

    fn open_material_path(&mut self, path: &str) {
        let Some(engine) = self.engine.as_ref() else {
            return;
        };
        let resolved = engine
            .resolve_path(path)
            .ok()
            .filter(|resolved| std::path::Path::new(resolved).exists());
        let absolute = if let Some(resolved) = resolved {
            resolved
        } else if std::path::Path::new(path).is_absolute() {
            path.to_owned()
        } else {
            engine.asset_path(path).unwrap_or_else(|_| path.to_owned())
        };
        match engine.load_material(&absolute) {
            Ok(surface) => {
                let asset_path = if std::path::Path::new(path).is_absolute() {
                    None
                } else {
                    Some(path.to_owned())
                };
                self.material_directx = surface.normal_directx;
                let generator = std::fs::read_to_string(
                    std::path::Path::new(&absolute).with_extension("mgraph"),
                )
                .ok()
                .and_then(|text| MaterialGraph::from_text(&text))
                .unwrap_or_default();
                self.material = Some(MaterialDocument {
                    surface,
                    asset_path,
                    file_path: Some(absolute),
                    dirty: false,
                    preview_dirty: true,
                    source: String::new(),
                    generator_history: generators::GraphHistory::new(generator.clone()),
                    generator_packing: generators::ChannelPacking::default(),
                    generator_bits: 8,
                    generator,
                    thumbnails: [None, None, None, None],
                    source_thumbnail: None,
                });
                self.material_preview = None;
                self.material_last_preview = None;
                self.reset_generator_view();
                self.refresh_material_thumbnails();
                self.show_material_pane();
                self.log(LogLevel::Action, format!("Opened material {path}"));
            }
            Err(_) => self.log(LogLevel::Error, format!("Could not load material {path}")),
        }
    }

    fn new_material_from_texture(&mut self, index: usize) {
        let Some(asset) = self.assets.get(index).cloned() else {
            return;
        };
        if asset.kind != "image" {
            return;
        }
        let project_path = self
            .engine
            .as_ref()
            .and_then(|engine| engine.asset_project_path(&asset.relative_path).ok())
            .unwrap_or_else(|| asset.relative_path.clone());
        let mut surface = self
            .engine
            .as_ref()
            .and_then(|engine| engine.new_material().ok())
            .unwrap_or_default();
        surface.name = std::path::Path::new(&asset.name)
            .file_stem()
            .and_then(|stem| stem.to_str())
            .unwrap_or("New Material")
            .to_owned();
        surface.albedo_map = project_path;
        let generator = MaterialGraph::default();
        self.material = Some(MaterialDocument {
            surface,
            asset_path: None,
            file_path: None,
            dirty: true,
            preview_dirty: true,
            source: asset.relative_path.clone(),
            generator_history: generators::GraphHistory::new(generator.clone()),
            generator_packing: generators::ChannelPacking::default(),
            generator_bits: 8,
            generator,
            thumbnails: [None, None, None, None],
            source_thumbnail: None,
        });
        self.material_preview = None;
        self.material_last_preview = None;
        self.material_baking = None;
        self.reset_generator_view();
        self.refresh_material_thumbnails();
        self.show_material_pane();
        self.log(
            LogLevel::Action,
            format!("New material from {}", asset.name),
        );
    }

    fn save_material(&mut self) {
        let Some(engine) = self.engine.as_ref() else {
            self.log(LogLevel::Error, "Save material: no engine");
            return;
        };
        let Some(document) = self.material.as_ref() else {
            return;
        };
        let (asset_path, absolute) = if let Some(path) = &document.asset_path {
            (
                path.clone(),
                document.file_path.clone().unwrap_or_else(|| path.clone()),
            )
        } else {
            let file_name = format!("{}.mat", sanitize_material_name(&document.surface.name));
            let relative = format!("materials/{file_name}");
            let absolute = engine
                .asset_path(&relative)
                .unwrap_or_else(|_| relative.clone());
            (relative, absolute)
        };
        let surface = document.surface.clone();
        match engine.save_material(&absolute, &surface) {
            Ok(()) => {
                if let Some(document) = self.material.as_mut() {
                    document.asset_path = Some(asset_path.clone());
                    document.file_path = Some(absolute.clone());
                    document.dirty = false;
                    document.preview_dirty = true;
                }
                let graph_path = std::path::Path::new(&absolute).with_extension("mgraph");
                if let Some(document) = self.material.as_ref() {
                    if std::fs::write(&graph_path, document.generator.to_text()).is_err() {
                        self.log(
                            LogLevel::Error,
                            format!("Could not save generator graph {}", graph_path.display()),
                        );
                    }
                }
                self.material_last_preview = None;
                self.refresh_assets();
                self.refresh_material_thumbnails();
                self.log(LogLevel::Action, format!("Saved material {asset_path}"));
            }
            Err(_) => self.log(
                LogLevel::Error,
                format!("Could not save material {absolute}"),
            ),
        }
    }

    fn apply_material(&mut self) {
        if self
            .material
            .as_ref()
            .is_some_and(|document| document.dirty)
        {
            self.save_material();
        }
        let Some(engine) = self.engine.as_mut() else {
            return;
        };
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            self.log(LogLevel::Error, "Apply material: no entity selected");
            return;
        };
        let Some(asset_path) = self
            .material
            .as_ref()
            .and_then(|document| document.asset_path.clone())
        else {
            self.log(LogLevel::Error, "Apply material: save the material first");
            return;
        };
        match engine.apply_material(entity, &asset_path) {
            Ok(()) => {
                let name = self
                    .material
                    .as_ref()
                    .map(|document| document.surface.name.clone())
                    .unwrap_or_else(|| asset_path.clone());
                self.refresh_components();
                self.log(
                    LogLevel::Action,
                    format!("Assigned {name} to the selected entity"),
                );
                self.commit("Assign Material", None);
            }
            Err(_) => self.log(
                LogLevel::Error,
                format!("Could not assign material {asset_path}"),
            ),
        }
    }

    fn reset_generator_view(&mut self) {
        self.generator_selected = self
            .material
            .as_ref()
            .and_then(|document| document.generator.nodes.first())
            .map(|node| node.id);
        self.generator_preview = None;
        self.generator_node_previews.clear();
        self.generator_status = None;
        self.generator_link = None;
        self.generator_search.clear();
        self.generator_reconnect = false;
        self.generator_texture_target = None;
        self.generator_texture_search.clear();
        self.fit_generator_view();
    }

    fn fit_generator_view(&mut self) {
        let Some(document) = self.material.as_ref() else {
            self.generator_pan = iced::Vector::new(0.0, 0.0);
            self.generator_zoom = 1.0;
            return;
        };
        let nodes = &document.generator.nodes;
        if nodes.is_empty() {
            self.generator_pan = iced::Vector::new(24.0, 24.0);
            self.generator_zoom = 1.0;
            return;
        }
        let min_x = nodes
            .iter()
            .map(|node| node.position.x)
            .fold(f32::MAX, f32::min);
        let min_y = nodes
            .iter()
            .map(|node| node.position.y)
            .fold(f32::MAX, f32::min);
        let max_x = nodes
            .iter()
            .map(|node| node.position.x)
            .fold(f32::MIN, f32::max)
            + 170.0;
        let max_y = nodes
            .iter()
            .map(|node| node.position.y)
            .fold(f32::MIN, f32::max)
            + 130.0;
        let width = (max_x - min_x).max(1.0);
        let height = (max_y - min_y).max(1.0);
        let zoom = (640.0 / width).min(420.0 / height).clamp(0.4, 1.0);
        self.generator_zoom = zoom;
        self.generator_pan = iced::Vector::new(28.0 - min_x * zoom, 28.0 - min_y * zoom);
    }

    fn show_generator_pane(&mut self) {
        self.panes.restore();
        let mut target = None;
        for (pane, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::Generator)
            {
                group.active = index;
                target = Some(*pane);
            }
        }
        if target.is_none() {
            for (pane, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Viewport) {
                    group.pages.push(PaneKind::Generator);
                    group.active = group.pages.len() - 1;
                    target = Some(*pane);
                    break;
                }
            }
        }
        if target.is_none() {
            if let Some((pane, group)) = self.panes.iter_mut().next() {
                group.pages.push(PaneKind::Generator);
                group.active = group.pages.len() - 1;
                target = Some(*pane);
            }
        }
        if let Some(pane) = target {
            self.panes.maximize(pane);
        }
        self.mark_generator_preview_dirty();
    }

    fn set_generator_node_previews(&mut self, previews: Vec<(generators::NodeId, Vec<u8>)>) {
        self.generator_node_previews = previews
            .into_iter()
            .map(|(id, pixels)| {
                (
                    id,
                    iced::widget::image::Handle::from_rgba(
                        GENERATOR_NODE_PREVIEW,
                        GENERATOR_NODE_PREVIEW,
                        pixels,
                    ),
                )
            })
            .collect();
    }

    fn mark_generator_preview_dirty(&mut self) {
        if let Some(job) = self.generator_live_job.as_ref() {
            job.cancel.store(true, std::sync::atomic::Ordering::Relaxed);
        }
        self.generator_preview_dirty = true;
        self.generator_preview_changed = Some(Instant::now());
        self.generator_preview_version = self.generator_preview_version.wrapping_add(1);
    }

    fn push_generator_history(&mut self, name: impl Into<String>, key: Option<String>) {
        if let Some(document) = self.material.as_mut() {
            document.dirty = true;
            let graph = document.generator.clone();
            document.generator_history.push(graph, name, key);
        }
        self.mark_generator_preview_dirty();
    }

    fn edit_generator(&mut self, edit: material_generator::Edit) {
        let snap = self.generator_snap;
        if !matches!(
            edit,
            material_generator::Edit::DetachLink(_, _) | material_generator::Edit::Connect { .. }
        ) {
            self.generator_reconnect = false;
        }
        match edit {
            material_generator::Edit::Select(selected) => {
                self.generator_selected = selected;
                self.generator_link = None;
            }
            material_generator::Edit::SelectLink(link) => {
                self.generator_link = link;
                self.generator_selected = None;
            }
            material_generator::Edit::DeleteLink(to, input) => {
                if let Some(document) = self.material.as_mut() {
                    document
                        .generator
                        .links
                        .retain(|link| !(link.to == to && link.input == input));
                }
                if self.generator_link == Some((to, input)) {
                    self.generator_link = None;
                }
                self.generator_reconnect = false;
                self.push_generator_history("Delete link", None);
            }
            material_generator::Edit::DetachLink(to, input) => {
                if let Some(document) = self.material.as_mut() {
                    document
                        .generator
                        .links
                        .retain(|link| !(link.to == to && link.input == input));
                }
                if self.generator_link == Some((to, input)) {
                    self.generator_link = None;
                }
                self.generator_reconnect = true;
                self.push_generator_history("Detach link", Some("generator:reconnect".to_owned()));
            }
            material_generator::Edit::InsertDot(to, input, position) => {
                let dot_position = snap_generator_position(position, snap);
                let inserted = if let Some(document) = self.material.as_mut() {
                    let existed = document
                        .generator
                        .links
                        .iter()
                        .find(|link| link.to == to && link.input == input)
                        .copied();
                    if let Some(link) = existed {
                        document
                            .generator
                            .links
                            .retain(|candidate| !(candidate.to == to && candidate.input == input));
                        let dot = document
                            .generator
                            .add_node(generators::NodeKind::Dot, dot_position);
                        let first = document.generator.connect(link.from, link.output, dot, 0);
                        let second = document.generator.connect(dot, 0, to, input);
                        (first && second).then_some(dot)
                    } else {
                        None
                    }
                } else {
                    None
                };
                if let Some(dot) = inserted {
                    self.generator_selected = Some(dot);
                    self.generator_link = None;
                    self.push_generator_history("Insert reroute dot", None);
                } else {
                    self.generator_status = Some("Could not insert the reroute dot".to_owned());
                }
            }
            material_generator::Edit::Undo => {
                if !self.undo_generator() {
                    self.generator_status = Some("Nothing to undo in the graph".to_owned());
                }
            }
            material_generator::Edit::Redo => {
                if !self.redo_generator() {
                    self.generator_status = Some("Nothing to redo in the graph".to_owned());
                }
            }
            material_generator::Edit::Fit => {
                self.fit_generator_view();
                self.generator_status = Some("Framed the graph".to_owned());
            }
            material_generator::Edit::SetSnap(value) => {
                self.generator_snap = value;
            }
            material_generator::Edit::SetSearch(value) => {
                self.generator_search = value;
            }
            material_generator::Edit::TogglePaletteGroup(name) => {
                if !self.generator_palette_collapsed.remove(&name) {
                    self.generator_palette_collapsed.insert(name);
                }
            }
            material_generator::Edit::Noop => {}
            material_generator::Edit::SetPackingPreset(index) => {
                if let Some(document) = self.material.as_mut() {
                    document
                        .generator_packing
                        .set_preset(generators::PackingPreset::from_index(index));
                }
            }
            material_generator::Edit::ApplyPackingUser(index) => {
                if let Some(document) = self.material.as_mut() {
                    if let Some(preset) = self.packing_user_presets.get(index).cloned() {
                        document.generator_packing.apply_user_preset(&preset);
                        self.packing_preset_name = preset.name.clone();
                        self.generator_status = Some(format!("Packing preset: {}", preset.name));
                    }
                }
            }
            material_generator::Edit::SetPackingPresetName(name) => {
                self.packing_preset_name = name;
            }
            material_generator::Edit::SavePackingPreset => {
                let name = self.packing_preset_name.trim().to_owned();
                if name.is_empty() {
                    self.generator_status = Some("Name the packing preset first".to_owned());
                } else if let Some(document) = self.material.as_ref() {
                    let preset = generators::NamedPackingPreset {
                        name: name.clone(),
                        channels: document.generator_packing.channels,
                    };
                    if let Some(existing) = self
                        .packing_user_presets
                        .iter_mut()
                        .find(|existing| existing.name == name)
                    {
                        *existing = preset;
                    } else {
                        self.packing_user_presets.push(preset);
                    }
                    match self.packing_store_path.clone() {
                        Some(path) => match generators::save_user_packing_presets(
                            &path,
                            &self.packing_user_presets,
                        ) {
                            Ok(()) => {
                                self.generator_status =
                                    Some(format!("Saved packing preset {name}"));
                                self.log(LogLevel::Action, format!("Saved packing preset {name}"));
                            }
                            Err(error) => {
                                self.generator_status =
                                    Some(format!("Could not save packing presets: {error}"));
                            }
                        },
                        None => {
                            self.generator_status =
                                Some("No config directory for packing presets".to_owned());
                        }
                    }
                }
            }
            material_generator::Edit::DeletePackingPreset => {
                let name = self.packing_preset_name.trim().to_owned();
                if !name.is_empty() {
                    self.packing_user_presets
                        .retain(|preset| preset.name != name);
                    if let Some(document) = self.material.as_mut() {
                        if document.generator_packing.custom_name.as_deref() == Some(name.as_str())
                        {
                            document.generator_packing.custom_name = None;
                        }
                    }
                    if let Some(path) = self.packing_store_path.clone() {
                        if let Err(error) =
                            generators::save_user_packing_presets(&path, &self.packing_user_presets)
                        {
                            self.generator_status =
                                Some(format!("Could not save packing presets: {error}"));
                        } else {
                            self.generator_status = Some(format!("Deleted packing preset {name}"));
                        }
                    }
                }
            }
            material_generator::Edit::SetPackingChannel(channel, source) => {
                if let Some(document) = self.material.as_mut() {
                    document.generator_packing.preset = generators::PackingPreset::Custom;
                    document.generator_packing.custom_name = None;
                    document.generator_packing.channels[channel.min(3)] =
                        generators::PackSource::from_index(source);
                }
            }
            material_generator::Edit::SetBits(bits) => {
                if let Some(document) = self.material.as_mut() {
                    document.generator_bits = if bits == 16 { 16 } else { 8 };
                }
            }
            material_generator::Edit::SetImagePath(node, path) => {
                if let Some(document) = self.material.as_mut() {
                    if let Some(node) = document.generator.node_mut(node) {
                        if let generators::NodeKind::Image { path: texture, .. }
                        | generators::NodeKind::NormalMap { path: texture, .. } = &mut node.kind
                        {
                            *texture = path;
                        }
                    }
                }
                self.push_generator_history("Set image texture", None);
            }
            material_generator::Edit::CancelBake => self.cancel_generator_bake(),
            material_generator::Edit::OpenTexturePicker(node) => {
                self.generator_texture_target = Some(node);
                self.generator_texture_search.clear();
            }
            material_generator::Edit::Copy => {
                if let Some(node) = self
                    .material
                    .as_ref()
                    .and_then(|document| {
                        self.generator_selected
                            .and_then(|id| document.generator.node(id))
                    })
                    .cloned()
                {
                    self.generator_clipboard = Some(node);
                    self.generator_status = Some("Copied node".to_owned());
                }
            }
            material_generator::Edit::Paste => {
                let pasted = if let (Some(clipboard), Some(document)) =
                    (self.generator_clipboard.clone(), self.material.as_mut())
                {
                    let position =
                        iced::Point::new(clipboard.position.x + 24.0, clipboard.position.y + 24.0);
                    Some(document.generator.add_node(clipboard.kind, position))
                } else {
                    None
                };
                if let Some(id) = pasted {
                    self.generator_selected = Some(id);
                    self.generator_link = None;
                    self.push_generator_history("Paste node", None);
                }
            }
            material_generator::Edit::Viewport { pan, zoom } => {
                self.generator_pan = pan;
                self.generator_zoom = zoom.clamp(0.35, 2.2);
            }
            material_generator::Edit::OpenMaterial => self.show_material_pane(),
            material_generator::Edit::Bake => self.bake_generator(),
            material_generator::Edit::AddNode(kind) => {
                let Some(document) = self.material.as_mut() else {
                    self.generator_status = Some("Open a material first".to_owned());
                    return;
                };
                if matches!(kind, generators::NodeKind::Output { .. }) {
                    if let Some(output) = document.generator.output_node() {
                        self.generator_selected = Some(output);
                        return;
                    }
                }
                let title = kind.title().to_owned();
                let id = document.generator.add_node_auto(kind);
                if let Some(node) = document.generator.node_mut(id) {
                    node.position = snap_generator_position(node.position, snap);
                }
                self.generator_selected = Some(id);
                self.generator_link = None;
                self.push_generator_history(format!("Add {title}"), None);
            }
            material_generator::Edit::DeleteNode(id) => {
                let title = self
                    .material
                    .as_ref()
                    .and_then(|document| document.generator.node(id))
                    .map(|node| node.kind.title().to_owned())
                    .unwrap_or_else(|| "node".to_owned());
                if let Some(document) = self.material.as_mut() {
                    document.generator.remove_node(id);
                }
                if self.generator_selected == Some(id) {
                    self.generator_selected = None;
                }
                if self.generator_link.is_some_and(|(to, _)| to == id) {
                    self.generator_link = None;
                }
                self.push_generator_history(format!("Delete {title}"), None);
            }
            material_generator::Edit::Connect {
                from,
                output,
                to,
                input,
            } => {
                let connected = if let Some(document) = self.material.as_mut() {
                    document.generator.connect(from, output, to, input)
                } else {
                    false
                };
                if connected {
                    self.generator_status = None;
                    self.generator_link = Some((to, input));
                    let key = self
                        .generator_reconnect
                        .then(|| "generator:reconnect".to_owned());
                    self.generator_reconnect = false;
                    self.push_generator_history(format!("Connect {from} → {to}"), key);
                } else if self.material.is_some() {
                    self.generator_status = Some("That connection would create a cycle".to_owned());
                }
            }
            material_generator::Edit::MoveNode(id, delta) => {
                if let Some(document) = self.material.as_mut() {
                    if let Some(node) = document.generator.node_mut(id) {
                        // Snapping happens in the canvas against the drag origin.
                        node.position = generators::offset_position(node.position, delta);
                    }
                }
                self.push_generator_history("Move node", Some(format!("generator:move:{id}")));
            }
            material_generator::Edit::SetPreviewChannel(index) => {
                self.generator_preview_channel =
                    index.min(generators::PreviewChannel::ALL.len() - 1);
            }
            material_generator::Edit::ApplyPreset(index) => {
                if let Some(preset) = material_library::presets().get(index) {
                    let resolution = self.material.as_ref().map(|d| d.generator.resolution);
                    let mut graph = preset.build(&material_library::PresetParams::default());
                    if let Some(resolution) = resolution {
                        graph.resolution = resolution;
                    }
                    if let Some(document) = self.material.as_mut() {
                        document.generator = graph;
                    }
                    self.generator_selected = None;
                    self.generator_link = None;
                    self.push_generator_history(
                        format!("Library: {}", preset.name),
                        Some("generator:library".to_owned()),
                    );
                    self.fit_generator_view();
                }
            }
            material_generator::Edit::SetResolution(resolution) => {
                if let Some(document) = self.material.as_mut() {
                    document.generator.resolution = resolution.clamp(64, 2048);
                }
                self.push_generator_history(
                    "Set generator resolution",
                    Some("generator:resolution".to_owned()),
                );
            }
            material_generator::Edit::SetParam(id, param) => {
                let label = param.label();
                let key = param.key();
                if let Some(document) = self.material.as_mut() {
                    if let Some(node) = document.generator.node_mut(id) {
                        apply_generator_param(&mut node.kind, param);
                    }
                }
                self.push_generator_history(
                    format!("Set {label}"),
                    Some(format!("generator:param:{id}:{key}")),
                );
            }
        }
    }

    fn resolve_graph_textures(
        &self,
        graph: &MaterialGraph,
    ) -> Result<Vec<(String, String)>, String> {
        let mut texture_paths: Vec<(String, String)> = Vec::new();
        for node in &graph.nodes {
            let (generators::NodeKind::Image { path, .. }
            | generators::NodeKind::NormalMap { path, .. }) = &node.kind
            else {
                continue;
            };
            if path.is_empty() || texture_paths.iter().any(|(key, _)| key == path) {
                continue;
            }
            // Only graphs that read project images need the engine to find them;
            // purely procedural graphs preview and bake without one.
            let Some(engine) = self.engine.as_ref() else {
                return Err("No engine".to_owned());
            };
            let resolved = engine
                .resolve_path(path)
                .ok()
                .filter(|resolved| std::path::Path::new(resolved).exists())
                .or_else(|| engine.asset_path(path).ok());
            let Some(resolved) = resolved else {
                return Err(format!("Image texture missing: {path}"));
            };
            texture_paths.push((path.clone(), resolved));
        }
        Ok(texture_paths)
    }

    fn bake_generator(&mut self) {
        if self.generator_bake.is_some() {
            self.generator_status = Some("A bake is already running".to_owned());
            return;
        }
        if self
            .material
            .as_ref()
            .is_some_and(|document| document.file_path.is_none())
        {
            self.save_material();
        }
        let Some(document) = self.material.as_ref() else {
            self.generator_status = Some("Open a material first".to_owned());
            return;
        };
        let Some(file_path) = document.file_path.clone() else {
            self.generator_status = Some("Save the material before baking".to_owned());
            return;
        };
        let asset_path = document.asset_path.clone().unwrap_or_default();
        let directory = std::path::Path::new(&file_path)
            .parent()
            .map(std::path::Path::to_path_buf)
            .unwrap_or_default();
        let asset_directory = std::path::Path::new(&asset_path)
            .parent()
            .map(|path| path.to_string_lossy().replace('\\', "/"))
            .unwrap_or_default();
        let stem = std::path::Path::new(&asset_path)
            .file_stem()
            .and_then(|stem| stem.to_str())
            .unwrap_or("generated")
            .to_owned();
        let packing = document.generator_packing.clone();
        let bits = document.generator_bits;
        let graph = document.generator.clone();

        // Resolve paths on the UI thread; decoding and baking both run on the
        // worker so a large texture never stalls the interface.
        let texture_paths = match self.resolve_graph_textures(&graph) {
            Ok(paths) => paths,
            Err(error) => {
                self.generator_status = Some(error);
                return;
            }
        };

        let (tx, rx) = std::sync::mpsc::channel();
        let cancel = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
        let worker_cancel = cancel.clone();
        let worker_graph = graph;
        let worker_tx = tx.clone();
        std::thread::spawn(move || {
            let cancelled = || worker_cancel.load(std::sync::atomic::Ordering::Relaxed);
            let total = texture_paths.len().max(1);
            let mut textures = generators::TextureMap::new();
            for (index, (key, path)) in texture_paths.into_iter().enumerate() {
                if cancelled() {
                    let _ = worker_tx.send(GeneratorBakeEvent::Done(Err("Cancelled".to_owned())));
                    return;
                }
                let name = std::path::Path::new(&path)
                    .file_name()
                    .and_then(|name| name.to_str())
                    .unwrap_or(&path)
                    .to_owned();
                let _ = worker_tx.send(GeneratorBakeEvent::Progress(
                    index as f32 / total as f32 * 0.1,
                    format!("Decode {name}"),
                ));
                match generators::decode_texture(&path) {
                    Ok(texture) => {
                        textures.insert(key, texture);
                    }
                    Err(error) => {
                        let _ = worker_tx.send(GeneratorBakeEvent::Done(Err(error)));
                        return;
                    }
                }
            }
            let mut progress = |fraction: f32, stage: &str| {
                let _ = worker_tx.send(GeneratorBakeEvent::Progress(
                    0.1 + fraction * 0.9,
                    stage.to_owned(),
                ));
            };
            let result = worker_graph
                .bake_with(&textures, Some(&mut progress), Some(&cancelled))
                .map(Box::new);
            if result.is_ok() {
                let previews =
                    worker_graph.node_previews(&textures, GENERATOR_NODE_PREVIEW, Some(&cancelled));
                let _ = worker_tx.send(GeneratorBakeEvent::NodePreviews(previews));
            }
            let _ = worker_tx.send(GeneratorBakeEvent::Done(result));
        });
        // The initial job message keeps the channel alive briefly so a fast
        // bake does not race the worker's first send.
        let _ = tx.send(GeneratorBakeEvent::Progress(0.0, "Preparing".to_owned()));
        self.generator_bake = Some(GeneratorBakeJob {
            rx,
            cancel,
            directory,
            asset_directory,
            stem,
            packing,
            bits,
            progress: 0.0,
            stage: "Preparing".to_owned(),
        });
        self.generator_status = Some("Baking in the background…".to_owned());
    }

    fn pump_generator_bake(&mut self) {
        let Some(mut job) = self.generator_bake.take() else {
            return;
        };
        let mut done = None;
        let mut previews = None;
        loop {
            match job.rx.try_recv() {
                Ok(GeneratorBakeEvent::Progress(fraction, stage)) => {
                    job.progress = fraction.clamp(0.0, 1.0);
                    job.stage = stage;
                }
                Ok(GeneratorBakeEvent::NodePreviews(images)) => previews = Some(images),
                Ok(GeneratorBakeEvent::Done(result)) => {
                    done = Some(result);
                    break;
                }
                Err(std::sync::mpsc::TryRecvError::Empty) => break,
                Err(std::sync::mpsc::TryRecvError::Disconnected) => {
                    done = Some(Err("Bake worker stopped unexpectedly".to_owned()));
                    break;
                }
            }
        }
        if let Some(previews) = previews {
            self.set_generator_node_previews(previews);
        }
        match done {
            Some(Ok(baked)) => self.finish_generator_bake(job, *baked),
            Some(Err(error)) => {
                if error == "Cancelled" {
                    self.generator_status = Some("Bake cancelled".to_owned());
                    self.log(LogLevel::Action, "Generator bake cancelled");
                } else {
                    self.generator_status = Some(error.clone());
                    self.log(LogLevel::Error, format!("Generator: {error}"));
                }
            }
            None => self.generator_bake = Some(job),
        }
    }

    fn pump_generator_preview(&mut self) {
        if let Some(job) = self.generator_live_job.take() {
            let mut done = None;
            let mut previews = None;
            loop {
                match job.rx.try_recv() {
                    Ok(GeneratorBakeEvent::Progress(..)) => {}
                    Ok(GeneratorBakeEvent::NodePreviews(images)) => previews = Some(images),
                    Ok(GeneratorBakeEvent::Done(result)) => {
                        done = Some(result);
                        break;
                    }
                    Err(std::sync::mpsc::TryRecvError::Empty) => break,
                    Err(std::sync::mpsc::TryRecvError::Disconnected) => {
                        done = Some(Err("Preview worker stopped".to_owned()));
                        break;
                    }
                }
            }
            if let Some(result) = done {
                if job.version == self.generator_preview_version {
                    if let Some(previews) = previews {
                        self.set_generator_node_previews(previews);
                    }
                    match result {
                        Ok(baked) => {
                            self.generator_preview_maps = generators::PreviewChannel::ALL
                                .iter()
                                .map(|channel| {
                                    iced::widget::image::Handle::from_rgba(
                                        baked.resolution,
                                        baked.resolution,
                                        baked.channel_rgba(*channel),
                                    )
                                })
                                .collect();
                            self.generator_preview = Some(iced::widget::image::Handle::from_rgba(
                                baked.resolution,
                                baked.resolution,
                                baked.base,
                            ));
                            self.generator_preview_revision =
                                self.generator_preview_revision.wrapping_add(1);
                            self.generator_preview_dirty = false;
                        }
                        Err(error) => {
                            self.generator_status = Some(error);
                            self.generator_preview_dirty = false;
                        }
                    }
                }
            } else {
                self.generator_live_job = Some(job);
                return;
            }
        }
        if self.generator_bake.is_some() || !self.generator_preview_dirty {
            return;
        }
        if self
            .generator_preview_changed
            .is_some_and(|changed| changed.elapsed() < std::time::Duration::from_millis(160))
        {
            return;
        }
        let Some(document) = self.material.as_ref() else {
            self.generator_preview_dirty = false;
            return;
        };
        let mut graph = document.generator.clone();
        graph.resolution = graph.resolution.clamp(96, 256);
        let texture_paths = match self.resolve_graph_textures(&graph) {
            Ok(paths) => paths,
            Err(error) => {
                self.generator_status = Some(error);
                self.generator_preview_dirty = false;
                return;
            }
        };
        let version = self.generator_preview_version;
        let (tx, rx) = std::sync::mpsc::channel();
        let cancel = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
        let worker_cancel = cancel.clone();
        let worker_tx = tx.clone();
        std::thread::spawn(move || {
            let cancelled = || worker_cancel.load(std::sync::atomic::Ordering::Relaxed);
            let mut textures = generators::TextureMap::new();
            for (key, path) in texture_paths {
                if cancelled() {
                    let _ = worker_tx.send(GeneratorBakeEvent::Done(Err("Cancelled".to_owned())));
                    return;
                }
                match generators::decode_texture(&path) {
                    Ok(texture) => {
                        textures.insert(key, texture);
                    }
                    Err(error) => {
                        let _ = worker_tx.send(GeneratorBakeEvent::Done(Err(error)));
                        return;
                    }
                }
            }
            let result = graph
                .bake_with(&textures, None, Some(&cancelled))
                .map(Box::new);
            if result.is_ok() {
                let previews =
                    graph.node_previews(&textures, GENERATOR_NODE_PREVIEW, Some(&cancelled));
                let _ = worker_tx.send(GeneratorBakeEvent::NodePreviews(previews));
            }
            let _ = worker_tx.send(GeneratorBakeEvent::Done(result));
        });
        self.generator_preview_dirty = false;
        self.generator_live_job = Some(GeneratorPreviewJob {
            rx,
            cancel,
            version,
        });
    }

    fn cancel_generator_bake(&mut self) {
        let Some(job) = self.generator_bake.as_mut() else {
            return;
        };
        job.cancel.store(true, std::sync::atomic::Ordering::Relaxed);
        job.stage = "Cancelling…".to_owned();
        self.generator_status = Some("Cancelling bake…".to_owned());
    }

    fn finish_generator_bake(&mut self, job: GeneratorBakeJob, baked: generators::BakedMaterial) {
        let Some(engine) = self.engine.as_ref() else {
            self.generator_status = Some("Bake finished without an engine".to_owned());
            return;
        };
        let GeneratorBakeJob {
            directory,
            asset_directory,
            stem,
            packing,
            bits,
            ..
        } = job;
        let relative = |name: &str| {
            if asset_directory.is_empty() {
                name.to_owned()
            } else {
                format!("{asset_directory}/{name}")
            }
        };
        let save8 = |name: &str, pixels: &[u8]| -> Option<String> {
            let path = directory.join(name);
            let text = path.to_string_lossy().to_string();
            engine
                .save_texture_rgba(&text, baked.resolution, baked.resolution, pixels)
                .ok()
                .map(|_| text)
        };
        let save16 = |name: &str, pixels: &[u16]| -> Option<String> {
            let path = directory.join(name);
            let text = path.to_string_lossy().to_string();
            engine
                .save_texture_rgba16(&text, baked.resolution, baked.resolution, pixels)
                .ok()
                .map(|_| text)
        };
        let suffix = if bits == 16 { "16" } else { "8" };
        let base_name = format!("{stem}_gen_base_{suffix}.png");
        let roughness_name = format!("{stem}_gen_roughness_{suffix}.png");
        let metallic_name = format!("{stem}_gen_metallic_{suffix}.png");
        let height_name = format!("{stem}_gen_height_{suffix}.png");
        let packed_name = format!("{stem}_gen_packed_{suffix}.png");
        let base_path = if bits == 16 {
            save16(&base_name, &baked.base16)
        } else {
            save8(&base_name, &baked.base)
        };
        let roughness_pixels = gray_to_rgba(&baked.roughness);
        let metallic_pixels = gray_to_rgba(&baked.metallic);
        let height_pixels = gray_to_rgba(&baked.height);
        let roughness_pixels16 = gray_to_rgba16(&baked.roughness16);
        let metallic_pixels16 = gray_to_rgba16(&baked.metallic16);
        let height_pixels16 = gray_to_rgba16(&baked.height16);
        let _roughness_path = if bits == 16 {
            save16(&roughness_name, &roughness_pixels16)
        } else {
            save8(&roughness_name, &roughness_pixels)
        };
        let _metallic_path = if bits == 16 {
            save16(&metallic_name, &metallic_pixels16)
        } else {
            save8(&metallic_name, &metallic_pixels)
        };
        let height_path = if bits == 16 {
            save16(&height_name, &height_pixels16)
        } else {
            save8(&height_name, &height_pixels)
        };
        let Some(base_path) = base_path else {
            self.generator_status = Some("Could not write the generated base colour".to_owned());
            return;
        };
        let Some(height_path) = height_path else {
            self.generator_status = Some("Could not write the generated height map".to_owned());
            return;
        };
        let packed_path = if bits == 16 {
            save16(&packed_name, &packing.packed16(&baked))
        } else {
            save8(&packed_name, &packing.packed8(&baked))
        };
        let normal_path = directory
            .join(format!("{stem}_gen_normal.png"))
            .to_string_lossy()
            .to_string();
        let normal_options = doppio_engine::TextureMapOptions {
            strength: 1.0,
            directx: self.material_directx,
            wrap: true,
            min_value: 0.0,
            max_value: 1.0,
            invert: false,
            radius: 8,
        };
        // An authored Normal input (OpenGL convention) wins over the height-derived one.
        let authored_normal = baked.normal.is_some();
        let normal = if let Some(authored) = &baked.normal {
            let pixels: Vec<u8> = authored.iter().flat_map(|pixel| *pixel).collect();
            engine
                .save_texture_rgba(&normal_path, baked.resolution, baked.resolution, &pixels)
                .is_ok()
        } else {
            engine
                .generate_texture_map(
                    &height_path,
                    &normal_path,
                    doppio_engine::TextureMapKind::Normal,
                    normal_options,
                )
                .is_ok()
        };
        let emissive_name = format!("{stem}_gen_emissive_{suffix}.png");
        let emissive_path = if baked.has_emissive {
            if bits == 16 {
                save16(&emissive_name, &baked.emissive16)
            } else {
                save8(&emissive_name, &baked.emissive)
            }
        } else {
            None
        };
        // Depth: store the height in the normal map's alpha channel. The renderer's
        // parallax occlusion mapping reads it from there (no extra texture sampler).
        let mut has_depth = false;
        if normal {
            if let Ok((width, height, mut pixels)) = engine.load_texture_rgba(&normal_path) {
                let expected = baked.resolution as usize;
                if width as usize == expected
                    && height as usize == expected
                    && baked.height.len() == expected * expected
                    && pixels.len() == expected * expected * 4
                {
                    for (index, value) in baked.height.iter().enumerate() {
                        pixels[index * 4 + 3] = *value;
                    }
                    has_depth = engine
                        .save_texture_rgba(&normal_path, width, height, &pixels)
                        .is_ok();
                }
            }
        }
        let height_range = baked
            .height
            .iter()
            .copied()
            .fold((u8::MAX, u8::MIN), |(low, high), value| {
                (low.min(value), high.max(value))
            });
        let has_relief = height_range.1.saturating_sub(height_range.0) > 12;
        let base_relative = relative(&base_name);
        let normal_relative = relative(&format!("{stem}_gen_normal.png"));
        let packed_relative = relative(&packed_name);
        let base_map = engine
            .asset_project_path(&base_relative)
            .unwrap_or(base_relative);
        let normal_map = engine
            .asset_project_path(&normal_relative)
            .unwrap_or(normal_relative);
        let packed_map = engine
            .asset_project_path(&packed_relative)
            .unwrap_or(packed_relative);
        let is_orm = packing.preset == generators::PackingPreset::Orm;
        if let Some(document) = self.material.as_mut() {
            document.surface.albedo_map = base_map;
            if emissive_path.is_some() {
                let relative_emissive = relative(&emissive_name);
                document.surface.emission_map = engine
                    .asset_project_path(&relative_emissive)
                    .unwrap_or(relative_emissive);
                document.surface.emission = [1.0, 1.0, 1.0];
                document.surface.emission_strength = document.surface.emission_strength.max(1.0);
            }
            if baked.has_opacity
                && document.surface.alpha_mode == doppio_engine::MaterialAlphaMode::Opaque
            {
                // The Opacity input is in the base colour's alpha: cut it out by default.
                document.surface.alpha_mode = doppio_engine::MaterialAlphaMode::Cutout;
            }
            if normal {
                document.surface.normal_map = normal_map;
                document.surface.normal_directx = if authored_normal {
                    false
                } else {
                    self.material_directx
                };
                document.surface.ao_strength = 1.0;
                if has_depth && has_relief {
                    // Real parallax depth; keep a value the user already tuned.
                    if document.surface.height_scale <= 0.0 {
                        document.surface.height_scale = 0.035;
                    }
                } else if has_depth {
                    document.surface.height_scale = 0.0;
                }
            }
            if is_orm && packed_path.is_some() {
                document.surface.orm_map = packed_map;
            }
            document.dirty = true;
            document.preview_dirty = true;
        }
        self.generator_preview = Some(iced::widget::image::Handle::from_rgba(
            baked.resolution,
            baked.resolution,
            baked.base,
        ));
        self.generator_preview_revision = self.generator_preview_revision.wrapping_add(1);
        self.generator_status = Some(format!(
            "Baked {}×{} · {} · {}{}",
            baked.resolution,
            baked.resolution,
            packing.preset.label(),
            bits,
            "-bit"
        ));
        self.material_last_preview = None;
        self.refresh_material_thumbnails();
        self.log(
            LogLevel::Action,
            format!(
                "Generator baked {}×{} ({} bit) into {stem}; base={base_path}",
                baked.resolution, baked.resolution, bits
            ),
        );
        // A bake is meant to be visible: persist the generated maps and, when
        // an entity is selected, assign the material so the 3D viewport uses
        // exactly what the node graph produced.
        if self.entities.get(self.selected).is_some() {
            self.apply_material();
        } else {
            self.save_material();
        }
    }

    fn import_mesh_materials(&mut self, path: &str) {
        let Some(engine) = self.engine.as_ref() else {
            self.log(LogLevel::Error, "Mesh material import: no engine");
            return;
        };
        let source = std::path::Path::new(path);
        let resolved = engine
            .resolve_path(path)
            .ok()
            .filter(|resolved| std::path::Path::new(resolved).exists())
            .or_else(|| engine.asset_path(path).ok())
            .unwrap_or_else(|| path.to_owned());
        let materials = match crate::uikit::mesh_material_import::import_mesh_materials(
            std::path::Path::new(&resolved),
        ) {
            Ok(materials) => materials,
            Err(error) => {
                self.log(LogLevel::Error, format!("Mesh material import: {error}"));
                return;
            }
        };
        if materials.is_empty() {
            self.log(
                LogLevel::Error,
                format!("No materials found in {}", source.display()),
            );
            return;
        }
        let stem = source
            .file_stem()
            .and_then(|stem| stem.to_str())
            .unwrap_or("imported");
        let directory = self
            .asset_root_dir()
            .map(|root| asset_folders::resolve(&root, asset_folders::AssetFolder::Materials))
            .unwrap_or_else(|| std::path::PathBuf::from("Materials"));
        if std::fs::create_dir_all(&directory).is_err() {
            self.log(LogLevel::Error, "Could not create materials directory");
            return;
        }
        let mut written = 0usize;
        for definition in materials {
            let mut surface = engine.new_material().unwrap_or_default();
            surface.name = definition.name.clone();
            surface.albedo = definition.albedo;
            surface.metallic = definition.metallic;
            surface.roughness = definition.roughness;
            surface.emission = definition.emission;
            surface.emission_strength = definition.emission_strength;
            surface.uv_tiling = definition.uv_tiling;
            surface.uv_offset = definition.uv_offset;
            surface.alpha_mode = match definition.alpha_mode {
                1 => doppio_engine::MaterialAlphaMode::Cutout,
                2 => doppio_engine::MaterialAlphaMode::Blend,
                _ => doppio_engine::MaterialAlphaMode::Opaque,
            };
            surface.alpha_cutoff = definition.alpha_cutoff;
            let map_path = |value: &str| {
                engine
                    .asset_project_path(value)
                    .unwrap_or_else(|_| value.to_owned())
            };
            surface.albedo_map = map_path(&definition.albedo_map);
            surface.normal_map = map_path(&definition.normal_map);
            surface.emission_map = map_path(&definition.emission_map);
            if !definition.ao_map.is_empty()
                || !definition.roughness_map.is_empty()
                || !definition.metallic_map.is_empty()
            {
                let name = sanitize_material_name(&definition.name);
                let output = directory.join(format!("{stem}_{name}_orm.png"));
                let ao = (!definition.ao_map.is_empty()).then_some(definition.ao_map.as_str());
                let roughness = (!definition.roughness_map.is_empty())
                    .then_some(definition.roughness_map.as_str());
                let metallic = (!definition.metallic_map.is_empty())
                    .then_some(definition.metallic_map.as_str());
                if engine
                    .pack_texture_maps(
                        ao,
                        roughness,
                        metallic,
                        &output.to_string_lossy(),
                        doppio_engine::TexturePackKind::Orm,
                    )
                    .is_ok()
                {
                    surface.orm_map = map_path(&output.to_string_lossy());
                }
            }
            let file = directory.join(format!(
                "{stem}_{}.mat",
                sanitize_material_name(&definition.name)
            ));
            if engine
                .save_material(&file.to_string_lossy(), &surface)
                .is_ok()
            {
                let mut graph = MaterialGraph::default();
                if !definition.albedo_map.is_empty() {
                    graph.nodes[0].kind = generators::NodeKind::Image {
                        path: definition.albedo_map.clone(),
                        projection: generators::Projection::Uv,
                        tiling: definition.uv_tiling,
                        offset: definition.uv_offset,
                        filter: generators::ImageFilter::Linear,
                        wrap: generators::WrapMode::Repeat,
                    };
                }
                let _ = std::fs::write(file.with_extension("mgraph"), graph.to_text());
                written += 1;
            }
        }
        self.refresh_assets();
        self.log(
            LogLevel::Action,
            format!("Imported {written} material(s) from {}", source.display()),
        );
    }

    fn drop_asset_on_entity(&mut self, asset_index: usize, entity_index: usize) {
        let Some(asset) = self.assets.get(asset_index).cloned() else {
            return;
        };
        let Some(entity) = self.entities.get(entity_index).map(|entity| entity.id) else {
            return;
        };
        match asset.kind.as_str() {
            "material" => {
                if let Some(engine) = self.engine.as_mut() {
                    if engine.apply_material(entity, &asset.relative_path).is_ok() {
                        self.refresh_components();
                        self.commit("Assign Material", None);
                        self.log(
                            LogLevel::Action,
                            format!("Assigned {} to entity", asset.relative_path),
                        );
                    }
                }
            }
            "image" => {
                self.new_material_from_texture(asset_index);
                self.save_material();
                self.apply_material();
            }
            _ => self.log(
                LogLevel::Ui,
                format!("Cannot drop {} onto an entity", asset.kind),
            ),
        }
    }

    /// Absolute asset root, when a project (or a test override) provides one.
    fn asset_root_dir(&self) -> Option<std::path::PathBuf> {
        if let Some(root) = &self.asset_root_override {
            return Some(root.clone());
        }
        let engine = self.engine.as_ref()?;
        let root = engine.asset_path("").ok()?;
        let root = std::path::PathBuf::from(root);
        (!root.as_os_str().is_empty()).then_some(root)
    }

    fn wizard_status(&mut self, text: impl Into<String>, ok: bool) {
        self.wizard.status = Some(panels::asset_wizard::Status {
            text: text.into(),
            ok,
        });
    }

    fn open_wizard(&mut self, tab: panels::asset_wizard::Tab) {
        self.wizard.open = true;
        self.wizard.tab = tab;
        self.wizard.status = None;
        self.open_menu = None;
    }

    fn pack_output_path(&self, root: &std::path::Path) -> std::path::PathBuf {
        let name = self
            .project
            .active
            .as_ref()
            .map(|project| asset_wizard::sanitize_name(&project.name))
            .unwrap_or(None)
            .unwrap_or_else(|| "game".to_owned());
        root.parent()
            .unwrap_or(root)
            .join(format!("{}.cap", name.replace(' ', "_")))
    }

    fn poll_pack_job(&mut self) {
        let Some(job) = &self.pack_job else {
            return;
        };
        let finished = job.lock().ok().and_then(|mut slot| slot.take());
        if let Some(result) = finished {
            self.pack_job = None;
            self.edit_wizard(panels::asset_wizard::Edit::PackDone(result));
        }
    }

    fn edit_wizard(&mut self, edit: panels::asset_wizard::Edit) {
        use panels::asset_wizard::{Edit, Tab};
        match edit {
            Edit::Open(tab) => self.open_wizard(tab),
            Edit::Close => self.wizard.open = false,
            Edit::Tab(tab) => {
                self.wizard.tab = tab;
                self.wizard.status = None;
            }
            Edit::Template(index) => {
                if let Some(template) = asset_wizard::Template::ALL.get(index) {
                    self.wizard.template = *template;
                    self.wizard.status = None;
                }
            }
            Edit::Name(name) => self.wizard.name = name,
            Edit::Create => self.wizard_create(),
            Edit::ImportInput(value) => self.wizard.import_input = value,
            Edit::ImportAdd => self.wizard_queue_input(),
            Edit::ImportRemove(index) => {
                if index < self.wizard.sources.len() {
                    self.wizard.sources.remove(index);
                }
            }
            Edit::ImportRun => self.wizard_import(),
            Edit::GenerateIds(value) => self.wizard.generate_ids = value,
            Edit::PackRun => self.wizard_pack(),
            Edit::PackDone(result) => {
                self.wizard.packing = false;
                match result {
                    Ok(output) => {
                        let text = if output.is_empty() {
                            "Packed assets.".to_owned()
                        } else {
                            format!("Packed: {output}")
                        };
                        self.log(LogLevel::Action, &text);
                        self.wizard_status(text, true);
                    }
                    Err(error) => {
                        self.log(LogLevel::Error, &error);
                        self.wizard_status(error, false);
                    }
                }
            }
        }
        let _ = Tab::Create;
    }

    fn wizard_queue_input(&mut self) {
        let raw = self
            .wizard
            .import_input
            .trim()
            .trim_matches(|c| c == '"' || c == '\'');
        let path = raw.strip_prefix("file://").unwrap_or(raw).trim().to_owned();
        if !path.is_empty() && !self.wizard.sources.contains(&path) {
            self.wizard.sources.push(path);
        }
        self.wizard.import_input.clear();
    }

    fn wizard_create(&mut self) {
        let template = self.wizard.template;
        let name = self.wizard.name.clone();
        if template.opens_editor() {
            self.wizard.open = false;
            match template {
                asset_wizard::Template::Material => {
                    self.new_material();
                    if let (Some(name), Some(document)) =
                        (asset_wizard::sanitize_name(&name), self.material.as_mut())
                    {
                        document.surface.name = name;
                    }
                }
                _ => self.new_texture_set(),
            }
            self.wizard.name.clear();
            return;
        }
        let Some(root) = self.asset_root_dir() else {
            self.wizard_status("Open a project first: there is no asset folder yet.", false);
            return;
        };
        let directory = self.asset_directory.clone();
        match asset_wizard::create(&root, &directory, template, &name) {
            Ok(relative) => {
                self.refresh_assets();
                self.asset_selected = self
                    .assets
                    .iter()
                    .position(|asset| asset.relative_path == relative);
                self.wizard.name.clear();
                self.wizard_status(format!("Created {relative}"), true);
                self.log(LogLevel::Action, format!("Created asset {relative}"));
            }
            Err(error) => self.wizard_status(error, false),
        }
    }

    fn wizard_import(&mut self) {
        self.wizard_queue_input();
        if self.wizard.sources.is_empty() {
            self.wizard_status("Add at least one file or folder to import.", false);
            return;
        }
        let Some(root) = self.asset_root_dir() else {
            self.wizard_status("Open a project first: there is no asset folder yet.", false);
            return;
        };
        let sources: Vec<std::path::PathBuf> = self
            .wizard
            .sources
            .iter()
            .map(std::path::PathBuf::from)
            .collect();
        let directory = self.asset_directory.clone();
        let report = asset_wizard::import(&root, &directory, &sources);
        for relative in &report.imported {
            let lower = relative.to_ascii_lowercase();
            if lower.ends_with(".glb") || lower.ends_with(".gltf") || lower.ends_with(".obj") {
                let absolute = root.join(relative).to_string_lossy().into_owned();
                self.import_mesh_materials(&absolute);
            }
        }
        self.refresh_assets();
        let mut text = report.summary();
        if let Some((source, reason)) = report.skipped.first() {
            text.push_str(&format!(" First skipped: {source} ({reason})."));
        }
        let ok = !report.imported.is_empty();
        if ok {
            self.wizard.sources.clear();
        }
        self.log(LogLevel::Action, &text);
        self.wizard_status(text, ok);
    }

    fn wizard_pack(&mut self) {
        if self.wizard.packing {
            return;
        }
        let Some(root) = self.asset_root_dir() else {
            self.wizard_status("Open a project first: there is no asset folder yet.", false);
            return;
        };
        let Some(packer) = asset_wizard::find_packer() else {
            self.wizard_status("caf-pack was not found; build it or set CAF_PACK.", false);
            return;
        };
        let output = self.pack_output_path(&root);
        let header = self
            .wizard
            .generate_ids
            .then(|| output.with_extension("hpp"));
        let arguments = asset_wizard::pack_arguments(&root, &output, header.as_deref());
        let slot = std::sync::Arc::new(std::sync::Mutex::new(None));
        let worker = slot.clone();
        std::thread::spawn(move || {
            let result = asset_wizard::run_packer(&packer, &arguments);
            if let Ok(mut guard) = worker.lock() {
                *guard = Some(result);
            }
        });
        self.pack_job = Some(slot);
        self.wizard.packing = true;
        self.wizard_status("Packing…", true);
    }

    fn handle_dropped_file(&mut self, path: std::path::PathBuf) {
        if self.wizard.open {
            let text = path.to_string_lossy().into_owned();
            if !self.wizard.sources.contains(&text) {
                self.wizard.sources.push(text);
            }
            self.wizard.tab = panels::asset_wizard::Tab::Import;
            return;
        }
        let extension = path
            .extension()
            .and_then(|extension| extension.to_str())
            .unwrap_or_default()
            .to_ascii_lowercase();
        let text = path.to_string_lossy().into_owned();
        if extension == "mat" || extension == "material" {
            let Some(engine) = self.engine.as_mut() else {
                return;
            };
            let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
                self.log(LogLevel::Error, "Drop material: select an entity first");
                return;
            };
            match engine.apply_material(entity, &text) {
                Ok(()) => {
                    self.refresh_components();
                    self.log(LogLevel::Action, format!("Dropped material {text}"));
                    self.commit("Assign Material", None);
                }
                Err(_) => self.log(LogLevel::Error, format!("Could not assign {text}")),
            }
            return;
        }
        if matches!(extension.as_str(), "obj" | "gltf" | "glb") {
            self.import_mesh_materials(&text);
            return;
        }
        if matches!(
            extension.as_str(),
            "png" | "jpg" | "jpeg" | "tga" | "gif" | "hdr" | "exr"
        ) {
            if self.material.is_some() {
                self.assign_material_texture(material::TextureSlot::Albedo, text);
            } else {
                self.log(
                    LogLevel::Error,
                    "Drop texture: open a material or create one from the texture first",
                );
            }
            return;
        }
        self.log(LogLevel::Error, format!("Unsupported dropped file: {text}"));
    }

    fn load_thumbnail_info(&self, path: &str) -> Option<(iced::widget::image::Handle, u32, u32)> {
        let engine = self.engine.as_ref()?;
        let resolved = engine
            .resolve_path(path)
            .ok()
            .filter(|resolved| std::path::Path::new(resolved).exists())
            .or_else(|| engine.asset_path(path).ok())?;
        if let Ok((width, height, rgba)) = engine.load_texture_thumbnail(&resolved, 256) {
            return Some((
                iced::widget::image::Handle::from_rgba(width, height, rgba),
                width,
                height,
            ));
        }
        if let Ok((width, height, rgba)) = engine.load_texture_rgba(&resolved) {
            return Some((
                iced::widget::image::Handle::from_rgba(width, height, rgba),
                width,
                height,
            ));
        }
        std::path::Path::new(&resolved)
            .exists()
            .then(|| (iced::widget::image::Handle::from_path(resolved), 0, 0))
    }

    fn load_thumbnail(&self, path: &str) -> Option<iced::widget::image::Handle> {
        self.load_thumbnail_info(path).map(|(handle, _, _)| handle)
    }

    fn refresh_material_thumbnails(&mut self) {
        let Some(document) = self.material.as_ref() else {
            return;
        };
        let maps = [
            document.surface.albedo_map.clone(),
            document.surface.normal_map.clone(),
            document.surface.orm_map.clone(),
            document.surface.emission_map.clone(),
        ];
        let source = document.source.clone();
        let thumbnails: [Option<iced::widget::image::Handle>; 4] = maps.map(|path| {
            (!path.is_empty())
                .then(|| self.load_thumbnail(&path))
                .flatten()
        });
        let source_thumbnail = (!source.is_empty())
            .then(|| self.load_thumbnail(&source))
            .flatten();
        if let Some(document) = self.material.as_mut() {
            document.thumbnails = thumbnails;
            document.source_thumbnail = source_thumbnail;
        }
    }

    fn edit_material(&mut self, edit: material::Edit) {
        match edit {
            material::Edit::Save => self.save_material(),
            material::Edit::Apply => self.apply_material(),
            material::Edit::New => self.new_material(),
            material::Edit::OpenGenerator => self.show_generator_pane(),
            material::Edit::OpenTextureSet => self.show_texture_set_pane(),
            material::Edit::ToggleSection(section) => {
                if !self.material_collapsed.remove(&section) {
                    self.material_collapsed.insert(section);
                }
            }
            material::Edit::OpenTexturePicker(slot) => {
                self.material_picker = Some(slot);
                self.material_picker_search.clear();
            }
            material::Edit::DropTextureAsset(slot) => {
                if let Some(asset_index) = self.asset_drag.take() {
                    if let Some(asset) = self.assets.get(asset_index).cloned() {
                        if asset.kind == "image" {
                            self.assign_material_texture(slot, asset.relative_path);
                        }
                    }
                }
            }
            material::Edit::ClearTexture(slot) => {
                if let Some(document) = self.material.as_mut() {
                    match slot {
                        material::TextureSlot::Albedo => document.surface.albedo_map.clear(),
                        material::TextureSlot::Normal => document.surface.normal_map.clear(),
                        material::TextureSlot::Orm => document.surface.orm_map.clear(),
                        material::TextureSlot::Emission => document.surface.emission_map.clear(),
                        material::TextureSlot::Source => document.source.clear(),
                    }
                    if slot != material::TextureSlot::Source {
                        document.dirty = true;
                        document.preview_dirty = true;
                    }
                }
                self.refresh_material_thumbnails();
            }
            material::Edit::ApplyPreset(index) => {
                if let Some(preset) = self.material_presets.get(index).cloned() {
                    if let Some(document) = self.material.as_mut() {
                        let name = document.surface.name.clone();
                        let maps = (
                            document.surface.albedo_map.clone(),
                            document.surface.normal_map.clone(),
                            document.surface.orm_map.clone(),
                            document.surface.emission_map.clone(),
                        );
                        let tiling = document.surface.uv_tiling;
                        let offset = document.surface.uv_offset;
                        document.surface = preset.material;
                        document.surface.name = name;
                        document.surface.albedo_map = maps.0;
                        document.surface.normal_map = maps.1;
                        document.surface.orm_map = maps.2;
                        document.surface.emission_map = maps.3;
                        document.surface.uv_tiling = tiling;
                        document.surface.uv_offset = offset;
                        document.dirty = true;
                        document.preview_dirty = true;
                    }
                    self.refresh_material_thumbnails();
                }
            }
            material::Edit::SetDirectX(value) => self.material_directx = value,
            material::Edit::SetWrap(value) => self.material_wrap = value,
            material::Edit::SetBakeStrength(value) => self.material_bake_strength = value,
            material::Edit::SetBakeRadius(value) => self.material_bake_radius = value,
            material::Edit::SetPacking(index) => self.material_packing = index.min(2),
            material::Edit::Generate(operation) => self.generate_material_maps(operation),
            material::Edit::BakeSceneAo {
                resolution,
                rays,
                max_distance,
            } => self.bake_scene_ao(resolution, rays, max_distance),
            material::Edit::SetPreviewShape(index) => {
                self.material_preview_shape = index.min(2);
                if let Some(document) = self.material.as_mut() {
                    document.preview_dirty = true;
                }
            }
            material::Edit::OpenHeightPicker => {
                self.material_height_picker = true;
                self.material_height_search.clear();
            }
            material::Edit::OpenPreviewEnvironment => {
                self.material_environment_picker = true;
                self.material_environment_search.clear();
            }
            material::Edit::ClearPreviewEnvironment => {
                self.material_preview_environment.clear();
                if let Some(document) = self.material.as_mut() {
                    document.preview_dirty = true;
                }
            }
            material::Edit::SetPreviewExposure(value) => {
                self.material_preview_exposure = value.clamp(0.1, 4.0);
                if let Some(document) = self.material.as_mut() {
                    document.preview_dirty = true;
                }
            }
            material::Edit::SetSunYaw(value) => {
                self.material_sun_yaw = value.clamp(-180.0, 180.0);
                self.mark_material_preview_dirty();
            }
            material::Edit::SetSunElevation(value) => {
                self.material_sun_elevation = value.clamp(2.0, 89.0);
                self.mark_material_preview_dirty();
            }
            material::Edit::SetSunIntensity(value) => {
                self.material_sun_intensity = value.clamp(0.0, 8.0);
                self.mark_material_preview_dirty();
            }
            material::Edit::SetPreviewShadows(value) => {
                self.material_preview_shadows = value;
                self.mark_material_preview_dirty();
            }
            material::Edit::PreviewPressed => {
                self.material_preview_dragging = true;
                self.material_preview_last = None;
            }
            material::Edit::PreviewMoved(point) => {
                if self.material_preview_dragging {
                    if let Some(last) = self.material_preview_last {
                        self.material_preview_yaw -= (point.x - last.x) * 0.5;
                        self.material_preview_pitch = (self.material_preview_pitch
                            + (point.y - last.y) * 0.5)
                            .clamp(-80.0, 80.0);
                        if let Some(document) = self.material.as_mut() {
                            document.preview_dirty = true;
                        }
                    }
                    self.material_preview_last = Some(point);
                }
            }
            material::Edit::PreviewReleased => {
                self.material_preview_dragging = false;
                self.material_preview_last = None;
            }
            edit => self.edit_material_surface(edit),
        }
    }

    fn edit_material_surface(&mut self, edit: material::Edit) {
        let mut changed = false;
        if let Some(document) = self.material.as_mut() {
            let surface = &mut document.surface;
            match edit {
                material::Edit::Name(value) => {
                    surface.name = value;
                    changed = true;
                }
                material::Edit::Albedo(axis, value) => {
                    surface.albedo[axis.min(3)] = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::Metallic(value) => {
                    surface.metallic = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::Roughness(value) => {
                    surface.roughness = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::Reflectance(value) => {
                    surface.reflectance = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::Emission(axis, value) => {
                    surface.emission[axis.min(2)] = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::EmissionStrength(value) => {
                    surface.emission_strength = value.max(0.0);
                    changed = true;
                }
                material::Edit::NormalStrength(value) => {
                    surface.normal_strength = value.clamp(0.0, 4.0);
                    changed = true;
                }
                material::Edit::HeightScale(value) => {
                    surface.height_scale = value.clamp(0.0, 0.5);
                    changed = true;
                }
                material::Edit::ParallaxSteps(value) => {
                    surface.parallax_steps = value.clamp(4.0, 128.0);
                    changed = true;
                }
                material::Edit::Geometric(value) => {
                    surface.displacement_mode = i32::from(value);
                    changed = true;
                }
                material::Edit::DisplacementScale(value) => {
                    surface.displacement_scale = value.clamp(0.0, 0.5);
                    changed = true;
                }
                material::Edit::Subdivisions(value) => {
                    surface.mesh_subdivisions = (value.round() as i32).clamp(1, 9);
                    changed = true;
                }
                material::Edit::NormalDirectX(value) => {
                    surface.normal_directx = value;
                    changed = true;
                }
                material::Edit::AoStrength(value) => {
                    surface.ao_strength = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::UvTiling(axis, value) => {
                    surface.uv_tiling[axis.min(1)] = value.max(0.001);
                    changed = true;
                }
                material::Edit::UvObjectScale(value) => {
                    surface.uv_object_scale = value;
                    changed = true;
                }
                material::Edit::UvOffset(axis, value) => {
                    surface.uv_offset[axis.min(1)] = value;
                    changed = true;
                }
                material::Edit::AlphaMode(index) => {
                    surface.alpha_mode = match index {
                        1 => doppio_engine::MaterialAlphaMode::Cutout,
                        2 => doppio_engine::MaterialAlphaMode::Blend,
                        _ => doppio_engine::MaterialAlphaMode::Opaque,
                    };
                    changed = true;
                }
                material::Edit::AlphaCutoff(value) => {
                    surface.alpha_cutoff = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::Transmission(value) => {
                    surface.transmission = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::Ior(value) => {
                    surface.ior = value.clamp(1.0, 3.0);
                    changed = true;
                }
                material::Edit::Clearcoat(value) => {
                    surface.clearcoat = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::ClearcoatRoughness(value) => {
                    surface.clearcoat_roughness = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::Sheen(axis, value) => {
                    surface.sheen_color[axis.min(2)] = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::SheenRoughness(value) => {
                    surface.sheen_roughness = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::WindStrength(value) => {
                    surface.wind_strength = value.clamp(0.0, 0.5);
                    changed = true;
                }
                material::Edit::WindSpeed(value) => {
                    surface.wind_speed = value.clamp(0.0, 8.0);
                    changed = true;
                }
                material::Edit::WindFlutter(value) => {
                    surface.wind_flutter = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::ModTint(axis, value) => {
                    surface.mod_tint[axis.min(2)] = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::ModWetness(value) => {
                    surface.mod_wetness = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::ModSnow(value) => {
                    surface.mod_snow = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::ModAge(value) => {
                    surface.mod_age = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::Translucency(value) => {
                    surface.translucency = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::Iridescence(value) => {
                    surface.iridescence = value.clamp(0.0, 1.0);
                    changed = true;
                }
                material::Edit::IridescenceThickness(value) => {
                    surface.iridescence_thickness = value.clamp(50.0, 1500.0);
                    changed = true;
                }
                material::Edit::IridescenceIor(value) => {
                    surface.iridescence_ior = value.clamp(1.0, 2.5);
                    changed = true;
                }
                material::Edit::Reflection(value) => {
                    surface.reflection = value.clamp(0.0, 1.0);
                    changed = true;
                }
                _ => {}
            }
            if changed {
                document.dirty = true;
                document.preview_dirty = true;
            }
        }
    }

    fn choose_material_texture(&mut self, source: String) {
        let Some(slot) = self.material_picker.take() else {
            return;
        };
        self.assign_material_texture(slot, source);
    }

    /// Merges an external height image into the normal map's alpha (parallax depth).
    /// The result is a new `<material>_height_normal.png` next to the material, so the
    /// artist's original normal map is never overwritten. Without a normal map the
    /// normal is derived from the height first.
    fn pack_height_into_normal(&mut self, source: &str) {
        let Some(engine) = self.engine.as_ref() else {
            self.log(
                LogLevel::Error,
                "Packing a height map needs an engine session",
            );
            return;
        };
        let Some(document) = self.material.as_ref() else {
            return;
        };
        let resolve = |path: &str| -> Option<String> {
            engine
                .resolve_path(path)
                .ok()
                .filter(|resolved| std::path::Path::new(resolved).exists())
                .or_else(|| engine.asset_path(path).ok())
        };
        let Some(height_path) = resolve(source) else {
            self.log(LogLevel::Error, format!("Height map not found: {source}"));
            return;
        };
        let Some(file_path) = document.file_path.clone() else {
            self.log(
                LogLevel::Error,
                "Save the material before packing a height map",
            );
            return;
        };
        let directory = std::path::Path::new(&file_path)
            .parent()
            .map(std::path::Path::to_path_buf)
            .unwrap_or_default();
        let asset_path = document.asset_path.clone().unwrap_or_default();
        let asset_directory = std::path::Path::new(&asset_path)
            .parent()
            .map(|path| path.to_string_lossy().replace('\\', "/"))
            .unwrap_or_default();
        let stem = std::path::Path::new(&asset_path)
            .file_stem()
            .and_then(|stem| stem.to_str())
            .unwrap_or("material")
            .to_owned();
        let out_name = format!("{stem}_height_normal.png");
        let out_path = directory.join(&out_name).to_string_lossy().into_owned();
        let existing_normal = if document.surface.normal_map.is_empty() {
            None
        } else {
            resolve(&document.surface.normal_map)
        };
        let normal = match existing_normal {
            Some(path) => engine.load_texture_rgba(&path),
            None => engine
                .generate_texture_map(
                    &height_path,
                    &out_path,
                    doppio_engine::TextureMapKind::Normal,
                    doppio_engine::TextureMapOptions {
                        strength: 1.0,
                        directx: self.material_directx,
                        wrap: true,
                        min_value: 0.0,
                        max_value: 1.0,
                        invert: false,
                        radius: 8,
                    },
                )
                .and_then(|_| engine.load_texture_rgba(&out_path)),
        };
        let (Ok((nw, nh, mut normal_pixels)), Ok((hw, hh, height_pixels))) =
            (normal, engine.load_texture_rgba(&height_path))
        else {
            self.log(LogLevel::Error, "Could not read the normal or height image");
            return;
        };
        if !height_pack::pack(
            &mut normal_pixels,
            nw as usize,
            nh as usize,
            &height_pixels,
            hw as usize,
            hh as usize,
        ) || engine
            .save_texture_rgba(&out_path, nw, nh, &normal_pixels)
            .is_err()
        {
            self.log(LogLevel::Error, "Could not write the packed normal map");
            return;
        }
        let relative = if asset_directory.is_empty() {
            out_name
        } else {
            format!("{asset_directory}/{out_name}")
        };
        let project_path = engine
            .asset_project_path(&relative)
            .unwrap_or_else(|_| relative.clone());
        if let Some(document) = self.material.as_mut() {
            document.surface.normal_map = project_path;
            if document.surface.height_scale <= 0.0 {
                document.surface.height_scale = 0.035;
            }
            document.dirty = true;
            document.preview_dirty = true;
        }
        self.refresh_material_thumbnails();
        self.log(
            LogLevel::Action,
            format!("Packed height {source} into {relative}"),
        );
    }

    fn assign_material_texture(&mut self, slot: material::TextureSlot, source: String) {
        let project_path = self
            .engine
            .as_ref()
            .and_then(|engine| engine.asset_project_path(&source).ok())
            .unwrap_or_else(|| source.clone());
        if let Some(document) = self.material.as_mut() {
            match slot {
                material::TextureSlot::Albedo => {
                    document.surface.albedo_map = project_path;
                    document.dirty = true;
                    document.preview_dirty = true;
                }
                material::TextureSlot::Normal => {
                    document.surface.normal_map = project_path;
                    document.dirty = true;
                    document.preview_dirty = true;
                }
                material::TextureSlot::Orm => {
                    document.surface.orm_map = project_path;
                    document.dirty = true;
                    document.preview_dirty = true;
                }
                material::TextureSlot::Emission => {
                    document.surface.emission_map = project_path;
                    document.dirty = true;
                    document.preview_dirty = true;
                }
                material::TextureSlot::Source => {
                    document.source = source;
                }
            }
        }
        self.refresh_material_thumbnails();
    }

    fn mark_material_preview_dirty(&mut self) {
        self.material_light_generation = self.material_light_generation.wrapping_add(1);
        if let Some(document) = self.material.as_mut() {
            document.preview_dirty = true;
        }
    }

    fn update_material_preview(&mut self) {
        const PREVIEW_SIZE: u32 = 768;
        let visible = self
            .panes
            .iter()
            .any(|(_, group)| group.pages.contains(&PaneKind::Material));
        let yaw = self.material_preview_yaw;
        let pitch = self.material_preview_pitch;
        let shape = match self.material_preview_shape {
            1 => doppio_engine::PreviewShape::Cube,
            2 => doppio_engine::PreviewShape::Plane,
            _ => doppio_engine::PreviewShape::Sphere,
        };
        let environment = self.material_preview_environment.clone();
        let exposure = self.material_preview_exposure;
        let Some(engine) = self.engine.as_mut() else {
            self.material_preview_pending = false;
            self.material_preview_inflight = None;
            self.material_preview_pixels.clear();
            return;
        };
        if self.material_preview_pending {
            match engine.poll_material_preview(&mut self.material_preview_pixels) {
                Ok(doppio_engine::RenderState::Pending) => {}
                Ok(doppio_engine::RenderState::Ready) => {
                    let pixels = std::mem::take(&mut self.material_preview_pixels);
                    self.material_preview = Some(iced::widget::image::Handle::from_rgba(
                        PREVIEW_SIZE,
                        PREVIEW_SIZE,
                        pixels,
                    ));
                    let same = self.material_preview_inflight.as_ref().is_some_and(
                        |(
                            surface,
                            inflight_shape,
                            inflight_environment,
                            inflight_exposure,
                            inflight_yaw,
                            inflight_pitch,
                        )| {
                            self.material.as_ref().is_some_and(|document| {
                                document.surface == *surface
                                    && *inflight_shape == self.material_preview_shape
                                    && inflight_environment == &self.material_preview_environment
                                    && (*inflight_exposure - self.material_preview_exposure).abs()
                                        < 1e-5
                                    && (*inflight_yaw - self.material_preview_yaw).abs() < 1e-5
                                    && (*inflight_pitch - self.material_preview_pitch).abs() < 1e-5
                            })
                        },
                    );
                    if same && self.material_light_inflight == self.material_light_generation {
                        if let Some(document) = self.material.as_mut() {
                            document.preview_dirty = false;
                        }
                    }
                    self.material_preview_pending = false;
                    self.material_preview_inflight = None;
                    self.material_last_preview = Some(Instant::now());
                }
                _ => {
                    self.material_preview_pending = false;
                    self.material_preview_inflight = None;
                    self.material_preview_pixels.clear();
                    self.material_last_preview = Some(Instant::now());
                }
            }
        }
        if !visible {
            return;
        }
        let Some(document) = self.material.as_ref() else {
            return;
        };
        if !document.preview_dirty || self.material_preview_pending {
            return;
        }
        if self
            .material_last_preview
            .is_some_and(|last| last.elapsed() < std::time::Duration::from_millis(120))
        {
            return;
        }
        let surface = document.surface.clone();
        engine.set_material_preview_light(
            self.material_sun_yaw,
            self.material_sun_elevation,
            self.material_sun_intensity,
            self.material_preview_shadows,
        );
        self.material_light_inflight = self.material_light_generation;
        self.material_preview_pixels = vec![0u8; PREVIEW_SIZE as usize * PREVIEW_SIZE as usize * 4];
        if engine
            .begin_material_preview(
                &surface,
                PREVIEW_SIZE,
                yaw,
                pitch,
                shape,
                &environment,
                exposure,
            )
            .is_ok()
        {
            self.material_preview_pending = true;
            self.material_preview_inflight = Some((
                surface,
                self.material_preview_shape,
                environment,
                exposure,
                yaw,
                pitch,
            ));
            self.material_last_preview = Some(Instant::now());
        } else {
            self.material_preview_pixels.clear();
            self.material_last_preview = Some(Instant::now());
            if let Some(document) = self.material.as_mut() {
                document.preview_dirty = false;
            }
            self.material_baking = Some("Preview unavailable".to_owned());
        }
    }

    fn import_materialcraft(&mut self, path: &str) {
        let Some(engine) = self.engine.as_ref() else {
            self.log(LogLevel::Error, "MaterialCraft import: no engine");
            return;
        };
        let resolved = engine
            .resolve_path(path)
            .ok()
            .filter(|resolved| std::path::Path::new(resolved).exists())
            .or_else(|| engine.asset_path(path).ok())
            .unwrap_or_else(|| path.to_owned());
        match crate::uikit::materialcraft_io::import_portable(
            engine,
            std::path::Path::new(&resolved),
            None,
        ) {
            Ok(imported) => {
                self.material_directx = imported.surface.normal_directx;
                self.material = Some(MaterialDocument {
                    surface: imported.surface,
                    asset_path: imported.asset_path.clone(),
                    file_path: Some(imported.material_path.to_string_lossy().into_owned()),
                    dirty: false,
                    preview_dirty: true,
                    source: imported.source_map.clone().unwrap_or_default(),
                    generator_history: generators::GraphHistory::new(imported.graph.clone()),
                    generator_packing: generators::ChannelPacking::default(),
                    generator_bits: 8,
                    generator: imported.graph,
                    thumbnails: [None, None, None, None],
                    source_thumbnail: None,
                });
                self.material_baking = None;
                self.material_last_preview = None;
                self.reset_generator_view();
                self.refresh_assets();
                self.refresh_material_thumbnails();
                self.show_material_pane();
                for warning in &imported.warnings {
                    self.log(LogLevel::Error, format!("MaterialCraft: {warning}"));
                }
                self.log(
                    LogLevel::Action,
                    format!(
                        "Imported MaterialCraft material into {}",
                        imported.material_path.display()
                    ),
                );
            }
            Err(error) => self.log(LogLevel::Error, format!("MaterialCraft import: {error}")),
        }
    }

    fn export_materialcraft(&mut self, path: &str) {
        let Some(engine) = self.engine.as_ref() else {
            self.log(LogLevel::Error, "MaterialCraft export: no engine");
            return;
        };
        let Some(document) = self.material.as_ref() else {
            self.log(
                LogLevel::Error,
                "MaterialCraft export: open a material first",
            );
            return;
        };
        let graph = document.generator.clone();
        let name = if document.surface.name.is_empty() {
            "Doppio Material"
        } else {
            &document.surface.name
        };
        let output = if path.to_ascii_lowercase().ends_with(".mcmat") {
            std::path::PathBuf::from(path)
        } else {
            std::path::PathBuf::from(format!("{path}.mcmat"))
        };
        match crate::uikit::materialcraft_io::export_portable(engine, &graph, name, &output) {
            Ok(exported) => self.log(
                LogLevel::Action,
                format!(
                    "Exported {} maps to {} ({} bytes{})",
                    exported.maps,
                    exported.path.display(),
                    exported.bytes,
                    if exported.unknown_nodes > 0 {
                        format!(", {} placeholder node(s)", exported.unknown_nodes)
                    } else {
                        String::new()
                    }
                ),
            ),
            Err(error) => self.log(LogLevel::Error, format!("MaterialCraft export: {error}")),
        }
    }

    fn bake_scene_ao(&mut self, resolution: u32, rays: u32, max_distance: f32) {
        let Some(engine) = self.engine.as_ref() else {
            self.log(LogLevel::Error, "Scene AO: no engine");
            return;
        };
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            self.material_baking = Some("Scene AO: select a mesh entity first".to_owned());
            self.log(LogLevel::Error, "Scene AO: select a mesh entity first");
            return;
        };
        match engine.bake_ao(entity, resolution, rays, max_distance, "processed/ao") {
            Ok(result) if result.ok => {
                if let Some(document) = self.material.as_mut() {
                    document.surface.orm_map = result.orm_path.clone();
                    document.surface.ao_strength = 1.0;
                    document.dirty = true;
                    document.preview_dirty = true;
                }
                self.material_last_preview = None;
                self.refresh_material_thumbnails();
                let backend = if result.used_gpu { "GPU" } else { "CPU" };
                self.material_baking = Some(format!(
                    "Scene AO baked {}×{} on {backend} ({} triangles)",
                    resolution, resolution, result.triangles
                ));
                self.log(
                    LogLevel::Action,
                    format!("Baked scene AO into {}", result.orm_path),
                );
            }
            Ok(result) => {
                let message = if result.error.is_empty() {
                    "Scene AO bake failed".to_owned()
                } else {
                    result.error
                };
                self.material_baking = Some(message.clone());
                self.log(LogLevel::Error, message);
            }
            Err(_) => {
                self.material_baking = Some("Scene AO bake failed".to_owned());
                self.log(LogLevel::Error, "Scene AO bake failed");
            }
        }
    }

    fn generate_material_maps(&mut self, operation: material::MapOperation) {
        let needs_file = self
            .material
            .as_ref()
            .is_none_or(|document| document.file_path.is_none());
        if needs_file {
            self.save_material();
            let saved = self
                .material
                .as_ref()
                .is_some_and(|document| document.file_path.is_some());
            if !saved {
                self.log(
                    LogLevel::Error,
                    "Map baker: save the material before generating maps",
                );
                return;
            }
        }
        let Some(engine) = self.engine.as_ref() else {
            self.log(LogLevel::Error, "Map baker: no engine");
            return;
        };
        let Some(document) = self.material.as_ref() else {
            return;
        };
        let file_path = document.file_path.clone().unwrap_or_default();
        let asset_path = document.asset_path.clone().unwrap_or_default();
        let directory = std::path::Path::new(&file_path)
            .parent()
            .map(std::path::Path::to_path_buf)
            .unwrap_or_default();
        let asset_directory = std::path::Path::new(&asset_path)
            .parent()
            .map(|path| path.to_string_lossy().replace('\\', "/"))
            .unwrap_or_default();
        let stem = std::path::Path::new(&asset_path)
            .file_stem()
            .and_then(|stem| stem.to_str())
            .unwrap_or("material")
            .to_owned();
        let source_absolute = if !document.source.is_empty() {
            engine.asset_path(&document.source).ok()
        } else if !document.surface.albedo_map.is_empty() {
            engine.resolve_path(&document.surface.albedo_map).ok()
        } else {
            None
        };
        let albedo_absolute = if !document.surface.albedo_map.is_empty() {
            engine.resolve_path(&document.surface.albedo_map).ok()
        } else {
            source_absolute.clone()
        };
        let Some(height_source) = source_absolute.clone() else {
            self.log(LogLevel::Error, "Map baker: choose a source texture first");
            return;
        };

        let options = doppio_engine::TextureMapOptions {
            strength: self.material_bake_strength,
            directx: self.material_directx,
            wrap: self.material_wrap,
            min_value: 0.0,
            max_value: 1.0,
            invert: false,
            radius: self.material_bake_radius.max(1.0) as u32,
        };
        let relative = |name: &str| {
            if asset_directory.is_empty() {
                name.to_owned()
            } else {
                format!("{asset_directory}/{name}")
            }
        };
        let absolute = |name: &str| directory.join(name);

        let mut generated_normal: Option<(String, String)> = None;
        let mut generated_ao: Option<(String, String)> = None;
        let mut generated_roughness: Option<(String, String)> = None;
        let mut generated_metallic: Option<(String, String)> = None;
        let mut error: Option<String> = None;

        let mut run = |source: &str,
                       name: &str,
                       kind: doppio_engine::TextureMapKind,
                       invert: bool,
                       target: &mut Option<(String, String)>| {
            let output = absolute(name);
            let output_text = output.to_string_lossy().to_string();
            let call_options = doppio_engine::TextureMapOptions { invert, ..options };
            match engine.generate_texture_map(source, &output_text, kind, call_options) {
                Ok(()) => *target = Some((relative(name), output_text)),
                Err(_) => error = Some(format!("Could not generate {name}")),
            }
        };

        let wants_normal = matches!(
            operation,
            material::MapOperation::Normal | material::MapOperation::All
        );
        let wants_ao = matches!(
            operation,
            material::MapOperation::AmbientOcclusion
                | material::MapOperation::Orm
                | material::MapOperation::All
        );
        let wants_roughness = matches!(
            operation,
            material::MapOperation::Roughness
                | material::MapOperation::Orm
                | material::MapOperation::All
        );
        let wants_metallic = matches!(
            operation,
            material::MapOperation::Metallic
                | material::MapOperation::Orm
                | material::MapOperation::All
        );
        if wants_normal {
            run(
                &height_source,
                &format!("{stem}_normal.png"),
                doppio_engine::TextureMapKind::Normal,
                false,
                &mut generated_normal,
            );
        }
        if wants_ao {
            run(
                &height_source,
                &format!("{stem}_ao.png"),
                doppio_engine::TextureMapKind::AmbientOcclusion,
                false,
                &mut generated_ao,
            );
        }
        if wants_roughness {
            if let Some(albedo) = &albedo_absolute {
                run(
                    albedo,
                    &format!("{stem}_roughness.png"),
                    doppio_engine::TextureMapKind::Roughness,
                    true,
                    &mut generated_roughness,
                );
            }
        }
        if wants_metallic {
            if let Some(albedo) = &albedo_absolute {
                run(
                    albedo,
                    &format!("{stem}_metallic.png"),
                    doppio_engine::TextureMapKind::Metallic,
                    false,
                    &mut generated_metallic,
                );
            }
        }

        let existing = |name: &str| {
            let path = absolute(name);
            path.exists().then(|| path.to_string_lossy().to_string())
        };
        let ao_path = generated_ao
            .as_ref()
            .map(|(_, absolute)| absolute.clone())
            .or_else(|| existing(&format!("{stem}_ao.png")));
        let roughness_path = generated_roughness
            .as_ref()
            .map(|(_, absolute)| absolute.clone())
            .or_else(|| existing(&format!("{stem}_roughness.png")));
        let metallic_path = generated_metallic
            .as_ref()
            .map(|(_, absolute)| absolute.clone())
            .or_else(|| existing(&format!("{stem}_metallic.png")));
        let pack = matches!(
            operation,
            material::MapOperation::AmbientOcclusion
                | material::MapOperation::Roughness
                | material::MapOperation::Metallic
                | material::MapOperation::Orm
                | material::MapOperation::All
        );
        let mut orm_relative = None;
        let mut packed_relative = None;
        if pack {
            let (pack_kind, name) = match self.material_packing {
                1 => (
                    doppio_engine::TexturePackKind::UnityHdrpMask,
                    format!("{stem}_mask.png"),
                ),
                2 => (
                    doppio_engine::TexturePackKind::UnityMetallicSmoothness,
                    format!("{stem}_metallic_smoothness.png"),
                ),
                _ => (
                    doppio_engine::TexturePackKind::Orm,
                    format!("{stem}_orm.png"),
                ),
            };
            let output = absolute(&name);
            let output_text = output.to_string_lossy().to_string();
            match engine.pack_texture_maps(
                ao_path.as_deref(),
                roughness_path.as_deref(),
                metallic_path.as_deref(),
                &output_text,
                pack_kind,
            ) {
                Ok(()) => {
                    let relative_path = relative(&name);
                    if pack_kind == doppio_engine::TexturePackKind::Orm {
                        orm_relative = Some(relative_path);
                    } else {
                        packed_relative = Some(relative_path);
                    }
                }
                Err(_) => error = Some("Could not pack the material map".to_owned()),
            }
        }

        let normal_relative = generated_normal
            .as_ref()
            .map(|(relative, _)| relative.clone());
        if let Some(document) = self.material.as_mut() {
            if let Some(path) = normal_relative {
                document.surface.normal_map = engine.asset_project_path(&path).unwrap_or(path);
                document.surface.normal_directx = self.material_directx;
                document.surface.ao_strength = 1.0;
                document.dirty = true;
                document.preview_dirty = true;
            }
            if let Some(path) = orm_relative {
                document.surface.orm_map = engine.asset_project_path(&path).unwrap_or(path);
                document.surface.ao_strength = 1.0;
                document.dirty = true;
                document.preview_dirty = true;
            }
        }
        self.refresh_material_thumbnails();
        self.material_last_preview = None;
        let (level, message) = match error {
            Some(error) => (LogLevel::Error, error),
            None => (
                LogLevel::Action,
                packed_relative
                    .map(|path| format!("Packed {path}"))
                    .unwrap_or_else(|| "Maps generated and assigned".to_owned()),
            ),
        };
        self.material_baking = Some(message.clone());
        self.log(level, message);
    }

    fn apply_gizmo(&mut self, edit: gizmo::Edit) {
        let Some(id) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        let (name, key) = (edit.label(), edit.history_key(id));
        match edit {
            gizmo::Edit::Move { position, .. } | gizmo::Edit::MovePlane { position, .. } => {
                if let Some(engine) = self.engine.as_mut() {
                    let _ = engine.set_world_position(id, position);
                }
                let local = self
                    .engine
                    .as_ref()
                    .and_then(|engine| engine.position(id).ok())
                    .unwrap_or(position);
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    entity.position = local;
                }
            }
            gizmo::Edit::Rotate { rotation, .. } => {
                if let Some(engine) = self.engine.as_mut() {
                    let _ = engine.set_rotation(id, rotation);
                }
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    entity.rotation = rotation;
                }
            }
            gizmo::Edit::Scale { scale, .. } | gizmo::Edit::ScaleUniform { scale } => {
                if let Some(engine) = self.engine.as_mut() {
                    let _ = engine.set_scale(id, scale);
                }
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    entity.scale = scale;
                }
            }
        }
        self.sync_selected_transform();
        self.refresh_components();
        self.commit(name, Some(key));
    }

    fn set_gizmo_mode(&mut self, mode: gizmo::Mode) {
        self.gizmo_mode = mode;
        self.log(LogLevel::Action, format!("{} tool", mode.label()));
    }

    fn apply_viewport_settings(&mut self) {
        if let Some(engine) = self.engine.as_mut() {
            if let Err(error) = engine.set_viewport_settings(self.viewport_settings) {
                self.log(
                    LogLevel::Error,
                    format!("Viewport settings failed: {error:?}"),
                );
            }
        }
    }

    /// WASD/QE fly movement. Moves the orbit focus and camera together, so
    /// there is no height clamp at all: you can fly through cloud layers.
    fn fly_camera(&mut self, dt: f32) {
        if self.fly_keys.is_empty() || dt <= 0.0 {
            return;
        }
        let (sin_yaw, cos_yaw) = self.camera.yaw.sin_cos();
        let (sin_pitch, cos_pitch) = self.camera.pitch.sin_cos();
        let look = [-sin_yaw * cos_pitch, -sin_pitch, cos_yaw * cos_pitch];
        let right = [-cos_yaw, 0.0, -sin_yaw];
        let axis = |positive: FlyKey, negative: FlyKey| {
            let positive = self.fly_keys.contains(&positive);
            let negative = self.fly_keys.contains(&negative);
            match (positive, negative) {
                (true, false) => 1.0,
                (false, true) => -1.0,
                _ => 0.0,
            }
        };
        let forward = axis(FlyKey::Forward, FlyKey::Back);
        let strafe = axis(FlyKey::Right, FlyKey::Left);
        let vertical = axis(FlyKey::Up, FlyKey::Down);
        let direction = [
            look[0] * forward + right[0] * strafe,
            look[1] * forward + right[1] * strafe + vertical,
            look[2] * forward + right[2] * strafe,
        ];
        let boost = if self.fly_boost { 4.0 } else { 1.0 };
        let speed = self.camera.distance.max(5.0) * 1.5 * boost;
        for (focus, direction) in self.camera.focus.iter_mut().zip(direction) {
            *focus += direction * speed * dt;
        }
        self.apply_camera();
    }

    fn subscription(&self) -> iced::Subscription<Message> {
        iced::Subscription::batch([
            iced::time::every(std::time::Duration::from_millis(16)).map(|_| Message::ViewportTick),
            iced::event::listen().filter_map(|event| global_event_message(&event)),
            iced::keyboard::listen().filter_map(keyboard_event_message),
        ])
    }

    fn request_viewport(&mut self) {
        if self.viewport_pending {
            return;
        }
        if let Some(last) = self.viewport_last_request {
            if last.elapsed() < VIEWPORT_FRAME_INTERVAL {
                return;
            }
        }
        self.viewport_last_request = Some(Instant::now());
        if self.viewport_desired != self.viewport_render_size {
            let (width, height) = self.viewport_desired;
            self.viewport_render_size = (width, height);
            self.viewport_pixels = vec![0; (width * height * 4) as usize];
        }
        let (width, height) = self.viewport_render_size;
        let camera = self.camera;
        let started = self
            .engine
            .as_mut()
            .map(|engine| engine.render_begin(width, height));
        match started {
            Some(Ok(())) => {
                // The engine may auto-frame a freshly loaded scene on its first
                // render. Use the effective camera so the gizmo projects with
                // exactly the camera that produced this frame.
                let effective = self
                    .engine
                    .as_ref()
                    .and_then(|engine| engine.camera().ok())
                    .unwrap_or(camera);
                self.viewport_pending = true;
                self.viewport_pending_camera = Some(effective);
                self.viewport_started = Some(Instant::now());
            }
            Some(Err(_)) => self.log_render_failure(),
            None => {}
        }
    }

    fn orbit_camera(&mut self, dx: f32, dy: f32) {
        self.camera.yaw += dx * 0.008;
        self.camera.pitch = (self.camera.pitch + dy * 0.008).clamp(-PITCH_LIMIT, PITCH_LIMIT);
        self.apply_camera();
    }

    fn pan_camera(&mut self, dx: f32, dy: f32) {
        let (sin_yaw, cos_yaw) = self.camera.yaw.sin_cos();
        let (sin_pitch, cos_pitch) = self.camera.pitch.sin_cos();
        let right = [-cos_yaw, 0.0, -sin_yaw];
        let forward = [-sin_yaw * cos_pitch, -sin_pitch, cos_yaw * cos_pitch];
        let up = [
            right[1] * forward[2] - right[2] * forward[1],
            right[2] * forward[0] - right[0] * forward[2],
            right[0] * forward[1] - right[1] * forward[0],
        ];
        let scale = self.camera.distance * 0.0018;
        for axis in 0..3 {
            self.camera.focus[axis] += right[axis] * (-dx * scale) + up[axis] * (dy * scale);
        }
        self.apply_camera();
    }

    fn zoom_camera(&mut self, lines: f32) {
        self.camera.distance = (self.camera.distance * 0.9f32.powf(lines)).clamp(0.5, 20_000.0);
        self.apply_camera();
    }

    fn focus_selection(&mut self) {
        let target = self
            .entities
            .get(self.selected)
            .map(|entity| (entity.id, entity.position, entity.name.clone()))
            .map(|(id, local, name)| {
                let world = self
                    .engine
                    .as_ref()
                    .and_then(|engine| engine.world_position(id).ok())
                    .unwrap_or(local);
                (world, name)
            });
        if let Some((position, name)) = target {
            self.camera.focus = position;
            self.camera.distance = (self.camera.distance * 0.5).clamp(1.0, 40.0);
            self.apply_camera();
            self.log(LogLevel::Action, format!("Focused {name}"));
        }
    }

    /// Non-blocking readback: submit one frame, then map it a few ticks later.
    fn pump_viewport(&mut self) {
        let visible = self
            .panes
            .iter()
            .any(|(_, group)| group.active_kind() == PaneKind::Viewport);
        if !visible {
            return;
        }
        if !self.viewport_pending {
            self.request_viewport();
            return;
        }

        let polled = match self.engine.as_mut() {
            Some(engine) => Some(engine.render_poll(&mut self.viewport_pixels)),
            None => None,
        };
        match polled {
            Some(Ok(doppio_engine::RenderState::Ready)) => {
                self.viewport_pending = false;
                self.viewport_frame_camera = self.viewport_pending_camera.take();
                if self.viewport_frame.is_none() {
                    if let Some(camera) = self.viewport_frame_camera {
                        self.camera = camera;
                    }
                }
                if let Some(started) = self.viewport_started.take() {
                    self.viewport_latency_ms = started.elapsed().as_secs_f32() * 1000.0;
                }
                self.viewport_frames += 1;
                let elapsed = self.viewport_fps_window.elapsed().as_secs_f32();
                if elapsed >= 1.0 {
                    self.viewport_fps = self.viewport_frames as f32 / elapsed;
                    self.viewport_hud_latency_ms = self.viewport_latency_ms;
                    self.viewport_frames = 0;
                    self.viewport_fps_window = Instant::now();
                }
                let pixels = std::mem::take(&mut self.viewport_pixels);
                let hash = viewport_sample_hash(&pixels);
                if self.viewport_image_hash == Some(hash) {
                    // Static frame: keep the existing texture and reuse the
                    // readback buffer instead of uploading megabytes again.
                    self.viewport_pixels = pixels;
                } else {
                    self.viewport_image_hash = Some(hash);
                    if self.viewport_frame.is_none() {
                        self.log(LogLevel::Ui, "Viewport ready");
                    }
                    let (width, height) = self.viewport_render_size;
                    self.viewport_uploads += 1;
                    self.viewport_frame = Some(viewport_shader::Frame {
                        pixels: std::sync::Arc::new(pixels),
                        width,
                        height,
                        id: self.viewport_uploads,
                    });
                    self.viewport_pixels = vec![0; (width * height * 4) as usize];
                }
            }
            Some(Ok(doppio_engine::RenderState::Pending)) => {}
            Some(Ok(doppio_engine::RenderState::Idle)) => {
                self.viewport_pending = false;
                self.viewport_pending_camera = None;
            }
            Some(Ok(doppio_engine::RenderState::Failed)) | Some(Err(_)) => {
                self.viewport_pending = false;
                self.viewport_pending_camera = None;
                self.log_render_failure();
            }
            None => {}
        }
    }

    fn log_render_failure(&mut self) {
        if self.log.last().map(|entry| entry.text.as_str()) != Some("Viewport render failed") {
            self.log(LogLevel::Error, "Viewport render failed");
        }
    }

    fn update(&mut self, message: Message) {
        // Pointer noise never changes the scene; everything else may, so markers rebuild.
        let noisy = matches!(
            &message,
            Message::ViewportTick
                | Message::ViewportHover(_)
                | Message::ViewportMoved(_)
                | Message::ViewportKey(_, _)
                | Message::ViewportBoost(_)
                | Message::PointerReleased
                | Message::ViewportResized(_, _)
        );
        self.update_inner(message);
        if !noisy {
            self.markers_dirty = true;
        }
        if self.markers_dirty {
            self.refresh_markers();
        }
    }

    /// Fills the scene with a floor, a few meshes, three kinds of light and a camera, framed so
    /// the gizmos and markers read well. Used by the headless `scene3d` fixture and the docs.
    #[cfg_attr(not(any(test, feature = "headless")), allow(dead_code))]
    pub(crate) fn build_demo_scene(&mut self) {
        let place = |app: &mut App,
                     label: &str,
                     position: [f32; 3],
                     rotation: [f32; 3],
                     scale: [f32; 3]| {
            let Some(index) = prefabs::PREFABS.iter().position(|p| p.label == label) else {
                return;
            };
            app.update(Message::CreatePrefab(index));
            if let (Some(engine), Some(entity)) =
                (app.engine.as_mut(), app.entities.get(app.selected))
            {
                let id = entity.id;
                let _ = engine.set_position(id, position);
                let _ = engine.set_rotation(id, rotation);
                let _ = engine.set_scale(id, scale);
            }
            if let Some(engine) = app.engine.as_ref() {
                app.entities = projectloader::read_entities(engine);
            }
        };
        place(self, "Plane", [0.0, 0.0, 0.0], [0.0; 3], [8.0, 1.0, 8.0]);
        place(
            self,
            "Cube",
            [0.0, 0.5, 0.0],
            [0.0, 25.0, 0.0],
            [1.0, 1.0, 1.0],
        );
        place(
            self,
            "Physics Cube",
            [2.6, 0.5, 1.0],
            [0.0; 3],
            [1.0, 1.0, 1.0],
        );
        place(
            self,
            "Directional Light",
            [-3.0, 4.5, -2.0],
            [-50.0, 35.0, 0.0],
            [1.0; 3],
        );
        place(self, "Point Light", [-2.4, 1.6, 2.2], [0.0; 3], [1.0; 3]);
        place(
            self,
            "Spot Light",
            [3.2, 3.2, -2.6],
            [-55.0, 150.0, 0.0],
            [1.0; 3],
        );
        place(
            self,
            "Camera",
            [-5.0, 2.6, 5.5],
            [-12.0, -38.0, 0.0],
            [1.0; 3],
        );
        self.camera = doppio_engine::Camera {
            yaw: 0.85,
            pitch: 0.5,
            distance: 11.0,
            focus: [0.0, 1.0, 0.0],
        };
        self.selected = 1.min(self.entities.len().saturating_sub(1));
        self.sync_selected_transform();
        self.refresh_components();
        self.markers_dirty = true;
        self.refresh_markers();
    }

    /// Rebuilds the viewport markers from the engine's component data.
    fn refresh_markers(&mut self) {
        self.markers_dirty = false;
        let Some(engine) = self.engine.as_ref() else {
            self.markers.clear();
            return;
        };
        let entities: Vec<scene_markers::EntityTransform> = self
            .entities
            .iter()
            .map(|e| (e.id, e.name.clone(), e.position, e.rotation, e.scale))
            .collect();
        self.markers =
            scene_markers::collect(engine, &entities, |id| engine.world_position(id).ok());
    }

    fn update_inner(&mut self, message: Message) {
        if !matches!(
            &message,
            Message::ToggleMenu(_)
                | Message::HierarchyContext(_)
                | Message::ViewportTick
                | Message::ViewportHover(_)
                | Message::ViewportKey(_, _)
                | Message::ViewportBoost(_)
                | Message::ViewportResized(_, _)
                | Message::ViewportMoved(_)
                | Message::PointerReleased
                | Message::TabEnter(_, _)
                | Message::TabExit(_, _)
                | Message::PaneEnter(_, _)
                | Message::PaneExit(_, _)
                // Hover bookkeeping from widgets behind an open menu must not
                // dismiss it; only deliberate actions (or a click outside,
                // Escape, choosing an item) close menus.
                | Message::HierarchyRowEnter(_)
                | Message::HierarchyRowExit(_)
                | Message::HierarchyRootEnter
                | Message::HierarchyRootExit
                | Message::HierarchyHoverClear
                | Message::Noop
        ) {
            self.open_menu = None;
            self.hierarchy_menu = None;
        }
        match message {
            Message::PaneDragged(event) => {
                if let pane_grid::DragEvent::Dropped { pane, target } = event {
                    self.panes.drop(pane, target);
                }
            }
            Message::PaneResized(event) => {
                self.panes.resize(event.split, event.ratio);
            }
            Message::SelectEntity(index) => {
                self.select_entity_click(index);
            }
            Message::HierarchyContext(index) => {
                if index < self.entities.len() {
                    self.select_index(index);
                    self.hierarchy_menu = Some(index);
                }
            }
            Message::HierarchyDoubleClick(index) => {
                self.select_index(index);
                self.focus_selection();
            }
            Message::HierarchyRowPress(index) => {
                self.select_entity_click(index);
                if let Some(entity) = self.entities.get(index) {
                    self.hierarchy_drag = Some(entity.id);
                    self.hierarchy_hover = Some(entity.id);
                    self.hierarchy_root_hover = false;
                }
            }
            Message::HierarchyRowRelease(index) => {
                if let Some(asset) = self.asset_drag.take() {
                    self.drop_asset_on_entity(asset, index);
                    self.hierarchy_hover = None;
                    self.hierarchy_root_hover = false;
                    return;
                }
                self.hierarchy_hover = self.entities.get(index).map(|entity| entity.id);
                self.hierarchy_root_hover = false;
                self.finish_hierarchy_drag();
            }
            Message::HierarchyRowEnter(index) => {
                self.hierarchy_hover = self.entities.get(index).map(|entity| entity.id);
                self.hierarchy_root_hover = false;
            }
            Message::HierarchyRowExit(index) => {
                if self.hierarchy_hover == self.entities.get(index).map(|entity| entity.id) {
                    self.hierarchy_hover = None;
                }
            }
            Message::HierarchyRootEnter => {
                self.hierarchy_root_hover = true;
                self.hierarchy_hover = None;
            }
            Message::HierarchyRootExit => {
                self.hierarchy_root_hover = false;
            }
            Message::HierarchyRootRelease => {
                self.hierarchy_root_hover = true;
                self.hierarchy_hover = None;
                self.finish_hierarchy_drag();
            }
            Message::HierarchyHoverClear => {
                self.hierarchy_hover = None;
                self.hierarchy_root_hover = false;
            }
            Message::PointerReleased => {
                self.finish_tab_drag();
                self.finish_hierarchy_drag();
                self.asset_drag = None;
            }
            Message::TogglePrefabPalette => {
                self.prefab_open = !self.prefab_open;
                self.open_menu = None;
                self.hierarchy_menu = None;
            }
            Message::ClosePrefabPalette => {
                self.prefab_open = false;
            }
            Message::SelectPrefabCategory(index) => {
                self.prefab_category = index.min(prefabs::CATEGORIES.len().saturating_sub(1));
            }
            Message::CreatePrefab(index) => {
                self.create_prefab(index);
            }
            Message::AssetPressed(index) => {
                self.asset_selected = Some(index);
                self.asset_drag = Some(index);
            }
            Message::AssetReleased(index) => {
                // A single click only selects; opening needs a double click. Releasing a dragged
                // asset on a folder moves it there.
                if let Some(source) = self.asset_drag.take() {
                    if source != index {
                        self.move_asset_into_folder(source, index);
                    }
                }
            }
            Message::AssetFile(action) => self.asset_file_action(action),
            Message::AssetDoubleClick(index) => {
                self.asset_double_clicked(index);
            }
            Message::AssetSearch(text) => {
                self.asset_search = text;
            }
            Message::AssetTreeToggle(path) => {
                if !self.asset_tree_collapsed.remove(&path) {
                    self.asset_tree_collapsed.insert(path);
                }
            }
            Message::AssetTreePanel => {
                self.asset_tree_open = !self.asset_tree_open;
            }
            Message::Wizard(edit) => self.edit_wizard(edit),
            Message::OpenScriptEditor => {
                self.show_script_pane();
            }
            Message::OpenScriptPath(path) => {
                self.open_script_path(&path);
            }
            Message::ScriptSelect(index) => {
                self.script.select(index);
            }
            Message::ScriptTabClose(index) => {
                self.script.close(index);
            }
            Message::ScriptAction(action) => {
                self.script.update(action);
            }
            Message::ScriptSave => {
                if let Some(index) = self.script.current_index() {
                    match self.script.save(index) {
                        Ok(()) => self.log(LogLevel::Action, "Saved script"),
                        Err(error) => self.log(LogLevel::Error, error),
                    }
                }
            }
            Message::ScriptRevert => {
                if let Some(index) = self.script.current_index() {
                    match self.script.revert(index) {
                        Ok(()) => self.log(LogLevel::Action, "Reverted script"),
                        Err(error) => self.log(LogLevel::Error, error),
                    }
                }
            }
            Message::ScriptClose => {
                if let Some(index) = self.script.current_index() {
                    self.script.close(index);
                }
            }
            Message::ScriptAttach => {
                self.attach_active_script();
            }
            Message::ScriptDetach => {
                self.detach_script();
            }
            Message::OpenDryadNodes => {
                self.show_dryad_pane();
            }
            Message::OpenDryadPath(path) => {
                self.open_dryad_path(&path);
            }
            Message::DryadCanvas(event) => self.handle_dryad_canvas(event),
            Message::DryadSearch(search) => {
                self.dryad.search = search;
            }
            Message::DryadAddNode(kind) => {
                let id = self.dryad.add_node(kind);
                if id.is_empty() {
                    self.log(LogLevel::Error, "Open a Dryad script before adding nodes");
                } else {
                    self.show_dryad_pane();
                }
            }
            Message::DryadSelectNode(selected) => {
                self.dryad.selected = selected;
                self.dryad.selected_link = None;
            }
            Message::DryadSelectLink(link) => {
                self.dryad.selected_link = link;
                self.dryad.selected = None;
            }
            Message::DryadNumber { node, field, text } => {
                self.dryad.set_draft(&node, &field, text.clone());
                if let Ok(value) = text.trim().parse::<f64>() {
                    self.dryad.set_number(&node, &field, value);
                }
            }
            Message::DryadBool { node, field, value } => {
                self.dryad.set_bool(&node, &field, value);
            }
            Message::DryadText { node, value } => {
                self.dryad.set_text(&node, value);
            }
            Message::DryadDelete => {
                self.dryad.delete_selected();
            }
            Message::DryadSave => {
                let path = self.dryad.script_path.clone();
                match self.dryad.save() {
                    Ok(()) => {
                        if let Some(path) = path {
                            let _ = self.script.reload_if_clean(&path);
                        }
                        self.log(LogLevel::Action, "Saved Dryad graph");
                    }
                    Err(error) => self.log(LogLevel::Error, error),
                }
            }
            Message::DryadSync => match self.dryad.sync_from_dyd() {
                Ok(()) => self.log(LogLevel::Action, "Synced Dryad graph from .dyd"),
                Err(error) => self.log(LogLevel::Error, error),
            },
            Message::DryadAttach => {
                let relative = if self.dryad.script_relative.is_empty() {
                    self.dryad
                        .script_path
                        .as_ref()
                        .map(|path| path.display().to_string())
                } else {
                    Some(self.dryad.script_relative.clone())
                };
                if let Some(relative) = relative {
                    self.attach_script_path(&relative);
                } else {
                    self.log(LogLevel::Error, "Open a Dryad script before attaching it");
                }
            }
            Message::DryadRun => self.run_dryad_preview(),
            Message::DryadFit => self.dryad.fit_view(),
            Message::DryadAddMotion => self.dryad.add_motion_example(),
            Message::Noop => {}
            Message::AssetNavigate(path) => {
                self.asset_directory = panels::assetmanager::normalize_directory(&path);
                self.asset_search.clear();
                self.asset_selected = None;
            }
            Message::AssetUp => {
                let directory = panels::assetmanager::normalize_directory(&self.asset_directory);
                self.asset_directory =
                    panels::assetmanager::parent_directory(&directory).to_owned();
                self.asset_search.clear();
                self.asset_selected = None;
            }
            Message::AssetRoot => {
                self.asset_directory.clear();
                self.asset_search.clear();
                self.asset_selected = None;
            }
            Message::AssetRefresh => {
                self.refresh_assets();
                self.log(LogLevel::Action, "Refreshed project assets");
            }
            Message::AssetViewMode(mode) => {
                self.asset_view_mode = mode;
            }
            Message::Project(edit) => self.handle_project(edit),
            Message::OpenMaterial(path) => {
                self.open_material_path(&path);
            }
            Message::SetMaterialTexture { slot, path } => {
                self.assign_material_texture(slot, path);
            }
            Message::Material(edit) => {
                self.edit_material(edit);
            }
            Message::TextureSet(edit) => self.edit_texture_set(edit),
            Message::Generator(edit) => {
                self.edit_generator(edit);
            }
            Message::GeneratorTextureSearch(text) => {
                self.generator_texture_search = text;
            }
            Message::GeneratorTexturePick(path) => {
                let target = self.generator_texture_target.take();
                if let (Some(node), Some(document)) = (target, self.material.as_mut()) {
                    if let Some(node) = document.generator.node_mut(node) {
                        if let generators::NodeKind::Image { path: texture, .. }
                        | generators::NodeKind::NormalMap { path: texture, .. } = &mut node.kind
                        {
                            *texture = path;
                        }
                    }
                }
                self.push_generator_history("Set image texture", None);
            }
            Message::GeneratorTextureClose => {
                self.generator_texture_target = None;
            }
            Message::ImportMaterialcraft(path) => self.import_materialcraft(&path),
            Message::ExportMaterialcraft(path) => self.export_materialcraft(&path),
            Message::OpenTextureSet(path) => self.open_texture_set(&path),
            Message::TextureGen(edit) => self.edit_texgen(edit),
            Message::ShowTextureGenerator => self.show_texgen_pane(),
            Message::NatureGen(edit) => self.edit_nature(edit),
            Message::ShowNatureGenerator => self.show_nature_pane(),
            Message::WaterGen(edit) => self.edit_water(edit),
            Message::ShowWaterGenerator => self.show_water_pane(),
            Message::TerrainGen(edit) => self.edit_terrain(edit),
            Message::ShowTerrainGenerator => self.show_terrain_pane(),
            Message::OpenGenerator => {
                self.show_generator_pane();
            }
            Message::MaterialPickerSearch(text) => {
                self.material_picker_search = text;
            }
            Message::MaterialPickerPick(path) => {
                self.choose_material_texture(path);
            }
            Message::MaterialPickerClose => {
                self.material_picker = None;
            }
            Message::MaterialHeightSearch(text) => {
                self.material_height_search = text;
            }
            Message::MaterialHeightPick(path) => {
                self.material_height_picker = false;
                self.material_height_search.clear();
                self.pack_height_into_normal(&path);
            }
            Message::MaterialHeightClose => {
                self.material_height_picker = false;
            }
            Message::MaterialEnvironmentSearch(text) => {
                self.material_environment_search = text;
            }
            Message::MaterialEnvironmentPick(path) => {
                self.material_preview_environment = path;
                self.material_environment_picker = false;
                self.material_environment_search.clear();
                if let Some(document) = self.material.as_mut() {
                    document.preview_dirty = true;
                }
            }
            Message::MaterialEnvironmentClose => {
                self.material_environment_picker = false;
            }
            Message::BehaviourSearch(text) => {
                self.behaviour_search = text;
            }
            Message::BehaviourPick(picked) => {
                self.behaviour_picker = false;
                self.behaviour_search.clear();
                self.add_behaviour(picked);
            }
            Message::BehaviourClose => {
                self.behaviour_picker = false;
            }
            Message::BehaviourNew => {
                // Hand over to the wizard with the Lua template preselected.
                self.behaviour_picker = false;
                self.wizard.template = asset_wizard::Template::LuaScript;
                self.open_wizard(panels::asset_wizard::Tab::Create);
            }
            Message::MaterialAssetSearch(text) => {
                self.material_asset_search = text;
            }
            Message::MaterialAssetPick(path) => {
                self.material_asset_picker = false;
                self.material_asset_search.clear();
                let Some(engine) = self.engine.as_mut() else {
                    return;
                };
                let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
                    self.log(LogLevel::Error, "Material picker: select an entity first");
                    return;
                };
                match engine.apply_material(entity, &path) {
                    Ok(()) => {
                        self.refresh_components();
                        self.log(LogLevel::Action, format!("Assigned {path}"));
                        self.commit("Assign Material", None);
                    }
                    Err(_) => {
                        self.log(LogLevel::Error, format!("Could not assign material {path}"))
                    }
                }
            }
            Message::MaterialAssetClose => {
                self.material_asset_picker = false;
            }
            Message::FileDropped(path) => self.handle_dropped_file(path),
            Message::ImportMeshMaterials(path) => self.import_mesh_materials(&path),
            Message::TabPressed(pane, page) => {
                if let Some(group) = self.panes.get_mut(pane) {
                    if let Some(index) = group.pages.iter().position(|item| *item == page) {
                        group.active = index;
                        self.tab_drag = Some((pane, page));
                        self.tab_hover = Some((pane, page));
                    }
                }
            }
            Message::TabReleased(pane, page) => {
                if self.tab_drag.is_some_and(|(source_pane, source_page)| {
                    source_pane != pane || source_page != page
                }) {
                    self.tab_hover = Some((pane, page));
                    self.finish_tab_drag();
                } else {
                    self.finish_tab_drag();
                }
            }
            Message::TabEnter(pane, page) => {
                self.tab_hover = Some((pane, page));
                self.tab_hover_pane = None;
            }
            Message::TabExit(pane, page) => {
                if self.tab_hover == Some((pane, page)) {
                    self.tab_hover = None;
                }
                if self.tab_drag == Some((pane, page)) {
                    self.tab_dragging = true;
                }
            }
            Message::PaneEnter(pane, _page) => {
                if self.tab_drag.is_some() {
                    self.tab_hover = None;
                    self.tab_hover_pane = Some(pane);
                }
            }
            Message::PaneExit(pane, _page) => {
                if self.tab_hover_pane == Some(pane) {
                    self.tab_hover_pane = None;
                }
            }
            Message::PaneReleased(pane, _page) => {
                if self.tab_drag.is_some() {
                    self.tab_hover = None;
                    self.tab_hover_pane = Some(pane);
                    self.finish_tab_drag();
                }
            }
            Message::SplitPage(pane) => {
                self.split_active_page(pane);
            }
            Message::DockHover(pane, zone) => {
                self.dock_hover = zone.map(|zone| (pane, zone));
            }
            Message::DockDrop(pane, zone) => {
                let source = self.tab_drag;
                self.tab_drag = None;
                self.tab_hover = None;
                self.tab_hover_pane = None;
                self.dock_hover = None;
                self.tab_dragging = false;
                if let Some(source) = source {
                    self.dock_page(source, pane, zone);
                }
            }
            Message::ToggleMaximize(pane) => {
                if self.panes.maximized() == Some(pane) {
                    self.panes.restore();
                } else {
                    self.panes.maximize(pane);
                }
            }
            Message::HierarchyRootToggle => {
                self.hierarchy_root_open = !self.hierarchy_root_open;
            }
            Message::HierarchySearch(value) => {
                self.hierarchy_search = value;
            }
            Message::HierarchyToggle(index) => {
                if let Some(entity) = self.entities.get(index) {
                    let id = entity.id;
                    if !self.collapsed_hierarchy.remove(&id) {
                        self.collapsed_hierarchy.insert(id);
                    }
                }
            }
            Message::TogglePlay => {
                self.playing = !self.playing;
                self.log(LogLevel::Action, if self.playing { "Play" } else { "Stop" });
                if let Some(engine) = self.engine.as_mut() {
                    engine.tick(0.016);
                    let stats = engine.frame_stats();
                    self.log(
                        LogLevel::Engine,
                        format!(
                            "Engine frame {} ({} entities, {:.2} ms)",
                            stats.frame_index,
                            stats.entity_count,
                            stats.delta_time * 1000.0
                        ),
                    );
                }
            }
            Message::AddEntity => {
                let name = format!("Entity {}", self.entities.len() + 1);
                self.add_entity_named(name.clone());
                self.commit(format!("Add {name}"), None);
            }
            Message::AddEntityNamed(name) => {
                self.add_entity_named(name.clone());
                self.commit(format!("Add {name}"), None);
            }
            Message::SelectEntityByName(name) => {
                if let Some(index) = self.entities.iter().position(|entity| entity.name == name) {
                    self.select_index(index);
                } else {
                    self.log(LogLevel::Error, format!("No entity named {name}"));
                }
            }
            Message::SetCamera(camera) => {
                self.camera = camera;
                self.apply_camera();
            }
            Message::SetGizmoMode(mode) => {
                self.set_gizmo_mode(mode);
            }
            Message::GizmoShortcut(key) => {
                if self.viewport_hovered {
                    let mode = match key {
                        1 => gizmo::Mode::Translate,
                        2 => gizmo::Mode::Rotate,
                        3 => gizmo::Mode::Scale,
                        _ => self.gizmo_mode,
                    };
                    self.set_gizmo_mode(mode);
                }
            }
            Message::Gizmo(gizmo::Message::Changed(edit)) => {
                self.apply_gizmo(edit);
            }
            Message::Inspector(edit) => {
                self.handle_inspector(edit);
            }
            Message::SetComponentField {
                component,
                field,
                value,
            } => {
                self.write_component_field(component, field, value);
            }
            Message::SetParent { entity, parent } => {
                self.set_entity_parent(entity, parent);
            }
            Message::SaveScene => {
                if let Some(engine) = self.engine.as_mut() {
                    match engine.scene_path() {
                        Some(path) => match engine.save_scene(&path) {
                            Ok(()) => {
                                self.log(LogLevel::Action, format!("Saved {path}"));
                                self.history.amend_current(|state| state.dirty = false);
                                self.update_project_scene(&path);
                            }
                            Err(_) => self.log(LogLevel::Error, format!("Could not save {path}")),
                        },
                        None => self.log(LogLevel::Error, "No scene path to save"),
                    }
                } else {
                    self.log(LogLevel::Error, "No engine session to save");
                }
            }
            Message::RenderViewport => {
                self.viewport_last_request = None;
                self.request_viewport();
            }
            Message::ViewportTick => {
                self.poll_pack_job();
                let dt = self
                    .viewport_last_tick
                    .map_or(1.0 / 60.0, |last| last.elapsed().as_secs_f32())
                    .clamp(0.001, 0.1);
                self.viewport_last_tick = Some(Instant::now());
                if self.viewport_hovered {
                    self.fly_camera(dt);
                }
                self.pump_water_preview(dt);
                self.tick_engine_water(dt);
                self.pump_viewport();
                self.pump_generator_bake();
                self.pump_generator_preview();
                self.pump_texgen_export(false);
                self.update_material_preview();
            }
            Message::ViewportHover(hovered) => {
                self.viewport_hovered = hovered;
                if !hovered {
                    self.fly_keys.clear();
                    self.fly_boost = false;
                }
            }
            Message::ViewportKey(key, pressed) => {
                if pressed {
                    self.fly_keys.insert(key);
                } else {
                    self.fly_keys.remove(&key);
                }
            }
            Message::ViewportBoost(boost) => {
                self.fly_boost = boost;
            }
            Message::ViewportResized(width, height) => {
                self.viewport_logical = (width, height);
                self.viewport_desired = Self::viewport_target_size(width, height);
            }
            Message::ViewportGridShortcut => {
                if self.viewport_hovered {
                    self.toggle_grid();
                }
            }
            Message::ToggleMarkers => {
                self.show_markers = !self.show_markers;
                let state = if self.show_markers { "on" } else { "off" };
                self.log(LogLevel::Action, format!("Scene markers {state}"));
            }
            Message::MarkerSelected(id) => {
                if let Some(index) = self.entities.iter().position(|entity| entity.id == id) {
                    self.select_index(index);
                }
            }
            Message::ToggleGrid => {
                self.toggle_grid();
            }
            Message::SetViewportSettings {
                show_grid,
                anti_aliasing,
                render_scale,
            } => {
                if let Some(show_grid) = show_grid {
                    self.viewport_settings.show_grid = show_grid;
                }
                if let Some(anti_aliasing) = anti_aliasing {
                    self.viewport_settings.anti_aliasing = anti_aliasing;
                }
                if let Some(render_scale) = render_scale {
                    self.viewport_settings.render_scale = render_scale;
                }
                self.apply_viewport_settings();
                self.log(LogLevel::Action, "Viewport settings updated");
            }
            Message::ViewportPressed(button) => {
                self.viewport_hovered = true;
                self.viewport_drag = Some(ViewportDrag {
                    button,
                    last: None,
                    distance: 0.0,
                });
            }
            Message::ViewportReleased(button) => {
                if button == mouse::Button::Left
                    && self.texture_set_paint_mode
                    && self.texture_set_painting
                {
                    self.finish_texture_set_stroke();
                }
                if let Some(drag) = self.viewport_drag.take() {
                    if button == mouse::Button::Right && drag.distance < 4.0 {
                        self.focus_selection();
                    }
                }
            }
            Message::ViewportMoved(point) => {
                self.viewport_hovered = true;
                let paint_drag = self.texture_set_paint_mode
                    && self
                        .viewport_drag
                        .as_ref()
                        .is_some_and(|drag| drag.button == mouse::Button::Left);
                if paint_drag {
                    if let Some((u, v)) = self.pick_texture_uv(point) {
                        let start = !self.texture_set_painting;
                        self.paint_texture_set([u, v], start);
                    }
                    if let Some(drag) = self.viewport_drag.as_mut() {
                        drag.distance += 1.0;
                        drag.last = Some(point);
                    }
                    return;
                }
                if let Some((button, Some(last))) = self
                    .viewport_drag
                    .as_ref()
                    .map(|drag| (drag.button, drag.last))
                {
                    let dx = point.x - last.x;
                    let dy = point.y - last.y;
                    match button {
                        mouse::Button::Left => self.orbit_camera(dx, dy),
                        mouse::Button::Right | mouse::Button::Middle => self.pan_camera(dx, dy),
                        _ => {}
                    }
                    if let Some(drag) = self.viewport_drag.as_mut() {
                        drag.distance += dx.abs() + dy.abs();
                    }
                }
                if let Some(drag) = self.viewport_drag.as_mut() {
                    drag.last = Some(point);
                }
            }
            Message::ViewportScrolled(delta) => {
                self.viewport_hovered = true;
                let lines = match delta {
                    mouse::ScrollDelta::Lines { y, .. } => y,
                    mouse::ScrollDelta::Pixels { y, .. } => y / 40.0,
                };
                self.zoom_camera(lines);
            }
            Message::ViewportDoubleClick | Message::FocusSelection => {
                self.focus_selection();
            }
            Message::ResetCamera => {
                self.camera = doppio_engine::Camera::default();
                self.apply_camera();
            }
            Message::Undo => {
                self.undo();
            }
            Message::Redo => {
                self.redo();
            }
            Message::CloseScene => {
                self.engine = None;
                self.entities.clear();
                self.assets.clear();
                self.selected = 0;
                self.name_input.clear();
                self.viewport_frame = None;
                self.viewport_frame_camera = None;
                self.viewport_pending_camera = None;
                self.log(LogLevel::Action, "Closed scene");
            }
            Message::ToggleMenu(menu) => {
                self.open_menu = if self.open_menu == Some(menu) {
                    None
                } else {
                    Some(menu)
                };
            }
            Message::CloseMenu => {
                self.open_menu = None;
                self.hierarchy_menu = None;
            }
            Message::ToggleConsoleFilter(level) => {
                if !self.console_filters.remove(&level) {
                    self.console_filters.insert(level);
                }
            }
            Message::Layout(preset) => {
                self.panes =
                    pane_grid::State::with_configuration(Self::layout_configuration(preset));
                self.viewport_hovered = false;
                self.fly_keys.clear();
            }
        }
    }

    fn menu_items(&self, menu: MenuId) -> (f32, Vec<menu::Item<'_, Message>>) {
        match menu {
            MenuId::Project => (
                126.0,
                vec![
                    menu::Item {
                        id: id::PROJECT_MANAGER_MENU,
                        label: "Project Manager…",
                        shortcut: Some("Ctrl+O"),
                        message: Message::Project(project_manager::Edit::Open),
                    },
                    menu::Item {
                        id: id::PROJECT_REFRESH_ASSETS,
                        label: "Refresh Assets",
                        shortcut: Some("F5"),
                        message: Message::AssetRefresh,
                    },
                ],
            ),
            MenuId::Scene => (
                191.0,
                vec![
                    menu::Item {
                        id: id::ADD_ENTITY,
                        label: "Add Entity",
                        shortcut: Some("Ctrl+N"),
                        message: Message::AddEntity,
                    },
                    menu::Item {
                        id: id::CREATE_ENTITY,
                        label: "Create Entity…",
                        shortcut: Some("Ctrl+Shift+N"),
                        message: Message::TogglePrefabPalette,
                    },
                    menu::Item {
                        id: id::MATERIAL_NEW,
                        label: "New Material",
                        shortcut: Some("Ctrl+Shift+M"),
                        message: Message::Material(material::Edit::New),
                    },
                    menu::Item {
                        id: id::PANE_GENERATOR,
                        label: "Material Generator",
                        shortcut: None,
                        message: Message::OpenGenerator,
                    },
                    menu::Item {
                        id: id::SCRIPT_EDITOR_MENU,
                        label: "Script Editor…",
                        shortcut: Some("Ctrl+Shift+E"),
                        message: Message::OpenScriptEditor,
                    },
                    menu::Item {
                        id: id::DRYAD_NODES_MENU,
                        label: "Dryad Nodes…",
                        shortcut: Some("Ctrl+Shift+D"),
                        message: Message::OpenDryadNodes,
                    },
                    menu::Item {
                        id: id::SAVE_SCENE,
                        label: "Save Scene",
                        shortcut: Some("Ctrl+S"),
                        message: Message::SaveScene,
                    },
                    menu::Item {
                        id: id::TOGGLE_PLAY,
                        label: if self.playing { "Pause" } else { "Play" },
                        shortcut: Some("Ctrl+Enter"),
                        message: Message::TogglePlay,
                    },
                ],
            ),
            MenuId::Edit => (
                251.0,
                vec![
                    menu::Item {
                        id: id::UNDO,
                        label: "Undo",
                        shortcut: Some("Ctrl+Z"),
                        message: Message::Undo,
                    },
                    menu::Item {
                        id: id::REDO,
                        label: "Redo",
                        shortcut: Some("Ctrl+Shift+Z"),
                        message: Message::Redo,
                    },
                    menu::Item {
                        id: id::DELETE_ENTITY,
                        label: "Delete Selected",
                        shortcut: None,
                        message: Message::Inspector(panels::inspector::Edit::Delete),
                    },
                ],
            ),
            MenuId::View => (
                341.0,
                vec![
                    menu::Item {
                        id: id::SHOW_TERRAIN_GENERATOR,
                        label: "Terrain Generator",
                        shortcut: None,
                        message: Message::ShowTerrainGenerator,
                    },
                    menu::Item {
                        id: id::SHOW_WATER_GENERATOR,
                        label: "Water Generator",
                        shortcut: None,
                        message: Message::ShowWaterGenerator,
                    },
                    menu::Item {
                        id: id::SHOW_NATURE_GENERATOR,
                        label: "Nature Generator",
                        shortcut: None,
                        message: Message::ShowNatureGenerator,
                    },
                    menu::Item {
                        id: id::SHOW_TEXTURE_GENERATOR,
                        label: "Texture Generator",
                        shortcut: None,
                        message: Message::ShowTextureGenerator,
                    },
                    menu::Item {
                        id: id::FOCUS_SELECTION,
                        label: "Focus Selection",
                        shortcut: Some("Ctrl+F"),
                        message: Message::FocusSelection,
                    },
                    menu::Item {
                        id: id::RESET_CAMERA,
                        label: "Reset Camera",
                        shortcut: Some("Ctrl+0"),
                        message: Message::ResetCamera,
                    },
                    menu::Item {
                        id: id::RENDER_VIEWPORT,
                        label: "Render Now",
                        shortcut: Some("Ctrl+R"),
                        message: Message::RenderViewport,
                    },
                    menu::Item {
                        id: id::TOGGLE_MARKERS,
                        label: if self.show_markers {
                            "Hide Scene Markers"
                        } else {
                            "Show Scene Markers"
                        },
                        shortcut: None,
                        message: Message::ToggleMarkers,
                    },
                    menu::Item {
                        id: id::TOGGLE_GRID,
                        label: if self.viewport_settings.show_grid {
                            "Hide Grid"
                        } else {
                            "Show Grid"
                        },
                        shortcut: Some("G"),
                        message: Message::ToggleGrid,
                    },
                    menu::Item {
                        id: id::GIZMO_MOVE,
                        label: "Move Tool",
                        shortcut: Some("1"),
                        message: Message::SetGizmoMode(gizmo::Mode::Translate),
                    },
                    menu::Item {
                        id: id::GIZMO_ROTATE,
                        label: "Rotate Tool",
                        shortcut: Some("2"),
                        message: Message::SetGizmoMode(gizmo::Mode::Rotate),
                    },
                    menu::Item {
                        id: id::GIZMO_SCALE,
                        label: "Scale Tool",
                        shortcut: Some("3"),
                        message: Message::SetGizmoMode(gizmo::Mode::Scale),
                    },
                    menu::Item {
                        id: id::LAYOUT_DEFAULT,
                        label: "Default Layout",
                        shortcut: Some("Ctrl+1"),
                        message: Message::Layout(LayoutPreset::Default),
                    },
                    menu::Item {
                        id: id::LAYOUT_VIEWPORT,
                        label: "Viewport Layout",
                        shortcut: Some("Ctrl+2"),
                        message: Message::Layout(LayoutPreset::Viewport),
                    },
                ],
            ),
        }
    }

    fn view(&self) -> Element<'_, Message> {
        let brand = row![
            text("Doppio").size(14).color(theme::TEXT),
            text("Caffeine").size(11).color(theme::TEXT_MUTED),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center);

        let project_menu = menu::trigger(
            id::MENU_PROJECT,
            "Project",
            self.open_menu == Some(MenuId::Project),
            Message::ToggleMenu(MenuId::Project),
        );
        let scene_menu = menu::trigger(
            id::MENU_SCENE,
            "Scene",
            self.open_menu == Some(MenuId::Scene),
            Message::ToggleMenu(MenuId::Scene),
        );
        let edit_menu = menu::trigger(
            id::MENU_EDIT,
            "Edit",
            self.open_menu == Some(MenuId::Edit),
            Message::ToggleMenu(MenuId::Edit),
        );
        let view_menu = menu::trigger(
            id::MENU_VIEW,
            "View",
            self.open_menu == Some(MenuId::View),
            Message::ToggleMenu(MenuId::View),
        );
        let menu_group = toolbar::group(vec![project_menu, scene_menu, edit_menu, view_menu]);

        let engine_status = if self.engine.is_some() {
            status::chip("C++ engine", theme::ChipTone::Accent)
        } else {
            status::chip("fixtures", theme::ChipTone::Neutral)
        };
        let dirty = self
            .engine
            .as_ref()
            .map(|engine| engine.is_dirty())
            .unwrap_or(false);
        let save_status = if dirty {
            status::chip("Unsaved", theme::ChipTone::Warning)
        } else {
            status::chip("Saved", theme::ChipTone::Success)
        };
        let entity_status = status::chip(
            format!("{} entities", self.entities.len()),
            theme::ChipTone::Neutral,
        );
        let play_status = if self.playing {
            status::chip("Playing", theme::ChipTone::Success)
        } else {
            status::chip("Stopped", theme::ChipTone::Neutral)
        };
        let mut status_row = row![engine_status].spacing(6);
        if let Some(active) = self.project.active.as_ref() {
            status_row = status_row.push(status::chip(
                format!("◈ {}", active.name),
                theme::ChipTone::Accent,
            ));
        }
        if let Some(version) = self.engine.as_ref().map(|engine| engine.capi_version()) {
            status_row = status_row.push(status::chip(
                format!("C ABI v{version}"),
                theme::ChipTone::Neutral,
            ));
        }
        let status_row = status_row
            .push(save_status)
            .push(play_status)
            .push(entity_status);
        let bar = toolbar::view(brand.into(), vec![menu_group], status_row.into());

        let grid = pane_grid(&self.panes, |pane, group, _| self.pane_content(pane, group))
            .on_drag(Message::PaneDragged)
            .on_resize(8, Message::PaneResized)
            .min_size(160)
            .spacing(3);

        let base: Element<'_, Message> = container(column![bar, grid].spacing(4).padding(4))
            .width(Length::Fill)
            .height(Length::Fill)
            .style(theme::background)
            .into();

        if self.project.open {
            let manager = project_manager::view(
                &self.project,
                project_manager::Callbacks {
                    on_tab: Box::new(|tab| Message::Project(project_manager::Edit::Tab(tab))),
                    on_name: Box::new(|value| Message::Project(project_manager::Edit::Name(value))),
                    on_location: Box::new(|value| {
                        Message::Project(project_manager::Edit::Location(value))
                    }),
                    on_template: Box::new(|index| {
                        Message::Project(project_manager::Edit::Template(index))
                    }),
                    on_create: Message::Project(project_manager::Edit::Create),
                    on_default_location: Message::Project(project_manager::Edit::DefaultLocation),
                    on_recent_search: Box::new(|value| {
                        Message::Project(project_manager::Edit::RecentSearch(value))
                    }),
                    on_recent_select: Box::new(|index| {
                        Message::Project(project_manager::Edit::RecentSelect(index))
                    }),
                    on_recent_open: Box::new(|index| {
                        Message::Project(project_manager::Edit::RecentOpen(index))
                    }),
                    on_browse_path: Box::new(|value| {
                        Message::Project(project_manager::Edit::BrowsePath(value))
                    }),
                    on_browse_scan: Message::Project(project_manager::Edit::BrowseScan),
                    on_browse_default: Message::Project(project_manager::Edit::BrowseDefault),
                    on_browse_search: Box::new(|value| {
                        Message::Project(project_manager::Edit::BrowseSearch(value))
                    }),
                    on_browse_select: Box::new(|index| {
                        Message::Project(project_manager::Edit::BrowseSelect(index))
                    }),
                    on_browse_open: Box::new(|index| {
                        Message::Project(project_manager::Edit::BrowseOpen(index))
                    }),
                    on_close: Message::Project(project_manager::Edit::Close),
                    on_dismiss_error: Message::Project(project_manager::Edit::DismissError),
                },
            );
            let scrim = container(iced::widget::space())
                .width(Length::Fill)
                .height(Length::Fill)
                .style(theme::scrim);
            let overlay = iced::widget::mouse_area(
                container(manager)
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .center(Length::Fill),
            )
            .on_press(Message::Project(project_manager::Edit::Close));
            iced::widget::stack([base, scrim.into(), overlay.into()]).into()
        } else if self.wizard.open {
            let root = self.asset_root_dir();
            let destination = if self.asset_directory.is_empty() {
                "asset root".to_owned()
            } else {
                self.asset_directory.clone()
            };
            let packer = asset_wizard::find_packer().map(|path| path.display().to_string());
            let pack_output = root
                .as_deref()
                .map(|root| self.pack_output_path(root).display().to_string())
                .unwrap_or_else(|| "(open a project)".to_owned());
            let wizard = panels::asset_wizard::view(
                &self.wizard,
                panels::asset_wizard::Context {
                    destination,
                    packer,
                    pack_output,
                },
                Message::Wizard,
            );
            let scrim = container(iced::widget::space())
                .width(Length::Fill)
                .height(Length::Fill)
                .style(theme::scrim);
            let overlay = iced::widget::mouse_area(
                container(wizard)
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .center(Length::Fill),
            )
            .on_press(Message::Wizard(panels::asset_wizard::Edit::Close));
            iced::widget::stack([base, scrim.into(), overlay.into()]).into()
        } else if let Some(menu) = self.open_menu {
            let (x, items) = self.menu_items(menu);
            let overlay =
                iced::widget::mouse_area(menu::overlay(x, items)).on_press(Message::CloseMenu);
            iced::widget::stack([base, overlay.into()]).into()
        } else if let Some(_node) = self.generator_texture_target {
            let search = self.generator_texture_search.to_lowercase();
            let items: Vec<crate::uikit::components::asset_picker::Item> = self
                .assets
                .iter()
                .enumerate()
                .filter(|(_, asset)| asset.kind == "image")
                .filter(|(_, asset)| {
                    search.is_empty() || asset.name.to_lowercase().contains(&search)
                })
                .take(200)
                .map(
                    |(_index, asset)| crate::uikit::components::asset_picker::Item {
                        key: asset.relative_path.clone(),
                        label: asset.name.clone(),
                        detail: asset.relative_path.clone(),
                        thumbnail: self.load_thumbnail(&asset.relative_path),
                    },
                )
                .collect();
            let picker = crate::uikit::components::asset_picker::view(
                "Image node",
                items,
                &self.generator_texture_search,
                crate::uikit::components::asset_picker::Callbacks {
                    ids: crate::uikit::components::asset_picker::Ids {
                        root: id::GENERATOR_TEXTURE_PICKER,
                        close: id::GENERATOR_TEXTURE_PICKER_CLOSE,
                        search: id::GENERATOR_TEXTURE_PICKER_SEARCH,
                        item: id::generator_texture_item,
                    },
                    on_search: Box::new(Message::GeneratorTextureSearch),
                    on_pick: Box::new(Message::GeneratorTexturePick),
                    on_close: Message::GeneratorTextureClose,
                },
            );
            let overlay = iced::widget::mouse_area(
                container(picker)
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .center(Length::Fill),
            )
            .on_press(Message::GeneratorTextureClose);
            iced::widget::stack([base, overlay.into()]).into()
        } else if let Some((_, _, kind)) = self.nature.picker {
            let search = self.nature.picker_search.to_lowercase();
            let items: Vec<crate::uikit::components::asset_picker::Item> = self
                .assets
                .iter()
                .filter(|asset| !asset.is_directory && asset.kind == kind.asset_kind())
                .filter(|asset| search.is_empty() || asset.name.to_lowercase().contains(&search))
                .take(200)
                .map(|asset| crate::uikit::components::asset_picker::Item {
                    key: asset.relative_path.clone(),
                    label: asset.name.clone(),
                    detail: asset.relative_path.clone(),
                    thumbnail: (kind == nature_generator_panel::PickKind::Image)
                        .then(|| self.nature_thumbnail(&asset.relative_path))
                        .flatten(),
                })
                .collect();
            let (title, placeholder, empty) = match kind {
                nature_generator_panel::PickKind::Image => (
                    "Pick an image",
                    "Search images…",
                    "No images in the project: export one from the Texture Generator or import it.",
                ),
                nature_generator_panel::PickKind::Material => (
                    "Pick a material",
                    "Search materials…",
                    "No materials in the project yet.",
                ),
            };
            let picker = crate::uikit::components::asset_picker::view_labeled(
                title,
                items,
                &self.nature.picker_search,
                crate::uikit::components::asset_picker::Callbacks {
                    ids: crate::uikit::components::asset_picker::Ids {
                        root: id::NATUREGEN_PICKER,
                        close: id::NATUREGEN_PICKER_CLOSE,
                        search: id::NATUREGEN_PICKER_SEARCH,
                        item: id::naturegen_picker_item,
                    },
                    on_search: Box::new(|text| {
                        Message::NatureGen(nature_generator_panel::Edit::PickerSearch(text))
                    }),
                    on_pick: Box::new(|path| {
                        Message::NatureGen(nature_generator_panel::Edit::PickerPick(path))
                    }),
                    on_close: Message::NatureGen(nature_generator_panel::Edit::PickerClose),
                },
                crate::uikit::components::asset_picker::Labels {
                    placeholder,
                    badge: kind.badge(),
                    empty,
                    footer: None,
                },
            );
            let overlay = iced::widget::mouse_area(
                container(picker)
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .center(Length::Fill),
            )
            .on_press(Message::NatureGen(
                nature_generator_panel::Edit::PickerClose,
            ));
            iced::widget::stack([base, overlay.into()]).into()
        } else if self.behaviour_picker {
            let catalog = behaviour::catalog(&self.component_types, &self.components, &self.assets);
            let mut sections: Vec<behaviour_picker::Section<Message>> = Vec::new();
            let mut index = 0usize;
            for group in behaviour::GROUPS {
                let rows: Vec<behaviour_picker::Row<Message>> =
                    behaviour::filter(&catalog, &self.behaviour_search)
                        .into_iter()
                        .filter(|entry| entry.group == group)
                        .map(|entry| {
                            let row = behaviour_picker::Row {
                                id: id::behaviour_item(index),
                                label: entry.label.clone(),
                                detail: entry.detail.clone(),
                                tag: entry.added.then_some("Added"),
                                on_pick: (!entry.added)
                                    .then(|| Message::BehaviourPick(entry.behaviour.clone())),
                            };
                            index += 1;
                            row
                        })
                        .collect();
                sections.push(behaviour_picker::Section { title: group, rows });
            }
            let empty = if self.engine.is_none() && self.component_types.is_empty() {
                "No engine session: components are listed once the engine is connected. Project scripts, meshes and materials appear here."
            } else {
                "Nothing matches this search."
            };
            let picker = behaviour_picker::view(
                sections,
                &self.behaviour_search,
                empty,
                behaviour_picker::Callbacks {
                    on_search: Box::new(Message::BehaviourSearch),
                    on_close: Message::BehaviourClose,
                    on_new_script: Message::BehaviourNew,
                },
            );
            let overlay = iced::widget::mouse_area(
                container(picker)
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .center(Length::Fill),
            )
            .on_press(Message::BehaviourClose);
            iced::widget::stack([base, overlay.into()]).into()
        } else if self.material_asset_picker {
            let search = self.material_asset_search.to_lowercase();
            let items: Vec<crate::uikit::components::asset_picker::Item> = self
                .assets
                .iter()
                .enumerate()
                .filter(|(_, asset)| asset.kind == "material")
                .filter(|(_, asset)| {
                    search.is_empty()
                        || asset.name.to_lowercase().contains(&search)
                        || asset.relative_path.to_lowercase().contains(&search)
                })
                .take(200)
                .map(
                    |(index, asset)| crate::uikit::components::asset_picker::Item {
                        key: asset.relative_path.clone(),
                        label: asset.name.clone(),
                        detail: asset.relative_path.clone(),
                        thumbnail: self
                            .asset_thumbnails
                            .get(index)
                            .and_then(Option::as_ref)
                            .cloned(),
                    },
                )
                .collect();
            let picker = crate::uikit::components::asset_picker::view(
                "Material",
                items,
                &self.material_asset_search,
                crate::uikit::components::asset_picker::Callbacks {
                    ids: crate::uikit::components::asset_picker::Ids {
                        root: id::MATERIAL_ASSET_PICKER,
                        close: id::MATERIAL_ASSET_PICKER_CLOSE,
                        search: id::MATERIAL_ASSET_PICKER_SEARCH,
                        item: id::material_asset_item,
                    },
                    on_search: Box::new(Message::MaterialAssetSearch),
                    on_pick: Box::new(Message::MaterialAssetPick),
                    on_close: Message::MaterialAssetClose,
                },
            );
            let overlay = iced::widget::mouse_area(
                container(picker)
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .center(Length::Fill),
            )
            .on_press(Message::MaterialAssetClose);
            iced::widget::stack([base, overlay.into()]).into()
        } else if self.material_height_picker {
            let search = self.material_height_search.to_lowercase();
            let items: Vec<crate::uikit::components::asset_picker::Item> = self
                .assets
                .iter()
                .filter(|asset| asset.kind == "image")
                .filter(|asset| search.is_empty() || asset.name.to_lowercase().contains(&search))
                .take(200)
                .map(|asset| crate::uikit::components::asset_picker::Item {
                    key: asset.relative_path.clone(),
                    label: asset.name.clone(),
                    detail: asset.relative_path.clone(),
                    thumbnail: self.load_thumbnail(&asset.relative_path),
                })
                .collect();
            let picker = crate::uikit::components::asset_picker::view_labeled(
                "Height map → normal alpha",
                items,
                &self.material_height_search,
                crate::uikit::components::asset_picker::Callbacks {
                    ids: crate::uikit::components::asset_picker::Ids {
                        root: id::MATERIAL_HEIGHT_PICKER,
                        close: id::MATERIAL_HEIGHT_PICKER_CLOSE,
                        search: id::MATERIAL_HEIGHT_PICKER_SEARCH,
                        item: id::material_height_item,
                    },
                    on_search: Box::new(Message::MaterialHeightSearch),
                    on_pick: Box::new(Message::MaterialHeightPick),
                    on_close: Message::MaterialHeightClose,
                },
                crate::uikit::components::asset_picker::Labels {
                    placeholder: "Search height images…",
                    badge: "HEIGHT",
                    empty: "No images in the project yet.",
                    footer: None,
                },
            );
            let overlay = iced::widget::mouse_area(
                container(picker)
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .center(Length::Fill),
            )
            .on_press(Message::MaterialHeightClose);
            iced::widget::stack([base, overlay.into()]).into()
        } else if self.material_environment_picker {
            let search = self.material_environment_search.to_lowercase();
            let items: Vec<crate::uikit::components::asset_picker::Item> = self
                .assets
                .iter()
                .enumerate()
                .filter(|(_, asset)| asset.kind == "image")
                .filter(|(_, asset)| {
                    search.is_empty() || asset.name.to_lowercase().contains(&search)
                })
                .take(200)
                .map(
                    |(_index, asset)| crate::uikit::components::asset_picker::Item {
                        key: asset.relative_path.clone(),
                        label: asset.name.clone(),
                        detail: asset.relative_path.clone(),
                        thumbnail: self.load_thumbnail(&asset.relative_path),
                    },
                )
                .collect();
            let picker = crate::uikit::components::asset_picker::view(
                "Preview environment",
                items,
                &self.material_environment_search,
                crate::uikit::components::asset_picker::Callbacks {
                    ids: crate::uikit::components::asset_picker::Ids {
                        root: id::MATERIAL_ENVIRONMENT_PICKER,
                        close: id::MATERIAL_ENVIRONMENT_PICKER_CLOSE,
                        search: id::MATERIAL_ENVIRONMENT_PICKER_SEARCH,
                        item: id::material_environment_item,
                    },
                    on_search: Box::new(Message::MaterialEnvironmentSearch),
                    on_pick: Box::new(Message::MaterialEnvironmentPick),
                    on_close: Message::MaterialEnvironmentClose,
                },
            );
            let overlay = iced::widget::mouse_area(
                container(picker)
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .center(Length::Fill),
            )
            .on_press(Message::MaterialEnvironmentClose);
            iced::widget::stack([base, overlay.into()]).into()
        } else if let Some(slot) = self.material_picker {
            let search = self.material_picker_search.to_lowercase();
            let items: Vec<crate::uikit::components::asset_picker::Item> = self
                .assets
                .iter()
                .enumerate()
                .filter(|(_, asset)| asset.kind == "image")
                .filter(|(_, asset)| {
                    search.is_empty() || asset.name.to_lowercase().contains(&search)
                })
                .take(200)
                .map(
                    |(_index, asset)| crate::uikit::components::asset_picker::Item {
                        key: asset.relative_path.clone(),
                        label: asset.name.clone(),
                        detail: asset.relative_path.clone(),
                        thumbnail: self.load_thumbnail(&asset.relative_path),
                    },
                )
                .collect();
            let picker = crate::uikit::components::asset_picker::view(
                slot.label(),
                items,
                &self.material_picker_search,
                crate::uikit::components::asset_picker::Callbacks {
                    ids: crate::uikit::components::asset_picker::Ids {
                        root: id::MATERIAL_PICKER,
                        close: id::MATERIAL_PICKER_CLOSE,
                        search: id::MATERIAL_PICKER_SEARCH,
                        item: id::material_picker_item,
                    },
                    on_search: Box::new(Message::MaterialPickerSearch),
                    on_pick: Box::new(Message::MaterialPickerPick),
                    on_close: Message::MaterialPickerClose,
                },
            );
            let overlay = iced::widget::mouse_area(
                container(picker)
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .center(Length::Fill),
            )
            .on_press(Message::MaterialPickerClose);
            iced::widget::stack([base, overlay.into()]).into()
        } else if self.prefab_open {
            let active = prefabs::CATEGORIES
                .get(self.prefab_category)
                .map(|category| category.key)
                .unwrap_or("essentials");
            let palette = prefab_palette::view(
                prefabs::CATEGORIES,
                active,
                prefabs::PREFABS,
                prefab_palette::Callbacks {
                    on_category: Box::new(|key| {
                        Message::SelectPrefabCategory(
                            prefabs::CATEGORIES
                                .iter()
                                .position(|category| category.key == key)
                                .unwrap_or(0),
                        )
                    }),
                    on_item: Box::new(|key| {
                        Message::CreatePrefab(
                            prefabs::PREFABS
                                .iter()
                                .position(|prefab| prefab.key == key)
                                .unwrap_or(0),
                        )
                    }),
                    on_close: Message::ClosePrefabPalette,
                },
            );
            let overlay = iced::widget::mouse_area(
                container(palette)
                    .width(Length::Fill)
                    .height(Length::Fill)
                    .center(Length::Fill),
            )
            .on_press(Message::ClosePrefabPalette);
            iced::widget::stack([base, overlay.into()]).into()
        } else {
            base
        }
    }

    fn pane_content<'a>(
        &'a self,
        pane: pane_grid::Pane,
        group: &'a PaneGroup,
    ) -> pane_grid::Content<'a, Message> {
        let active = group.active_kind();
        let (pane_id, _title, subtitle, body) = self.pane_parts(active);
        let tab_drag = self.tab_drag;
        let tab_hover = self.tab_hover;
        let tabs = group
            .pages
            .iter()
            .map(|kind| tabs::Tab {
                id: kind.tab_id(),
                label: kind.title(),
                active: *kind == active,
                hovered: tab_hover == Some((pane, *kind)),
                drop_target: tab_drag.is_some()
                    && tab_drag != Some((pane, *kind))
                    && tab_hover == Some((pane, *kind)),
                on_press: Message::TabPressed(pane, *kind),
                on_release: Message::TabReleased(pane, *kind),
                on_enter: Message::TabEnter(pane, *kind),
                on_exit: Message::TabExit(pane, *kind),
            })
            .collect();
        let split = (group.pages.len() > 1).then(|| {
            (
                id::pane_split(&format!("{pane:?}")),
                Message::SplitPage(pane),
            )
        });
        let maximized = self.panes.maximized() == Some(pane);
        let controls = tabs::controls(
            split,
            (
                id::pane_maximize(&format!("{pane:?}")),
                maximized,
                Message::ToggleMaximize(pane),
            ),
        );
        let title_bar = pane_grid::TitleBar::new(tabs::view(tabs, subtitle, None))
            .controls(controls)
            .always_show_controls()
            .padding([0, 2])
            .style(theme::panel_title);
        let body = container(
            iced::widget::mouse_area(container(panel::body(body)).id(pane_id))
                .on_enter(Message::PaneEnter(pane, active))
                .on_exit(Message::PaneExit(pane, active))
                .on_release(Message::PaneReleased(pane, active)),
        )
        .width(Length::Fill)
        .height(Length::Fill);
        let body: Element<'a, Message> = if self.tab_drag.is_some() && self.tab_dragging {
            let hovered = self
                .dock_hover
                .filter(|(target, _)| *target == pane)
                .map(|(_, zone)| zone);
            iced::widget::stack![
                body,
                dock_overlay::view(
                    hovered,
                    move |zone| Message::DockHover(pane, zone),
                    move |zone| Message::DockDrop(pane, zone),
                )
            ]
            .into()
        } else {
            body.into()
        };
        pane_grid::Content::new(body).title_bar(title_bar)
    }

    fn pane_parts(
        &self,
        kind: PaneKind,
    ) -> (id::UiId, &'static str, Option<String>, Element<'_, Message>) {
        let (pane_id, title, subtitle, body): (
            id::UiId,
            &str,
            Option<String>,
            Element<'_, Message>,
        ) = match kind {
            PaneKind::Hierarchy => {
                let query = self.hierarchy_search.trim().to_lowercase();
                let subtitle = if query.is_empty() {
                    format!("{} entities", self.entities.len())
                } else {
                    let matches = self
                        .entities
                        .iter()
                        .filter(|entity| entity.name.to_lowercase().contains(&query))
                        .count();
                    format!("{matches} matches")
                };
                (
                    id::PANE_HIERARCHY,
                    "Hierarchy",
                    Some(subtitle),
                    panels::hierarchy::view(
                        panels::hierarchy::View {
                            entities: &self.entities,
                            selected: self.selected,
                            collapsed: &self.collapsed_hierarchy,
                            menu_for: self.hierarchy_menu,
                            root_label: &self.scene_label,
                            root_open: self.hierarchy_root_open,
                            search: &self.hierarchy_search,
                            dragging: self.hierarchy_drag,
                            hovered: self.hierarchy_hover,
                            root_hovered: self.hierarchy_root_hover,
                        },
                        panels::hierarchy::Callbacks {
                            on_root_toggle: Message::HierarchyRootToggle,
                            on_search: Box::new(Message::HierarchySearch),
                            on_toggle: Box::new(Message::HierarchyToggle),
                            on_press: Box::new(Message::HierarchyRowPress),
                            on_release: Box::new(Message::HierarchyRowRelease),
                            on_enter: Box::new(Message::HierarchyRowEnter),
                            on_exit: Box::new(Message::HierarchyRowExit),
                            on_context: Box::new(Message::HierarchyContext),
                            on_root_enter: Message::HierarchyRootEnter,
                            on_root_exit: Message::HierarchyRootExit,
                            on_root_release: Message::HierarchyRootRelease,
                            on_empty_hover: Message::HierarchyHoverClear,
                            on_open_create: Message::TogglePrefabPalette,
                            on_action: Box::new(|action| match action {
                                panels::hierarchy::Action::AddEntity => Message::AddEntity,
                                panels::hierarchy::Action::Focus => Message::FocusSelection,
                                panels::hierarchy::Action::Delete => {
                                    Message::Inspector(panels::inspector::Edit::Delete)
                                }
                            }),
                        },
                    ),
                )
            }
            PaneKind::Assets => {
                let count = panels::assetmanager::visible_count(
                    &self.assets,
                    &self.asset_directory,
                    &self.asset_search,
                );
                let subtitle = if !self.asset_search.trim().is_empty() {
                    format!("{count} matches")
                } else if self.asset_directory.is_empty() {
                    format!("{count} items · root")
                } else {
                    format!("{count} items · {}", self.asset_directory)
                };
                (
                    id::PANE_ASSETS,
                    "Assets",
                    Some(subtitle),
                    panels::assetmanager::view(
                        panels::assetmanager::View {
                            assets: &self.assets,
                            thumbnails: &self.asset_thumbnails,
                            tints: &self.asset_tints,
                            search: &self.asset_search,
                            directory: &self.asset_directory,
                            mode: self.asset_view_mode,
                            selected: self.asset_selected,
                            edit: self.asset_edit.as_ref(),
                            collapsed: &self.asset_tree_collapsed,
                            tree_open: self.asset_tree_open,
                        },
                        panels::assetmanager::Callbacks {
                            on_search: Box::new(Message::AssetSearch),
                            on_press: Box::new(Message::AssetPressed),
                            on_release: Box::new(Message::AssetReleased),
                            on_double_click: Box::new(Message::AssetDoubleClick),
                            on_folder: Box::new(Message::AssetNavigate),
                            on_crumb: Box::new(Message::AssetNavigate),
                            on_up: Message::AssetUp,
                            on_root: Message::AssetRoot,
                            on_refresh: Message::AssetRefresh,
                            on_mode: Box::new(Message::AssetViewMode),
                            on_new: Message::Wizard(panels::asset_wizard::Edit::Open(
                                panels::asset_wizard::Tab::Create,
                            )),
                            on_import: Message::Wizard(panels::asset_wizard::Edit::Open(
                                panels::asset_wizard::Tab::Import,
                            )),
                            on_tree_toggle: Box::new(Message::AssetTreeToggle),
                            on_noop: Message::Noop,
                            on_tree_panel: Message::AssetTreePanel,
                            on_file: Box::new(Message::AssetFile),
                        },
                    ),
                )
            }
            PaneKind::Viewport => {
                let scene = self
                    .engine
                    .as_ref()
                    .and_then(|engine| engine.scene_path())
                    .and_then(|path| path.rsplit(['/', '\\']).next().map(str::to_owned))
                    .unwrap_or_else(|| "No scene".to_owned());
                let grid = if self.viewport_settings.show_grid {
                    "grid"
                } else {
                    "no grid"
                };
                let aa = match self.viewport_settings.anti_aliasing {
                    doppio_engine::AntiAliasing::Off => "no AA",
                    doppio_engine::AntiAliasing::Fxaa => "FXAA",
                    doppio_engine::AntiAliasing::Taa => "TAA",
                };
                let status_line = format!(
                    "{}×{} · {} · {} · {} · {:.0} ms · {:.0} FPS",
                    self.viewport_render_size.0,
                    self.viewport_render_size.1,
                    grid,
                    aa,
                    self.gizmo_mode.label(),
                    self.viewport_hud_latency_ms,
                    self.viewport_fps,
                );
                let hint = self
                    .entities
                    .get(self.selected)
                    .map(|entity| format!("Selected: {} · Double-click to focus", entity.name))
                    .unwrap_or_else(|| "Double-click to focus the selection".to_owned());
                let world_position = self.entities.get(self.selected).and_then(|entity| {
                    self.engine
                        .as_ref()
                        .and_then(|engine| engine.world_position(entity.id).ok())
                });
                let state = panels::sceneviewport::ViewportState {
                    frame: self.viewport_frame.as_ref(),
                    status: status_line,
                    hint,
                    empty: self.engine.is_some() && self.entities.is_empty(),
                    dragging: self.viewport_drag.is_some(),
                    camera: self.viewport_frame_camera.as_ref().unwrap_or(&self.camera),
                    size: Size::new(self.viewport_logical.0, self.viewport_logical.1),
                    mode: self.gizmo_mode,
                    markers: &self.markers,
                    show_markers: self.show_markers && self.engine.is_some(),
                    selected_entity: self.entities.get(self.selected).map(|entity| entity.id),
                    target: self
                        .entities
                        .get(self.selected)
                        .filter(|_| self.engine.is_some())
                        .map(|entity| gizmo::Target {
                            position: entity.position,
                            world_position: world_position.unwrap_or(entity.position),
                            rotation: entity.rotation,
                            scale: entity.scale,
                        }),
                };
                let callbacks = panels::sceneviewport::ViewportCallbacks {
                    surface_left_press: Message::ViewportPressed(mouse::Button::Left),
                    surface_right_press: Message::ViewportPressed(mouse::Button::Right),
                    surface_left_release: Message::ViewportReleased(mouse::Button::Left),
                    surface_right_release: Message::ViewportReleased(mouse::Button::Right),
                    surface_exit: Message::ViewportHover(false),
                    surface_move: Box::new(Message::ViewportMoved),
                    surface_scroll: Box::new(Message::ViewportScrolled),
                    surface_double_click: Message::ViewportDoubleClick,
                    surface_resized: Box::new(|size| {
                        Message::ViewportResized(size.width, size.height)
                    }),
                    surface_gizmo: Box::new(Message::Gizmo),
                    surface_marker: Box::new(|message| match message {
                        scene_markers::Message::Select(id) => Message::MarkerSelected(id),
                    }),
                };
                (
                    id::PANE_VIEWPORT,
                    "Viewport",
                    Some(scene),
                    panels::sceneviewport::view(state, callbacks),
                )
            }
            PaneKind::Generator => {
                if let Some(document) = &self.material {
                    let generator_view = material_generator::View {
                        graph: &document.generator,
                        selected: self.generator_selected,
                        selected_link: self.generator_link,
                        pan: self.generator_pan,
                        zoom: self.generator_zoom,
                        preview: self
                            .generator_preview_maps
                            .get(self.generator_preview_channel)
                            .or(self.generator_preview.as_ref()),
                        preview_channel: self.generator_preview_channel,
                        preview_3d: self.material_preview.as_ref(),
                        node_previews: &self.generator_node_previews,
                        status: self.generator_status.as_deref(),
                        material_name: Some(&document.surface.name),
                        dirty: document.dirty,
                        can_undo: document.generator_history.can_undo(),
                        undo_name: document.generator_history.undo_name(),
                        can_redo: document.generator_history.can_redo(),
                        redo_name: document.generator_history.redo_name(),
                        snap: self.generator_snap,
                        search: &self.generator_search,
                        collapsed_groups: &self.generator_palette_collapsed,
                        clipboard: self.generator_clipboard.is_some(),
                        packing: &document.generator_packing,
                        packing_user_presets: &self.packing_user_presets,
                        packing_preset_name: &self.packing_preset_name,
                        bits: document.generator_bits,
                        baking: self.generator_bake.as_ref().map(|job| {
                            material_generator::BakeProgress {
                                fraction: job.progress,
                                stage: &job.stage,
                            }
                        }),
                    };
                    (
                        id::PANE_GENERATOR,
                        "Material Generator",
                        Some(format!(
                            "{} · {}×{}",
                            document.surface.name,
                            document.generator.resolution,
                            document.generator.resolution
                        )),
                        material_generator::view(generator_view, Message::Generator),
                    )
                } else {
                    (
                        id::PANE_GENERATOR,
                        "Material Generator",
                        Some("No material open".to_owned()),
                        container(
                            column![
                                text("Open or create a material first")
                                    .size(12)
                                    .color(theme::TEXT),
                                button::primary(
                                    id::MATERIAL_NEW,
                                    "New Material",
                                    Message::Material(material::Edit::New)
                                ),
                            ]
                            .spacing(6),
                        )
                        .padding(10)
                        .into(),
                    )
                }
            }
            PaneKind::Inspector => {
                let selected = self
                    .entities
                    .get(self.selected)
                    .map(|entity| format!("ID {}", entity.id))
                    .unwrap_or_else(|| "No selection".to_owned());
                (
                    id::PANE_INSPECTOR,
                    "Inspector",
                    Some(selected),
                    panels::inspector::view(
                        panels::inspector::Inspector {
                            entity: self.entities.get(self.selected),
                            name_input: &self.name_input,
                            components: &self.components,
                            component_types: &self.component_types,
                            field_inputs: &self.field_inputs,
                            transform_inputs: &self.transform_inputs,
                            collapsed_components: &self.collapsed_components,
                            material: self.selected_material.as_deref(),
                            material_thumbnail: self.selected_material_thumbnail.as_ref(),
                        },
                        Message::Inspector,
                    ),
                )
            }
            PaneKind::Material => {
                let (subtitle, body) = if let Some(document) = &self.material {
                    let path = document
                        .asset_path
                        .clone()
                        .unwrap_or_else(|| "unsaved".to_owned());
                    let subtitle = if document.dirty {
                        format!("{path} • unsaved")
                    } else {
                        path
                    };
                    let view = material::MaterialView {
                        material: &document.surface,
                        asset_path: document.asset_path.as_deref(),
                        dirty: document.dirty,
                        preview: self.material_preview.as_ref(),
                        thumbnails: [
                            document.thumbnails[0].as_ref(),
                            document.thumbnails[1].as_ref(),
                            document.thumbnails[2].as_ref(),
                            document.thumbnails[3].as_ref(),
                        ],
                        source: &document.source,
                        source_thumbnail: document.source_thumbnail.as_ref(),
                        presets: &self.material_presets,
                        collapsed: &self.material_collapsed,
                        baking: self.material_baking.as_deref(),
                        directx: self.material_directx,
                        wrap: self.material_wrap,
                        bake_strength: self.material_bake_strength,
                        bake_radius: self.material_bake_radius,
                        packing: self.material_packing,
                        engine_ready: self.engine.is_some(),
                        preview_yaw: self.material_preview_yaw,
                        preview_pitch: self.material_preview_pitch,
                        preview_shape: self.material_preview_shape,
                        preview_environment: &self.material_preview_environment,
                        preview_exposure: self.material_preview_exposure,
                        sun_yaw: self.material_sun_yaw,
                        sun_elevation: self.material_sun_elevation,
                        sun_intensity: self.material_sun_intensity,
                        preview_shadows: self.material_preview_shadows,
                        map_details: [
                            self.material_map_details[0].as_str(),
                            self.material_map_details[1].as_str(),
                            self.material_map_details[2].as_str(),
                            self.material_map_details[3].as_str(),
                        ],
                    };
                    (Some(subtitle), material::view(view, Message::Material))
                } else {
                    (
                        Some("No material open".to_owned()),
                        container(
                            column![
                                text("No material open").size(12).color(theme::TEXT),
                                text("Select a .mat or an image in Assets, or create a new material.")
                                    .size(10)
                                    .color(theme::TEXT_MUTED),
                                button::primary(
                                    id::MATERIAL_NEW,
                                    "New Material",
                                    Message::Material(material::Edit::New)
                                ),
                            ]
                            .spacing(6),
                        )
                        .padding(10)
                        .into(),
                    )
                };
                (id::PANE_MATERIAL, "Material", subtitle, body)
            }
            PaneKind::NatureGenerator => (
                id::PANE_NATURE_GENERATOR,
                "Nature Generator",
                Some(format!("{} · {}", self.nature.name, self.nature.stats)),
                {
                    let (materials, textures) = self.nature_assets();
                    nature_generator_panel::view(
                        self.nature.view_state(materials, textures, {
                            let mut thumbnails = std::collections::HashMap::new();
                            if let Some(node) = self
                                .nature
                                .selected
                                .and_then(|id| self.nature.graph.node(id))
                            {
                                for path in node.strings.iter().filter(|p| !p.is_empty()) {
                                    if let Some(handle) = self.nature_thumbnail(path) {
                                        thumbnails.insert(path.clone(), handle);
                                    }
                                }
                            }
                            thumbnails
                        }),
                        Message::NatureGen,
                    )
                },
            ),
            PaneKind::WaterGenerator => (
                id::PANE_WATER_GENERATOR,
                "Water Generator",
                Some(format!("{} · {}", self.watergen.name, self.watergen.stats)),
                {
                    let (materials, _) = self.nature_assets();
                    water_generator_panel::view(
                        self.watergen.view_state(materials),
                        Message::WaterGen,
                    )
                },
            ),
            PaneKind::TerrainGenerator => (
                id::PANE_TERRAIN_GENERATOR,
                "Terrain Generator",
                Some(format!(
                    "{} · {}",
                    self.terraingen.name, self.terraingen.stats
                )),
                {
                    let (mut layers, textures) = self.nature_assets();
                    layers.extend(textures);
                    terrain_generator_panel::view(
                        self.terraingen.view_state(layers),
                        Message::TerrainGen,
                    )
                },
            ),
            PaneKind::TextureGenerator => (
                id::PANE_TEXTURE_GENERATOR,
                "Texture Generator",
                Some(format!(
                    "{} · {}×{}",
                    self.texgen.name, self.texgen.graph.resolution, self.texgen.graph.resolution
                )),
                texture_generator_panel::view(self.texgen.view_state(), Message::TextureGen),
            ),
            PaneKind::TextureSet => {
                let (subtitle, body) = if let Some(set) = &self.texture_set {
                    let path = self
                        .texture_set_path
                        .as_ref()
                        .map(|path| path.display().to_string());
                    let subtitle = if self.texture_set_dirty {
                        format!("{} • unsaved", set.name)
                    } else {
                        set.name.clone()
                    };
                    let view = texture_set_panel::View {
                        set,
                        preview: self.texture_set_preview.as_ref(),
                        path,
                        dirty: self.texture_set_dirty,
                        status: self.texture_set_status.as_deref(),
                        can_undo: self.texture_set_history.can_undo(),
                        can_redo: self.texture_set_history.can_redo(),
                        paint_mode: self.texture_set_paint_mode,
                        painting: self.texture_set_painting,
                        cursor: self.texture_set_cursor,
                        export_preset: self.texture_set_export_preset,
                        export_presets: &self.texture_set_export_presets,
                    };
                    (
                        Some(subtitle),
                        texture_set_panel::view(view, Message::TextureSet),
                    )
                } else {
                    (
                        Some("No texture set open".to_owned()),
                        container(
                            column![
                                text("No texture set open").size(12).color(theme::TEXT),
                                text(
                                    "Create a texture set to paint UDIM tiles and export PBR maps."
                                )
                                .size(10)
                                .color(theme::TEXT_MUTED),
                                button::primary(
                                    id::TEXTURE_SET_NEW,
                                    "New Texture Set",
                                    Message::TextureSet(texture_set_panel::Edit::New)
                                ),
                            ]
                            .spacing(6),
                        )
                        .padding(10)
                        .into(),
                    )
                };
                (id::PANE_TEXTURE_SET, "Texture Set", subtitle, body)
            }
            PaneKind::Script => {
                let subtitle = self
                    .script
                    .current()
                    .map(|document| {
                        if document.is_dirty() {
                            format!("{} • unsaved", document.title)
                        } else {
                            document.title.clone()
                        }
                    })
                    .unwrap_or_else(|| "No file open".to_owned());
                (
                    id::PANE_SCRIPT,
                    "Script",
                    Some(subtitle),
                    script_panel::view(
                        &self.script,
                        script_panel::Callbacks {
                            ids: script_panel::Ids {
                                root: id::SCRIPT_ROOT,
                                editor: id::SCRIPT_EDITOR,
                                save: id::SCRIPT_SAVE,
                                revert: id::SCRIPT_REVERT,
                                close: id::SCRIPT_CLOSE,
                                attach: id::SCRIPT_ATTACH,
                                detach: id::SCRIPT_DETACH,
                                tab: id::script_tab,
                                tab_close: id::script_tab_close,
                            },
                            on_select: Box::new(Message::ScriptSelect),
                            on_tab_close: Box::new(Message::ScriptTabClose),
                            on_action: Box::new(Message::ScriptAction),
                            on_save: Message::ScriptSave,
                            on_revert: Message::ScriptRevert,
                            on_close: Message::ScriptClose,
                            on_attach: Message::ScriptAttach,
                            on_detach: Message::ScriptDetach,
                        },
                    ),
                )
            }
            PaneKind::DryadNodes => {
                let subtitle = if let Some(path) = self.dryad.script_path.as_ref() {
                    let name = path
                        .file_name()
                        .and_then(|name| name.to_str())
                        .unwrap_or("script.dyd");
                    if self.dryad.dirty {
                        format!("{name} • unsaved")
                    } else {
                        name.to_owned()
                    }
                } else {
                    "No script open".to_owned()
                };
                (
                    id::PANE_DRYAD_NODES,
                    "Dryad Nodes",
                    Some(subtitle),
                    dryad_nodes::view(
                        dryad_nodes::View { state: &self.dryad },
                        dryad_nodes::Callbacks {
                            on_canvas: Box::new(Message::DryadCanvas),
                            on_search: Box::new(Message::DryadSearch),
                            add_node: Box::new(Message::DryadAddNode),
                            select_node: Box::new(Message::DryadSelectNode),
                            select_link: Box::new(Message::DryadSelectLink),
                            delete: Message::DryadDelete,
                            save: Message::DryadSave,
                            sync: Message::DryadSync,
                            attach: Message::DryadAttach,
                            run: Message::DryadRun,
                            fit: Message::DryadFit,
                            add_motion: Message::DryadAddMotion,
                            number_text: Box::new(|node, field, text| Message::DryadNumber {
                                node,
                                field,
                                text,
                            }),
                            set_bool: Box::new(|node, field, value| Message::DryadBool {
                                node,
                                field,
                                value,
                            }),
                            set_text: Box::new(|node, value| Message::DryadText { node, value }),
                            noop: Message::Noop,
                        },
                    ),
                )
            }
            PaneKind::Console => (
                id::PANE_CONSOLE,
                "Console",
                Some(format!("{} lines", self.log.len())),
                panels::console::view(
                    &self.log,
                    &self.console_filters,
                    Message::ToggleConsoleFilter,
                ),
            ),
        };

        (pane_id, title, subtitle, body)
    }
}

fn snap_generator_position(point: iced::Point, snap: bool) -> iced::Point {
    if snap {
        iced::Point::new(
            (point.x / 16.0).round() * 16.0,
            (point.y / 16.0).round() * 16.0,
        )
    } else {
        point
    }
}

fn gray_to_rgba(gray: &[u8]) -> Vec<u8> {
    let mut pixels = Vec::with_capacity(gray.len() * 4);
    for value in gray {
        pixels.extend_from_slice(&[*value, *value, *value, 255]);
    }
    pixels
}

fn gray_to_rgba16(gray: &[u16]) -> Vec<u16> {
    let mut pixels = Vec::with_capacity(gray.len() * 4);
    for value in gray {
        pixels.extend_from_slice(&[*value, *value, *value, u16::MAX]);
    }
    pixels
}

fn apply_generator_param(kind: &mut generators::NodeKind, param: material_generator::Param) {
    use generators::NodeKind;
    use material_generator::Param;
    match (kind, param) {
        (NodeKind::Color { color }, Param::Color(axis, value)) => {
            color[axis.min(3)] = value.clamp(0.0, 1.0);
        }
        (NodeKind::Noise { color_a, .. }, Param::ColorA(axis, value))
        | (NodeKind::Checker { color_a, .. }, Param::ColorA(axis, value))
        | (NodeKind::Bricks { color_a, .. }, Param::ColorA(axis, value))
        | (NodeKind::Gradient { color_a, .. }, Param::ColorA(axis, value)) => {
            color_a[axis.min(3)] = value.clamp(0.0, 1.0);
        }
        (NodeKind::Noise { color_b, .. }, Param::ColorB(axis, value))
        | (NodeKind::Checker { color_b, .. }, Param::ColorB(axis, value))
        | (NodeKind::Bricks { color_b, .. }, Param::ColorB(axis, value))
        | (NodeKind::Gradient { color_b, .. }, Param::ColorB(axis, value)) => {
            color_b[axis.min(3)] = value.clamp(0.0, 1.0);
        }
        (NodeKind::Bricks { mortar_color, .. }, Param::MortarColor(axis, value)) => {
            mortar_color[axis.min(3)] = value.clamp(0.0, 1.0);
        }
        (NodeKind::Noise { scale, .. }, Param::Scale(value))
        | (NodeKind::Checker { scale, .. }, Param::Scale(value))
        | (NodeKind::Voronoi { scale, .. }, Param::Scale(value)) => {
            *scale = value.clamp(1.0, 48.0);
        }
        (
            NodeKind::Image { projection, .. } | NodeKind::NormalMap { projection, .. },
            Param::Projection(index),
        ) => {
            *projection = generators::Projection::from_index(index);
        }
        (
            NodeKind::Image { tiling, .. } | NodeKind::NormalMap { tiling, .. },
            Param::ImageTiling(axis, value),
        ) => {
            tiling[axis.min(1)] = value.clamp(0.01, 32.0);
        }
        (
            NodeKind::Image { offset, .. } | NodeKind::NormalMap { offset, .. },
            Param::ImageOffset(axis, value),
        ) => {
            offset[axis.min(1)] = value.clamp(-8.0, 8.0);
        }
        (
            NodeKind::Image { filter, .. } | NodeKind::NormalMap { filter, .. },
            Param::ImageFilter(index),
        ) => {
            *filter = generators::ImageFilter::from_index(index);
        }
        (
            NodeKind::Image { wrap, .. } | NodeKind::NormalMap { wrap, .. },
            Param::ImageWrap(index),
        ) => {
            *wrap = generators::WrapMode::from_index(index);
        }
        (NodeKind::NormalBlend { mode, .. }, Param::NormalMode(index)) => {
            *mode = generators::NormalBlendMode::from_index(index);
        }
        (NodeKind::Sharpen { amount, .. }, Param::SharpenAmount(value)) => {
            *amount = value.clamp(0.0, 4.0);
        }
        (NodeKind::Sharpen { radius, .. }, Param::SharpenRadius(value)) => {
            *radius = value.clamp(1.0, 16.0);
        }
        (NodeKind::Emboss { strength, .. }, Param::EmbossStrength(value)) => {
            *strength = value.clamp(0.0, 8.0);
        }
        (NodeKind::DirectionalBlur { radius, .. }, Param::DirectionalRadius(value)) => {
            *radius = value.clamp(1.0, 64.0);
        }
        (
            NodeKind::Emboss { angle, .. } | NodeKind::DirectionalBlur { angle, .. },
            Param::Angle(value),
        ) => {
            *angle = value.clamp(-360.0, 360.0);
        }
        (
            NodeKind::EdgeMask { strength, .. } | NodeKind::CavityMask { strength, .. },
            Param::MaskStrength(value),
        ) => {
            *strength = value.clamp(0.0, 10.0);
        }
        (NodeKind::DistanceMask { .. }, Param::MaskStrength(_)) => {}
        (
            NodeKind::CavityMask { radius, .. } | NodeKind::DistanceMask { radius, .. },
            Param::MaskRadius(value),
        ) => {
            *radius = value.clamp(1.0, 256.0);
        }
        (
            NodeKind::EdgeMask { invert, .. }
            | NodeKind::CavityMask { invert, .. }
            | NodeKind::DistanceMask { invert, .. },
            Param::MaskInvert(value),
        ) => {
            *invert = value;
        }
        (NodeKind::Noise { noise_type, .. }, Param::NoiseType(index)) => {
            *noise_type = generators::NoiseType::from_index(index);
        }
        (NodeKind::Noise { persistence, .. }, Param::Persistence(value)) => {
            *persistence = value.clamp(0.05, 1.0);
        }
        (NodeKind::Noise { lacunarity, .. }, Param::Lacunarity(value)) => {
            *lacunarity = value.clamp(1.0, 4.0);
        }
        (NodeKind::Voronoi { jitter, .. }, Param::VoronoiJitter(value)) => {
            *jitter = value.clamp(0.0, 1.0);
        }
        (NodeKind::Voronoi { mode, .. }, Param::VoronoiMode(index)) => {
            *mode = generators::VoronoiMode::from_index(index);
        }
        (NodeKind::Voronoi { seed, .. }, Param::Seed(value)) => {
            *seed = value.clamp(0, 9999);
        }
        (NodeKind::Noise { octaves, .. }, Param::Octaves(value)) => {
            *octaves = value.clamp(1, 8);
        }
        (NodeKind::Noise { seed, .. }, Param::Seed(value)) => {
            *seed = value.clamp(0, 9999);
        }
        (NodeKind::Bricks { rows, .. }, Param::Rows(value)) => {
            *rows = value.clamp(1, 32);
        }
        (NodeKind::Bricks { columns, .. }, Param::Columns(value)) => {
            *columns = value.clamp(1, 32);
        }
        (NodeKind::Bricks { mortar, .. }, Param::Mortar(value)) => {
            *mortar = value.clamp(0.0, 0.35);
        }
        (NodeKind::Gradient { angle, .. }, Param::Angle(value)) => {
            *angle = value.rem_euclid(360.0);
        }
        (NodeKind::Blend { factor, .. }, Param::Factor(value)) => {
            *factor = value.clamp(0.0, 1.0);
        }
        (NodeKind::NormalBlend { strength, .. }, Param::Factor(value)) => {
            *strength = value.clamp(0.0, 2.0);
        }
        (NodeKind::Blend { mode, .. }, Param::Mode(value)) => {
            *mode = generators::BlendMode::from_index(value);
        }
        (NodeKind::Levels { brightness, .. }, Param::Brightness(value)) => {
            *brightness = value.clamp(-1.0, 1.0);
        }
        (NodeKind::Levels { contrast, .. }, Param::Contrast(value)) => {
            *contrast = value.clamp(0.0, 4.0);
        }
        (NodeKind::Levels { gamma, .. }, Param::Gamma(value)) => {
            *gamma = value.clamp(0.1, 4.0);
        }
        (NodeKind::Levels { invert, .. }, Param::Invert(value)) => *invert = value,
        (NodeKind::Curve { points }, Param::CurvePoint(index, value)) => {
            points[index.min(4)] = value.clamp(0.0, 1.0);
        }
        (NodeKind::Hsl { hue, .. }, Param::Hue(value)) => {
            *hue = value.clamp(-180.0, 180.0);
        }
        (NodeKind::Hsl { saturation, .. }, Param::Saturation(value)) => {
            *saturation = value.clamp(0.0, 2.0);
        }
        (NodeKind::Hsl { lightness, .. }, Param::Lightness(value)) => {
            *lightness = value.clamp(-1.0, 1.0);
        }
        (NodeKind::Threshold { threshold, .. }, Param::Threshold(value)) => {
            *threshold = value.clamp(0.0, 1.0);
        }
        (NodeKind::Threshold { softness, .. }, Param::Softness(value)) => {
            *softness = value.clamp(0.0, 0.5);
        }
        (NodeKind::Threshold { invert, .. }, Param::ThresholdInvert(value)) => *invert = value,
        (NodeKind::Blur { radius, .. }, Param::BlurRadius(value)) => {
            *radius = value.clamp(0.0, 64.0);
        }
        (NodeKind::Blur { wrap, .. }, Param::BlurWrap(index)) => {
            *wrap = generators::WrapMode::from_index(index);
        }
        (NodeKind::Warp { amount, .. }, Param::WarpAmount(value)) => {
            *amount = value.clamp(0.0, 0.5);
        }
        (NodeKind::Warp { scale, .. }, Param::WarpScale(value)) => {
            *scale = value.clamp(1.0, 32.0);
        }
        (NodeKind::Warp { seed, .. }, Param::WarpSeed(value)) => *seed = value.clamp(0, 9999),
        (NodeKind::Value { value }, Param::Value(new_value)) => {
            *value = new_value.clamp(0.0, 1.0);
        }
        (NodeKind::Output { roughness, .. }, Param::Roughness(value)) => {
            *roughness = value.clamp(0.0, 1.0);
        }
        (NodeKind::Output { metallic, .. }, Param::Metallic(value)) => {
            *metallic = value.clamp(0.0, 1.0);
        }
        (NodeKind::Output { height, .. }, Param::Height(value)) => {
            *height = value.clamp(0.0, 1.0);
        }
        (NodeKind::Output { ao, .. }, Param::Ao(value)) => {
            *ao = value.clamp(0.0, 1.0);
        }
        (NodeKind::Output { options, .. }, Param::OutputAutoHeight(value)) => {
            options.auto_height = value;
        }
        (NodeKind::Output { options, .. }, Param::OutputNormalizeHeight(value)) => {
            options.normalize_height = value;
        }
        (NodeKind::Output { options, .. }, Param::OutputHeightContrast(value)) => {
            options.height_contrast = value.clamp(0.1, 4.0);
        }
        (NodeKind::TextureOutput { mode }, Param::TextureOutputMode(index)) => {
            *mode = generators::TextureOutputMode::from_index(index);
        }
        (NodeKind::Proc { op, params, .. }, Param::Proc(index, value)) => {
            if let (Some(slot), Some(spec)) = (params.get_mut(index), op.0.params.get(index)) {
                *slot = value.clamp(spec.min, spec.max);
            }
        }
        (NodeKind::Proc { colors, .. }, Param::ProcColor(index, axis, value)) => {
            if let Some(color) = colors.get_mut(index) {
                color[axis.min(3)] = value.clamp(0.0, 1.0);
            }
        }
        _ => {}
    }
}

fn sanitize_material_name(name: &str) -> String {
    let mut cleaned = String::new();
    for character in name.chars() {
        if character.is_ascii_alphanumeric() || matches!(character, ' ' | '-' | '_' | '(' | ')') {
            cleaned.push(character);
        } else if character == '/' || character == '\\' {
            cleaned.push('-');
        }
    }
    let cleaned = cleaned.trim();
    if cleaned.is_empty() {
        "Material".to_owned()
    } else {
        cleaned.to_owned()
    }
}

fn prefab_scale(kind: prefabs::Kind) -> [f32; 3] {
    match kind {
        prefabs::Kind::Mesh { scale, .. } | prefabs::Kind::Physics { scale, .. } => scale,
        _ => [1.0; 3],
    }
}

fn unique_entity_name(engine: &doppio_engine::Engine, base: &str) -> String {
    let mut name = base.to_owned();
    let mut suffix = 2;
    while (0..engine.entity_count()).any(|index| {
        engine
            .entity_at(index)
            .map(|info| info.name == name)
            .unwrap_or(false)
    }) {
        name = format!("{base} {suffix}");
        suffix += 1;
    }
    name
}

fn component_type_id(engine: &doppio_engine::Engine, name: &str) -> Option<u32> {
    engine
        .component_types()
        .into_iter()
        .find(|kind| kind.name == name)
        .map(|kind| kind.id)
}

fn add_component(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    name: &str,
) -> Result<u32, String> {
    let type_id = component_type_id(engine, name)
        .ok_or_else(|| format!("component {name} is not available"))?;
    engine
        .add_component(entity, type_id)
        .map_err(|error| format!("{error:?}"))?;
    Ok(type_id)
}

fn set_component_field(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    type_id: u32,
    field: &str,
    value: doppio_engine::FieldValue,
) -> Result<(), String> {
    let components = engine.entity_components(entity);
    let component = components
        .iter()
        .position(|component| component.type_id == type_id)
        .ok_or_else(|| "component missing after add".to_owned())?;
    let field_index = engine
        .component_types()
        .iter()
        .find(|kind| kind.id == type_id)
        .and_then(|kind| kind.fields.iter().position(|info| info.name == field))
        .ok_or_else(|| format!("field {field} is not available"))?;
    engine
        .set_component_field(entity, component as u32, field_index as u32, &value)
        .map_err(|error| format!("{error:?}"))
}

fn enum_field(
    engine: &doppio_engine::Engine,
    type_name: &str,
    field: &str,
    option: &str,
) -> Result<doppio_engine::FieldValue, String> {
    let type_info = engine
        .component_types()
        .into_iter()
        .find(|kind| kind.name == type_name)
        .ok_or_else(|| format!("component {type_name} is not available"))?;
    let field_info = type_info
        .fields
        .iter()
        .find(|info| info.name == field)
        .ok_or_else(|| format!("field {field} is not available"))?;
    let option = field_info
        .options
        .iter()
        .position(|value| value == option)
        .ok_or_else(|| format!("{type_name}.{field} cannot be {option}"))?;
    Ok(doppio_engine::FieldValue::Enum(option as i32))
}

fn add_mesh(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    primitive: &str,
    scale: [f32; 3],
) -> Result<(), String> {
    let filter = add_component(engine, entity, "Mesh Filter")?;
    let value = enum_field(engine, "Mesh Filter", "Primitive", primitive)?;
    set_component_field(engine, entity, filter, "Primitive", value)?;
    add_component(engine, entity, "Mesh Renderer")?;
    engine
        .set_scale(entity, scale)
        .map_err(|error| format!("{error:?}"))
}

fn add_light(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    kind: prefabs::LightKind,
) -> Result<(), String> {
    let light = add_component(engine, entity, "Light")?;
    set_component_field(
        engine,
        entity,
        light,
        "Color",
        doppio_engine::FieldValue::Color([1.0, 1.0, 1.0, 1.0]),
    )?;
    set_component_field(
        engine,
        entity,
        light,
        "Intensity",
        doppio_engine::FieldValue::F32(1.0),
    )?;
    let (component, radius) = match kind {
        prefabs::LightKind::Directional => ("Directional Light", None),
        prefabs::LightKind::Point => ("Point Light", Some(10.0)),
        prefabs::LightKind::Spot => ("Spot Light", Some(12.0)),
    };
    let type_id = add_component(engine, entity, component)?;
    if let Some(radius) = radius {
        set_component_field(
            engine,
            entity,
            type_id,
            "Radius",
            doppio_engine::FieldValue::F32(radius),
        )?;
    }
    Ok(())
}

fn add_physics(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    body: &str,
    collider: &str,
    scale: [f32; 3],
) -> Result<(), String> {
    let rigid = add_component(engine, entity, "Rigid Body 3D")?;
    let value = enum_field(engine, "Rigid Body 3D", "Body type", body)?;
    set_component_field(engine, entity, rigid, "Body type", value)?;
    let collider_id = add_component(engine, entity, "Collider 3D")?;
    let shape = enum_field(engine, "Collider 3D", "Shape", collider)?;
    set_component_field(engine, entity, collider_id, "Shape", shape)?;
    let half = if collider == "Capsule" {
        [0.5, 1.0, 0.5]
    } else {
        [0.5, 0.5, 0.5]
    };
    set_component_field(
        engine,
        entity,
        collider_id,
        "Half extents",
        doppio_engine::FieldValue::Vec3(half),
    )?;
    let _ = scale;
    Ok(())
}

fn create_prefab_entity(
    engine: &mut doppio_engine::Engine,
    prefab: &prefabs::Prefab,
    spawn: [f32; 3],
) -> Result<u32, String> {
    let id = engine
        .create_entity(&unique_entity_name(engine, prefab.label))
        .map_err(|error| format!("{error:?}"))?;
    engine
        .set_position(id, spawn)
        .map_err(|error| format!("{error:?}"))?;
    match prefab.kind {
        prefabs::Kind::Empty => {}
        prefabs::Kind::Mesh { primitive, scale } => {
            add_mesh(engine, id, primitive, scale)?;
        }
        prefabs::Kind::Light { kind } => add_light(engine, id, kind)?,
        prefabs::Kind::Camera => {
            add_component(engine, id, "Camera 3D")?;
        }
        prefabs::Kind::Physics {
            primitive,
            body,
            collider,
            scale,
        } => {
            add_mesh(engine, id, primitive, scale)?;
            add_physics(engine, id, body, collider, scale)?;
        }
        prefabs::Kind::DuplicateSelection => {
            return Err("duplicate is handled separately".to_owned());
        }
    }
    Ok(id)
}

fn duplicate_entity(
    engine: &mut doppio_engine::Engine,
    source: u32,
    label: &str,
) -> Result<u32, String> {
    let source_name = engine
        .entity_name(source)
        .map_err(|error| format!("{error:?}"))?;
    let name = unique_entity_name(engine, &format!("{source_name} Copy"));
    let id = engine
        .create_entity(&name)
        .map_err(|error| format!("{error:?}"))?;
    let position = engine
        .position(source)
        .map_err(|error| format!("{error:?}"))?;
    let rotation = engine
        .rotation(source)
        .map_err(|error| format!("{error:?}"))?;
    let scale = engine.scale(source).map_err(|error| format!("{error:?}"))?;
    engine
        .set_position(id, [position[0] + 1.0, position[1], position[2]])
        .map_err(|error| format!("{error:?}"))?;
    engine
        .set_rotation(id, rotation)
        .map_err(|error| format!("{error:?}"))?;
    engine
        .set_scale(id, scale)
        .map_err(|error| format!("{error:?}"))?;
    if let Ok(parent) = engine.entity_parent(source) {
        let _ = engine.set_entity_parent(id, parent);
    }
    let data = engine.entity_component_data(source);
    for component in &data {
        engine
            .add_component(id, component.info.type_id)
            .map_err(|error| format!("{error:?}"))?;
    }
    for component in &data {
        let components = engine.entity_components(id);
        let Some(index) = components
            .iter()
            .position(|candidate| candidate.type_id == component.info.type_id)
        else {
            continue;
        };
        for (field, value) in component.values.iter().enumerate() {
            let _ = engine.set_component_field(id, index as u32, field as u32, value);
        }
    }
    let _ = label;
    Ok(id)
}

fn format_transform_number(value: f32) -> String {
    if value.fract().abs() < f32::EPSILON {
        format!("{value:.0}")
    } else {
        format!("{value:.3}")
    }
}

mod naturegen;
mod terraingen;
mod texturegen;
mod watergen;

#[cfg(test)]
mod tests;

#[cfg(any(test, feature = "headless"))]
pub mod headless;

#[cfg(feature = "agent")]
mod agent;

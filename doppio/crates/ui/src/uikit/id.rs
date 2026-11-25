//! Hierarchical widget ids: `group.subgroup.object`.
//!
//! Iced ids are opaque strings. Doppio gives them a fixed taxonomy so tests,
//! docs scripts and agents can address a widget without guessing:
//!
//! ```
//! use doppio_ui::uikit::id;
//!
//! assert_eq!(id::ADD_ENTITY.key(), "editor.toolbar.add_entity");
//! assert_eq!(id::PANE_VIEWPORT.key(), "editor.pane.viewport");
//! ```
//!
//! Dynamic rows (entities, assets) use [`UiId::dynamic`] and keep the same
//! prefix, e.g. `hierarchy.list.entity_row.3`.

use iced::widget::Id;

/// A stable, hierarchical widget id.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct UiId(UiKey);

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
enum UiKey {
    Fixed(&'static str),
    Dynamic(String),
}

impl UiId {
    /// A compile-time id such as `editor.toolbar.add_entity`.
    pub const fn fixed(key: &'static str) -> Self {
        Self(UiKey::Fixed(key))
    }

    /// An id whose last segment is built at runtime (list rows, indices…).
    pub fn dynamic(key: impl Into<String>) -> Self {
        Self(UiKey::Dynamic(key.into()))
    }

    pub fn key(&self) -> &str {
        match &self.0 {
            UiKey::Fixed(key) => key,
            UiKey::Dynamic(key) => key,
        }
    }

    /// The `group` segment (`editor`, `hierarchy`, `inspector`, …).
    pub fn group(&self) -> &str {
        self.segments().0
    }

    /// The `subgroup` segment (`toolbar`, `pane`, `list`, `camera`, …).
    pub fn subgroup(&self) -> &str {
        self.segments().1
    }

    /// The `object` segment, including any dynamic suffix.
    pub fn object(&self) -> &str {
        self.segments().2
    }

    fn segments(&self) -> (&str, &str, &str) {
        let mut parts = self.key().splitn(3, '.');
        (
            parts.next().unwrap_or_default(),
            parts.next().unwrap_or_default(),
            parts.next().unwrap_or_default(),
        )
    }
}

impl From<UiId> for Id {
    fn from(value: UiId) -> Self {
        Id::from(value.key().to_owned())
    }
}

/// One documented widget: the id plus the text agents and docs use for it.
#[derive(Debug, Clone)]
pub struct UiMeta {
    pub id: UiId,
    pub description: &'static str,
    pub kind: &'static str,
}

// ── Editor chrome ─────────────────────────────────────────────────────────
pub const LAYOUT_DEFAULT: UiId = UiId::fixed("editor.layout.default");
pub const LAYOUT_VIEWPORT: UiId = UiId::fixed("editor.layout.viewport");
pub const MENU_PROJECT: UiId = UiId::fixed("editor.menu.project");
pub const PROJECT_MANAGER_MENU: UiId = UiId::fixed("editor.menu.project.manager");
pub const PROJECT_REFRESH_ASSETS: UiId = UiId::fixed("editor.menu.project.refresh_assets");
pub const SCRIPT_EDITOR_MENU: UiId = UiId::fixed("editor.menu.scene.script_editor");
pub const DRYAD_NODES_MENU: UiId = UiId::fixed("editor.menu.scene.dryad_nodes");
pub const SCATTER_TERRAIN_MENU: UiId = UiId::fixed("editor.menu.scene.scatter_terrain");
pub const CONVERT_TERRAINS_MENU: UiId = UiId::fixed("editor.menu.scene.convert_terrains");
pub const MENU_SCENE: UiId = UiId::fixed("editor.menu.scene");
pub const MENU_EDIT: UiId = UiId::fixed("editor.menu.edit");
pub const MENU_VIEW: UiId = UiId::fixed("editor.menu.view");
pub const UNDO: UiId = UiId::fixed("editor.toolbar.undo");
pub const REDO: UiId = UiId::fixed("editor.toolbar.redo");
pub const ADD_ENTITY: UiId = UiId::fixed("editor.toolbar.add_entity");
pub const TOGGLE_PLAY: UiId = UiId::fixed("editor.toolbar.toggle_play");
pub const SAVE_SCENE: UiId = UiId::fixed("editor.toolbar.save");
pub const RENDER_VIEWPORT: UiId = UiId::fixed("editor.toolbar.render");
pub const TOGGLE_GRID: UiId = UiId::fixed("editor.view.toggle_grid");
pub const GIZMO_MOVE: UiId = UiId::fixed("editor.view.gizmo_move");
pub const GIZMO_ROTATE: UiId = UiId::fixed("editor.view.gizmo_rotate");
pub const GIZMO_SCALE: UiId = UiId::fixed("editor.view.gizmo_scale");

// ── Panes ────────────────────────────────────────────────────────────────
pub const PANE_HIERARCHY: UiId = UiId::fixed("editor.pane.hierarchy");
pub const PANE_ASSETS: UiId = UiId::fixed("editor.pane.assets");
pub const ASSET_SEARCH: UiId = UiId::fixed("assets.toolbar.search");
pub const ASSET_UP: UiId = UiId::fixed("assets.toolbar.up");
pub const ASSET_ROOT: UiId = UiId::fixed("assets.toolbar.root");
pub const ASSET_REFRESH: UiId = UiId::fixed("assets.toolbar.refresh");
pub const ASSET_VIEW_GRID: UiId = UiId::fixed("assets.toolbar.view_grid");
pub const ASSET_VIEW_LIST: UiId = UiId::fixed("assets.toolbar.view_list");
pub const ASSET_EMPTY: UiId = UiId::fixed("assets.tree.empty");
pub const ASSET_NEW: UiId = UiId::fixed("assets.toolbar.new");
pub const ASSET_IMPORT: UiId = UiId::fixed("assets.toolbar.import");
pub const ASSET_NEW_FOLDER: UiId = UiId::fixed("assets.files.new_folder");
pub const ASSET_RENAME: UiId = UiId::fixed("assets.files.rename");
pub const ASSET_DELETE: UiId = UiId::fixed("assets.files.delete");
pub const ASSET_DELETE_CONFIRM: UiId = UiId::fixed("assets.files.delete_confirm");
pub const ASSET_EDIT_NAME: UiId = UiId::fixed("assets.files.name");
pub const ASSET_EDIT_OK: UiId = UiId::fixed("assets.files.ok");
pub const ASSET_EDIT_CANCEL: UiId = UiId::fixed("assets.files.cancel");
pub const ASSET_TREE: UiId = UiId::fixed("assets.tree.folders");
pub const ASSET_TREE_TOGGLE: UiId = UiId::fixed("assets.toolbar.tree_toggle");
pub const WIZARD: UiId = UiId::fixed("assets.wizard");
pub const WIZARD_CLOSE: UiId = UiId::fixed("assets.wizard.close");
pub const WIZARD_TAB_CREATE: UiId = UiId::fixed("assets.wizard.tab.create");
pub const WIZARD_TAB_IMPORT: UiId = UiId::fixed("assets.wizard.tab.import");
pub const WIZARD_TAB_PACK: UiId = UiId::fixed("assets.wizard.tab.pack");
pub const WIZARD_NAME: UiId = UiId::fixed("assets.wizard.create.name");
pub const WIZARD_CREATE: UiId = UiId::fixed("assets.wizard.create.confirm");
pub const WIZARD_IMPORT_PATH: UiId = UiId::fixed("assets.wizard.import.path");
pub const WIZARD_IMPORT_ADD: UiId = UiId::fixed("assets.wizard.import.add");
pub const WIZARD_IMPORT_RUN: UiId = UiId::fixed("assets.wizard.import.run");
pub const WIZARD_PACK_RUN: UiId = UiId::fixed("assets.wizard.pack.run");
pub const WIZARD_PACK_IDS: UiId = UiId::fixed("assets.wizard.pack.ids");

/// One template card in the wizard's Create tab.
pub fn wizard_template(index: usize) -> UiId {
    UiId::dynamic(format!("assets.wizard.create.template.{index}"))
}

/// One queued source in the wizard's Import tab.
pub fn wizard_source(index: usize) -> UiId {
    UiId::dynamic(format!("assets.wizard.import.source.{index}"))
}

/// One folder row in the asset browser's tree.
pub fn asset_tree_row(path: &str) -> UiId {
    UiId::dynamic(format!("assets.tree.folder.{path}"))
}

pub const PROJECT_MANAGER: UiId = UiId::fixed("project.manager");
pub const PROJECT_CLOSE: UiId = UiId::fixed("project.manager.close");
pub const PROJECT_TAB_CREATE: UiId = UiId::fixed("project.manager.tab.create");
pub const PROJECT_TAB_RECENT: UiId = UiId::fixed("project.manager.tab.recent");
pub const PROJECT_TAB_BROWSE: UiId = UiId::fixed("project.manager.tab.browse");
pub const PROJECT_NAME: UiId = UiId::fixed("project.create.name");
pub const PROJECT_LOCATION: UiId = UiId::fixed("project.create.location");
pub const PROJECT_CREATE: UiId = UiId::fixed("project.create.submit");
pub const PROJECT_DEFAULT_LOCATION: UiId = UiId::fixed("project.create.default_location");
pub const PROJECT_RECENT_SEARCH: UiId = UiId::fixed("project.recent.search");
pub const PROJECT_RECENT_OPEN: UiId = UiId::fixed("project.recent.open");
pub const PROJECT_BROWSE_PATH: UiId = UiId::fixed("project.browse.path");
pub const PROJECT_BROWSE_SCAN: UiId = UiId::fixed("project.browse.scan");
pub const PROJECT_BROWSE_DEFAULT: UiId = UiId::fixed("project.browse.default");
pub const PROJECT_BROWSE_SEARCH: UiId = UiId::fixed("project.browse.search");
pub const PROJECT_BROWSE_OPEN: UiId = UiId::fixed("project.browse.open");
pub const PROJECT_ERROR_DISMISS: UiId = UiId::fixed("project.error.dismiss");

// ── Script editor ────────────────────────────────────────────────────────
pub const SCRIPT_ROOT: UiId = UiId::fixed("script.editor");
pub const SCRIPT_EDITOR: UiId = UiId::fixed("script.editor.code");
pub const SCRIPT_SAVE: UiId = UiId::fixed("script.editor.save");
pub const SCRIPT_REVERT: UiId = UiId::fixed("script.editor.revert");
pub const SCRIPT_CLOSE: UiId = UiId::fixed("script.editor.close");
pub const SCRIPT_ATTACH: UiId = UiId::fixed("script.editor.attach");
pub const SCRIPT_DETACH: UiId = UiId::fixed("script.editor.detach");
pub const SCRIPT_EMPTY: UiId = UiId::fixed("script.editor.empty");

// ── Dryad nodes ──────────────────────────────────────────────────────────
pub const DRYAD_ROOT: UiId = UiId::fixed("dryad.nodes");
pub const DRYAD_CANVAS: UiId = UiId::fixed("dryad.nodes.canvas");
pub const DRYAD_PALETTE_SEARCH: UiId = UiId::fixed("dryad.nodes.search");
pub const DRYAD_SAVE: UiId = UiId::fixed("dryad.nodes.save");
pub const DRYAD_SYNC: UiId = UiId::fixed("dryad.nodes.sync");
pub const DRYAD_DELETE: UiId = UiId::fixed("dryad.nodes.delete");
pub const DRYAD_FIT: UiId = UiId::fixed("dryad.nodes.fit");
pub const DRYAD_ATTACH: UiId = UiId::fixed("dryad.nodes.attach");
pub const DRYAD_RUN: UiId = UiId::fixed("dryad.nodes.run");
pub const DRYAD_ADD_MOTION: UiId = UiId::fixed("dryad.nodes.add_motion");
pub const DRYAD_STATUS: UiId = UiId::fixed("dryad.nodes.status");
pub const DRYAD_PROPERTIES: UiId = UiId::fixed("dryad.nodes.properties");
pub const DRYAD_EMPTY: UiId = UiId::fixed("dryad.nodes.empty");
pub const PANE_VIEWPORT: UiId = UiId::fixed("editor.pane.viewport");
pub const VIEWPORT_MARKERS: UiId = UiId::fixed("viewport.canvas.markers");
pub const VIEWPORT_BRUSH_CURSOR: UiId = UiId::fixed("viewport.canvas.brush_cursor");
pub const TOGGLE_MARKERS: UiId = UiId::fixed("editor.view.toggle_markers");
pub const TOGGLE_SNAP: UiId = UiId::fixed("editor.view.toggle_snap");
pub const VIEWPORT_GIZMO: UiId = UiId::fixed("viewport.canvas.gizmo");
pub const PANE_INSPECTOR: UiId = UiId::fixed("editor.pane.inspector");
pub const PANE_MATERIAL: UiId = UiId::fixed("editor.pane.material");
pub const PANE_TEXTURE_SET: UiId = UiId::fixed("editor.pane.texture_set");
pub const PANE_TEXTURE_GENERATOR: UiId = UiId::fixed("editor.pane.texture_generator");
pub const PANE_TAB_TEXTURE_GENERATOR: UiId = UiId::fixed("editor.pane.tab.texture_generator");
pub const TEXTUREGEN_CANVAS: UiId = UiId::fixed("texturegen.canvas");
pub const TEXTUREGEN_NEW: UiId = UiId::fixed("texturegen.toolbar.new");
pub const TEXTUREGEN_LIBRARY: UiId = UiId::fixed("texturegen.library");
pub const TEXTUREGEN_SIZE: UiId = UiId::fixed("texturegen.size");
pub const TEXTUREGEN_UNDO: UiId = UiId::fixed("texturegen.toolbar.undo");
pub const TEXTUREGEN_REDO: UiId = UiId::fixed("texturegen.toolbar.redo");
pub const TEXTUREGEN_FIT: UiId = UiId::fixed("texturegen.toolbar.fit");
pub const TEXTUREGEN_RESEED: UiId = UiId::fixed("texturegen.toolbar.reseed");
pub const TEXTUREGEN_SAVE: UiId = UiId::fixed("texturegen.toolbar.save");
pub const TEXTUREGEN_EXPORT: UiId = UiId::fixed("texturegen.toolbar.export");
pub const TEXTUREGEN_SEARCH: UiId = UiId::fixed("texturegen.search");
pub const TEXTUREGEN_SNAP: UiId = UiId::fixed("texturegen.snap");
pub const TEXTUREGEN_NAME: UiId = UiId::fixed("texturegen.name");
pub const TEXTUREGEN_PREVIEW: UiId = UiId::fixed("texturegen.preview");
pub const TEXTUREGEN_BITS: UiId = UiId::fixed("texturegen.export.bits");
pub const TEXTUREGEN_FLIP_COLS: UiId = UiId::fixed("texturegen.export.flip_columns");
pub const TEXTUREGEN_FLIP_ROWS: UiId = UiId::fixed("texturegen.export.flip_rows");
pub const TEXTUREGEN_EXPORT_FLIPBOOK: UiId = UiId::fixed("texturegen.export.flipbook");
pub const SHOW_TEXTURE_GENERATOR: UiId = UiId::fixed("editor.menu.view.texture_generator");
pub const PANE_NATURE_GENERATOR: UiId = UiId::fixed("editor.pane.nature_generator");
pub const PANE_TAB_NATURE_GENERATOR: UiId = UiId::fixed("editor.pane.tab.nature_generator");
pub const PANE_WATER_GENERATOR: UiId = UiId::fixed("editor.pane.water_generator");
pub const PANE_TAB_WATER_GENERATOR: UiId = UiId::fixed("editor.pane.tab.water_generator");
pub const SHOW_WATER_GENERATOR: UiId = UiId::fixed("editor.menu.view.water_generator");
pub const PANE_PHOTON: UiId = UiId::fixed("editor.pane.photon");
pub const PANE_TAB_PHOTON: UiId = UiId::fixed("editor.pane.tab.photon");
pub const SHOW_PHOTON: UiId = UiId::fixed("editor.menu.view.photon");
pub const PHOTON_ADD: UiId = UiId::fixed("photon.setup.add");
pub const PHOTON_PRESET: UiId = UiId::fixed("photon.setup.preset");
pub const PHOTON_ENABLED: UiId = UiId::fixed("photon.lighting.enabled");
pub const PHOTON_PATH: UiId = UiId::fixed("photon.lighting.path");
pub const PHOTON_QUALITY: UiId = UiId::fixed("photon.lighting.quality");
pub const PHOTON_INTENSITY: UiId = UiId::fixed("photon.lighting.intensity");
pub const PHOTON_BOUNCE_GAIN: UiId = UiId::fixed("photon.lighting.bounce_gain");
pub const PHOTON_SKY: UiId = UiId::fixed("photon.lighting.sky");
pub const PHOTON_AMBIENT: UiId = UiId::fixed("photon.lighting.ambient");
pub const PHOTON_RADIUS: UiId = UiId::fixed("photon.lighting.radius");
pub const PHOTON_VOXEL_SKINNED: UiId = UiId::fixed("photon.voxels.skinned");
pub const PHOTON_VOXEL_CUTOUT: UiId = UiId::fixed("photon.voxels.cutout");
pub const PHOTON_VOXEL_TERRAIN: UiId = UiId::fixed("photon.voxels.terrain");
pub const PHOTON_FULL_REBUILDS: UiId = UiId::fixed("photon.voxels.full_rebuilds");
pub const PHOTON_REBUILD: UiId = UiId::fixed("photon.voxels.rebuild");
pub const PHOTON_DEBUG_VIEW: UiId = UiId::fixed("photon.debug.view");
pub const PHOTON_STATS: UiId = UiId::fixed("photon.stats");
pub const PANE_PROFILER: UiId = UiId::fixed("editor.pane.profiler");
pub const PANE_TAB_PROFILER: UiId = UiId::fixed("editor.pane.tab.profiler");
pub const SHOW_PROFILER: UiId = UiId::fixed("editor.menu.view.profiler");
pub const PROFILER_GPU: UiId = UiId::fixed("profiler.setup.gpu_timing");
pub const PROFILER_RESET: UiId = UiId::fixed("profiler.setup.reset");
pub const PROFILER_FRAME: UiId = UiId::fixed("profiler.summary.frame");
pub const PROFILER_TABLE: UiId = UiId::fixed("profiler.table.scopes");
pub const PROFILER_ROW: UiId = UiId::fixed("profiler.table.row");
pub const PROFILER_CHART_FPS: UiId = UiId::fixed("profiler.chart.fps");
pub const PROFILER_CHART_CPU: UiId = UiId::fixed("profiler.chart.cpu_cores");
pub const PROFILER_CHART_GPU: UiId = UiId::fixed("profiler.chart.gpu");
pub const PROFILER_CHART_MEMORY: UiId = UiId::fixed("profiler.chart.memory");
pub const PANE_ENVIRONMENT: UiId = UiId::fixed("editor.pane.environment");
pub const PANE_TAB_ENVIRONMENT: UiId = UiId::fixed("editor.pane.tab.environment");
pub const SHOW_ENVIRONMENT: UiId = UiId::fixed("editor.menu.view.environment");
pub const PANE_AUDIO: UiId = UiId::fixed("editor.pane.audio");
pub const PANE_TAB_AUDIO: UiId = UiId::fixed("editor.pane.tab.audio");
pub const TRANSPORT_PLAY: UiId = UiId::fixed("editor.transport.play");
pub const TRANSPORT_STOP: UiId = UiId::fixed("editor.transport.stop");
pub const PANE_GAMEPLAY: UiId = UiId::fixed("editor.pane.gameplay");
pub const PANE_TAB_GAMEPLAY: UiId = UiId::fixed("editor.pane.tab.gameplay");
pub const SHOW_GAMEPLAY: UiId = UiId::fixed("editor.menu.view.gameplay");
pub const GAMEPLAY_VIEW: UiId = UiId::fixed("gameplay.view.surface");
pub const PANE_CAMERA_PREVIEW: UiId = UiId::fixed("editor.pane.camera_preview");
pub const PANE_TAB_CAMERA_PREVIEW: UiId = UiId::fixed("editor.pane.tab.camera_preview");
pub const SHOW_CAMERA_PREVIEW: UiId = UiId::fixed("editor.menu.view.camera_preview");
pub const CAMERA_PREVIEW_VIEW: UiId = UiId::fixed("camerapreview.view.surface");
pub const PERF_ENABLED: UiId = UiId::fixed("performance.features.enabled");
pub const PERF_INSTANCING: UiId = UiId::fixed("performance.features.instancing");
pub const PERF_OCCLUSION: UiId = UiId::fixed("performance.features.occlusion");
pub const PERF_REFLECTIONS: UiId = UiId::fixed("performance.features.reflections");
pub const PERF_SCREENSPACEREFLECTIONS: UiId = UiId::fixed("performance.features.reflections_ssr");
pub const PERF_REFLECTIONSWHENSETTLED: UiId =
    UiId::fixed("performance.features.reflections_settled");
pub const PERF_VOLUMETRICS: UiId = UiId::fixed("performance.features.volumetrics");
pub const PERF_IBL: UiId = UiId::fixed("performance.features.ibl");
pub const PERF_INSTANCINGBATCH: UiId = UiId::fixed("performance.features.instancing_batch");
pub const PERF_OCCLUDERS: UiId = UiId::fixed("performance.features.occluders");
pub const PERF_OCCLUDERRADIUS: UiId = UiId::fixed("performance.features.occluder_radius");
pub const PERF_REFLECTIONSTRENGTH: UiId = UiId::fixed("performance.features.reflection_strength");
pub const PERF_REFLECTIONSCALE: UiId = UiId::fixed("performance.features.reflection_scale");
pub const PERF_PROBERESOLUTION: UiId = UiId::fixed("performance.features.probe_resolution");
pub const PERF_MAXPROBES: UiId = UiId::fixed("performance.features.max_probes");
pub const PERF_SSRSTEPS: UiId = UiId::fixed("performance.features.ssr_steps");
pub const PERF_FOGDENSITY: UiId = UiId::fixed("performance.features.fog_density");
pub const PERF_ADD: UiId = UiId::fixed("performance.setup.add");
pub const PERF_RENDER_SCALE: UiId = UiId::fixed("performance.viewport.render_scale");
pub const PERF_ANTI_ALIASING: UiId = UiId::fixed("performance.viewport.anti_aliasing");
pub const PERF_SHADOWS: UiId = UiId::fixed("performance.viewport.shadows");
pub const PERF_GRID: UiId = UiId::fixed("performance.viewport.grid");
pub const PANE_PERFORMANCE: UiId = UiId::fixed("editor.pane.performance");
pub const PANE_TAB_PERFORMANCE: UiId = UiId::fixed("editor.pane.tab.performance");
pub const SHOW_PERFORMANCE: UiId = UiId::fixed("editor.menu.view.performance");
pub const SHOW_AUDIO: UiId = UiId::fixed("editor.menu.view.audio");
pub const PANE_PHASESHIFT: UiId = UiId::fixed("editor.pane.phaseshift");
pub const PANE_TAB_PHASESHIFT: UiId = UiId::fixed("editor.pane.tab.phaseshift");
pub const SHOW_PHASESHIFT: UiId = UiId::fixed("editor.menu.view.phaseshift");
pub const PANE_RIG2D: UiId = UiId::fixed("editor.pane.rig2d");
pub const PANE_TAB_RIG2D: UiId = UiId::fixed("editor.pane.tab.rig2d");
pub const SHOW_RIG2D: UiId = UiId::fixed("editor.menu.view.rig2d");
pub const AUDIO_ADD: UiId = UiId::fixed("audio.setup.add");
pub const AUDIO_ENABLED: UiId = UiId::fixed("audio.mixer.enabled");
pub const AUDIO_OUTPUT: UiId = UiId::fixed("audio.output.speakers");
pub const AUDIO_DEVICE: UiId = UiId::fixed("audio.output.device");
pub const AUDIO_MASTER: UiId = UiId::fixed("audio.output.master");
pub const AUDIO_WORLD: UiId = UiId::fixed("audio.output.world");
pub const AUDIO_AMBIENT: UiId = UiId::fixed("audio.output.ambient");
pub const AUDIO_MUSIC: UiId = UiId::fixed("audio.output.music");
pub const AUDIO_PROPAGATION_DELAY: UiId = UiId::fixed("audio.travel.delay");
pub const AUDIO_DOPPLER: UiId = UiId::fixed("audio.travel.doppler");
pub const AUDIO_SPEED_OF_SOUND: UiId = UiId::fixed("audio.travel.speed");
pub const AUDIO_AIR_ABSORPTION: UiId = UiId::fixed("audio.travel.air");
pub const AUDIO_OCCLUSION: UiId = UiId::fixed("audio.travel.occlusion");
pub const AUDIO_DIFFRACTION: UiId = UiId::fixed("audio.travel.diffraction");
pub const AUDIO_REVERB: UiId = UiId::fixed("audio.travel.reverb");
pub const AUDIO_ADD_SOURCE: UiId = UiId::fixed("audio.sounds.add_source");
pub const AUDIO_ADD_WALL: UiId = UiId::fixed("audio.sounds.add_wall");
pub const ENV_ADD: UiId = UiId::fixed("environment.setup.add");
pub const ENV_PRESET: UiId = UiId::fixed("environment.setup.preset");
pub const ENV_CLOUDS_ENABLED: UiId = UiId::fixed("environment.clouds.enabled");
pub const ENV_CLOUDS_QUALITY: UiId = UiId::fixed("environment.clouds.quality");
pub const ENV_CLOUDS_DIM_SUN: UiId = UiId::fixed("environment.clouds.dim_sun");
pub const ENV_CLOUDS_COVERAGE: UiId = UiId::fixed("environment.clouds.coverage");
pub const ENV_CLOUDS_DENSITY: UiId = UiId::fixed("environment.clouds.density");
pub const ENV_CLOUDS_BASE_ALTITUDE: UiId = UiId::fixed("environment.clouds.base_altitude");
pub const ENV_CLOUDS_TOP_ALTITUDE: UiId = UiId::fixed("environment.clouds.top_altitude");
pub const ENV_CLOUDS_DETAIL: UiId = UiId::fixed("environment.clouds.detail");
pub const ENV_CLOUDS_WIND_SPEED: UiId = UiId::fixed("environment.clouds.wind_speed");
pub const ENV_CLOUDS_WIND_DIRECTION: UiId = UiId::fixed("environment.clouds.wind_direction");
pub const ENV_CLOUDS_SUN_SCATTER: UiId = UiId::fixed("environment.clouds.sun_scatter");
pub const ENV_CLOUDS_DARKNESS: UiId = UiId::fixed("environment.clouds.darkness");
pub const ENV_CLOUDS_SKY_LIGHT: UiId = UiId::fixed("environment.clouds.sky_light");
pub const ENV_CLOUDS_MIST_DENSITY: UiId = UiId::fixed("environment.clouds.mist_density");
pub const ENV_CLOUDS_MIST_HEIGHT: UiId = UiId::fixed("environment.clouds.mist_height");
pub const ENV_CLOUDS_TINT_R: UiId = UiId::fixed("environment.clouds.tint_r");
pub const ENV_CLOUDS_TINT_G: UiId = UiId::fixed("environment.clouds.tint_g");
pub const ENV_CLOUDS_TINT_B: UiId = UiId::fixed("environment.clouds.tint_b");
pub const ENV_WEATHER_ENABLED: UiId = UiId::fixed("environment.weather.enabled");
pub const ENV_WEATHER_LINK_SKY: UiId = UiId::fixed("environment.weather.link_sky");
pub const ENV_FOG_ENABLED: UiId = UiId::fixed("environment.fog.enabled");
pub const ENV_FOG_DENSITY: UiId = UiId::fixed("environment.fog.density");
pub const ENV_FOG_HEIGHT_FALLOFF: UiId = UiId::fixed("environment.fog.height_falloff");
pub const ENV_FOG_BASE_HEIGHT: UiId = UiId::fixed("environment.fog.base_height");
pub const ENV_FOG_START: UiId = UiId::fixed("environment.fog.start");
pub const ENV_FOG_MAX_OPACITY: UiId = UiId::fixed("environment.fog.max_opacity");
pub const ENV_FOG_SKY_BLEND: UiId = UiId::fixed("environment.fog.sky_blend");
pub const ENV_FOG_SUN_SCATTER: UiId = UiId::fixed("environment.fog.sun_scatter");
pub const ENV_FOG_SUN_EXPONENT: UiId = UiId::fixed("environment.fog.sun_exponent");
pub const ENV_FOG_COLOR_R: UiId = UiId::fixed("environment.fog.color_r");
pub const ENV_FOG_COLOR_G: UiId = UiId::fixed("environment.fog.color_g");
pub const ENV_FOG_COLOR_B: UiId = UiId::fixed("environment.fog.color_b");
pub const ENV_RAIN_ENABLED: UiId = UiId::fixed("environment.rain.enabled");
pub const ENV_RAIN_INTENSITY: UiId = UiId::fixed("environment.rain.intensity");
pub const ENV_RAIN_WIND_SPEED: UiId = UiId::fixed("environment.rain.wind_speed");
pub const ENV_RAIN_WIND_DIRECTION: UiId = UiId::fixed("environment.rain.wind_direction");
pub const ENV_RAIN_DROP_LENGTH: UiId = UiId::fixed("environment.rain.drop_length");
pub const ENV_RAIN_SPEED: UiId = UiId::fixed("environment.rain.speed");
pub const ENV_RAIN_BRIGHTNESS: UiId = UiId::fixed("environment.rain.brightness");
pub const ENV_RAIN_WETNESS: UiId = UiId::fixed("environment.rain.wetness");
pub const ENV_CLOUDS_GROUND_SHADOWS: UiId = UiId::fixed("environment.clouds.ground_shadows");
pub const ENV_CLOUDS_SHADOW_STRENGTH: UiId = UiId::fixed("environment.clouds.shadow_strength");
pub const ENV_CLOUDS_SILVER_LINING: UiId = UiId::fixed("environment.clouds.silver_lining");
pub const ENV_CLOUDS_SUN_SHAFTS: UiId = UiId::fixed("environment.clouds.sun_shafts");
pub const ENV_RAIN_SPLASHES: UiId = UiId::fixed("environment.rain.splashes");
pub const ENV_AUDIO_ENABLED: UiId = UiId::fixed("environment.audio.enabled");
pub const ENV_AUDIO_VOLUME: UiId = UiId::fixed("environment.audio.volume");
pub const ENV_WEATHER_WIND_GUST: UiId = UiId::fixed("environment.weather.wind_gust");
pub const ENV_WEATHER_WIND_FOLIAGE: UiId = UiId::fixed("environment.weather.wind_foliage");
pub const ENV_LIGHTNING_ENABLED: UiId = UiId::fixed("environment.lightning.enabled");
pub const ENV_LIGHTNING_THUNDER: UiId = UiId::fixed("environment.lightning.thunder");
pub const ENV_LIGHTNING_RATE: UiId = UiId::fixed("environment.lightning.rate");
pub const ENV_LIGHTNING_INTENSITY: UiId = UiId::fixed("environment.lightning.intensity");
pub const ENV_LIGHTNING_DISTANCE_MIN: UiId = UiId::fixed("environment.lightning.distance_min");
pub const ENV_LIGHTNING_DISTANCE_MAX: UiId = UiId::fixed("environment.lightning.distance_max");
pub const WATERGEN_CANVAS: UiId = UiId::fixed("watergen.canvas");
pub const WATERGEN_UNDO: UiId = UiId::fixed("watergen.toolbar.undo");
pub const WATERGEN_REDO: UiId = UiId::fixed("watergen.toolbar.redo");
pub const WATERGEN_FIT: UiId = UiId::fixed("watergen.toolbar.fit");
pub const WATERGEN_NEW: UiId = UiId::fixed("watergen.toolbar.new");
pub const WATERGEN_SEARCH: UiId = UiId::fixed("watergen.search");
pub const WATERGEN_SNAP: UiId = UiId::fixed("watergen.snap");
pub const WATERGEN_DELETE_NODE: UiId = UiId::fixed("watergen.delete_node");
pub const WATERGEN_PREVIEW: UiId = UiId::fixed("watergen.preview");
pub const WATERGEN_PLAY: UiId = UiId::fixed("watergen.play");
pub const WATERGEN_PRESET: UiId = UiId::fixed("watergen.preset");
pub const WATERGEN_NAME: UiId = UiId::fixed("watergen.name");
pub const WATERGEN_EXPORT: UiId = UiId::fixed("watergen.export");
pub const WATERGEN_PLACE: UiId = UiId::fixed("watergen.place");
pub const WATERGEN_STATS: UiId = UiId::fixed("watergen.stats");
pub const PANE_WORLD: UiId = UiId::fixed("editor.pane.world");
pub const PANE_TAB_WORLD: UiId = UiId::fixed("editor.pane.tab.world");
pub const SHOW_WORLD: UiId = UiId::fixed("editor.menu.view.world");
pub const WORLD_NAME: UiId = UiId::fixed("world.toolbar.name");
pub const WORLD_UNDO: UiId = UiId::fixed("world.toolbar.undo");
pub const WORLD_REDO: UiId = UiId::fixed("world.toolbar.redo");
pub const WORLD_NEW: UiId = UiId::fixed("world.toolbar.new");
pub const WORLD_SAVE: UiId = UiId::fixed("world.toolbar.save");
pub const WORLD_PREVIEW: UiId = UiId::fixed("world.toolbar.preview");
pub const WORLD_SAVE_PREFAB: UiId = UiId::fixed("world.toolbar.save_prefab");
pub const WORLD_BUILD: UiId = UiId::fixed("world.toolbar.build");
pub const WORLD_LIBRARY_SEARCH: UiId = UiId::fixed("world.library.search");
pub const WORLD_TERRAIN: UiId = UiId::fixed("world.composition.terrain");
pub const WORLD_TERRAIN_SIZE_X: UiId = UiId::fixed("world.composition.terrain_size_x");
pub const WORLD_TERRAIN_SIZE_Z: UiId = UiId::fixed("world.composition.terrain_size_z");
pub const WORLD_PREVIEW_NOTE: UiId = UiId::fixed("world.composition.preview");
pub const WORLD_SEED: UiId = UiId::fixed("world.rules.seed");
pub const WORLD_RESEED: UiId = UiId::fixed("world.rules.reseed");
pub const WORLD_UNIQUE: UiId = UiId::fixed("world.rules.unique");
pub const WORLD_ADD_TERM: UiId = UiId::fixed("world.rules.add_term");
pub const WORLD_ADD_ADAPT: UiId = UiId::fixed("world.rules.add_adapt");
pub const WORLD_ADD_PIN: UiId = UiId::fixed("world.rules.add_pin");
pub const WORLD_ADD_MOVE: UiId = UiId::fixed("world.rules.add_move");
pub const WORLD_REMOVE_IN_REGION: UiId = UiId::fixed("world.rules.remove_in_region");
pub const WORLD_STATUS: UiId = UiId::fixed("world.status");
pub const PANE_TERRAIN_GENERATOR: UiId = UiId::fixed("editor.pane.terrain_generator");
pub const PANE_TAB_TERRAIN_GENERATOR: UiId = UiId::fixed("editor.pane.tab.terrain_generator");
pub const SHOW_TERRAIN_GENERATOR: UiId = UiId::fixed("editor.menu.view.terrain_generator");
pub const TERRAINGEN_CANVAS: UiId = UiId::fixed("terraingen.canvas");
pub const TERRAINGEN_UNDO: UiId = UiId::fixed("terraingen.toolbar.undo");
pub const TERRAINGEN_REDO: UiId = UiId::fixed("terraingen.toolbar.redo");
pub const TERRAINGEN_FIT: UiId = UiId::fixed("terraingen.toolbar.fit");
pub const TERRAINGEN_NEW: UiId = UiId::fixed("terraingen.toolbar.new");
pub const TERRAINGEN_SEARCH: UiId = UiId::fixed("terraingen.search");
pub const TERRAINGEN_SNAP: UiId = UiId::fixed("terraingen.snap");
pub const TERRAINGEN_DELETE_NODE: UiId = UiId::fixed("terraingen.delete_node");
pub const TERRAINGEN_PREVIEW: UiId = UiId::fixed("terraingen.preview");
pub const TERRAINGEN_PRESET: UiId = UiId::fixed("terraingen.preset");
pub const TERRAINGEN_NAME: UiId = UiId::fixed("terraingen.name");
pub const TERRAINGEN_EXPORT: UiId = UiId::fixed("terraingen.export");
pub const TERRAINGEN_PLACE: UiId = UiId::fixed("terraingen.place");
pub const TERRAINGEN_STATS: UiId = UiId::fixed("terraingen.stats");
pub const NATUREGEN_PREVIEW: UiId = UiId::fixed("naturegen.preview");
pub const NATUREGEN_PRESET: UiId = UiId::fixed("naturegen.preset");
pub const NATUREGEN_QUALITY: UiId = UiId::fixed("naturegen.quality");
pub const NATUREGEN_NAME: UiId = UiId::fixed("naturegen.name");
pub const NATUREGEN_EXPORT: UiId = UiId::fixed("naturegen.export");
pub const NATUREGEN_PLACE: UiId = UiId::fixed("naturegen.place");
pub const NATUREGEN_STATS: UiId = UiId::fixed("naturegen.stats");
pub const SHOW_NATURE_GENERATOR: UiId = UiId::fixed("editor.menu.view.nature_generator");
pub const NATUREGEN_CANVAS: UiId = UiId::fixed("naturegen.canvas");
pub const NATUREGEN_UNDO: UiId = UiId::fixed("naturegen.toolbar.undo");
pub const NATUREGEN_REDO: UiId = UiId::fixed("naturegen.toolbar.redo");
pub const NATUREGEN_FIT: UiId = UiId::fixed("naturegen.toolbar.fit");
pub const NATUREGEN_NEW: UiId = UiId::fixed("naturegen.toolbar.new");
pub const NATUREGEN_SEARCH: UiId = UiId::fixed("naturegen.search");
pub const NATUREGEN_SNAP: UiId = UiId::fixed("naturegen.snap");
pub const NATUREGEN_MERGE_PARTS: UiId = UiId::fixed("naturegen.merge_parts");
pub const NATUREGEN_MICROMESH: UiId = UiId::fixed("naturegen.micromesh");
pub const NATUREGEN_SCATTER: UiId = UiId::fixed("naturegen.scatter");
pub const TERRAINGEN_SCATTER: UiId = UiId::fixed("terraingen.scatter");
pub const TERRAINGEN_SCATTER_PLAN: UiId = UiId::fixed("terraingen.scatter_plan");
pub const NATUREGEN_DELETE_NODE: UiId = UiId::fixed("naturegen.delete_node");
pub const NATUREGEN_EDIT_MATERIAL: UiId = UiId::fixed("naturegen.edit_material");
pub const PANE_GENERATOR: UiId = UiId::fixed("editor.pane.generator");
pub const PANE_SCRIPT: UiId = UiId::fixed("editor.pane.script");
pub const PANE_DRYAD_NODES: UiId = UiId::fixed("editor.pane.dryad_nodes");
pub const PANE_CONSOLE: UiId = UiId::fixed("editor.pane.console");
pub const HIERARCHY_ROOT: UiId = UiId::fixed("hierarchy.tree.scene");
pub const HIERARCHY_ADD: UiId = UiId::fixed("hierarchy.toolbar.add");
pub const CREATE_ENTITY: UiId = UiId::fixed("editor.scene.create_entity");
pub const HIERARCHY_EMPTY: UiId = UiId::fixed("hierarchy.tree.empty");
pub const PREFAB_PALETTE: UiId = UiId::fixed("create.palette");
pub const PREFAB_CLOSE: UiId = UiId::fixed("create.palette.close");
pub const HIERARCHY_SEARCH: UiId = UiId::fixed("hierarchy.search");
pub const PANE_TAB_HIERARCHY: UiId = UiId::fixed("editor.pane.tab.hierarchy");
pub const PANE_TAB_ASSETS: UiId = UiId::fixed("editor.pane.tab.assets");
pub const PANE_TAB_VIEWPORT: UiId = UiId::fixed("editor.pane.tab.viewport");
pub const PANE_TAB_INSPECTOR: UiId = UiId::fixed("editor.pane.tab.inspector");
pub const PANE_TAB_MATERIAL: UiId = UiId::fixed("editor.pane.tab.material");
pub const PANE_TAB_GENERATOR: UiId = UiId::fixed("editor.pane.tab.generator");
pub const PANE_TAB_TEXTURE_SET: UiId = UiId::fixed("editor.pane.tab.texture_set");
pub const PANE_TAB_SCRIPT: UiId = UiId::fixed("editor.pane.tab.script");
pub const PANE_TAB_DRYAD_NODES: UiId = UiId::fixed("editor.pane.tab.dryad_nodes");
pub const PANE_TAB_CONSOLE: UiId = UiId::fixed("editor.pane.tab.console");
pub const CONSOLE_FILTER_ERROR: UiId = UiId::fixed("console.filter.error");
pub const CONSOLE_FILTER_LOG: UiId = UiId::fixed("console.filter.log");
pub const CONSOLE_FILTER_ACTION: UiId = UiId::fixed("console.filter.action");
pub const CONSOLE_FILTER_ENGINE: UiId = UiId::fixed("console.filter.engine");
pub const CONSOLE_FILTER_UI: UiId = UiId::fixed("console.filter.ui");

// ── Viewport ─────────────────────────────────────────────────────────────
pub const VIEWPORT_SURFACE: UiId = UiId::fixed("viewport.canvas.surface");
pub const RESET_CAMERA: UiId = UiId::fixed("viewport.camera.reset");
pub const FOCUS_SELECTION: UiId = UiId::fixed("viewport.camera.focus_selection");

// ── Inspector ────────────────────────────────────────────────────────────
pub const ENTITY_NAME: UiId = UiId::fixed("inspector.entity.name");
pub const DELETE_ENTITY: UiId = UiId::fixed("inspector.entity.delete");
pub const INSPECTOR_MATERIAL: UiId = UiId::fixed("inspector.material.slot");
pub const INSPECTOR_MATERIAL_CLEAR: UiId = UiId::fixed("inspector.material.clear");

// ── Material studio ──────────────────────────────────────────────────────
pub const MATERIAL_NEW: UiId = UiId::fixed("material.toolbar.new");
pub const MATERIAL_SAVE: UiId = UiId::fixed("material.toolbar.save");
pub const MATERIAL_APPLY: UiId = UiId::fixed("material.toolbar.apply");
pub const MATERIAL_NAME: UiId = UiId::fixed("material.surface.name");
pub const MATERIAL_PRESET: UiId = UiId::fixed("material.surface.preset");
pub const MATERIAL_PREVIEW: UiId = UiId::fixed("material.preview.sphere");
pub const MATERIAL_TEXTURE_ALBEDO: UiId = UiId::fixed("material.map.albedo");
pub const MATERIAL_TEXTURE_NORMAL: UiId = UiId::fixed("material.map.normal");
pub const MATERIAL_TEXTURE_ORM: UiId = UiId::fixed("material.map.orm");
pub const MATERIAL_TEXTURE_EMISSION: UiId = UiId::fixed("material.map.emission");
pub const MATERIAL_TEXTURE_SOURCE: UiId = UiId::fixed("material.map.source");
pub const MATERIAL_BAKE_NORMAL: UiId = UiId::fixed("material.bake.normal");
pub const MATERIAL_BAKE_AO: UiId = UiId::fixed("material.bake.ao");
pub const MATERIAL_BAKE_ROUGHNESS: UiId = UiId::fixed("material.bake.roughness");
pub const MATERIAL_BAKE_METALLIC: UiId = UiId::fixed("material.bake.metallic");
pub const MATERIAL_BAKE_ORM: UiId = UiId::fixed("material.bake.orm");
pub const MATERIAL_BAKE_ALL: UiId = UiId::fixed("material.bake.all");
pub const MATERIAL_GENERATOR: UiId = UiId::fixed("material.toolbar.generator");
pub const MATERIAL_TEXTURE_SET: UiId = UiId::fixed("material.toolbar.texture_set");
pub const INSPECTOR_MATERIAL_PICKER: UiId = UiId::fixed("inspector.material.picker");
pub const INSPECTOR_ADD_BEHAVIOUR: UiId = UiId::fixed("inspector.behaviour.add");
pub const BEHAVIOUR_PICKER: UiId = UiId::fixed("inspector.behaviour.picker");
pub const BEHAVIOUR_PICKER_CLOSE: UiId = UiId::fixed("inspector.behaviour.picker.close");
pub const BEHAVIOUR_PICKER_SEARCH: UiId = UiId::fixed("inspector.behaviour.picker.search");
pub const BEHAVIOUR_NEW: UiId = UiId::fixed("inspector.behaviour.new");

/// One script in the behaviour picker.
pub fn behaviour_item(index: usize) -> UiId {
    UiId::dynamic(format!("inspector.behaviour.picker.item.{index}"))
}

pub const MATERIAL_ASSET_PICKER: UiId = UiId::fixed("material.asset.picker");
pub const MATERIAL_ASSET_PICKER_CLOSE: UiId = UiId::fixed("material.asset.picker.close");
pub const MATERIAL_ASSET_PICKER_SEARCH: UiId = UiId::fixed("material.asset.picker.search");
pub const MATERIAL_PREVIEW_SHAPE: UiId = UiId::fixed("material.preview.shape");
pub const MATERIAL_PREVIEW_ENVIRONMENT: UiId = UiId::fixed("material.preview.environment");
pub const MATERIAL_PREVIEW_ENVIRONMENT_CLEAR: UiId =
    UiId::fixed("material.preview.environment.clear");
pub const MATERIAL_PREVIEW_EXPOSURE: UiId = UiId::fixed("material.preview.exposure");
pub const MATERIAL_SUN_YAW: UiId = UiId::fixed("material.preview.sun_yaw");
pub const MATERIAL_SUN_ELEVATION: UiId = UiId::fixed("material.preview.sun_elevation");
pub const MATERIAL_SUN_INTENSITY: UiId = UiId::fixed("material.preview.sun_intensity");
pub const MATERIAL_PREVIEW_SHADOWS: UiId = UiId::fixed("material.preview.shadows");
pub const MATERIAL_ENVIRONMENT_PICKER: UiId = UiId::fixed("material.environment.picker");
pub const MATERIAL_ENVIRONMENT_PICKER_CLOSE: UiId =
    UiId::fixed("material.environment.picker.close");
pub const MATERIAL_ENVIRONMENT_PICKER_SEARCH: UiId =
    UiId::fixed("material.environment.picker.search");
pub const MATERIAL_PACK_HEIGHT: UiId = UiId::fixed("material.surface.pack_height");
pub const MATERIAL_HEIGHT_PICKER: UiId = UiId::fixed("material.height.picker");
pub const MATERIAL_HEIGHT_PICKER_CLOSE: UiId = UiId::fixed("material.height.picker.close");
pub const MATERIAL_HEIGHT_PICKER_SEARCH: UiId = UiId::fixed("material.height.picker.search");
pub const MATERIAL_SCROLL: UiId = UiId::fixed("material.body.scroll");
pub const MATERIAL_BAKE_DIRECTX: UiId = UiId::fixed("material.bake.directx");
pub const MATERIAL_BAKE_WRAP: UiId = UiId::fixed("material.bake.wrap");
pub const MATERIAL_BAKE_SCENE_AO: UiId = UiId::fixed("material.bake.scene_ao");
pub const FILEBROWSER: UiId = UiId::fixed("filebrowser.window.root");
pub const FILEBROWSER_CLOSE: UiId = UiId::fixed("filebrowser.window.close");
pub const FILEBROWSER_UP: UiId = UiId::fixed("filebrowser.nav.up");
pub const FILEBROWSER_REFRESH: UiId = UiId::fixed("filebrowser.nav.refresh");
pub const FILEBROWSER_PATH: UiId = UiId::fixed("filebrowser.nav.path");
pub const FILEBROWSER_SEARCH: UiId = UiId::fixed("filebrowser.filter.search");
pub const FILEBROWSER_SUPPORTED: UiId = UiId::fixed("filebrowser.filter.supported");
pub const FILEBROWSER_HIDDEN: UiId = UiId::fixed("filebrowser.filter.hidden");
pub const FILEBROWSER_SELECT_SUPPORTED: UiId = UiId::fixed("filebrowser.action.select_all");
pub const FILEBROWSER_CLEAR: UiId = UiId::fixed("filebrowser.action.clear");
pub const FILEBROWSER_CONFIRM: UiId = UiId::fixed("filebrowser.action.confirm");
pub const FILEBROWSER_CANCEL: UiId = UiId::fixed("filebrowser.action.cancel");
pub const WIZARD_IMPORT_BROWSE: UiId = UiId::fixed("assets.wizard.import.browse");
pub const ASSET_PICK: UiId = UiId::fixed("asset.pick.root");
pub const ASSET_PICK_CLOSE: UiId = UiId::fixed("asset.pick.close");
pub const ASSET_PICK_SEARCH: UiId = UiId::fixed("asset.pick.search");
pub const ASSET_PICK_NONE: UiId = UiId::fixed("asset.pick.none");
pub const MATERIAL_PICKER: UiId = UiId::fixed("material.picker");
pub const MATERIAL_PICKER_CLOSE: UiId = UiId::fixed("material.picker.close");
pub const MATERIAL_PICKER_SEARCH: UiId = UiId::fixed("material.picker.search");

// ── Material generator ───────────────────────────────────────────────────
pub const NODE_CANVAS: UiId = UiId::fixed("generator.canvas");
pub const GENERATOR_BAKE: UiId = UiId::fixed("generator.toolbar.bake");
pub const GENERATOR_OPEN_MATERIAL: UiId = UiId::fixed("generator.toolbar.material");
pub const GENERATOR_DELETE_NODE: UiId = UiId::fixed("generator.node.delete");
pub const GENERATOR_RESOLUTION: UiId = UiId::fixed("generator.toolbar.resolution");
pub const GENERATOR_PREVIEW: UiId = UiId::fixed("generator.preview");
pub const GENERATOR_LIBRARY: UiId = UiId::fixed("generator.library");
pub const GENERATOR_CHANNELS: UiId = UiId::fixed("generator.preview.channels");

/// One chip of the output-preview channel switcher (`Base`, `Normal`, `Height`…).
pub fn generator_channel(label: &str) -> UiId {
    UiId::dynamic(format!(
        "generator.preview.channel.{}",
        label.to_lowercase()
    ))
}
pub const GENERATOR_UNDO: UiId = UiId::fixed("generator.toolbar.undo");
pub const GENERATOR_REDO: UiId = UiId::fixed("generator.toolbar.redo");
pub const GENERATOR_FIT: UiId = UiId::fixed("generator.toolbar.fit");
pub const GENERATOR_RESEED: UiId = UiId::fixed("generator.toolbar.reseed");
pub const GENERATOR_SNAP: UiId = UiId::fixed("generator.toolbar.snap");
pub const GENERATOR_SEARCH: UiId = UiId::fixed("generator.toolbar.search");
pub const GENERATOR_DELETE_LINK: UiId = UiId::fixed("generator.link.delete");
pub const GENERATOR_COPY: UiId = UiId::fixed("generator.toolbar.copy");
pub const GENERATOR_PASTE: UiId = UiId::fixed("generator.toolbar.paste");
pub const GENERATOR_TEXTURE_PICKER: UiId = UiId::fixed("generator.texture.picker");
pub const GENERATOR_TEXTURE_PICKER_CLOSE: UiId = UiId::fixed("generator.texture.picker.close");
pub const GENERATOR_TEXTURE_PICKER_SEARCH: UiId = UiId::fixed("generator.texture.picker.search");
pub const NATUREGEN_PICKER: UiId = UiId::fixed("naturegen.picker");
pub const NATUREGEN_PICKER_CLOSE: UiId = UiId::fixed("naturegen.picker.close");
pub const NATUREGEN_PICKER_SEARCH: UiId = UiId::fixed("naturegen.picker.search");
pub const GENERATOR_BAKE_PROGRESS: UiId = UiId::fixed("generator.bake.progress");
pub const GENERATOR_CANCEL_BAKE: UiId = UiId::fixed("generator.bake.cancel");
pub const GENERATOR_PACKING_NAME: UiId = UiId::fixed("generator.packing.name");
pub const GENERATOR_PACKING_SAVE: UiId = UiId::fixed("generator.packing.save");
pub const GENERATOR_PACKING_DELETE: UiId = UiId::fixed("generator.packing.delete");

pub fn generator_group(name: &str) -> UiId {
    UiId::dynamic(format!(
        "generator.group.{}",
        name.to_lowercase().replace(' ', "_")
    ))
}

pub fn texturegen_add(kind: &str) -> UiId {
    UiId::dynamic(format!("texturegen.add.{kind}"))
}

pub fn texturegen_group(name: &str) -> UiId {
    UiId::dynamic(format!(
        "texturegen.group.{}",
        name.to_lowercase().replace([' ', '&'], "_")
    ))
}

pub fn texturegen_mode(label: &str) -> UiId {
    UiId::dynamic(format!("texturegen.preview.mode.{}", label.to_lowercase()))
}

pub fn naturegen_group(name: &str) -> UiId {
    UiId::dynamic(format!(
        "naturegen.group.{}",
        name.to_lowercase().replace(' ', "_")
    ))
}

pub fn naturegen_add(op: &str) -> UiId {
    UiId::dynamic(format!("naturegen.add.{op}"))
}

pub fn naturegen_param(key: &str) -> UiId {
    UiId::dynamic(format!("naturegen.param.{key}"))
}

/// One row of the profiler's table (`profiler.table.row.N`).
pub fn profiler_row(index: usize) -> UiId {
    UiId::dynamic(format!("profiler.table.row.{index}"))
}

pub fn watergen_group(name: &str) -> UiId {
    UiId::dynamic(format!(
        "watergen.group.{}",
        name.to_lowercase().replace(' ', "_")
    ))
}

pub fn watergen_add(op: &str) -> UiId {
    UiId::dynamic(format!("watergen.add.{op}"))
}

pub fn watergen_param(key: &str) -> UiId {
    UiId::dynamic(format!("watergen.param.{key}"))
}

/// A recipe of the World tab's Library (`world.library.item.N`).
pub fn world_library_item(index: usize) -> UiId {
    UiId::dynamic(format!("world.library.item.{index}"))
}

/// A class row of the composition (`world.composition.class_row.N`, N = the class's place in the file).
pub fn world_class_row(index: usize) -> UiId {
    UiId::dynamic(format!("world.composition.class_row.{index}"))
}

pub fn world_class_remove(index: usize) -> UiId {
    UiId::dynamic(format!("world.composition.class_remove.{index}"))
}

pub fn world_water_row(index: usize) -> UiId {
    UiId::dynamic(format!("world.composition.water_row.{index}"))
}

pub fn world_water_remove(index: usize) -> UiId {
    UiId::dynamic(format!("world.composition.water_remove.{index}"))
}

/// A slider of a water row: `world.composition.water.N.<min|level|carve>`.
pub fn world_water_value(index: usize, key: &str) -> UiId {
    UiId::dynamic(format!("world.composition.water.{index}.{key}"))
}

pub fn world_material_slot(index: usize) -> UiId {
    UiId::dynamic(format!("world.composition.material_slot.{index}"))
}

pub fn world_material_remove(index: usize) -> UiId {
    UiId::dynamic(format!("world.composition.material_remove.{index}"))
}

/// A slider of the selected class: `world.rules.class.<variants|priority|cell|...>`.
pub fn world_class_field(key: &str) -> UiId {
    UiId::dynamic(format!("world.rules.class.{key}"))
}

/// A control of a `where` term: `world.rules.term.N.<mask|rise0|rise1|fall1|fall0|remove>`.
pub fn world_term(index: usize, key: &str) -> UiId {
    UiId::dynamic(format!("world.rules.term.{index}.{key}"))
}

/// A control of an `adapt` line: `world.rules.adapt.N.<target|mask|amount|remove>`.
pub fn world_adapt(index: usize, key: &str) -> UiId {
    UiId::dynamic(format!("world.rules.adapt.{index}.{key}"))
}

/// A slider of the edit region: `world.rules.region.N` (x0, z0, x1, z1).
pub fn world_region(index: usize) -> UiId {
    UiId::dynamic(format!("world.rules.region.{index}"))
}

/// A control of an override row: `world.rules.override.N.<row|remove|cell_x|cell_z|x|z|variant>`.
pub fn world_override(index: usize, key: &str) -> UiId {
    UiId::dynamic(format!("world.rules.override.{index}.{key}"))
}

pub fn terraingen_group(name: &str) -> UiId {
    UiId::dynamic(format!(
        "terraingen.group.{}",
        name.to_lowercase().replace(' ', "_")
    ))
}

pub fn terraingen_add(op: &str) -> UiId {
    UiId::dynamic(format!("terraingen.add.{op}"))
}

pub fn terraingen_param(key: &str) -> UiId {
    UiId::dynamic(format!("terraingen.param.{key}"))
}

pub fn generator_add(kind: &str) -> UiId {
    UiId::dynamic(format!("generator.add.{kind}"))
}

pub const TEXTURE_SET_NEW: UiId = UiId::fixed("texture_set.toolbar.new");
pub const TEXTURE_SET_SAVE: UiId = UiId::fixed("texture_set.toolbar.save");
pub const TEXTURE_SET_UNDO: UiId = UiId::fixed("texture_set.toolbar.undo");
pub const TEXTURE_SET_REDO: UiId = UiId::fixed("texture_set.toolbar.redo");
pub const TEXTURE_SET_TILE_ADD: UiId = UiId::fixed("texture_set.tiles.add");
pub const TEXTURE_SET_LAYER_ADD_PAINT: UiId = UiId::fixed("texture_set.layers.add_paint");
pub const TEXTURE_SET_LAYER_ADD_FILL: UiId = UiId::fixed("texture_set.layers.add_fill");
pub const TEXTURE_SET_LAYER_DELETE: UiId = UiId::fixed("texture_set.layers.delete");
pub const TEXTURE_SET_LAYER_UP: UiId = UiId::fixed("texture_set.layers.up");
pub const TEXTURE_SET_LAYER_DOWN: UiId = UiId::fixed("texture_set.layers.down");
pub const TEXTURE_SET_UV_CANVAS: UiId = UiId::fixed("texture_set.uv.canvas");
pub const TEXTURE_SET_NAME: UiId = UiId::fixed("texture_set.name");
pub const TEXTURE_SET_CHANNEL: UiId = UiId::fixed("texture_set.channel");
pub const TEXTURE_SET_MASK: UiId = UiId::fixed("texture_set.mask");
pub const TEXTURE_SET_PAINT_3D: UiId = UiId::fixed("texture_set.paint_3d");
pub const TEXTURE_SET_EXPORT_PRESET: UiId = UiId::fixed("texture_set.export.preset");
pub const TEXTURE_SET_EXPORT: UiId = UiId::fixed("texture_set.export.run");
pub const TEXTURE_SET_BAKE_AO: UiId = UiId::fixed("texture_set.bake.ao");
pub const TEXTURE_SET_BAKE_CURVATURE: UiId = UiId::fixed("texture_set.bake.curvature");
pub const TEXTURE_SET_BAKE_SCENE_AO: UiId = UiId::fixed("texture_set.bake.scene_ao");
pub const TEXTURE_SET_BAKE_NORMAL: UiId = UiId::fixed("texture_set.bake.normal");
pub const TEXTURE_SET_BAKE_POSITION: UiId = UiId::fixed("texture_set.bake.position");
pub const TEXTURE_SET_BAKE_OBJECT_ID: UiId = UiId::fixed("texture_set.bake.object_id");

pub fn texture_set_tile(tile: u32) -> UiId {
    UiId::dynamic(format!("texture_set.tile.{tile}"))
}

pub fn texture_set_layer(index: usize) -> UiId {
    UiId::dynamic(format!("texture_set.layer.{index}"))
}

pub fn texture_set_layer_visible(index: usize) -> UiId {
    UiId::dynamic(format!("texture_set.layer.visible.{index}"))
}

pub fn texture_set_brush(name: &str) -> UiId {
    UiId::dynamic(format!("texture_set.brush.{name}"))
}

pub fn texture_set_brush_color(axis: usize) -> UiId {
    UiId::dynamic(format!("texture_set.brush.color.{axis}"))
}

pub fn generator_field(name: &str) -> UiId {
    UiId::dynamic(format!("generator.field.{name}"))
}

pub fn generator_axis(name: &str, axis: usize) -> UiId {
    UiId::dynamic(format!("generator.field.{name}.{axis}"))
}

/// A labelled slider/text control in the material surface.
pub fn material_field(name: &str) -> UiId {
    UiId::dynamic(format!("material.surface.{name}"))
}

pub fn material_axis(name: &str, axis: usize) -> UiId {
    UiId::dynamic(format!("material.surface.{name}.{axis}"))
}

/// A texture slot or its clear button (`suffix` is empty or `clear`).
pub fn material_texture(slot: &str) -> UiId {
    UiId::dynamic(format!("material.map.{slot}"))
}

pub fn material_texture_clear(slot: &str) -> UiId {
    UiId::dynamic(format!("material.map.{slot}.clear"))
}

pub fn material_section(section: &str) -> UiId {
    UiId::dynamic(format!("material.section.{section}"))
}

pub fn material_alpha_mode(mode: usize) -> UiId {
    UiId::dynamic(format!("material.surface.alpha_mode.{mode}"))
}

pub fn naturegen_picker_item(index: usize) -> UiId {
    UiId::dynamic(format!("naturegen.picker.item.{index}"))
}

pub fn generator_texture_item(index: usize) -> UiId {
    UiId::dynamic(format!("generator.texture.picker.item.{index}"))
}

pub fn material_asset_item(index: usize) -> UiId {
    UiId::dynamic(format!("material.asset.picker.item.{index}"))
}

pub fn material_height_item(index: usize) -> UiId {
    UiId::dynamic(format!("material.height.picker.item.{index}"))
}

pub fn material_environment_item(index: usize) -> UiId {
    UiId::dynamic(format!("material.environment.picker.item.{index}"))
}

/// One entry of the file browser's list.
pub fn file_browser_row(index: usize) -> UiId {
    UiId::dynamic(format!("filebrowser.list.entry.{index}"))
}

/// One shortcut in the file browser's sidebar.
pub fn file_browser_place(index: usize) -> UiId {
    UiId::dynamic(format!("filebrowser.places.place.{index}"))
}

/// One step of the file browser's path trail.
pub fn file_browser_crumb(index: usize) -> UiId {
    UiId::dynamic(format!("filebrowser.nav.crumb.{index}"))
}

/// One row of the shared asset picker.
pub fn asset_pick_item(index: usize) -> UiId {
    UiId::dynamic(format!("asset.pick.item.{index}"))
}

/// One kind filter chip of the shared asset picker (`0` is "All").
pub fn asset_pick_kind(index: usize) -> UiId {
    UiId::dynamic(format!("asset.pick.kind.{index}"))
}

pub fn material_picker_item(index: usize) -> UiId {
    UiId::dynamic(format!("material.picker.item.{index}"))
}

/// One editable field inside a component block.
pub fn component_field(component: usize, field: usize, axis: usize) -> UiId {
    UiId::dynamic(format!(
        "inspector.component.field.{component}.{field}.{axis}"
    ))
}

/// The expand/collapse chevron of a hierarchy row.
pub fn tree_toggle(parent: &str) -> UiId {
    UiId::dynamic(format!("hierarchy.tree.toggle.{parent}"))
}

/// The remove button of one component block.
pub fn transform_axis(kind: usize, axis: usize) -> UiId {
    UiId::dynamic(format!("inspector.transform.{kind}.{axis}"))
}

pub fn component_toggle(component: usize) -> UiId {
    UiId::dynamic(format!("inspector.component.toggle.{component}"))
}

pub fn component_remove(component: usize) -> UiId {
    UiId::dynamic(format!("inspector.component.remove.{component}"))
}

pub fn pane_maximize(pane: &str) -> UiId {
    UiId::dynamic(format!("editor.pane.maximize.{pane}"))
}

/// One row of the hierarchy list.
pub fn prefab_category(key: &str) -> UiId {
    UiId::dynamic(format!("create.palette.category.{key}"))
}

pub fn prefab_item(key: &str) -> UiId {
    UiId::dynamic(format!("create.palette.item.{key}"))
}

/// The button that turns a pane's active tab into a window of its own.
pub fn pane_detach(pane: &str) -> UiId {
    UiId::dynamic(format!("editor.pane.detach.{pane}"))
}

/// The button of a detached window that docks its page back into the editor.
pub fn window_dock(kind: &str) -> UiId {
    UiId::dynamic(format!("editor.window.dock.{kind}"))
}

/// The body of a detached window.
pub fn window_body(kind: &str) -> UiId {
    UiId::dynamic(format!("editor.window.body.{kind}"))
}

pub fn pane_split(pane: &str) -> UiId {
    UiId::dynamic(format!("editor.pane.split.{pane}"))
}

/// One row of the hierarchy list.
pub fn entity_row(index: usize) -> UiId {
    UiId::dynamic(format!("hierarchy.list.entity_row.{index}"))
}

/// One card in the asset manager grid.
pub fn asset_card(index: usize) -> UiId {
    UiId::dynamic(format!("assets.grid.asset_card.{index}"))
}

/// One row in the asset manager list view.
pub fn asset_row(index: usize) -> UiId {
    UiId::dynamic(format!("assets.list.asset_row.{index}"))
}

/// One breadcrumb in the asset browser.
pub fn asset_crumb(index: usize) -> UiId {
    UiId::dynamic(format!("assets.toolbar.crumb.{index}"))
}

pub fn project_template(index: usize) -> UiId {
    UiId::dynamic(format!("project.create.template.{index}"))
}

pub fn project_recent_row(index: usize) -> UiId {
    UiId::dynamic(format!("project.recent.row.{index}"))
}

pub fn project_browse_row(index: usize) -> UiId {
    UiId::dynamic(format!("project.browse.row.{index}"))
}

pub fn script_tab(index: usize) -> UiId {
    UiId::dynamic(format!("script.editor.tab.{index}"))
}

pub fn script_tab_close(index: usize) -> UiId {
    UiId::dynamic(format!("script.editor.tab.close.{index}"))
}

pub fn dryad_palette_item(type_name: &str) -> UiId {
    UiId::dynamic(format!("dryad.nodes.palette.{type_name}"))
}

pub fn dryad_property(name: &str) -> UiId {
    UiId::dynamic(format!("dryad.nodes.property.{name}"))
}

/// The statically documented part of the UI tree.
pub const CATALOG: &[UiMeta] = &[
    UiMeta {
        id: NATUREGEN_CANVAS,
        description: "Nature Generator node canvas: wire shapes, modifiers, trees, plants and scatter nodes into the Output.",
        kind: "node_canvas",
    },
    UiMeta {
        id: NATUREGEN_UNDO,
        description: "Undoes the last graph edit.",
        kind: "button",
    },
    UiMeta {
        id: NATUREGEN_REDO,
        description: "Redoes the last undone graph edit.",
        kind: "button",
    },
    UiMeta {
        id: NATUREGEN_FIT,
        description: "Frames the whole graph in the canvas.",
        kind: "button",
    },
    UiMeta {
        id: NATUREGEN_NEW,
        description: "Starts an empty graph with an Output node.",
        kind: "button",
    },
    UiMeta {
        id: NATUREGEN_SEARCH,
        description: "Filters the node palette.",
        kind: "text_field",
    },
    UiMeta {
        id: NATUREGEN_SNAP,
        description: "Snaps node positions to a 16 px grid.",
        kind: "checkbox",
    },
    UiMeta {
        id: NATUREGEN_MERGE_PARTS,
        description: "Places the solid parts, and the cut-out parts, as one entity each, with a material slot per part.",
        kind: "checkbox",
    },
    UiMeta {
        id: NATUREGEN_MICROMESH,
        description: "Turns Micromesh (GPU cluster LOD) on for the dense solid parts, rocks and trunks, when placing.",
        kind: "checkbox",
    },
    UiMeta {
        id: TERRAINGEN_SCATTER,
        description: "Plants the project's scatter plan on the terrain (the selected one, else the last placed): trees, boulders and grass by slope, wetness and height.",
        kind: "button",
    },
    UiMeta {
        id: TERRAINGEN_SCATTER_PLAN,
        description: "Opens Scatter/default.scatter in the script editor.",
        kind: "button",
    },
    UiMeta {
        id: NATUREGEN_SCATTER,
        description: "Writes the model into Scatter/default.scatter as a class that places every part, so the Scatter action can plant it on a terrain.",
        kind: "button",
    },
    UiMeta {
        id: NATUREGEN_EDIT_MATERIAL,
        description: "Adds an Edit Library Material node after the selected library material or leaf texture, wired in place, so its colours, surface and wind can be changed.",
        kind: "button",
    },
    UiMeta {
        id: NATUREGEN_DELETE_NODE,
        description: "Deletes the selected node.",
        kind: "button",
    },
    UiMeta {
        id: SHOW_NATURE_GENERATOR,
        description: "Opens the Nature Generator tab (procedural rocks, trees and plants).",
        kind: "menu_item",
    },
    UiMeta {
        id: PANE_NATURE_GENERATOR,
        description: "Nature Generator: procedural rocks, trees and plants with a live 3D preview, OBJ export and one-click placement in the scene.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_NATURE_GENERATOR,
        description: "Tab of the Nature Generator pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: NATUREGEN_PREVIEW,
        description: "Software-rendered preview of the generated model. Drag to orbit, scroll to zoom.",
        kind: "image",
    },
    UiMeta {
        id: NATUREGEN_PRESET,
        description: "Starts from a ready-made rock, tree or plant.",
        kind: "dropdown",
    },
    UiMeta {
        id: NATUREGEN_QUALITY,
        description: "Detail level: stylised low-poly, balanced or detailed.",
        kind: "dropdown",
    },
    UiMeta {
        id: NATUREGEN_NAME,
        description: "Model name; used for the exported files and the group entity.",
        kind: "text_field",
    },
    UiMeta {
        id: NATUREGEN_EXPORT,
        description: "Writes one .obj per part and the .nature recipe into the project's nature folder.",
        kind: "button",
    },
    UiMeta {
        id: NATUREGEN_PLACE,
        description: "Creates the model in the scene as a group of entities, one per part, each with its own material.",
        kind: "button",
    },
    UiMeta {
        id: NATUREGEN_STATS,
        description: "Triangles, vertices and parts of the generated model.",
        kind: "text",
    },
    UiMeta {
        id: PANE_WATER_GENERATOR,
        description: "Water Generator: water bodies built with nodes (wave sources, modifiers, look) with a live animated preview and one-click placement as an engine water surface.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_WATER_GENERATOR,
        description: "Tab of the Water Generator pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: SHOW_WATER_GENERATOR,
        description: "Opens the Water Generator tab (lakes, rivers, seas and storms from nodes).",
        kind: "menu_item",
    },
    UiMeta {
        id: PANE_PHOTON,
        description: "Photon: the global illumination settings of the scene (switch, path, quality, bounce gain, voxel geometry, presets) and live statistics of the voxel far field.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_PHOTON,
        description: "Tab of the Photon pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: SHOW_PHOTON,
        description: "Opens the Photon tab (global illumination: settings, presets and voxel statistics).",
        kind: "menu_item",
    },
    UiMeta {
        id: PHOTON_ADD,
        description: "Adds the Photon GI component to a new entity when the scene has none (on, compute path, High quality).",
        kind: "button",
    },
    UiMeta {
        id: PHOTON_PRESET,
        description: "Photon presets: Balanced, Interior (strong bounce), Outdoor, Fast (Eco) and Diagnostic. A preset sets every Photon setting at once.",
        kind: "dropdown",
    },
    UiMeta {
        id: PHOTON_ENABLED,
        description: "Switches Photon global illumination on or off for the scene.",
        kind: "checkbox",
    },
    UiMeta {
        id: PHOTON_PATH,
        description: "Photon path: the screen fallback gather, or the compute cascades with the voxel far field.",
        kind: "dropdown",
    },
    UiMeta {
        id: PHOTON_QUALITY,
        description: "Photon quality tier: probe spacing and cascade count (Off, Low, Balanced, High, Eco).",
        kind: "dropdown",
    },
    UiMeta {
        id: PHOTON_INTENSITY,
        description: "Multiplier of the indirect light Photon adds to the scene.",
        kind: "slider",
    },
    UiMeta {
        id: PHOTON_BOUNCE_GAIN,
        description: "Scales the light the voxels hold (sun, lights, sky), so the bounce between surfaces grows without touching the direct light.",
        kind: "slider",
    },
    UiMeta {
        id: PHOTON_SKY,
        description: "Strength of the sky light a ray finds when it leaves the screen and the voxels.",
        kind: "slider",
    },
    UiMeta {
        id: PHOTON_AMBIENT,
        description: "Extra ambient bounce added to the sky fallback.",
        kind: "slider",
    },
    UiMeta {
        id: PHOTON_RADIUS,
        description: "Near-field reach of the screen cascades, in world units.",
        kind: "slider",
    },
    UiMeta {
        id: PHOTON_VOXEL_SKINNED,
        description: "Puts skinned meshes in the voxel volume in their current pose; they are revoxelised where they move.",
        kind: "checkbox",
    },
    UiMeta {
        id: PHOTON_VOXEL_CUTOUT,
        description: "Treats cut-out meshes (foliage cards) as solid in the voxel volume.",
        kind: "checkbox",
    },
    UiMeta {
        id: PHOTON_VOXEL_TERRAIN,
        description: "Puts terrain meshes in the voxel volume.",
        kind: "checkbox",
    },
    UiMeta {
        id: PHOTON_FULL_REBUILDS,
        description: "Diagnostic: rebuilds every voxel level whenever anything changes instead of only the voxels around what moved.",
        kind: "checkbox",
    },
    UiMeta {
        id: PHOTON_REBUILD,
        description: "Throws the voxel volume away; the next frame voxelises the whole scene again.",
        kind: "button",
    },
    UiMeta {
        id: PHOTON_DEBUG_VIEW,
        description: "Debug view: the lit scene, the GI alone, or the sky fallback alone.",
        kind: "dropdown",
    },
    UiMeta {
        id: PHOTON_STATS,
        description: "Voxel volume statistics: objects, triangles, regions and levels rebuilt, update time, and whether the scene is over the 1M-triangle budget.",
        kind: "text",
    },
    UiMeta {
        id: PANE_PROFILER,
        description: "Profiler: the engine's timed scopes (render stages and, with GPU timing, the GPU passes) as a tree with average, worst and call counts.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_PROFILER,
        description: "Tab of the Profiler pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: SHOW_PROFILER,
        description: "Opens the Profiler tab (timed scopes of the renderer, GPU timing).",
        kind: "menu_item",
    },
    UiMeta {
        id: PROFILER_GPU,
        description: "GPU timing: every GPU pass waits for the GPU at its edges so its time is the real cost. Frames get slower while it is on.",
        kind: "checkbox",
    },
    UiMeta {
        id: PROFILER_RESET,
        description: "Forgets every sample; the averages start again.",
        kind: "button",
    },
    UiMeta {
        id: PROFILER_FRAME,
        description: "Frame time of the renderer (average of the top scope) and the frame rate it implies.",
        kind: "text",
    },
    UiMeta {
        id: PROFILER_TABLE,
        description: "The profiler's scope tree: name, a bar, the average and worst time and how many times it ran.",
        kind: "list",
    },
    UiMeta {
        id: PROFILER_ROW,
        description: "A scope of the profiler's table; rows use a .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: PROFILER_CHART_FPS,
        description: "Frame rate over the last minute (the viewport's frames per second, sampled four times a second).",
        kind: "chart",
    },
    UiMeta {
        id: PROFILER_CHART_CPU,
        description: "CPU use of every logical core over the last minute, one colour per core, with the whole machine and this process below it.",
        kind: "chart",
    },
    UiMeta {
        id: PROFILER_CHART_GPU,
        description: "GPU busy share and video memory in use over the last minute (NVML or sysfs; empty where the platform cannot say).",
        kind: "chart",
    },
    UiMeta {
        id: PROFILER_CHART_MEMORY,
        description: "Memory in use on the machine and by this process over the last minute, in MiB.",
        kind: "chart",
    },
    UiMeta {
        id: PANE_ENVIRONMENT,
        description: "Environment: clouds, fog and rain of the scene, with presets (clear sky, cloudy, fog, light rain, storm) and the coupling that makes them agree with the sun and Photon's sky light.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_ENVIRONMENT,
        description: "Tab of the Environment pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: PANE_AUDIO,
        description: "The Audio tab: the scene's volumetric audio, its room and one row per sound.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_AUDIO,
        description: "Brings the Audio tab to the front.",
        kind: "pane_tab",
    },
    UiMeta {
        id: TRANSPORT_PLAY,
        description: "Play / Pause: starts the game's simulation (the scene as the compiled game runs it) and shows it in the Gameplay tab; while it plays, pauses it; while paused, resumes it.",
        kind: "button",
    },
    UiMeta {
        id: TRANSPORT_STOP,
        description: "Stop: ends the game and puts the scene back as it was before Play.",
        kind: "button",
    },
    UiMeta {
        id: PANE_GAMEPLAY,
        description: "Gameplay preview: the scene through its active camera, running the game's simulation while Play is on.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_GAMEPLAY,
        description: "Tab of the Gameplay preview.",
        kind: "tab",
    },
    UiMeta {
        id: SHOW_GAMEPLAY,
        description: "Brings the Gameplay preview to the front.",
        kind: "menu_item",
    },
    UiMeta {
        id: GAMEPLAY_VIEW,
        description: "The Gameplay picture; while the game plays the keyboard and the pointer reach its scripts through it.",
        kind: "image",
    },
    UiMeta {
        id: PANE_CAMERA_PREVIEW,
        description: "Camera preview: the scene through the selected Camera 3D (or the active one), without playing.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_CAMERA_PREVIEW,
        description: "Tab of the Camera preview.",
        kind: "tab",
    },
    UiMeta {
        id: SHOW_CAMERA_PREVIEW,
        description: "Brings the Camera preview to the front.",
        kind: "menu_item",
    },
    UiMeta {
        id: CAMERA_PREVIEW_VIEW,
        description: "The Camera preview picture.",
        kind: "image",
    },
    UiMeta {
        id: PERF_ENABLED,
        description: "Performance: Render features on. Switch of the Forward Render Features component.",
        kind: "checkbox",
    },
    UiMeta {
        id: PERF_INSTANCING,
        description: "Performance: Instancing: copies drawn together. Switch of the Forward Render Features component.",
        kind: "checkbox",
    },
    UiMeta {
        id: PERF_OCCLUSION,
        description: "Performance: Occlusion culling: skip what a big object hides. Switch of the Forward Render Features component.",
        kind: "checkbox",
    },
    UiMeta {
        id: PERF_REFLECTIONS,
        description: "Performance: Reflections (probes and planes). Switch of the Forward Render Features component.",
        kind: "checkbox",
    },
    UiMeta {
        id: PERF_SCREENSPACEREFLECTIONS,
        description: "Performance: Screen-space reflections. Switch of the Forward Render Features component.",
        kind: "checkbox",
    },
    UiMeta {
        id: PERF_REFLECTIONSWHENSETTLED,
        description: "Performance: Reflections only while the view is still. Switch of the Forward Render Features component.",
        kind: "checkbox",
    },
    UiMeta {
        id: PERF_VOLUMETRICS,
        description: "Performance: Volumetric light. Switch of the Forward Render Features component.",
        kind: "checkbox",
    },
    UiMeta {
        id: PERF_IBL,
        description: "Performance: Sky as diffuse fill light. Switch of the Forward Render Features component.",
        kind: "checkbox",
    },
    UiMeta {
        id: PERF_INSTANCINGBATCH,
        description: "Performance: Instances per batch. Number of the Forward Render Features component.",
        kind: "slider",
    },
    UiMeta {
        id: PERF_OCCLUDERS,
        description: "Performance: Occluders tested. Number of the Forward Render Features component.",
        kind: "slider",
    },
    UiMeta {
        id: PERF_OCCLUDERRADIUS,
        description: "Performance: Smallest occluder (m). Number of the Forward Render Features component.",
        kind: "slider",
    },
    UiMeta {
        id: PERF_REFLECTIONSTRENGTH,
        description: "Performance: Reflection strength. Number of the Forward Render Features component.",
        kind: "slider",
    },
    UiMeta {
        id: PERF_REFLECTIONSCALE,
        description: "Performance: Planar reflection resolution. Number of the Forward Render Features component.",
        kind: "slider",
    },
    UiMeta {
        id: PERF_PROBERESOLUTION,
        description: "Performance: Probe resolution (px). Number of the Forward Render Features component.",
        kind: "slider",
    },
    UiMeta {
        id: PERF_MAXPROBES,
        description: "Performance: Reflection probes. Number of the Forward Render Features component.",
        kind: "slider",
    },
    UiMeta {
        id: PERF_SSRSTEPS,
        description: "Performance: SSR march steps. Number of the Forward Render Features component.",
        kind: "slider",
    },
    UiMeta {
        id: PERF_FOGDENSITY,
        description: "Performance: Volumetric density. Number of the Forward Render Features component.",
        kind: "slider",
    },
    UiMeta {
        id: PERF_ADD,
        description: "Adds the Forward Render Features component on a new entity, so the scene's render options can be changed.",
        kind: "button",
    },
    UiMeta {
        id: PERF_RENDER_SCALE,
        description: "Internal render scale of the editor viewport: below 1 is lighter, above 1 supersamples.",
        kind: "dropdown",
    },
    UiMeta {
        id: PERF_ANTI_ALIASING,
        description: "Anti-aliasing of the editor viewport: off, FXAA or temporal.",
        kind: "dropdown",
    },
    UiMeta {
        id: PERF_SHADOWS,
        description: "Draws the lights' shadow maps in the editor viewport.",
        kind: "checkbox",
    },
    UiMeta {
        id: PERF_GRID,
        description: "Shows the ground grid in the editor viewport.",
        kind: "checkbox",
    },
    UiMeta {
        id: PANE_PERFORMANCE,
        description: "Performance: instancing, occlusion culling, reflections and volumetric light of the scene, and the editor viewport's render scale, anti-aliasing, shadows and grid.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_PERFORMANCE,
        description: "Tab of the Performance panel.",
        kind: "tab",
    },
    UiMeta {
        id: SHOW_PERFORMANCE,
        description: "Brings the Performance tab to the front.",
        kind: "menu_item",
    },
    UiMeta {
        id: SHOW_AUDIO,
        description: "Shows the Audio tab (volumetric audio).",
        kind: "menu_item",
    },
    UiMeta {
        id: PANE_PHASESHIFT,
        description: "The PhaseShift tab: the selected entity's animator parameters, states and transitions.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_PHASESHIFT,
        description: "Brings the PhaseShift tab to the front.",
        kind: "pane_tab",
    },
    UiMeta {
        id: SHOW_PHASESHIFT,
        description: "Shows the PhaseShift tab (animator state machine).",
        kind: "menu_item",
    },
    UiMeta {
        id: PANE_RIG2D,
        description: "The Rig 2D tab: the selected entity's 2D bones, skinned mesh and rig parameters.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_RIG2D,
        description: "Brings the Rig 2D tab to the front.",
        kind: "pane_tab",
    },
    UiMeta {
        id: SHOW_RIG2D,
        description: "Shows the Rig 2D tab (bones and parameters).",
        kind: "menu_item",
    },
    UiMeta {
        id: AUDIO_ADD,
        description: "Adds the Volumetric Audio component on a new entity.",
        kind: "button",
    },
    UiMeta {
        id: AUDIO_ENABLED,
        description: "Volumetric audio on or off for the scene.",
        kind: "checkbox",
    },
    UiMeta {
        id: AUDIO_OUTPUT,
        description: "Plays the mixer on this computer's sound device; off, it mixes silently so the live numbers still move.",
        kind: "checkbox",
    },
    UiMeta {
        id: AUDIO_DEVICE,
        description: "The sound device the speakers switch plays on: the system default or one of the computer's outputs.",
        kind: "dropdown",
    },
    UiMeta {
        id: AUDIO_MASTER,
        description: "Master volume of the volumetric audio.",
        kind: "slider",
    },
    UiMeta {
        id: AUDIO_WORLD,
        description: "Volume of the World layer: localised, spatial sounds.",
        kind: "slider",
    },
    UiMeta {
        id: AUDIO_AMBIENT,
        description: "Volume of the Ambient layer: rain, wind, anything that covers the scene.",
        kind: "slider",
    },
    UiMeta {
        id: AUDIO_MUSIC,
        description: "Volume of the Music layer.",
        kind: "slider",
    },
    UiMeta {
        id: AUDIO_PROPAGATION_DELAY,
        description: "Sound takes distance / speed of sound to arrive.",
        kind: "checkbox",
    },
    UiMeta {
        id: AUDIO_DOPPLER,
        description: "How much a moving source is Doppler shifted: 0 none, 1 real.",
        kind: "slider",
    },
    UiMeta {
        id: AUDIO_SPEED_OF_SOUND,
        description: "Speed of sound in metres per second.",
        kind: "slider",
    },
    UiMeta {
        id: AUDIO_AIR_ABSORPTION,
        description: "The air takes the highs: far sound is dull, near sound is clear.",
        kind: "checkbox",
    },
    UiMeta {
        id: AUDIO_OCCLUSION,
        description: "Walls between the source and the listener muffle it: the lows pass, the highs do not.",
        kind: "checkbox",
    },
    UiMeta {
        id: AUDIO_DIFFRACTION,
        description: "Sound bends round the end of a wall or through a doorway and seems to come from there.",
        kind: "checkbox",
    },
    UiMeta {
        id: AUDIO_REVERB,
        description: "The room round the listener sets a reverb tail, and far hard surfaces send echoes.",
        kind: "checkbox",
    },
    UiMeta {
        id: AUDIO_ADD_SOURCE,
        description: "Adds an entity with an Audio Emitter; pick its clip in the Inspector.",
        kind: "button",
    },
    UiMeta {
        id: AUDIO_ADD_WALL,
        description: "Adds a 6 x 3 x 0.3 m wall with an Acoustic Obstacle (concrete).",
        kind: "button",
    },
    UiMeta {
        id: SHOW_ENVIRONMENT,
        description: "Opens the Environment tab (clouds, fog, rain, presets).",
        kind: "menu_item",
    },
    UiMeta {
        id: ENV_ADD,
        description: "Adds the Weather and Volumetric Clouds components on a new entity when the scene has none, starting from the Clear sky preset.",
        kind: "button",
    },
    UiMeta {
        id: ENV_PRESET,
        description: "Applies a weather preset: Off, Clear sky, Cloudy, Fog, Light rain or Storm. Sets clouds, fog and rain together.",
        kind: "dropdown",
    },
    UiMeta {
        id: ENV_CLOUDS_ENABLED,
        description: "Draws procedural volumetric clouds in the sky.",
        kind: "checkbox",
    },
    UiMeta {
        id: ENV_CLOUDS_QUALITY,
        description: "Cloud ray-march quality: Off, Low (8 steps), Medium (16) or High (24).",
        kind: "dropdown",
    },
    UiMeta {
        id: ENV_CLOUDS_DIM_SUN,
        description: "The clouds dim the sun by their cover (the direct light, Photon's bounce and the fog glow follow).",
        kind: "checkbox",
    },
    UiMeta {
        id: ENV_CLOUDS_COVERAGE,
        description: "Sky coverage, 0 (clear) to 1 (the sky is full).",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_DENSITY,
        description: "Extinction of the cloud medium: thin and wispy to thick and dark.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_BASE_ALTITUDE,
        description: "Height of the bottom of the cloud layer, in metres.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_TOP_ALTITUDE,
        description: "Height of the top of the cloud layer, in metres.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_DETAIL,
        description: "Weight of the high-frequency detail octaves.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_WIND_SPEED,
        description: "How fast the clouds drift, in metres per second.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_WIND_DIRECTION,
        description: "Direction the clouds drift, in degrees around the vertical axis.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_SUN_SCATTER,
        description: "How strongly the sun lights the clouds.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_DARKNESS,
        description: "Darkens the clouds towards a storm, 0 to 1.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_SKY_LIGHT,
        description: "Sky light that fills the shadowed side of the clouds.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_MIST_DENSITY,
        description: "Ground mist at the horizon, 0 to 1 (also thickens the volumetric fog of the forward pass).",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_MIST_HEIGHT,
        description: "Height the ground mist fades out over, in metres.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_TINT_R,
        description: "Red of the tint on the sun-lit side of the clouds.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_TINT_G,
        description: "Green of the tint on the sun-lit side of the clouds.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_TINT_B,
        description: "Blue of the tint on the sun-lit side of the clouds.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_WEATHER_ENABLED,
        description: "Master switch of the Weather component (fog, rain and the coupling).",
        kind: "checkbox",
    },
    UiMeta {
        id: ENV_WEATHER_LINK_SKY,
        description: "Couples the sections: rain thickens the clouds and the fog, an overcast sky dims the sun and greys Photon's sky light, fog tints it.",
        kind: "checkbox",
    },
    UiMeta {
        id: ENV_FOG_ENABLED,
        description: "Exponential height fog, integrated along each view ray (the sky fades into it at the horizon).",
        kind: "checkbox",
    },
    UiMeta {
        id: ENV_FOG_DENSITY,
        description: "Fog density per metre at the base height.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_FOG_HEIGHT_FALLOFF,
        description: "How fast the fog thins with height; 0 is the same density everywhere.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_FOG_BASE_HEIGHT,
        description: "World height where the fog has the density above.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_FOG_START,
        description: "Distance from the camera before the fog begins, in metres.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_FOG_MAX_OPACITY,
        description: "The most the fog can hide, 0 to 1.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_FOG_SKY_BLEND,
        description: "0 uses the fog colour, 1 uses the sky's colour at the horizon in the direction looked at.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_FOG_SUN_SCATTER,
        description: "Glow of the fog towards the sun.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_FOG_SUN_EXPONENT,
        description: "How tight the glow towards the sun is.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_FOG_COLOR_R,
        description: "Red of the fog colour.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_FOG_COLOR_G,
        description: "Green of the fog colour.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_FOG_COLOR_B,
        description: "Blue of the fog colour.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_RAIN_ENABLED,
        description: "Falling rain: layered streaks hidden behind nearer geometry, and wet surfaces.",
        kind: "checkbox",
    },
    UiMeta {
        id: ENV_RAIN_INTENSITY,
        description: "How hard it rains, 0 to 1.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_RAIN_WIND_SPEED,
        description: "Sideways wind that leans the streaks, in metres per second.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_RAIN_WIND_DIRECTION,
        description: "Direction of that wind, in degrees around the vertical axis.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_RAIN_DROP_LENGTH,
        description: "Length of a streak, 0 to 1.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_RAIN_SPEED,
        description: "Animation speed of the falling rain.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_RAIN_BRIGHTNESS,
        description: "How bright a streak is.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_RAIN_WETNESS,
        description: "How wet surfaces get at full intensity: darker and glossier.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_GROUND_SHADOWS,
        description: "The clouds cast moving shadows on the ground and on everything lit by the sun (the sun is traced through the layer; nothing is baked).",
        kind: "checkbox",
    },
    UiMeta {
        id: ENV_CLOUDS_SHADOW_STRENGTH,
        description: "How dark a cloud's shadow is, 0 (none) to 1 (as dark as the cloud is thick).",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_SILVER_LINING,
        description: "A bright rim on the edge of the clouds that sit in front of the sun.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_CLOUDS_SUN_SHAFTS,
        description: "Light shafts: rays that stream through the gaps of the clouds when the sun is in view.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_AUDIO_ENABLED,
        description: "Plays the weather's sound: a rain bed, a wind bed that follows the gusts, and a clap of thunder for every strike, panned to where it struck. Made from noise, no files.",
        kind: "checkbox",
    },
    UiMeta {
        id: ENV_AUDIO_VOLUME,
        description: "The master volume of the weather's sound, 0 to 1.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_RAIN_SPLASHES,
        description: "Rings and splashes where the rain meets the ground near the camera.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_WEATHER_WIND_GUST,
        description: "How much the wind surges: the rain's slant, the foliage and the clouds share the gusts.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_WEATHER_WIND_FOLIAGE,
        description: "How strongly foliage materials sway with the wind (0 leaves foliage alone).",
        kind: "slider",
    },
    UiMeta {
        id: ENV_LIGHTNING_ENABLED,
        description: "Lightning: a flash that lights the clouds, the scene and Photon's bounce, with a bolt in the sky. Needs an overcast sky when linked.",
        kind: "checkbox",
    },
    UiMeta {
        id: ENV_LIGHTNING_THUNDER,
        description: "Each strike raises a thunder event that arrives after the sound's travel time (the Console logs it; scripts can play it).",
        kind: "checkbox",
    },
    UiMeta {
        id: ENV_LIGHTNING_RATE,
        description: "Strikes per minute.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_LIGHTNING_INTENSITY,
        description: "The brightness of the flash, 0 to 2.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_LIGHTNING_DISTANCE_MIN,
        description: "The closest a strike lands, in metres.",
        kind: "slider",
    },
    UiMeta {
        id: ENV_LIGHTNING_DISTANCE_MAX,
        description: "The farthest a strike lands, in metres.",
        kind: "slider",
    },
    UiMeta {
        id: WATERGEN_CANVAS,
        description: "Water Generator node canvas: wire wave sources and modifiers into the Output, and a Water Look beside them.",
        kind: "node_canvas",
    },
    UiMeta {
        id: WATERGEN_UNDO,
        description: "Undoes the last water graph edit.",
        kind: "button",
    },
    UiMeta {
        id: WATERGEN_REDO,
        description: "Redoes the last undone water graph edit.",
        kind: "button",
    },
    UiMeta {
        id: WATERGEN_FIT,
        description: "Frames the whole water graph in the canvas.",
        kind: "button",
    },
    UiMeta {
        id: WATERGEN_NEW,
        description: "Starts an empty water graph with an Output node.",
        kind: "button",
    },
    UiMeta {
        id: WATERGEN_SEARCH,
        description: "Filters the water node palette.",
        kind: "text_field",
    },
    UiMeta {
        id: WATERGEN_SNAP,
        description: "Snaps nodes to a 16 px grid while dragging.",
        kind: "checkbox",
    },
    UiMeta {
        id: WATERGEN_DELETE_NODE,
        description: "Deletes the selected node (the Output node stays).",
        kind: "button",
    },
    UiMeta {
        id: WATERGEN_PREVIEW,
        description: "Top-down animated preview of the water surface, computed from the same waves the engine animates.",
        kind: "image",
    },
    UiMeta {
        id: WATERGEN_PLAY,
        description: "Animates the preview: waves travel using the engine's dispersion relation.",
        kind: "checkbox",
    },
    UiMeta {
        id: WATERGEN_PRESET,
        description: "Starts from a ready-made lake, river, sea, storm, pool or swamp.",
        kind: "dropdown",
    },
    UiMeta {
        id: WATERGEN_NAME,
        description: "Water name; used for the saved .water file and the entity.",
        kind: "text_field",
    },
    UiMeta {
        id: WATERGEN_EXPORT,
        description: "Writes the .water graph into the project's water folder.",
        kind: "button",
    },
    UiMeta {
        id: WATERGEN_PLACE,
        description: "Creates the water body in the scene as an animated engine water surface.",
        kind: "button",
    },
    UiMeta {
        id: WATERGEN_STATS,
        description: "Waves, size and depth of the evaluated water.",
        kind: "text",
    },
    UiMeta {
        id: PANE_WORLD,
        description: "World Composer: a world made of recipes (terrain, water, materials and classes of trees, rocks and plants), built deterministically by the engine into the scene or an instance bake.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_WORLD,
        description: "Tab of the World Composer pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: SHOW_WORLD,
        description: "Opens the World Composer tab (compose a world from terrain, water and nature recipes).",
        kind: "menu_item",
    },
    UiMeta {
        id: WORLD_NAME,
        description: "Name of the world: the recipe is saved as World/<Name>.generator.world and the build as the World <Name> entity.",
        kind: "text_field",
    },
    UiMeta {
        id: WORLD_UNDO,
        description: "Undoes the last composition edit.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_REDO,
        description: "Redoes the composition edit that was undone.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_NEW,
        description: "Starts an empty composition (on the terrain of the current one).",
        kind: "button",
    },
    UiMeta {
        id: WORLD_SAVE,
        description: "Saves the composition as World/<Name>.generator.world.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_PREVIEW,
        description: "Runs the coarse preview build off the UI thread (at most 2000 instances) and shows the count per class.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_SAVE_PREFAB,
        description: "Saves the built World <Name> entity tree as World/<Name>.prefab, linked to its .generator.world recipe.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_BUILD,
        description: "Builds the world: resolves every recipe, bakes the terrain, places the classes and writes the scene (entities for a small world, a .cinst instance bake for a large one). One undo step.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_LIBRARY_SEARCH,
        description: "Filters the Library's recipes by path.",
        kind: "text_field",
    },
    UiMeta {
        id: WORLD_TERRAIN,
        description: "The terrain recipe the world grows on.",
        kind: "text",
    },
    UiMeta {
        id: WORLD_TERRAIN_SIZE_X,
        description: "Width of the terrain in metres (0: the terrain recipe's own size).",
        kind: "slider",
    },
    UiMeta {
        id: WORLD_TERRAIN_SIZE_Z,
        description: "Depth of the terrain in metres (0: the terrain recipe's own size).",
        kind: "slider",
    },
    UiMeta {
        id: WORLD_PREVIEW_NOTE,
        description: "What the coarse preview counted, per class.",
        kind: "text",
    },
    UiMeta {
        id: WORLD_SEED,
        description: "The world seed: the same seed, composition and terrain always give the same world.",
        kind: "text_field",
    },
    UiMeta {
        id: WORLD_RESEED,
        description: "Derives a new seed from the current one (undoable).",
        kind: "button",
    },
    UiMeta {
        id: WORLD_UNIQUE,
        description: "Bakes this class per instance (a hero object) instead of picking from its variant pool.",
        kind: "checkbox",
    },
    UiMeta {
        id: WORLD_ADD_TERM,
        description: "Adds a where term (a trapezoid over a mask) to the selected class.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_ADD_ADAPT,
        description: "Adds an adapt line: the ground context at an instance drives its size, lean, tint, wetness or snow.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_ADD_PIN,
        description: "Adds an override pin row (keep one variant at a cell) for the selected class.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_ADD_MOVE,
        description: "Adds an override move row (move the instance of a cell) for the selected class.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_REMOVE_IN_REGION,
        description: "Removes every instance of the last build inside the region (override remove lines, undoable) and rebuilds.",
        kind: "button",
    },
    UiMeta {
        id: WORLD_STATUS,
        description: "Status of the composition: the engine's problems, or what the last build or save did.",
        kind: "status",
    },
    UiMeta {
        id: UiId::fixed("world.library.item"),
        description: "A recipe of the World tab's Library; click to add it to the composition. Rows use the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("world.composition.class_row"),
        description: "A class of the composition (ref, variants, priority, density hint, preview count); click to edit its rules. Rows use the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("world.composition.class_remove"),
        description: "Removes a class from the composition; uses the .N suffix.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("world.composition.water_row"),
        description: "A water body of the composition (river or lake); uses the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("world.composition.water_remove"),
        description: "Removes a water body; uses the .N suffix.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("world.composition.water"),
        description: "Sliders of a water body (.N.min, .N.level, .N.carve).",
        kind: "slider",
    },
    UiMeta {
        id: UiId::fixed("world.composition.material_slot"),
        description: "The slot a material binding fills (terrain.layer0, ...); uses the .N suffix.",
        kind: "text_field",
    },
    UiMeta {
        id: UiId::fixed("world.composition.material_remove"),
        description: "Removes a material binding; uses the .N suffix.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("world.rules.class"),
        description: "Sliders of the selected class (.variants, .priority, .cell, .radius, .density, .scale_min, .scale_max, .tilt, .sink, .tint, .base).",
        kind: "slider",
    },
    UiMeta {
        id: UiId::fixed("world.rules.term"),
        description: "Controls of a where term (.N.mask, .N.rise0, .N.rise1, .N.fall1, .N.fall0, .N.remove).",
        kind: "slider",
    },
    UiMeta {
        id: UiId::fixed("world.rules.adapt"),
        description: "Controls of an adapt line (.N.target, .N.mask, .N.amount, .N.remove).",
        kind: "dropdown",
    },
    UiMeta {
        id: UiId::fixed("world.rules.region"),
        description: "The edit region in metres from the terrain's corner (.0 x0, .1 z0, .2 x1, .3 z1).",
        kind: "slider",
    },
    UiMeta {
        id: UiId::fixed("world.rules.override"),
        description: "Controls of an override row (.N.row, .N.remove, .N.cell_x, .N.cell_z, .N.x, .N.z, .N.variant).",
        kind: "list_row",
    },
    UiMeta {
        id: PANE_TERRAIN_GENERATOR,
        description: "Terrain Generator: heightfields built with nodes (noise, mountains, erosion, masks) with a live 3D preview and placement as an engine terrain with splat layers.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_TERRAIN_GENERATOR,
        description: "Tab of the Terrain Generator pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: SHOW_TERRAIN_GENERATOR,
        description: "Opens the Terrain Generator tab (mountains, islands, canyons from nodes).",
        kind: "menu_item",
    },
    UiMeta {
        id: TERRAINGEN_CANVAS,
        description: "Terrain Generator node canvas: wire sources, modifiers, masks and erosion into the Output.",
        kind: "node_canvas",
    },
    UiMeta {
        id: TERRAINGEN_UNDO,
        description: "Undoes the last terrain graph edit.",
        kind: "button",
    },
    UiMeta {
        id: TERRAINGEN_REDO,
        description: "Redoes the last undone terrain graph edit.",
        kind: "button",
    },
    UiMeta {
        id: TERRAINGEN_FIT,
        description: "Frames the whole terrain graph in the canvas.",
        kind: "button",
    },
    UiMeta {
        id: TERRAINGEN_NEW,
        description: "Starts an empty terrain graph with an Output node.",
        kind: "button",
    },
    UiMeta {
        id: TERRAINGEN_SEARCH,
        description: "Filters the terrain node palette.",
        kind: "text_field",
    },
    UiMeta {
        id: TERRAINGEN_SNAP,
        description: "Snaps nodes to a 16 px grid while dragging.",
        kind: "checkbox",
    },
    UiMeta {
        id: TERRAINGEN_DELETE_NODE,
        description: "Deletes the selected node (the Output node stays).",
        kind: "button",
    },
    UiMeta {
        id: TERRAINGEN_PREVIEW,
        description: "Software 3D preview of the terrain with its splat layers and sea level. Drag to orbit, scroll to zoom.",
        kind: "image",
    },
    UiMeta {
        id: TERRAINGEN_PRESET,
        description: "Starts from a ready-made hills, range, island, badlands, archipelago or highland lake.",
        kind: "dropdown",
    },
    UiMeta {
        id: TERRAINGEN_NAME,
        description: "Terrain name; used for the saved graph file and the entity.",
        kind: "text_field",
    },
    UiMeta {
        id: TERRAINGEN_EXPORT,
        description: "Writes the terrain graph and a 16-bit height map PNG into the project's terrain folder.",
        kind: "button",
    },
    UiMeta {
        id: TERRAINGEN_PLACE,
        description: "Creates the terrain in the scene as an engine terrain with ground, rock, dirt and mud splat layers.",
        kind: "button",
    },
    UiMeta {
        id: TERRAINGEN_STATS,
        description: "Size, height range and resolution of the evaluated terrain.",
        kind: "text",
    },
    UiMeta {
        id: VIEWPORT_MARKERS,
        description: "Scene markers over the viewport: light rays, bulbs and cones, camera frustums, colliders and locators. Click one to select its entity.",
        kind: "canvas",
    },
    UiMeta {
        id: VIEWPORT_BRUSH_CURSOR,
        description: "The terrain brush's ring on the ground under the mouse: the radius, where the stroke is at full strength, and the centre. It only draws; clicks pass through to the viewport.",
        kind: "canvas",
    },
    UiMeta {
        id: TOGGLE_SNAP,
        description: "Snaps gizmo edits to a grid: 0.25 m for Move, 15 degrees for Rotate, 0.1 for Scale.",
        kind: "menu_item",
    },
    UiMeta {
        id: TOGGLE_MARKERS,
        description: "Shows or hides the scene markers (lights, cameras, colliders, empties).",
        kind: "menu_item",
    },
    UiMeta {
        id: SHOW_TEXTURE_GENERATOR,
        description: "Opens the Texture Generator tab (graphs that bake one texture with alpha).",
        kind: "menu_item",
    },
    UiMeta {
        id: PANE_TEXTURE_GENERATOR,
        description: "Texture Generator: node graph that bakes one texture (leaf, flame, packed mask) with alpha.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TAB_TEXTURE_GENERATOR,
        description: "Tab of the Texture Generator pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: TEXTUREGEN_CANVAS,
        description: "Texture Generator node canvas: pan, zoom, drag nodes and connect ports.",
        kind: "node_canvas",
    },
    UiMeta {
        id: TEXTUREGEN_NEW,
        description: "Starts a new texture graph (a leaf feeding a Texture Output).",
        kind: "button",
    },
    UiMeta {
        id: TEXTUREGEN_LIBRARY,
        description: "Replaces the graph with a ready-made texture: foliage, feathers, fire, smoke, effects, data.",
        kind: "dropdown",
    },
    UiMeta {
        id: TEXTUREGEN_SIZE,
        description: "Texture resolution in pixels (64 to 2048).",
        kind: "dropdown",
    },
    UiMeta {
        id: TEXTUREGEN_UNDO,
        description: "Undoes the last graph edit.",
        kind: "button",
    },
    UiMeta {
        id: TEXTUREGEN_REDO,
        description: "Redoes the last undone graph edit.",
        kind: "button",
    },
    UiMeta {
        id: TEXTUREGEN_RESEED,
        description: "Gives every seeded node a new seed: a variation of the same texture graph.",
        kind: "button",
    },
    UiMeta {
        id: TEXTUREGEN_FIT,
        description: "Frames the whole graph in the canvas.",
        kind: "button",
    },
    UiMeta {
        id: TEXTUREGEN_SAVE,
        description: "Saves the graph as a .texgraph file in the project's textures folder.",
        kind: "button",
    },
    UiMeta {
        id: TEXTUREGEN_EXPORT,
        description: "Bakes and writes the texture as a single PNG next to the graph.",
        kind: "button",
    },
    UiMeta {
        id: TEXTUREGEN_SEARCH,
        description: "Filters the texture node palette.",
        kind: "text_field",
    },
    UiMeta {
        id: TEXTUREGEN_SNAP,
        description: "Snaps node positions to a 16 px grid.",
        kind: "checkbox",
    },
    UiMeta {
        id: TEXTUREGEN_NAME,
        description: "Name of the texture; used for the .texgraph and .png file names.",
        kind: "text_field",
    },
    UiMeta {
        id: TEXTUREGEN_PREVIEW,
        description: "Texture preview over a checkerboard, so transparency is visible.",
        kind: "image",
    },
    UiMeta {
        id: TEXTUREGEN_BITS,
        description: "PNG bit depth: 8 or 16 bits per channel.",
        kind: "dropdown",
    },
    UiMeta {
        id: TEXTUREGEN_FLIP_COLS,
        description: "Flipbook columns.",
        kind: "dropdown",
    },
    UiMeta {
        id: TEXTUREGEN_FLIP_ROWS,
        description: "Flipbook rows.",
        kind: "dropdown",
    },
    UiMeta {
        id: TEXTUREGEN_EXPORT_FLIPBOOK,
        description: "Bakes columns x rows frames, advancing every Phase parameter, into one sheet.",
        kind: "button",
    },
    UiMeta {
        id: LAYOUT_DEFAULT,
        description: "Restores the default panel layout.",
        kind: "menu_item",
    },
    UiMeta {
        id: LAYOUT_VIEWPORT,
        description: "Gives the viewport most of the workspace.",
        kind: "menu_item",
    },
    UiMeta {
        id: MENU_PROJECT,
        description: "Project menu: project manager and asset refresh.",
        kind: "menu",
    },
    UiMeta {
        id: PROJECT_MANAGER_MENU,
        description: "Opens the Project Manager dialog.",
        kind: "menu_item",
    },
    UiMeta {
        id: PROJECT_REFRESH_ASSETS,
        description: "Rescans the project asset root.",
        kind: "menu_item",
    },
    UiMeta {
        id: SCRIPT_EDITOR_MENU,
        description: "Opens the Script Editor pane.",
        kind: "menu_item",
    },
    UiMeta {
        id: DRYAD_NODES_MENU,
        description: "Opens the Dryad Nodes pane.",
        kind: "menu_item",
    },
    UiMeta {
        id: SCATTER_TERRAIN_MENU,
        description: "Scatters the project's plan (Scatter/default.scatter: pines, boulders, grass by slope, wetness and height) on the selected terrain, as one undo step.",
        kind: "menu_item",
    },
    UiMeta {
        id: CONVERT_TERRAINS_MENU,
        description: "Saves every terrain still in the old .cterrain format as a native .terrain next to it (the old file stays; the engine checks the new one bit for bit).",
        kind: "menu_item",
    },
    UiMeta {
        id: MENU_SCENE,
        description: "Scene menu: add, save and play.",
        kind: "menu",
    },
    UiMeta {
        id: MENU_EDIT,
        description: "Edit menu: undo, redo and delete.",
        kind: "menu",
    },
    UiMeta {
        id: MENU_VIEW,
        description: "View menu: focus, camera reset and render.",
        kind: "menu",
    },
    UiMeta {
        id: UNDO,
        description: "Reverts the last editor step.",
        kind: "button",
    },
    UiMeta {
        id: REDO,
        description: "Reapplies the last undone editor step.",
        kind: "button",
    },
    UiMeta {
        id: ADD_ENTITY,
        description: "Adds an entity to the current scene.",
        kind: "button",
    },
    UiMeta {
        id: TOGGLE_PLAY,
        description: "Toggles play/pause and ticks the engine.",
        kind: "button",
    },
    UiMeta {
        id: SAVE_SCENE,
        description: "Saves the current .caf scene.",
        kind: "button",
    },
    UiMeta {
        id: RENDER_VIEWPORT,
        description: "Forces an offscreen viewport render.",
        kind: "button",
    },
    UiMeta {
        id: TOGGLE_GRID,
        description: "Shows or hides the reference ground grid.",
        kind: "menu_item",
    },
    UiMeta {
        id: GIZMO_MOVE,
        description: "Switches the viewport gizmo to move mode.",
        kind: "menu_item",
    },
    UiMeta {
        id: GIZMO_ROTATE,
        description: "Switches the viewport gizmo to rotate mode.",
        kind: "menu_item",
    },
    UiMeta {
        id: GIZMO_SCALE,
        description: "Switches the viewport gizmo to scale mode.",
        kind: "menu_item",
    },
    UiMeta {
        id: PANE_HIERARCHY,
        description: "Scene hierarchy: select, add and delete entities.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_ASSETS,
        description: "Project assets grouped by file kind.",
        kind: "pane",
    },
    UiMeta {
        id: ASSET_SEARCH,
        description: "Filters the asset browser by file name or path.",
        kind: "text_input",
    },
    UiMeta {
        id: ASSET_UP,
        description: "Moves to the parent folder in the asset browser.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_ROOT,
        description: "Returns the asset browser to the asset root.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_REFRESH,
        description: "Rescans the project asset root.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_VIEW_GRID,
        description: "Shows assets as thumbnail cards.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_VIEW_LIST,
        description: "Shows assets as compact rows.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_EMPTY,
        description: "Shown when a folder or search has no assets.",
        kind: "label",
    },
    UiMeta {
        id: ASSET_NEW,
        description: "Opens the asset wizard on the Create tab (folder, material, script…).",
        kind: "button",
    },
    UiMeta {
        id: ASSET_IMPORT,
        description: "Opens the asset wizard on the Import tab.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_NEW_FOLDER,
        description: "Starts a new folder in the folder being browsed.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_RENAME,
        description: "Renames the selected asset or folder.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_DELETE,
        description: "Asks to move the selected asset or folder to the project trash.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_DELETE_CONFIRM,
        description: "Confirms moving the selected asset to the trash.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_EDIT_NAME,
        description: "Name of the folder being created or the asset being renamed.",
        kind: "text_input",
    },
    UiMeta {
        id: ASSET_EDIT_OK,
        description: "Applies the new folder name or the rename.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_EDIT_CANCEL,
        description: "Cancels the folder, rename or delete action.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_TREE_TOGGLE,
        description: "Shows or hides the folder tree beside the file view.",
        kind: "chip",
    },
    UiMeta {
        id: ASSET_TREE,
        description: "Folder tree of the project asset root.",
        kind: "list",
    },
    UiMeta {
        id: WIZARD,
        description: "Asset wizard: create, import and pack project assets.",
        kind: "dialog",
    },
    UiMeta {
        id: WIZARD_CLOSE,
        description: "Closes the asset wizard.",
        kind: "button",
    },
    UiMeta {
        id: WIZARD_TAB_CREATE,
        description: "Wizard tab: create a new asset from a template.",
        kind: "chip",
    },
    UiMeta {
        id: WIZARD_TAB_IMPORT,
        description: "Wizard tab: copy files into the project assets.",
        kind: "chip",
    },
    UiMeta {
        id: WIZARD_TAB_PACK,
        description: "Wizard tab: pack raw assets into a .cap with caf-pack.",
        kind: "chip",
    },
    UiMeta {
        id: WIZARD_NAME,
        description: "Name of the asset to create.",
        kind: "text_input",
    },
    UiMeta {
        id: WIZARD_CREATE,
        description: "Creates the asset in the current folder.",
        kind: "button",
    },
    UiMeta {
        id: WIZARD_IMPORT_PATH,
        description: "Path of a file or folder to import.",
        kind: "text_input",
    },
    UiMeta {
        id: WIZARD_IMPORT_ADD,
        description: "Queues the typed path for import.",
        kind: "button",
    },
    UiMeta {
        id: WIZARD_IMPORT_RUN,
        description: "Copies the queued files into the current folder.",
        kind: "button",
    },
    UiMeta {
        id: WIZARD_PACK_RUN,
        description: "Runs caf-pack over the asset root to build a .cap.",
        kind: "button",
    },
    UiMeta {
        id: WIZARD_PACK_IDS,
        description: "Also generate the asset id header.",
        kind: "checkbox",
    },
    UiMeta {
        id: PROJECT_MANAGER,
        description: "Create, reopen or browse Caffeine projects.",
        kind: "dialog",
    },
    UiMeta {
        id: PROJECT_CLOSE,
        description: "Closes the project manager without changing the project.",
        kind: "button",
    },
    UiMeta {
        id: PROJECT_TAB_CREATE,
        description: "Project manager tab for creating a project.",
        kind: "pane_tab",
    },
    UiMeta {
        id: PROJECT_TAB_RECENT,
        description: "Project manager tab for recent projects.",
        kind: "pane_tab",
    },
    UiMeta {
        id: PROJECT_TAB_BROWSE,
        description: "Project manager tab for scanning a folder.",
        kind: "pane_tab",
    },
    UiMeta {
        id: PROJECT_NAME,
        description: "Name of the project being created.",
        kind: "text_input",
    },
    UiMeta {
        id: PROJECT_LOCATION,
        description: "Folder that will contain the new project.",
        kind: "text_input",
    },
    UiMeta {
        id: PROJECT_CREATE,
        description: "Creates the project and opens it.",
        kind: "button",
    },
    UiMeta {
        id: PROJECT_DEFAULT_LOCATION,
        description: "Restores the default projects folder.",
        kind: "button",
    },
    UiMeta {
        id: PROJECT_RECENT_SEARCH,
        description: "Filters recent projects by name or path.",
        kind: "text_input",
    },
    UiMeta {
        id: PROJECT_RECENT_OPEN,
        description: "Opens the selected recent project.",
        kind: "button",
    },
    UiMeta {
        id: PROJECT_BROWSE_PATH,
        description: "Folder scanned for project.caffeine files.",
        kind: "text_input",
    },
    UiMeta {
        id: PROJECT_BROWSE_SCAN,
        description: "Scans the folder for projects.",
        kind: "button",
    },
    UiMeta {
        id: PROJECT_BROWSE_DEFAULT,
        description: "Scans the default projects folder.",
        kind: "button",
    },
    UiMeta {
        id: PROJECT_BROWSE_SEARCH,
        description: "Filters scanned projects.",
        kind: "text_input",
    },
    UiMeta {
        id: PROJECT_BROWSE_OPEN,
        description: "Opens the selected scanned project.",
        kind: "button",
    },
    UiMeta {
        id: PROJECT_ERROR_DISMISS,
        description: "Dismisses the project manager error banner.",
        kind: "button",
    },
    UiMeta {
        id: VIEWPORT_GIZMO,
        description: "Transform gizmo over the scene viewport (move, rotate or scale).",
        kind: "gizmo",
    },
    UiMeta {
        id: PANE_VIEWPORT,
        description: "Offscreen engine render with camera controls.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_INSPECTOR,
        description: "Selected entity transform and actions.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_TEXTURE_SET,
        description: "Texture-set painting pane with UDIM tiles, layers, masks and export presets.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_MATERIAL,
        description: "PBR material studio with a rendered sphere preview, maps and a map baker.",
        kind: "pane",
    },
    UiMeta {
        id: MATERIAL_NEW,
        description: "Creates an unsaved material.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_SAVE,
        description: "Saves the open .mat material.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_APPLY,
        description: "Assigns the open material to the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_NAME,
        description: "Material name.",
        kind: "text_input",
    },
    UiMeta {
        id: MATERIAL_PRESET,
        description: "Applies a built-in PBR preset.",
        kind: "dropdown",
    },
    UiMeta {
        id: MATERIAL_PREVIEW,
        description: "Material sphere preview; drag to orbit.",
        kind: "preview",
    },
    UiMeta {
        id: MATERIAL_TEXTURE_ALBEDO,
        description: "Albedo/base-colour texture slot.",
        kind: "texture_slot",
    },
    UiMeta {
        id: MATERIAL_TEXTURE_NORMAL,
        description: "Normal map texture slot.",
        kind: "texture_slot",
    },
    UiMeta {
        id: MATERIAL_TEXTURE_ORM,
        description: "Occlusion/roughness/metallic texture slot.",
        kind: "texture_slot",
    },
    UiMeta {
        id: MATERIAL_TEXTURE_EMISSION,
        description: "Emission texture slot.",
        kind: "texture_slot",
    },
    UiMeta {
        id: MATERIAL_TEXTURE_SOURCE,
        description: "Source texture for the map baker.",
        kind: "texture_slot",
    },
    UiMeta {
        id: MATERIAL_BAKE_NORMAL,
        description: "Generates a normal map from the source height texture.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_BAKE_AO,
        description: "Generates an ambient-occlusion map from the source height texture.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_BAKE_ROUGHNESS,
        description: "Generates a roughness map from the albedo texture.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_BAKE_METALLIC,
        description: "Generates a metallic map from the albedo texture.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_BAKE_ORM,
        description: "Packs AO, roughness and metallic into an ORM map.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_BAKE_ALL,
        description: "Generates normal, AO and ORM from the source texture.",
        kind: "button",
    },
    UiMeta {
        id: INSPECTOR_MATERIAL_PICKER,
        description: "Opens the searchable material picker for the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: INSPECTOR_ADD_BEHAVIOUR,
        description: "Opens the picker of everything that can be attached to the entity: components, scripts, meshes, materials.",
        kind: "button",
    },
    UiMeta {
        id: BEHAVIOUR_PICKER,
        description: "Floating, searchable picker of components, scripts, meshes and materials.",
        kind: "dialog",
    },
    UiMeta {
        id: BEHAVIOUR_PICKER_CLOSE,
        description: "Closes the behaviour picker.",
        kind: "button",
    },
    UiMeta {
        id: BEHAVIOUR_PICKER_SEARCH,
        description: "Filters the behaviour picker by name, detail or group.",
        kind: "text_input",
    },
    UiMeta {
        id: BEHAVIOUR_NEW,
        description: "Opens the asset wizard to create a new Lua script.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_ASSET_PICKER,
        description: "Searchable .mat material picker.",
        kind: "palette",
    },
    UiMeta {
        id: MATERIAL_ASSET_PICKER_CLOSE,
        description: "Closes the material picker.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_ASSET_PICKER_SEARCH,
        description: "Searches .mat assets.",
        kind: "text_input",
    },
    UiMeta {
        id: MATERIAL_PREVIEW_SHAPE,
        description: "Chooses sphere, cube or plane for the material preview.",
        kind: "dropdown",
    },
    UiMeta {
        id: MATERIAL_PREVIEW_ENVIRONMENT,
        description: "Chooses an HDRI environment for the material preview.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_PREVIEW_ENVIRONMENT_CLEAR,
        description: "Returns the material preview to the studio environment.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_PREVIEW_EXPOSURE,
        description: "Environment exposure for the material preview.",
        kind: "slider",
    },
    UiMeta {
        id: MATERIAL_SUN_YAW,
        description: "Horizontal direction of the preview sun, in degrees.",
        kind: "slider",
    },
    UiMeta {
        id: MATERIAL_SUN_ELEVATION,
        description: "Height of the preview sun above the horizon, in degrees.",
        kind: "slider",
    },
    UiMeta {
        id: MATERIAL_SUN_INTENSITY,
        description: "Brightness of the preview sun.",
        kind: "slider",
    },
    UiMeta {
        id: MATERIAL_PREVIEW_SHADOWS,
        description: "Toggles shadow-mapped shadows in the material preview.",
        kind: "checkbox",
    },
    UiMeta {
        id: MATERIAL_PACK_HEIGHT,
        description: "Merges an external height image into the normal map's alpha for parallax depth.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_HEIGHT_PICKER,
        description: "Picker of the height image to pack into the normal map.",
        kind: "palette",
    },
    UiMeta {
        id: MATERIAL_HEIGHT_PICKER_CLOSE,
        description: "Closes the height picker.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_HEIGHT_PICKER_SEARCH,
        description: "Searches the project's images for a height map.",
        kind: "text_input",
    },
    UiMeta {
        id: MATERIAL_ENVIRONMENT_PICKER,
        description: "HDRI environment picker for the material preview.",
        kind: "palette",
    },
    UiMeta {
        id: MATERIAL_ENVIRONMENT_PICKER_CLOSE,
        description: "Closes the preview environment picker.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_ENVIRONMENT_PICKER_SEARCH,
        description: "Searches HDRI/environment images.",
        kind: "text_input",
    },
    UiMeta {
        id: MATERIAL_TEXTURE_SET,
        description: "Opens the TextureCraft-style texture-set painting pane.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_GENERATOR,
        description: "Opens the node-based material generator for this material.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_SCROLL,
        description: "Scrollable material studio body.",
        kind: "scrollable",
    },
    UiMeta {
        id: MATERIAL_BAKE_DIRECTX,
        description: "Writes DirectX-convention normals instead of OpenGL.",
        kind: "checkbox",
    },
    UiMeta {
        id: MATERIAL_BAKE_SCENE_AO,
        description: "Bakes ambient occlusion from the selected entity's scene geometry and assigns the ORM map.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_BAKE_WRAP,
        description: "Samples the height field toroidally for tiling maps.",
        kind: "checkbox",
    },
    UiMeta {
        id: FILEBROWSER,
        description: "File browser: Doppio's own picker for files and folders on this computer, the same on every operating system.",
        kind: "palette",
    },
    UiMeta {
        id: FILEBROWSER_CLOSE,
        description: "Closes the file browser without adding anything.",
        kind: "button",
    },
    UiMeta {
        id: FILEBROWSER_UP,
        description: "Goes to the parent folder.",
        kind: "button",
    },
    UiMeta {
        id: FILEBROWSER_REFRESH,
        description: "Lists the folder again.",
        kind: "button",
    },
    UiMeta {
        id: FILEBROWSER_PATH,
        description: "Type or paste a path (`~`, relative, quoted or file:// all work); Enter opens a folder or selects a file.",
        kind: "text_input",
    },
    UiMeta {
        id: FILEBROWSER_SEARCH,
        description: "Filters the listed entries by name.",
        kind: "text_input",
    },
    UiMeta {
        id: FILEBROWSER_SUPPORTED,
        description: "Shows only the file types the project can import (folders always show).",
        kind: "checkbox",
    },
    UiMeta {
        id: FILEBROWSER_HIDDEN,
        description: "Shows hidden files and folders.",
        kind: "checkbox",
    },
    UiMeta {
        id: FILEBROWSER_SELECT_SUPPORTED,
        description: "Selects every importable file listed in this folder.",
        kind: "button",
    },
    UiMeta {
        id: FILEBROWSER_CLEAR,
        description: "Empties the selection.",
        kind: "button",
    },
    UiMeta {
        id: FILEBROWSER_CONFIRM,
        description: "Adds the selected files and folders to the import queue and closes the browser.",
        kind: "button",
    },
    UiMeta {
        id: FILEBROWSER_CANCEL,
        description: "Closes the file browser without adding anything.",
        kind: "button",
    },
    UiMeta {
        id: WIZARD_IMPORT_BROWSE,
        description: "Opens the file browser to choose what to import, instead of typing a path.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_PICK,
        description: "Shared asset picker: lists the project's assets a field accepts, with thumbnails, kind filters and search.",
        kind: "palette",
    },
    UiMeta {
        id: ASSET_PICK_CLOSE,
        description: "Closes the asset picker without changing the field.",
        kind: "button",
    },
    UiMeta {
        id: ASSET_PICK_SEARCH,
        description: "Search field of the asset picker (name or path).",
        kind: "text_input",
    },
    UiMeta {
        id: ASSET_PICK_NONE,
        description: "Empties the field the asset picker was opened for.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_PICKER,
        description: "Project texture picker for a material map slot.",
        kind: "palette",
    },
    UiMeta {
        id: MATERIAL_PICKER_CLOSE,
        description: "Closes the material texture picker.",
        kind: "button",
    },
    UiMeta {
        id: MATERIAL_PICKER_SEARCH,
        description: "Search field for the material texture picker.",
        kind: "text_input",
    },
    UiMeta {
        id: PANE_GENERATOR,
        description: "Node-based material generator with a reusable graph canvas.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_SCRIPT,
        description: "Script editor for Lua and Dryad files with tabs, save, revert and attach.",
        kind: "pane",
    },
    UiMeta {
        id: PANE_DRYAD_NODES,
        description: "Visual Dryad node editor that writes .dyd and .node.json.",
        kind: "pane",
    },
    UiMeta {
        id: SCRIPT_ROOT,
        description: "Script editor root: tabs, toolbar and source code.",
        kind: "pane",
    },
    UiMeta {
        id: SCRIPT_EDITOR,
        description: "Monospace source editor for the active script file.",
        kind: "text_editor",
    },
    UiMeta {
        id: SCRIPT_SAVE,
        description: "Saves the active script file.",
        kind: "button",
    },
    UiMeta {
        id: SCRIPT_REVERT,
        description: "Discards unsaved changes in the active script.",
        kind: "button",
    },
    UiMeta {
        id: SCRIPT_CLOSE,
        description: "Closes the active script tab.",
        kind: "button",
    },
    UiMeta {
        id: SCRIPT_ATTACH,
        description: "Attaches the active script to the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: SCRIPT_DETACH,
        description: "Clears the script component from the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: SCRIPT_EMPTY,
        description: "Shown when the script editor has no open file.",
        kind: "label",
    },
    UiMeta {
        id: DRYAD_ROOT,
        description: "Dryad node editor: palette, canvas, properties and runtime actions.",
        kind: "pane",
    },
    UiMeta {
        id: DRYAD_CANVAS,
        description: "Dryad graph canvas with exec and data wires.",
        kind: "node_canvas",
    },
    UiMeta {
        id: DRYAD_PALETTE_SEARCH,
        description: "Filters the Dryad node palette.",
        kind: "text_input",
    },
    UiMeta {
        id: DRYAD_SAVE,
        description: "Writes the Dryad .dyd and .node.json files.",
        kind: "button",
    },
    UiMeta {
        id: DRYAD_SYNC,
        description: "Rebuilds the canvas from the current .dyd source.",
        kind: "button",
    },
    UiMeta {
        id: DRYAD_DELETE,
        description: "Deletes the selected Dryad node or link.",
        kind: "button",
    },
    UiMeta {
        id: DRYAD_FIT,
        description: "Frames the whole Dryad graph.",
        kind: "button",
    },
    UiMeta {
        id: DRYAD_ATTACH,
        description: "Attaches the open Dryad script to the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: DRYAD_RUN,
        description: "Runs one Dryad preview frame on the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: DRYAD_ADD_MOTION,
        description: "Builds an OnUpdate -> Translate motion example.",
        kind: "button",
    },
    UiMeta {
        id: DRYAD_STATUS,
        description: "Dryad editor status and emitted-script message.",
        kind: "label",
    },
    UiMeta {
        id: DRYAD_PROPERTIES,
        description: "Selected Dryad node properties and inline input values.",
        kind: "card",
    },
    UiMeta {
        id: DRYAD_EMPTY,
        description: "Shown when no Dryad script is open.",
        kind: "label",
    },
    UiMeta {
        id: NODE_CANVAS,
        description: "Reusable node canvas: pan, zoom, drag nodes and connect ports.",
        kind: "node_canvas",
    },
    UiMeta {
        id: GENERATOR_BAKE,
        description: "Bakes the generator graph into the open material's texture maps.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_OPEN_MATERIAL,
        description: "Opens the Material studio for the generated material.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_DELETE_NODE,
        description: "Deletes the selected generator node.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_RESOLUTION,
        description: "Bake resolution for the generator graph.",
        kind: "dropdown",
    },
    UiMeta {
        id: GENERATOR_LIBRARY,
        description: "Starts the graph from a procedural library material (bricks, wood, rust, rock…).",
        kind: "dropdown",
    },
    UiMeta {
        id: GENERATOR_CHANNELS,
        description: "Switches the output preview between Base, Normal, Height, Roughness, Metal, AO, Emissive and Opacity.",
        kind: "chip",
    },
    UiMeta {
        id: GENERATOR_PREVIEW,
        description: "Preview of the last baked base-colour texture.",
        kind: "preview",
    },
    UiMeta {
        id: GENERATOR_UNDO,
        description: "Undoes the last material graph edit.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_REDO,
        description: "Redoes the last undone material graph edit.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_RESEED,
        description: "Gives every seeded node a new seed: a variation of the same material graph.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_FIT,
        description: "Fits the whole material graph in the canvas.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_SNAP,
        description: "Snaps generator nodes to a 16-unit grid.",
        kind: "checkbox",
    },
    UiMeta {
        id: GENERATOR_SEARCH,
        description: "Filters the generator node palette.",
        kind: "text_input",
    },
    UiMeta {
        id: GENERATOR_DELETE_LINK,
        description: "Deletes the selected generator link.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_COPY,
        description: "Copies the selected generator node.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_PASTE,
        description: "Pastes the copied generator node.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_TEXTURE_PICKER,
        description: "Project texture picker for an Image generator node.",
        kind: "palette",
    },
    UiMeta {
        id: GENERATOR_TEXTURE_PICKER_CLOSE,
        description: "Closes the Image node texture picker.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_TEXTURE_PICKER_SEARCH,
        description: "Searches textures for the Image node.",
        kind: "text_input",
    },
    UiMeta {
        id: NATUREGEN_PICKER,
        description: "Asset picker with previews for a Nature node (image or material).",
        kind: "palette",
    },
    UiMeta {
        id: NATUREGEN_PICKER_CLOSE,
        description: "Closes the Nature asset picker.",
        kind: "button",
    },
    UiMeta {
        id: NATUREGEN_PICKER_SEARCH,
        description: "Searches the project assets in the Nature asset picker.",
        kind: "text_input",
    },
    UiMeta {
        id: GENERATOR_BAKE_PROGRESS,
        description: "Progress of the background generator bake.",
        kind: "progress",
    },
    UiMeta {
        id: GENERATOR_CANCEL_BAKE,
        description: "Cancels the running generator bake.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_PACKING_NAME,
        description: "Name for saving the current channel-packing setup as a user preset.",
        kind: "text_input",
    },
    UiMeta {
        id: GENERATOR_PACKING_SAVE,
        description: "Saves the current channel packing as a named user preset.",
        kind: "button",
    },
    UiMeta {
        id: GENERATOR_PACKING_DELETE,
        description: "Deletes the named channel-packing user preset.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_NEW,
        description: "Creates a new texture set with UDIM tiles and a paint layer.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_SAVE,
        description: "Saves the texture set as a .texset project.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_UNDO,
        description: "Undoes the last tile paint stroke.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_REDO,
        description: "Redoes the last undone tile paint stroke.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_TILE_ADD,
        description: "Adds the next free UDIM tile.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_LAYER_ADD_PAINT,
        description: "Adds a paintable layer.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_LAYER_ADD_FILL,
        description: "Adds a solid-colour fill layer.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_LAYER_DELETE,
        description: "Deletes the selected layer.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_LAYER_UP,
        description: "Moves the selected layer up in the stack.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_LAYER_DOWN,
        description: "Moves the selected layer down in the stack.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_UV_CANVAS,
        description: "UV tile painting canvas for the active texture-set layer and channel.",
        kind: "canvas",
    },
    UiMeta {
        id: TEXTURE_SET_NAME,
        description: "Texture-set project name.",
        kind: "text_input",
    },
    UiMeta {
        id: TEXTURE_SET_CHANNEL,
        description: "Active texture-set channel (base colour, roughness, metallic, height, AO or curvature).",
        kind: "dropdown",
    },
    UiMeta {
        id: TEXTURE_SET_MASK,
        description: "Paints into the active layer mask instead of its colour.",
        kind: "checkbox",
    },
    UiMeta {
        id: TEXTURE_SET_PAINT_3D,
        description: "Toggles painting on the selected mesh in the 3D viewport.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_EXPORT_PRESET,
        description: "Selects the map export preset for texture sets.",
        kind: "dropdown",
    },
    UiMeta {
        id: TEXTURE_SET_EXPORT,
        description: "Exports the texture set with the selected preset.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_BAKE_AO,
        description: "Bakes ambient occlusion from the texture-set height channel.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_BAKE_CURVATURE,
        description: "Bakes a curvature/cavity mask from the height channel.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_BAKE_SCENE_AO,
        description: "Bakes AO from the selected entity scene geometry into the texture set.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_BAKE_NORMAL,
        description: "Bakes world normals from the selected mesh into the texture set.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_BAKE_POSITION,
        description: "Bakes world positions from the selected mesh into the texture set.",
        kind: "button",
    },
    UiMeta {
        id: TEXTURE_SET_BAKE_OBJECT_ID,
        description: "Bakes an object-ID mask from the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("generator.add"),
        description: "Adds a generator node from the palette; ids use generator.add.<kind>.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("generator.field"),
        description: "A generator node parameter; ids use generator.field.<name>[.<axis>].",
        kind: "slider",
    },
    UiMeta {
        id: CREATE_ENTITY,
        description: "Opens the create-entity prefab palette.",
        kind: "menu_item",
    },
    UiMeta {
        id: HIERARCHY_ADD,
        description: "Adds a new entity from the Hierarchy toolbar.",
        kind: "button",
    },
    UiMeta {
        id: PREFAB_PALETTE,
        description: "Create-entity palette with built-in prefabs.",
        kind: "palette",
    },
    UiMeta {
        id: PREFAB_CLOSE,
        description: "Closes the create-entity palette.",
        kind: "button",
    },
    UiMeta {
        id: HIERARCHY_EMPTY,
        description: "Shown when the scene has no entities or the search has no matches.",
        kind: "tree_row",
    },
    UiMeta {
        id: HIERARCHY_ROOT,
        description: "The scene root row of the hierarchy tree.",
        kind: "tree_row",
    },
    UiMeta {
        id: HIERARCHY_SEARCH,
        description: "Search field for filtering hierarchy entities.",
        kind: "text_input",
    },
    UiMeta {
        id: PANE_TAB_HIERARCHY,
        description: "Hierarchy tab in a shared pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: PANE_TAB_ASSETS,
        description: "Assets tab in a shared pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: PANE_TAB_VIEWPORT,
        description: "Viewport tab in a shared pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: PANE_TAB_INSPECTOR,
        description: "Inspector tab in a shared pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: PANE_TAB_SCRIPT,
        description: "Script editor tab in a shared pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: PANE_TAB_DRYAD_NODES,
        description: "Dryad Nodes tab in a shared pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: PANE_TAB_CONSOLE,
        description: "Console tab in a shared pane.",
        kind: "pane_tab",
    },
    UiMeta {
        id: CONSOLE_FILTER_ERROR,
        description: "Show or hide error entries in the Console.",
        kind: "filter",
    },
    UiMeta {
        id: CONSOLE_FILTER_LOG,
        description: "Show or hide general log entries in the Console.",
        kind: "filter",
    },
    UiMeta {
        id: CONSOLE_FILTER_ACTION,
        description: "Show or hide editor action entries in the Console.",
        kind: "filter",
    },
    UiMeta {
        id: CONSOLE_FILTER_ENGINE,
        description: "Show or hide engine entries in the Console.",
        kind: "filter",
    },
    UiMeta {
        id: CONSOLE_FILTER_UI,
        description: "Show or hide UI entries in the Console.",
        kind: "filter",
    },
    UiMeta {
        id: PANE_CONSOLE,
        description: "Editor log and engine messages.",
        kind: "pane",
    },
    UiMeta {
        id: VIEWPORT_SURFACE,
        description: "Engine render output shown inside the viewport pane.",
        kind: "image",
    },
    UiMeta {
        id: FOCUS_SELECTION,
        description: "Frames the selected entity in the viewport.",
        kind: "button",
    },
    UiMeta {
        id: RESET_CAMERA,
        description: "Restores the default camera.",
        kind: "button",
    },
    UiMeta {
        id: ENTITY_NAME,
        description: "Editable name of the selected entity.",
        kind: "text_input",
    },
    UiMeta {
        id: DELETE_ENTITY,
        description: "Deletes the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: INSPECTOR_MATERIAL,
        description: "The selected entity's assigned material; click to open the editor.",
        kind: "texture_slot",
    },
    UiMeta {
        id: INSPECTOR_MATERIAL_CLEAR,
        description: "Unassigns the material from the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("inspector.component.field"),
        description: "One component field editor; fields use .component.field.axis.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("inspector.component.remove"),
        description: "Removes one component; buttons use the .component suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("hierarchy.tree.toggle"),
        description: "Expand/collapse chevron of a hierarchy row.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("hierarchy.list.entity_row"),
        description: "A row in the hierarchy list; rows use the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("assets.grid.asset_card"),
        description: "A project asset card; cards use the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("assets.list.asset_row"),
        description: "A project asset list row; rows use the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("assets.toolbar.crumb"),
        description: "An asset-browser breadcrumb; crumbs use the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("project.create.template"),
        description: "A project template button; templates use the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("project.recent.row"),
        description: "A recent-project row; rows use the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("project.browse.row"),
        description: "A scanned-project row; rows use the .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("script.editor.tab"),
        description: "A script editor file tab; tabs use the .N suffix.",
        kind: "pane_tab",
    },
    UiMeta {
        id: UiId::fixed("script.editor.tab.close"),
        description: "Closes a script editor tab; tabs use the .N suffix.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("dryad.nodes.palette"),
        description: "A Dryad palette node; entries use the type-name suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("dryad.nodes.property"),
        description: "A Dryad node property; entries use the field-name suffix.",
        kind: "text_input",
    },
    UiMeta {
        id: UiId::fixed("material.surface"),
        description: "A material property control; ids use material.surface.<name>[.<axis>].",
        kind: "slider",
    },
    UiMeta {
        id: UiId::fixed("material.map"),
        description: "A material texture slot; ids use material.map.<slot>.",
        kind: "texture_slot",
    },
    UiMeta {
        id: UiId::fixed("material.section"),
        description: "A collapsible material section; ids use material.section.<name>.",
        kind: "card",
    },
    UiMeta {
        id: UiId::fixed("material.picker.item"),
        description: "A texture row in the material picker; rows use a .N suffix.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("phaseshift.parameters.row"),
        description: "A parameter of the PhaseShift animator (name, type, live value); rows use the .N suffix.",
        kind: "card",
    },
    UiMeta {
        id: UiId::fixed("phaseshift.add"),
        description: "Gives the selected entity a PhaseShift when it has none.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("phaseshift.parameters.add"),
        description: "Adds a PhaseShift parameter of a kind (float, bool, int, trigger); the name is picked for you.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("phaseshift.parameters.remove"),
        description: "Removes a PhaseShift parameter; rows use the .N suffix.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("phaseshift.parameters.smoothing"),
        description: "Smoothing time slider of a float PhaseShift parameter, in seconds; uses the .N suffix.",
        kind: "slider",
    },
    UiMeta {
        id: UiId::fixed("phaseshift.states.row"),
        description: "A state of the PhaseShift animator; rows use the .N suffix and select the state.",
        kind: "list_row",
    },
    UiMeta {
        id: UiId::fixed("rig2d.attach.path"),
        description: "The path of a .rig2d file to attach to the selected entity; Enter attaches it.",
        kind: "text_field",
    },
    UiMeta {
        id: UiId::fixed("rig2d.attach.button"),
        description: "Attaches the path in the attach field to the selected entity.",
        kind: "button",
    },
    UiMeta {
        id: UiId::fixed("rig2d.parameters.reset"),
        description: "Returns every rig parameter to its default.",
        kind: "button",
    },
];

/// Human description for a catalogued id, including dynamic list rows.
pub fn description(key: &str) -> Option<&'static str> {
    CATALOG
        .iter()
        .find(|meta| meta.id.key() == key)
        .or_else(|| {
            CATALOG
                .iter()
                .find(|meta| meta.kind == "list_row" && key.starts_with(meta.id.key()))
        })
        .map(|meta| meta.description)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keys_are_grouped() {
        assert_eq!(ADD_ENTITY.group(), "editor");
        assert_eq!(ADD_ENTITY.subgroup(), "toolbar");
        assert_eq!(ADD_ENTITY.object(), "add_entity");
        assert_eq!(entity_row(7).group(), "hierarchy");
        assert_eq!(entity_row(7).subgroup(), "list");
        assert_eq!(entity_row(7).object(), "entity_row.7");
    }

    #[test]
    fn catalog_is_unique_and_described() {
        let mut keys = std::collections::HashSet::new();
        for meta in CATALOG {
            assert!(keys.insert(meta.id.key()), "duplicate {}", meta.id.key());
            assert!(!meta.description.is_empty());
        }
        assert!(description("hierarchy.list.entity_row.12").is_some());
        assert!(description("assets.grid.asset_card.2").is_some());
        assert!(description("missing").is_none());
    }
}

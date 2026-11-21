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
        kind: "tab",
    },
    UiMeta {
        id: PROJECT_TAB_RECENT,
        description: "Project manager tab for recent projects.",
        kind: "tab",
    },
    UiMeta {
        id: PROJECT_TAB_BROWSE,
        description: "Project manager tab for scanning a folder.",
        kind: "tab",
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
        kind: "tab",
    },
    UiMeta {
        id: PANE_TAB_ASSETS,
        description: "Assets tab in a shared pane.",
        kind: "tab",
    },
    UiMeta {
        id: PANE_TAB_VIEWPORT,
        description: "Viewport tab in a shared pane.",
        kind: "tab",
    },
    UiMeta {
        id: PANE_TAB_INSPECTOR,
        description: "Inspector tab in a shared pane.",
        kind: "tab",
    },
    UiMeta {
        id: PANE_TAB_SCRIPT,
        description: "Script editor tab in a shared pane.",
        kind: "tab",
    },
    UiMeta {
        id: PANE_TAB_DRYAD_NODES,
        description: "Dryad Nodes tab in a shared pane.",
        kind: "tab",
    },
    UiMeta {
        id: PANE_TAB_CONSOLE,
        description: "Console tab in a shared pane.",
        kind: "tab",
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
        kind: "tab",
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

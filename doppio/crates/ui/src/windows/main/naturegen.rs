//! Editor state and message handling of the **Nature Generator** pane (rocks, trees, plants).
//!
//! A model is a node graph (`uikit::nature`). This module owns the graph being edited with an undo
//! history, renders the preview, writes `.obj` files and places the result in the scene as a group
//! of entities, one per part, each with its own material (library presets, Texture Generator cards,
//! plain colours, or **your own project materials and textures**).

use super::{add_component, set_component_field, App, LogLevel, PaneKind};
use crate::uikit::asset_folders;
use crate::uikit::generators::TextureMap;
use crate::uikit::material_library;
use crate::uikit::nature::mesh::MaterialSpec;
use crate::uikit::nature::{self, preview, NatureGraph, NodeId, Quality};
use crate::uikit::panels::nature_generator::{self as panel, Edit};
use crate::uikit::recipe;
use crate::uikit::recipe_ref;
use crate::uikit::texture_graph;
use iced::widget::image::Handle;
use iced::{Point, Vector};
use std::collections::HashSet;
use std::path::{Path, PathBuf};

const PREVIEW_SIZE: u32 = 330;

/// Wind and light-through-leaf settings of a part's material (see `MaterialSurface`).
///
/// Every part of one model that sways uses the same `sway`, so leaves stay attached to the
/// branches that carry them: the shader moves a vertex by `sway` x height above the entity origin,
/// the same way for trunk and crown. Only leaves and petals flutter.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(crate) struct Foliage {
    pub sway: f32,
    pub flutter: f32,
    pub translucency: f32,
}

impl Foliage {
    const STILL: Foliage = Foliage {
        sway: 0.0,
        flutter: 0.0,
        translucency: 0.0,
    };

    /// The profile for a part by its name (`trunk`, `leaves`, `blades`, ...).
    pub(crate) fn for_part(part: &str) -> Foliage {
        let tree = 0.012;
        let low = 0.25;
        match part {
            "trunk" | "branches" | "roots" => Foliage {
                sway: tree,
                ..Foliage::STILL
            },
            "leaves" => Foliage {
                sway: tree,
                flutter: 0.6,
                translucency: 0.55,
            },
            "blades" | "fronds" => Foliage {
                sway: low,
                flutter: 0.25,
                translucency: 0.45,
            },
            "petals" => Foliage {
                sway: low,
                flutter: 0.35,
                translucency: 0.4,
            },
            "stem" | "center" => Foliage {
                sway: low,
                ..Foliage::STILL
            },
            _ => Foliage::STILL,
        }
    }

    /// The profile of a part: a Material node's own wind and translucency, else the part's default.
    pub(crate) fn for_spec(part: &str, spec: &MaterialSpec) -> Foliage {
        match spec {
            MaterialSpec::Custom(custom) => Foliage {
                sway: custom.sway,
                flutter: custom.flutter,
                translucency: custom.translucency,
            },
            _ => Foliage::for_part(part),
        }
    }

    /// File-name suffix so materials with different wind do not overwrite each other.
    fn suffix(self) -> String {
        if self == Foliage::STILL {
            String::new()
        } else {
            format!(
                "_w{}f{}t{}",
                (self.sway * 1000.0).round() as i32,
                (self.flutter * 100.0).round() as i32,
                (self.translucency * 100.0).round() as i32
            )
        }
    }
}

pub(super) struct NatureGen {
    pub graph: NatureGraph,
    undo: Vec<(NatureGraph, Option<String>)>,
    cursor: usize,
    pub quality: Quality,
    pub name: String,
    pub selected: Option<NodeId>,
    pub link: Option<(NodeId, usize)>,
    pub pan: Vector,
    pub zoom: f32,
    pub snap: bool,
    /// Place the parts that share a kind of surface (solid or cut-out) as one entity with a material slot each.
    pub merge_parts: bool,
    /// Switch Micromesh on for the dense solid parts (rocks, trunks).
    pub micromesh: bool,
    pub search: String,
    pub collapsed: HashSet<String>,
    pub presets: Vec<nature::NaturePreset>,
    pub preset_labels: Vec<String>,
    pub preview: Option<Handle>,
    pub view: preview::View,
    pub stats: String,
    pub status: Option<String>,
    /// The open asset picker: node, string index, what it takes.
    pub picker: Option<(NodeId, usize, panel::PickKind)>,
    pub picker_search: String,
    /// Decoded thumbnails by project path (filled while drawing, cleared when assets change).
    pub thumb_cache: std::cell::RefCell<std::collections::HashMap<String, Option<Handle>>>,
    pub dirty: bool,
    orbit: Option<Point>,
}

impl NatureGen {
    pub fn new() -> Self {
        let presets = nature::presets();
        let first = presets.iter().position(|p| p.name == "Oak").unwrap_or(0);
        let preset_labels = presets
            .iter()
            .map(|p| format!("{} · {}", p.category, p.name))
            .collect();
        let graph = presets[first].graph();
        let mut state = NatureGen {
            undo: vec![(graph.clone(), None)],
            cursor: 0,
            graph,
            quality: presets[first].quality,
            name: presets[first].name.to_owned(),
            selected: None,
            link: None,
            pan: Vector::new(0.0, 0.0),
            zoom: 1.0,
            snap: false,
            merge_parts: true,
            micromesh: true,
            search: String::new(),
            collapsed: HashSet::new(),
            presets,
            preset_labels,
            preview: None,
            view: preview::View::default(),
            stats: String::new(),
            status: None,
            picker: None,
            picker_search: String::new(),
            thumb_cache: Default::default(),
            dirty: false,
            orbit: None,
        };
        state.fit();
        state.refresh();
        state
    }

    /// Replaces the graph (preset or new) and starts a fresh history.
    pub(super) fn load(&mut self, graph: NatureGraph) {
        self.undo = vec![(graph.clone(), None)];
        self.cursor = 0;
        self.graph = graph;
        self.selected = self.graph.output_node();
        self.link = None;
        self.fit();
        self.refresh();
    }

    fn push(&mut self, key: Option<String>) {
        self.dirty = true;
        // Edits with the same key (dragging a node, moving a slider) merge into one undo step.
        if key.is_some()
            && self.undo.get(self.cursor).and_then(|(_, k)| k.clone()) == key
            && self.cursor + 1 == self.undo.len()
        {
            self.undo[self.cursor].0 = self.graph.clone();
            return;
        }
        self.undo.truncate(self.cursor + 1);
        self.undo.push((self.graph.clone(), key));
        self.cursor = self.undo.len() - 1;
        if self.undo.len() > 200 {
            self.undo.remove(0);
            self.cursor -= 1;
        }
    }

    pub fn fit(&mut self) {
        let nodes = &self.graph.nodes;
        if nodes.is_empty() {
            self.pan = Vector::new(24.0, 24.0);
            self.zoom = 1.0;
            return;
        }
        let min_x = nodes.iter().map(|n| n.position.x).fold(f32::MAX, f32::min);
        let min_y = nodes.iter().map(|n| n.position.y).fold(f32::MAX, f32::min);
        let max_x = nodes.iter().map(|n| n.position.x).fold(f32::MIN, f32::max) + 170.0;
        let max_y = nodes.iter().map(|n| n.position.y).fold(f32::MIN, f32::max) + 130.0;
        let zoom = (780.0 / (max_x - min_x).max(1.0))
            .min(470.0 / (max_y - min_y).max(1.0))
            .clamp(0.3, 1.4);
        self.zoom = zoom;
        self.pan = Vector::new(24.0 - min_x * zoom, 24.0 - min_y * zoom);
    }

    /// Rebuilds the model and the preview image.
    pub fn refresh(&mut self) {
        match self.graph.build(self.quality) {
            Ok(model) => {
                self.stats = format!(
                    "{} triangles · {} vertices · {} part{}",
                    model.triangles(),
                    model.vertices(),
                    model.parts.len(),
                    if model.parts.len() == 1 { "" } else { "s" }
                );
                if self
                    .status
                    .as_deref()
                    .is_some_and(|s| s.starts_with("Graph:"))
                {
                    self.status = None;
                }
                let pixels = preview::render(&model, PREVIEW_SIZE, PREVIEW_SIZE, self.view);
                self.preview = Some(Handle::from_rgba(PREVIEW_SIZE, PREVIEW_SIZE, pixels));
            }
            Err(error) => {
                self.stats = "no model".to_owned();
                self.status = Some(format!("Graph: {error}"));
                self.preview = None;
            }
        }
    }

    pub fn view_state(
        &self,
        materials: Vec<String>,
        textures: Vec<String>,
        thumbnails: std::collections::HashMap<String, Handle>,
    ) -> panel::View<'_> {
        panel::View {
            graph: &self.graph,
            selected: self.selected,
            selected_link: self.link,
            pan: self.pan,
            zoom: self.zoom,
            snap: self.snap,
            merge_parts: self.merge_parts,
            micromesh: self.micromesh,
            search: &self.search,
            collapsed_groups: &self.collapsed,
            quality: self.quality,
            name: &self.name,
            presets: &self.preset_labels,
            preview: self.preview.as_ref(),
            stats: &self.stats,
            status: self.status.as_deref(),
            dirty: self.dirty,
            project_materials: materials,
            project_textures: textures,
            thumbnails,
        }
    }
}

/// RGBA pixels of a leaf/petal card material and their size. Atlas names (`Atlas · ...`) bake every
/// cell of the atlas and tile them, so one image holds several leaf shapes.
fn texture_cards_png(name: &str, size: u32) -> Result<(Vec<u8>, u32, u32), String> {
    let bake = |mut graph: crate::uikit::generators::MaterialGraph,
                cell: u32|
     -> Result<Vec<u8>, String> {
        graph.resolution = cell;
        Ok(graph
            .bake_texture_with(&TextureMap::new(), None, None)?
            .rgba8)
    };
    if let Some((cols, rows, graphs)) = texture_graph::atlas(name) {
        let cell = (size / 2).max(64);
        let (width, height) = (cell * cols, cell * rows);
        let mut pixels = vec![0u8; (width * height * 4) as usize];
        for (index, graph) in graphs.into_iter().enumerate() {
            let (cx, cy) = (index as u32 % cols, index as u32 / cols);
            let tile = bake(graph, cell)?;
            for y in 0..cell {
                let from = (y * cell * 4) as usize;
                let to = (((cy * cell + y) * width + cx * cell) * 4) as usize;
                pixels[to..to + (cell * 4) as usize]
                    .copy_from_slice(&tile[from..from + (cell * 4) as usize]);
            }
        }
        return Ok((pixels, width, height));
    }
    let preset = texture_graph::presets()
        .iter()
        .find(|p| p.name == name)
        .ok_or_else(|| format!("unknown texture preset `{name}`"))?;
    Ok((bake(preset.build(), size)?, size, size))
}

fn snap_position(point: Point, snap: bool) -> Point {
    if snap {
        Point::new(
            (point.x / 16.0).round() * 16.0,
            (point.y / 16.0).round() * 16.0,
        )
    } else {
        point
    }
}

impl App {
    pub(super) fn show_nature_pane(&mut self) {
        self.panes.restore();
        let mut target = None;
        for (pane, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::NatureGenerator)
            {
                group.active = index;
                target = Some(*pane);
            }
        }
        if target.is_none() {
            for (pane, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Viewport) {
                    group.pages.push(PaneKind::NatureGenerator);
                    group.active = group.pages.len() - 1;
                    target = Some(*pane);
                    break;
                }
            }
        }
        if let Some(pane) = target {
            self.panes.maximize(pane);
        }
    }

    /// The project's `.mat` files and images (asset-root relative), for the Part node's pickers.
    /// A decoded thumbnail of a project image, cached so drawing it every frame stays cheap.
    pub(super) fn nature_thumbnail(&self, path: &str) -> Option<Handle> {
        if path.is_empty() {
            return None;
        }
        if let Some(found) = self.nature.thumb_cache.borrow().get(path) {
            return found.clone();
        }
        let handle = self.load_thumbnail(path);
        self.nature
            .thumb_cache
            .borrow_mut()
            .insert(path.to_owned(), handle.clone());
        handle
    }

    pub(super) fn nature_assets(&self) -> (Vec<String>, Vec<String>) {
        let pick = |kind: &str| -> Vec<String> {
            self.assets
                .iter()
                .filter(|a| !a.is_directory && a.kind == kind)
                .map(|a| a.relative_path.clone())
                .collect()
        };
        (pick("material"), pick("image"))
    }

    pub(super) fn edit_nature(&mut self, edit: Edit) {
        let snap = self.nature.snap;
        match edit {
            Edit::Select(selected) => {
                self.nature.selected = selected;
                self.nature.link = None;
            }
            Edit::SelectLink(link) => {
                self.nature.link = link;
                self.nature.selected = None;
            }
            Edit::DeleteLink(to, input) | Edit::DetachLink(to, input) => {
                self.nature
                    .graph
                    .links
                    .retain(|l| !(l.to == to && l.input == input));
                self.nature.link = None;
                self.nature.push(None);
                self.nature.refresh();
            }
            Edit::Connect {
                from,
                output,
                to,
                input,
            } => {
                if self.nature.graph.connect(from, output, to, input) {
                    self.nature.status = None;
                    self.nature.link = Some((to, input));
                    self.nature.push(None);
                    self.nature.refresh();
                } else {
                    self.nature.status = Some("Those ports cannot connect: the colours must match and the graph cannot loop".to_owned());
                }
            }
            Edit::MoveNode(id, delta) => {
                if let Some(node) = self.nature.graph.node_mut(id) {
                    node.position =
                        Point::new(node.position.x + delta.x, node.position.y + delta.y);
                }
                self.nature.push(Some(format!("move:{id}")));
            }
            Edit::Viewport { pan, zoom } => {
                self.nature.pan = pan;
                self.nature.zoom = zoom.clamp(0.3, 2.0);
            }
            Edit::AddNode(op) => {
                if op == "output" {
                    if let Some(output) = self.nature.graph.output_node() {
                        self.nature.selected = Some(output);
                        return;
                    }
                }
                let at = Point::new(
                    (260.0 - self.nature.pan.x) / self.nature.zoom,
                    (160.0 - self.nature.pan.y) / self.nature.zoom
                        + self.nature.graph.nodes.len() as f32 * 6.0,
                );
                let id = self.nature.graph.add(op, snap_position(at, snap));
                self.nature.selected = Some(id);
                self.nature.link = None;
                self.nature.push(None);
                self.nature.refresh();
            }
            Edit::DeleteNode(id) => {
                if self.nature.graph.node(id).is_some_and(|n| n.op == "output") {
                    self.nature.status = Some(
                        "The Output node is the graph's result and cannot be deleted".to_owned(),
                    );
                    return;
                }
                self.nature.graph.remove(id);
                if self.nature.selected == Some(id) {
                    self.nature.selected = None;
                }
                self.nature.link = None;
                self.nature.push(None);
                self.nature.refresh();
            }
            Edit::SetParam(id, index, value) => {
                if let Some(node) = self.nature.graph.node_mut(id) {
                    if let Some(spec) = node.def().params.get(index) {
                        let v = if matches!(spec.kind, crate::uikit::procedural::ParamKind::Float) {
                            value
                        } else {
                            value.round()
                        };
                        node.params[index] = v.clamp(spec.min, spec.max);
                    }
                }
                self.nature.push(Some(format!("param:{id}:{index}")));
                self.nature.refresh();
            }
            Edit::OpenPicker(id, index, kind) => {
                self.nature.picker = Some((id, index, kind));
                self.nature.picker_search.clear();
            }
            Edit::PickerSearch(text) => self.nature.picker_search = text,
            Edit::PickerClose => self.nature.picker = None,
            Edit::PickerPick(path) => {
                if let Some((id, index, _)) = self.nature.picker.take() {
                    self.nature.graph.set_string(id, index, path);
                    self.nature.push(Some(format!("string:{id}:{index}")));
                    self.nature.refresh();
                }
            }
            Edit::DropAsset(id, index, kind) => {
                // The asset being dragged from the Assets pane lands on the slot it was released on.
                if let Some(asset) = self.asset_drag.take() {
                    let path = self
                        .assets
                        .get(asset)
                        .filter(|a| !a.is_directory && a.kind == kind.asset_kind())
                        .map(|a| a.relative_path.clone());
                    match path {
                        Some(path) => {
                            self.nature.graph.set_string(id, index, path);
                            self.nature.push(Some(format!("string:{id}:{index}")));
                            self.nature.refresh();
                        }
                        None => {
                            self.nature.status = Some(format!("Drop a {} here", kind.asset_kind()));
                        }
                    }
                }
            }
            Edit::SetString(id, index, text) => {
                self.nature.graph.set_string(id, index, text);
                self.nature.push(Some(format!("string:{id}:{index}")));
                self.nature.refresh();
            }
            Edit::Undo | Edit::Redo => {
                let undo = matches!(edit, Edit::Undo);
                let target = if undo {
                    self.nature.cursor.checked_sub(1)
                } else {
                    Some(self.nature.cursor + 1).filter(|c| *c < self.nature.undo.len())
                };
                match target {
                    Some(cursor) => {
                        self.nature.cursor = cursor;
                        self.nature.graph = self.nature.undo[cursor].0.clone();
                        self.nature.dirty = true;
                        if self
                            .nature
                            .selected
                            .is_some_and(|id| self.nature.graph.node(id).is_none())
                        {
                            self.nature.selected = None;
                        }
                        self.nature.refresh();
                    }
                    None => {
                        self.nature.status = Some(format!(
                            "Nothing to {} in the graph",
                            if undo { "undo" } else { "redo" }
                        ))
                    }
                }
            }
            Edit::Fit => self.nature.fit(),
            Edit::SetSnap(value) => self.nature.snap = value,
            Edit::SetMergeParts(value) => self.nature.merge_parts = value,
            Edit::SetMicromesh(value) => self.nature.micromesh = value,
            Edit::SetSearch(value) => self.nature.search = value,
            Edit::TogglePaletteGroup(name) => {
                if !self.nature.collapsed.remove(&name) {
                    self.nature.collapsed.insert(name);
                }
            }
            Edit::ApplyPreset(index) => {
                if let Some(graph) = self
                    .nature
                    .presets
                    .get(index)
                    .map(|p| (p.graph(), p.quality, p.name))
                {
                    self.nature.quality = graph.1;
                    self.nature.name = graph.2.to_owned();
                    self.nature.load(graph.0);
                    self.nature.dirty = true;
                    self.nature.status = None;
                    // A preset is only a mould: its materials become project assets right away.
                    if let Err(error) = self.materialize_nature_assets() {
                        self.nature.status =
                            Some(format!("Could not write the materials: {error}"));
                    }
                    let name = self.nature.name.clone();
                    self.log(LogLevel::Action, format!("Nature preset: {name}"));
                }
            }
            Edit::SetQuality(index) => {
                self.nature.quality = Quality::from_index(index);
                self.nature.dirty = true;
                self.nature.refresh();
            }
            Edit::SetName(name) => {
                self.nature.name = name;
                self.nature.dirty = true;
            }
            Edit::New => {
                let mut graph = NatureGraph::new();
                graph.add("output", Point::new(300.0, 0.0));
                self.nature.name = "Model".to_owned();
                self.nature.load(graph);
                self.nature.dirty = true;
            }
            Edit::Export => self.export_nature(),
            Edit::Place => self.place_nature(),
            Edit::AddToScatter => self.add_nature_to_scatter_plan(),
            Edit::Noop => {}
            Edit::PreviewPress => self.nature.orbit = Some(Point::ORIGIN),
            Edit::PreviewMove(point) => {
                if self.nature.orbit.is_some() {
                    let last = self.nature.orbit.replace(point).unwrap_or(point);
                    if last != Point::ORIGIN {
                        self.nature.view.yaw -= (point.x - last.x) * 0.012;
                        self.nature.view.pitch =
                            (self.nature.view.pitch + (point.y - last.y) * 0.012).clamp(-1.3, 1.3);
                        self.nature.refresh();
                    }
                }
            }
            Edit::PreviewRelease => self.nature.orbit = None,
            Edit::PreviewZoom(delta) => {
                self.nature.view.zoom =
                    (self.nature.view.zoom * (1.0 + delta * 0.1)).clamp(0.4, 4.0);
                self.nature.refresh();
            }
        }
    }

    fn nature_dir(&self) -> Option<PathBuf> {
        self.asset_root_dir()
            .map(|root| asset_folders::resolve(&root, asset_folders::AssetFolder::NatureModels))
    }

    fn export_nature(&mut self) {
        self.ensure_workspace();
        let Some(dir) = self.nature_dir() else {
            self.nature.status =
                Some("Open a project first: models are written to its assets folder".to_owned());
            return;
        };
        if let Err(error) = self.materialize_nature_assets() {
            self.nature.status = Some(format!("Could not write the materials: {error}"));
            return;
        }
        let name = self.nature.name.clone();
        let model = match self.nature.graph.build(self.nature.quality) {
            Ok(model) => model,
            Err(error) => {
                self.nature.status = Some(format!("Graph: {error}"));
                return;
            }
        };
        match nature::write_obj_files(&model, &dir, &name) {
            Ok(files) => {
                let slug = nature::slug(&name);
                let graph = nature::to_text(&name, &self.nature.graph, self.nature.quality);
                let product = files.first().cloned().unwrap_or_default();
                if let Err(error) = self.write_recipe(
                    &dir,
                    crate::uikit::recipe::Kind::Nature,
                    &slug,
                    &name,
                    &product,
                    graph,
                ) {
                    self.log(LogLevel::Error, format!("Nature recipe: {error}"));
                }
                self.nature.dirty = false;
                self.nature.status = Some(format!(
                    "Exported {} file(s) to {}",
                    files.len(),
                    dir.display()
                ));
                self.log(LogLevel::Action, format!("Exported nature model {name}"));
            }
            Err(error) => self.nature.status = Some(format!("Export failed: {error}")),
        }
    }

    /// Writes the maps and the `.mat` of a part's material (once) and returns the path to apply.
    /// Bakes the part's own masks, generates its rock texture set and writes a `.mat` around it.
    fn ensure_surface_material(
        &self,
        part: &nature::mesh::Part,
        spec: &nature::surface::SurfaceSpec,
    ) -> Result<String, String> {
        let engine = self.engine.as_ref().ok_or("The engine is not available")?;
        let root = self.asset_root_dir().ok_or("No project is open")?;
        let mat_folder = asset_folders::ensure(&root, asset_folders::AssetFolder::NatureMaterials)
            .map_err(|e| e.to_string())?;
        let tex_folder = asset_folders::ensure(&root, asset_folders::AssetFolder::NatureTextures)
            .map_err(|e| e.to_string())?;
        let mat_rel = asset_folders::relative(&root, &mat_folder);
        let tex_rel = asset_folders::relative(&root, &tex_folder);
        let slug = format!(
            "{}_{}",
            part.name.to_lowercase().replace(' ', "_"),
            spec.slug()
        );
        let mat_file = mat_folder.join(format!("{slug}.mat"));
        let relative = |file: &str| format!("{tex_rel}/{file}");
        let relative_mat = |file: &str| format!("{mat_rel}/{file}");
        let absolute_ok = engine
            .asset_path("")
            .map(|root| !root.is_empty())
            .unwrap_or(false);
        let map_ref = |file: &str| {
            if absolute_ok {
                engine
                    .asset_project_path(&relative(file))
                    .unwrap_or_else(|_| relative(file))
            } else {
                tex_folder.join(file).display().to_string()
            }
        };
        if !mat_file.exists() {
            let res = spec.resolution as usize;
            let maps = nature::bake::bake(part, res);
            let set = nature::surface::rock(&maps, spec);
            let save = |file: &str, pixels: &[u8]| {
                image::save_buffer(
                    tex_folder.join(file),
                    pixels,
                    res as u32,
                    res as u32,
                    image::ColorType::Rgba8,
                )
                .map_err(|e| e.to_string())
            };
            save(&format!("{slug}_albedo.png"), &set.albedo)?;
            save(&format!("{slug}_normal.png"), &set.normal)?;
            save(&format!("{slug}_orm.png"), &set.orm)?;
            let mut material = engine.new_material().map_err(|e| format!("{e:?}"))?;
            material.name = slug.clone();
            material.albedo_map = map_ref(&format!("{slug}_albedo.png"));
            material.normal_map = map_ref(&format!("{slug}_normal.png"));
            material.orm_map = map_ref(&format!("{slug}_orm.png"));
            material.roughness = 1.0;
            material.metallic = 1.0;
            material.uv_object_scale = false;
            material.height_scale = 0.03;
            engine
                .save_material(&mat_file.to_string_lossy(), &material)
                .map_err(|e| format!("{e:?}"))?;
        }
        Ok(if absolute_ok {
            relative_mat(&format!("{slug}.mat"))
        } else {
            mat_file.display().to_string()
        })
    }

    /// Golden rule: every material in the graph is a real asset. The library, texture and colour
    /// nodes are only moulds: this writes each one's picture and `.mat` into the project and turns
    /// the node into **Your Material** pointing at them, so nothing is left hidden in the UI and
    /// the user can open and change the files (or point the node somewhere else) at will.
    pub(super) fn materialize_nature_assets(&mut self) -> Result<usize, String> {
        if self.nature_dir().is_none() {
            return Ok(0);
        }
        let graph = &self.nature.graph;
        let mut jobs: Vec<(NodeId, MaterialSpec)> = Vec::new();
        for node in &graph.nodes {
            let spec = match node.op {
                "mat_library" | "mat_cards" => {
                    let (labels, key) = if node.op == "mat_library" {
                        (nature::ops::LIBRARY_LABELS, "preset")
                    } else {
                        (nature::ops::CARD_LABELS, "card")
                    };
                    let i = graph.get(node.id, key).unwrap_or(0.0).max(0.0) as usize;
                    // The real material, whatever the quality: low poly only changes the model.
                    nature::ops::spec_for_label(
                        labels[i.min(labels.len() - 1)],
                        nature::Quality::Balanced,
                    )
                }
                "mat_solid" => MaterialSpec::Solid {
                    rgb: [
                        graph.get(node.id, "r").unwrap_or(0.4),
                        graph.get(node.id, "g").unwrap_or(0.4),
                        graph.get(node.id, "b").unwrap_or(0.4),
                    ],
                    roughness: graph.get(node.id, "roughness").unwrap_or(0.8),
                },
                _ => continue,
            };
            jobs.push((node.id, spec));
        }
        let count = jobs.len();
        for (id, spec) in jobs {
            let path = self.ensure_nature_material(&spec, Foliage::STILL)?;
            let atlas = nature::ops::atlas_of(&spec);
            if let Some(node) = self.nature.graph.node_mut(id) {
                node.op = "mat_project";
                node.params = vec![atlas.0 as f32, atlas.1 as f32];
                node.strings = vec![path];
            }
        }
        if count > 0 {
            self.nature.dirty = true;
        }
        Ok(count)
    }

    /// True when a part's material cuts its shape out of a picture (leaves, petals, blades).
    fn material_is_cutout(&self, spec: &MaterialSpec) -> bool {
        match spec {
            MaterialSpec::Cards(_) | MaterialSpec::ProjectTexture(..) => true,
            MaterialSpec::Project(path, atlas) => {
                *atlas != (1, 1)
                    || self.asset_root_dir().is_some_and(|root| {
                        std::fs::read_to_string(root.join(path))
                            .map(|text| text.to_lowercase().contains("cutout"))
                            .unwrap_or(false)
                    })
            }
            _ => false,
        }
    }

    /// The file a material or texture slot uses: a plain path as it is, a recipe path resolved
    /// to the product it bakes (`uikit::recipe_ref`).
    fn slot_product(&self, path: &str, wanted: recipe::Kind) -> Result<String, String> {
        match self.asset_root_dir() {
            Some(root) => recipe_ref::resolve_product(&root, path, wanted),
            None => Ok(path.to_owned()),
        }
    }

    /// A vegetation part (leaves, grass, petals, trunks) that uses a project `.mat` which carries no
    /// wind or translucency of its own gets a **derived copy** with the part's profile, named like
    /// `oak_leaves_w12f60t55.mat` beside the nature materials: the source is never modified. A `.mat`
    /// where you set wind or translucency yourself is used exactly as it is, and so is every part that
    /// does not move (rocks, ground).
    fn with_foliage(&self, product: String, foliage: Foliage) -> Result<String, String> {
        if foliage == Foliage::STILL {
            return Ok(product);
        }
        let engine = self.engine.as_ref().ok_or("The engine is not available")?;
        let source = engine
            .resolve_path(&product)
            .ok()
            .filter(|resolved| Path::new(resolved).exists())
            .or_else(|| engine.asset_path(&product).ok())
            .ok_or_else(|| format!("material `{product}` not found"))?;
        let mut material = engine
            .load_material(&source)
            .map_err(|e| format!("{e:?}"))?;
        if material.wind_strength > 0.0
            || material.wind_flutter > 0.0
            || material.translucency > 0.0
        {
            return Ok(product);
        }
        let root = self.asset_root_dir().ok_or("No project is open")?;
        let folder = asset_folders::ensure(&root, asset_folders::AssetFolder::NatureMaterials)
            .map_err(|e| e.to_string())?;
        let stem = Path::new(&product)
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("material");
        let file = format!("{stem}{}.mat", foliage.suffix());
        let target = folder.join(&file);
        if !target.exists() {
            material.name = format!("{stem}{}", foliage.suffix());
            material.wind_strength = foliage.sway;
            material.wind_speed = 1.0;
            material.wind_flutter = foliage.flutter;
            material.translucency = foliage.translucency;
            engine
                .save_material(&target.to_string_lossy(), &material)
                .map_err(|e| format!("{e:?}"))?;
        }
        let absolute_ok = engine
            .asset_path("")
            .map(|root| !root.is_empty())
            .unwrap_or(false);
        let relative = asset_folders::relative(&root, &target);
        Ok(if absolute_ok {
            relative
        } else {
            target.display().to_string()
        })
    }

    fn ensure_nature_material(
        &mut self,
        spec: &MaterialSpec,
        foliage: Foliage,
    ) -> Result<String, String> {
        // Your own material is used exactly as it is; a `generator.material` recipe stands for
        // the material it baked.
        if let MaterialSpec::Project(path, _) = spec {
            let product = self.slot_product(path, recipe::Kind::Material)?;
            return self.with_foliage(product, foliage);
        }
        let resolved_texture = match spec {
            MaterialSpec::ProjectTexture(path, _) => {
                self.slot_product(path, recipe::Kind::Texture)?
            }
            _ => String::new(),
        };
        let engine = self.engine.as_ref().ok_or("The engine is not available")?;
        let root = self.asset_root_dir().ok_or("No project is open")?;
        let mat_folder = asset_folders::ensure(&root, asset_folders::AssetFolder::NatureMaterials)
            .map_err(|e| e.to_string())?;
        let tex_folder = asset_folders::ensure(&root, asset_folders::AssetFolder::NatureTextures)
            .map_err(|e| e.to_string())?;
        let mat_rel = asset_folders::relative(&root, &mat_folder);
        let tex_rel = asset_folders::relative(&root, &tex_folder);
        let slug = spec.slug();
        let mat_slug = format!("{slug}{}", foliage.suffix());
        let mat_file = mat_folder.join(format!("{mat_slug}.mat"));
        let relative = |file: &str| format!("{tex_rel}/{file}");
        let relative_mat = |file: &str| format!("{mat_rel}/{file}");
        let absolute_ok = engine
            .asset_path("")
            .map(|root| !root.is_empty())
            .unwrap_or(false);
        let map_ref = |file: &str| {
            if absolute_ok {
                engine
                    .asset_project_path(&relative(file))
                    .unwrap_or_else(|_| relative(file))
            } else {
                tex_folder.join(file).display().to_string()
            }
        };
        let mut material = engine.new_material().map_err(|e| format!("{e:?}"))?;
        material.wind_strength = foliage.sway;
        material.wind_speed = 1.0;
        material.wind_flutter = foliage.flutter;
        material.translucency = foliage.translucency;
        material.name = mat_slug.clone();
        // Cards and your own PNGs are always rebuilt (so changing the source updates the model);
        // baked library materials are reused.
        let mut write = true;
        match spec {
            MaterialSpec::Preset(name) => {
                write = !mat_file.exists();
                if write && nature::surface::BarkKind::from_name(name).is_some() {
                    // Generated bark: albedo, normal + height and occlusion/roughness, with
                    // furrows deep enough for parallax and self-shadowing.
                    let kind = nature::surface::BarkKind::from_name(name).expect("checked");
                    let res = 512usize;
                    let set = nature::surface::bark(kind, res, 1);
                    let save = |file: &str, pixels: &[u8]| {
                        image::save_buffer(
                            tex_folder.join(file),
                            pixels,
                            res as u32,
                            res as u32,
                            image::ColorType::Rgba8,
                        )
                        .map_err(|e| e.to_string())
                    };
                    save(&format!("{slug}_albedo.png"), &set.albedo)?;
                    save(&format!("{slug}_normal.png"), &set.normal)?;
                    save(&format!("{slug}_orm.png"), &set.orm)?;
                    material.albedo_map = map_ref(&format!("{slug}_albedo.png"));
                    material.normal_map = map_ref(&format!("{slug}_normal.png"));
                    material.orm_map = map_ref(&format!("{slug}_orm.png"));
                    material.roughness = 1.0;
                    material.metallic = 1.0;
                    material.uv_object_scale = false;
                    material.height_scale = 0.05;
                } else if write {
                    let preset = material_library::presets()
                        .iter()
                        .find(|p| p.name == *name)
                        .ok_or_else(|| format!("unknown material preset `{name}`"))?;
                    let mut graph = preset.build(&material_library::PresetParams::default());
                    graph.resolution = 512;
                    let baked = graph.bake().map_err(|e| format!("{name}: {e}"))?;
                    let file = format!("{slug}_base.png");
                    image::save_buffer(
                        tex_folder.join(&file),
                        &baked.base,
                        512,
                        512,
                        image::ColorType::Rgba8,
                    )
                    .map_err(|e| e.to_string())?;
                    material.albedo_map = map_ref(&file);
                    material.roughness = 0.85;
                }
            }
            MaterialSpec::Cards(name) => {
                write = !mat_file.exists();
                if write {
                    let (pixels, width, height) = texture_cards_png(name, 512)?;
                    let file = format!("{slug}_cards.png");
                    image::save_buffer(
                        tex_folder.join(&file),
                        &pixels,
                        width,
                        height,
                        image::ColorType::Rgba8,
                    )
                    .map_err(|e| e.to_string())?;
                    material.albedo_map = map_ref(&file);
                    // Veins and a rounded blade profile from the card itself.
                    let normal =
                        nature::surface::card_normal(&pixels, width as usize, height as usize);
                    let normal_file = format!("{slug}_cards_n.png");
                    image::save_buffer(
                        tex_folder.join(&normal_file),
                        &normal,
                        width,
                        height,
                        image::ColorType::Rgba8,
                    )
                    .map_err(|e| e.to_string())?;
                    material.normal_map = map_ref(&normal_file);
                    material.normal_strength = 0.9;
                    material.alpha_mode = doppio_engine::MaterialAlphaMode::Cutout;
                    material.alpha_cutoff = 0.45;
                    material.roughness = 0.55;
                    material.uv_object_scale = false;
                }
            }
            MaterialSpec::ProjectTexture(..) => {
                // A cut-out material around your image (or around what a `generator.texture`
                // recipe bakes), referencing it where it is.
                let path = &resolved_texture;
                material.albedo_map = engine
                    .asset_project_path(path)
                    .unwrap_or_else(|_| path.clone());
                material.alpha_mode = doppio_engine::MaterialAlphaMode::Cutout;
                material.alpha_cutoff = 0.45;
                material.roughness = 0.7;
                material.uv_object_scale = false;
            }
            MaterialSpec::Solid { rgb, roughness } => {
                write = !mat_file.exists();
                material.albedo = [rgb[0], rgb[1], rgb[2], 1.0];
                material.roughness = *roughness;
            }
            MaterialSpec::Custom(custom) => {
                // Start from a project material when one was picked: its maps and settings stay
                // unless a Texture layer or the node's own switches change them.
                let mut based = false;
                if !custom.base.is_empty() {
                    let found = engine
                        .resolve_path(&custom.base)
                        .ok()
                        .filter(|resolved| Path::new(resolved).exists())
                        .or_else(|| engine.asset_path(&custom.base).ok());
                    if let Some(Ok(mut loaded)) = found.map(|path| engine.load_material(&path)) {
                        loaded.name = mat_slug.clone();
                        loaded.wind_strength = loaded.wind_strength.max(foliage.sway);
                        loaded.wind_flutter = loaded.wind_flutter.max(foliage.flutter);
                        loaded.translucency = loaded.translucency.max(foliage.translucency);
                        material = loaded;
                        based = true;
                    }
                }
                if !based {
                    material.albedo = [custom.rgb[0], custom.rgb[1], custom.rgb[2], 1.0];
                    material.roughness = custom.roughness;
                    material.metallic = custom.metallic;
                }
                // The Texture nodes' images are summed per map into one set of PNGs.
                let root = engine
                    .asset_path("")
                    .ok()
                    .filter(|root| !root.is_empty())
                    .map(PathBuf::from)
                    // Without an open project, paths are relative to the assets folder `dir` is in.
                    .or_else(|| self.asset_root_dir());
                let load = |relative: &str| -> Option<nature::texlayers::Image> {
                    let path = Path::new(relative);
                    let full = if path.is_absolute() {
                        path.to_path_buf()
                    } else {
                        root.as_ref()?.join(relative)
                    };
                    let image = image::open(full).ok()?.to_rgba8();
                    Some(nature::texlayers::Image {
                        width: image.width() as usize,
                        height: image.height() as usize,
                        rgba: image.into_raw(),
                    })
                };
                let size = 512usize;
                let baked = nature::texlayers::bake(custom, size, &load);
                let save = |suffix: &str, pixels: &Option<Vec<u8>>| -> Result<String, String> {
                    let Some(pixels) = pixels else {
                        return Ok(String::new());
                    };
                    let file = format!("{mat_slug}_{suffix}.png");
                    image::save_buffer(
                        tex_folder.join(&file),
                        pixels,
                        size as u32,
                        size as u32,
                        image::ColorType::Rgba8,
                    )
                    .map_err(|e| e.to_string())?;
                    Ok(map_ref(&file))
                };
                // Only the maps a layer wrote replace the base material's.
                for (suffix, pixels, slot) in [
                    ("albedo", &baked.albedo, &mut material.albedo_map),
                    ("normal", &baked.normal, &mut material.normal_map),
                    ("orm", &baked.orm, &mut material.orm_map),
                ] {
                    let written = save(suffix, pixels)?;
                    if !written.is_empty() {
                        *slot = written;
                    }
                }
                if !material.normal_map.is_empty() {
                    material.normal_strength = custom.normal_strength;
                }
                if !material.orm_map.is_empty() {
                    // The summed map drives roughness and metal.
                    material.roughness = 1.0;
                    material.metallic = 1.0;
                }
                if custom.cutout || baked.has_alpha {
                    material.alpha_mode = doppio_engine::MaterialAlphaMode::Cutout;
                    material.alpha_cutoff = custom.cutoff;
                }
                material.uv_object_scale = false;
            }
            MaterialSpec::Project(..) => unreachable!("handled above"),
        }
        if write {
            engine
                .save_material(&mat_file.to_string_lossy(), &material)
                .map_err(|e| format!("{e:?}"))?;
        }
        Ok(if absolute_ok {
            relative_mat(&format!("{mat_slug}.mat"))
        } else {
            mat_file.display().to_string()
        })
    }

    /// A small scene of rocks, trees and plants for the documentation screenshots and the headless
    /// `nature` fixture. Models are written under the asset root override.
    #[cfg_attr(not(any(test, feature = "headless")), allow(dead_code))]
    pub(crate) fn build_nature_demo(&mut self) {
        let layout: [(&str, f32, f32); 15] = [
            ("Oak", -2.6, -1.0),
            ("Pine", 2.4, -2.2),
            ("Birch", 0.2, -3.0),
            ("Autumn Maple", 5.2, 0.2),
            ("Mossy Rock", -0.6, 1.4),
            ("Boulder", 3.6, 2.0),
            ("Flat Slab", -3.4, 2.4),
            ("Pebble", 0.9, 2.5),
            ("Pebble", 1.3, 2.9),
            ("Grass Clump", -1.5, 2.0),
            ("Tall Grass", 1.8, 1.0),
            ("Fern Patch", -2.0, 0.6),
            ("Shrub", 4.6, 3.2),
            ("Pink Flower", -0.1, 3.3),
            ("White Flower", 0.5, 3.6),
        ];
        // Light and ground first, so the models read like a scene.
        if let Some(index) = crate::uikit::prefabs::PREFABS
            .iter()
            .position(|p| p.label == "Directional Light")
        {
            self.update(super::Message::CreatePrefab(index));
            if let (Some(engine), Some(entity)) =
                (self.engine.as_mut(), self.entities.get(self.selected))
            {
                let _ = engine.set_position(entity.id, [0.0, 8.0, 0.0]);
                let _ = engine.set_rotation(entity.id, [-38.0, 130.0, 0.0]);
            }
        }
        let ground_dir = self.asset_root_dir();
        if let Some(index) = crate::uikit::prefabs::PREFABS
            .iter()
            .position(|p| p.label == "Plane")
        {
            self.update(super::Message::CreatePrefab(index));
            if let (Some(engine), Some(entity)) =
                (self.engine.as_mut(), self.entities.get(self.selected))
            {
                let id = entity.id;
                let _ = engine.set_position(id, [0.0, 0.0, 0.0]);
                let _ = engine.set_scale(id, [30.0, 1.0, 30.0]);
                if let (Some(dir), Ok(mut material)) = (ground_dir.clone(), engine.new_material()) {
                    let path =
                        asset_folders::resolve(&dir, asset_folders::AssetFolder::NatureMaterials)
                            .join("ground.mat");
                    let _ = std::fs::create_dir_all(path.parent().unwrap());
                    material.albedo = [0.22, 0.34, 0.15, 1.0];
                    material.roughness = 0.95;
                    material.uv_object_scale = false;
                    if engine
                        .save_material(&path.to_string_lossy(), &material)
                        .is_ok()
                    {
                        let _ = engine.apply_material(id, &path.to_string_lossy());
                    }
                }
            }
        }
        for (name, x, z) in layout {
            let Some(index) = self.nature.presets.iter().position(|p| p.name == name) else {
                continue;
            };
            self.edit_nature(Edit::ApplyPreset(index));
            self.camera.focus = [x, 0.0, z];
            self.place_nature();
        }
        self.camera = doppio_engine::Camera {
            yaw: 0.9,
            pitch: 0.38,
            distance: 21.0,
            focus: [0.8, 1.5, 0.4],
        };
        let oak = self
            .nature
            .presets
            .iter()
            .position(|p| p.name == "Oak")
            .unwrap_or(0);
        self.edit_nature(Edit::ApplyPreset(oak));
        self.nature.status = None;
    }

    /// Writes the model as a **composite scatter class** into the project's default plan: the solid parts and the
    /// cut-out parts become two merged meshes with their material slots, and the class places both on every instance.
    /// Spacing and size come from the model's footprint; the terms (where) start as gentle slopes and are the
    /// plan's to edit.
    pub(super) fn add_nature_to_scatter_plan(&mut self) {
        if self.engine.is_none() {
            self.nature.status = Some("The engine is not available".to_owned());
            return;
        }
        self.ensure_workspace();
        let Some(dir) = self.nature_dir() else {
            self.nature.status =
                Some("Open a project first: models are written to its assets folder".to_owned());
            return;
        };
        if let Err(error) = self.materialize_nature_assets() {
            self.nature.status = Some(format!("Could not write the materials: {error}"));
            return;
        }
        let name = self.nature.name.clone();
        let model = match self.nature.graph.build(self.nature.quality) {
            Ok(model) => model,
            Err(error) => {
                self.nature.status = Some(format!("Graph: {error}"));
                return;
            }
        };
        let root = self.asset_root_dir();
        let relative_dir = root
            .as_ref()
            .map(|root| asset_folders::relative(root, &dir))
            .unwrap_or_default();
        let as_project_path = |path: &str| -> String {
            match &root {
                Some(root) => asset_folders::relative(root, std::path::Path::new(path)),
                None => path.to_owned(),
            }
        };
        let mut parts_lines = Vec::new();
        for (label, want_cutout) in [("solid", false), ("foliage", true)] {
            let members: Vec<usize> = (0..model.parts.len())
                .filter(|&i| {
                    let part = &model.parts[i];
                    !part.is_empty()
                        && part
                            .instances
                            .as_ref()
                            .is_none_or(|set| set.transforms.is_empty())
                        && self.material_is_cutout(&part.material) == want_cutout
                })
                .collect();
            if members.is_empty() {
                continue;
            }
            let file = format!("{}_{label}.obj", nature::slug(&name));
            if let Err(error) = std::fs::create_dir_all(&dir).and_then(|_| {
                std::fs::write(
                    dir.join(&file),
                    model.merged_obj(&members, &format!("{name} {label}")),
                )
            }) {
                self.nature.status = Some(format!("Could not write the meshes: {error}"));
                return;
            }
            let slots: Vec<String> = members
                .iter()
                .map(|&i| {
                    let part = &model.parts[i];
                    let made = match &part.surface {
                        Some(spec) => self.ensure_surface_material(part, spec),
                        None => self.ensure_nature_material(
                            &part.material,
                            Foliage::for_spec(&part.name, &part.material),
                        ),
                    };
                    made.map(|path| as_project_path(&path)).unwrap_or_default()
                })
                .collect();
            let triangles: usize = members
                .iter()
                .map(|&i| model.parts[i].triangle_count())
                .sum();
            let micromesh =
                self.nature.micromesh && !want_cutout && triangles >= MICROMESH_MIN_TRIANGLES;
            parts_lines.push(format!(
                "  part mesh {relative_dir}/{file} slots {}{}{}",
                slots.join(";"),
                if micromesh { " micromesh on" } else { "" },
                if want_cutout { " cutout on" } else { "" },
            ));
        }
        if parts_lines.is_empty() {
            self.nature.status = Some("The model has no parts to scatter".to_owned());
            return;
        }
        let (plan_file, text) = match self.default_scatter_plan() {
            Ok(found) => found,
            Err(error) => {
                self.nature.status = Some(format!("Could not read the scatter plan: {error}"));
                return;
            }
        };
        let next_id = text
            .lines()
            .filter_map(|line| {
                let words: Vec<&str> = line.split('#').next()?.split_whitespace().collect();
                (words.first() == Some(&"class")).then(|| words.get(1)?.parse::<u32>().ok())?
            })
            .max()
            .map_or(1, |id| id + 1);
        // The footprint decides the spacing: a crown of 6 m wants about 3 m of room each side.
        let (lo, hi) = model.bounds();
        let radius = (0.4 * (hi[0] - lo[0]).max(hi[2] - lo[2])).clamp(0.4, 12.0);
        let class_name = nature::slug(&name);
        let mut block = format!(
            "\n# Added from the Nature generator ({name})\nclass {next_id} name {class_name} cell {:.1} radius {radius:.1} density 0.8 priority 20 scale 0.8 1.3 tilt 0.1 tint 0.08\n",
            (radius * 3.0).max(2.0)
        );
        for line in &parts_lines {
            block.push_str(line);
            block.push('\n');
        }
        block.push_str("  slope 0.00 0.00 0.15 0.35\n");
        let mut updated = text;
        if !updated.ends_with('\n') {
            updated.push('\n');
        }
        updated.push_str(&block);
        if crate::uikit::scatter::ScatterPlan::from_text(&updated).is_err() {
            self.nature.status =
                Some("The scatter plan did not parse after the addition".to_owned());
            return;
        }
        if let Err(error) = std::fs::write(&plan_file, updated) {
            self.nature.status = Some(format!("Could not write the scatter plan: {error}"));
            return;
        }
        self.nature.status = Some(format!(
            "Added class {next_id} ({class_name}) to Scatter/default.scatter: use Scatter in the Terrain tab, or edit the plan"
        ));
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.refresh_assets();
            self.assets = engine.assets();
        }
    }

    /// Creates the model in the scene: a group entity with one child per part.
    pub(super) fn place_nature(&mut self) {
        self.ensure_workspace();
        let Some(dir) = self.nature_dir() else {
            self.nature.status =
                Some("Open a project first: models are written to its assets folder".to_owned());
            return;
        };
        if self.engine.is_none() {
            self.nature.status = Some("The engine is not available".to_owned());
            return;
        }
        let name = self.nature.name.clone();
        let model = match self.nature.graph.build(self.nature.quality) {
            Ok(model) => model,
            Err(error) => {
                self.nature.status = Some(format!("Graph: {error}"));
                return;
            }
        };
        let files = match nature::write_obj_files(&model, &dir, &name) {
            Ok(files) => files,
            Err(error) => {
                self.nature.status = Some(format!("Could not write the meshes: {error}"));
                return;
            }
        };
        let materials: Vec<Result<String, String>> = model
            .parts
            .iter()
            .map(|part| match &part.surface {
                Some(spec) => self.ensure_surface_material(part, spec),
                None => self.ensure_nature_material(
                    &part.material,
                    Foliage::for_spec(&part.name, &part.material),
                ),
            })
            .collect();
        let focus = self.camera.focus;
        let absolute_ok = self
            .engine
            .as_ref()
            .and_then(|e| e.asset_path("").ok())
            .is_some_and(|root| !root.is_empty());
        let models_rel = self
            .asset_root_dir()
            .map(|root| asset_folders::relative(&root, &dir))
            .unwrap_or_default();
        let cutouts: Vec<bool> = model
            .parts
            .iter()
            .map(|part| self.material_is_cutout(&part.material))
            .collect();
        let engine = self.engine.as_mut().expect("checked above");
        let group = match engine.create_entity(&name) {
            Ok(id) => id,
            Err(error) => {
                self.nature.status = Some(format!("Could not create the group: {error:?}"));
                return;
            }
        };
        let _ = engine.set_position(group, [focus[0], 0.0, focus[2]]);
        let mut problems = Vec::new();
        let mesh_path = |file: &str| {
            if absolute_ok {
                format!("{models_rel}/{file}")
            } else {
                dir.join(file).display().to_string()
            }
        };
        // Parts that are not copies of a small mesh and share a kind of surface (solid, or cut-out cards) become one
        // entity: one OBJ with a group per part, and a Material slot for each, so a tree is a trunk entity and a
        // foliage entity instead of a pile of parts.
        let mut merged = vec![false; model.parts.len()];
        if self.nature.merge_parts {
            for (label, want_cutout) in [("solid", false), ("foliage", true)] {
                let members: Vec<usize> = (0..model.parts.len())
                    .filter(|&i| {
                        let part = &model.parts[i];
                        !part.is_empty()
                            && part
                                .instances
                                .as_ref()
                                .is_none_or(|set| set.transforms.is_empty())
                            && cutouts[i] == want_cutout
                    })
                    .collect();
                if members.len() < 2 {
                    continue;
                }
                let file = format!("{}_{label}.obj", nature::slug(&name));
                if let Err(error) = std::fs::write(
                    dir.join(&file),
                    model.merged_obj(&members, &format!("{name} {label}")),
                ) {
                    problems.push(format!("{label}: {error}"));
                    continue;
                }
                let Ok(child) = engine.create_entity(&format!("{name} {label}")) else {
                    continue;
                };
                let _ = engine.set_entity_parent(child, Some(group));
                let triangles: usize = members
                    .iter()
                    .map(|&i| model.parts[i].triangle_count())
                    .sum();
                let slots: Vec<&Result<String, String>> =
                    members.iter().map(|&i| &materials[i]).collect();
                let names: Vec<&str> = members
                    .iter()
                    .map(|&i| model.parts[i].name.as_str())
                    .collect();
                build_merged_entity(
                    engine,
                    child,
                    MeshLook {
                        cutout: want_cutout,
                        micromesh: self.nature.micromesh
                            && !want_cutout
                            && triangles >= MICROMESH_MIN_TRIANGLES,
                    },
                    &mesh_path(&file),
                    &slots,
                    &names,
                    &mut problems,
                );
                for i in members {
                    merged[i] = true;
                }
            }
        }
        for ((((index, part), file), material), cutout) in model
            .parts
            .iter()
            .enumerate()
            .zip(files.iter())
            .zip(materials.iter())
            .zip(cutouts.iter())
        {
            if merged[index] {
                continue;
            }
            let micromesh = self.nature.micromesh
                && !*cutout
                && part.triangle_count() >= MICROMESH_MIN_TRIANGLES;
            match &part.instances {
                // Copies of one small mesh: write the template once and make one entity per copy.
                // They share mesh and material, so the renderer batches them into one instanced
                // draw call, and every copy gets its own wind phase from its position.
                Some(set) if !set.transforms.is_empty() => {
                    let template_file =
                        format!("{}_{}_instance.obj", nature::slug(&name), part.name);
                    if let Err(error) =
                        std::fs::write(dir.join(&template_file), set.template.to_obj())
                    {
                        problems.push(format!("{}: {error}", part.name));
                        continue;
                    }
                    let mesh_ref = mesh_path(&template_file);
                    let Ok(holder) = engine.create_entity(&format!("{name} {}", part.name)) else {
                        continue;
                    };
                    let _ = engine.set_entity_parent(holder, Some(group));
                    for (index, transform) in set.transforms.iter().enumerate() {
                        let Ok(child) =
                            engine.create_entity(&format!("{name} {} {}", part.name, index + 1))
                        else {
                            continue;
                        };
                        let _ = engine.set_entity_parent(child, Some(holder));
                        let _ = engine.set_position(child, transform.position);
                        let _ = engine.set_rotation(child, transform.euler_degrees);
                        let s = transform.scale;
                        let _ = engine.set_scale(child, [s, s, s]);
                        build_part_entity(
                            engine,
                            child,
                            MeshLook {
                                cutout: *cutout,
                                micromesh: false,
                            },
                            &mesh_ref,
                            material,
                            &part.name,
                            &mut problems,
                        );
                    }
                }
                _ => {
                    let Ok(child) = engine.create_entity(&format!("{name} {}", part.name)) else {
                        continue;
                    };
                    let _ = engine.set_entity_parent(child, Some(group));
                    build_part_entity(
                        engine,
                        child,
                        MeshLook {
                            cutout: *cutout,
                            micromesh,
                        },
                        &mesh_path(file),
                        material,
                        &part.name,
                        &mut problems,
                    );
                }
            }
        }
        if let Some(engine) = self.engine.as_ref() {
            self.entities = super::projectloader::read_entities(engine);
        }
        if let Some(index) = self.entities.iter().position(|entity| entity.id == group) {
            self.select_index(index);
        }
        self.nature.dirty = false;
        self.nature.status = if problems.is_empty() {
            Some(format!(
                "Placed {name} ({} part{})",
                model.parts.len(),
                if model.parts.len() == 1 { "" } else { "s" }
            ))
        } else {
            Some(format!("Placed with problems: {}", problems.join("; ")))
        };
        self.refresh_components();
        self.commit("Place nature model", None);
        self.log(LogLevel::Action, format!("Placed nature model {name}"));
    }
}

#[allow(dead_code)]
fn _vector_is_used(_: Vector) {}

/// A solid part below this many triangles is not worth a cluster hierarchy (see `micromeshes.md`).
const MICROMESH_MIN_TRIANGLES: usize = 3000;

/// How a placed mesh draws: cut-out cards (no shadows of their own) and Micromesh (cluster LOD).
#[derive(Clone, Copy)]
struct MeshLook {
    cutout: bool,
    micromesh: bool,
}

/// The longest text the engine's field transport carries.
const MAX_SLOT_TEXT: usize = 250;

/// Adds the Mesh Filter (primitive Custom, the file) and the Mesh Renderer (shadows, Micromesh) to an entity.
/// Returns the component type ids of the filter, for the callers that set more on it.
fn add_mesh_components(
    engine: &mut doppio_engine::Engine,
    child: u32,
    look: MeshLook,
    mesh_ref: &str,
    problems: &mut Vec<String>,
) -> Option<u32> {
    let MeshLook { cutout, micromesh } = look;
    // The renderer draws a custom mesh from the Mesh Filter (primitive Custom + path); the Mesh
    // Renderer carries the shadow flags and the Micromesh switch.
    let mut filter_type = None;
    match add_component(engine, child, "Mesh Filter") {
        Ok(filter) => {
            filter_type = Some(filter);
            let outcome = super::enum_field(engine, "Mesh Filter", "Primitive", "Custom")
                .and_then(|value| set_component_field(engine, child, filter, "Primitive", value))
                .and_then(|_| {
                    set_component_field(
                        engine,
                        child,
                        filter,
                        "Mesh",
                        doppio_engine::FieldValue::String(mesh_ref.to_owned()),
                    )
                });
            if let Err(error) = outcome {
                problems.push(error);
            }
        }
        Err(error) => problems.push(error),
    }
    match add_component(engine, child, "Mesh Renderer") {
        Ok(renderer) => {
            // Cut-out cards (leaves, petals, blades) would only shadow each other: let the sun
            // fall through them and keep the shadows for trunks, rocks and moss.
            if cutout {
                let _ = set_component_field(
                    engine,
                    child,
                    renderer,
                    "Cast shadows",
                    doppio_engine::FieldValue::Bool(false),
                );
            }
            if micromesh {
                let _ = set_component_field(
                    engine,
                    child,
                    renderer,
                    "Micromesh",
                    doppio_engine::FieldValue::Bool(true),
                );
            }
        }
        Err(error) => problems.push(error),
    }
    filter_type
}

/// Gives an entity its mesh and its material. Cut-out cards cast alpha-tested shadows: the engine reads
/// the albedo's alpha, so nothing special is needed here.
fn build_part_entity(
    engine: &mut doppio_engine::Engine,
    child: u32,
    look: MeshLook,
    mesh_ref: &str,
    material: &Result<String, String>,
    part_name: &str,
    problems: &mut Vec<String>,
) {
    add_mesh_components(engine, child, look, mesh_ref, problems);
    match material {
        Ok(path) => {
            if let Err(error) = engine.apply_material(child, path) {
                problems.push(format!("{part_name}: {error:?}"));
            }
        }
        Err(error) => problems.push(format!("{part_name}: {error}")),
    }
}

/// One entity for several parts (`NatureModel::merged_obj`): the mesh has a sub-mesh per part and the Mesh
/// Filter's **Material slots** give each its own material, in the same order. The first usable material is also the
/// entity's own, which an empty slot falls back to.
fn build_merged_entity(
    engine: &mut doppio_engine::Engine,
    child: u32,
    look: MeshLook,
    mesh_ref: &str,
    materials: &[&Result<String, String>],
    part_names: &[&str],
    problems: &mut Vec<String>,
) {
    let filter = add_mesh_components(engine, child, look, mesh_ref, problems);
    // Applying a material stores it as a project path; read each back so the slots hold the same form.
    let mut slots = Vec::new();
    let mut first = None;
    for (material, part) in materials.iter().zip(part_names) {
        match material {
            Ok(path) => match engine
                .apply_material(child, path)
                .and_then(|_| engine.entity_material(child))
            {
                Ok(stored) => {
                    first.get_or_insert_with(|| stored.clone());
                    slots.push(stored);
                }
                Err(error) => {
                    problems.push(format!("{part}: {error:?}"));
                    slots.push(String::new());
                }
            },
            Err(error) => {
                problems.push(format!("{part}: {error}"));
                slots.push(String::new());
            }
        }
    }
    if let Some(first) = first {
        let _ = engine.apply_material(child, &first);
    }
    // A text field holds 255 bytes: keep the slots that fit whole, say so for the rest (they use the entity's own).
    let mut joined = String::new();
    for (index, slot) in slots.iter().enumerate() {
        let extra = slot.len() + usize::from(index > 0);
        if joined.len() + extra > MAX_SLOT_TEXT {
            problems.push(format!(
                "{} parts did not fit the material slot list",
                slots.len() - index
            ));
            break;
        }
        if index > 0 {
            joined.push(';');
        }
        joined.push_str(slot);
    }
    if let Some(filter) = filter {
        if let Err(error) = set_component_field(
            engine,
            child,
            filter,
            "Material slots",
            doppio_engine::FieldValue::String(joined),
        ) {
            problems.push(error);
        }
    }
}

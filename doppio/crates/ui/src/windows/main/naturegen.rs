//! Editor state and message handling of the **Nature Generator** pane (rocks, trees, plants).
//!
//! A model is a node graph (`uikit::nature`). This module owns the graph being edited with an undo
//! history, renders the preview, writes `.obj` files and places the result in the scene as a group
//! of entities, one per part, each with its own material (library presets, Texture Generator cards,
//! plain colours, or **your own project materials and textures**).

use super::{add_component, set_component_field, App, LogLevel, PaneKind};
use crate::uikit::generators::TextureMap;
use crate::uikit::material_library;
use crate::uikit::nature::mesh::MaterialSpec;
use crate::uikit::nature::{self, preview, NatureGraph, NodeId, Quality};
use crate::uikit::panels::nature_generator::{self as panel, Edit};
use crate::uikit::texture_graph;
use iced::widget::image::Handle;
use iced::{Point, Vector};
use std::collections::HashSet;
use std::path::{Path, PathBuf};

const PREVIEW_SIZE: u32 = 330;

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
    pub search: String,
    pub collapsed: HashSet<String>,
    pub presets: Vec<nature::NaturePreset>,
    pub preset_labels: Vec<String>,
    pub preview: Option<Handle>,
    pub view: preview::View,
    pub stats: String,
    pub status: Option<String>,
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
            search: String::new(),
            collapsed: HashSet::new(),
            presets,
            preset_labels,
            preview: None,
            view: preview::View::default(),
            stats: String::new(),
            status: None,
            dirty: false,
            orbit: None,
        };
        state.fit();
        state.refresh();
        state
    }

    /// Replaces the graph (preset or new) and starts a fresh history.
    fn load(&mut self, graph: NatureGraph) {
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
        let zoom = (620.0 / (max_x - min_x).max(1.0))
            .min(380.0 / (max_y - min_y).max(1.0))
            .clamp(0.3, 1.0);
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

    pub fn view_state(&self, materials: Vec<String>, textures: Vec<String>) -> panel::View<'_> {
        panel::View {
            graph: &self.graph,
            selected: self.selected,
            selected_link: self.link,
            pan: self.pan,
            zoom: self.zoom,
            snap: self.snap,
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
        self.asset_root_dir().map(|root| root.join("nature"))
    }

    fn export_nature(&mut self) {
        let Some(dir) = self.nature_dir() else {
            self.nature.status =
                Some("Open a project first: models are written to its assets folder".to_owned());
            return;
        };
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
                let recipe = dir.join(format!("{}.{}", nature::slug(&name), nature::EXTENSION));
                let _ = std::fs::write(
                    &recipe,
                    nature::to_text(&name, &self.nature.graph, self.nature.quality),
                );
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
        dir: &Path,
    ) -> Result<String, String> {
        let engine = self.engine.as_ref().ok_or("The engine is not available")?;
        let folder = dir.join("materials");
        std::fs::create_dir_all(&folder).map_err(|e| e.to_string())?;
        let slug = format!(
            "{}_{}",
            part.name.to_lowercase().replace(' ', "_"),
            spec.slug()
        );
        let mat_file = folder.join(format!("{slug}.mat"));
        let relative = |file: &str| format!("nature/materials/{file}");
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
                folder.join(file).display().to_string()
            }
        };
        if !mat_file.exists() {
            let res = spec.resolution as usize;
            let maps = nature::bake::bake(part, res);
            let set = nature::surface::rock(&maps, spec);
            let save = |file: &str, pixels: &[u8]| {
                image::save_buffer(
                    folder.join(file),
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
            relative(&format!("{slug}.mat"))
        } else {
            mat_file.display().to_string()
        })
    }

    fn ensure_nature_material(
        &mut self,
        spec: &MaterialSpec,
        dir: &Path,
    ) -> Result<String, String> {
        // Your own material is used exactly as it is.
        if let MaterialSpec::Project(path) = spec {
            return Ok(path.clone());
        }
        let engine = self.engine.as_ref().ok_or("The engine is not available")?;
        let folder = dir.join("materials");
        std::fs::create_dir_all(&folder).map_err(|e| e.to_string())?;
        let slug = spec.slug();
        let mat_file = folder.join(format!("{slug}.mat"));
        let relative = |file: &str| format!("nature/materials/{file}");
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
                folder.join(file).display().to_string()
            }
        };
        let mut material = engine.new_material().map_err(|e| format!("{e:?}"))?;
        material.name = slug.clone();
        // Cards and your own PNGs are always rebuilt (so changing the source updates the model);
        // baked library materials are reused.
        let mut write = true;
        match spec {
            MaterialSpec::Preset(name) => {
                write = !mat_file.exists();
                if write {
                    let preset = material_library::presets()
                        .iter()
                        .find(|p| p.name == *name)
                        .ok_or_else(|| format!("unknown material preset `{name}`"))?;
                    let mut graph = preset.build(&material_library::PresetParams::default());
                    graph.resolution = 512;
                    let baked = graph.bake().map_err(|e| format!("{name}: {e}"))?;
                    let file = format!("{slug}_base.png");
                    image::save_buffer(
                        folder.join(&file),
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
                        folder.join(&file),
                        &pixels,
                        width,
                        height,
                        image::ColorType::Rgba8,
                    )
                    .map_err(|e| e.to_string())?;
                    material.albedo_map = map_ref(&file);
                    material.alpha_mode = doppio_engine::MaterialAlphaMode::Cutout;
                    material.alpha_cutoff = 0.45;
                    material.roughness = 0.7;
                    material.uv_object_scale = false;
                }
            }
            MaterialSpec::ProjectTexture(path) => {
                // A cut-out material around your image, referencing it where it is.
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
            MaterialSpec::Project(_) => unreachable!("handled above"),
        }
        if write {
            engine
                .save_material(&mat_file.to_string_lossy(), &material)
                .map_err(|e| format!("{e:?}"))?;
        }
        Ok(if absolute_ok {
            relative(&format!("{slug}.mat"))
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
                    let path = dir.join("nature").join("ground.mat");
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

    /// Creates the model in the scene: a group entity with one child per part.
    pub(super) fn place_nature(&mut self) {
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
                Some(spec) => self.ensure_surface_material(part, spec, &dir),
                None => self.ensure_nature_material(&part.material, &dir),
            })
            .collect();
        let focus = self.camera.focus;
        let absolute_ok = self
            .engine
            .as_ref()
            .and_then(|e| e.asset_path("").ok())
            .is_some_and(|root| !root.is_empty());
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
        for ((part, file), material) in model.parts.iter().zip(files.iter()).zip(materials.iter()) {
            let Ok(child) = engine.create_entity(&format!("{name} {}", part.name)) else {
                continue;
            };
            let _ = engine.set_entity_parent(child, Some(group));
            let mesh_ref = if absolute_ok {
                format!("nature/{file}")
            } else {
                dir.join(file).display().to_string()
            };
            // The renderer draws a custom mesh from the Mesh Filter (primitive Custom + path); the
            // Mesh Renderer carries the shadow flags and is where materials are read from.
            match add_component(engine, child, "Mesh Filter") {
                Ok(filter) => {
                    let outcome = super::enum_field(engine, "Mesh Filter", "Primitive", "Custom")
                        .and_then(|value| {
                            set_component_field(engine, child, filter, "Primitive", value)
                        })
                        .and_then(|_| {
                            set_component_field(
                                engine,
                                child,
                                filter,
                                "Mesh",
                                doppio_engine::FieldValue::String(mesh_ref.clone()),
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
                    // Cut-out cards (leaves, petals, blades) would only shadow each other: let the
                    // sun fall through them and keep the shadows for trunks, rocks and moss.
                    if matches!(
                        part.material,
                        MaterialSpec::Cards(_) | MaterialSpec::ProjectTexture(_)
                    ) {
                        let _ = set_component_field(
                            engine,
                            child,
                            renderer,
                            "Cast shadows",
                            doppio_engine::FieldValue::Bool(false),
                        );
                    }
                }
                Err(error) => problems.push(error),
            }
            match material {
                Ok(path) => {
                    if let Err(error) = engine.apply_material(child, path) {
                        problems.push(format!("{}: {error:?}", part.name));
                    }
                }
                Err(error) => problems.push(format!("{}: {error}", part.name)),
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

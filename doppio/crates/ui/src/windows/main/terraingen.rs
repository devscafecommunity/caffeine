//! Editor state and message handling of the **Terrain Generator** pane (lakes, rivers, seas).
//!
//! A water body is a node graph (`uikit::water`). This module owns the graph being edited with an
//! undo history, renders the animated top-down preview, saves `.water` files and places the result
//! in the scene as an engine water surface (`caffeine_session_set_water`, C ABI v7).

use super::{App, LogLevel, PaneKind};
use crate::uikit::panels::terrain_generator::{self as panel, Edit};
use crate::uikit::terrain::{self, preview, NodeId, TerrainGraph, TerrainModel};
use iced::widget::image::Handle;
use iced::{Point, Vector};
use std::collections::HashSet;
use std::path::PathBuf;

const PREVIEW_SIZE: u32 = 330;
/// Grid used for the live preview; the Output node's resolution is used when placing.
const PREVIEW_RES: usize = 129;

pub(super) struct TerrainGen {
    pub graph: TerrainGraph,
    undo: Vec<(TerrainGraph, Option<String>)>,
    cursor: usize,
    pub name: String,
    pub selected: Option<NodeId>,
    pub link: Option<(NodeId, usize)>,
    pub pan: Vector,
    pub zoom: f32,
    pub snap: bool,
    pub search: String,
    pub collapsed: HashSet<String>,
    pub presets: Vec<terrain::TerrainPreset>,
    pub preset_labels: Vec<String>,
    pub preview: Option<Handle>,
    pub stats: String,
    pub status: Option<String>,
    /// The terrain entity Place made last: what the Scatter card scatters on (the selection wins when it is a terrain).
    pub placed: Option<u32>,
    pub dirty: bool,
    pub view: preview::View,
    orbit: Option<Point>,
    /// The evaluated terrain at preview resolution, cached for orbiting.
    pub model: Option<TerrainModel>,
}

impl TerrainGen {
    pub fn new() -> Self {
        let presets = terrain::presets();
        let first = presets
            .iter()
            .position(|p| p.name == "Alpine Range")
            .unwrap_or(0);
        let preset_labels = presets
            .iter()
            .map(|p| format!("{} · {}", p.category, p.name))
            .collect();
        let graph = presets[first].graph();
        let mut state = TerrainGen {
            undo: vec![(graph.clone(), None)],
            cursor: 0,
            graph,
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
            stats: String::new(),
            status: None,
            placed: None,
            dirty: false,
            view: preview::View::default(),
            orbit: None,
            model: None,
        };
        state.fit();
        state.refresh();
        state
    }

    pub(super) fn load(&mut self, graph: TerrainGraph) {
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

    /// Re-evaluates the graph at preview resolution and redraws the preview.
    pub fn refresh(&mut self) {
        match self.graph.build(PREVIEW_RES) {
            Ok(model) => {
                let (lo, hi) = model.height.min_max();
                self.stats = format!(
                    "{:.0} × {:.0} m · {:.0}-{:.0} m high · {}²",
                    model.size[0],
                    model.size[1],
                    lo * model.max_height,
                    hi * model.max_height,
                    self.graph.output_resolution()
                );
                if self
                    .status
                    .as_deref()
                    .is_some_and(|s| s.starts_with("Graph:"))
                {
                    self.status = None;
                }
                self.model = Some(model);
                self.redraw();
            }
            Err(error) => {
                self.stats = "no terrain".to_owned();
                self.status = Some(format!("Graph: {error}"));
                self.model = None;
                self.preview = None;
            }
        }
    }

    /// Redraws the preview from the cached model (orbiting does not re-evaluate the graph).
    pub fn redraw(&mut self) {
        if let Some(model) = &self.model {
            let sea = model.sea_metres / model.max_height;
            let pixels = preview::render(model, sea, PREVIEW_SIZE, PREVIEW_SIZE, self.view);
            self.preview = Some(Handle::from_rgba(PREVIEW_SIZE, PREVIEW_SIZE, pixels));
        }
    }

    pub fn view_state(&self, textures: Vec<String>) -> panel::View<'_> {
        panel::View {
            project_textures: textures,
            graph: &self.graph,
            selected: self.selected,
            selected_link: self.link,
            pan: self.pan,
            zoom: self.zoom,
            snap: self.snap,
            search: &self.search,
            collapsed_groups: &self.collapsed,
            name: &self.name,
            presets: &self.preset_labels,
            preview: self.preview.as_ref(),
            stats: &self.stats,
            status: self.status.as_deref(),
            dirty: self.dirty,
        }
    }
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
    pub(super) fn show_terrain_pane(&mut self) {
        self.panes.restore();
        let mut target = None;
        for (pane, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::TerrainGenerator)
            {
                group.active = index;
                target = Some(*pane);
            }
        }
        if target.is_none() {
            for (pane, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Viewport) {
                    group.pages.push(PaneKind::TerrainGenerator);
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

    pub(super) fn terrain_dir(&self) -> Option<PathBuf> {
        self.asset_root_dir().map(|root| root.join("Terrain"))
    }

    pub(super) fn edit_terrain(&mut self, edit: Edit) {
        let snap = self.terraingen.snap;
        match edit {
            Edit::Select(selected) => {
                self.terraingen.selected = selected;
                self.terraingen.link = None;
            }
            Edit::SelectLink(link) => {
                self.terraingen.link = link;
                self.terraingen.selected = None;
            }
            Edit::DeleteLink(to, input) | Edit::DetachLink(to, input) => {
                self.terraingen
                    .graph
                    .links
                    .retain(|l| !(l.to == to && l.input == input));
                self.terraingen.link = None;
                self.terraingen.push(None);
                self.terraingen.refresh();
            }
            Edit::Connect {
                from,
                output,
                to,
                input,
            } => {
                if self.terraingen.graph.connect(from, output, to, input) {
                    self.terraingen.status = None;
                    self.terraingen.link = Some((to, input));
                    self.terraingen.push(None);
                    self.terraingen.refresh();
                } else {
                    self.terraingen.status = Some("Those ports cannot connect: the colours must match and the graph cannot loop".to_owned());
                }
            }
            Edit::MoveNode(id, delta) => {
                if let Some(node) = self.terraingen.graph.node_mut(id) {
                    node.position =
                        Point::new(node.position.x + delta.x, node.position.y + delta.y);
                }
                self.terraingen.push(Some(format!("move:{id}")));
            }
            Edit::Viewport { pan, zoom } => {
                self.terraingen.pan = pan;
                self.terraingen.zoom = zoom.clamp(0.3, 2.0);
            }
            Edit::AddNode(op) => {
                if op == "output" {
                    if let Some(output) = self.terraingen.graph.output_node() {
                        self.terraingen.selected = Some(output);
                        return;
                    }
                }
                let at = Point::new(
                    (260.0 - self.terraingen.pan.x) / self.terraingen.zoom,
                    (160.0 - self.terraingen.pan.y) / self.terraingen.zoom
                        + self.terraingen.graph.nodes.len() as f32 * 6.0,
                );
                let id = self.terraingen.graph.add(op, snap_position(at, snap));
                self.terraingen.selected = Some(id);
                self.terraingen.link = None;
                self.terraingen.push(None);
                self.terraingen.refresh();
            }
            Edit::DeleteNode(id) => {
                if self
                    .terraingen
                    .graph
                    .node(id)
                    .is_some_and(|n| n.op == "output")
                {
                    self.terraingen.status = Some(
                        "The Output node is the graph's result and cannot be deleted".to_owned(),
                    );
                    return;
                }
                self.terraingen.graph.remove(id);
                if self.terraingen.selected == Some(id) {
                    self.terraingen.selected = None;
                }
                self.terraingen.link = None;
                self.terraingen.push(None);
                self.terraingen.refresh();
            }
            Edit::SetParam(id, index, value) => {
                if let Some(node) = self.terraingen.graph.node_mut(id) {
                    if let Some(spec) = node.def().params.get(index) {
                        let v = if matches!(spec.kind, crate::uikit::procedural::ParamKind::Float) {
                            value
                        } else {
                            value.round()
                        };
                        node.params[index] = v.clamp(spec.min, spec.max);
                    }
                }
                self.terraingen.push(Some(format!("param:{id}:{index}")));
                self.terraingen.refresh();
            }
            Edit::Undo => {
                if self.terraingen.cursor > 0 {
                    self.terraingen.cursor -= 1;
                    self.terraingen.graph = self.terraingen.undo[self.terraingen.cursor].0.clone();
                    self.terraingen.refresh();
                }
            }
            Edit::Redo => {
                if self.terraingen.cursor + 1 < self.terraingen.undo.len() {
                    self.terraingen.cursor += 1;
                    self.terraingen.graph = self.terraingen.undo[self.terraingen.cursor].0.clone();
                    self.terraingen.refresh();
                }
            }
            Edit::Fit => self.terraingen.fit(),
            Edit::SetSnap(on) => self.terraingen.snap = on,
            Edit::SetSearch(value) => self.terraingen.search = value,
            Edit::TogglePaletteGroup(group) => {
                if !self.terraingen.collapsed.remove(&group) {
                    self.terraingen.collapsed.insert(group);
                }
            }
            Edit::ApplyPreset(index) => {
                if let Some(preset) = self.terraingen.presets.get(index) {
                    let graph = preset.graph();
                    self.terraingen.name = preset.name.to_owned();
                    self.terraingen.load(graph);
                    self.terraingen.dirty = false;
                    // A preset is only a mould: its layer pictures become project assets now.
                    if let Err(error) = self.materialize_terrain_assets() {
                        self.terraingen.status = Some(error);
                    }
                }
            }
            Edit::SetString(id, index, text) => {
                self.terraingen.graph.set_string(id, index, text);
                self.terraingen.push(Some(format!("string:{id}:{index}")));
                self.terraingen.refresh();
            }
            Edit::PickAsset(id, index) => {
                let current = self
                    .terraingen
                    .graph
                    .node(id)
                    .and_then(|node| node.strings.get(index).cloned())
                    .unwrap_or_default();
                self.open_asset_pick(
                    super::assetpick::PickTarget::TerrainString { node: id, index },
                    vec![crate::uikit::asset_kind::AssetKind::Image],
                    "Terrain layer picture".to_owned(),
                    current,
                );
            }
            Edit::SetName(name) => self.terraingen.name = name,
            Edit::Scatter => self.scatter_from_terrain_tab(),
            Edit::EditScatterPlan => self.edit_scatter_plan(),
            Edit::New => {
                let mut graph = TerrainGraph::new();
                graph.add("output", Point::new(400.0, 80.0));
                self.terraingen.name = "Water".to_owned();
                self.terraingen.load(graph);
            }
            Edit::PreviewPress => self.terraingen.orbit = Some(Point::new(f32::NAN, 0.0)),
            Edit::PreviewMove(point) => {
                if let Some(last) = self.terraingen.orbit {
                    if !last.x.is_nan() {
                        self.terraingen.view.yaw += (point.x - last.x) * 0.012;
                        self.terraingen.view.pitch = (self.terraingen.view.pitch
                            + (point.y - last.y) * 0.01)
                            .clamp(0.12, 1.45);
                        self.terraingen.redraw();
                    }
                    self.terraingen.orbit = Some(point);
                }
            }
            Edit::PreviewRelease => self.terraingen.orbit = None,
            Edit::PreviewZoom(delta) => {
                self.terraingen.view.zoom =
                    (self.terraingen.view.zoom * (1.0 + delta * 0.1)).clamp(0.4, 4.0);
                self.terraingen.redraw();
            }
            Edit::Export => self.export_terrain(),
            Edit::Place => self.place_terrain(),
            Edit::Noop => {}
        }
    }

    /// Writes `<slug>.generator.terrain` beside the height map the terrain exports.
    fn save_terrain_recipe(&mut self) -> Result<std::path::PathBuf, String> {
        self.ensure_workspace();
        let dir = self
            .terrain_dir()
            .ok_or("Open a project first: terrains are written to its assets folder")?;
        let slug = crate::uikit::nature::slug(&self.terraingen.name);
        let name = self.terraingen.name.clone();
        let graph = terrain::to_text(&name, &self.terraingen.graph);
        self.write_recipe(
            &dir,
            crate::uikit::recipe::Kind::Terrain,
            &slug,
            &name,
            &format!("{slug}_height.png"),
            graph,
        )
    }

    fn export_terrain(&mut self) {
        self.ensure_workspace();
        let Some(dir) = self.terrain_dir() else {
            self.terraingen.status =
                Some("Open a project first: terrains are written to its assets folder".to_owned());
            return;
        };
        let slug = crate::uikit::nature::slug(&self.terraingen.name);
        let res = self.terraingen.graph.output_resolution();
        let result = self.terraingen.graph.build(res).and_then(|model| {
            std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
            let name = self.terraingen.name.clone();
            let graph = terrain::to_text(&name, &self.terraingen.graph);
            self.write_recipe(
                &dir,
                crate::uikit::recipe::Kind::Terrain,
                &slug,
                &name,
                &format!("{slug}_height.png"),
                graph,
            )?;
            // A 16-bit height map, ready for any other tool.
            let pixels: Vec<u16> = model
                .height
                .data
                .iter()
                .map(|v| (v.clamp(0.0, 1.0) * 65535.0).round() as u16)
                .collect();
            image::ImageBuffer::<image::Luma<u16>, Vec<u16>>::from_raw(
                res as u32, res as u32, pixels,
            )
            .ok_or_else(|| "bad height map".to_owned())?
            .save(dir.join(format!("{slug}_height.png")))
            .map_err(|e| e.to_string())
        });
        self.terraingen.status = Some(match result {
            Ok(()) => format!("Saved Terrain/{slug}.generator.terrain and {slug}_height.png"),
            Err(error) => format!("Could not save: {error}"),
        });
    }

    /// Golden rule: the terrain's layer pictures are project assets. Empty slots on the Output node
    /// are filled by baking material-library pictures into `Terrain/Layers/`; existing files and
    /// paths you chose are never touched.
    pub(super) fn materialize_terrain_assets(&mut self) -> Result<(), String> {
        self.ensure_workspace();
        let Some(dir) = self.terrain_dir() else {
            return Ok(());
        };
        let Some(output) = self.terraingen.graph.output_node() else {
            return Ok(());
        };
        // Ground, rock, dirt and mud, from the material library.
        const SOURCES: [(&str, &str); 4] = [
            ("ground", "Ground · Lawn Grass"),
            ("rock", "Rock · Cliff Granite"),
            ("dirt", "Rock · Sandstone"),
            ("mud", "Ground · Pine Needles"),
        ];
        let layers = self.terraingen.graph.layer_paths();
        for (i, (slug, label)) in SOURCES.iter().enumerate() {
            if layers.get(i).is_some_and(|p| !p.is_empty()) {
                continue;
            }
            let crate::uikit::nature::mesh::MaterialSpec::Preset(preset) =
                crate::uikit::nature::ops::spec_for_label(
                    label,
                    crate::uikit::nature::Quality::Balanced,
                )
            else {
                continue;
            };
            let folder = dir.join("Layers");
            std::fs::create_dir_all(&folder).map_err(|e| e.to_string())?;
            let file = folder.join(format!("{slug}.png"));
            if !file.exists() {
                let preset = crate::uikit::material_library::presets()
                    .iter()
                    .find(|p| p.name == preset)
                    .ok_or_else(|| format!("unknown material preset `{preset}`"))?;
                let mut graph =
                    preset.build(&crate::uikit::material_library::PresetParams::default());
                graph.resolution = 512;
                let baked = graph.bake().map_err(|e| format!("{slug}: {e}"))?;
                image::save_buffer(&file, &baked.base, 512, 512, image::ColorType::Rgba8)
                    .map_err(|e| e.to_string())?;
            }
            let rooted = self
                .engine
                .as_ref()
                .and_then(|e| e.asset_path("").ok())
                .is_some_and(|root| !root.is_empty());
            // The slot holds a material: the layer's `.mat`, with its picture as the albedo map.
            let mat_file = folder.join(format!("{slug}.mat"));
            if !mat_file.exists() {
                let engine = self.engine.as_ref().ok_or("The engine is not available")?;
                let mut material = engine.new_material().map_err(|e| format!("{e:?}"))?;
                material.name = format!("{slug} terrain layer");
                material.albedo_map = if rooted {
                    engine
                        .asset_project_path(&format!("Terrain/Layers/{slug}.png"))
                        .unwrap_or_else(|_| format!("Terrain/Layers/{slug}.png"))
                } else {
                    file.display().to_string()
                };
                material.roughness = 0.9;
                material.uv_object_scale = false;
                engine
                    .save_material(&mat_file.to_string_lossy(), &material)
                    .map_err(|e| format!("{e:?}"))?;
            }
            let path = if rooted {
                format!("Terrain/Layers/{slug}.mat")
            } else {
                mat_file.display().to_string()
            };
            self.terraingen.graph.set_string(output, i, path);
            self.terraingen.dirty = true;
        }
        Ok(())
    }

    /// Builds the terrain at the Output node's resolution and places it as an engine terrain.
    pub(super) fn place_terrain(&mut self) {
        if let Err(error) = self.materialize_terrain_assets() {
            self.terraingen.status = Some(error);
            return;
        }
        // The recipe goes first: whatever happens to the scene below, the graph is never lost.
        let saved = match self.save_terrain_recipe() {
            Ok(_) => format!(
                ". Recipe: {}.generator.terrain",
                crate::uikit::nature::slug(&self.terraingen.name)
            ),
            Err(error) => format!(". The recipe was not saved ({error})"),
        };
        let layers = self.terraingen.graph.layer_paths();
        let layer_root = self.asset_root_dir();
        let res = self.terraingen.graph.output_resolution();
        let model = match self.terraingen.graph.build(res) {
            Ok(model) => model,
            Err(error) => {
                self.terraingen.status = Some(format!("Graph: {error}{saved}"));
                return;
            }
        };
        let name = self.terraingen.name.clone();
        let focus = self.camera.focus;
        let Some(engine) = self.engine.as_mut() else {
            self.terraingen.status = Some("The engine is not available".to_owned());
            return;
        };
        let id = match engine.create_entity(&name) {
            Ok(id) => id,
            Err(error) => {
                self.terraingen.status = Some(format!("Could not create the entity: {error:?}"));
                return;
            }
        };
        // The terrain mesh is centred on its entity: put it under the camera focus.
        let _ = engine.set_position(id, [focus[0], 0.0, focus[2]]);
        self.terraingen.placed = Some(id);
        let settings = doppio_engine::Terrain {
            resolution: [res as u32, res as u32],
            size: model.size,
            max_height: model.max_height,
            cast_shadows: true,
        };
        if let Err(error) =
            engine.set_terrain(id, &settings, &model.height.data, Some(&model.splat))
        {
            self.terraingen.status = Some(format!(
                "Could not create the terrain ({error:?}): the engine library needs C ABI v8"
            ));
            return;
        }
        // The masks the graph makes (flow, wetness, erosion wear and deposit) go with the terrain, so scatter and
        // the biome table can place by them. An engine without C ABI v15 just has none.
        let sent: Vec<String> = model
            .masks
            .iter()
            .filter(|(name, bytes)| {
                engine
                    .set_terrain_mask(id, name, [res as u32, res as u32], bytes)
                    .is_ok()
            })
            .map(|(name, _)| name.clone())
            .collect();
        // The four layer pictures are the project's textures, written from the Output node.
        if let Some(type_id) = super::component_type_id(engine, "Terrain") {
            for (i, path) in layers.iter().enumerate().filter(|(_, p)| !p.is_empty()) {
                // A `generator.material` / `generator.texture` recipe stands for what it baked.
                let recipe_kind = layer_root
                    .as_ref()
                    .and_then(|root| crate::uikit::recipe::kind_of(&root.join(path)));
                let path = &match (recipe_kind, layer_root.as_ref()) {
                    (Some(kind), Some(root)) => {
                        match crate::uikit::recipe_ref::resolve_product(root, path, kind) {
                            Ok(product) => product,
                            Err(error) => {
                                self.terraingen.status = Some(error);
                                continue;
                            }
                        }
                    }
                    _ => path.clone(),
                };
                // A slot is a material (its albedo map paints the layer) or, as before, a picture.
                let picture = if path.ends_with(".mat") {
                    let file = layer_root
                        .as_ref()
                        .map(|root| root.join(path))
                        .unwrap_or_else(|| std::path::PathBuf::from(path));
                    match engine.load_material(&file.to_string_lossy()) {
                        Ok(material) if !material.albedo_map.is_empty() => material.albedo_map,
                        _ => continue,
                    }
                } else {
                    path.clone()
                };
                let resolved = engine
                    .asset_project_path(&picture)
                    .unwrap_or_else(|_| picture.clone());
                let _ = super::set_component_field(
                    engine,
                    id,
                    type_id,
                    &format!("Splat Layer Paths {}", i + 1),
                    doppio_engine::FieldValue::String(resolved),
                );
            }
        }
        self.entities = super::projectloader::read_entities(engine);
        if let Some(index) = self.entities.iter().position(|entity| entity.id == id) {
            self.select_index(index);
        }
        self.terraingen.dirty = false;
        self.terraingen.status = Some(format!(
            "Placed {name} ({res}² vertices); the sea level is at {:.0} m, add a Water body there. Masks: height, slope, curvature{}{saved}",
            model.sea_metres,
            sent.iter().map(|n| format!(", {n}")).collect::<String>()
        ));
        self.refresh_components();
        self.commit("Place terrain", None);
        self.log(LogLevel::Action, format!("Placed terrain {name}"));
    }
}

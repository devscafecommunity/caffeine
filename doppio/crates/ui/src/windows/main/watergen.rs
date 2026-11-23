//! Editor state and message handling of the **Water Generator** pane (lakes, rivers, seas).
//!
//! A water body is a node graph (`uikit::water`). This module owns the graph being edited with an
//! undo history, renders the animated top-down preview, saves `.water` files and places the result
//! in the scene as an engine water surface (`caffeine_session_set_water`, C ABI v7).

use super::{App, LogLevel, PaneKind};
use crate::uikit::panels::water_generator::{self as panel, Edit};
use crate::uikit::water::{self, preview, NodeId, WaterGraph};
use iced::widget::image::Handle;
use iced::{Point, Vector};
use std::collections::HashSet;
use std::path::PathBuf;

/// The preview is drawn small and scaled by the widget; it redraws at most `PREVIEW_FPS` times a second.
const PREVIEW_SIZE: u32 = 160;
const PREVIEW_FPS: f32 = 20.0;

pub(super) struct WaterGen {
    pub graph: WaterGraph,
    undo: Vec<(WaterGraph, Option<String>)>,
    cursor: usize,
    pub name: String,
    pub selected: Option<NodeId>,
    pub link: Option<(NodeId, usize)>,
    pub pan: Vector,
    pub zoom: f32,
    pub snap: bool,
    pub search: String,
    pub collapsed: HashSet<String>,
    pub presets: Vec<water::WaterPreset>,
    pub preset_labels: Vec<String>,
    pub preview: Option<Handle>,
    pub stats: String,
    pub status: Option<String>,
    pub dirty: bool,
    pub playing: bool,
    pub time: f32,
    /// Time since the preview was last drawn (animation throttle).
    pub since_draw: f32,
    /// The evaluated water, cached for the preview animation and placement.
    pub water: Option<doppio_engine::Water>,
}

impl WaterGen {
    pub fn new() -> Self {
        let presets = water::presets();
        let first = presets
            .iter()
            .position(|p| p.name == "Open Sea")
            .unwrap_or(0);
        let preset_labels = presets
            .iter()
            .map(|p| format!("{} · {}", p.category, p.name))
            .collect();
        let graph = presets[first].graph();
        let mut state = WaterGen {
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
            dirty: false,
            playing: true,
            time: 0.0,
            since_draw: 0.0,
            water: None,
        };
        state.fit();
        state.refresh();
        state
    }

    pub(super) fn load(&mut self, graph: WaterGraph) {
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

    /// Re-evaluates the graph and redraws the preview.
    pub fn refresh(&mut self) {
        match self.graph.build_report() {
            Ok((water, dropped)) => {
                self.stats = format!(
                    "{} wave{} · {:.0} × {:.0} m · {:.1} m deep",
                    water.waves.len(),
                    if water.waves.len() == 1 { "" } else { "s" },
                    water.size[0],
                    water.size[1],
                    water.depth
                );
                if self
                    .status
                    .as_deref()
                    .is_some_and(|s| s.starts_with("Graph:") || s.starts_with("Mesh:"))
                {
                    self.status = None;
                }
                if dropped > 0 {
                    self.status = Some(format!(
                        "Mesh: {dropped} wave{} too fine for the mesh were faded out (raise Mesh resolution, shrink the body or lengthen the waves)",
                        if dropped == 1 { "" } else { "s" }
                    ));
                }
                self.water = Some(water);
                self.redraw();
            }
            Err(error) => {
                self.stats = "no water".to_owned();
                self.status = Some(format!("Graph: {error}"));
                self.water = None;
                self.preview = None;
            }
        }
    }

    /// Redraws the preview at the current time (the waves are already evaluated).
    pub fn redraw(&mut self) {
        if let Some(water) = &self.water {
            let pixels = preview::render(water, PREVIEW_SIZE, self.time);
            self.preview = Some(Handle::from_rgba(PREVIEW_SIZE, PREVIEW_SIZE, pixels));
        }
    }

    pub fn view_state(&self, materials: Vec<String>) -> panel::View<'_> {
        panel::View {
            graph: &self.graph,
            selected: self.selected,
            selected_link: self.link,
            pan: self.pan,
            zoom: self.zoom,
            snap: self.snap,
            search: &self.search,
            collapsed_groups: &self.collapsed,
            playing: self.playing,
            name: &self.name,
            presets: &self.preset_labels,
            preview: self.preview.as_ref(),
            stats: &self.stats,
            status: self.status.as_deref(),
            dirty: self.dirty,
            project_materials: materials,
        }
    }
}

/// Writes the Water Material node's colour, opacity and roughness as a `.mat` in the project and
/// returns the path to assign (asset-root relative when the project has one).
fn write_water_material(
    engine: &doppio_engine::Engine,
    dir: &std::path::Path,
    name: &str,
    albedo: [f32; 4],
    roughness: f32,
) -> Result<String, String> {
    std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    let slug = crate::uikit::nature::slug(name);
    let file = dir.join(format!("{slug}.mat"));
    let relative = format!("Water/{slug}.mat");
    let rooted = engine
        .asset_path("")
        .map(|root| !root.is_empty())
        .unwrap_or(false);
    let path = if rooted {
        relative
    } else {
        file.display().to_string()
    };
    // An existing material is the user's now: it is never overwritten.
    if file.exists() {
        return Ok(path);
    }
    let mut material = engine.new_material().map_err(|e| format!("{e:?}"))?;
    material.name = format!("{name} water");
    material.albedo = albedo;
    material.roughness = roughness;
    material.metallic = 0.0;
    // Water: a low base reflectance (the Fresnel term does the rest), a thin sharp coat for the sky
    // reflection, see-through, and the index of refraction of water.
    material.reflectance = 0.5;
    material.alpha_mode = doppio_engine::MaterialAlphaMode::Blend;
    material.transmission = 0.12;
    material.ior = 1.333;
    material.clearcoat = 0.35;
    material.clearcoat_roughness = 0.03;
    material.uv_object_scale = false;
    engine
        .save_material(&file.to_string_lossy(), &material)
        .map_err(|e| format!("{e:?}"))?;
    Ok(path)
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
    /// Golden rule: the water's material is a real asset. The Water Material node is only a mould:
    /// this writes its `.mat` into the project (once; an existing file is never overwritten) and
    /// turns the node into **Your Water Material** pointing at it.
    pub(super) fn materialize_water_assets(&mut self) -> Result<(), String> {
        self.ensure_workspace();
        let Some(dir) = self.water_dir() else {
            return Ok(());
        };
        let name = self.watergen.name.clone();
        let jobs: Vec<(crate::uikit::water::NodeId, [f32; 4], f32)> = self
            .watergen
            .graph
            .nodes
            .iter()
            .filter(|n| n.op == "look")
            .map(|n| {
                (
                    n.id,
                    [n.params[0], n.params[1], n.params[2], n.params[3]],
                    n.params[4],
                )
            })
            .collect();
        for (id, albedo, roughness) in jobs {
            let engine = self.engine.as_ref().ok_or("The engine is not available")?;
            let path = write_water_material(engine, &dir, &name, albedo, roughness)
                .map_err(|e| format!("Could not write the water material: {e}"))?;
            if let Some(node) = self.watergen.graph.node_mut(id) {
                node.op = "look_asset";
                node.params.clear();
                node.strings = vec![path];
            }
            self.watergen.dirty = true;
        }
        Ok(())
    }

    pub(super) fn show_water_pane(&mut self) {
        self.panes.restore();
        let mut target = None;
        for (pane, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::WaterGenerator)
            {
                group.active = index;
                target = Some(*pane);
            }
        }
        if target.is_none() {
            for (pane, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Viewport) {
                    group.pages.push(PaneKind::WaterGenerator);
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

    /// Advances the preview animation while the tab is in front and playing.
    pub(super) fn pump_water_preview(&mut self, dt: f32) {
        let visible = self
            .panes
            .iter()
            .any(|(_, group)| group.active_kind() == PaneKind::WaterGenerator);
        if visible && self.watergen.playing {
            self.watergen.time += dt;
            self.watergen.since_draw += dt;
            if self.watergen.since_draw >= 1.0 / PREVIEW_FPS {
                self.watergen.since_draw = 0.0;
                self.watergen.redraw();
            }
        }
    }

    /// Steps the engine so water surfaces in the scene animate in the editor viewport.
    pub(super) fn tick_engine_water(&mut self, dt: f32) {
        let visible = self
            .panes
            .iter()
            .any(|(_, group)| group.active_kind() == PaneKind::Viewport);
        // Scenes can be opened or edited at any time: look for water now and then.
        self.water_scan = self.water_scan.wrapping_add(1);
        if self.water_scan % 90 == 1 {
            if let Some(engine) = self.engine.as_ref() {
                self.has_water = self.entities.iter().any(|e| engine.water(e.id).is_ok());
            }
        }
        if visible && self.has_water {
            if let Some(engine) = self.engine.as_mut() {
                engine.tick(dt);
            }
        }
    }

    fn water_dir(&self) -> Option<PathBuf> {
        self.asset_root_dir().map(|root| root.join("Water"))
    }

    pub(super) fn edit_water(&mut self, edit: Edit) {
        let snap = self.watergen.snap;
        match edit {
            Edit::Select(selected) => {
                self.watergen.selected = selected;
                self.watergen.link = None;
            }
            Edit::SelectLink(link) => {
                self.watergen.link = link;
                self.watergen.selected = None;
            }
            Edit::DeleteLink(to, input) | Edit::DetachLink(to, input) => {
                self.watergen
                    .graph
                    .links
                    .retain(|l| !(l.to == to && l.input == input));
                self.watergen.link = None;
                self.watergen.push(None);
                self.watergen.refresh();
            }
            Edit::Connect {
                from,
                output,
                to,
                input,
            } => {
                if self.watergen.graph.connect(from, output, to, input) {
                    self.watergen.status = None;
                    self.watergen.link = Some((to, input));
                    self.watergen.push(None);
                    self.watergen.refresh();
                } else {
                    self.watergen.status = Some("Those ports cannot connect: the colours must match and the graph cannot loop".to_owned());
                }
            }
            Edit::MoveNode(id, delta) => {
                if let Some(node) = self.watergen.graph.node_mut(id) {
                    node.position =
                        Point::new(node.position.x + delta.x, node.position.y + delta.y);
                }
                self.watergen.push(Some(format!("move:{id}")));
            }
            Edit::Viewport { pan, zoom } => {
                self.watergen.pan = pan;
                self.watergen.zoom = zoom.clamp(0.3, 2.0);
            }
            Edit::AddNode(op) => {
                if op == "output" {
                    if let Some(output) = self.watergen.graph.output_node() {
                        self.watergen.selected = Some(output);
                        return;
                    }
                }
                let at = Point::new(
                    (260.0 - self.watergen.pan.x) / self.watergen.zoom,
                    (160.0 - self.watergen.pan.y) / self.watergen.zoom
                        + self.watergen.graph.nodes.len() as f32 * 6.0,
                );
                let id = self.watergen.graph.add(op, snap_position(at, snap));
                self.watergen.selected = Some(id);
                self.watergen.link = None;
                self.watergen.push(None);
                self.watergen.refresh();
            }
            Edit::DeleteNode(id) => {
                if self
                    .watergen
                    .graph
                    .node(id)
                    .is_some_and(|n| n.op == "output")
                {
                    self.watergen.status = Some(
                        "The Output node is the graph's result and cannot be deleted".to_owned(),
                    );
                    return;
                }
                self.watergen.graph.remove(id);
                if self.watergen.selected == Some(id) {
                    self.watergen.selected = None;
                }
                self.watergen.link = None;
                self.watergen.push(None);
                self.watergen.refresh();
            }
            Edit::SetParam(id, index, value) => {
                if let Some(node) = self.watergen.graph.node_mut(id) {
                    if let Some(spec) = node.def().params.get(index) {
                        let v = if matches!(spec.kind, crate::uikit::procedural::ParamKind::Float) {
                            value
                        } else {
                            value.round()
                        };
                        node.params[index] = v.clamp(spec.min, spec.max);
                    }
                }
                self.watergen.push(Some(format!("param:{id}:{index}")));
                self.watergen.refresh();
            }
            Edit::PickAsset(id, index) => {
                let current = self
                    .watergen
                    .graph
                    .node(id)
                    .and_then(|node| node.strings.get(index).cloned())
                    .unwrap_or_default();
                self.open_asset_pick(
                    super::assetpick::PickTarget::WaterString { node: id, index },
                    vec![crate::uikit::asset_kind::AssetKind::Material],
                    "Water look material".to_owned(),
                    current,
                );
            }
            Edit::SetString(id, index, text) => {
                let Some(text) = self.reference_text(
                    text,
                    &[crate::uikit::asset_kind::AssetKind::Material],
                    "Water look material",
                ) else {
                    return;
                };
                self.watergen.graph.set_string(id, index, text);
                self.watergen.push(Some(format!("string:{id}:{index}")));
                self.watergen.refresh();
            }
            Edit::Undo => {
                if self.watergen.cursor > 0 {
                    self.watergen.cursor -= 1;
                    self.watergen.graph = self.watergen.undo[self.watergen.cursor].0.clone();
                    self.watergen.refresh();
                }
            }
            Edit::Redo => {
                if self.watergen.cursor + 1 < self.watergen.undo.len() {
                    self.watergen.cursor += 1;
                    self.watergen.graph = self.watergen.undo[self.watergen.cursor].0.clone();
                    self.watergen.refresh();
                }
            }
            Edit::Fit => self.watergen.fit(),
            Edit::SetSnap(on) => self.watergen.snap = on,
            Edit::SetSearch(value) => self.watergen.search = value,
            Edit::TogglePaletteGroup(group) => {
                if !self.watergen.collapsed.remove(&group) {
                    self.watergen.collapsed.insert(group);
                }
            }
            Edit::ApplyPreset(index) => {
                if let Some(preset) = self.watergen.presets.get(index) {
                    let graph = preset.graph();
                    self.watergen.name = preset.name.to_owned();
                    self.watergen.load(graph);
                    self.watergen.dirty = false;
                    // A preset is only a mould: its material becomes a project asset right away.
                    if let Err(error) = self.materialize_water_assets() {
                        self.watergen.status = Some(error);
                    }
                }
            }
            Edit::SetName(name) => self.watergen.name = name,
            Edit::New => {
                let mut graph = WaterGraph::new();
                graph.add("output", Point::new(400.0, 80.0));
                self.watergen.name = "Water".to_owned();
                self.watergen.load(graph);
            }
            Edit::TogglePlay(on) => self.watergen.playing = on,
            Edit::Export => self.export_water(),
            Edit::Place => self.place_water(),
            Edit::Noop => {}
        }
    }

    /// Writes `<slug>.generator.water` beside the water material, so the graph can be reopened later.
    fn save_water_recipe(&mut self) -> Result<std::path::PathBuf, String> {
        self.ensure_workspace();
        let dir = self
            .water_dir()
            .ok_or("Open a project first: water graphs are written to its assets folder")?;
        let slug = crate::uikit::nature::slug(&self.watergen.name);
        let name = self.watergen.name.clone();
        let graph = water::to_text(&name, &self.watergen.graph);
        self.write_recipe(
            &dir,
            crate::uikit::recipe::Kind::Water,
            &slug,
            &name,
            &format!("{slug}.mat"),
            graph,
        )
    }

    fn export_water(&mut self) {
        let slug = crate::uikit::nature::slug(&self.watergen.name);
        self.watergen.status = Some(match self.save_water_recipe() {
            Ok(_) => format!("Saved Water/{slug}.generator.water"),
            Err(error) => format!("Could not save: {error}"),
        });
    }

    /// Creates the water body in the scene as an animated engine water surface.
    pub(super) fn place_water(&mut self) {
        if let Err(error) = self.materialize_water_assets() {
            self.watergen.status = Some(error);
            return;
        }
        // The recipe goes first: whatever happens to the scene below, the graph is never lost.
        let saved = match self.save_water_recipe() {
            Ok(_) => format!(
                "; recipe saved as {}.generator.water",
                crate::uikit::nature::slug(&self.watergen.name)
            ),
            Err(error) => format!("; the recipe was not saved ({error})"),
        };
        self.watergen.refresh();
        let Some(water) = self.watergen.water.clone() else {
            self.watergen.status = Some(format!("Graph: nothing to place{saved}"));
            return;
        };
        let material_path = self.watergen.graph.look_asset();
        let name = self.watergen.name.clone();
        let focus = self.camera.focus;
        self.ensure_workspace();
        let water_dir = self.water_dir();
        let Some(engine) = self.engine.as_mut() else {
            self.watergen.status = Some("The engine is not available".to_owned());
            return;
        };
        let id = match engine.create_entity(&name) {
            Ok(id) => id,
            Err(error) => {
                self.watergen.status = Some(format!("Could not create the entity: {error:?}"));
                return;
            }
        };
        let _ = engine.set_position(id, [focus[0], water.level, focus[2]]);
        let mut placed = water.clone();
        // The entity's position carries the level.
        placed.level = 0.0;
        if let Err(error) = engine.set_water(id, &placed) {
            self.watergen.status = Some(format!(
                "Could not create the water ({error:?}): the engine library needs C ABI v7"
            ));
            return;
        }
        // The water's material is a real project asset (`water/<name>.mat`), assigned to the
        // entity: open it in the Material tab to change it.
        let _ = water_dir;
        let Some(path) = material_path else {
            self.watergen.status = Some(
                "Open a project first: the water material is saved in its assets folder".to_owned(),
            );
            return;
        };
        if let Err(error) = engine.apply_material(id, &path) {
            self.watergen.status = Some(format!("Could not assign the water material: {error:?}"));
            return;
        }
        self.entities = super::projectloader::read_entities(engine);
        if let Some(index) = self.entities.iter().position(|entity| entity.id == id) {
            self.select_index(index);
        }
        self.has_water = true;
        self.watergen.dirty = false;
        self.watergen.status = Some(format!(
            "Placed {name} ({} waves){saved}",
            water.waves.len()
        ));
        self.refresh_components();
        self.commit("Place water", None);
        self.log(LogLevel::Action, format!("Placed water {name}"));
    }
}

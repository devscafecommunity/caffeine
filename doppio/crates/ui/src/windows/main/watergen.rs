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

const PREVIEW_SIZE: u32 = 256;

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
            water: None,
        };
        state.fit();
        state.refresh();
        state
    }

    fn load(&mut self, graph: WaterGraph) {
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
        let zoom = (620.0 / (max_x - min_x).max(1.0))
            .min(380.0 / (max_y - min_y).max(1.0))
            .clamp(0.3, 1.0);
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

    pub fn view_state(&self) -> panel::View<'_> {
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
            self.watergen.redraw();
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
        self.asset_root_dir().map(|root| root.join("water"))
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

    fn export_water(&mut self) {
        let Some(dir) = self.water_dir() else {
            self.watergen.status = Some(
                "Open a project first: water graphs are written to its assets folder".to_owned(),
            );
            return;
        };
        let slug = crate::uikit::nature::slug(&self.watergen.name);
        let result = std::fs::create_dir_all(&dir).and_then(|_| {
            std::fs::write(
                dir.join(format!("{slug}.water")),
                water::to_text(&self.watergen.name, &self.watergen.graph),
            )
        });
        self.watergen.status = Some(match result {
            Ok(()) => format!("Saved water/{slug}.water"),
            Err(error) => format!("Could not save: {error}"),
        });
    }

    /// Creates the water body in the scene as an animated engine water surface.
    pub(super) fn place_water(&mut self) {
        let Some(water) = self.watergen.water.clone() else {
            self.watergen.status = Some("Graph: nothing to place".to_owned());
            return;
        };
        let name = self.watergen.name.clone();
        let focus = self.camera.focus;
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
        self.entities = super::projectloader::read_entities(engine);
        if let Some(index) = self.entities.iter().position(|entity| entity.id == id) {
            self.select_index(index);
        }
        self.has_water = true;
        self.watergen.dirty = false;
        self.watergen.status = Some(format!("Placed {name} ({} waves)", water.waves.len()));
        self.refresh_components();
        self.commit("Place water", None);
        self.log(LogLevel::Action, format!("Placed water {name}"));
    }
}

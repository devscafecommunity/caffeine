//! Editor state and message handling of the **Texture Generator** pane.
//!
//! Kept apart from the Material Generator code in `main.rs` on purpose: a texture graph is its
//! own document (`.texgraph`) with its own result node, palette, presets, preview and export.
//! It reuses the shared node store, the canvas component and the parameter editor.

use super::{apply_generator_param, snap_generator_position, App, LogLevel, PaneKind};
use crate::uikit::generators::{self, GraphHistory, MaterialGraph, NodeId, NodeKind};
use crate::uikit::panels::material_generator as nodes_panel;
use crate::uikit::panels::texture_generator::{self as panel, Edit};
use crate::uikit::texture_graph::{self, BitDepth};
use iced::widget::image::Handle;
use std::collections::{HashMap, HashSet};
use std::path::PathBuf;

/// Preview and thumbnail sizes (pixels).
const PREVIEW_SIZE: u32 = 256;
const THUMB_SIZE: u32 = 96;

pub(super) struct TextureGen {
    pub graph: MaterialGraph,
    pub history: GraphHistory,
    pub name: String,
    pub path: Option<PathBuf>,
    pub dirty: bool,
    pub selected: Option<NodeId>,
    pub link: Option<(NodeId, usize)>,
    pub pan: iced::Vector,
    pub zoom: f32,
    pub snap: bool,
    pub search: String,
    pub collapsed: HashSet<String>,
    pub preview: Option<Handle>,
    pub preview_mode: usize,
    pub node_previews: HashMap<NodeId, Handle>,
    pub status: Option<String>,
    pub bits: u8,
    pub flip: (u32, u32),
    reconnect: bool,
}

impl TextureGen {
    pub fn new() -> Self {
        let graph = texture_graph::new_graph();
        let mut state = TextureGen {
            history: GraphHistory::new(graph.clone()),
            selected: graph.texture_output_node(),
            graph,
            name: "Texture".to_owned(),
            path: None,
            dirty: false,
            link: None,
            pan: iced::Vector::new(0.0, 0.0),
            zoom: 1.0,
            snap: false,
            search: String::new(),
            collapsed: HashSet::new(),
            preview: None,
            preview_mode: 0,
            node_previews: HashMap::new(),
            status: None,
            bits: 8,
            flip: (4, 4),
            reconnect: false,
        };
        state.fit();
        state.refresh();
        state
    }

    /// Replaces the graph (new document or library preset) and resets the view.
    fn load_graph(&mut self, graph: MaterialGraph) {
        self.history = GraphHistory::new(graph.clone());
        self.selected = graph.texture_output_node();
        self.graph = graph;
        self.link = None;
        self.fit();
        self.refresh();
    }

    pub fn fit(&mut self) {
        let nodes = &self.graph.nodes;
        if nodes.is_empty() {
            self.pan = iced::Vector::new(24.0, 24.0);
            self.zoom = 1.0;
            return;
        }
        let min_x = nodes.iter().map(|n| n.position.x).fold(f32::MAX, f32::min);
        let min_y = nodes.iter().map(|n| n.position.y).fold(f32::MAX, f32::min);
        let max_x = nodes.iter().map(|n| n.position.x).fold(f32::MIN, f32::max) + 170.0;
        let max_y = nodes.iter().map(|n| n.position.y).fold(f32::MIN, f32::max) + 130.0;
        let zoom = (640.0 / (max_x - min_x).max(1.0))
            .min(420.0 / (max_y - min_y).max(1.0))
            .clamp(0.4, 1.0);
        self.zoom = zoom;
        self.pan = iced::Vector::new(28.0 - min_x * zoom, 28.0 - min_y * zoom);
    }

    /// Rebuilds the preview and the node thumbnails. Both are small (256 / 96 px), so this is
    /// done synchronously after each edit.
    pub fn refresh(&mut self) {
        let textures = HashMap::new();
        let mut small = self.graph.clone();
        small.resolution = PREVIEW_SIZE;
        match small.bake_texture_with(&textures, None, None) {
            Ok(baked) => {
                self.status = self.status.take().filter(|s| !s.starts_with("Bake failed"));
                self.preview = Some(preview_handle(
                    &baked.rgba8,
                    baked.resolution,
                    self.preview_mode,
                ));
            }
            Err(error) => {
                self.preview = None;
                self.status = Some(format!("Bake failed: {error}"));
            }
        }
        self.node_previews = self
            .graph
            .node_previews(&textures, THUMB_SIZE, None)
            .into_iter()
            .map(|(id, pixels)| (id, Handle::from_rgba(THUMB_SIZE, THUMB_SIZE, pixels)))
            .collect();
    }

    fn push_history(&mut self, name: impl Into<String>, key: Option<String>) {
        self.dirty = true;
        self.history.push(self.graph.clone(), name, key);
    }

    pub fn view_state(&self) -> panel::View<'_> {
        panel::View {
            graph: &self.graph,
            name: &self.name,
            selected: self.selected,
            selected_link: self.link,
            pan: self.pan,
            zoom: self.zoom,
            preview: self.preview.as_ref(),
            preview_mode: self.preview_mode,
            node_previews: &self.node_previews,
            status: self.status.as_deref(),
            dirty: self.dirty,
            can_undo: self.history.can_undo(),
            can_redo: self.history.can_redo(),
            snap: self.snap,
            search: &self.search,
            collapsed_groups: &self.collapsed,
            bits: self.bits,
            flip_columns: self.flip.0,
            flip_rows: self.flip.1,
        }
    }
}

/// RGBA preview, composited over a checkerboard or showing one channel.
fn preview_handle(rgba: &[u8], size: u32, mode: usize) -> Handle {
    let mut out = Vec::with_capacity(rgba.len());
    let width = size as usize;
    for (index, pixel) in rgba.as_chunks::<4>().0.iter().enumerate() {
        let (x, y) = (index % width, index / width);
        let value = |channel: usize| pixel[channel];
        let rgb = match mode {
            1 => [pixel[0], pixel[1], pixel[2]],
            2 => [pixel[3]; 3],
            3 => [value(0); 3],
            4 => [value(1); 3],
            5 => [value(2); 3],
            _ => {
                let checker: f32 = if ((x / 16) + (y / 16)) % 2 == 0 {
                    0.30
                } else {
                    0.42
                };
                let a = pixel[3] as f32 / 255.0;
                let mix = |c: u8| ((c as f32 / 255.0 * a + checker * (1.0 - a)) * 255.0) as u8;
                [mix(pixel[0]), mix(pixel[1]), mix(pixel[2])]
            }
        };
        out.extend_from_slice(&[rgb[0], rgb[1], rgb[2], 255]);
    }
    Handle::from_rgba(size, size, out)
}

fn file_stem(name: &str) -> String {
    super::sanitize_material_name(name)
        .replace(' ', "_")
        .to_lowercase()
}

impl App {
    pub(super) fn texgen_dir(&self) -> PathBuf {
        self.asset_root_dir()
            .map(|root| root.join("textures"))
            .unwrap_or_else(|| PathBuf::from("textures"))
    }

    pub(super) fn show_texgen_pane(&mut self) {
        self.panes.restore();
        let mut target = None;
        for (pane, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::TextureGenerator)
            {
                group.active = index;
                target = Some(*pane);
            }
        }
        if target.is_none() {
            for (pane, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Viewport) {
                    group.pages.push(PaneKind::TextureGenerator);
                    group.active = group.pages.len() - 1;
                    target = Some(*pane);
                    break;
                }
            }
        }
        if target.is_none() {
            if let Some((pane, group)) = self.panes.iter_mut().next() {
                group.pages.push(PaneKind::TextureGenerator);
                group.active = group.pages.len() - 1;
                target = Some(*pane);
            }
        }
        if let Some(pane) = target {
            self.panes.maximize(pane);
        }
    }

    pub(super) fn edit_texgen(&mut self, edit: Edit) {
        match edit {
            Edit::Node(edit) => self.edit_texgen_node(edit),
            Edit::New => {
                self.texgen = TextureGen::new();
                self.texgen.dirty = true;
                self.texgen.status = Some("New texture".to_owned());
                self.show_texgen_pane();
                self.log(LogLevel::Action, "New texture graph");
            }
            Edit::ApplyPreset(index) => {
                if let Some(preset) = texture_graph::presets().get(index) {
                    let resolution = self.texgen.graph.resolution;
                    let mut graph = preset.build();
                    graph.resolution = resolution;
                    self.texgen.load_graph(graph.clone());
                    self.texgen.name = preset.name.to_owned();
                    self.texgen.dirty = true;
                    self.texgen.status = None;
                    self.log(
                        LogLevel::Action,
                        format!("Texture library: {}", preset.name),
                    );
                }
            }
            Edit::SetBits(bits) => self.texgen.bits = if bits == 16 { 16 } else { 8 },
            Edit::SetName(name) => {
                self.texgen.name = name;
                self.texgen.dirty = true;
            }
            Edit::SetFlipbook(columns, rows) => {
                self.texgen.flip = (columns.clamp(1, 16), rows.clamp(1, 16));
            }
            Edit::SetPreviewMode(mode) => {
                self.texgen.preview_mode = mode.min(panel::PREVIEW_MODES.len() - 1);
                self.texgen.refresh();
            }
            Edit::Save => self.save_texgen(),
            Edit::Export => self.export_texgen(false),
            Edit::ExportFlipbook => self.export_texgen(true),
        }
    }

    fn save_texgen(&mut self) {
        let stem = file_stem(&self.texgen.name);
        let path = self.texgen.path.clone().unwrap_or_else(|| {
            self.texgen_dir()
                .join(format!("{stem}.{}", texture_graph::EXTENSION))
        });
        match texture_graph::save(&self.texgen.graph, &path) {
            Ok(()) => {
                self.texgen.path = Some(path.clone());
                self.texgen.dirty = false;
                self.texgen.status = Some(format!("Saved {}", path.display()));
                self.log(
                    LogLevel::Action,
                    format!("Saved texture graph {}", path.display()),
                );
            }
            Err(error) => {
                self.texgen.status = Some(format!("Could not save: {error}"));
                self.log(LogLevel::Error, format!("Texture graph: {error}"));
            }
        }
    }

    fn export_texgen(&mut self, flipbook: bool) {
        let stem = file_stem(&self.texgen.name);
        let directory = self.texgen_dir();
        let depth = if self.texgen.bits == 16 {
            BitDepth::Sixteen
        } else {
            BitDepth::Eight
        };
        let (path, baked) = if flipbook {
            let (columns, rows) = self.texgen.flip;
            let name = format!("{stem}_flipbook_{columns}x{rows}.png");
            (
                directory.join(name),
                texture_graph::bake_flipbook(&self.texgen.graph, columns, rows),
            )
        } else {
            (
                directory.join(format!("{stem}.png")),
                self.texgen
                    .graph
                    .bake_texture_with(&HashMap::new(), None, None),
            )
        };
        let result = baked.and_then(|baked| texture_graph::export_png(&baked, &path, depth));
        match result {
            Ok(()) => {
                self.texgen.status = Some(format!("Exported {}", path.display()));
                self.log(
                    LogLevel::Action,
                    format!("Exported texture {}", path.display()),
                );
                // The graph travels with its image.
                if self.texgen.path.is_none() || self.texgen.dirty {
                    self.save_texgen();
                    if let Some(saved) = self.texgen.status.take() {
                        self.texgen.status = Some(format!("Exported {} · {saved}", path.display()));
                    }
                }
            }
            Err(error) => {
                self.texgen.status = Some(format!("Export failed: {error}"));
                self.log(LogLevel::Error, format!("Texture export: {error}"));
            }
        }
    }

    fn texgen_undo_redo(&mut self, undo: bool) {
        let step = if undo {
            self.texgen.history.undo()
        } else {
            self.texgen.history.redo()
        };
        match step {
            Some((name, graph)) => {
                self.texgen.graph = graph;
                self.texgen.dirty = true;
                self.texgen.link = None;
                if self
                    .texgen
                    .selected
                    .is_some_and(|id| self.texgen.graph.node(id).is_none())
                {
                    self.texgen.selected = None;
                }
                self.texgen.status = Some(format!("{} {name}", if undo { "Undo" } else { "Redo" }));
                self.texgen.refresh();
            }
            None => {
                self.texgen.status = Some(format!(
                    "Nothing to {} in the graph",
                    if undo { "undo" } else { "redo" }
                ));
            }
        }
    }

    fn edit_texgen_node(&mut self, edit: nodes_panel::Edit) {
        use nodes_panel::Edit as N;
        let snap = self.texgen.snap;
        if !matches!(edit, N::DetachLink(..) | N::Connect { .. }) {
            self.texgen.reconnect = false;
        }
        // Edits that change the graph record an undo step and refresh the preview.
        let mut changed: Option<(String, Option<String>)> = None;
        let mut moved = false;
        match edit {
            N::Select(selected) => {
                self.texgen.selected = selected;
                self.texgen.link = None;
            }
            N::SelectLink(link) => {
                self.texgen.link = link;
                self.texgen.selected = None;
            }
            N::DeleteLink(to, input) => {
                self.texgen
                    .graph
                    .links
                    .retain(|link| !(link.to == to && link.input == input));
                if self.texgen.link == Some((to, input)) {
                    self.texgen.link = None;
                }
                changed = Some(("Delete link".to_owned(), None));
            }
            N::DetachLink(to, input) => {
                self.texgen
                    .graph
                    .links
                    .retain(|link| !(link.to == to && link.input == input));
                if self.texgen.link == Some((to, input)) {
                    self.texgen.link = None;
                }
                self.texgen.reconnect = true;
                changed = Some((
                    "Detach link".to_owned(),
                    Some("texgen:reconnect".to_owned()),
                ));
            }
            N::InsertDot(to, input, position) => {
                let dot_position = snap_generator_position(position, snap);
                let existed = self
                    .texgen
                    .graph
                    .links
                    .iter()
                    .find(|link| link.to == to && link.input == input)
                    .copied();
                if let Some(link) = existed {
                    self.texgen
                        .graph
                        .links
                        .retain(|c| !(c.to == to && c.input == input));
                    let dot = self.texgen.graph.add_node(NodeKind::Dot, dot_position);
                    self.texgen.graph.connect(link.from, link.output, dot, 0);
                    self.texgen.graph.connect(dot, 0, to, input);
                    self.texgen.selected = Some(dot);
                    self.texgen.link = None;
                    changed = Some(("Insert reroute dot".to_owned(), None));
                }
            }
            N::Undo => self.texgen_undo_redo(true),
            N::Redo => self.texgen_undo_redo(false),
            N::Fit => {
                self.texgen.fit();
                self.texgen.status = Some("Framed the graph".to_owned());
            }
            N::SetSnap(value) => self.texgen.snap = value,
            N::SetSearch(value) => self.texgen.search = value,
            N::TogglePaletteGroup(name) => {
                if !self.texgen.collapsed.remove(&name) {
                    self.texgen.collapsed.insert(name);
                }
            }
            N::Viewport { pan, zoom } => {
                self.texgen.pan = pan;
                self.texgen.zoom = zoom.clamp(0.35, 2.2);
            }
            N::AddNode(kind) => {
                if matches!(kind, NodeKind::TextureOutput { .. }) {
                    if let Some(output) = self.texgen.graph.texture_output_node() {
                        self.texgen.selected = Some(output);
                        return;
                    }
                }
                let title = kind.title().to_owned();
                let id = self.texgen.graph.add_node_auto(kind);
                if let Some(node) = self.texgen.graph.node_mut(id) {
                    node.position = snap_generator_position(node.position, snap);
                }
                self.texgen.selected = Some(id);
                self.texgen.link = None;
                changed = Some((format!("Add {title}"), None));
            }
            N::DeleteNode(id) => {
                // The Texture Output is the graph's result and cannot be removed.
                if self.texgen.graph.texture_output_node() == Some(id) {
                    self.texgen.status = Some("The Texture Output cannot be deleted".to_owned());
                    return;
                }
                let title = self
                    .texgen
                    .graph
                    .node(id)
                    .map(|node| node.kind.title().to_owned())
                    .unwrap_or_else(|| "node".to_owned());
                self.texgen.graph.remove_node(id);
                if self.texgen.selected == Some(id) {
                    self.texgen.selected = None;
                }
                self.texgen.link = None;
                changed = Some((format!("Delete {title}"), None));
            }
            N::Connect {
                from,
                output,
                to,
                input,
            } => {
                if self.texgen.graph.connect(from, output, to, input) {
                    self.texgen.status = None;
                    self.texgen.link = Some((to, input));
                    let key = self.texgen.reconnect.then(|| "texgen:reconnect".to_owned());
                    self.texgen.reconnect = false;
                    changed = Some((format!("Connect {from} → {to}"), key));
                } else {
                    self.texgen.status = Some("That connection would create a cycle".to_owned());
                }
            }
            N::MoveNode(id, delta) => {
                if let Some(node) = self.texgen.graph.node_mut(id) {
                    node.position = generators::offset_position(node.position, delta);
                }
                moved = true;
                self.texgen
                    .push_history("Move node", Some(format!("texgen:move:{id}")));
            }
            N::SetResolution(resolution) => {
                self.texgen.graph.resolution = resolution.clamp(64, 2048);
                changed = Some((
                    "Set texture size".to_owned(),
                    Some("texgen:resolution".to_owned()),
                ));
            }
            N::SetParam(id, param) => {
                let label = param.label();
                let key = param.key();
                if let Some(node) = self.texgen.graph.node_mut(id) {
                    apply_generator_param(&mut node.kind, param);
                }
                // Switching the output mode changes its ports: drop links to ports that vanished.
                if let Some(count) = self
                    .texgen
                    .graph
                    .node(id)
                    .filter(|node| matches!(node.kind, NodeKind::TextureOutput { .. }))
                    .map(|node| node.kind.inputs().len())
                {
                    self.texgen
                        .graph
                        .links
                        .retain(|link| link.to != id || link.input < count);
                }
                changed = Some((
                    format!("Set {label}"),
                    Some(format!("texgen:param:{id}:{key}")),
                ));
            }
            // Material-only edits (baking, packing, image pickers, clipboard) have no
            // meaning in a texture graph.
            _ => {}
        }
        if let Some((name, key)) = changed {
            self.texgen.push_history(name, key);
            self.texgen.refresh();
        } else if moved {
            // Moving a node does not change the picture.
        }
    }
}

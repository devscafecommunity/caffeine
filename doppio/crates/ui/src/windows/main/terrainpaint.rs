//! Editor side of the **Terrain Paint** pane (terrain editing roadmap, phase 2): the material palette of a terrain, the
//! paint brush and auto-layering.
//!
//! A terrain is painted with four layers until layered painting is turned on; then it has a **palette** of up to 64
//! layers (`doppio_engine::PaletteLayer`) and a map naming up to four of them at every texel. The palette is edited as
//! text through the engine (`palette_apply`), every edit is one engine record and one undo step (a slider drag folds
//! into one: the engine merges an edit into the record of the one before it), and painting is a stroke that goes through
//! the same mouse path as sculpting (`terraineditor.rs`): a left press on the terrain starts it, moves extend it, the
//! release closes it. Every change arrives as a [`Edit`] message, so an agent can do what the mouse can.

use super::{App, LogLevel, PaneKind};
use crate::uikit::panels::terrain_paint::{AutoRule, BrushAmount, Edit, MAX_RULES, RULE_MASKS};
use doppio_engine::{BrushPreset, Falloff, PaintBrush, PaintMode, PaletteLayer, TerrainLayers};

pub(super) struct State {
    pub active: bool,
    /// The terrain being edited: the selected one, else the first of the scene.
    pub entity: Option<u32>,
    pub terrain_name: Option<String>,
    pub layers: Option<TerrainLayers>,
    pub palette: Vec<PaletteLayer>,
    pub selected: usize,
    pub mode: PaintMode,
    pub from_layer: usize,
    pub radius: f32,
    pub strength: f32,
    pub spacing: f32,
    pub falloff: Falloff,
    pub shared_shape: bool,
    pub rules: Vec<AutoRule>,
    pub fallback: usize,
    pub status: Option<String>,
    /// The palette edit that may still absorb the next one with the same key: (key, record, history length, cursor).
    merge: Option<(String, u32, usize, usize)>,
}

impl Default for State {
    fn default() -> Self {
        State {
            active: false,
            entity: None,
            terrain_name: None,
            layers: None,
            palette: Vec::new(),
            selected: 0,
            mode: PaintMode::Paint,
            from_layer: 0,
            radius: 8.0,
            strength: 0.6,
            spacing: 0.0,
            falloff: Falloff::Smooth,
            shared_shape: false,
            rules: Vec::new(),
            fallback: 0,
            status: None,
            merge: None,
        }
    }
}

/// The text of a `.terrainpalette` for these layers (the format the engine reads).
pub fn format_palette(layers: &[PaletteLayer]) -> String {
    let clean = |text: &str| text.replace(['\n', '\r', '#'], " ").trim().to_owned();
    let mut out = String::from("# caffeine terrain palette 1\n");
    for layer in layers {
        let name = clean(&layer.name);
        out.push_str(&format!(
            "layer {}\n",
            if name.is_empty() { "Layer" } else { &name }
        ));
        if !layer.albedo.trim().is_empty() {
            out.push_str(&format!("  albedo {}\n", clean(&layer.albedo)));
        }
        if !layer.normal.trim().is_empty() {
            out.push_str(&format!("  normal {}\n", clean(&layer.normal)));
        }
        out.push_str(&format!("  tile {}\n", layer.tile_size_m));
        out.push_str(&format!("  rotation {}\n", layer.rotation_degrees));
        out.push_str(&format!(
            "  tint {} {} {}\n",
            layer.tint[0], layer.tint[1], layer.tint[2]
        ));
        out.push_str(&format!("  resolution {}\n", layer.resolution));
        if layer.locked {
            out.push_str("  locked 1\n");
        }
    }
    out
}

/// The auto-layering rules as biome-table text: the layer is the class; several rules on one layer are one class whose
/// terms must all hold.
pub fn rules_text(rules: &[AutoRule]) -> String {
    let mut layers: Vec<usize> = Vec::new();
    for rule in rules {
        if !layers.contains(&rule.layer) {
            layers.push(rule.layer);
        }
    }
    let mut out = String::new();
    for layer in layers {
        out.push_str(&format!("class {layer} base 1\n"));
        for rule in rules.iter().filter(|r| r.layer == layer) {
            out.push_str(&format!(
                "  {} {} {} {} {}\n",
                rule.mask, rule.corners[0], rule.corners[1], rule.corners[2], rule.corners[3]
            ));
        }
    }
    out
}

/// A new layer of the palette: flat grey until it gets a picture.
fn new_layer(name: String) -> PaletteLayer {
    PaletteLayer {
        name,
        albedo: String::new(),
        normal: String::new(),
        tile_size_m: 8.0,
        rotation_degrees: 0.0,
        tint: [0.55, 0.55, 0.55],
        resolution: 1024,
        locked: false,
        texels: 0,
    }
}

impl App {
    /// The terrain the pane edits: the selected entity when it is a terrain, else the first terrain of the scene.
    pub(super) fn paint_terrain(&self) -> Option<u32> {
        let engine = self.engine.as_ref()?;
        let is_terrain = |id: u32| {
            engine
                .entity_components(id)
                .iter()
                .any(|component| component.name == "Terrain")
        };
        self.entities
            .get(self.selected)
            .map(|entity| entity.id)
            .filter(|id| is_terrain(*id))
            .or_else(|| {
                self.entities
                    .iter()
                    .map(|entity| entity.id)
                    .find(|id| is_terrain(*id))
            })
    }

    /// Reads the terrain's layers into the panel's state.
    pub(super) fn refresh_paint_state(&mut self) {
        let entity = self.paint_terrain();
        let Some(engine) = self.engine.as_ref() else {
            return;
        };
        let state = &mut self.terrain_paint;
        state.entity = entity;
        state.terrain_name = entity.and_then(|id| {
            self.entities
                .iter()
                .find(|e| e.id == id)
                .map(|e| e.name.clone())
        });
        state.layers = entity.and_then(|id| engine.terrain_layers(id));
        state.palette = match (entity, state.layers) {
            (Some(id), Some(layers)) if layers.enabled => engine.palette_layers(id),
            _ => Vec::new(),
        };
        let last = state.palette.len().saturating_sub(1);
        state.selected = state.selected.min(last);
        state.from_layer = state.from_layer.min(last);
        state.fallback = state.fallback.min(last);
        for rule in &mut state.rules {
            rule.layer = rule.layer.min(last);
        }
    }

    pub(super) fn show_terrain_paint_pane(&mut self) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::TerrainPaint)
            {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Inspector) {
                    group.pages.push(PaneKind::TerrainPaint);
                    group.active = group.pages.len() - 1;
                    break;
                }
            }
        }
        self.ensure_terrain_library();
        self.refresh_paint_state();
    }

    fn set_paint_tool(&mut self, on: bool) {
        if on {
            self.ensure_terrain_library();
            // One tool at a time: sculpting and painting share the left button.
            if self.terrain_editor.active {
                self.set_terrain_tool(false);
            }
            if self.terrain_vegetation.active {
                self.terrain_vegetation.active = false;
            }
            self.texture_set_paint_mode = false;
            self.refresh_paint_state();
        } else {
            self.finish_terrain_stroke();
        }
        self.terrain_paint.active = on;
        let radius = self.terrain_paint.radius;
        self.set_terrain_brush_cursor(on, radius);
        self.log(
            LogLevel::Action,
            if on {
                "Terrain paint on"
            } else {
                "Terrain paint off"
            },
        );
    }

    /// Applies a changed palette as one undo step; an edit with the same `key` as the one just before it folds into it.
    fn apply_palette(&mut self, layers: &[PaletteLayer], name: &str, key: Option<String>) -> bool {
        let Some(entity) = self.terrain_paint.entity else {
            return false;
        };
        let text = format_palette(layers);
        let merge = key
            .as_ref()
            .and_then(|key| {
                self.terrain_paint
                    .merge
                    .as_ref()
                    .filter(|(k, _, length, cursor)| {
                        k == key
                            && *length == self.history.len()
                            && *cursor == self.history.cursor()
                    })
                    .map(|(_, record, _, _)| *record)
            })
            .unwrap_or(0);
        let Some(engine) = self.engine.as_mut() else {
            return false;
        };
        match engine.palette_apply(entity, &text, merge) {
            Ok(0) => {
                self.refresh_paint_state();
                true
            }
            Ok(record) => {
                if record != merge {
                    self.commit_terrain_stroke(record, name.to_owned());
                }
                self.terrain_paint.merge =
                    key.map(|key| (key, record, self.history.len(), self.history.cursor()));
                self.terrain_paint.status = None;
                self.refresh_paint_state();
                true
            }
            Err(error) => {
                self.log(LogLevel::Error, format!("Palette: {error}"));
                self.terrain_paint.status = Some(format!("Palette: {error}"));
                false
            }
        }
    }

    /// An edit of the selected layer.
    fn edit_selected_layer(
        &mut self,
        name: &str,
        key: &str,
        change: impl FnOnce(&mut PaletteLayer),
    ) {
        let index = self.terrain_paint.selected;
        let mut layers = self.terrain_paint.palette.clone();
        let Some(layer) = layers.get_mut(index) else {
            return;
        };
        let before = layer.clone();
        change(layer);
        if *layer == before {
            return;
        }
        self.apply_palette(&layers, name, Some(format!("layer{index}.{key}")));
    }

    pub(super) fn edit_terrain_paint(&mut self, edit: Edit) {
        self.refresh_paint_state();
        match edit {
            Edit::Active(on) => self.set_paint_tool(on),
            Edit::EnableLayers(on) => {
                let (Some(entity), Some(engine)) =
                    (self.terrain_paint.entity, self.engine.as_mut())
                else {
                    return;
                };
                match engine.terrain_layers_enable(entity, on) {
                    Ok(()) => {
                        self.commit(
                            if on {
                                "Turn on terrain layers"
                            } else {
                                "Turn off terrain layers"
                            },
                            None,
                        );
                        self.log(
                            LogLevel::Action,
                            if on {
                                "Terrain layers on"
                            } else {
                                "Terrain layers off"
                            },
                        );
                        self.terrain_paint.status = None;
                    }
                    Err(error) => {
                        self.terrain_paint.status =
                            Some(format!("Could not change the layers: {error:?}"));
                    }
                }
                self.refresh_paint_state();
            }
            Edit::SelectLayer(index) => {
                if index < self.terrain_paint.palette.len() {
                    self.terrain_paint.selected = index;
                }
            }
            Edit::AddLayer => {
                let mut layers = self.terrain_paint.palette.clone();
                if layers.len() >= doppio_engine::PALETTE_MAX {
                    self.terrain_paint.status =
                        Some("A palette holds at most 64 layers".to_owned());
                    return;
                }
                layers.push(new_layer(format!("Layer {}", layers.len())));
                if self.apply_palette(&layers, "Add terrain layer", None) {
                    self.terrain_paint.selected = layers.len() - 1;
                }
            }
            Edit::AddLayerWith {
                name,
                albedo,
                tint,
                tile_size_m,
            } => {
                let mut layers = self.terrain_paint.palette.clone();
                if layers.len() >= doppio_engine::PALETTE_MAX {
                    self.terrain_paint.status =
                        Some("A palette holds at most 64 layers".to_owned());
                    return;
                }
                let mut layer = new_layer(name);
                layer.albedo = albedo;
                layer.tint = tint;
                layer.tile_size_m = tile_size_m.clamp(0.5, 100.0);
                layers.push(layer);
                if self.apply_palette(&layers, "Add terrain layer", None) {
                    self.terrain_paint.selected = layers.len() - 1;
                }
            }
            Edit::AddLayerFromAsset => {
                let picked = self
                    .asset_selected
                    .and_then(|index| self.assets.get(index))
                    .map(|asset| (asset.name.clone(), asset.relative_path.clone()));
                let Some((name, path)) = picked else {
                    self.terrain_paint.status =
                        Some("Select a picture in the Assets panel first".to_owned());
                    return;
                };
                let mut layers = self.terrain_paint.palette.clone();
                if layers.len() >= doppio_engine::PALETTE_MAX {
                    self.terrain_paint.status =
                        Some("A palette holds at most 64 layers".to_owned());
                    return;
                }
                let mut layer = new_layer(
                    std::path::Path::new(&name)
                        .file_stem()
                        .map_or(name.clone(), |s| s.to_string_lossy().into_owned()),
                );
                layer.albedo = path;
                layer.tint = [1.0, 1.0, 1.0];
                layers.push(layer);
                if self.apply_palette(&layers, "Add terrain layer", None) {
                    self.terrain_paint.selected = layers.len() - 1;
                }
            }
            Edit::RemoveLayer => {
                let (Some(entity), index) =
                    (self.terrain_paint.entity, self.terrain_paint.selected)
                else {
                    return;
                };
                let Some(engine) = self.engine.as_mut() else {
                    return;
                };
                match engine.palette_remove(entity, index as u32) {
                    Ok(record) if record != 0 => {
                        self.commit_terrain_stroke(record, "Remove terrain layer".to_owned());
                        self.terrain_paint.status = None;
                    }
                    Ok(_) => {}
                    Err(_) => {
                        self.terrain_paint.status =
                            Some("The last layer cannot be removed".to_owned());
                    }
                }
                self.refresh_paint_state();
            }
            Edit::MoveLayer(delta) => {
                let (Some(entity), index) =
                    (self.terrain_paint.entity, self.terrain_paint.selected)
                else {
                    return;
                };
                let target = index as i64 + i64::from(delta);
                if target < 0 || target >= self.terrain_paint.palette.len() as i64 {
                    return;
                }
                let Some(engine) = self.engine.as_mut() else {
                    return;
                };
                if let Ok(record) = engine.palette_move(entity, index as u32, target as u32) {
                    if record != 0 {
                        self.commit_terrain_stroke(record, "Move terrain layer".to_owned());
                    }
                    self.terrain_paint.selected = target as usize;
                }
                self.refresh_paint_state();
            }
            Edit::LayerName(name) => {
                self.edit_selected_layer("Rename terrain layer", "name", |l| l.name = name)
            }
            Edit::LayerAlbedo(path) => {
                self.edit_selected_layer("Set terrain layer picture", "albedo", |l| l.albedo = path)
            }
            Edit::LayerNumber(number, value) => {
                self.edit_selected_layer("Edit terrain layer", number.key(), |l| {
                    number.set(l, value)
                })
            }
            Edit::LayerResolution(resolution) => {
                self.edit_selected_layer("Edit terrain layer", "resolution", |l| {
                    l.resolution = resolution
                })
            }
            Edit::LayerLocked(locked) => {
                self.edit_selected_layer("Edit terrain layer", "locked", |l| l.locked = locked)
            }
            Edit::Mode(mode) => self.terrain_paint.mode = mode,
            Edit::FromLayer(index) => self.terrain_paint.from_layer = index,
            Edit::Brush(amount, value) => {
                let range = amount.range();
                let value = value.clamp(*range.start(), *range.end());
                match amount {
                    BrushAmount::Radius => {
                        self.terrain_paint.radius = value;
                        if self.terrain_paint.active {
                            self.set_terrain_brush_cursor(true, value);
                        }
                    }
                    BrushAmount::Strength => self.terrain_paint.strength = value,
                    BrushAmount::Spacing => self.terrain_paint.spacing = value,
                }
            }
            Edit::Falloff(falloff) => self.terrain_paint.falloff = falloff,
            Edit::SharedShape(on) => self.terrain_paint.shared_shape = on,
            Edit::AddRule(mask) => {
                if !RULE_MASKS.contains(&mask.as_str())
                    || self.terrain_paint.rules.len() >= MAX_RULES
                {
                    return;
                }
                let corners = match mask.as_str() {
                    "slope" => [0.3, 0.4, 1.0, 1.0],
                    "height" => [0.6, 0.7, 1.0, 1.0],
                    _ => [0.55, 0.6, 1.0, 1.0],
                };
                let layer = self.terrain_paint.selected;
                self.terrain_paint.rules.push(AutoRule {
                    layer,
                    mask,
                    corners,
                });
            }
            Edit::RemoveRule(index) => {
                if index < self.terrain_paint.rules.len() {
                    self.terrain_paint.rules.remove(index);
                }
            }
            Edit::RuleLayer(index, layer) => {
                let last = self.terrain_paint.palette.len().saturating_sub(1);
                if let Some(rule) = self.terrain_paint.rules.get_mut(index) {
                    rule.layer = layer.min(last);
                }
            }
            Edit::RuleCorner(index, corner, value) => {
                if let Some(rule) = self.terrain_paint.rules.get_mut(index) {
                    let at = corner.index();
                    rule.corners[at] = value.clamp(0.0, 1.0);
                    // The four numbers never decrease: pull the others along.
                    for i in (at + 1)..4 {
                        rule.corners[i] = rule.corners[i].max(rule.corners[at]);
                    }
                    for i in (0..at).rev() {
                        rule.corners[i] = rule.corners[i].min(rule.corners[at]);
                    }
                }
            }
            Edit::Fallback(layer) => {
                let last = self.terrain_paint.palette.len().saturating_sub(1);
                self.terrain_paint.fallback = layer.min(last);
            }
            Edit::ApplyRules => self.apply_auto_layer(),
        }
    }

    fn apply_auto_layer(&mut self) {
        let Some(entity) = self.terrain_paint.entity else {
            return;
        };
        if self.terrain_paint.rules.is_empty() {
            self.terrain_paint.status = Some("Add a rule first".to_owned());
            return;
        }
        let text = rules_text(&self.terrain_paint.rules);
        let fallback = self.terrain_paint.fallback as u32;
        let Some(engine) = self.engine.as_mut() else {
            return;
        };
        match engine.terrain_autolayer(entity, &text, fallback, None) {
            Ok(0) => self.terrain_paint.status = Some("The rules changed nothing".to_owned()),
            Ok(record) => {
                self.commit_terrain_stroke(record, "Auto-layer terrain".to_owned());
                self.log(LogLevel::Action, "Auto-layered the terrain");
                self.terrain_paint.status = None;
            }
            Err(error) => {
                self.log(LogLevel::Error, format!("Auto-layer: {error}"));
                self.terrain_paint.status = Some(format!("Auto-layer: {error}"));
            }
        }
        self.refresh_paint_state();
    }

    /// The paint brush a stroke starts with: the panel's numbers, on the Terrain Brush's shape when asked for, with the
    /// held modifiers applied (Ctrl turns paint into erase and back, Shift smooths).
    pub(super) fn paint_brush(&self, smooth: bool) -> PaintBrush {
        let state = &self.terrain_paint;
        let mut shape = if state.shared_shape {
            self.terrain_editor.working.clone()
        } else {
            BrushPreset::default()
        };
        shape.radius_m = state.radius;
        shape.strength = state.strength;
        shape.spacing_m = state.spacing;
        shape.falloff = state.falloff;
        let mut mode = state.mode;
        if smooth {
            mode = PaintMode::Smooth;
        } else if self.terrain_editor.invert {
            mode = match mode {
                PaintMode::Paint => PaintMode::Erase,
                PaintMode::Erase => PaintMode::Paint,
                other => other,
            };
        }
        PaintBrush {
            shape,
            mode,
            layer: state.selected as u32,
            from_layer: state.from_layer as u32,
        }
    }

    /// Opens a paint stroke on the terrain under the cursor (the mouse path). `false` when there is none, or its layers
    /// are off.
    pub(super) fn begin_paint_stroke(&mut self) -> bool {
        if self.terrain_editor.stroke.is_some() || self.views.session {
            return false;
        }
        let Some((entity, hit)) = self.terrain_cursor.hit else {
            return false;
        };
        if self.terrain_paint.entity != Some(entity) {
            self.terrain_paint.entity = Some(entity);
            self.refresh_paint_state();
        }
        if !self.terrain_paint.layers.is_some_and(|l| l.enabled) {
            self.terrain_paint.status =
                Some("Turn on layered painting for this terrain first".to_owned());
            return false;
        }
        let brush = self.paint_brush(self.fly_boost);
        if !self.open_paint_stroke(entity, &brush) {
            return false;
        }
        self.add_terrain_stroke_point(hit);
        true
    }

    /// Opens an engine paint stroke; records the failure in the status line.
    pub(super) fn open_paint_stroke(&mut self, entity: u32, brush: &PaintBrush) -> bool {
        let Some(engine) = self.engine.as_mut() else {
            return false;
        };
        match engine.terrain_paint_begin(entity, brush) {
            Ok(()) => {
                self.terrain_editor.stroke = Some(super::terraineditor::OpenStroke {
                    entity,
                    points: 0,
                    paint: true,
                    vegetation: false,
                });
                true
            }
            Err(error) => {
                let message = format!("Could not start painting: {error:?}");
                self.log(LogLevel::Error, message.clone());
                self.terrain_paint.status = Some(message);
                false
            }
        }
    }

    /// One whole paint stroke along world-space points, as an agent or a test makes it: the same path as the mouse.
    pub(super) fn paint_stroke_along(
        &mut self,
        terrain: Option<u32>,
        points: &[[f64; 3]],
    ) -> Result<doppio_engine::StrokeSummary, String> {
        self.ensure_terrain_library();
        if points.is_empty() {
            return Err("a stroke needs at least one point".to_owned());
        }
        if self.views.session {
            return Err("stop the game before painting".to_owned());
        }
        self.finish_terrain_stroke();
        self.refresh_paint_state();
        let entity = terrain
            .or(self.terrain_paint.entity)
            .ok_or("there is no terrain to paint")?;
        if !self.terrain_paint.layers.is_some_and(|l| l.enabled) {
            return Err("turn on layered painting first".to_owned());
        }
        let brush = self.paint_brush(false);
        if !self.open_paint_stroke(entity, &brush) {
            return Err(self
                .terrain_paint
                .status
                .clone()
                .unwrap_or_else(|| "could not start painting".to_owned()));
        }
        for point in points {
            self.add_terrain_stroke_point(*point);
        }
        self.finish_terrain_stroke()
            .ok_or_else(|| "the stroke could not be finished".to_owned())
    }

    /// `[` and `]` while painting: the brush grows or shrinks by `factor`.
    pub(super) fn scale_paint_brush(&mut self, factor: f32) {
        let radius = (self.terrain_paint.radius * factor).clamp(0.1, 500.0);
        self.terrain_paint.radius = radius;
        if self.terrain_paint.active {
            self.set_terrain_brush_cursor(true, radius);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn layer(name: &str) -> PaletteLayer {
        PaletteLayer {
            name: name.to_owned(),
            albedo: "Textures/rock one.png".to_owned(),
            normal: String::new(),
            tile_size_m: 4.5,
            rotation_degrees: 30.0,
            tint: [0.5, 0.25, 1.0],
            resolution: 2048,
            locked: true,
            texels: 0,
        }
    }

    #[test]
    fn a_palette_is_written_in_the_engines_format() {
        let text = format_palette(&[layer("Mossy rock"), new_layer("Plain".to_owned())]);
        assert!(text.contains("layer Mossy rock\n  albedo Textures/rock one.png\n"));
        assert!(text.contains(
            "  tile 4.5\n  rotation 30\n  tint 0.5 0.25 1\n  resolution 2048\n  locked 1\n"
        ));
        // A layer with no picture or lock writes neither line.
        let plain = text.split("layer Plain").nth(1).unwrap();
        assert!(!plain.contains("albedo") && !plain.contains("locked"));
    }

    #[test]
    fn names_cannot_break_the_file() {
        let text = format_palette(&[layer("Evil # name\nlayer Injected")]);
        assert_eq!(
            text.matches("layer ").count(),
            2,
            "one real layer line, one inside the name: {text}"
        );
        assert!(!text.contains('#') || text.starts_with("# caffeine"));
        let empty = format_palette(&[layer("   ")]);
        assert!(empty.contains("layer Layer\n"));
    }

    #[test]
    fn rules_on_one_layer_are_one_class_whose_terms_all_hold() {
        let rules = [
            AutoRule {
                layer: 1,
                mask: "slope".to_owned(),
                corners: [0.3, 0.4, 1.0, 1.0],
            },
            AutoRule {
                layer: 2,
                mask: "height".to_owned(),
                corners: [0.6, 0.7, 1.0, 1.0],
            },
            AutoRule {
                layer: 1,
                mask: "height".to_owned(),
                corners: [0.0, 0.0, 0.5, 0.6],
            },
        ];
        assert_eq!(
            rules_text(&rules),
            "class 1 base 1\n  slope 0.3 0.4 1 1\n  height 0 0 0.5 0.6\nclass 2 base 1\n  height 0.6 0.7 1 1\n"
        );
        assert_eq!(rules_text(&[]), "");
    }
}

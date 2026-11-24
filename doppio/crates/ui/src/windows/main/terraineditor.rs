//! Editor side of the **Terrain Brush** pane (terrain editing roadmap, phase 1): the brush library, the working brush
//! and the sculpting tool.
//!
//! The library is the engine's built-in brushes (a procedural stamp each, plus a tool for every mode) followed by the
//! `.brush` files of the project's `Brushes` folder. Choosing one copies it into the **working brush**, which the
//! panel edits freely; the library itself changes only when a brush is saved. With the tool on, a left press on the
//! terrain opens an engine stroke, every move of the mouse adds the point under it and the release closes the stroke
//! and records one undo step that carries the engine's stroke record (see `history.rs`: terrain heights are not in
//! the scene snapshot, so undo reverts the record tile by tile). The panel only draws; every change arrives as a
//! [`Edit`] message, so an agent can do whatever the mouse can (`main/agent.rs`).

use super::{App, LogLevel, PaneKind};
use crate::history;
use crate::uikit::panels::terrain_brush::{Amount, Corner, Edit, Entry, PROTECT_MASKS};
use doppio_engine::{BrushMode, BrushPreset, BrushProtect, Falloff, StampKind};
use iced::widget::image::Handle;

/// The folder of the project that holds the user's `.brush` files.
pub const BRUSH_FOLDER: &str = "Brushes";
/// The category of every brush read from that folder (and of a new one).
pub const CUSTOM_CATEGORY: &str = "Custom";
/// Side of a library thumbnail in pixels (drawn at `brush_card::THUMBNAIL` logical pixels).
const THUMBNAIL_PIXELS: u32 = 64;

/// A stroke that is open: the terrain it edits and the stamps laid so far.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(super) struct OpenStroke {
    pub entity: u32,
    pub points: u32,
}

#[derive(Default)]
pub(super) struct State {
    pub active: bool,
    pub library: Vec<BrushPreset>,
    pub thumbnails: Vec<Option<Handle>>,
    pub selected: usize,
    /// The brush being edited: a copy of a library brush with the user's changes.
    pub working: BrushPreset,
    pub search: String,
    pub category: Option<String>,
    pub status: Option<String>,
    pub stroke: Option<OpenStroke>,
    /// Ctrl is held: raise paints lower, pinch paints bulge, keep-higher paints keep-lower.
    pub invert: bool,
    /// The scene has at least one terrain (read when the pane opens or the tool turns on).
    pub terrain_present: bool,
    loaded: bool,
}

impl State {
    /// The categories of the library in the order they first appear.
    pub fn categories(&self) -> Vec<String> {
        let mut seen: Vec<String> = Vec::new();
        for brush in &self.library {
            if !seen.contains(&brush.category) {
                seen.push(brush.category.clone());
            }
        }
        seen
    }

    /// The library indices the search and the category chip leave on show.
    pub fn visible(&self) -> Vec<usize> {
        let needle = self.search.trim().to_lowercase();
        self.library
            .iter()
            .enumerate()
            .filter(|(_, brush)| {
                self.category
                    .as_ref()
                    .is_none_or(|category| *category == brush.category)
                    && (needle.is_empty()
                        || brush.name.to_lowercase().contains(&needle)
                        || brush.category.to_lowercase().contains(&needle)
                        || brush.mode.label().to_lowercase().contains(&needle))
            })
            .map(|(index, _)| index)
            .collect()
    }

    /// The brush a stroke starts with: the working brush with the held modifiers applied.
    /// `smooth` (Shift) turns any brush into a plain smoothing brush of the same size.
    pub fn effective(&self, smooth: bool) -> BrushPreset {
        let mut brush = self.working.clone();
        if smooth {
            brush.mode = BrushMode::Smooth;
            brush.stamp_kind = StampKind::None;
            brush.stamp.clear();
            brush.signed_stamp = false;
            brush.falloff = Falloff::Smooth;
            brush.protect.clear();
        } else if self.invert {
            brush.mode = brush.mode.inverted();
        }
        brush
    }
}

/// The RGBA picture of a brush: its stamp (grey, 0..1) or, for a brush without one, the falloff curve as a round dab.
pub fn thumbnail_rgba(size: u32, stamp: Option<&[f32]>, falloff: Falloff) -> Vec<u8> {
    let size = size.max(2);
    let mut pixels = Vec::with_capacity((size * size * 4) as usize);
    for y in 0..size {
        for x in 0..size {
            let value = match stamp {
                Some(stamp) if stamp.len() >= (size * size) as usize => {
                    stamp[(y * size + x) as usize]
                }
                _ => {
                    let dx = (x as f32 + 0.5) / size as f32 * 2.0 - 1.0;
                    let dy = (y as f32 + 0.5) / size as f32 * 2.0 - 1.0;
                    falloff.weight(1.0 - (dx * dx + dy * dy).sqrt())
                }
            };
            let grey = (value.clamp(0.0, 1.0) * 215.0) as u8 + 20;
            pixels.extend_from_slice(&[grey, grey, grey, 255]);
        }
    }
    pixels
}

/// The file name a brush is saved under: its name with anything but letters, digits, `-` and `_` turned into `_`.
pub fn file_stem(name: &str) -> String {
    let stem: String = name
        .trim()
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '-' || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect();
    if stem.trim_matches('_').is_empty() {
        "Brush".to_owned()
    } else {
        stem
    }
}

impl App {
    /// Loads the library once: the engine's brushes, then the project's. Cheap after the first call.
    pub(super) fn ensure_terrain_library(&mut self) {
        if self.terrain_editor.loaded {
            return;
        }
        let Some(engine) = self.engine.as_ref() else {
            return;
        };
        if !engine.has_brush_library() {
            self.terrain_editor.status =
                Some("This engine library has no brush library (C ABI 28).".to_owned());
            return;
        }
        let library = engine.builtin_brushes();
        self.terrain_editor.loaded = true;
        self.terrain_editor.library = library;
        self.read_custom_brushes();
        self.rebuild_brush_thumbnails();
        if let Some(first) = self.terrain_editor.library.first().cloned() {
            self.terrain_editor.working = first;
            self.terrain_editor.selected = 0;
        }
        self.refresh_terrain_presence();
    }

    /// Replaces the user's brushes of the library with the `.brush` files of the project's `Brushes` folder.
    fn read_custom_brushes(&mut self) {
        self.terrain_editor
            .library
            .retain(|brush| brush.category != CUSTOM_CATEGORY);
        let Some(root) = self.asset_root_dir() else {
            return;
        };
        let folder = root.join(BRUSH_FOLDER);
        let Ok(entries) = std::fs::read_dir(&folder) else {
            return;
        };
        let mut files: Vec<std::path::PathBuf> = entries
            .filter_map(Result::ok)
            .map(|entry| entry.path())
            .filter(|path| path.extension().is_some_and(|ext| ext == "brush"))
            .collect();
        files.sort();
        let mut problems = Vec::new();
        let mut loaded = Vec::new();
        for file in files {
            let name = file
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_default();
            let parsed = std::fs::read_to_string(&file)
                .map_err(|e| e.to_string())
                .and_then(|text| {
                    self.engine
                        .as_ref()
                        .ok_or_else(|| "no engine".to_owned())?
                        .parse_brush(&text)
                });
            match parsed {
                Ok(mut brush) => {
                    brush.category = CUSTOM_CATEGORY.to_owned();
                    if brush.name.is_empty() {
                        brush.name = name.trim_end_matches(".brush").to_owned();
                    }
                    loaded.push(brush);
                }
                Err(error) => problems.push(format!("{BRUSH_FOLDER}/{name}: {error}")),
            }
        }
        self.terrain_editor.library.extend(loaded);
        if !problems.is_empty() {
            let note = format!("Could not read {}", problems.join("; "));
            self.log(LogLevel::Error, note.clone());
            self.terrain_editor.status = Some(note);
        }
    }

    fn rebuild_brush_thumbnails(&mut self) {
        let Some(engine) = self.engine.as_ref() else {
            return;
        };
        let thumbnails = self
            .terrain_editor
            .library
            .iter()
            .map(|brush| {
                let stamp = (brush.stamp_kind == StampKind::Procedural)
                    .then(|| engine.brush_stamp_image(&brush.stamp, THUMBNAIL_PIXELS, brush.seed))
                    .flatten();
                Some(Handle::from_rgba(
                    THUMBNAIL_PIXELS,
                    THUMBNAIL_PIXELS,
                    thumbnail_rgba(THUMBNAIL_PIXELS, stamp.as_deref(), brush.falloff),
                ))
            })
            .collect();
        self.terrain_editor.thumbnails = thumbnails;
    }

    /// Whether the scene has a terrain: read from the entities' components, so only when it matters.
    pub(super) fn refresh_terrain_presence(&mut self) {
        let present = self.engine.as_ref().is_some_and(|engine| {
            self.entities.iter().any(|entity| {
                engine
                    .entity_components(entity.id)
                    .iter()
                    .any(|component| component.name == "Terrain")
            })
        });
        self.terrain_editor.terrain_present = present;
    }

    pub(super) fn show_terrain_brush_pane(&mut self) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::TerrainBrush)
            {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Inspector) {
                    group.pages.push(PaneKind::TerrainBrush);
                    group.active = group.pages.len() - 1;
                    break;
                }
            }
        }
        self.ensure_terrain_library();
        self.refresh_terrain_presence();
    }

    /// What the panel draws: the visible brushes, with thumbnails.
    pub(super) fn terrain_brush_entries(&self) -> Vec<Entry<'_>> {
        self.terrain_editor
            .visible()
            .into_iter()
            .map(|index| {
                let brush = &self.terrain_editor.library[index];
                Entry {
                    index,
                    name: &brush.name,
                    detail: brush.mode.label(),
                    thumbnail: self
                        .terrain_editor
                        .thumbnails
                        .get(index)
                        .and_then(Option::as_ref),
                }
            })
            .collect()
    }

    pub(super) fn set_terrain_tool(&mut self, on: bool) {
        self.ensure_terrain_library();
        if on {
            self.refresh_terrain_presence();
            // One tool at a time: painting a texture set and sculpting a terrain both use the left button.
            self.texture_set_paint_mode = false;
        } else {
            self.finish_terrain_stroke();
        }
        self.terrain_editor.active = on;
        let radius = self.terrain_editor.working.radius_m;
        self.set_terrain_brush_cursor(on, radius);
        self.log(
            LogLevel::Action,
            if on {
                "Terrain brush on"
            } else {
                "Terrain brush off"
            },
        );
    }

    pub(super) fn select_terrain_brush(&mut self, index: usize) {
        self.ensure_terrain_library();
        let Some(brush) = self.terrain_editor.library.get(index).cloned() else {
            return;
        };
        self.terrain_editor.selected = index;
        self.terrain_editor.working = brush;
        self.sync_terrain_cursor_radius();
    }

    fn sync_terrain_cursor_radius(&mut self) {
        if self.terrain_editor.active {
            let radius = self.terrain_editor.working.radius_m;
            self.set_terrain_brush_cursor(true, radius);
        }
    }

    /// Selects the brush called `name` (case-insensitive); `false` when the library has none.
    pub(super) fn select_terrain_brush_named(&mut self, name: &str) -> bool {
        self.ensure_terrain_library();
        let found = self
            .terrain_editor
            .library
            .iter()
            .position(|brush| brush.name.eq_ignore_ascii_case(name));
        if let Some(index) = found {
            self.select_terrain_brush(index);
        }
        found.is_some()
    }

    pub(super) fn edit_terrain_brush(&mut self, edit: Edit) {
        self.ensure_terrain_library();
        match edit {
            Edit::Active(on) => self.set_terrain_tool(on),
            Edit::Search(text) => self.terrain_editor.search = text,
            Edit::Category(category) => self.terrain_editor.category = category,
            Edit::Select(index) => self.select_terrain_brush(index),
            Edit::Mode(mode) => self.terrain_editor.working.mode = mode,
            Edit::Falloff(falloff) => self.terrain_editor.working.falloff = falloff,
            Edit::Signed(signed) => self.terrain_editor.working.signed_stamp = signed,
            Edit::Symmetry(kind) => self.terrain_editor.working.symmetry = kind,
            Edit::Amount(amount, value) => {
                amount.set(&mut self.terrain_editor.working, value);
                if amount == Amount::Radius {
                    self.sync_terrain_cursor_radius();
                }
            }
            // A band across the whole range to start with: the user narrows it.
            Edit::AddProtect(mask) => self.add_protection(mask, [0.0, 0.0, 1.0, 1.0]),
            Edit::AddProtectRange(mask, corners) => self.add_protection(mask, corners),
            Edit::ClearProtect => self.terrain_editor.working.protect.clear(),
            Edit::RemoveProtect(index) => {
                if index < self.terrain_editor.working.protect.len() {
                    self.terrain_editor.working.protect.remove(index);
                }
            }
            Edit::Protect(index, corner, value) => {
                if let Some(term) = self.terrain_editor.working.protect.get_mut(index) {
                    let value = value.clamp(0.0, 1.0);
                    match corner {
                        Corner::Rise0 => term.rise0 = value,
                        Corner::Rise1 => term.rise1 = value,
                        Corner::Fall1 => term.fall1 = value,
                        Corner::Fall0 => term.fall0 = value,
                    }
                    // The four numbers of a term never decrease: pull the others along.
                    term.rise1 = term.rise1.max(term.rise0);
                    term.fall1 = term.fall1.max(term.rise1);
                    term.fall0 = term.fall0.max(term.fall1);
                    match corner {
                        Corner::Fall0 => {
                            term.fall1 = term.fall1.min(term.fall0);
                            term.rise1 = term.rise1.min(term.fall1);
                            term.rise0 = term.rise0.min(term.rise1);
                        }
                        Corner::Fall1 => {
                            term.rise1 = term.rise1.min(term.fall1);
                            term.rise0 = term.rise0.min(term.rise1);
                        }
                        Corner::Rise1 => term.rise0 = term.rise0.min(term.rise1),
                        Corner::Rise0 => {}
                    }
                }
            }
            Edit::Name(name) => self.terrain_editor.working.name = name,
            Edit::Save => self.save_terrain_brush(),
            Edit::Revert => {
                let index = self.terrain_editor.selected;
                self.select_terrain_brush(index);
            }
            Edit::Reload => {
                self.terrain_editor.status = None;
                self.read_custom_brushes();
                self.rebuild_brush_thumbnails();
                self.terrain_editor.selected = self
                    .terrain_editor
                    .selected
                    .min(self.terrain_editor.library.len().saturating_sub(1));
                self.refresh_terrain_presence();
                // A file that could not be read has already said so; do not bury it.
                if self.terrain_editor.status.is_none() {
                    self.terrain_editor.status = Some(format!(
                        "{} brushes in the library",
                        self.terrain_editor.library.len()
                    ));
                }
            }
        }
    }

    fn add_protection(&mut self, mask: String, corners: [f32; 4]) {
        if !PROTECT_MASKS.contains(&mask.as_str()) {
            self.terrain_editor.status = Some(format!(
                "No mask called {mask}: use slope, height or curvature"
            ));
            return;
        }
        if self.terrain_editor.working.protect.len() >= 4 {
            self.terrain_editor.status =
                Some("A brush holds at most four protection terms".to_owned());
            return;
        }
        // The four numbers of a term never decrease.
        let mut sorted = corners.map(|c| c.clamp(0.0, 1.0));
        sorted.sort_by(f32::total_cmp);
        self.terrain_editor.working.protect.push(BrushProtect {
            mask,
            rise0: sorted[0],
            rise1: sorted[1],
            fall1: sorted[2],
            fall0: sorted[3],
        });
    }

    /// Writes the working brush to `Brushes/<name>.brush` of the project and selects it in the library.
    fn save_terrain_brush(&mut self) {
        let Some(root) = self.asset_root_dir() else {
            self.terrain_editor.status =
                Some("Open a project first: brushes are saved in its Brushes folder.".to_owned());
            return;
        };
        let mut brush = self.terrain_editor.working.clone();
        if brush.name.trim().is_empty() {
            brush.name = "Brush".to_owned();
        }
        brush.category = CUSTOM_CATEGORY.to_owned();
        let Some(text) = self
            .engine
            .as_ref()
            .and_then(|engine| engine.format_brush(&brush))
        else {
            self.terrain_editor.status =
                Some("Could not write the brush: the engine library cannot format it.".to_owned());
            return;
        };
        let folder = root.join(BRUSH_FOLDER);
        let file = folder.join(format!("{}.brush", file_stem(&brush.name)));
        let written = std::fs::create_dir_all(&folder).and_then(|()| std::fs::write(&file, text));
        match written {
            Ok(()) => {
                let message = format!(
                    "Saved {BRUSH_FOLDER}/{}",
                    file.file_name()
                        .map_or_else(String::new, |n| n.to_string_lossy().into_owned())
                );
                self.log(LogLevel::Action, message.clone());
                self.terrain_editor.status = Some(message);
                self.read_custom_brushes();
                self.rebuild_brush_thumbnails();
                let name = brush.name.clone();
                self.select_terrain_brush_named(&name);
            }
            Err(error) => {
                let message = format!("Could not save the brush: {error}");
                self.log(LogLevel::Error, message.clone());
                self.terrain_editor.status = Some(message);
            }
        }
    }

    // ------------------------------------------------------------------------------------------------ the stroke

    /// Opens a stroke on the terrain under the cursor. `false` when there is no tool, no terrain hit or the engine
    /// refuses.
    pub(super) fn begin_terrain_stroke(&mut self) -> bool {
        if !self.terrain_editor.active || self.terrain_editor.stroke.is_some() || self.views.session
        {
            return false;
        }
        let Some((entity, hit)) = self.terrain_cursor.hit else {
            return false;
        };
        let brush = self.terrain_editor.effective(self.fly_boost);
        if !self.open_terrain_stroke(entity, &brush) {
            return false;
        }
        self.add_terrain_stroke_point(hit);
        true
    }

    /// Opens an engine stroke with `brush` on `entity`; records the failure in the status line.
    fn open_terrain_stroke(&mut self, entity: u32, brush: &BrushPreset) -> bool {
        let Some(engine) = self.engine.as_mut() else {
            return false;
        };
        match engine.terrain_stroke_begin_preset(entity, brush) {
            Ok(()) => {
                self.terrain_editor.stroke = Some(OpenStroke { entity, points: 0 });
                true
            }
            Err(error) => {
                let message = format!("Could not start the stroke: {error:?}");
                self.log(LogLevel::Error, message.clone());
                self.terrain_editor.status = Some(message);
                false
            }
        }
    }

    pub(super) fn add_terrain_stroke_point(&mut self, world: [f64; 3]) {
        let Some(engine) = self.engine.as_mut() else {
            return;
        };
        if engine.terrain_stroke_point(world, 1.0).is_ok() {
            if let Some(stroke) = self.terrain_editor.stroke.as_mut() {
                stroke.points += 1;
            }
        }
        // The pointer moves are passive messages; a stroke changes what the viewport shows.
        self.viewport_dirty = true;
        self.scene_epoch += 1;
    }

    /// Follows the mouse while a stroke is open.
    pub(super) fn extend_terrain_stroke(&mut self) {
        if self.terrain_editor.stroke.is_none() {
            return;
        }
        if let Some((_, hit)) = self.terrain_cursor.hit {
            self.add_terrain_stroke_point(hit);
        }
    }

    /// Closes the open stroke and records it as one undo step. Returns what it changed.
    pub(super) fn finish_terrain_stroke(&mut self) -> Option<doppio_engine::StrokeSummary> {
        let stroke = self.terrain_editor.stroke.take()?;
        let engine = self.engine.as_mut()?;
        let summary = match engine.terrain_stroke_end() {
            Ok(summary) => summary,
            Err(error) => {
                self.log(
                    LogLevel::Error,
                    format!("Could not end the stroke: {error:?}"),
                );
                return None;
            }
        };
        self.viewport_dirty = true;
        self.scene_epoch += 1;
        if summary.record != 0 {
            let name = format!("Sculpt {}", self.terrain_editor.working.name);
            self.commit_terrain_stroke(summary.record, name.clone());
            self.log(
                LogLevel::Action,
                format!(
                    "{name}: {} stamps, {} tiles, {} KiB of undo",
                    summary.stamps,
                    summary.tiles_changed,
                    summary.bytes / 1024
                ),
            );
        }
        let _ = stroke;
        Some(summary)
    }

    /// One whole stroke along world-space points (metres), as an agent or a test makes it: the same path as the mouse.
    pub(super) fn terrain_stroke_along(
        &mut self,
        terrain: Option<u32>,
        points: &[[f64; 3]],
    ) -> Result<doppio_engine::StrokeSummary, String> {
        self.ensure_terrain_library();
        if points.is_empty() {
            return Err("a stroke needs at least one point".to_owned());
        }
        if self.views.session {
            return Err("stop the game before sculpting".to_owned());
        }
        self.finish_terrain_stroke();
        let entity = terrain
            .or_else(|| self.entities.get(self.selected).map(|e| e.id))
            .ok_or("select a terrain first")?;
        let brush = self.terrain_editor.effective(false);
        if !self.open_terrain_stroke(entity, &brush) {
            return Err(self
                .terrain_editor
                .status
                .clone()
                .unwrap_or_else(|| "could not start the stroke".to_owned()));
        }
        for point in points {
            self.add_terrain_stroke_point(*point);
        }
        self.finish_terrain_stroke()
            .ok_or_else(|| "the stroke could not be finished".to_owned())
    }

    fn commit_terrain_stroke(&mut self, record: u32, name: String) {
        let mut after = self.capture_state();
        after.terrain_stroke = Some(record);
        self.history.push(after, name, None);
    }

    /// Applies the terrain side of an undo or redo step.
    pub(super) fn move_terrain_stroke(&mut self, stroke: history::StrokeMove) {
        let Some(engine) = self.engine.as_mut() else {
            return;
        };
        let result = match stroke {
            history::StrokeMove::Revert(record) => engine.terrain_stroke_revert(record),
            history::StrokeMove::Reapply(record) => engine.terrain_stroke_reapply(record),
        };
        if let Err(error) = result {
            self.log(
                LogLevel::Error,
                format!("The terrain stroke could not be moved ({error:?}): it is older than the undo memory of terrain strokes"),
            );
        }
    }

    /// `[` and `]`: the brush grows or shrinks by `factor` (kept between 0.1 m and 500 m).
    pub(super) fn scale_terrain_brush(&mut self, factor: f32) {
        let radius = (self.terrain_editor.working.radius_m * factor).clamp(0.1, 500.0);
        self.terrain_editor.working.radius_m = radius;
        self.sync_terrain_cursor_radius();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_stamp_thumbnail_follows_the_stamp() {
        let size = 4u32;
        let stamp: Vec<f32> = (0..16).map(|i| i as f32 / 15.0).collect();
        let pixels = thumbnail_rgba(size, Some(&stamp), Falloff::Smooth);
        assert_eq!(pixels.len(), 4 * 4 * 4);
        assert!(
            pixels[15 * 4] > pixels[0],
            "the lightest stamp value is the lightest pixel"
        );
        assert!(pixels
            .chunks(4)
            .all(|p| p[3] == 255 && p[0] == p[1] && p[1] == p[2]));
    }

    #[test]
    fn a_tool_thumbnail_is_a_round_dab() {
        let pixels = thumbnail_rgba(16, None, Falloff::Linear);
        let at = |x: usize, y: usize| pixels[(y * 16 + x) * 4];
        assert!(
            at(8, 8) > at(1, 8),
            "bright in the middle, dark at the edge"
        );
        assert_eq!(at(0, 0), at(15, 15));
    }

    #[test]
    fn file_names_are_safe() {
        assert_eq!(file_stem("My Brush/2"), "My_Brush_2");
        assert_eq!(file_stem("   "), "Brush");
        assert_eq!(file_stem("../../etc"), "______etc");
    }

    #[test]
    fn the_shift_brush_is_a_plain_smoother_of_the_same_size() {
        let mut state = State::default();
        state.working.radius_m = 12.0;
        state.working.mode = BrushMode::Raise;
        state.working.stamp_kind = StampKind::Procedural;
        state.working.stamp = "ridge".to_owned();
        let smooth = state.effective(true);
        assert_eq!(smooth.mode, BrushMode::Smooth);
        assert_eq!(smooth.radius_m, 12.0);
        assert_eq!(smooth.stamp_kind, StampKind::None);
    }

    #[test]
    fn ctrl_inverts_raise_to_lower() {
        let mut state = State::default();
        state.working.mode = BrushMode::Raise;
        state.invert = true;
        assert_eq!(state.effective(false).mode, BrushMode::Lower);
        state.working.mode = BrushMode::Flatten;
        assert_eq!(state.effective(false).mode, BrushMode::Flatten);
    }

    #[test]
    fn search_and_category_filter_the_library() {
        let mut state = State::default();
        for (name, category, mode) in [
            ("Soft Raise", "Tools", BrushMode::Raise),
            ("Crater Fresh", "Craters", BrushMode::StampMax),
            ("Dune Ripple", "Dunes", BrushMode::Raise),
        ] {
            state.library.push(BrushPreset {
                name: name.to_owned(),
                category: category.to_owned(),
                mode,
                ..BrushPreset::default()
            });
        }
        assert_eq!(state.visible(), vec![0, 1, 2]);
        state.search = "crat".to_owned();
        assert_eq!(state.visible(), vec![1]);
        state.search = String::new();
        state.category = Some("Dunes".to_owned());
        assert_eq!(state.visible(), vec![2]);
        state.category = None;
        state.search = "keep higher".to_owned();
        assert_eq!(state.visible(), vec![1], "the mode's name is searchable");
        assert_eq!(state.categories(), vec!["Tools", "Craters", "Dunes"]);
    }
}

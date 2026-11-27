//! Editor side of the **Terrain Path** pane (terrain editing roadmap, phase 5): roads and rivers drawn on a terrain.
//!
//! The tool works like the other terrain tools, with the mouse in the viewport: a left click on the ground drops a control
//! point (and dragging with the button held places it), a drag on a handle moves it, a right click on a handle removes it,
//! Backspace drops the last point, Enter applies and Escape starts over. Every click asks the engine for the path it
//! would follow (`terrain_spline_sample`), which draws as a ribbon over the ground (`components::path_overlay`) and gives the
//! elevation profile and the numbers of the panel: the user sees what a road will cut and fill before it exists. Apply is
//! one engine call (`terrain_spline_apply`) and one undo step; the engine shapes the ground, paints the layer and writes the
//! mask together. Every change arrives as an [`Edit`] message, so an agent can do what the mouse can.

use super::{App, LogLevel, PaneKind};
use crate::uikit::components::path_overlay::{self, PathOverlay, Sample};
use crate::uikit::components::viewport_math::Basis;
use crate::uikit::panels::terrain_path::{Edit, Number, PointRow, Profile, Stats, PRESETS};
use doppio_engine::{SplineDesc, SplineKind, SplinePoint};
use iced::{Point, Size};

/// Samples of the elevation chart.
const PROFILE_SAMPLES: usize = 96;

#[derive(Debug, Clone, Copy, PartialEq)]
pub(super) struct PathPoint {
    pub world: [f64; 3],
    /// 0 follows the path's width.
    pub width_m: f32,
}

pub(super) struct State {
    pub active: bool,
    /// The terrain the path is on.
    pub entity: Option<u32>,
    pub terrain_name: Option<String>,
    pub kind: SplineKind,
    pub preset: Option<usize>,
    pub width_m: f32,
    pub shoulder_m: f32,
    pub depth_m: f32,
    pub smooth_m: f32,
    /// Percent.
    pub grade_percent: f32,
    pub layer: Option<usize>,
    pub layer_names: Vec<String>,
    pub layer_strength: f32,
    pub write_mask: bool,
    pub mask_name: String,
    pub points: Vec<PathPoint>,
    /// The points as the panel lists them (kept with `points` by `rebuild_path_preview`).
    pub rows: Vec<PointRow>,
    pub selected: Option<usize>,
    /// The handle being dragged.
    pub drag: Option<usize>,
    /// The last mouse position over the viewport, to hit-test the handles.
    pub mouse: Option<Point>,
    /// A right click removed a point: its release must not frame the selection.
    pub swallow_right: bool,
    pub overlay: PathOverlay,
    pub stats: Option<Stats>,
    pub profile: Profile,
    pub status: Option<String>,
}

impl Default for State {
    fn default() -> Self {
        let preset = PRESETS[1];
        State {
            active: false,
            entity: None,
            terrain_name: None,
            kind: preset.kind,
            preset: Some(1),
            width_m: preset.width_m,
            shoulder_m: preset.shoulder_m,
            depth_m: preset.depth_m,
            smooth_m: preset.smooth_m,
            grade_percent: preset.max_grade * 100.0,
            layer: None,
            layer_names: Vec::new(),
            layer_strength: 1.0,
            write_mask: true,
            mask_name: preset.mask.to_owned(),
            points: Vec::new(),
            rows: Vec::new(),
            selected: None,
            drag: None,
            mouse: None,
            swallow_right: false,
            overlay: PathOverlay::default(),
            stats: None,
            profile: Profile::default(),
            status: None,
        }
    }
}

impl State {
    /// What the engine is asked to apply or preview.
    pub fn desc(&self) -> SplineDesc {
        SplineDesc {
            kind: self.kind,
            width_m: self.width_m,
            shoulder_m: self.shoulder_m,
            depth_m: self.depth_m,
            smooth_m: self.smooth_m,
            max_grade: self.grade_percent / 100.0,
            layer: self.layer.map_or(-1, |l| l as i32),
            layer_strength: self.layer_strength,
            mask: if self.write_mask {
                self.mask_name.trim().to_owned()
            } else {
                String::new()
            },
        }
    }

    pub fn engine_points(&self) -> Vec<SplinePoint> {
        self.points
            .iter()
            .map(|p| SplinePoint {
                world: p.world,
                width_m: p.width_m,
            })
            .collect()
    }

    fn word(&self) -> &'static str {
        match self.kind {
            SplineKind::Road => "road",
            SplineKind::River => "river",
        }
    }
}

impl App {
    pub(super) fn show_terrain_path_pane(&mut self) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::TerrainPath)
            {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Inspector) {
                    group.pages.push(PaneKind::TerrainPath);
                    group.active = group.pages.len() - 1;
                    break;
                }
            }
        }
        self.refresh_path_state();
    }

    /// Reads the terrain and its palette into the panel's state.
    pub(super) fn refresh_path_state(&mut self) {
        let target = self.terrain_path.entity.or_else(|| self.paint_terrain());
        let Some(engine) = self.engine.as_ref() else {
            return;
        };
        let names: Vec<String> = match target.and_then(|id| engine.terrain_layers(id)) {
            Some(layers) if layers.enabled => target
                .map(|id| engine.palette_layers(id))
                .unwrap_or_default()
                .into_iter()
                .map(|layer| layer.name)
                .collect(),
            _ => Vec::new(),
        };
        let name = target.and_then(|id| {
            self.entities
                .iter()
                .find(|entity| entity.id == id)
                .map(|entity| entity.name.clone())
        });
        let state = &mut self.terrain_path;
        state.layer_names = names;
        if state.layer.is_some_and(|l| l >= state.layer_names.len()) {
            state.layer = None;
        }
        state.terrain_name = name;
        if state.entity.is_none() {
            state.entity = target;
        }
    }

    fn set_path_tool(&mut self, on: bool) {
        if on {
            // One tool at a time: all of them use the left button.
            if self.terrain_editor.active {
                self.set_terrain_tool(false);
            }
            self.terrain_paint.active = false;
            self.terrain_vegetation.active = false;
            self.texture_set_paint_mode = false;
            self.terrain_path.entity = None;
            self.refresh_path_state();
        } else {
            self.terrain_path.drag = None;
        }
        self.terrain_path.active = on;
        let radius = (self.terrain_path.width_m * 0.5).max(0.25);
        self.set_terrain_brush_cursor(on, radius);
        self.log(
            LogLevel::Action,
            if on {
                "Terrain path on"
            } else {
                "Terrain path off"
            },
        );
    }

    /// The ground under a world x and z: a ray from above, straight down (the engine's rays reach 10 km).
    fn path_ground(&self, entity: u32, x: f64, z: f64) -> Option<[f64; 3]> {
        self.engine
            .as_ref()?
            .terrain_raycast(entity, [x, 5_000.0, z], [0.0, -1.0, 0.0])
    }

    /// Asks the engine for the path and rebuilds the overlay, the profile and the numbers.
    pub(super) fn rebuild_path_preview(&mut self) {
        let points: Vec<[f32; 3]> = self
            .terrain_path
            .points
            .iter()
            .map(|p| [p.world[0] as f32, p.world[1] as f32, p.world[2] as f32])
            .collect();
        let river = self.terrain_path.kind == SplineKind::River;
        let selected = self.terrain_path.selected;
        let mut samples = Vec::new();
        let mut stats = None;
        let mut profile = Profile::default();
        if let (true, Some(entity), Some(engine)) = (
            points.len() >= 2,
            self.terrain_path.entity,
            self.engine.as_ref(),
        ) {
            let chord: f32 = points
                .windows(2)
                .map(|w| ((w[1][0] - w[0][0]).powi(2) + (w[1][2] - w[0][2]).powi(2)).sqrt())
                .sum();
            let step = (chord / 160.0).clamp(0.5, 25.0);
            let desc = self.terrain_path.desc();
            if let Ok(found) = engine.terrain_spline_sample(
                entity,
                &desc,
                &self.terrain_path.engine_points(),
                step,
            ) {
                let bed = if river { desc.depth_m } else { 0.0 };
                // The ground under each sample, by sight from above (the entity's transform is honoured).
                let ground = |x: f32, z: f32, fallback: f32| {
                    engine
                        .terrain_raycast(
                            entity,
                            [f64::from(x), 5_000.0, f64::from(z)],
                            [0.0, -1.0, 0.0],
                        )
                        .map_or(fallback, |hit| hit[1] as f32)
                };
                let mut cut = 0.0f32;
                let mut fill = 0.0f32;
                let mut steepest = 0.0f32;
                for pair in found.windows(2) {
                    let run = (pair[1].along_m - pair[0].along_m).max(1e-3);
                    steepest = steepest.max((pair[1].world[1] - pair[0].world[1]).abs() / run);
                }
                for k in 0..PROFILE_SAMPLES {
                    let index = k * (found.len() - 1) / (PROFILE_SAMPLES - 1);
                    let s = &found[index];
                    let level = s.world[1] - bed;
                    let here = ground(s.world[0], s.world[2], s.world[1]);
                    profile.ground.push(here);
                    profile.path.push(level);
                    cut = cut.max(here - level);
                    fill = fill.max(level - here);
                }
                stats = Some(Stats {
                    length_m: found.last().map_or(0.0, |s| s.along_m),
                    cut_m: cut,
                    fill_m: fill,
                    grade_percent: steepest * 100.0,
                    drop_m: (found[0].world[1] - found[found.len() - 1].world[1]).max(0.0),
                });
                samples = found
                    .iter()
                    .map(|s| Sample {
                        position: [s.world[0], s.world[1] + 0.08, s.world[2]],
                        width: s.width_m,
                    })
                    .collect();
            }
        }
        let state = &mut self.terrain_path;
        state.rows = state
            .points
            .iter()
            .map(|p| PointRow {
                x: p.world[0],
                z: p.world[2],
                width_m: p.width_m,
            })
            .collect();
        state.overlay = PathOverlay {
            samples,
            points,
            selected,
            river,
        };
        state.stats = stats;
        state.profile = profile;
    }

    fn set_path_numbers(&mut self, preset: usize) {
        let Some(p) = PRESETS.get(preset) else {
            return;
        };
        let state = &mut self.terrain_path;
        state.kind = p.kind;
        state.preset = Some(preset);
        state.width_m = p.width_m;
        state.shoulder_m = p.shoulder_m;
        state.depth_m = p.depth_m;
        state.smooth_m = p.smooth_m;
        state.grade_percent = if p.max_grade > 0.0 {
            p.max_grade * 100.0
        } else {
            state.grade_percent
        };
        state.mask_name = p.mask.to_owned();
    }

    fn sync_path_cursor(&mut self) {
        if self.terrain_path.active {
            let radius = (self.terrain_path.width_m * 0.5).max(0.25);
            self.set_terrain_brush_cursor(true, radius);
        }
    }

    pub(super) fn edit_terrain_path(&mut self, edit: Edit) {
        self.refresh_path_state();
        match edit {
            Edit::Active(on) => self.set_path_tool(on),
            Edit::Kind(kind) => {
                if self.terrain_path.kind != kind {
                    // The first preset of the kind is a sensible start; the points stay.
                    let first = PRESETS.iter().position(|p| p.kind == kind).unwrap_or(0);
                    self.set_path_numbers(first);
                    self.sync_path_cursor();
                    self.rebuild_path_preview();
                }
            }
            Edit::Preset(index) => {
                self.set_path_numbers(index);
                self.sync_path_cursor();
                self.rebuild_path_preview();
            }
            Edit::Number(number, value) => {
                let range = number.range();
                let value = value.clamp(*range.start(), *range.end());
                let state = &mut self.terrain_path;
                match number {
                    Number::Width => state.width_m = value,
                    Number::Shoulder => state.shoulder_m = value,
                    Number::Depth => state.depth_m = value,
                    Number::Smooth => state.smooth_m = value,
                    Number::Grade => state.grade_percent = value,
                    Number::LayerStrength => state.layer_strength = value,
                    Number::PointWidth => {
                        if let Some(point) = state.selected.and_then(|i| state.points.get_mut(i)) {
                            point.width_m = value;
                        }
                    }
                }
                if !matches!(number, Number::LayerStrength | Number::PointWidth) {
                    state.preset = None;
                }
                self.sync_path_cursor();
                self.rebuild_path_preview();
            }
            Edit::Layer(layer) => {
                self.terrain_path.layer =
                    layer.filter(|l| *l < self.terrain_path.layer_names.len());
            }
            Edit::WriteMask(on) => self.terrain_path.write_mask = on,
            Edit::MaskName(name) => self.terrain_path.mask_name = name,
            Edit::SelectPoint(index) => {
                if index < self.terrain_path.points.len() {
                    self.terrain_path.selected = Some(index);
                    self.rebuild_path_preview();
                }
            }
            Edit::RemovePoint(index) => self.remove_path_point(index),
            Edit::RemoveLast => {
                if self.terrain_path.active || !self.terrain_path.points.is_empty() {
                    let last = self.terrain_path.points.len().saturating_sub(1);
                    self.remove_path_point(last);
                }
            }
            Edit::Clear => {
                let state = &mut self.terrain_path;
                state.points.clear();
                state.selected = None;
                state.drag = None;
                state.status = None;
                self.rebuild_path_preview();
            }
            Edit::Reverse => {
                self.terrain_path.points.reverse();
                self.terrain_path.selected = self
                    .terrain_path
                    .selected
                    .map(|i| self.terrain_path.points.len().saturating_sub(1 + i));
                self.rebuild_path_preview();
            }
            Edit::AddPoint { world } => {
                let Some(entity) = self.terrain_path.entity else {
                    self.terrain_path.status = Some("There is no terrain to draw on".to_owned());
                    return;
                };
                match self.path_ground(entity, world[0], world[2]) {
                    Some(hit) => self.push_path_point(hit),
                    None => {
                        self.terrain_path.status =
                            Some("That point is not on the terrain".to_owned());
                    }
                }
            }
            Edit::MovePoint { index, world } => {
                let Some(entity) = self.terrain_path.entity else {
                    return;
                };
                if index >= self.terrain_path.points.len() {
                    self.terrain_path.status = Some(format!("There is no point {}", index + 1));
                    return;
                }
                if let Some(hit) = self.path_ground(entity, world[0], world[2]) {
                    self.terrain_path.points[index].world = hit;
                    self.rebuild_path_preview();
                } else {
                    self.terrain_path.status = Some("That point is not on the terrain".to_owned());
                }
            }
            Edit::Apply => self.apply_path(),
        }
    }

    fn push_path_point(&mut self, world: [f64; 3]) {
        let state = &mut self.terrain_path;
        state.points.push(PathPoint {
            world,
            width_m: 0.0,
        });
        state.selected = Some(state.points.len() - 1);
        state.status = None;
        self.rebuild_path_preview();
    }

    fn remove_path_point(&mut self, index: usize) {
        let state = &mut self.terrain_path;
        if index >= state.points.len() {
            return;
        }
        state.points.remove(index);
        state.selected = if state.points.is_empty() {
            None
        } else {
            Some(index.min(state.points.len() - 1))
        };
        state.drag = None;
        state.status = None;
        self.rebuild_path_preview();
    }

    /// Shapes the ground along the path as one undo step.
    pub(super) fn apply_path(&mut self) {
        let word = self.terrain_path.word();
        if self.terrain_path.points.len() < 2 {
            self.terrain_path.status = Some(format!(
                "Drop at least two points to make a {word}: click on the terrain"
            ));
            return;
        }
        let Some(entity) = self.terrain_path.entity else {
            return;
        };
        let desc = self.terrain_path.desc();
        let points = self.terrain_path.engine_points();
        let Some(engine) = self.engine.as_mut() else {
            return;
        };
        match engine.terrain_spline_apply(entity, &desc, &points) {
            Ok((0, _)) => {
                self.terrain_path.status = Some(format!("The {word} changed nothing"));
            }
            Ok((record, length)) => {
                self.commit_terrain_stroke(record, format!("Apply {word}"));
                self.log(
                    LogLevel::Action,
                    format!("Applied a {word} of {length:.0} m"),
                );
                let state = &mut self.terrain_path;
                state.points.clear();
                state.selected = None;
                state.drag = None;
                state.status = Some(format!("{word} applied: {length:.0} m"));
                self.viewport_dirty = true;
                self.scene_epoch += 1;
                self.rebuild_path_preview();
                // The mask it wrote and the layer it painted changed what the other tabs list.
                self.refresh_paint_state();
                self.refresh_vegetation_state();
            }
            Err(error) => {
                self.log(LogLevel::Error, format!("Terrain path: {error}"));
                self.terrain_path.status = Some(error);
            }
        }
    }

    /// Escape: starts over when a path is being drawn. `true` when it did.
    pub(super) fn cancel_path(&mut self) -> bool {
        if self.terrain_path.active && !self.terrain_path.points.is_empty() {
            self.edit_terrain_path(Edit::Clear);
            return true;
        }
        false
    }

    fn path_basis(&self) -> Option<Basis> {
        let camera = self.viewport_frame_camera.as_ref().unwrap_or(&self.camera);
        Basis::new(
            camera,
            Size::new(self.viewport_logical.0, self.viewport_logical.1),
        )
    }

    /// The handle under the mouse, if any.
    fn path_handle_at_mouse(&self) -> Option<usize> {
        let basis = self.path_basis()?;
        let mouse = self.terrain_path.mouse?;
        path_overlay::pick(&basis, &self.terrain_path.overlay.points, mouse)
    }

    /// A left press with the tool on: grabs the handle under the mouse, or drops a point on the ground there (and keeps
    /// it grabbed, so the click can drag the new point into place). `false` when the tool is off or the press hit
    /// neither a handle nor the terrain, so the camera can take it.
    pub(super) fn path_press(&mut self) -> bool {
        if !self.terrain_path.active || self.views.session {
            return false;
        }
        if let Some(index) = self.path_handle_at_mouse() {
            self.terrain_path.selected = Some(index);
            self.terrain_path.drag = Some(index);
            self.rebuild_path_preview();
            return true;
        }
        let Some((entity, hit)) = self.terrain_cursor.hit else {
            return false;
        };
        if self.terrain_path.entity != Some(entity) {
            // Another terrain: start a new path on it.
            self.terrain_path.points.clear();
            self.terrain_path.entity = Some(entity);
            self.refresh_path_state();
        }
        self.push_path_point(hit);
        self.terrain_path.drag = Some(self.terrain_path.points.len() - 1);
        true
    }

    /// A right press with the tool on: removes the handle under the mouse. `true` when there was one.
    pub(super) fn path_right_press(&mut self) -> bool {
        if !self.terrain_path.active {
            return false;
        }
        match self.path_handle_at_mouse() {
            Some(index) => {
                self.remove_path_point(index);
                self.terrain_path.swallow_right = true;
                true
            }
            None => false,
        }
    }

    /// The mouse moved: drags the grabbed handle along the ground. `true` while one is grabbed.
    pub(super) fn path_move(&mut self, point: Point) -> bool {
        self.terrain_path.mouse = Some(point);
        let Some(index) = self.terrain_path.drag else {
            return false;
        };
        if let Some((_, hit)) = self.terrain_cursor.hit {
            if let Some(path_point) = self.terrain_path.points.get_mut(index) {
                if path_point.world != hit {
                    path_point.world = hit;
                    self.rebuild_path_preview();
                }
            }
        }
        true
    }

    pub(super) fn path_release(&mut self) {
        self.terrain_path.drag = None;
    }

    /// The ground under the mouse, for the leg from the last point.
    pub(super) fn path_ghost(&self) -> Option<[f32; 3]> {
        if !self.terrain_path.active || self.terrain_path.drag.is_some() {
            return None;
        }
        self.terrain_cursor
            .hit
            .map(|(_, hit)| [hit[0] as f32, hit[1] as f32 + 0.08, hit[2] as f32])
    }
}

//! The **terrain brush cursor**: where a terrain brush would act, drawn on the ground under the mouse.
//!
//! While a brush tool is on, every mouse move over the viewport casts the mouse's ray at the scene's terrains (the
//! engine's `terrain_raycast`), takes the nearest hit and asks the engine for the ring a brush of the chosen radius
//! makes there (`terrain_brush_outline`: points on the relief). `uikit::components::brush_cursor` draws it. Nothing is
//! edited here; the brush tools of the Terrain Editor turn the cursor on and size it, and agents can too
//! (`terrain_brush_cursor`).

use super::App;
use crate::uikit::components::brush_cursor::BrushCursor;
use crate::uikit::components::viewport_math::Basis;
use iced::{Point, Size};

/// Points in the outer ring and in the inner one.
const OUTER_SEGMENTS: u32 = 72;
const INNER_SEGMENTS: u32 = 48;

#[derive(Debug, Clone, PartialEq)]
pub(super) struct State {
    /// A brush tool is on: the cursor follows the mouse.
    pub active: bool,
    /// Brush radius, metres.
    pub radius_m: f32,
    /// The brush under the mouse now; `None` off the terrain, off the viewport or while no tool is on.
    pub cursor: Option<BrushCursor>,
    /// The terrain entity and the world-space point under the mouse: where a stroke's next stamp goes.
    pub hit: Option<(u32, [f64; 3])>,
}

impl Default for State {
    fn default() -> Self {
        State {
            active: false,
            radius_m: 6.0,
            cursor: None,
            hit: None,
        }
    }
}

impl App {
    /// Turns the brush cursor on or off and sets its radius (metres, kept between 0.05 and 2000).
    pub(super) fn set_terrain_brush_cursor(&mut self, active: bool, radius_m: f32) {
        self.terrain_cursor.active = active;
        self.terrain_cursor.radius_m = radius_m.clamp(0.05, 2000.0);
        if !active {
            self.terrain_cursor.cursor = None;
            self.terrain_cursor.hit = None;
        }
    }

    pub(super) fn clear_terrain_cursor(&mut self) {
        self.terrain_cursor.cursor = None;
        self.terrain_cursor.hit = None;
    }

    /// Follows the mouse: the ring on the nearest terrain under `point` (a viewport position), or none.
    pub(super) fn update_terrain_cursor(&mut self, point: Point) {
        if !self.terrain_cursor.active {
            return;
        }
        let hit = self.terrain_hit_at(point);
        self.terrain_cursor.cursor =
            hit.and_then(|(entity, hit)| self.terrain_cursor_ring(entity, hit));
        self.terrain_cursor.hit = hit;
    }

    /// The nearest terrain the mouse's ray meets, and the world-space point it meets it at.
    fn terrain_hit_at(&self, point: Point) -> Option<(u32, [f64; 3])> {
        let engine = self.engine.as_ref()?;
        let camera = self.viewport_frame_camera.as_ref().unwrap_or(&self.camera);
        let basis = Basis::new(
            camera,
            Size::new(self.viewport_logical.0, self.viewport_logical.1),
        )?;
        let (origin, direction) = basis.ray(point);
        let origin = origin.map(f64::from);
        let direction = direction.map(f64::from);
        // The nearest terrain the ray meets (a scene has one or two).
        self.entities
            .iter()
            .filter_map(|entity| {
                engine
                    .terrain_raycast(entity.id, origin, direction)
                    .map(|hit| (entity.id, hit))
            })
            .min_by(|(_, a), (_, b)| {
                let distance =
                    |hit: &[f64; 3]| (0..3).map(|i| (hit[i] - origin[i]).powi(2)).sum::<f64>();
                distance(a).total_cmp(&distance(b))
            })
    }

    fn terrain_cursor_ring(&self, entity: u32, hit: [f64; 3]) -> Option<BrushCursor> {
        let engine = self.engine.as_ref()?;
        let radius = self.terrain_cursor.radius_m;
        let outer = engine.terrain_brush_outline(entity, hit, radius, OUTER_SEGMENTS)?;
        let inner = engine.terrain_brush_outline(entity, hit, radius * 0.5, INNER_SEGMENTS)?;
        Some(BrushCursor {
            center: [hit[0] as f32, hit[1] as f32 + 0.05, hit[2] as f32],
            outer,
            inner,
        })
    }
}

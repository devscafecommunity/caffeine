//! The **brush cursor**: where a terrain brush is on the ground.
//!
//! The engine gives the ring a brush makes on the terrain surface (`Engine::terrain_brush_outline`: points on the
//! relief), and this overlay draws it in the viewport's projection, so the brush shows as a decal that hugs the ground:
//! an outer ring at the brush radius, an inner ring where the stroke is at full strength, and a cross at the centre.
//! It draws and does nothing else: clicks and drags pass through to the viewport.

use super::viewport_math::{Basis, Vec3};
use crate::uikit::theme;
use doppio_engine::Camera;
use iced::widget::canvas::{self, Frame, LineCap, LineJoin, Path, Stroke};
use iced::{mouse, Color, Element, Length, Point, Rectangle, Size};

/// The brush under the mouse, in world space (metres).
#[derive(Debug, Clone, PartialEq, Default)]
pub struct BrushCursor {
    pub center: Vec3,
    /// The ring at the brush radius, on the terrain surface.
    pub outer: Vec<Vec3>,
    /// The ring at half the radius.
    pub inner: Vec<Vec3>,
}

pub struct View<'a> {
    pub camera: &'a Camera,
    pub viewport: Size,
    pub cursor: &'a BrushCursor,
}

pub fn view<'a, Message: 'a>(view: View<'a>) -> Element<'a, Message> {
    iced::widget::container(
        iced::widget::canvas(Program {
            camera: *view.camera,
            viewport: view.viewport,
            cursor: view.cursor.clone(),
        })
        .width(Length::Fill)
        .height(Length::Fill),
    )
    .id(crate::uikit::id::VIEWPORT_BRUSH_CURSOR)
    .width(Length::Fill)
    .height(Length::Fill)
    .into()
}

struct Program {
    camera: Camera,
    viewport: Size,
    cursor: BrushCursor,
}

/// The ring as screen polylines: a point behind the camera ends one line and starts the next.
fn polylines(basis: &Basis, ring: &[Vec3], close: bool) -> Vec<Vec<Point>> {
    let mut lines: Vec<Vec<Point>> = Vec::new();
    let mut current: Vec<Point> = Vec::new();
    let count = ring.len();
    let total = if close { count + 1 } else { count };
    for index in 0..total {
        match basis.project(ring[index % count.max(1)]) {
            Some(point) => current.push(point),
            None => {
                if current.len() > 1 {
                    lines.push(std::mem::take(&mut current));
                } else {
                    current.clear();
                }
            }
        }
    }
    if current.len() > 1 {
        lines.push(current);
    }
    lines
}

fn path(points: &[Point]) -> Path {
    Path::new(|builder| {
        if let Some(first) = points.first() {
            builder.move_to(*first);
            for point in &points[1..] {
                builder.line_to(*point);
            }
        }
    })
}

impl<Message> canvas::Program<Message> for Program {
    type State = ();

    fn draw(
        &self,
        _state: &Self::State,
        renderer: &iced::Renderer,
        _theme: &iced::Theme,
        bounds: Rectangle,
        _cursor: mouse::Cursor,
    ) -> Vec<canvas::Geometry> {
        let Some(basis) = Basis::new(&self.camera, self.viewport) else {
            return Vec::new();
        };
        if self.cursor.outer.len() < 3 {
            return Vec::new();
        }
        let mut frame = Frame::new(renderer, bounds.size());
        let halo = theme::OVERLAY_HALO;
        let accent = theme::ACCENT;
        let mut ring = |points: &[Vec3], width: f32, color: Color| {
            for line in polylines(&basis, points, true) {
                let shape = path(&line);
                // A dark line under a bright one reads on any ground colour.
                frame.stroke(
                    &shape,
                    Stroke::default()
                        .with_color(halo)
                        .with_width(width + 2.0)
                        .with_line_cap(LineCap::Round)
                        .with_line_join(LineJoin::Round),
                );
                frame.stroke(
                    &shape,
                    Stroke::default()
                        .with_color(color)
                        .with_width(width)
                        .with_line_cap(LineCap::Round)
                        .with_line_join(LineJoin::Round),
                );
            }
        };
        ring(&self.cursor.outer, 2.0, accent);
        ring(&self.cursor.inner, 1.0, Color { a: 0.75, ..accent });
        if let Some(center) = basis.project(self.cursor.center) {
            let arm = 5.0;
            let cross = Path::new(|builder| {
                builder.move_to(Point::new(center.x - arm, center.y));
                builder.line_to(Point::new(center.x + arm, center.y));
                builder.move_to(Point::new(center.x, center.y - arm));
                builder.line_to(Point::new(center.x, center.y + arm));
            });
            frame.stroke(&cross, Stroke::default().with_color(halo).with_width(3.0));
            frame.stroke(&cross, Stroke::default().with_color(accent).with_width(1.5));
        }
        vec![frame.into_geometry()]
    }

    fn mouse_interaction(
        &self,
        _state: &Self::State,
        _bounds: Rectangle,
        _cursor: mouse::Cursor,
    ) -> mouse::Interaction {
        mouse::Interaction::None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn basis() -> Basis {
        Basis::new(
            &Camera {
                yaw: 0.0,
                pitch: 0.6,
                distance: 40.0,
                focus: [0.0, 0.0, 0.0],
            },
            Size::new(800.0, 600.0),
        )
        .expect("basis")
    }

    fn ring(radius: f32, count: usize) -> Vec<Vec3> {
        (0..count)
            .map(|i| {
                let a = i as f32 / count as f32 * std::f32::consts::TAU;
                [radius * a.cos(), 0.0, radius * a.sin()]
            })
            .collect()
    }

    #[test]
    fn a_ring_in_front_of_the_camera_is_one_closed_line() {
        let lines = polylines(&basis(), &ring(5.0, 48), true);
        assert_eq!(lines.len(), 1);
        assert_eq!(lines[0].len(), 49, "48 points and the one that closes it");
        assert_eq!(lines[0].first(), lines[0].last());
    }

    #[test]
    fn the_part_behind_the_camera_is_left_out() {
        // A ring around the camera: half of it is behind.
        let camera = basis();
        let around: Vec<Vec3> = ring(10.0, 48)
            .into_iter()
            .map(|p| {
                [
                    p[0] + camera.position[0],
                    p[1] + camera.position[1],
                    p[2] + camera.position[2],
                ]
            })
            .collect();
        let lines = polylines(&camera, &around, true);
        let drawn: usize = lines.iter().map(Vec::len).sum();
        assert!(drawn < 49, "some of the ring is behind the camera: {drawn}");
    }
}

//! The **path overlay**: a road or river being drawn on the terrain.
//!
//! The engine gives the path it would follow, with the width and the height of its profile at every sample
//! (`Engine::terrain_spline_sample`); this overlay draws it in the viewport's projection: a translucent ribbon as wide as
//! the corridor, its two edges, a dashed centre line, the control points as numbered handles (the selected one larger and
//! in the accent colour) and a faint dashed leg from the last point to the mouse, so the next click is predictable. It
//! draws and does nothing else: clicks and drags pass through to the viewport, which hit-tests the handles itself.

use super::viewport_math::{self as vm, Basis, Vec3};
use crate::uikit::theme;
use doppio_engine::Camera;
use iced::widget::canvas::{self, Frame, LineCap, LineDash, LineJoin, Path, Stroke, Text};
use iced::{mouse, Color, Element, Length, Point, Rectangle, Size};

/// One point of the path: where it is (world space, on the profile) and the corridor's full width there.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Sample {
    pub position: Vec3,
    pub width: f32,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct PathOverlay {
    pub samples: Vec<Sample>,
    /// The control points, on the ground.
    pub points: Vec<Vec3>,
    pub selected: Option<usize>,
    /// Rivers are drawn in the water colour, roads in the earth one.
    pub river: bool,
}

pub struct View<'a> {
    pub camera: &'a Camera,
    pub viewport: Size,
    pub overlay: &'a PathOverlay,
    /// The ground under the mouse: the next point if the user clicks.
    pub ghost: Option<Vec3>,
}

pub fn view<'a, Message: 'a>(view: View<'a>) -> Element<'a, Message> {
    iced::widget::container(
        iced::widget::canvas(Program {
            camera: *view.camera,
            viewport: view.viewport,
            overlay: view.overlay.clone(),
            ghost: view.ghost,
        })
        .width(Length::Fill)
        .height(Length::Fill),
    )
    .id(crate::uikit::id::VIEWPORT_PATH_OVERLAY)
    .width(Length::Fill)
    .height(Length::Fill)
    .into()
}

struct Program {
    camera: Camera,
    viewport: Size,
    overlay: PathOverlay,
    ghost: Option<Vec3>,
}

/// The two edges of the corridor: every sample offset sideways by half its width, on the ground plane of the path.
pub fn edges(samples: &[Sample]) -> (Vec<Vec3>, Vec<Vec3>) {
    let mut left = Vec::with_capacity(samples.len());
    let mut right = Vec::with_capacity(samples.len());
    for (index, sample) in samples.iter().enumerate() {
        let before = samples[index.saturating_sub(1)].position;
        let after = samples[(index + 1).min(samples.len() - 1)].position;
        let direction = vm::normalize([after[0] - before[0], 0.0, after[2] - before[2]]);
        // Perpendicular on the ground: (x, z) -> (-z, x).
        let side = [-direction[2], 0.0, direction[0]];
        let half = sample.width * 0.5;
        left.push(vm::add(sample.position, vm::scale(side, half)));
        right.push(vm::add(sample.position, vm::scale(side, -half)));
    }
    (left, right)
}

fn project_line(basis: &Basis, points: &[Vec3]) -> Vec<Vec<Point>> {
    let mut lines: Vec<Vec<Point>> = Vec::new();
    let mut current: Vec<Point> = Vec::new();
    for point in points {
        match basis.project(*point) {
            Some(screen) => current.push(screen),
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
        let overlay = &self.overlay;
        if overlay.points.is_empty() && self.ghost.is_none() {
            return Vec::new();
        }
        let mut frame = Frame::new(renderer, bounds.size());
        let halo = theme::OVERLAY_HALO;
        let tone = if overlay.river {
            theme::ICON_CYAN
        } else {
            theme::ICON_ORANGE
        };

        // The ribbon: one quad per stretch, so a bend or a point behind the camera never breaks the fill.
        if overlay.samples.len() >= 2 {
            let (left, right) = edges(&overlay.samples);
            for index in 0..overlay.samples.len() - 1 {
                let corners = [
                    basis.project(left[index]),
                    basis.project(left[index + 1]),
                    basis.project(right[index + 1]),
                    basis.project(right[index]),
                ];
                if let [Some(a), Some(b), Some(c), Some(d)] = corners {
                    let quad = Path::new(|builder| {
                        builder.move_to(a);
                        builder.line_to(b);
                        builder.line_to(c);
                        builder.line_to(d);
                        builder.close();
                    });
                    frame.fill(&quad, Color { a: 0.26, ..tone });
                }
            }
            for edge in [&left, &right] {
                for line in project_line(&basis, edge) {
                    let shape = path(&line);
                    frame.stroke(
                        &shape,
                        Stroke::default()
                            .with_color(halo)
                            .with_width(3.0)
                            .with_line_join(LineJoin::Round),
                    );
                    frame.stroke(
                        &shape,
                        Stroke::default()
                            .with_color(Color { a: 0.9, ..tone })
                            .with_width(1.5)
                            .with_line_join(LineJoin::Round),
                    );
                }
            }
            let centre: Vec<Vec3> = overlay.samples.iter().map(|s| s.position).collect();
            for line in project_line(&basis, &centre) {
                frame.stroke(
                    &path(&line),
                    Stroke {
                        line_dash: LineDash {
                            segments: &[6.0, 5.0],
                            offset: 0,
                        },
                        ..Stroke::default()
                            .with_color(theme::OVERLAY_WHITE)
                            .with_width(1.2)
                    },
                );
            }
        } else if overlay.points.len() >= 2 {
            // Before the engine has answered: the bare polyline through the points.
            for line in project_line(&basis, &overlay.points) {
                frame.stroke(
                    &path(&line),
                    Stroke::default().with_color(tone).with_width(2.0),
                );
            }
        }

        // The leg to the mouse.
        if let (Some(last), Some(ghost)) = (overlay.points.last(), self.ghost) {
            if let (Some(a), Some(b)) = (basis.project(*last), basis.project(ghost)) {
                frame.stroke(
                    &Path::line(a, b),
                    Stroke {
                        line_dash: LineDash {
                            segments: &[4.0, 4.0],
                            offset: 0,
                        },
                        ..Stroke::default()
                            .with_color(Color { a: 0.7, ..tone })
                            .with_width(1.5)
                            .with_line_cap(LineCap::Round)
                    },
                );
            }
        }
        if let Some(ghost) = self.ghost.and_then(|g| basis.project(g)) {
            let ring = Path::circle(ghost, 4.0);
            frame.stroke(&ring, Stroke::default().with_color(halo).with_width(3.0));
            frame.stroke(
                &ring,
                Stroke::default()
                    .with_color(Color { a: 0.85, ..tone })
                    .with_width(1.5),
            );
        }

        // The handles, over everything.
        for (index, point) in overlay.points.iter().enumerate() {
            let Some(screen) = basis.project(*point) else {
                continue;
            };
            let selected = overlay.selected == Some(index);
            let radius = if selected { HANDLE_SELECTED } else { HANDLE };
            let circle = Path::circle(screen, radius);
            frame.fill(&circle, if selected { theme::ACCENT } else { tone });
            frame.stroke(
                &circle,
                Stroke::default()
                    .with_color(theme::OVERLAY_WHITE)
                    .with_width(if selected { 2.0 } else { 1.5 }),
            );
            frame.fill_text(Text {
                content: format!("{}", index + 1),
                position: Point::new(screen.x + radius + 3.0, screen.y - radius - 11.0),
                color: theme::OVERLAY_WHITE,
                size: 10.0.into(),
                ..Text::default()
            });
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

/// Handle radii in pixels; the viewport hit-tests with `PICK_RADIUS`.
pub const HANDLE: f32 = 6.0;
pub const HANDLE_SELECTED: f32 = 8.0;
pub const PICK_RADIUS: f32 = 12.0;

/// The handle nearest to `mouse` within `PICK_RADIUS` pixels, if any (the last one wins a tie: it is on top).
pub fn pick(basis: &Basis, points: &[Vec3], mouse: Point) -> Option<usize> {
    let mut best: Option<(usize, f32)> = None;
    for (index, point) in points.iter().enumerate() {
        let Some(screen) = basis.project(*point) else {
            continue;
        };
        let distance = ((screen.x - mouse.x).powi(2) + (screen.y - mouse.y).powi(2)).sqrt();
        if distance <= PICK_RADIUS && best.is_none_or(|(_, d)| distance <= d) {
            best = Some((index, distance));
        }
    }
    best.map(|(index, _)| index)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn basis() -> Basis {
        Basis::new(
            &Camera {
                yaw: 0.0,
                pitch: 1.0,
                distance: 60.0,
                focus: [0.0, 0.0, 0.0],
            },
            Size::new(800.0, 600.0),
        )
        .expect("basis")
    }

    fn straight() -> Vec<Sample> {
        (0..5)
            .map(|i| Sample {
                position: [i as f32 * 4.0, 0.0, 0.0],
                width: 6.0,
            })
            .collect()
    }

    #[test]
    fn the_edges_are_half_a_width_to_each_side_of_the_path() {
        let (left, right) = edges(&straight());
        assert_eq!(left.len(), 5);
        for (l, r) in left.iter().zip(&right) {
            assert!((vm::length(vm::sub(*l, *r)) - 6.0).abs() < 1e-4);
            assert!((l[0] - r[0]).abs() < 1e-4, "the path runs along x");
        }
    }

    #[test]
    fn a_handle_is_picked_within_its_radius_and_the_nearest_wins() {
        let basis = basis();
        let points = [[0.0, 0.0, 0.0], [2.0, 0.0, 0.0], [20.0, 0.0, 0.0]];
        let near = basis.project(points[1]).unwrap();
        assert_eq!(
            pick(&basis, &points, Point::new(near.x + 2.0, near.y)),
            Some(1)
        );
        assert_eq!(
            pick(&basis, &points, Point::new(near.x + 300.0, near.y)),
            None
        );
    }
}

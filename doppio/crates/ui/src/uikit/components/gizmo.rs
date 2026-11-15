//! Transform gizmo drawn over the viewport.
//!
//! The gizmo uses the same orbit camera as the engine viewport. It supports
//! global-axis move, rotate and scale, and emits absolute transform edits so
//! the app can apply them through the normal undo path.

use crate::uikit::theme;
use doppio_engine::Camera;
use iced::widget::canvas::{self, Frame, LineCap, Path, Stroke, Text};
use iced::{mouse, Color, Element, Length, Point, Rectangle, Size};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    Translate,
    Rotate,
    Scale,
}

impl Mode {
    pub fn label(self) -> &'static str {
        match self {
            Mode::Translate => "Move",
            Mode::Rotate => "Rotate",
            Mode::Scale => "Scale",
        }
    }

    fn handle_pixels(self) -> f32 {
        match self {
            Mode::Translate | Mode::Scale => 72.0,
            Mode::Rotate => 58.0,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Axis {
    X,
    Y,
    Z,
}

impl Axis {
    fn index(self) -> usize {
        match self {
            Axis::X => 0,
            Axis::Y => 1,
            Axis::Z => 2,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Axis::X => "X",
            Axis::Y => "Y",
            Axis::Z => "Z",
        }
    }

    fn vector(self) -> [f32; 3] {
        match self {
            Axis::X => [1.0, 0.0, 0.0],
            Axis::Y => [0.0, 1.0, 0.0],
            Axis::Z => [0.0, 0.0, 1.0],
        }
    }

    fn color(self) -> Color {
        match self {
            Axis::X => theme::DANGER,
            Axis::Y => theme::SUCCESS,
            Axis::Z => theme::ACCENT,
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct Target {
    /// Local transform values edited by the handles.
    pub position: [f32; 3],
    /// Resolved position after parent transforms, used for drawing/hit testing.
    pub world_position: [f32; 3],
    pub rotation: [f32; 3],
    pub scale: [f32; 3],
}

#[derive(Debug, Clone, Copy)]
pub enum Edit {
    Move { axis: Axis, position: [f32; 3] },
    Rotate { axis: Axis, rotation: [f32; 3] },
    Scale { axis: Axis, scale: [f32; 3] },
}

#[derive(Debug, Clone)]
pub enum Message {
    Changed(Edit),
}

pub struct View<'a> {
    pub mode: Mode,
    pub camera: &'a Camera,
    pub viewport: Size,
    pub target: Option<Target>,
}

pub fn view<'a>(view: View<'a>) -> Element<'a, Message> {
    iced::widget::container(
        iced::widget::canvas(Program {
            mode: view.mode,
            camera: *view.camera,
            viewport: view.viewport,
            target: view.target,
        })
        .width(Length::Fill)
        .height(Length::Fill),
    )
    .id(crate::uikit::id::VIEWPORT_GIZMO)
    .width(Length::Fill)
    .height(Length::Fill)
    .into()
}

struct Program {
    mode: Mode,
    camera: Camera,
    viewport: Size,
    target: Option<Target>,
}

#[derive(Default)]
struct State {
    active: Option<Active>,
    mode: Option<Mode>,
}

#[derive(Clone, Copy)]
struct Active {
    axis: Axis,
    start_cursor: Point,
    start_target: Target,
    center: Point,
    axis_end: Point,
    axis_length_world: f32,
}

struct Basis {
    position: [f32; 3],
    right: [f32; 3],
    up: [f32; 3],
    forward: [f32; 3],
    tan_half: f32,
    aspect: f32,
    viewport: Size,
}

impl Basis {
    fn new(camera: &Camera, viewport: Size) -> Option<Self> {
        if viewport.width <= 1.0 || viewport.height <= 1.0 {
            return None;
        }
        let (sin_yaw, cos_yaw) = camera.yaw.sin_cos();
        let (sin_pitch, cos_pitch) = camera.pitch.sin_cos();
        let offset = [sin_yaw * cos_pitch, sin_pitch, -cos_yaw * cos_pitch];
        let focus = camera.focus;
        let position = [
            focus[0] + offset[0] * camera.distance,
            focus[1] + offset[1] * camera.distance,
            focus[2] + offset[2] * camera.distance,
        ];
        let forward = [-offset[0], -offset[1], -offset[2]];
        let right = [-cos_yaw, 0.0, -sin_yaw];
        let up = cross(right, forward);
        Some(Self {
            position,
            right,
            up,
            forward,
            tan_half: (std::f32::consts::FRAC_PI_3 * 0.5).tan(),
            aspect: viewport.width / viewport.height,
            viewport,
        })
    }

    fn project(&self, point: [f32; 3]) -> Option<Point> {
        let rel = [
            point[0] - self.position[0],
            point[1] - self.position[1],
            point[2] - self.position[2],
        ];
        let x = dot(rel, self.right);
        let y = dot(rel, self.up);
        let z = dot(rel, self.forward);
        if z <= 0.05 {
            return None;
        }
        let ndc_x = x / (z * self.tan_half * self.aspect);
        let ndc_y = y / (z * self.tan_half);
        Some(Point::new(
            (ndc_x * 0.5 + 0.5) * self.viewport.width,
            (0.5 - ndc_y * 0.5) * self.viewport.height,
        ))
    }

    fn units_per_pixel(&self, point: [f32; 3]) -> Option<f32> {
        let rel = [
            point[0] - self.position[0],
            point[1] - self.position[1],
            point[2] - self.position[2],
        ];
        let depth = dot(rel, self.forward);
        (depth > 0.05).then(|| 2.0 * depth * self.tan_half / self.viewport.height)
    }
}

fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

fn add(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

fn scale(a: [f32; 3], amount: f32) -> [f32; 3] {
    [a[0] * amount, a[1] * amount, a[2] * amount]
}

fn vector_length(vector: iced::Vector) -> f32 {
    (vector.x * vector.x + vector.y * vector.y).sqrt()
}

fn vector_scale(vector: iced::Vector, amount: f32) -> iced::Vector {
    iced::Vector::new(vector.x * amount, vector.y * amount)
}

fn point_segment_distance(point: Point, a: Point, b: Point) -> f32 {
    let ab = b - a;
    let length_sq = ab.x * ab.x + ab.y * ab.y;
    if length_sq <= 1e-6 {
        return vector_length(point - a);
    }
    let ap = point - a;
    let t = ((ap.x * ab.x + ap.y * ab.y) / length_sq).clamp(0.0, 1.0);
    vector_length(point - Point::new(a.x + ab.x * t, a.y + ab.y * t))
}

type AxisGeometry = (Axis, Point, Point);
type Hit = (Axis, Point, Point, Point, f32);

fn ring_points(target: [f32; 3], axis: Axis, radius: f32, basis: &Basis) -> Option<Vec<Point>> {
    let mut points = Vec::with_capacity(49);
    for index in 0..=48 {
        let angle = index as f32 / 48.0 * std::f32::consts::TAU;
        let (sin, cos) = angle.sin_cos();
        let offset = match axis {
            Axis::X => [0.0, cos * radius, sin * radius],
            Axis::Y => [cos * radius, 0.0, sin * radius],
            Axis::Z => [cos * radius, sin * radius, 0.0],
        };
        points.push(basis.project(add(target, offset))?);
    }
    Some(points)
}

impl Program {
    fn basis(&self, _bounds: Rectangle) -> Option<Basis> {
        Basis::new(&self.camera, self.viewport)
    }

    fn handle_geometry(
        &self,
        target: Target,
        basis: &Basis,
    ) -> Option<(Point, f32, Vec<AxisGeometry>)> {
        let center = basis.project(target.world_position)?;
        let units = basis.units_per_pixel(target.world_position)?;
        let length = self.mode.handle_pixels() * units;
        let mut axes = Vec::new();
        for axis in [Axis::X, Axis::Y, Axis::Z] {
            let end = basis.project(add(target.world_position, scale(axis.vector(), length)))?;
            axes.push((axis, center, end));
        }
        Some((center, length, axes))
    }

    fn hit_test(&self, cursor: Point) -> Option<Hit> {
        let target = self.target?;
        let basis = self.basis(Rectangle::default())?;
        match self.mode {
            Mode::Translate | Mode::Scale => {
                let (center, length, axes) = self.handle_geometry(target, &basis)?;
                if vector_length(cursor - center) < 12.0 {
                    return None;
                }
                let mut best: Option<Hit> = None;
                let mut best_distance = 8.0;
                for (axis, start, end) in axes {
                    let distance = point_segment_distance(cursor, start, end);
                    if distance < best_distance {
                        best_distance = distance;
                        best = Some((axis, center, end, cursor, length));
                    }
                }
                best
            }
            Mode::Rotate => {
                let center = basis.project(target.world_position)?;
                let units = basis.units_per_pixel(target.world_position)?;
                let radius = self.mode.handle_pixels() * units;
                let mut best: Option<Hit> = None;
                let mut best_distance = 7.0;
                for axis in [Axis::X, Axis::Y, Axis::Z] {
                    let Some(points) = ring_points(target.world_position, axis, radius, &basis)
                    else {
                        continue;
                    };
                    for window in points.windows(2) {
                        let distance = point_segment_distance(cursor, window[0], window[1]);
                        if distance < best_distance {
                            best_distance = distance;
                            best = Some((axis, center, window[0], cursor, radius));
                        }
                    }
                }
                best
            }
        }
    }

    fn edit(&self, active: Active, cursor: Point) -> Option<Edit> {
        match self.mode {
            Mode::Translate => {
                let axis_screen = active.axis_end - active.center;
                let length_sq = axis_screen.x * axis_screen.x + axis_screen.y * axis_screen.y;
                if length_sq < 1e-4 {
                    return None;
                }
                let delta = cursor - active.start_cursor;
                let t = (delta.x * axis_screen.x + delta.y * axis_screen.y) / length_sq;
                let position = add(
                    active.start_target.world_position,
                    scale(active.axis.vector(), t * active.axis_length_world),
                );
                Some(Edit::Move {
                    axis: active.axis,
                    position,
                })
            }
            Mode::Scale => {
                let axis_screen = active.axis_end - active.center;
                let length_sq = axis_screen.x * axis_screen.x + axis_screen.y * axis_screen.y;
                if length_sq < 1e-4 {
                    return None;
                }
                let delta = cursor - active.start_cursor;
                let t = (delta.x * axis_screen.x + delta.y * axis_screen.y) / length_sq;
                let mut scale = active.start_target.scale;
                scale[active.axis.index()] =
                    (scale[active.axis.index()] * (1.0 + t)).clamp(0.01, 1000.0);
                Some(Edit::Scale {
                    axis: active.axis,
                    scale,
                })
            }
            Mode::Rotate => {
                let center = active.center;
                let start = active.start_cursor - center;
                let current = cursor - center;
                if vector_length(start) < 1e-3 || vector_length(current) < 1e-3 {
                    return None;
                }
                let start_angle = start.y.atan2(start.x);
                let current_angle = current.y.atan2(current.x);
                let mut delta = current_angle - start_angle;
                while delta > std::f32::consts::PI {
                    delta -= std::f32::consts::TAU;
                }
                while delta < -std::f32::consts::PI {
                    delta += std::f32::consts::TAU;
                }
                let mut rotation = active.start_target.rotation;
                rotation[active.axis.index()] += delta.to_degrees();
                Some(Edit::Rotate {
                    axis: active.axis,
                    rotation,
                })
            }
        }
    }
}

impl canvas::Program<Message> for Program {
    type State = State;

    fn update(
        &self,
        state: &mut Self::State,
        event: &canvas::Event,
        bounds: Rectangle,
        cursor: mouse::Cursor,
    ) -> Option<canvas::Action<Message>> {
        if state.mode != Some(self.mode) {
            state.mode = Some(self.mode);
            state.active = None;
        }

        let local = cursor
            .position()
            .map(|position| Point::new(position.x - bounds.x, position.y - bounds.y));

        match event {
            canvas::Event::Mouse(mouse::Event::ButtonPressed(mouse::Button::Left)) => {
                if !cursor.is_over(bounds) {
                    return None;
                }
                let cursor = local?;
                let (axis, center, axis_end, _, axis_length_world) = self.hit_test(cursor)?;
                let target = self.target?;
                state.active = Some(Active {
                    axis,
                    start_cursor: cursor,
                    start_target: target,
                    center,
                    axis_end,
                    axis_length_world,
                });
                Some(canvas::Action::capture())
            }
            canvas::Event::Mouse(mouse::Event::CursorMoved { .. }) => {
                let active = state.active?;
                let cursor = local?;
                self.edit(active, cursor)
                    .map(|edit| canvas::Action::publish(Message::Changed(edit)).and_capture())
            }
            canvas::Event::Mouse(mouse::Event::ButtonReleased(mouse::Button::Left)) => {
                state.active.take().map(|_| canvas::Action::capture())
            }
            _ => None,
        }
    }

    fn draw(
        &self,
        state: &Self::State,
        renderer: &iced::Renderer,
        _theme: &iced::Theme,
        bounds: Rectangle,
        _cursor: mouse::Cursor,
    ) -> Vec<canvas::Geometry> {
        let Some(target) = self.target else {
            return Vec::new();
        };
        let Some(basis) = self.basis(bounds) else {
            return Vec::new();
        };
        let Some(center) = basis.project(target.world_position) else {
            return Vec::new();
        };
        let Some(units) = basis.units_per_pixel(target.world_position) else {
            return Vec::new();
        };

        let mut frame = Frame::new(renderer, bounds.size());
        let active = state.active.map(|active| active.axis);
        let handle_length = self.mode.handle_pixels() * units;

        match self.mode {
            Mode::Translate | Mode::Scale => {
                for axis in [Axis::X, Axis::Y, Axis::Z] {
                    let Some(end) = basis.project(add(
                        target.world_position,
                        scale(axis.vector(), handle_length),
                    )) else {
                        continue;
                    };
                    let color = if active == Some(axis) {
                        theme::WARNING
                    } else {
                        axis.color()
                    };
                    frame.stroke(
                        &Path::line(center, end),
                        Stroke::default()
                            .with_color(color)
                            .with_width(2.5)
                            .with_line_cap(LineCap::Round),
                    );

                    let direction = end - center;
                    let length = vector_length(direction).max(1e-3);
                    let direction = iced::Vector::new(direction.x / length, direction.y / length);
                    let perpendicular = iced::Vector::new(-direction.y, direction.x);
                    if self.mode == Mode::Translate {
                        let tip = end;
                        let base = end - vector_scale(direction, 10.0);
                        let arrow = Path::new(|builder| {
                            builder.move_to(tip);
                            builder.line_to(base + vector_scale(perpendicular, 4.5));
                            builder.line_to(base - vector_scale(perpendicular, 4.5));
                            builder.close();
                        });
                        frame.fill(&arrow, color);
                    } else {
                        let half = 4.5;
                        frame.fill(
                            &Path::rectangle(
                                Point::new(end.x - half, end.y - half),
                                Size::new(half * 2.0, half * 2.0),
                            ),
                            color,
                        );
                    }

                    frame.fill_text(Text {
                        content: axis.label().to_owned(),
                        position: end
                            + vector_scale(perpendicular, 10.0)
                            + vector_scale(direction, 8.0),
                        color,
                        size: 11.0.into(),
                        align_x: iced::alignment::Horizontal::Center.into(),
                        align_y: iced::alignment::Vertical::Center,
                        ..Text::default()
                    });
                }
            }
            Mode::Rotate => {
                for axis in [Axis::X, Axis::Y, Axis::Z] {
                    let Some(points) =
                        ring_points(target.world_position, axis, handle_length, &basis)
                    else {
                        continue;
                    };
                    let color = if active == Some(axis) {
                        theme::WARNING
                    } else {
                        axis.color()
                    };
                    let path = Path::new(|builder| {
                        builder.move_to(points[0]);
                        for point in &points[1..] {
                            builder.line_to(*point);
                        }
                    });
                    frame.stroke(
                        &path,
                        Stroke::default()
                            .with_color(color)
                            .with_width(2.0)
                            .with_line_cap(LineCap::Round),
                    );
                    let label_position = basis.project(add(
                        target.world_position,
                        scale(axis.vector(), handle_length * 1.22),
                    ));
                    if let Some(label_position) = label_position {
                        frame.fill_text(Text {
                            content: axis.label().to_owned(),
                            position: label_position,
                            color,
                            size: 11.0.into(),
                            align_x: iced::alignment::Horizontal::Center.into(),
                            align_y: iced::alignment::Vertical::Center,
                            ..Text::default()
                        });
                    }
                }
            }
        }

        frame.fill(&Path::circle(center, 3.5), theme::TEXT);
        vec![frame.into_geometry()]
    }

    fn mouse_interaction(
        &self,
        state: &Self::State,
        _bounds: Rectangle,
        cursor: mouse::Cursor,
    ) -> mouse::Interaction {
        if state.active.is_some() {
            return mouse::Interaction::Grabbing;
        }
        let Some(position) = cursor.position_in(_bounds) else {
            return mouse::Interaction::None;
        };
        if self.hit_test(position).is_some() {
            mouse::Interaction::Pointer
        } else {
            mouse::Interaction::None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_camera() -> Camera {
        Camera {
            yaw: 0.65,
            pitch: 0.35,
            distance: 10.0,
            focus: [0.0; 3],
        }
    }

    #[test]
    fn focus_projects_to_the_viewport_center() {
        let camera = test_camera();
        let viewport = Size::new(800.0, 600.0);
        let basis = Basis::new(&camera, viewport).expect("basis");
        let center = basis.project(camera.focus).expect("focus visible");
        assert!((center.x - 400.0).abs() < 0.01);
        assert!((center.y - 300.0).abs() < 0.01);
    }

    #[test]
    fn gizmo_uses_world_position_when_a_parent_offsets_the_entity() {
        let camera = test_camera();
        let viewport = Size::new(800.0, 600.0);
        let target = Target {
            position: [1.0, 0.0, 0.0],
            world_position: [6.0, 0.0, 0.0],
            rotation: [0.0; 3],
            scale: [1.0; 3],
        };
        let program = Program {
            mode: Mode::Translate,
            camera,
            viewport,
            target: Some(target),
        };
        let basis = program.basis(Rectangle::default()).expect("basis");
        let (center, _, _) = program
            .handle_geometry(target, &basis)
            .expect("gizmo geometry");
        let expected = basis.project([6.0, 0.0, 0.0]).expect("world center");
        assert!((center.x - expected.x).abs() < 0.01);
        assert!((center.y - expected.y).abs() < 0.01);
    }

    #[test]
    fn translate_drag_moves_along_the_axis() {
        let camera = test_camera();
        let viewport = Size::new(800.0, 600.0);
        let program = Program {
            mode: Mode::Translate,
            camera,
            viewport,
            target: Some(Target {
                position: [0.0; 3],
                world_position: [0.0; 3],
                rotation: [0.0; 3],
                scale: [1.0; 3],
            }),
        };
        let basis = program.basis(Rectangle::default()).expect("basis");
        let center = basis.project([0.0; 3]).expect("center");
        let units = basis.units_per_pixel([0.0; 3]).expect("units");
        let length = Mode::Translate.handle_pixels() * units;
        let axis_end = basis.project([length, 0.0, 0.0]).expect("axis endpoint");
        let active = Active {
            axis: Axis::X,
            start_cursor: center,
            start_target: program.target.expect("target"),
            center,
            axis_end,
            axis_length_world: length,
        };
        let edit = program
            .edit(active, center + (axis_end - center) * 0.5)
            .expect("edit");
        let Edit::Move { position, .. } = edit else {
            panic!("expected move edit");
        };
        assert!((position[0] - length * 0.5).abs() < 0.001);
        assert_eq!(position[1], 0.0);
        assert_eq!(position[2], 0.0);
    }
}

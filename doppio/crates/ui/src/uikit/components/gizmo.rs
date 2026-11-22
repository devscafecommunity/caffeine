//! Transform gizmo drawn over the viewport.
//!
//! The gizmo uses the same orbit camera as the engine viewport and emits absolute transform
//! edits, so the app applies them through the normal undo path.
//!
//! * **Move** (`1`): world-axis arrows with shaded 3D cones, three plane handles (drag in a
//!   plane) and depth cueing, so an axis pointing away from you reads as farther.
//! * **Rotate** (`2`): three rings with a bright front half and a faint back half, a translucent
//!   sphere, and a wedge plus the angle in degrees while you drag.
//! * **Scale** (`3`): handles along the object's **local** axes (so a rotated object scales along
//!   what you see), shaded cube ends and a centre cube that scales all three axes together.
//!
//! The handle under the cursor is highlighted before you press it.

use super::viewport_math::{
    self as vm, euler_axes, point_in_polygon, point_segment_distance, Basis, Vec3,
};
use crate::uikit::theme;
use doppio_engine::Camera;
use iced::widget::canvas::{self, Fill, Frame, LineCap, Path, Stroke, Text};
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
            Mode::Translate | Mode::Scale => 78.0,
            Mode::Rotate => 62.0,
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
    const ALL: [Axis; 3] = [Axis::X, Axis::Y, Axis::Z];

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

    fn vector(self) -> Vec3 {
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

/// The three coordinate planes (named by the two axes that span them).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Plane {
    XY,
    XZ,
    YZ,
}

impl Plane {
    const ALL: [Plane; 3] = [Plane::XY, Plane::XZ, Plane::YZ];

    pub fn label(self) -> &'static str {
        match self {
            Plane::XY => "XY",
            Plane::XZ => "XZ",
            Plane::YZ => "YZ",
        }
    }

    fn spans(self) -> (Axis, Axis) {
        match self {
            Plane::XY => (Axis::X, Axis::Y),
            Plane::XZ => (Axis::X, Axis::Z),
            Plane::YZ => (Axis::Y, Axis::Z),
        }
    }

    fn normal(self) -> Vec3 {
        match self {
            Plane::XY => [0.0, 0.0, 1.0],
            Plane::XZ => [0.0, 1.0, 0.0],
            Plane::YZ => [1.0, 0.0, 0.0],
        }
    }

    fn color(self) -> Color {
        match self {
            Plane::XY => Axis::Z.color(),
            Plane::XZ => Axis::Y.color(),
            Plane::YZ => Axis::X.color(),
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

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Edit {
    Move {
        axis: Axis,
        position: [f32; 3],
    },
    /// Drag inside a plane: both coordinates of `position` can change.
    MovePlane {
        plane: Plane,
        position: [f32; 3],
    },
    Rotate {
        axis: Axis,
        rotation: [f32; 3],
    },
    /// A ring drag: the object that had Euler angles `start` is turned by `angle` degrees about the
    /// ring's world axis. Its Euler angles come from [`Edit::turned`].
    Turn {
        axis: Axis,
        start: [f32; 3],
        angle: f32,
    },
    Scale {
        axis: Axis,
        scale: [f32; 3],
    },
    /// The centre handle: all three axes scale by the same factor.
    ScaleUniform {
        scale: [f32; 3],
    },
}

impl Edit {
    /// Short name for the undo history and the console.
    pub fn label(&self) -> String {
        match self {
            Edit::Move { axis, .. } => format!("Move {}", axis.label()),
            Edit::MovePlane { plane, .. } => format!("Move {}", plane.label()),
            Edit::Rotate { axis, .. } | Edit::Turn { axis, .. } => {
                format!("Rotate {}", axis.label())
            }
            Edit::Scale { axis, .. } => format!("Scale {}", axis.label()),
            Edit::ScaleUniform { .. } => "Scale uniformly".to_owned(),
        }
    }

    /// Edits to the same handle merge into one undo step.
    pub fn history_key(&self, entity: u32) -> String {
        match self {
            Edit::Move { axis, .. } => format!("gizmo:move:{entity}:{}", axis.label()),
            Edit::MovePlane { plane, .. } => format!("gizmo:move:{entity}:{}", plane.label()),
            Edit::Rotate { axis, .. } | Edit::Turn { axis, .. } => {
                format!("gizmo:rotate:{entity}:{}", axis.label())
            }
            Edit::Scale { axis, .. } => format!("gizmo:scale:{entity}:{}", axis.label()),
            Edit::ScaleUniform { .. } => format!("gizmo:scale:{entity}:uniform"),
        }
    }
}

impl Edit {
    /// The Euler angles a `Turn` leaves the object with (`None` for the other edits).
    pub fn turned(&self) -> Option<[f32; 3]> {
        match *self {
            Edit::Turn { axis, start, angle } => Some(vm::turn_euler(start, axis.vector(), angle)),
            _ => None,
        }
    }
}

/// Grid steps used when snapping is on: metres, degrees and a scale factor.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SnapSteps {
    pub translate: f32,
    pub rotate: f32,
    pub scale: f32,
}

impl Default for SnapSteps {
    fn default() -> Self {
        Self {
            translate: 0.25,
            rotate: 15.0,
            scale: 0.1,
        }
    }
}

fn round_to(value: f32, step: f32) -> f32 {
    if step <= 0.0 {
        value
    } else {
        // `+ 0.0` turns a negative zero into zero so history keys and text stay clean.
        (value / step).round() * step + 0.0
    }
}

impl Edit {
    /// The same edit with the handle's own values moved onto the grid. Only the coordinates the
    /// handle controls are snapped, so dragging X never nudges Y or Z.
    pub fn snapped(&self, steps: SnapSteps) -> Edit {
        match *self {
            Edit::Move { axis, mut position } => {
                let i = axis.index();
                position[i] = round_to(position[i], steps.translate);
                Edit::Move { axis, position }
            }
            Edit::MovePlane {
                plane,
                mut position,
            } => {
                let (a, b) = plane.spans();
                for axis in [a, b] {
                    position[axis.index()] = round_to(position[axis.index()], steps.translate);
                }
                Edit::MovePlane { plane, position }
            }
            Edit::Rotate { axis, mut rotation } => {
                let i = axis.index();
                rotation[i] = round_to(rotation[i], steps.rotate);
                Edit::Rotate { axis, rotation }
            }
            // The swept angle goes onto the grid, not the Euler components it ends up in.
            Edit::Turn { axis, start, angle } => Edit::Turn {
                axis,
                start,
                angle: round_to(angle, steps.rotate),
            },
            Edit::Scale { axis, mut scale } => {
                let i = axis.index();
                scale[i] = round_to(scale[i], steps.scale).max(steps.scale.max(0.01));
                Edit::Scale { axis, scale }
            }
            Edit::ScaleUniform { scale } => {
                let value = round_to(scale[0], steps.scale).max(steps.scale.max(0.01));
                Edit::ScaleUniform {
                    scale: [value, value, value],
                }
            }
        }
    }
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
    hover: Option<Handle>,
    mode: Option<Mode>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Handle {
    Axis(Axis),
    Plane(Plane),
    Uniform,
}

#[derive(Clone, Copy)]
struct Active {
    handle: Handle,
    start_cursor: Point,
    start_target: Target,
    center: Point,
    axis_end: Point,
    axis_length_world: f32,
    /// Where the press ray met the dragged plane (plane handles only).
    start_hit: Vec3,
}

/// Screen geometry of the axis handles.
struct Geometry {
    center: Point,
    length: f32,
    /// `(axis, world direction, screen end point)`.
    axes: Vec<(Axis, Vec3, Point)>,
}

struct Hit {
    handle: Handle,
    center: Point,
    axis_end: Point,
    length: f32,
}

// ------------------------------------------------------------------- colours

pub(super) fn shade(color: Color, factor: f32) -> Color {
    Color {
        r: (color.r * factor).clamp(0.0, 1.0),
        g: (color.g * factor).clamp(0.0, 1.0),
        b: (color.b * factor).clamp(0.0, 1.0),
        a: color.a,
    }
}

pub(super) fn faded(color: Color, alpha: f32) -> Color {
    Color {
        a: color.a * alpha,
        ..color
    }
}

pub(super) fn lighten(color: Color, amount: f32) -> Color {
    Color {
        r: color.r + (1.0 - color.r) * amount,
        g: color.g + (1.0 - color.g) * amount,
        b: color.b + (1.0 - color.b) * amount,
        a: color.a,
    }
}

/// Light used to shade the 3D handles: from the upper left, towards the camera.
pub(super) fn light_direction(basis: &Basis) -> Vec3 {
    vm::normalize(vm::add(
        vm::add(vm::scale(basis.up, 0.55), vm::scale(basis.right, -0.4)),
        vm::scale(basis.forward, -0.75),
    ))
}

/// 1 when `direction` points at the camera, 0 when it points away.
pub(super) fn facing(direction: Vec3, basis: &Basis) -> f32 {
    (-vm::dot(direction, basis.forward) * 0.5 + 0.5).clamp(0.0, 1.0)
}

pub(super) fn polygon(points: &[Point]) -> Path {
    Path::new(|builder| {
        if let Some(first) = points.first() {
            builder.move_to(*first);
            for point in &points[1..] {
                builder.line_to(*point);
            }
            builder.close();
        }
    })
}

// ------------------------------------------------------------------ 3D shapes

/// A shaded cone (arrow head) from a base circle to a tip, drawn back to front.
pub(super) fn draw_cone(
    frame: &mut Frame,
    basis: &Basis,
    base_center: Vec3,
    tip: Vec3,
    radius: f32,
    color: Color,
) {
    const SEGMENTS: usize = 12;
    let axis = vm::normalize(vm::sub(tip, base_center));
    let helper = if axis[1].abs() < 0.9 {
        [0.0, 1.0, 0.0]
    } else {
        [1.0, 0.0, 0.0]
    };
    let u = vm::normalize(vm::cross(axis, helper));
    let v = vm::cross(axis, u);
    let ring: Vec<Vec3> = (0..SEGMENTS)
        .map(|k| {
            let angle = k as f32 / SEGMENTS as f32 * std::f32::consts::TAU;
            vm::add(
                base_center,
                vm::add(
                    vm::scale(u, angle.cos() * radius),
                    vm::scale(v, angle.sin() * radius),
                ),
            )
        })
        .collect();
    let light = light_direction(basis);
    let mut faces: Vec<(f32, Vec<Point>, f32)> = Vec::new();
    for k in 0..SEGMENTS {
        let (a, b) = (ring[k], ring[(k + 1) % SEGMENTS]);
        let normal = vm::normalize(vm::cross(vm::sub(a, tip), vm::sub(b, tip)));
        let normal = if vm::dot(normal, vm::sub(a, base_center)) < 0.0 {
            vm::scale(normal, -1.0)
        } else {
            normal
        };
        let to_camera = vm::sub(basis.position, tip);
        if vm::dot(normal, to_camera) < -0.35 * vm::length(to_camera) * radius.min(1.0) {
            continue; // facing away
        }
        let (Some(pa), Some(pb), Some(pt)) =
            (basis.project(a), basis.project(b), basis.project(tip))
        else {
            continue;
        };
        let depth = basis.depth(vm::scale(vm::add(vm::add(a, b), tip), 1.0 / 3.0));
        let lambert = (vm::dot(normal, light) * 0.5 + 0.5).clamp(0.0, 1.0);
        faces.push((depth, vec![pt, pa, pb], 0.55 + 0.6 * lambert));
    }
    faces.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap_or(std::cmp::Ordering::Equal));
    for (_, points, light_factor) in faces {
        frame.fill(&polygon(&points), shade(color, light_factor));
    }
}

/// A shaded box oriented along `axes`, drawn back to front with hidden faces removed.
pub(super) fn draw_box(
    frame: &mut Frame,
    basis: &Basis,
    center: Vec3,
    axes: [Vec3; 3],
    half: f32,
    color: Color,
) {
    // Faces as (axis index, sign): the face centre sits at `center + axis * sign * half`.
    let mut faces: Vec<(f32, Vec<Point>, f32)> = Vec::new();
    let light = light_direction(basis);
    for (index, sign) in [
        (0, 1.0),
        (0, -1.0),
        (1, 1.0),
        (1, -1.0),
        (2, 1.0),
        (2, -1.0),
    ] {
        let normal = vm::scale(axes[index], sign);
        let face_center = vm::along(center, normal, half);
        if vm::dot(normal, vm::sub(basis.position, face_center)) <= 0.0 {
            continue;
        }
        let (a, b) = match index {
            0 => (axes[1], axes[2]),
            1 => (axes[0], axes[2]),
            _ => (axes[0], axes[1]),
        };
        let corners = [
            vm::add(face_center, vm::add(vm::scale(a, half), vm::scale(b, half))),
            vm::add(
                face_center,
                vm::add(vm::scale(a, -half), vm::scale(b, half)),
            ),
            vm::add(
                face_center,
                vm::add(vm::scale(a, -half), vm::scale(b, -half)),
            ),
            vm::add(
                face_center,
                vm::add(vm::scale(a, half), vm::scale(b, -half)),
            ),
        ];
        let projected: Vec<Point> = corners.iter().filter_map(|c| basis.project(*c)).collect();
        if projected.len() != 4 {
            continue;
        }
        let lambert = (vm::dot(normal, light) * 0.5 + 0.5).clamp(0.0, 1.0);
        faces.push((basis.depth(face_center), projected, 0.5 + 0.65 * lambert));
    }
    faces.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap_or(std::cmp::Ordering::Equal));
    for (_, points, light_factor) in faces {
        let path = polygon(&points);
        frame.fill(&path, shade(color, light_factor));
        frame.stroke(
            &path,
            Stroke::default()
                .with_color(shade(color, 0.45))
                .with_width(1.0),
        );
    }
}

pub(super) fn ring_world(center: Vec3, axis: Axis, radius: f32, segments: usize) -> Vec<Vec3> {
    (0..=segments)
        .map(|index| {
            let angle = index as f32 / segments as f32 * std::f32::consts::TAU;
            let (sin, cos) = angle.sin_cos();
            let offset = match axis {
                Axis::X => [0.0, cos * radius, sin * radius],
                Axis::Y => [cos * radius, 0.0, sin * radius],
                Axis::Z => [cos * radius, sin * radius, 0.0],
            };
            vm::add(center, offset)
        })
        .collect()
}

fn ring_points(center: Vec3, axis: Axis, radius: f32, basis: &Basis) -> Option<Vec<Point>> {
    ring_world(center, axis, radius, 64)
        .into_iter()
        .map(|point| basis.project(point))
        .collect()
}

// ------------------------------------------------------------------- program

impl Program {
    fn basis(&self) -> Option<Basis> {
        Basis::new(&self.camera, self.viewport)
    }

    /// Direction of an axis handle: world axes, except Scale which follows the object.
    fn axis_direction(&self, target: &Target, axis: Axis) -> Vec3 {
        match self.mode {
            Mode::Scale => euler_axes(target.rotation)[axis.index()],
            _ => axis.vector(),
        }
    }

    fn handle_geometry(&self, target: &Target, basis: &Basis) -> Option<Geometry> {
        let center = basis.project(target.world_position)?;
        let units = basis.units_per_pixel(target.world_position)?;
        let length = self.mode.handle_pixels() * units;
        let mut axes = Vec::new();
        for axis in Axis::ALL {
            let direction = self.axis_direction(target, axis);
            let end = basis.project(vm::along(target.world_position, direction, length))?;
            axes.push((axis, direction, end));
        }
        Some(Geometry {
            center,
            length,
            axes,
        })
    }

    /// The four corners of a plane handle, in world space.
    fn plane_corners(&self, target: &Target, plane: Plane, length: f32) -> [Vec3; 4] {
        let (a, b) = plane.spans();
        let (a, b) = (
            self.axis_direction(target, a),
            self.axis_direction(target, b),
        );
        let at = |s: f32, t: f32| {
            vm::add(
                target.world_position,
                vm::add(vm::scale(a, s * length), vm::scale(b, t * length)),
            )
        };
        [
            at(0.28, 0.28),
            at(0.56, 0.28),
            at(0.56, 0.56),
            at(0.28, 0.56),
        ]
    }

    fn plane_polygon(
        &self,
        target: &Target,
        plane: Plane,
        length: f32,
        basis: &Basis,
    ) -> Option<Vec<Point>> {
        self.plane_corners(target, plane, length)
            .iter()
            .map(|corner| basis.project(*corner))
            .collect()
    }

    fn hit_test(&self, cursor: Point) -> Option<Hit> {
        let target = self.target?;
        let basis = self.basis()?;
        match self.mode {
            Mode::Translate | Mode::Scale => {
                let geometry = self.handle_geometry(&target, &basis)?;
                let distance_to_center = {
                    let d = cursor - geometry.center;
                    (d.x * d.x + d.y * d.y).sqrt()
                };
                if self.mode == Mode::Scale && distance_to_center < 11.0 {
                    return Some(Hit {
                        handle: Handle::Uniform,
                        center: geometry.center,
                        axis_end: geometry.center,
                        length: geometry.length,
                    });
                }
                if distance_to_center < 12.0 {
                    return None;
                }
                if self.mode == Mode::Translate {
                    for plane in Plane::ALL {
                        if let Some(points) =
                            self.plane_polygon(&target, plane, geometry.length, &basis)
                        {
                            if point_in_polygon(cursor, &points) {
                                return Some(Hit {
                                    handle: Handle::Plane(plane),
                                    center: geometry.center,
                                    axis_end: geometry.center,
                                    length: geometry.length,
                                });
                            }
                        }
                    }
                }
                let mut best: Option<Hit> = None;
                let mut best_distance = 9.0;
                for (axis, _, end) in &geometry.axes {
                    let distance = point_segment_distance(cursor, geometry.center, *end);
                    if distance < best_distance {
                        best_distance = distance;
                        best = Some(Hit {
                            handle: Handle::Axis(*axis),
                            center: geometry.center,
                            axis_end: *end,
                            length: geometry.length,
                        });
                    }
                }
                best
            }
            Mode::Rotate => {
                let center = basis.project(target.world_position)?;
                let units = basis.units_per_pixel(target.world_position)?;
                let radius = self.mode.handle_pixels() * units;
                let mut best: Option<Hit> = None;
                let mut best_distance = 8.0;
                for axis in Axis::ALL {
                    let Some(points) = ring_points(target.world_position, axis, radius, &basis)
                    else {
                        continue;
                    };
                    for window in points.windows(2) {
                        let distance = point_segment_distance(cursor, window[0], window[1]);
                        if distance < best_distance {
                            best_distance = distance;
                            best = Some(Hit {
                                handle: Handle::Axis(axis),
                                center,
                                axis_end: window[0],
                                length: radius,
                            });
                        }
                    }
                }
                best
            }
        }
    }

    fn edit(&self, active: Active, cursor: Point) -> Option<Edit> {
        match (self.mode, active.handle) {
            (Mode::Translate, Handle::Axis(axis)) => {
                let axis_screen = active.axis_end - active.center;
                let length_sq = axis_screen.x * axis_screen.x + axis_screen.y * axis_screen.y;
                if length_sq < 1e-4 {
                    return None;
                }
                let delta = cursor - active.start_cursor;
                let t = (delta.x * axis_screen.x + delta.y * axis_screen.y) / length_sq;
                let position = vm::along(
                    active.start_target.world_position,
                    axis.vector(),
                    t * active.axis_length_world,
                );
                Some(Edit::Move { axis, position })
            }
            (Mode::Translate, Handle::Plane(plane)) => {
                let basis = self.basis()?;
                let (origin, direction) = basis.ray(cursor);
                let hit = vm::ray_plane(
                    origin,
                    direction,
                    active.start_target.world_position,
                    plane.normal(),
                )?;
                let position = vm::add(
                    active.start_target.world_position,
                    vm::sub(hit, active.start_hit),
                );
                Some(Edit::MovePlane { plane, position })
            }
            (Mode::Scale, Handle::Axis(axis)) => {
                let axis_screen = active.axis_end - active.center;
                let length_sq = axis_screen.x * axis_screen.x + axis_screen.y * axis_screen.y;
                if length_sq < 1e-4 {
                    return None;
                }
                let delta = cursor - active.start_cursor;
                let t = (delta.x * axis_screen.x + delta.y * axis_screen.y) / length_sq;
                let mut scale = active.start_target.scale;
                scale[axis.index()] = (scale[axis.index()] * (1.0 + t)).clamp(0.01, 1000.0);
                Some(Edit::Scale { axis, scale })
            }
            (Mode::Scale, Handle::Uniform) => {
                let delta = cursor - active.start_cursor;
                // Right / up grows, left / down shrinks, exponentially so it never flips sign.
                let factor = ((delta.x - delta.y) / 140.0).exp();
                let start = active.start_target.scale;
                Some(Edit::ScaleUniform {
                    scale: [
                        (start[0] * factor).clamp(0.01, 1000.0),
                        (start[1] * factor).clamp(0.01, 1000.0),
                        (start[2] * factor).clamp(0.01, 1000.0),
                    ],
                })
            }
            (Mode::Rotate, Handle::Axis(axis)) => {
                let center = active.center;
                let start = active.start_cursor - center;
                let current = cursor - center;
                let (start_len, current_len) = (
                    (start.x * start.x + start.y * start.y).sqrt(),
                    (current.x * current.x + current.y * current.y).sqrt(),
                );
                if start_len < 1e-3 || current_len < 1e-3 {
                    return None;
                }
                let delta = sweep(start, current) * self.rotation_sign(axis);
                Some(Edit::Turn {
                    axis,
                    start: active.start_target.rotation,
                    angle: delta.to_degrees(),
                })
            }
            _ => None,
        }
    }

    /// Screen angles run clockwise (y points down); a positive rotation about an axis looks
    /// counter-clockwise when that axis points at the viewer. The sign makes the object turn the
    /// way the cursor goes whichever side of the ring you look at.
    fn rotation_sign(&self, axis: Axis) -> f32 {
        let Some(basis) = self.basis() else {
            return 1.0;
        };
        if vm::dot(axis.vector(), basis.forward) < 0.0 {
            -1.0
        } else {
            1.0
        }
    }

    fn press(&self, cursor: Point) -> Option<Active> {
        let hit = self.hit_test(cursor)?;
        let target = self.target?;
        let mut start_hit = target.world_position;
        if let Handle::Plane(plane) = hit.handle {
            let basis = self.basis()?;
            let (origin, direction) = basis.ray(cursor);
            start_hit = vm::ray_plane(origin, direction, target.world_position, plane.normal())?;
        }
        Some(Active {
            handle: hit.handle,
            start_cursor: cursor,
            start_target: target,
            center: hit.center,
            axis_end: hit.axis_end,
            axis_length_world: hit.length,
            start_hit,
        })
    }
}

/// Signed angle (radians, shortest way) from `start` to `current`.
fn sweep(start: iced::Vector, current: iced::Vector) -> f32 {
    let mut delta = current.y.atan2(current.x) - start.y.atan2(start.x);
    while delta > std::f32::consts::PI {
        delta -= std::f32::consts::TAU;
    }
    while delta < -std::f32::consts::PI {
        delta += std::f32::consts::TAU;
    }
    delta
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
            state.hover = None;
        }

        let local = cursor
            .position()
            .map(|position| Point::new(position.x - bounds.x, position.y - bounds.y));

        match event {
            canvas::Event::Mouse(mouse::Event::ButtonPressed(mouse::Button::Left)) => {
                if !cursor.is_over(bounds) {
                    return None;
                }
                let active = self.press(local?)?;
                state.active = Some(active);
                state.hover = Some(active.handle);
                Some(canvas::Action::capture())
            }
            canvas::Event::Mouse(mouse::Event::CursorMoved { .. }) => {
                let Some(active) = state.active else {
                    let hover = local
                        .filter(|_| cursor.is_over(bounds))
                        .and_then(|point| self.hit_test(point))
                        .map(|hit| hit.handle);
                    if hover != state.hover {
                        state.hover = hover;
                        return Some(canvas::Action::request_redraw());
                    }
                    return None;
                };
                self.edit(active, local?)
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
        let Some(basis) = self.basis() else {
            return Vec::new();
        };
        let Some(center) = basis.project(target.world_position) else {
            return Vec::new();
        };
        let Some(units) = basis.units_per_pixel(target.world_position) else {
            return Vec::new();
        };

        let mut frame = Frame::new(renderer, bounds.size());
        let active = state.active.map(|active| active.handle);
        let emphasised = active.or(state.hover);
        let handle_length = self.mode.handle_pixels() * units;
        let colour_of = |handle: Handle, base: Color| {
            if active == Some(handle) {
                theme::WARNING
            } else if emphasised == Some(handle) {
                lighten(base, 0.45)
            } else {
                base
            }
        };

        match self.mode {
            Mode::Translate | Mode::Scale => {
                if self.mode == Mode::Translate {
                    for plane in Plane::ALL {
                        let Some(points) =
                            self.plane_polygon(&target, plane, handle_length, &basis)
                        else {
                            continue;
                        };
                        let handle = Handle::Plane(plane);
                        let color = colour_of(handle, plane.color());
                        let hot = emphasised == Some(handle);
                        let path = polygon(&points);
                        frame.fill(&path, faded(color, if hot { 0.55 } else { 0.28 }));
                        frame.stroke(
                            &path,
                            Stroke::default()
                                .with_color(faded(color, if hot { 1.0 } else { 0.75 }))
                                .with_width(if hot { 2.0 } else { 1.2 }),
                        );
                    }
                }
                // Far axes first so near ones overlap them.
                let mut axes: Vec<(Axis, Vec3)> = Axis::ALL
                    .iter()
                    .map(|axis| (*axis, self.axis_direction(&target, *axis)))
                    .collect();
                axes.sort_by(|a, b| {
                    let depth = |direction: Vec3| {
                        basis.depth(vm::along(target.world_position, direction, handle_length))
                    };
                    depth(b.1)
                        .partial_cmp(&depth(a.1))
                        .unwrap_or(std::cmp::Ordering::Equal)
                });
                for (axis, direction) in axes {
                    let tip = vm::along(target.world_position, direction, handle_length);
                    let Some(end) = basis.project(tip) else {
                        continue;
                    };
                    let handle = Handle::Axis(axis);
                    let color = colour_of(handle, axis.color());
                    let near = facing(direction, &basis);
                    let alpha = 0.6 + 0.4 * near;
                    let hot = emphasised == Some(handle);
                    // A soft dark halo under the line keeps it readable over any surface.
                    frame.stroke(
                        &Path::line(center, end),
                        Stroke::default()
                            .with_color(theme::OVERLAY_HALO)
                            .with_width(if hot { 6.0 } else { 5.0 })
                            .with_line_cap(LineCap::Round),
                    );
                    frame.stroke(
                        &Path::line(center, end),
                        Stroke::default()
                            .with_color(faded(color, alpha))
                            .with_width(if hot { 3.6 } else { 2.8 })
                            .with_line_cap(LineCap::Round),
                    );
                    if self.mode == Mode::Translate {
                        let base =
                            vm::along(target.world_position, direction, handle_length * 0.78);
                        draw_cone(
                            &mut frame,
                            &basis,
                            base,
                            vm::along(target.world_position, direction, handle_length * 1.08),
                            handle_length * 0.075,
                            faded(color, alpha.max(0.8)),
                        );
                    } else {
                        let axes = [
                            self.axis_direction(&target, Axis::X),
                            self.axis_direction(&target, Axis::Y),
                            self.axis_direction(&target, Axis::Z),
                        ];
                        draw_box(
                            &mut frame,
                            &basis,
                            tip,
                            axes,
                            handle_length * 0.075,
                            faded(color, alpha.max(0.85)),
                        );
                    }
                    let outward = {
                        let d = end - center;
                        let len = (d.x * d.x + d.y * d.y).sqrt().max(1e-3);
                        iced::Vector::new(d.x / len, d.y / len)
                    };
                    frame.fill_text(Text {
                        content: axis.label().to_owned(),
                        position: end + iced::Vector::new(outward.x * 17.0, outward.y * 17.0),
                        color: faded(color, alpha),
                        size: 11.0.into(),
                        align_x: iced::alignment::Horizontal::Center.into(),
                        align_y: iced::alignment::Vertical::Center,
                        ..Text::default()
                    });
                }
                if self.mode == Mode::Scale {
                    let handle = Handle::Uniform;
                    let color = colour_of(handle, theme::OVERLAY_NEUTRAL);
                    draw_box(
                        &mut frame,
                        &basis,
                        target.world_position,
                        [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
                        units * 5.5,
                        color,
                    );
                }
            }
            Mode::Rotate => {
                // Translucent sphere the rings sit on.
                if let Some(edge) =
                    basis.project(vm::along(target.world_position, basis.right, handle_length))
                {
                    let radius = {
                        let d = edge - center;
                        (d.x * d.x + d.y * d.y).sqrt()
                    };
                    let disc = Path::circle(center, radius);
                    frame.fill(&disc, Fill::from(theme::OVERLAY_SPHERE_FILL));
                    frame.stroke(
                        &disc,
                        Stroke::default()
                            .with_color(theme::OVERLAY_SPHERE_EDGE)
                            .with_width(1.0),
                    );
                }
                let center_depth = basis.depth(target.world_position);
                for axis in Axis::ALL {
                    let handle = Handle::Axis(axis);
                    let color = colour_of(handle, axis.color());
                    let hot = emphasised == Some(handle);
                    let world = ring_world(target.world_position, axis, handle_length, 64);
                    // Back half first (faint), then the front half (bright).
                    for front in [false, true] {
                        let mut run: Vec<Point> = Vec::new();
                        let flush = |run: &mut Vec<Point>, frame: &mut Frame| {
                            if run.len() > 1 {
                                let path = Path::new(|builder| {
                                    builder.move_to(run[0]);
                                    for point in &run[1..] {
                                        builder.line_to(*point);
                                    }
                                });
                                frame.stroke(
                                    &path,
                                    Stroke::default()
                                        .with_color(faded(color, if front { 1.0 } else { 0.32 }))
                                        .with_width(match (front, hot) {
                                            (true, true) => 3.6,
                                            (true, false) => 2.6,
                                            (false, _) => 1.3,
                                        })
                                        .with_line_cap(LineCap::Round),
                                );
                            }
                            run.clear();
                        };
                        for segment in world.windows(2) {
                            let middle = vm::scale(vm::add(segment[0], segment[1]), 0.5);
                            let is_front = basis.depth(middle) < center_depth;
                            if is_front == front {
                                if let (Some(a), Some(b)) =
                                    (basis.project(segment[0]), basis.project(segment[1]))
                                {
                                    if run.is_empty() {
                                        run.push(a);
                                    }
                                    run.push(b);
                                    continue;
                                }
                            }
                            flush(&mut run, &mut frame);
                        }
                        flush(&mut run, &mut frame);
                    }
                    if let Some(label) = basis.project(vm::along(
                        target.world_position,
                        axis.vector(),
                        handle_length * 1.2,
                    )) {
                        frame.fill_text(Text {
                            content: axis.label().to_owned(),
                            position: label,
                            color: faded(color, 0.9),
                            size: 11.0.into(),
                            align_x: iced::alignment::Horizontal::Center.into(),
                            align_y: iced::alignment::Vertical::Center,
                            ..Text::default()
                        });
                    }
                }
            }
        }

        // While rotating, show the swept wedge and the angle.
        if let (Some(active), Mode::Rotate) = (state.active, self.mode) {
            if let (Some(cursor_point), Handle::Axis(axis)) =
                (_cursor.position_in(bounds), active.handle)
            {
                let start = active.start_cursor - active.center;
                let current = cursor_point - active.center;
                let delta = sweep(start, current);
                let radius = {
                    let d = active.axis_end - active.center;
                    (d.x * d.x + d.y * d.y).sqrt().max(30.0)
                } * 0.8;
                let start_angle = start.y.atan2(start.x);
                let steps = ((delta.abs() / 0.12).ceil() as usize).max(2);
                let wedge = Path::new(|builder| {
                    builder.move_to(active.center);
                    for step in 0..=steps {
                        let angle = start_angle + delta * step as f32 / steps as f32;
                        builder.line_to(Point::new(
                            active.center.x + angle.cos() * radius,
                            active.center.y + angle.sin() * radius,
                        ));
                    }
                    builder.close();
                });
                frame.fill(&wedge, faded(theme::WARNING, 0.22));
                frame.stroke(
                    &wedge,
                    Stroke::default()
                        .with_color(faded(theme::WARNING, 0.8))
                        .with_width(1.2),
                );
                // The label follows the object's real turn (the sign flips with the viewing side).
                let turned = delta * self.rotation_sign(axis);
                let label_at = active.center + iced::Vector::new(0.0, radius + 26.0);
                frame.fill(
                    &Path::rectangle(
                        Point::new(label_at.x - 62.0, label_at.y - 10.0),
                        Size::new(124.0, 20.0),
                    ),
                    theme::OVERLAY_PLATE,
                );
                frame.fill_text(Text {
                    content: format!(
                        "{} {:+.1}°  →  {:.1}°",
                        axis.label(),
                        turned.to_degrees(),
                        active.start_target.rotation[axis.index()] + turned.to_degrees()
                    ),
                    position: label_at,
                    color: theme::WARNING,
                    size: 12.0.into(),
                    align_x: iced::alignment::Horizontal::Center.into(),
                    align_y: iced::alignment::Vertical::Center,
                    ..Text::default()
                });
            }
        }

        if self.mode != Mode::Scale {
            frame.fill(&Path::circle(center, 3.5), theme::TEXT);
        }
        vec![frame.into_geometry()]
    }

    fn mouse_interaction(
        &self,
        state: &Self::State,
        bounds: Rectangle,
        cursor: mouse::Cursor,
    ) -> mouse::Interaction {
        if state.active.is_some() {
            return mouse::Interaction::Grabbing;
        }
        let Some(position) = cursor.position_in(bounds) else {
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

    const VIEWPORT: Size = Size {
        width: 800.0,
        height: 600.0,
    };

    fn test_camera() -> Camera {
        Camera {
            yaw: 0.65,
            pitch: 0.35,
            distance: 10.0,
            focus: [0.0; 3],
        }
    }

    fn target_at(position: [f32; 3], rotation: [f32; 3], scale: [f32; 3]) -> Target {
        Target {
            position,
            world_position: position,
            rotation,
            scale,
        }
    }

    fn program(mode: Mode, target: Target) -> Program {
        Program {
            mode,
            camera: test_camera(),
            viewport: VIEWPORT,
            target: Some(target),
        }
    }

    #[test]
    fn focus_projects_to_the_viewport_center() {
        let basis = Basis::new(&test_camera(), VIEWPORT).expect("basis");
        let center = basis.project([0.0; 3]).expect("focus visible");
        assert!((center.x - 400.0).abs() < 0.01);
        assert!((center.y - 300.0).abs() < 0.01);
    }

    #[test]
    fn gizmo_uses_world_position_when_a_parent_offsets_the_entity() {
        let target = Target {
            position: [1.0, 0.0, 0.0],
            world_position: [6.0, 0.0, 0.0],
            rotation: [0.0; 3],
            scale: [1.0; 3],
        };
        let program = program(Mode::Translate, target);
        let basis = program.basis().expect("basis");
        let geometry = program.handle_geometry(&target, &basis).expect("geometry");
        let expected = basis.project([6.0, 0.0, 0.0]).expect("world center");
        assert!((geometry.center.x - expected.x).abs() < 0.01);
        assert!((geometry.center.y - expected.y).abs() < 0.01);
    }

    fn press_axis(program: &Program, axis: Axis, fraction: f32) -> (Active, Point) {
        let target = program.target.expect("target");
        let basis = program.basis().expect("basis");
        let geometry = program.handle_geometry(&target, &basis).expect("geometry");
        let (_, _, end) = geometry
            .axes
            .iter()
            .find(|(a, _, _)| *a == axis)
            .copied()
            .expect("axis");
        let on_handle = geometry.center + (end - geometry.center) * 0.6;
        let active = program.press(on_handle).expect("press the handle");
        assert_eq!(
            active.handle,
            Handle::Axis(axis),
            "pressed the wrong handle"
        );
        let moved = geometry.center + (end - geometry.center) * (0.6 + fraction);
        (active, moved)
    }

    #[test]
    fn translate_drag_moves_along_the_axis() {
        let program = program(Mode::Translate, target_at([0.0; 3], [0.0; 3], [1.0; 3]));
        let (active, moved) = press_axis(&program, Axis::X, 0.5);
        let Some(Edit::Move { position, .. }) = program.edit(active, moved) else {
            panic!("expected a move edit");
        };
        assert!(position[0] > 0.1, "x grows along the handle: {position:?}");
        assert!(position[1].abs() < 1e-3 && position[2].abs() < 1e-3);
    }

    #[test]
    fn plane_handles_move_inside_their_plane() {
        let program = program(Mode::Translate, target_at([0.0; 3], [0.0; 3], [1.0; 3]));
        let target = program.target.unwrap();
        let basis = program.basis().unwrap();
        let geometry = program.handle_geometry(&target, &basis).unwrap();
        let corners = program.plane_corners(&target, Plane::XZ, geometry.length);
        let middle = basis
            .project(vm::scale(vm::add(corners[0], corners[2]), 0.5))
            .unwrap();
        let active = program.press(middle).expect("press the XZ plane handle");
        assert_eq!(active.handle, Handle::Plane(Plane::XZ));
        let Some(Edit::MovePlane { plane, position }) =
            program.edit(active, middle + iced::Vector::new(40.0, 25.0))
        else {
            panic!("expected a plane edit");
        };
        assert_eq!(plane, Plane::XZ);
        assert!(position[1].abs() < 1e-3, "an XZ drag keeps Y: {position:?}");
        assert!(position[0].abs() + position[2].abs() > 0.05, "{position:?}");
    }

    #[test]
    fn scale_drag_grows_one_axis_and_clamps() {
        let program = program(Mode::Scale, target_at([0.0; 3], [0.0; 3], [2.0, 1.0, 1.0]));
        let (active, moved) = press_axis(&program, Axis::X, 0.4);
        let Some(Edit::Scale { axis, scale }) = program.edit(active, moved) else {
            panic!("expected a scale edit");
        };
        assert_eq!(axis, Axis::X);
        assert!(scale[0] > 2.0, "{scale:?}");
        assert_eq!((scale[1], scale[2]), (1.0, 1.0));
        // Dragging far past the origin never flips the sign.
        let far = active.center - (active.axis_end - active.center) * 5.0;
        let Some(Edit::Scale { scale, .. }) = program.edit(active, far) else {
            panic!("expected a scale edit");
        };
        assert!(scale[0] >= 0.01);
    }

    #[test]
    fn scale_handles_follow_the_objects_local_axes() {
        // Rotated 90 degrees about Y: the local X axis points along world -Z.
        let rotated = program(Mode::Scale, target_at([0.0; 3], [0.0, 90.0, 0.0], [1.0; 3]));
        let upright = program(Mode::Scale, target_at([0.0; 3], [0.0; 3], [1.0; 3]));
        let end_of = |program: &Program| {
            let target = program.target.unwrap();
            let basis = program.basis().unwrap();
            program
                .handle_geometry(&target, &basis)
                .unwrap()
                .axes
                .iter()
                .find(|(axis, _, _)| *axis == Axis::X)
                .unwrap()
                .2
        };
        let (a, b) = (end_of(&rotated), end_of(&upright));
        assert!(
            (a.x - b.x).abs() + (a.y - b.y).abs() > 20.0,
            "a rotated object's X handle must move on screen: {a:?} vs {b:?}"
        );
        // Pressing the rotated handle still selects X.
        let (active, _) = press_axis(&rotated, Axis::X, 0.1);
        assert_eq!(active.handle, Handle::Axis(Axis::X));
    }

    #[test]
    fn the_centre_cube_scales_everything_together() {
        let program = program(Mode::Scale, target_at([0.0; 3], [0.0; 3], [1.0, 2.0, 3.0]));
        let basis = program.basis().unwrap();
        let center = basis.project([0.0; 3]).unwrap();
        let active = program.press(center).expect("press the centre cube");
        assert_eq!(active.handle, Handle::Uniform);
        let Some(Edit::ScaleUniform { scale }) =
            program.edit(active, center + iced::Vector::new(70.0, 0.0))
        else {
            panic!("expected a uniform edit");
        };
        let factor = scale[0] / 1.0;
        assert!(factor > 1.3);
        assert!((scale[1] / 2.0 - factor).abs() < 1e-4 && (scale[2] / 3.0 - factor).abs() < 1e-4);
        let Some(Edit::ScaleUniform { scale: smaller }) =
            program.edit(active, center + iced::Vector::new(-70.0, 0.0))
        else {
            panic!("expected a uniform edit");
        };
        assert!(smaller[0] < 1.0 && smaller[0] > 0.0);
    }

    #[test]
    fn rotate_drag_adds_the_swept_angle() {
        let program = program(
            Mode::Rotate,
            target_at([0.0; 3], [10.0, 0.0, 0.0], [1.0; 3]),
        );
        let target = program.target.unwrap();
        let basis = program.basis().unwrap();
        let center = basis.project(target.world_position).unwrap();
        let units = basis.units_per_pixel(target.world_position).unwrap();
        let ring = ring_points(
            target.world_position,
            Axis::Y,
            Mode::Rotate.handle_pixels() * units,
            &basis,
        )
        .unwrap();
        let start = ring[0];
        let active = program.press(start).expect("press the Y ring");
        assert_eq!(active.handle, Handle::Axis(Axis::Y));
        // A quarter turn around the centre in screen space.
        let radial = start - center;
        let quarter = center + iced::Vector::new(-radial.y, radial.x);
        let Some(edit @ Edit::Turn { angle, start, .. }) = program.edit(active, quarter) else {
            panic!("expected a turn");
        };
        assert!((angle.abs() - 90.0).abs() < 1.0, "{angle}");
        assert_eq!(
            start,
            [10.0, 0.0, 0.0],
            "the turn starts from the pressed rotation"
        );
        // The object ends up turned a quarter about the world Y axis, whatever Euler angles say so.
        let turned = edit.turned().expect("a turn has Euler angles");
        let expected = vm::turn_euler([10.0, 0.0, 0.0], Axis::Y.vector(), angle);
        for (a, b) in vm::euler_axes(turned)
            .iter()
            .zip(vm::euler_axes(expected).iter())
        {
            assert!(
                vm::length(vm::sub(*a, *b)) < 1e-3,
                "{turned:?} vs {expected:?}"
            );
        }
    }

    #[test]
    fn rotation_turns_the_way_the_cursor_goes_from_either_side() {
        // The same clockwise cursor sweep must turn the object in opposite world directions
        // when the axis points at the viewer versus away from it.
        let mut toward = program(Mode::Rotate, target_at([0.0; 3], [0.0; 3], [1.0; 3]));
        let mut away = program(Mode::Rotate, target_at([0.0; 3], [0.0; 3], [1.0; 3]));
        toward.camera.yaw = 0.0; // looks along +Z: the Z axis points away from the viewer
        away.camera.yaw = std::f32::consts::PI;
        assert_ne!(toward.rotation_sign(Axis::Z), away.rotation_sign(Axis::Z));
        for program in [&toward, &away] {
            let basis = program.basis().unwrap();
            let center = basis.project([0.0; 3]).unwrap();
            let units = basis.units_per_pixel([0.0; 3]).unwrap();
            let ring = ring_points(
                [0.0; 3],
                Axis::Z,
                Mode::Rotate.handle_pixels() * units,
                &basis,
            )
            .unwrap();
            let active = program.press(ring[8]).expect("press the Z ring");
            let radial = ring[8] - center;
            // Clockwise on screen: rotate the radial vector by +90 degrees in y-down coordinates.
            let clockwise = center + iced::Vector::new(-radial.y, radial.x);
            let Some(Edit::Turn { angle, .. }) = program.edit(active, clockwise) else {
                panic!("expected a turn");
            };
            // Clockwise as seen by the viewer is a negative turn about an axis pointing at them.
            let at_viewer = vm::dot(Axis::Z.vector(), basis.forward) < 0.0;
            assert_eq!(angle < 0.0, at_viewer, "{angle}");
        }
    }

    #[test]
    fn edits_have_stable_history_keys() {
        let edit = Edit::MovePlane {
            plane: Plane::XY,
            position: [0.0; 3],
        };
        assert_eq!(edit.history_key(7), "gizmo:move:7:XY");
        assert_eq!(edit.label(), "Move XY");
        assert_eq!(
            Edit::ScaleUniform { scale: [1.0; 3] }.history_key(7),
            "gizmo:scale:7:uniform"
        );
    }
}

#[cfg(test)]
mod snap_tests {
    use super::*;

    #[test]
    fn moving_one_axis_only_snaps_that_axis() {
        let edit = Edit::Move {
            axis: Axis::X,
            position: [1.13, 2.37, -0.61],
        };
        let Edit::Move { position, .. } = edit.snapped(SnapSteps::default()) else {
            panic!("still a move");
        };
        assert_eq!(position, [1.25, 2.37, -0.61]);
    }

    #[test]
    fn plane_moves_snap_both_coordinates_of_the_plane() {
        let edit = Edit::MovePlane {
            plane: Plane::XZ,
            position: [0.9, 5.55, -1.1],
        };
        let Edit::MovePlane { position, .. } = edit.snapped(SnapSteps::default()) else {
            panic!("still a plane move");
        };
        assert_eq!(position, [1.0, 5.55, -1.0]);
    }

    #[test]
    fn a_ring_turn_snaps_its_swept_angle_not_the_euler_components() {
        let edit = Edit::Turn {
            axis: Axis::X,
            start: [0.0, 180.0, 0.0],
            angle: 37.4,
        };
        let Edit::Turn { angle, start, .. } = edit.snapped(SnapSteps::default()) else {
            panic!("still a turn");
        };
        assert_eq!(angle, 30.0);
        assert_eq!(start, [0.0, 180.0, 0.0]);
    }

    #[test]
    fn rotation_snaps_to_degrees_and_never_leaves_negative_zero() {
        let edit = Edit::Rotate {
            axis: Axis::Y,
            rotation: [3.0, 37.4, -2.0],
        };
        let Edit::Rotate { rotation, .. } = edit.snapped(SnapSteps::default()) else {
            panic!("still a rotation");
        };
        assert_eq!(rotation, [3.0, 30.0, -2.0]);
        assert!(round_to(-0.01, 15.0).is_sign_positive());
    }

    #[test]
    fn scale_snaps_and_never_reaches_zero() {
        let Edit::ScaleUniform { scale } = (Edit::ScaleUniform {
            scale: [0.012, 0.012, 0.012],
        })
        .snapped(SnapSteps::default()) else {
            panic!("still uniform");
        };
        assert_eq!(scale, [0.1, 0.1, 0.1]);
        let Edit::Scale { scale, .. } = (Edit::Scale {
            axis: Axis::Z,
            scale: [1.0, 1.0, 2.46],
        })
        .snapped(SnapSteps::default()) else {
            panic!("still a scale");
        };
        assert_eq!(scale, [1.0, 1.0, 2.5]);
    }

    #[test]
    fn a_zero_step_leaves_values_alone() {
        let steps = SnapSteps {
            translate: 0.0,
            rotate: 0.0,
            scale: 0.0,
        };
        let edit = Edit::Move {
            axis: Axis::Z,
            position: [0.1, 0.2, 0.3],
        };
        assert_eq!(edit.snapped(steps), edit);
    }
}

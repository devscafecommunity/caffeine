//! Scene markers: viewport overlays that make non-mesh entities visible and readable.
//!
//! Lights, cameras, colliders and empty entities have no geometry, so without help they are
//! invisible in the viewport. This overlay draws, in the same projection as the transform gizmo:
//!
//! | Entity | Marker |
//! |---|---|
//! | Directional light | a sun with a bundle of parallel rays (the real shining direction, including the default sun used when the light is not rotated) |
//! | Point light | a bulb, plus the radius as a wire sphere when selected |
//! | Spot light | a bulb and the cone (angle and range) |
//! | Camera | a camera body with the view frustum (field of view, aspect, near and far) |
//! | Collider 3D | the box, sphere or capsule it uses (selected entity) |
//! | Empty entity | a small three-axis locator |
//!
//! Every marker has a faint vertical stem down to the ground and a ground ring so its height is
//! readable. Clicking a marker selects its entity (they cannot be picked by the mesh raycast).

use super::gizmo::{draw_box, draw_cone, faded, polygon, ring_world, shade};
use super::viewport_math::{self as vm, euler_axes, forward_from_euler, Basis, Vec3};
use crate::uikit::theme;
use doppio_engine::{Camera, Engine, FieldValue};
use iced::widget::canvas::{self, Frame, LineCap, Path, Stroke, Text};
use iced::{mouse, Color, Element, Length, Point, Rectangle, Size};

/// `(id, name, position, rotation, scale)` of an entity, as the editor holds it.
pub type EntityTransform = (u32, String, Vec3, Vec3, Vec3);

/// The sun used by the engine when a directional light has not been rotated.
pub const DEFAULT_SUN_DIRECTION: Vec3 = [0.35, -0.82, 0.45];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ColliderShape {
    Box,
    Sphere,
    Capsule,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Kind {
    Directional {
        direction: Vec3,
    },
    Point {
        radius: f32,
    },
    Spot {
        radius: f32,
        /// Full cone angle in degrees.
        angle: f32,
        direction: Vec3,
    },
    Camera {
        fov: f32,
        aspect: f32,
        near: f32,
        far: f32,
        axes: [Vec3; 3],
    },
    Collider {
        shape: ColliderShape,
        half_extents: Vec3,
        offset: Vec3,
        axes: [Vec3; 3],
        trigger: bool,
    },
    Empty {
        axes: [Vec3; 3],
    },
}

#[derive(Debug, Clone, PartialEq)]
pub struct Marker {
    pub id: u32,
    pub name: String,
    pub position: Vec3,
    pub scale: Vec3,
    /// Light colour, or a neutral tint for other kinds.
    pub color: [f32; 3],
    pub kind: Kind,
}

impl Marker {
    fn tint(&self) -> Color {
        let [r, g, b] = self.color;
        // Keep very dark light colours readable over a dark viewport.
        let lift = (0.35 - r.max(g).max(b)).max(0.0);
        Color {
            r: r + lift,
            g: g + lift,
            b: b + lift,
            a: 1.0,
        }
    }

    /// Whether the icon is drawn for every entity (collider wires only for the selection).
    fn has_icon(&self) -> bool {
        !matches!(self.kind, Kind::Collider { .. })
    }
}

#[derive(Debug, Clone)]
pub enum Message {
    Select(u32),
}

pub struct View<'a> {
    pub camera: &'a Camera,
    pub viewport: Size,
    pub markers: &'a [Marker],
    pub selected: Option<u32>,
}

pub fn view<'a>(view: View<'a>) -> Element<'a, Message> {
    iced::widget::container(
        iced::widget::canvas(Program {
            camera: *view.camera,
            viewport: view.viewport,
            markers: view.markers.to_vec(),
            selected: view.selected,
        })
        .width(Length::Fill)
        .height(Length::Fill),
    )
    .id(crate::uikit::id::VIEWPORT_MARKERS)
    .width(Length::Fill)
    .height(Length::Fill)
    .into()
}

// ----------------------------------------------------------------- collection

fn field_f32(values: &[FieldValue], index: usize) -> Option<f32> {
    match values.get(index)? {
        FieldValue::F32(value) => Some(*value),
        _ => None,
    }
}

fn field_vec3(values: &[FieldValue], index: usize) -> Option<Vec3> {
    match values.get(index)? {
        FieldValue::Vec3(value) => Some(*value),
        _ => None,
    }
}

fn is_unrotated(rotation: Vec3) -> bool {
    rotation.iter().all(|angle| angle.abs() < 1e-3)
}

/// Builds the markers of a scene from the engine's component data.
///
/// `entities` supplies the transform (already read by the editor); components are looked up by
/// name, in the order the C ABI declares their fields.
pub fn collect(
    engine: &Engine,
    entities: &[EntityTransform],
    world_position: impl Fn(u32) -> Option<Vec3>,
) -> Vec<Marker> {
    let mut markers = Vec::new();
    for (id, name, position, rotation, scale) in entities.iter().take(2000) {
        let components = engine.entity_component_data(*id);
        let find = |wanted: &str| components.iter().find(|c| c.info.name == wanted);
        let position = world_position(*id).unwrap_or(*position);
        let axes = euler_axes(*rotation);
        let light_color = find("Light")
            .and_then(|light| match light.values.first() {
                Some(FieldValue::Color(c)) => Some([c[0], c[1], c[2]]),
                _ => None,
            })
            .unwrap_or([1.0, 0.92, 0.55]);
        let make = |kind: Kind, color: [f32; 3]| Marker {
            id: *id,
            name: name.clone(),
            position,
            scale: *scale,
            color,
            kind,
        };

        if find("Directional Light").is_some() {
            let direction = if is_unrotated(*rotation) {
                vm::normalize(DEFAULT_SUN_DIRECTION)
            } else {
                forward_from_euler(*rotation)
            };
            markers.push(make(Kind::Directional { direction }, light_color));
        } else if let Some(spot) = find("Spot Light") {
            markers.push(make(
                Kind::Spot {
                    radius: field_f32(&spot.values, 0).unwrap_or(10.0),
                    angle: field_f32(&spot.values, 1).unwrap_or(45.0),
                    direction: forward_from_euler(*rotation),
                },
                light_color,
            ));
        } else if let Some(point) = find("Point Light") {
            markers.push(make(
                Kind::Point {
                    radius: field_f32(&point.values, 0).unwrap_or(10.0),
                },
                light_color,
            ));
        } else if let Some(camera) = find("Camera 3D") {
            markers.push(make(
                Kind::Camera {
                    fov: field_f32(&camera.values, 0).unwrap_or(60.0),
                    near: field_f32(&camera.values, 1).unwrap_or(0.1),
                    far: field_f32(&camera.values, 2).unwrap_or(1000.0),
                    aspect: field_f32(&camera.values, 3).unwrap_or(16.0 / 9.0),
                    axes,
                },
                [0.75, 0.85, 1.0],
            ));
        } else if find("Mesh Renderer").is_none() && find("Mesh Filter").is_none() {
            markers.push(make(Kind::Empty { axes }, [0.8, 0.8, 0.85]));
        }

        if let Some(collider) = find("Collider 3D") {
            let shape = match collider.values.first() {
                Some(FieldValue::Enum(1)) => ColliderShape::Sphere,
                Some(FieldValue::Enum(2)) => ColliderShape::Capsule,
                _ => ColliderShape::Box,
            };
            let trigger = matches!(collider.values.get(3), Some(FieldValue::Bool(true)));
            markers.push(make(
                Kind::Collider {
                    shape,
                    half_extents: field_vec3(&collider.values, 1).unwrap_or([0.5; 3]),
                    offset: field_vec3(&collider.values, 2).unwrap_or([0.0; 3]),
                    axes,
                    trigger,
                },
                if trigger {
                    [1.0, 0.7, 0.25]
                } else {
                    [0.45, 0.95, 0.55]
                },
            ));
        }
    }
    markers
}

// -------------------------------------------------------------------- drawing

struct Program {
    camera: Camera,
    viewport: Size,
    markers: Vec<Marker>,
    selected: Option<u32>,
}

#[derive(Default)]
struct State {
    hover: Option<u32>,
}

const ICON_RADIUS: f32 = 13.0;
/// World size of the camera gizmo (metres): frustum depth and half the body size.
const CAMERA_GIZMO_DEPTH: f32 = 2.6;
const CAMERA_BODY_HALF: f32 = 0.16;

impl Program {
    fn basis(&self) -> Option<Basis> {
        Basis::new(&self.camera, self.viewport)
    }

    /// The topmost marker icon under a screen point.
    fn pick(&self, point: Point) -> Option<u32> {
        let basis = self.basis()?;
        let mut best: Option<(f32, u32)> = None;
        for marker in self.markers.iter().filter(|marker| marker.has_icon()) {
            let Some(screen) = basis.project(marker.position) else {
                continue;
            };
            let d = point - screen;
            let distance = (d.x * d.x + d.y * d.y).sqrt();
            if distance <= ICON_RADIUS && best.is_none_or(|(nearest, _)| distance < nearest) {
                best = Some((distance, marker.id));
            }
        }
        best.map(|(_, id)| id)
    }
}

/// A line between two world points, skipped when either end is behind the camera.
fn world_line(frame: &mut Frame, basis: &Basis, a: Vec3, b: Vec3, color: Color, width: f32) {
    if let (Some(pa), Some(pb)) = (basis.project(a), basis.project(b)) {
        frame.stroke(
            &Path::line(pa, pb),
            Stroke::default()
                .with_color(color)
                .with_width(width)
                .with_line_cap(LineCap::Round),
        );
    }
}

fn world_polyline(frame: &mut Frame, basis: &Basis, points: &[Vec3], color: Color, width: f32) {
    let projected: Vec<Point> = points.iter().filter_map(|p| basis.project(*p)).collect();
    if projected.len() < 2 || projected.len() != points.len() {
        return;
    }
    let path = Path::new(|builder| {
        builder.move_to(projected[0]);
        for point in &projected[1..] {
            builder.line_to(*point);
        }
    });
    frame.stroke(
        &path,
        Stroke::default()
            .with_color(color)
            .with_width(width)
            .with_line_cap(LineCap::Round),
    );
}

fn wire_sphere(
    frame: &mut Frame,
    basis: &Basis,
    center: Vec3,
    radius: f32,
    color: Color,
    width: f32,
) {
    use super::gizmo::Axis;
    for axis in [Axis::X, Axis::Y, Axis::Z] {
        world_polyline(
            frame,
            basis,
            &ring_world(center, axis, radius, 48),
            color,
            width,
        );
    }
}

fn draw_marker(frame: &mut Frame, basis: &Basis, marker: &Marker, selected: bool, hovered: bool) {
    let Some(screen) = basis.project(marker.position) else {
        return;
    };
    let Some(units) = basis.units_per_pixel(marker.position) else {
        return;
    };
    let tint = marker.tint();
    let strength = if selected {
        1.0
    } else if hovered {
        0.9
    } else {
        0.7
    };
    let body = faded(tint, strength);
    let faint = faded(tint, if selected { 0.55 } else { 0.22 });
    let position = marker.position;

    // Height stem and ground ring: the depth cue that stops lights floating "flat".
    if marker.has_icon() && !matches!(marker.kind, Kind::Empty { .. }) && position[1].abs() > 0.05 {
        let ground = [position[0], 0.0, position[2]];
        world_line(
            frame,
            basis,
            position,
            ground,
            faded(theme::OVERLAY_WHITE, if selected { 0.28 } else { 0.12 }),
            1.0,
        );
        let ring: Vec<Vec3> = (0..=24)
            .map(|k| {
                let a = k as f32 / 24.0 * std::f32::consts::TAU;
                vm::add(ground, [a.cos() * units * 9.0, 0.0, a.sin() * units * 9.0])
            })
            .collect();
        world_polyline(frame, basis, &ring, faded(theme::OVERLAY_WHITE, 0.18), 1.0);
    }

    match &marker.kind {
        Kind::Directional { direction } => {
            // A bundle of parallel rays along the real shining direction.
            let length = units * 90.0;
            let helper = if direction[1].abs() < 0.9 {
                [0.0, 1.0, 0.0]
            } else {
                [1.0, 0.0, 0.0]
            };
            let u = vm::normalize(vm::cross(*direction, helper));
            let v = vm::cross(*direction, u);
            let spread = units * 15.0;
            let offsets = [[0.0, 0.0], [1.0, 0.0], [-1.0, 0.0], [0.0, 1.0], [0.0, -1.0]];
            for (index, offset) in offsets.iter().enumerate() {
                let start = vm::add(
                    position,
                    vm::add(
                        vm::scale(u, offset[0] * spread),
                        vm::scale(v, offset[1] * spread),
                    ),
                );
                let end = vm::along(
                    start,
                    *direction,
                    length * if index == 0 { 1.25 } else { 1.0 },
                );
                world_line(frame, basis, start, end, faded(tint, strength * 0.85), 1.6);
                draw_cone(
                    frame,
                    basis,
                    vm::along(end, *direction, -units * 9.0),
                    vm::along(end, *direction, units * 2.0),
                    units * 3.2,
                    faded(tint, strength),
                );
            }
            // Where the centre ray meets the ground.
            if direction[1] < -0.05 && position[1] > 0.0 {
                let t = position[1] / -direction[1];
                let hit = vm::along(position, *direction, t);
                world_line(frame, basis, position, hit, faint, 1.0);
                let cross_size = units * 7.0;
                world_line(
                    frame,
                    basis,
                    vm::add(hit, [-cross_size, 0.0, 0.0]),
                    vm::add(hit, [cross_size, 0.0, 0.0]),
                    faded(tint, 0.7),
                    1.4,
                );
                world_line(
                    frame,
                    basis,
                    vm::add(hit, [0.0, 0.0, -cross_size]),
                    vm::add(hit, [0.0, 0.0, cross_size]),
                    faded(tint, 0.7),
                    1.4,
                );
            }
            draw_sun_icon(frame, screen, body);
        }
        Kind::Point { radius } => {
            if selected {
                wire_sphere(frame, basis, position, *radius, faded(tint, 0.5), 1.2);
                // A soft shell so the volume reads as a volume.
                if let Some(edge) = basis.project(vm::along(position, basis.right, *radius)) {
                    let d = edge - screen;
                    let r = (d.x * d.x + d.y * d.y).sqrt();
                    frame.fill(&Path::circle(screen, r), faded(tint, 0.05));
                }
            }
            draw_bulb_icon(frame, screen, body);
        }
        Kind::Spot {
            radius,
            angle,
            direction,
        } => {
            // Unselected cones are capped so a long range does not flood the viewport.
            let range = (*radius).min(if selected { 40.0 } else { 6.0 });
            let half = (angle.clamp(1.0, 179.0) * 0.5).to_radians();
            let base_radius = range * half.tan().min(8.0);
            let helper = if direction[1].abs() < 0.9 {
                [0.0, 1.0, 0.0]
            } else {
                [1.0, 0.0, 0.0]
            };
            let u = vm::normalize(vm::cross(*direction, helper));
            let v = vm::cross(*direction, u);
            let center = vm::along(position, *direction, range);
            let ring: Vec<Vec3> = (0..=40)
                .map(|k| {
                    let a = k as f32 / 40.0 * std::f32::consts::TAU;
                    vm::add(
                        center,
                        vm::add(
                            vm::scale(u, a.cos() * base_radius),
                            vm::scale(v, a.sin() * base_radius),
                        ),
                    )
                })
                .collect();
            let wire = faded(tint, if selected { 0.75 } else { 0.32 });
            world_polyline(frame, basis, &ring, wire, 1.3);
            for k in (0..40).step_by(10) {
                world_line(frame, basis, position, ring[k], wire, 1.1);
            }
            world_line(frame, basis, position, center, faded(tint, 0.4), 1.0);
            if selected {
                let projected: Vec<Point> = ring.iter().filter_map(|p| basis.project(*p)).collect();
                if projected.len() == ring.len() {
                    frame.fill(&polygon(&projected), faded(tint, 0.07));
                }
            }
            draw_bulb_icon(frame, screen, body);
        }
        Kind::Camera {
            fov,
            aspect,
            near,
            far,
            axes,
        } => {
            let forward = vm::scale(axes[2], -1.0);
            let (right, up) = (axes[0], axes[1]);
            // The camera gizmo has a FIXED world size (it does not grow or shrink as you fly
            // around it, like the object it stands for); only its proportions follow the lens.
            let reach = CAMERA_GIZMO_DEPTH;
            let far_d = reach;
            let near_d = (reach * (near / far.max(*near + 1e-3)).clamp(0.04, 0.5)).min(far_d * 0.5);
            let tan = (fov.clamp(5.0, 170.0) * 0.5).to_radians().tan();
            let corners = |distance: f32| -> [Vec3; 4] {
                let h = distance * tan;
                let w = h * aspect;
                let center = vm::along(position, forward, distance);
                [
                    vm::add(center, vm::add(vm::scale(right, -w), vm::scale(up, h))),
                    vm::add(center, vm::add(vm::scale(right, w), vm::scale(up, h))),
                    vm::add(center, vm::add(vm::scale(right, w), vm::scale(up, -h))),
                    vm::add(center, vm::add(vm::scale(right, -w), vm::scale(up, -h))),
                ]
            };
            let far_c = corners(far_d);
            let near_c = corners(near_d);
            let wire = faded(tint, if selected { 0.95 } else { 0.6 });
            for corner in far_c {
                world_line(frame, basis, position, corner, wire, 1.2);
            }
            for rect in [far_c, near_c] {
                let mut closed = rect.to_vec();
                closed.push(rect[0]);
                world_polyline(frame, basis, &closed, wire, 1.3);
            }
            let projected: Vec<Point> = far_c.iter().filter_map(|p| basis.project(*p)).collect();
            if projected.len() == 4 {
                frame.fill(
                    &polygon(&projected),
                    faded(tint, if selected { 0.1 } else { 0.05 }),
                );
            }
            // "Up" triangle above the top edge, like the engine editors.
            let top_mid = vm::scale(vm::add(far_c[0], far_c[1]), 0.5);
            let lift = vm::scale(up, CAMERA_GIZMO_DEPTH * 0.16);
            let apex = vm::add(top_mid, lift);
            let tri: Vec<Point> = [far_c[0], far_c[1], apex]
                .iter()
                .filter_map(|p| basis.project(*p))
                .collect();
            if tri.len() == 3 {
                frame.fill(&polygon(&tri), faded(tint, 0.7));
            }
            // Camera body (fixed world size).
            draw_box(
                frame,
                basis,
                position,
                [right, up, axes[2]],
                CAMERA_BODY_HALF,
                faded(tint, 0.95),
            );
            draw_cone(
                frame,
                basis,
                vm::along(position, forward, CAMERA_BODY_HALF),
                vm::along(position, forward, CAMERA_BODY_HALF * 2.2),
                CAMERA_BODY_HALF * 0.7,
                shade(faded(tint, 0.95), 0.8),
            );
        }
        Kind::Collider {
            shape,
            half_extents,
            offset,
            axes,
            trigger,
        } => {
            if !selected {
                return;
            }
            let center = vm::add(
                position,
                vm::add(
                    vm::add(
                        vm::scale(axes[0], offset[0] * marker.scale[0]),
                        vm::scale(axes[1], offset[1] * marker.scale[1]),
                    ),
                    vm::scale(axes[2], offset[2] * marker.scale[2]),
                ),
            );
            let half = [
                half_extents[0] * marker.scale[0].abs(),
                half_extents[1] * marker.scale[1].abs(),
                half_extents[2] * marker.scale[2].abs(),
            ];
            let wire = faded(tint, 0.95);
            match shape {
                ColliderShape::Box => {
                    let corner = |sx: f32, sy: f32, sz: f32| {
                        vm::add(
                            center,
                            vm::add(
                                vm::add(
                                    vm::scale(axes[0], sx * half[0]),
                                    vm::scale(axes[1], sy * half[1]),
                                ),
                                vm::scale(axes[2], sz * half[2]),
                            ),
                        )
                    };
                    let c = [
                        corner(-1.0, -1.0, -1.0),
                        corner(1.0, -1.0, -1.0),
                        corner(1.0, 1.0, -1.0),
                        corner(-1.0, 1.0, -1.0),
                        corner(-1.0, -1.0, 1.0),
                        corner(1.0, -1.0, 1.0),
                        corner(1.0, 1.0, 1.0),
                        corner(-1.0, 1.0, 1.0),
                    ];
                    for (a, b) in [
                        (0, 1),
                        (1, 2),
                        (2, 3),
                        (3, 0),
                        (4, 5),
                        (5, 6),
                        (6, 7),
                        (7, 4),
                        (0, 4),
                        (1, 5),
                        (2, 6),
                        (3, 7),
                    ] {
                        world_line(frame, basis, c[a], c[b], wire, 1.5);
                    }
                }
                ColliderShape::Sphere => {
                    let radius = half[0].max(half[1]).max(half[2]);
                    wire_sphere(frame, basis, center, radius, wire, 1.4);
                }
                ColliderShape::Capsule => {
                    let radius = half[0].max(half[2]);
                    let half_height = (half[1] - radius).max(0.0);
                    for sign in [-1.0f32, 1.0] {
                        let cap = vm::along(center, axes[1], sign * half_height);
                        let ring: Vec<Vec3> = (0..=32)
                            .map(|k| {
                                let a = k as f32 / 32.0 * std::f32::consts::TAU;
                                vm::add(
                                    cap,
                                    vm::add(
                                        vm::scale(axes[0], a.cos() * radius),
                                        vm::scale(axes[2], a.sin() * radius),
                                    ),
                                )
                            })
                            .collect();
                        world_polyline(frame, basis, &ring, wire, 1.4);
                        // Half circles for the rounded ends.
                        for plane in [axes[0], axes[2]] {
                            let arc: Vec<Vec3> = (0..=16)
                                .map(|k| {
                                    let a = k as f32 / 16.0 * std::f32::consts::PI;
                                    vm::add(
                                        cap,
                                        vm::add(
                                            vm::scale(plane, a.cos() * radius),
                                            vm::scale(axes[1], sign * a.sin() * radius),
                                        ),
                                    )
                                })
                                .collect();
                            world_polyline(frame, basis, &arc, wire, 1.4);
                        }
                    }
                    for side in [axes[0], axes[2]] {
                        for sign in [-1.0f32, 1.0] {
                            world_line(
                                frame,
                                basis,
                                vm::add(
                                    vm::along(center, axes[1], half_height),
                                    vm::scale(side, sign * radius),
                                ),
                                vm::add(
                                    vm::along(center, axes[1], -half_height),
                                    vm::scale(side, sign * radius),
                                ),
                                wire,
                                1.4,
                            );
                        }
                    }
                }
            }
            if *trigger {
                frame.fill_text(Text {
                    content: "trigger".to_owned(),
                    position: screen + iced::Vector::new(0.0, 22.0),
                    color: wire,
                    size: 10.0.into(),
                    align_x: iced::alignment::Horizontal::Center.into(),
                    align_y: iced::alignment::Vertical::Center,
                    ..Text::default()
                });
            }
            return;
        }
        Kind::Empty { axes } => {
            let length = units * 16.0;
            for (axis, color) in axes
                .iter()
                .zip([theme::DANGER, theme::SUCCESS, theme::ACCENT])
            {
                world_line(
                    frame,
                    basis,
                    position,
                    vm::along(position, *axis, length),
                    faded(color, strength),
                    2.0,
                );
            }
            frame.fill(&Path::circle(screen, 2.5), faded(tint, strength));
        }
    }

    if selected || hovered {
        frame.fill_text(Text {
            content: marker.name.clone(),
            // Beside the icon, so it never sits on top of the transform gizmo's rings.
            position: screen + iced::Vector::new(ICON_RADIUS + 6.0, 0.0),
            color: if selected {
                theme::TEXT
            } else {
                theme::TEXT_MUTED
            },
            size: 11.0.into(),
            align_x: iced::alignment::Horizontal::Left.into(),
            align_y: iced::alignment::Vertical::Center,
            ..Text::default()
        });
    }
    if selected {
        frame.stroke(
            &Path::circle(screen, ICON_RADIUS + 2.0),
            Stroke::default()
                .with_color(faded(theme::ACCENT, 0.9))
                .with_width(1.5),
        );
    }
}

fn icon_plate(frame: &mut Frame, at: Point, color: Color) {
    frame.fill(&Path::circle(at, ICON_RADIUS - 1.0), theme::OVERLAY_PLATE);
    frame.stroke(
        &Path::circle(at, ICON_RADIUS - 1.0),
        Stroke::default()
            .with_color(faded(color, 0.7))
            .with_width(1.0),
    );
}

fn draw_sun_icon(frame: &mut Frame, at: Point, color: Color) {
    icon_plate(frame, at, color);
    frame.fill(&Path::circle(at, 4.2), color);
    for k in 0..8 {
        let angle = k as f32 / 8.0 * std::f32::consts::TAU;
        let (s, c) = angle.sin_cos();
        frame.stroke(
            &Path::line(
                Point::new(at.x + c * 6.0, at.y + s * 6.0),
                Point::new(at.x + c * 9.0, at.y + s * 9.0),
            ),
            Stroke::default()
                .with_color(color)
                .with_width(1.4)
                .with_line_cap(LineCap::Round),
        );
    }
}

fn draw_bulb_icon(frame: &mut Frame, at: Point, color: Color) {
    icon_plate(frame, at, color);
    frame.fill(&Path::circle(Point::new(at.x, at.y - 1.5), 4.6), color);
    frame.fill(
        &Path::rectangle(Point::new(at.x - 2.4, at.y + 3.0), Size::new(4.8, 3.0)),
        shade(color, 0.7),
    );
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
        let local = cursor
            .position()
            .map(|position| Point::new(position.x - bounds.x, position.y - bounds.y));
        match event {
            canvas::Event::Mouse(mouse::Event::CursorMoved { .. }) => {
                let hover = local
                    .filter(|_| cursor.is_over(bounds))
                    .and_then(|point| self.pick(point));
                if hover != state.hover {
                    state.hover = hover;
                    return Some(canvas::Action::request_redraw());
                }
                None
            }
            canvas::Event::Mouse(mouse::Event::ButtonPressed(mouse::Button::Left)) => {
                if !cursor.is_over(bounds) {
                    return None;
                }
                let id = self.pick(local?)?;
                Some(canvas::Action::publish(Message::Select(id)).and_capture())
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
        let Some(basis) = self.basis() else {
            return Vec::new();
        };
        if self.markers.is_empty() {
            return Vec::new();
        }
        let mut frame = Frame::new(renderer, bounds.size());
        // Far markers first so near icons sit on top.
        let mut order: Vec<&Marker> = self.markers.iter().collect();
        order.sort_by(|a, b| {
            basis
                .depth(b.position)
                .partial_cmp(&basis.depth(a.position))
                .unwrap_or(std::cmp::Ordering::Equal)
        });
        for marker in order {
            draw_marker(
                &mut frame,
                &basis,
                marker,
                self.selected == Some(marker.id),
                state.hover == Some(marker.id),
            );
        }
        vec![frame.into_geometry()]
    }

    fn mouse_interaction(
        &self,
        state: &Self::State,
        _bounds: Rectangle,
        _cursor: mouse::Cursor,
    ) -> mouse::Interaction {
        if state.hover.is_some() {
            mouse::Interaction::Pointer
        } else {
            mouse::Interaction::None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn camera() -> Camera {
        Camera {
            yaw: 0.65,
            pitch: 0.35,
            distance: 10.0,
            focus: [0.0; 3],
        }
    }

    fn marker(id: u32, position: Vec3, kind: Kind) -> Marker {
        Marker {
            id,
            name: format!("m{id}"),
            position,
            scale: [1.0; 3],
            color: [1.0, 0.9, 0.6],
            kind,
        }
    }

    fn program(markers: Vec<Marker>) -> Program {
        Program {
            camera: camera(),
            viewport: Size::new(800.0, 600.0),
            markers,
            selected: None,
        }
    }

    #[test]
    fn clicking_an_icon_picks_its_entity_and_colliders_are_not_clickable() {
        let program = program(vec![
            marker(3, [0.0, 0.0, 0.0], Kind::Point { radius: 5.0 }),
            marker(
                4,
                [0.0, 0.0, 0.0],
                Kind::Collider {
                    shape: ColliderShape::Box,
                    half_extents: [0.5; 3],
                    offset: [0.0; 3],
                    axes: euler_axes([0.0; 3]),
                    trigger: false,
                },
            ),
        ]);
        let basis = program.basis().unwrap();
        let screen = basis.project([0.0; 3]).unwrap();
        assert_eq!(program.pick(screen), Some(3));
        assert_eq!(program.pick(Point::new(screen.x + 60.0, screen.y)), None);
    }

    #[test]
    fn the_nearest_icon_wins_when_two_overlap() {
        let program = program(vec![
            marker(1, [0.0, 0.0, 0.0], Kind::Point { radius: 1.0 }),
            marker(2, [0.05, 0.0, 0.0], Kind::Point { radius: 1.0 }),
        ]);
        let basis = program.basis().unwrap();
        let near_second = basis.project([0.05, 0.0, 0.0]).unwrap();
        assert_eq!(program.pick(near_second), Some(2));
    }

    #[test]
    fn an_unrotated_directional_light_uses_the_engine_default_sun() {
        let sun = vm::normalize(DEFAULT_SUN_DIRECTION);
        assert!(sun[1] < -0.5, "the default sun shines downwards");
        assert!(is_unrotated([0.0; 3]) && !is_unrotated([0.0, 10.0, 0.0]));
        // A rotated light shines along its -Z axis: a 90 degree turn about Y points along -X.
        let forward = forward_from_euler([0.0, 90.0, 0.0]);
        assert!((forward[0] + 1.0).abs() < 1e-4, "{forward:?}");
    }

    #[test]
    fn marker_tints_stay_readable_for_dark_light_colours() {
        let mut dark = marker(1, [0.0; 3], Kind::Point { radius: 1.0 });
        dark.color = [0.0, 0.0, 0.0];
        let tint = dark.tint();
        assert!(tint.r >= 0.3 && tint.g >= 0.3 && tint.b >= 0.3);
    }
}

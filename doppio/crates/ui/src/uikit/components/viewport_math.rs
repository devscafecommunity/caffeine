//! Camera maths shared by the viewport overlays (transform gizmo and scene markers).
//!
//! The overlays draw in 2D over the engine's render, so they rebuild the orbit camera's
//! projection here: a 60 degree vertical field of view looking at `focus` from `distance`.
//! Everything works on plain `[f32; 3]` arrays: no engine types, no FFI.

use doppio_engine::Camera;
use iced::{Point, Size};

pub type Vec3 = [f32; 3];

pub fn dot(a: Vec3, b: Vec3) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

pub fn cross(a: Vec3, b: Vec3) -> Vec3 {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}

pub fn add(a: Vec3, b: Vec3) -> Vec3 {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

pub fn sub(a: Vec3, b: Vec3) -> Vec3 {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

pub fn scale(a: Vec3, amount: f32) -> Vec3 {
    [a[0] * amount, a[1] * amount, a[2] * amount]
}

pub fn length(a: Vec3) -> f32 {
    dot(a, a).sqrt()
}

pub fn normalize(a: Vec3) -> Vec3 {
    let len = length(a);
    if len < 1e-9 {
        [0.0, 0.0, 1.0]
    } else {
        scale(a, 1.0 / len)
    }
}

/// `origin + direction * distance`.
pub fn along(origin: Vec3, direction: Vec3, distance: f32) -> Vec3 {
    add(origin, scale(direction, distance))
}

/// Object axes (columns of the rotation matrix) for Euler angles in degrees, composed the way the
/// engine does it: `R = Rz * Ry * Rx`.
pub fn euler_axes(rotation_degrees: Vec3) -> [Vec3; 3] {
    let (sx, cx) = rotation_degrees[0].to_radians().sin_cos();
    let (sy, cy) = rotation_degrees[1].to_radians().sin_cos();
    let (sz, cz) = rotation_degrees[2].to_radians().sin_cos();
    // Column i is R * e_i.
    let x = [cy * cz, cy * sz, -sy];
    let y = [sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy];
    let z = [cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy];
    [x, y, z]
}

/// The direction an object looks along (and a directional light shines along): its -Z axis.
pub fn forward_from_euler(rotation_degrees: Vec3) -> Vec3 {
    scale(euler_axes(rotation_degrees)[2], -1.0)
}

/// Orbit camera projection (see the module docs).
pub struct Basis {
    pub position: Vec3,
    pub right: Vec3,
    pub up: Vec3,
    pub forward: Vec3,
    pub tan_half: f32,
    pub aspect: f32,
    pub viewport: Size,
}

impl Basis {
    pub fn new(camera: &Camera, viewport: Size) -> Option<Self> {
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

    /// Depth of a point along the view direction (positive = in front of the camera).
    pub fn depth(&self, point: Vec3) -> f32 {
        dot(sub(point, self.position), self.forward)
    }

    pub fn project(&self, point: Vec3) -> Option<Point> {
        let rel = sub(point, self.position);
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

    /// World units covered by one screen pixel at `point`'s depth.
    pub fn units_per_pixel(&self, point: Vec3) -> Option<f32> {
        let depth = self.depth(point);
        (depth > 0.05).then(|| 2.0 * depth * self.tan_half / self.viewport.height)
    }

    /// The world-space ray through a screen point: `(origin, unit direction)`.
    pub fn ray(&self, screen: Point) -> (Vec3, Vec3) {
        let ndc_x = screen.x / self.viewport.width * 2.0 - 1.0;
        let ndc_y = 1.0 - screen.y / self.viewport.height * 2.0;
        let direction = normalize(add(
            add(
                self.forward,
                scale(self.right, ndc_x * self.tan_half * self.aspect),
            ),
            scale(self.up, ndc_y * self.tan_half),
        ));
        (self.position, direction)
    }
}

/// Where a ray meets the plane through `point` with the given normal.
pub fn ray_plane(origin: Vec3, direction: Vec3, point: Vec3, normal: Vec3) -> Option<Vec3> {
    let denominator = dot(direction, normal);
    if denominator.abs() < 1e-6 {
        return None;
    }
    let t = dot(sub(point, origin), normal) / denominator;
    (t > 0.0).then(|| along(origin, direction, t))
}

/// Distance from a screen point to the segment `a`-`b`.
pub fn point_segment_distance(point: Point, a: Point, b: Point) -> f32 {
    let ab = b - a;
    let length_sq = ab.x * ab.x + ab.y * ab.y;
    if length_sq <= 1e-6 {
        let d = point - a;
        return (d.x * d.x + d.y * d.y).sqrt();
    }
    let ap = point - a;
    let t = ((ap.x * ab.x + ap.y * ab.y) / length_sq).clamp(0.0, 1.0);
    let d = point - Point::new(a.x + ab.x * t, a.y + ab.y * t);
    (d.x * d.x + d.y * d.y).sqrt()
}

/// Whether a screen point lies inside a convex or concave polygon (even-odd rule).
pub fn point_in_polygon(point: Point, polygon: &[Point]) -> bool {
    let mut inside = false;
    let mut j = polygon.len().saturating_sub(1);
    for i in 0..polygon.len() {
        let (pi, pj) = (polygon[i], polygon[j]);
        if (pi.y > point.y) != (pj.y > point.y)
            && point.x < (pj.x - pi.x) * (point.y - pi.y) / (pj.y - pi.y + f32::EPSILON) + pi.x
        {
            inside = !inside;
        }
        j = i;
    }
    inside
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

    #[test]
    fn euler_axes_follow_the_engine_convention() {
        let identity = euler_axes([0.0; 3]);
        assert_eq!(identity[0], [1.0, 0.0, 0.0]);
        assert_eq!(identity[2], [0.0, 0.0, 1.0]);
        // 90 degrees about Y turns +Z into +X and +X into -Z.
        let turned = euler_axes([0.0, 90.0, 0.0]);
        assert!((turned[2][0] - 1.0).abs() < 1e-5);
        assert!((turned[0][2] + 1.0).abs() < 1e-5);
        // A default light looks down -Z.
        assert_eq!(forward_from_euler([0.0; 3]), [-0.0, -0.0, -1.0]);
    }

    #[test]
    fn rays_go_through_the_point_that_projects_to_them() {
        let basis = Basis::new(&camera(), Size::new(800.0, 600.0)).expect("basis");
        let world = [1.5, 0.5, -0.5];
        let screen = basis.project(world).expect("visible");
        let (origin, direction) = basis.ray(screen);
        let t = dot(sub(world, origin), direction);
        let hit = along(origin, direction, t);
        assert!(length(sub(hit, world)) < 1e-3);
    }

    #[test]
    fn ray_plane_and_polygon_tests() {
        let hit = ray_plane([0.0, 5.0, 0.0], [0.0, -1.0, 0.0], [0.0; 3], [0.0, 1.0, 0.0]).unwrap();
        assert!(length(hit) < 1e-6);
        assert!(ray_plane([0.0, 5.0, 0.0], [1.0, 0.0, 0.0], [0.0; 3], [0.0, 1.0, 0.0]).is_none());
        let square = [
            Point::new(0.0, 0.0),
            Point::new(10.0, 0.0),
            Point::new(10.0, 10.0),
            Point::new(0.0, 10.0),
        ];
        assert!(point_in_polygon(Point::new(5.0, 5.0), &square));
        assert!(!point_in_polygon(Point::new(15.0, 5.0), &square));
    }
}

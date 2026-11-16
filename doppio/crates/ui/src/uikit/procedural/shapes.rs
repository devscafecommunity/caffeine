//! Gradients and basic shapes. Shapes repeat on an integer cell grid, so they
//! tile when they stay inside their cell.

use super::*;

const TAU: f32 = std::f32::consts::TAU;

const GRAD_P: &[ParamSpec] = &[
    float("angle", "Angle", 0.0, 360.0, 0.0, 1.0),
    float("scale", "Scale", 0.1, 8.0, 1.0, 0.05),
    float("offset", "Offset", -1.0, 1.0, 0.0, 0.01),
    choice("mode", "Mode", &["Ramp", "Mirror", "Repeat"], 0),
];

fn eval_linear(a: &Args) -> Vec<Pixels> {
    let (angle, scale, offset, mode) = (a.f(0).to_radians(), a.f(1), a.f(2), a.i(3));
    let (c, s) = (angle.cos(), angle.sin());
    let values = par_scalar(a.res, |_, _, u, v| {
        let t = ((u - 0.5) * c + (v - 0.5) * s) * scale + 0.5 + offset;
        match mode {
            0 => clamp01(t),
            1 => 1.0 - (fract(t * 0.5) * 2.0 - 1.0).abs(),
            _ => fract(t),
        }
    });
    pattern_outputs(a, &values)
}

const RADIAL_P: &[ParamSpec] = &[
    float("cx", "Center X", 0.0, 1.0, 0.5, 0.01),
    float("cy", "Center Y", 0.0, 1.0, 0.5, 0.01),
    float("radius", "Radius", 0.05, 2.0, 0.5, 0.01),
    float("power", "Falloff", 0.2, 6.0, 1.0, 0.05),
    boolean("invert", "Invert", false),
];

fn eval_radial(a: &Args) -> Vec<Pixels> {
    let (cx, cy, radius, power, invert) = (a.f(0), a.f(1), a.f(2), a.f(3), a.b(4));
    let values = par_scalar(a.res, |_, _, u, v| {
        let d = ((u - cx).powi(2) + (v - cy).powi(2)).sqrt() / radius;
        let t = clamp01(1.0 - d).powf(power);
        if invert {
            1.0 - t
        } else {
            t
        }
    });
    pattern_outputs(a, &values)
}

const ANGULAR_P: &[ParamSpec] = &[
    float("cx", "Center X", 0.0, 1.0, 0.5, 0.01),
    float("cy", "Center Y", 0.0, 1.0, 0.5, 0.01),
    float("rotation", "Rotation", 0.0, 360.0, 0.0, 1.0),
    int("repeat", "Repeat", 1, 16, 1),
];

fn eval_angular(a: &Args) -> Vec<Pixels> {
    let (cx, cy, rot, repeat) = (a.f(0), a.f(1), a.f(2).to_radians(), a.i(3));
    let values = par_scalar(a.res, |_, _, u, v| {
        let t = ((v - cy).atan2(u - cx) + rot) / TAU + 0.5;
        fract(t * repeat as f32)
    });
    pattern_outputs(a, &values)
}

fn eval_diamond_gradient(a: &Args) -> Vec<Pixels> {
    let (cx, cy, radius, power, invert) = (a.f(0), a.f(1), a.f(2), a.f(3), a.b(4));
    let values = par_scalar(a.res, |_, _, u, v| {
        let d = ((u - cx).abs() + (v - cy).abs()) / radius;
        let t = clamp01(1.0 - d).powf(power);
        if invert {
            1.0 - t
        } else {
            t
        }
    });
    pattern_outputs(a, &values)
}

const SHAPE_COMMON: usize = 0;

const CIRCLE_P: &[ParamSpec] = &[
    int("cells", "Repeat", 1, 16, 1),
    float("radius", "Radius", 0.02, 0.5, 0.35, 0.005),
    float("softness", "Softness", 0.0, 0.5, 0.01, 0.005),
];
const RECT_P: &[ParamSpec] = &[
    int("cells", "Repeat", 1, 16, 1),
    float("width", "Width", 0.02, 1.0, 0.7, 0.01),
    float("height", "Height", 0.02, 1.0, 0.5, 0.01),
    float("round", "Corner radius", 0.0, 0.5, 0.05, 0.005),
    float("softness", "Softness", 0.0, 0.5, 0.01, 0.005),
];
const POLY_P: &[ParamSpec] = &[
    int("cells", "Repeat", 1, 16, 1),
    int("sides", "Sides", 3, 12, 6),
    float("radius", "Radius", 0.02, 0.5, 0.38, 0.005),
    float("rotation", "Rotation", 0.0, 360.0, 0.0, 1.0),
    float("softness", "Softness", 0.0, 0.5, 0.01, 0.005),
];
const STAR_P: &[ParamSpec] = &[
    int("cells", "Repeat", 1, 16, 1),
    int("points", "Points", 3, 12, 5),
    float("radius", "Outer radius", 0.05, 0.5, 0.45, 0.005),
    float("inner", "Inner ratio", 0.1, 0.95, 0.45, 0.01),
    float("rotation", "Rotation", 0.0, 360.0, 0.0, 1.0),
    float("softness", "Softness", 0.0, 0.5, 0.01, 0.005),
];

/// Local centred coordinates of the repeat cell (−0.5..0.5).
fn cell_local(u: f32, v: f32, cells: i32) -> (f32, f32) {
    let (x, y) = (u * cells as f32, v * cells as f32);
    (x - x.floor() - 0.5, y - y.floor() - 0.5)
}

fn fill_edge(distance: f32, soft: f32) -> f32 {
    1.0 - smoothstep(-soft, soft + 1e-4, distance)
}

fn eval_circle(a: &Args) -> Vec<Pixels> {
    let (cells, radius, soft) = (a.i(SHAPE_COMMON).max(1), a.f(1), a.f(2));
    let values = par_scalar(a.res, |_, _, u, v| {
        let (x, y) = cell_local(u, v, cells);
        fill_edge((x * x + y * y).sqrt() - radius, soft)
    });
    pattern_outputs(a, &values)
}

fn eval_rectangle(a: &Args) -> Vec<Pixels> {
    let (cells, w, h, round, soft) = (a.i(0).max(1), a.f(1), a.f(2), a.f(3), a.f(4));
    let values = par_scalar(a.res, |_, _, u, v| {
        let (x, y) = cell_local(u, v, cells);
        let (qx, qy) = (x.abs() - (w * 0.5 - round), y.abs() - (h * 0.5 - round));
        let outside = (qx.max(0.0).powi(2) + qy.max(0.0).powi(2)).sqrt();
        fill_edge(outside + qx.max(qy).min(0.0) - round, soft)
    });
    pattern_outputs(a, &values)
}

fn eval_polygon(a: &Args) -> Vec<Pixels> {
    let (cells, sides, radius, rot, soft) = (
        a.i(0).max(1),
        a.i(1).max(3),
        a.f(2),
        a.f(3).to_radians(),
        a.f(4),
    );
    let sector = TAU / sides as f32;
    let values = par_scalar(a.res, |_, _, u, v| {
        let (x, y) = cell_local(u, v, cells);
        let angle = y.atan2(x) + rot;
        let r = (x * x + y * y).sqrt();
        let d = ((angle / sector + 0.5).floor() * sector - angle).cos() * r;
        fill_edge(d - radius * (sector * 0.5).cos(), soft)
    });
    pattern_outputs(a, &values)
}

fn eval_star(a: &Args) -> Vec<Pixels> {
    let (cells, points, radius, inner, rot, soft) = (
        a.i(0).max(1),
        a.i(1).max(3),
        a.f(2),
        a.f(3),
        a.f(4).to_radians(),
        a.f(5),
    );
    let sector = TAU / points as f32;
    let (ax, ay) = (radius, 0.0f32);
    let rin = radius * inner;
    let (bx, by) = (rin * (sector * 0.5).cos(), rin * (sector * 0.5).sin());
    let (ex, ey) = (bx - ax, by - ay);
    let len = (ex * ex + ey * ey).sqrt().max(1e-6);
    let values = par_scalar(a.res, |_, _, u, v| {
        let (x, y) = cell_local(u, v, cells);
        let r = (x * x + y * y).sqrt();
        let mut ang = ((y.atan2(x) + rot) % sector + sector) % sector;
        if ang > sector * 0.5 {
            ang = sector - ang;
        }
        let (px, py) = (r * ang.cos(), r * ang.sin());
        // Signed distance to the edge from the outer to the inner vertex.
        let d = ((px - ax) * ey - (py - ay) * ex) / len;
        fill_edge(d, soft)
    });
    pattern_outputs(a, &values)
}

pub static OPS: &[OpDef] = &[
    op!(
        "linear_gradient",
        "Linear Gradient",
        "Shapes",
        false,
        NO_PORTS,
        OUT_PATTERN,
        GRAD_P,
        COLORS_AB,
        eval_linear
    ),
    op!(
        "radial_gradient",
        "Radial Gradient",
        "Shapes",
        false,
        NO_PORTS,
        OUT_PATTERN,
        RADIAL_P,
        COLORS_AB,
        eval_radial
    ),
    op!(
        "angular_gradient",
        "Angular Gradient",
        "Shapes",
        false,
        NO_PORTS,
        OUT_PATTERN,
        ANGULAR_P,
        COLORS_AB,
        eval_angular
    ),
    op!(
        "diamond_gradient",
        "Diamond Gradient",
        "Shapes",
        false,
        NO_PORTS,
        OUT_PATTERN,
        RADIAL_P,
        COLORS_AB,
        eval_diamond_gradient
    ),
    op!(
        "circle",
        "Circle",
        "Shapes",
        true,
        NO_PORTS,
        OUT_PATTERN,
        CIRCLE_P,
        COLORS_AB,
        eval_circle
    ),
    op!(
        "rectangle",
        "Rectangle",
        "Shapes",
        true,
        NO_PORTS,
        OUT_PATTERN,
        RECT_P,
        COLORS_AB,
        eval_rectangle
    ),
    op!(
        "polygon",
        "Polygon",
        "Shapes",
        true,
        NO_PORTS,
        OUT_PATTERN,
        POLY_P,
        COLORS_AB,
        eval_polygon
    ),
    op!(
        "star",
        "Star",
        "Shapes",
        true,
        NO_PORTS,
        OUT_PATTERN,
        STAR_P,
        COLORS_AB,
        eval_star
    ),
];

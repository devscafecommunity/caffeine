//! Geometry and scattering: transforms, mirrors, polar, twirl, pixelate,
//! slope blur, tile sampler (splatter) and warp-by-input.

use super::*;

const TAU: f32 = std::f32::consts::TAU;
const NO_COLORS: &[ColorSpec] = &[];

const XFORM_P: &[ParamSpec] = &[
    int("tile_x", "Tile X", 1, 16, 1),
    int("tile_y", "Tile Y", 1, 16, 1),
    float("offset_x", "Offset X", -1.0, 1.0, 0.0, 0.005),
    float("offset_y", "Offset Y", -1.0, 1.0, 0.0, 0.005),
    float("rotate", "Rotate", 0.0, 360.0, 0.0, 1.0),
    float("scale", "Scale", 0.1, 8.0, 1.0, 0.02),
];
fn eval_transform(a: &Args) -> Vec<Pixels> {
    let img = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let (tx, ty, ox, oy, rot, scale) =
        (a.f(0), a.f(1), a.f(2), a.f(3), a.f(4).to_radians(), a.f(5));
    let (c, s) = (rot.cos(), rot.sin());
    let res = a.res;
    let i = &img;
    vec![par_fill(res, |_, _, u, v| {
        let (px, py) = ((u - 0.5) / scale, (v - 0.5) / scale);
        let (rx, ry) = (px * c + py * s, -px * s + py * c);
        sample_wrap(i, res, (rx + 0.5 - ox) * tx, (ry + 0.5 - oy) * ty)
    })]
}

const MIRROR_P: &[ParamSpec] = &[choice(
    "mode",
    "Axis",
    &["Horizontal", "Vertical", "Both"],
    2,
)];
fn eval_mirror(a: &Args) -> Vec<Pixels> {
    let img = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let mode = a.i(0);
    let res = a.res;
    let i = &img;
    vec![par_fill(res, |_, _, u, v| {
        let fold = |t: f32| 1.0 - (2.0 * t - 1.0).abs();
        let su = if mode == 0 || mode == 2 { fold(u) } else { u };
        let sv = if mode == 1 || mode == 2 { fold(v) } else { v };
        sample_wrap(i, res, su, sv)
    })]
}

const POLAR_P: &[ParamSpec] = &[
    choice("mode", "Mode", &["To polar", "From polar"], 0),
    float("radius", "Radius", 0.1, 1.0, 0.5, 0.01),
];
fn eval_polar(a: &Args) -> Vec<Pixels> {
    let img = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let (mode, radius) = (a.i(0), a.f(1));
    let res = a.res;
    let i = &img;
    vec![par_fill(res, |_, _, u, v| {
        if mode == 0 {
            let (ang, r) = (u * TAU, v * radius);
            sample_wrap(i, res, 0.5 + r * ang.cos(), 0.5 + r * ang.sin())
        } else {
            let (dx, dy) = (u - 0.5, v - 0.5);
            let (ang, r) = (
                dy.atan2(dx) / TAU + 0.5,
                (dx * dx + dy * dy).sqrt() / radius,
            );
            sample_wrap(i, res, ang, r.min(1.0))
        }
    })]
}

const TWIRL_P: &[ParamSpec] = &[
    float("amount", "Amount", -12.0, 12.0, 3.0, 0.05),
    float("radius", "Radius", 0.05, 0.5, 0.5, 0.01),
    float("cx", "Center X", 0.0, 1.0, 0.5, 0.01),
    float("cy", "Center Y", 0.0, 1.0, 0.5, 0.01),
];
fn eval_twirl(a: &Args) -> Vec<Pixels> {
    let img = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let (amount, radius, cx, cy) = (a.f(0), a.f(1), a.f(2), a.f(3));
    let res = a.res;
    let i = &img;
    vec![par_fill(res, |_, _, u, v| {
        let (dx, dy) = (u - cx, v - cy);
        let d = (dx * dx + dy * dy).sqrt();
        let t = clamp01(1.0 - d / radius);
        let ang = amount * t * t;
        let (c, s) = (ang.cos(), ang.sin());
        sample_wrap(i, res, cx + dx * c - dy * s, cy + dx * s + dy * c)
    })]
}

const PIXEL_P: &[ParamSpec] = &[int("cells", "Cells", 2, 256, 32)];
fn eval_pixelate(a: &Args) -> Vec<Pixels> {
    let img = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let cells = a.i(0).max(2);
    let res = a.res;
    let i = &img;
    vec![par_fill(res, |_, _, u, v| {
        let cu = ((u * cells as f32).floor() + 0.5) / cells as f32;
        let cv = ((v * cells as f32).floor() + 0.5) / cells as f32;
        sample_wrap(i, res, cu, cv)
    })]
}

const SLOPE_IN: &[PortDef] = &[port("Input"), scalar_port("Slope")];
const SLOPE_P: &[ParamSpec] = &[
    float("intensity", "Intensity", 0.0, 0.2, 0.03, 0.002),
    int("samples", "Samples", 2, 48, 16),
    choice("mode", "Mode", &["Blur", "Min", "Max"], 0),
];
fn eval_slope_blur(a: &Args) -> Vec<Pixels> {
    let img = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let slope = a.scalar(1, 0.5);
    let (intensity, samples, mode) = (a.f(0), a.i(1).max(2), a.i(2));
    let res = a.res;
    let mut gmax = 1e-6f32;
    let grads: Vec<(f32, f32)> = (0..res * res)
        .map(|k| {
            let (x, y) = ((k % res) as i32, (k / res) as i32);
            let gx = at_wrap(&slope, res, x + 1, y) - at_wrap(&slope, res, x - 1, y);
            let gy = at_wrap(&slope, res, x, y + 1) - at_wrap(&slope, res, x, y - 1);
            gmax = gmax.max((gx * gx + gy * gy).sqrt());
            (gx, gy)
        })
        .collect();
    let (i, g) = (&img, &grads);
    vec![par_fill(res, |x, y, u, v| {
        let (mut pu, mut pv) = (u, v);
        let mut acc = [0.0f32; 4];
        let mut best = i[y * res + x];
        for s in 0..samples {
            let p = sample_wrap(i, res, pu, pv);
            match mode {
                0 => {
                    for c in 0..4 {
                        acc[c] += p[c];
                    }
                }
                1 => {
                    if luma(p) < luma(best) || s == 0 {
                        best = p;
                    }
                }
                _ => {
                    if luma(p) > luma(best) || s == 0 {
                        best = p;
                    }
                }
            }
            let (gx, gy) = g[wrap_i((pv * res as f32) as i32, res as i32) as usize * res
                + wrap_i((pu * res as f32) as i32, res as i32) as usize];
            pu += gx / gmax * intensity / samples as f32 * 2.0;
            pv += gy / gmax * intensity / samples as f32 * 2.0;
        }
        if mode == 0 {
            let n = samples as f32;
            [acc[0] / n, acc[1] / n, acc[2] / n, acc[3] / n]
        } else {
            best
        }
    })]
}

const SPLAT_IN: &[PortDef] = &[port("Shape")];
const SPLAT_OUT: &[PortDef] = &[
    port("Color"),
    scalar_port("Value"),
    scalar_port("Variation"),
];
const SPLAT_P: &[ParamSpec] = &[
    int("count", "Grid", 1, 32, 8),
    float("jitter", "Jitter", 0.0, 1.0, 0.8, 0.01),
    float("scale_min", "Scale min", 0.1, 3.0, 0.7, 0.01),
    float("scale_max", "Scale max", 0.1, 3.0, 1.1, 0.01),
    float("rotation", "Random rotation", 0.0, 360.0, 360.0, 1.0),
    float("brightness", "Brightness variation", 0.0, 1.0, 0.4, 0.01),
    float("density", "Density", 0.0, 1.0, 0.9, 0.01),
    int("seed", "Seed", 0, 9999, 1),
    choice("blend", "Overlap", &["Max", "Add"], 0),
];
fn eval_tile_sampler(a: &Args) -> Vec<Pixels> {
    let shape = a.image(0, [1.0, 1.0, 1.0, 1.0]);
    let (count, jitter, smin, smax, rot, bvar, density, seed, blend) = (
        a.i(0).max(1),
        a.f(1),
        a.f(2),
        a.f(3).max(a.f(2)),
        a.f(4).to_radians(),
        a.f(5),
        a.f(6),
        a.i(7),
        a.i(8),
    );
    let res = a.res;
    let sh = &shape;
    let cells = par_fill(res, |_, _, u, v| {
        let (x, y) = (u * count as f32, v * count as f32);
        let (ix, iy) = (x.floor() as i32, y.floor() as i32);
        let mut best = [0.0f32; 4];
        let mut best_val = 0.0f32;
        let mut best_id = 0.0f32;
        let mut sum = [0.0f32; 4];
        for oy in -2..=2 {
            for ox in -2..=2 {
                let (cx, cy) = (ix + ox, iy + oy);
                let (wx, wy) = (wrap_i(cx, count), wrap_i(cy, count));
                if rand01(wx, wy, seed + 17) > density {
                    continue;
                }
                let (jx, jy) = rand2(wx, wy, seed);
                let s = smin + (smax - smin) * rand01(wx, wy, seed + 3);
                let ang = (rand01(wx, wy, seed + 5) - 0.5) * rot;
                let (c, sn) = (ang.cos(), ang.sin());
                let (px, py) = (
                    x - (cx as f32 + 0.5 + (jx - 0.5) * jitter),
                    y - (cy as f32 + 0.5 + (jy - 0.5) * jitter),
                );
                let (lx, ly) = ((px * c + py * sn) / s, (-px * sn + py * c) / s);
                let sample = sample_clip(sh, res, lx + 0.5, ly + 0.5);
                let b = 1.0 - bvar * rand01(wx, wy, seed + 9);
                let col = [sample[0] * b, sample[1] * b, sample[2] * b, 1.0];
                let val = luma(sample) * b;
                if blend == 1 {
                    for k in 0..3 {
                        sum[k] += col[k];
                    }
                }
                if val > best_val {
                    best_val = val;
                    best = col;
                    best_id = rand01(wx, wy, seed + 13);
                }
            }
        }
        if blend == 1 {
            [clamp01(sum[0]), clamp01(sum[1]), clamp01(sum[2]), best_id]
        } else {
            [best[0], best[1], best[2], best_id]
        }
    });
    let color: Pixels = cells.iter().map(|p| [p[0], p[1], p[2], 1.0]).collect();
    let value: Pixels = cells
        .iter()
        .map(|p| gray(luma([p[0], p[1], p[2], 1.0])))
        .collect();
    let id: Pixels = cells.iter().map(|p| gray(p[3])).collect();
    vec![color, value, id]
}

const WARP_IN: &[PortDef] = &[port("Input"), port("Warp")];
const WARP_P: &[ParamSpec] = &[
    float("intensity", "Intensity", 0.0, 0.5, 0.05, 0.002),
    float("angle", "Angle", 0.0, 360.0, 0.0, 1.0),
    choice(
        "mode",
        "Mode",
        &["Directional", "Vector (RG)", "Gradient"],
        0,
    ),
];
fn eval_warp_input(a: &Args) -> Vec<Pixels> {
    let img = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let warp = a.image(1, [0.5, 0.5, 0.5, 1.0]);
    let (intensity, angle, mode) = (a.f(0), a.f(1).to_radians(), a.i(2));
    let res = a.res;
    let lum: Vec<f32> = warp.iter().map(|p| luma(*p)).collect();
    let (i, w, l) = (&img, &warp, &lum);
    vec![par_fill(res, |x, y, u, v| {
        let (dx, dy) = match mode {
            0 => {
                let d = (l[y * res + x] - 0.5) * 2.0 * intensity;
                (angle.cos() * d, angle.sin() * d)
            }
            1 => {
                let p = w[y * res + x];
                (
                    (p[0] - 0.5) * 2.0 * intensity,
                    (p[1] - 0.5) * 2.0 * intensity,
                )
            }
            _ => {
                let (xi, yi) = (x as i32, y as i32);
                (
                    (at_wrap(l, res, xi + 1, yi) - at_wrap(l, res, xi - 1, yi)) * intensity * 4.0,
                    (at_wrap(l, res, xi, yi + 1) - at_wrap(l, res, xi, yi - 1)) * intensity * 4.0,
                )
            }
        };
        sample_wrap(i, res, u + dx, v + dy)
    })]
}

pub static OPS: &[OpDef] = &[
    op!(
        "transform_2d",
        "Transform 2D",
        "Geometry",
        true,
        IN_IMAGE,
        OUT_COLOR,
        XFORM_P,
        NO_COLORS,
        eval_transform
    ),
    op!(
        "mirror",
        "Mirror",
        "Geometry",
        true,
        IN_IMAGE,
        OUT_COLOR,
        MIRROR_P,
        NO_COLORS,
        eval_mirror
    ),
    op!(
        "polar",
        "Polar Coordinates",
        "Geometry",
        false,
        IN_IMAGE,
        OUT_COLOR,
        POLAR_P,
        NO_COLORS,
        eval_polar
    ),
    op!("twirl", "Twirl", "Geometry", true, IN_IMAGE, OUT_COLOR, TWIRL_P, NO_COLORS, eval_twirl),
    op!(
        "pixelate",
        "Pixelate",
        "Geometry",
        true,
        IN_IMAGE,
        OUT_COLOR,
        PIXEL_P,
        NO_COLORS,
        eval_pixelate
    ),
    op!(
        "slope_blur",
        "Slope Blur",
        "Geometry",
        true,
        SLOPE_IN,
        OUT_COLOR,
        SLOPE_P,
        NO_COLORS,
        eval_slope_blur
    ),
    op!(
        "tile_sampler",
        "Tile Sampler",
        "Geometry",
        true,
        SPLAT_IN,
        SPLAT_OUT,
        SPLAT_P,
        NO_COLORS,
        eval_tile_sampler
    ),
    op!(
        "warp_by_input",
        "Warp by Input",
        "Geometry",
        true,
        WARP_IN,
        OUT_COLOR,
        WARP_P,
        NO_COLORS,
        eval_warp_input
    ),
];

//! Shapes and effects for the Texture Generator: leaves, feathers, droplets, shock rings,
//! flames, smoke, sparks, bursts, glows and colour ramps.
//!
//! Unlike the pattern ops these are **single objects**, not tiles: they sit inside the image
//! with a transparent margin, so none of them is flagged `tileable`. Colour outputs carry the
//! object's mask in the alpha channel (the RGB is bled outside the mask so mip-maps do not
//! gain dark halos), and every op is deterministic for its seed. Effects that animate (flame,
//! smoke) expose a *Phase* parameter that loops, so a flipbook is one graph baked N times.

use super::*;

const TAU: f32 = std::f32::consts::TAU;
const PI: f32 = std::f32::consts::PI;

/// Four planes per pixel, filled in parallel.
fn planes(res: usize, f: impl Fn(f32, f32) -> [f32; 4] + Sync) -> [Vec<f32>; 4] {
    let packed = par_fill(res, |_, _, u, v| f(u, v));
    let mut out = [
        Vec::with_capacity(packed.len()),
        Vec::with_capacity(packed.len()),
        Vec::with_capacity(packed.len()),
        Vec::with_capacity(packed.len()),
    ];
    for pixel in packed {
        for (channel, plane) in out.iter_mut().enumerate() {
            plane.push(pixel[channel]);
        }
    }
    out
}

fn plane_image(plane: &[f32]) -> Pixels {
    plane.iter().map(|v| gray(clamp01(*v))).collect()
}

/// Colour image whose alpha is `mask` (RGB is kept everywhere).
fn with_alpha(rgb: Vec<[f32; 3]>, mask: &[f32]) -> Pixels {
    rgb.into_iter()
        .zip(mask.iter())
        .map(|(c, m)| [c[0], c[1], c[2], clamp01(*m)])
        .collect()
}

fn mix3(a: [f32; 4], b: [f32; 4], t: f32) -> [f32; 3] {
    let t = clamp01(t);
    [
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
    ]
}

/// Three-stop ramp (a -> b -> c) over `t` in 0..1.
fn ramp3(a: [f32; 4], b: [f32; 4], c: [f32; 4], t: f32) -> [f32; 3] {
    if t < 0.5 {
        mix3(a, b, t * 2.0)
    } else {
        mix3(b, c, (t - 0.5) * 2.0)
    }
}

fn pow_pos(v: f32, p: f32) -> f32 {
    clamp01(v).powf(p)
}

// ------------------------------------------------------------------ ports

const OUT_LEAF: &[PortDef] = &[
    port("Color"),
    scalar_port("Mask"),
    scalar_port("Veins"),
    scalar_port("Height"),
];
const OUT_FEATHER: &[PortDef] = &[port("Color"), scalar_port("Mask"), scalar_port("Barbs")];
const OUT_DROP: &[PortDef] = &[port("Color"), scalar_port("Mask"), scalar_port("Dome")];
const OUT_FLAME: &[PortDef] = &[port("Color"), scalar_port("Intensity"), scalar_port("Core")];
const OUT_FX: &[PortDef] = &[port("Color"), scalar_port("Value")];
const OUT_RAMP: &[PortDef] = &[port("Color"), scalar_port("Position")];

// ------------------------------------------------------------------- leaf

const LEAF_P: &[ParamSpec] = &[
    float("width", "Half width", 0.08, 0.48, 0.3, 0.005),
    float("tip", "Tip sharpness", 0.0, 1.0, 0.6, 0.01),
    float("base", "Base roundness", 0.0, 1.0, 0.5, 0.01),
    float("serration", "Serration", 0.0, 1.0, 0.0, 0.01),
    int("teeth", "Teeth", 4, 40, 14),
    float("bend", "Bend", -1.0, 1.0, 0.25, 0.01),
    int("veins", "Side veins", 2, 24, 9),
    float("vein_width", "Vein width", 0.02, 0.3, 0.09, 0.005),
    float("midrib", "Midrib width", 0.004, 0.05, 0.016, 0.001),
    float("sweep", "Vein sweep", 0.0, 2.0, 1.0, 0.05),
    float("texture", "Surface texture", 0.0, 1.0, 0.5, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const LEAF_C: &[ColorSpec] = &[
    color("edge", "Leaf edge", [0.30, 0.43, 0.10, 1.0]),
    color("body", "Leaf body", [0.09, 0.27, 0.05, 1.0]),
    color("vein", "Veins", [0.55, 0.66, 0.26, 1.0]),
];

fn eval_leaf(a: &Args) -> Vec<Pixels> {
    let (width, tip, base) = (a.f(0), a.f(1), a.f(2));
    let (serration, teeth, bend) = (a.f(3), a.f(4), a.f(5));
    let (veins, vein_w, midrib, sweep) = (a.f(6), a.f(7), a.f(8), a.f(9));
    let (texture, seed) = (a.f(10), a.i(11));
    let (edge, body, vein_c) = (a.color(0), a.color(1), a.color(2));
    let res = a.res;
    let px = 1.0 / res as f32;
    let p = 1.0 - 0.65 * base;
    let q = 0.7 + 1.1 * tip;
    let t_star = p / (p + q);
    let norm = t_star.powf(p) * (1.0 - t_star).powf(q);

    let [mask, vein_plane, height, _] = planes(res, |u, v| {
        let t = (0.95 - v) / 0.9;
        if t <= 0.0 || t >= 1.0 {
            return [0.0; 4];
        }
        let mut w = width * t.powf(p) * (1.0 - t).powf(q) / norm;
        w *= 1.0 - serration * 0.35 * fract(t * teeth);
        let cx = 0.5 + 0.18 * bend * t * t;
        let dx = (u - cx).abs();
        let m = smoothstep(-px * 1.2, px * 1.2, w - dx);
        if m <= 0.0 {
            return [0.0; 4];
        }
        let ax = (dx / w.max(1e-4)).clamp(0.0, 1.0);
        let rib = midrib * (1.0 - 0.65 * t);
        let mid = 1.0 - smoothstep(rib * 0.6, rib * 1.4, dx);
        let wobble = (perlin2(u, v, 8, 8, seed) - 0.5) * 0.35;
        let k = t * veins - ax * sweep + wobble;
        let f = (fract(k + 0.5) - 0.5).abs();
        let lateral = (1.0 - smoothstep(vein_w * 0.5, vein_w, f)) * (1.0 - ax * ax) * 0.9;
        let veins_total = mid.max(lateral * 0.8);
        let h = 0.55 + 0.25 * (1.0 - ax) - 0.22 * lateral + 0.3 * mid;
        [m, veins_total * m, clamp01(h) * m, 0.0]
    });

    let rgb: Vec<[f32; 3]> = (0..res * res)
        .map(|index| {
            let (x, y) = (index % res, index / res);
            let (u, v) = ((x as f32 + 0.5) * px, (y as f32 + 0.5) * px);
            let t = ((0.95 - v) / 0.9).clamp(0.0, 1.0);
            let w = (width * t.powf(p) * (1.0 - t).powf(q) / norm).max(1e-4);
            let cx = 0.5 + 0.18 * bend * t * t;
            let ax = ((u - cx).abs() / w).clamp(0.0, 1.0);
            let mut c = mix3(body, edge, ax.powf(2.2));
            let lift = vein_plane[index] * 0.8;
            for channel in 0..3 {
                c[channel] += (vein_c[channel] - c[channel]) * lift;
            }
            let n = fbm(Basis::Perlin, u, v, 6, seed + 7, 4, 0.5, 2.0);
            let shade = 1.0 + (n - 0.5) * texture * 0.7;
            [
                clamp01(c[0] * shade),
                clamp01(c[1] * shade),
                clamp01(c[2] * shade),
            ]
        })
        .collect();
    vec![
        with_alpha(rgb, &mask),
        plane_image(&mask),
        plane_image(&vein_plane),
        plane_image(&height),
    ]
}

// ---------------------------------------------------------------- feather

const FEATHER_P: &[ParamSpec] = &[
    float("width", "Half width", 0.06, 0.45, 0.2, 0.005),
    float("curve", "Vane curve", 0.3, 1.6, 0.75, 0.01),
    int("barbs", "Barbs", 20, 200, 80),
    float("slant", "Barb slant", 0.0, 2.0, 1.0, 0.05),
    float("split", "Splits", 0.0, 1.0, 0.35, 0.01),
    float("shaft", "Shaft width", 0.003, 0.03, 0.01, 0.001),
    float("bend", "Bend", -1.0, 1.0, 0.2, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const FEATHER_C: &[ColorSpec] = &[
    color("root", "Root", [0.92, 0.9, 0.84, 1.0]),
    color("tip", "Tip", [0.28, 0.4, 0.62, 1.0]),
    color("shaft", "Shaft", [0.95, 0.93, 0.85, 1.0]),
];

fn eval_feather(a: &Args) -> Vec<Pixels> {
    let (width, curve, barbs, slant) = (a.f(0), a.f(1), a.f(2), a.f(3));
    let (split, shaft, bend, seed) = (a.f(4), a.f(5), a.f(6), a.i(7));
    let (root, tipc, shaft_c) = (a.color(0), a.color(1), a.color(2));
    let res = a.res;
    let px = 1.0 / res as f32;
    let [mask, barb_plane, shade, _] = planes(res, |u, v| {
        let t = (0.96 - v) / 0.92;
        if t <= 0.0 || t >= 1.0 {
            return [0.0; 4];
        }
        let cx = 0.5 + 0.16 * bend * t * t;
        let w = width * (PI * t.powf(curve)).sin().max(0.0).powf(0.7);
        let dx = u - cx;
        let ax = (dx.abs() / w.max(1e-4)).clamp(0.0, 1.0);
        let side = if dx < 0.0 { -1 } else { 1 };
        let k = t * barbs - ax * slant * 6.0;
        let id = k.floor() as i32;
        let r = rand01(id, side, seed);
        let notch = if r < split {
            smoothstep(0.55, 0.95, ax) * (0.5 + 0.5 * rand01(id, side, seed + 5))
        } else {
            0.0
        };
        let barb = 0.5 + 0.5 * (TAU * k).cos();
        let vane = smoothstep(-px * 1.2, px * 1.2, w * (1.0 - notch) - dx.abs());
        let rib = 1.0 - smoothstep(shaft * 0.7, shaft * 1.3, dx.abs());
        let m = vane.max(rib * smoothstep(0.0, 0.03, t) * smoothstep(1.0, 0.9, t));
        [m, barb * vane, 0.85 + 0.15 * barb, rib]
    });
    let rgb: Vec<[f32; 3]> = (0..res * res)
        .map(|index| {
            let v = ((index / res) as f32 + 0.5) * px;
            let t = ((0.96 - v) / 0.92).clamp(0.0, 1.0);
            let mut c = mix3(root, tipc, smoothstep(0.1, 0.95, t));
            let s = shade[index];
            for channel in c.iter_mut() {
                *channel = clamp01(*channel * s);
            }
            // The shaft is lighter than the vane.
            let rib = (mask[index] - barb_plane[index]).max(0.0);
            let _ = rib;
            c
        })
        .collect();
    let mut color = with_alpha(rgb, &mask);
    // Paint the shaft with its own colour.
    let (cx0, _) = (0.5f32, 0.0f32);
    for (index, pixel) in color.iter_mut().enumerate() {
        let (x, y) = (index % res, index / res);
        let (u, v) = ((x as f32 + 0.5) * px, (y as f32 + 0.5) * px);
        let t = ((0.96 - v) / 0.92).clamp(0.0, 1.0);
        let cx = cx0 + 0.16 * bend * t * t;
        let rib = 1.0 - smoothstep(shaft * 0.7, shaft * 1.3, (u - cx).abs());
        for channel in 0..3 {
            pixel[channel] += (shaft_c[channel] - pixel[channel]) * rib;
        }
    }
    vec![color, plane_image(&mask), plane_image(&barb_plane)]
}

// ---------------------------------------------------------------- droplet

const DROP_P: &[ParamSpec] = &[
    float("radius", "Radius", 0.08, 0.4, 0.24, 0.005),
    float("tail", "Tail length", 0.0, 0.9, 0.5, 0.01),
    float("smooth", "Join smoothing", 0.01, 0.3, 0.12, 0.005),
    float("highlight", "Highlight", 0.0, 1.0, 0.7, 0.01),
    float("tilt", "Tilt", -1.0, 1.0, 0.0, 0.01),
];
const DROP_C: &[ColorSpec] = &[
    color("tint", "Tint", [0.66, 0.82, 0.96, 1.0]),
    color("light", "Highlight", [1.0, 1.0, 1.0, 1.0]),
];

fn smin(a: f32, b: f32, k: f32) -> f32 {
    let h = clamp01(0.5 + 0.5 * (b - a) / k);
    b + (a - b) * h - k * h * (1.0 - h)
}

fn eval_droplet(a: &Args) -> Vec<Pixels> {
    let (radius, tail, smooth, highlight, tilt) = (a.f(0), a.f(1), a.f(2), a.f(3), a.f(4));
    let (tint, light) = (a.color(0), a.color(1));
    let res = a.res;
    let px = 1.0 / res as f32;
    let centre = (0.5f32, 0.62f32);
    let tip = (0.5 + tilt * 0.18, 0.62 - tail * 0.55 - radius);
    let sdf = move |u: f32, v: f32| -> f32 {
        let circle = ((u - centre.0).powi(2) + (v - centre.1).powi(2)).sqrt() - radius;
        let (bx, by) = (centre.0 - tip.0, centre.1 - tip.1);
        let len2 = (bx * bx + by * by).max(1e-6);
        let s = clamp01(((u - tip.0) * bx + (v - tip.1) * by) / len2);
        let (qx, qy) = (tip.0 + bx * s, tip.1 + by * s);
        let cone = ((u - qx).powi(2) + (v - qy).powi(2)).sqrt() - radius * s.powf(1.2);
        smin(circle, cone, smooth)
    };
    let [mask, dome, spec, _] = planes(res, |u, v| {
        let d = sdf(u, v);
        let m = smoothstep(px * 1.2, -px * 1.2, d);
        if m <= 0.0 {
            return [0.0; 4];
        }
        let depth = clamp01(-d / (radius * 0.9));
        let h = (1.0 - (1.0 - depth).powi(2)).max(0.0).sqrt();
        // A soft window reflection on the upper left plus a rim light at the edge.
        let (hx, hy) = (
            u - (centre.0 - radius * 0.35),
            v - (centre.1 - radius * 0.4),
        );
        let window = (-(hx * hx + hy * hy) / (radius * radius * 0.05)).exp();
        let rim = (1.0 - depth).powf(3.0) * 0.4;
        [
            m,
            h * m,
            clamp01(window * highlight + rim * highlight) * m,
            0.0,
        ]
    });
    let rgb: Vec<[f32; 3]> = dome
        .iter()
        .zip(spec.iter())
        .map(|(h, s)| {
            let base = mix3(
                [tint[0] * 0.55, tint[1] * 0.55, tint[2] * 0.6, 1.0],
                tint,
                *h,
            );
            mix3([base[0], base[1], base[2], 1.0], light, *s)
        })
        .collect();
    let alpha: Vec<f32> = mask
        .iter()
        .zip(spec.iter())
        .map(|(m, s)| m * (0.45 + 0.55 * s))
        .collect();
    vec![
        with_alpha(rgb, &alpha),
        plane_image(&mask),
        plane_image(&dome),
    ]
}

// ------------------------------------------------------------- shockwave

const SHOCK_P: &[ParamSpec] = &[
    float("radius", "Radius", 0.08, 0.48, 0.32, 0.005),
    float("thickness", "Thickness", 0.01, 0.25, 0.06, 0.005),
    float("soft", "Softness", 0.0, 1.0, 0.5, 0.01),
    float("distort", "Distortion", 0.0, 1.0, 0.35, 0.01),
    int("scale", "Noise scale", 2, 24, 6),
    float("inner", "Inner glow", 0.0, 1.0, 0.35, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const SHOCK_C: &[ColorSpec] = &[
    color("outer", "Outer", [1.0, 0.45, 0.1, 1.0]),
    color("core", "Core", [1.0, 0.95, 0.75, 1.0]),
];

fn eval_shockwave(a: &Args) -> Vec<Pixels> {
    let (radius, thick, soft, distort) = (a.f(0), a.f(1), a.f(2), a.f(3));
    let (scale, inner, seed) = (a.i(4).max(1), a.f(5), a.i(6));
    let (outer, core) = (a.color(0), a.color(1));
    let res = a.res;
    let [value, ..] = planes(res, |u, v| {
        let (dx, dy) = (u - 0.5, v - 0.5);
        let r = (dx * dx + dy * dy).sqrt();
        let theta = dy.atan2(dx);
        let n = perlin2(
            0.5 + 0.5 * theta.cos() * 0.95,
            0.5 + 0.5 * theta.sin() * 0.95,
            scale,
            scale,
            seed,
        );
        let rr = r + (n - 0.5) * distort * thick * 3.0;
        let x = (rr - radius).abs() / thick;
        let ring = pow_pos(1.0 - x, 1.0 + soft * 3.0);
        let glow = if rr < radius {
            inner * 0.4 * pow_pos(1.0 - rr / radius, 2.0)
        } else {
            0.0
        };
        // Fade out before the image border so the texture never gets cut.
        let edge = smoothstep(0.5, 0.42, r);
        [clamp01(ring + glow) * edge, 0.0, 0.0, 0.0]
    });
    let rgb: Vec<[f32; 3]> = value.iter().map(|v| mix3(outer, core, *v * *v)).collect();
    vec![with_alpha(rgb, &value), plane_image(&value)]
}

// ------------------------------------------------------------------ flame

const FLAME_P: &[ParamSpec] = &[
    float("width", "Width", 0.08, 0.45, 0.22, 0.005),
    float("height", "Height", 0.4, 0.95, 0.8, 0.01),
    float("turbulence", "Turbulence", 0.0, 1.0, 0.6, 0.01),
    int("scale", "Noise scale", 2, 12, 4),
    float("stretch", "Vertical stretch", 1.0, 6.0, 2.5, 0.1),
    float("sharp", "Sharpness", 0.3, 3.0, 1.0, 0.05),
    float("phase", "Phase (loops 0..1)", 0.0, 1.0, 0.0, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const FLAME_C: &[ColorSpec] = &[
    color("outer", "Outer", [0.75, 0.08, 0.02, 1.0]),
    color("mid", "Middle", [1.0, 0.48, 0.05, 1.0]),
    color("core", "Core", [1.0, 0.95, 0.65, 1.0]),
];

fn eval_flame(a: &Args) -> Vec<Pixels> {
    let (width, height, turb, scale) = (a.f(0), a.f(1), a.f(2), a.i(3).max(1));
    let (stretch, sharp, phase, seed) = (a.f(4), a.f(5), a.f(6), a.i(7));
    let (outer, mid, core) = (a.color(0), a.color(1), a.color(2));
    let res = a.res;
    let py = ((scale as f32 / stretch).round() as i32).max(1);
    let [value, core_plane, ..] = planes(res, |u, v| {
        let h = (0.93 - v) / height;
        if !(0.0..1.0).contains(&h) {
            return [0.0; 4];
        }
        let sv = fract(v + phase);
        let n = perlin2(u, sv, scale * 2, py, seed);
        let n2 = perlin2(u, fract(sv * 1.7 + 0.31), scale * 4, py * 2, seed + 3);
        let wf = width * (1.0 - h).powf(0.8) * smoothstep(0.0, 0.18, h).max(0.02);
        let dx = u - 0.5 + (n - 0.5) * turb * 0.5 * h.sqrt();
        let ax = dx.abs() / wf.max(1e-4);
        let body = pow_pos(1.0 - ax, sharp) * (1.0 - h).powf(0.55);
        let eroded = body - (1.0 - n2) * turb * 0.45 * h;
        let val = clamp01(eroded * 1.5);
        [val, val.powf(2.6), 0.0, 0.0]
    });
    let rgb: Vec<[f32; 3]> = value.iter().map(|v| ramp3(outer, mid, core, *v)).collect();
    let alpha: Vec<f32> = value.iter().map(|v| smoothstep(0.02, 0.35, *v)).collect();
    vec![
        with_alpha(rgb, &alpha),
        plane_image(&value),
        plane_image(&core_plane),
    ]
}

// ------------------------------------------------------------------ smoke

const SMOKE_P: &[ParamSpec] = &[
    float("radius", "Radius", 0.12, 0.5, 0.34, 0.005),
    float("turbulence", "Turbulence", 0.0, 1.0, 0.7, 0.01),
    int("scale", "Noise scale", 2, 12, 4),
    float("density", "Density", 0.2, 2.0, 0.85, 0.01),
    float("soft", "Softness", 0.1, 1.0, 0.8, 0.01),
    float("phase", "Phase (loops 0..1)", 0.0, 1.0, 0.0, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const SMOKE_C: &[ColorSpec] = &[
    color("dark", "Shadow", [0.12, 0.12, 0.13, 1.0]),
    color("light", "Lit", [0.78, 0.78, 0.8, 1.0]),
];

fn eval_smoke(a: &Args) -> Vec<Pixels> {
    let (radius, turb, scale, density) = (a.f(0), a.f(1), a.i(2).max(1), a.f(3));
    let (soft, phase, seed) = (a.f(4), a.f(5), a.i(6));
    let (dark, light) = (a.color(0), a.color(1));
    let res = a.res;
    let [value, ..] = planes(res, |u, v| {
        let (dx, dy) = (u - 0.5, v - 0.5);
        let r = (dx * dx + dy * dy).sqrt();
        let a1 = TAU * phase;
        let (ou, ov) = (0.5 + 0.5 * a1.cos(), 0.5 + 0.5 * a1.sin());
        let (wu, wv) = (fract(u + ou * 0.25), fract(v + ov * 0.25));
        let big = fbm(Basis::Perlin, wu, wv, scale, seed, 3, 0.55, 2.0);
        let fine = fbm(Basis::Perlin, wu, wv, scale * 3, seed + 9, 4, 0.5, 2.0);
        let rr = r + (big - 0.5) * turb * radius * 1.1;
        let base = smoothstep(radius, radius * (1.0 - soft * 0.9), rr);
        let puff = smoothstep(0.1, 0.9, fine);
        let val = base * (0.45 + 0.75 * puff) * density;
        [clamp01(val) * smoothstep(0.5, 0.4, r), 0.0, 0.0, 0.0]
    });
    let rgb: Vec<[f32; 3]> = value
        .iter()
        .map(|v| mix3(dark, light, pow_pos(*v, 0.8)))
        .collect();
    vec![with_alpha(rgb, &value), plane_image(&value)]
}

// ----------------------------------------------------------------- sparks

const SPARK_P: &[ParamSpec] = &[
    int("count", "Count", 3, 220, 60),
    float("length", "Streak length", 0.01, 0.25, 0.07, 0.005),
    float("thickness", "Thickness", 0.001, 0.02, 0.005, 0.0005),
    float("spread", "Radial bias", 0.0, 1.0, 0.6, 0.01),
    float("glow", "Glow", 0.0, 1.0, 0.3, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const SPARK_C: &[ColorSpec] = &[
    color("hot", "Hot", [1.0, 0.95, 0.7, 1.0]),
    color("cool", "Cool", [1.0, 0.4, 0.08, 1.0]),
];

struct Spark {
    ax: f32,
    ay: f32,
    bx: f32,
    by: f32,
    power: f32,
}

fn eval_sparks(a: &Args) -> Vec<Pixels> {
    let (count, length, thick, spread) = (a.i(0).max(1), a.f(1), a.f(2), a.f(3));
    let (glow, seed) = (a.f(4), a.i(5));
    let (hot, cool) = (a.color(0), a.color(1));
    let res = a.res;
    let sparks: Vec<Spark> = (0..count)
        .map(|i| {
            let (r1, r2) = rand2(i, 1, seed);
            let (r3, r4) = rand2(i, 2, seed);
            let radius = r1.sqrt() * 0.4;
            let theta = r2 * TAU;
            let (px, py) = (0.5 + theta.cos() * radius, 0.5 + theta.sin() * radius);
            // Streaks point away from the centre, blended with a random direction.
            let random_dir = r3 * TAU;
            let dir = theta + (random_dir - theta + PI).rem_euclid(TAU) * (1.0 - spread)
                - PI * (1.0 - spread);
            let len = length * (0.4 + r4 * 1.2);
            Spark {
                ax: px,
                ay: py,
                bx: px + dir.cos() * len,
                by: py + dir.sin() * len,
                power: 0.45 + 0.55 * rand01(i, 3, seed),
            }
        })
        .collect();
    let sigma = thick.max(1e-4);
    let [value, ..] = planes(res, |u, v| {
        let mut best = 0.0f32;
        let mut halo = 0.0f32;
        for s in &sparks {
            let (ex, ey) = (s.bx - s.ax, s.by - s.ay);
            let len2 = (ex * ex + ey * ey).max(1e-8);
            let t = clamp01(((u - s.ax) * ex + (v - s.ay) * ey) / len2);
            let (qx, qy) = (s.ax + ex * t, s.ay + ey * t);
            let d2 = (u - qx).powi(2) + (v - qy).powi(2);
            if d2 > (sigma * 14.0).powi(2) {
                continue;
            }
            // The head (t = 0) is bright, the tail fades.
            let core = (-d2 / (2.0 * sigma * sigma)).exp() * (1.0 - t * 0.85) * s.power;
            best = best.max(core);
            halo += (-d2 / (2.0 * (sigma * 5.0).powi(2))).exp() * 0.25 * s.power;
        }
        [clamp01(best + halo * glow), 0.0, 0.0, 0.0]
    });
    let rgb: Vec<[f32; 3]> = value
        .iter()
        .map(|v| mix3(cool, hot, pow_pos(*v, 1.6)))
        .collect();
    vec![with_alpha(rgb, &value), plane_image(&value)]
}

// ------------------------------------------------------------------ burst

const BURST_P: &[ParamSpec] = &[
    int("rays", "Rays", 4, 64, 24),
    float("sharp", "Ray sharpness", 1.0, 24.0, 8.0, 0.5),
    float("reach", "Reach", 0.1, 1.0, 0.8, 0.01),
    float("variation", "Length variation", 0.0, 1.0, 0.6, 0.01),
    float("core", "Core size", 0.0, 0.4, 0.08, 0.005),
    int("seed", "Seed", 0, 9999, 1),
];
const BURST_C: &[ColorSpec] = &[
    color("outer", "Outer", [1.0, 0.55, 0.15, 1.0]),
    color("core", "Core", [1.0, 0.98, 0.85, 1.0]),
];

fn eval_burst(a: &Args) -> Vec<Pixels> {
    let (rays, sharp, reach, variation) = (a.i(0).max(1), a.f(1), a.f(2), a.f(3));
    let (core_size, seed) = (a.f(4), a.i(5));
    let (outer, core) = (a.color(0), a.color(1));
    let res = a.res;
    let [value, ..] = planes(res, |u, v| {
        let (dx, dy) = (u - 0.5, v - 0.5);
        let r = (dx * dx + dy * dy).sqrt();
        let theta = dy.atan2(dx);
        let pos = (theta / TAU + 0.5) * rays as f32;
        let index = pos.floor() as i32;
        let local = (fract(pos) - 0.5).abs() * 2.0;
        let ray = (1.0 - local).powf(sharp * 0.3);
        let len = reach * 0.5 * (1.0 - variation * rand01(wrap_i(index, rays), 0, seed));
        let fall = pow_pos(1.0 - r / len.max(1e-3), 1.4);
        let centre = pow_pos(1.0 - r / (core_size + 1e-3), 2.0);
        [clamp01(ray * fall + centre), 0.0, 0.0, 0.0]
    });
    let rgb: Vec<[f32; 3]> = value.iter().map(|v| mix3(outer, core, v * v)).collect();
    vec![with_alpha(rgb, &value), plane_image(&value)]
}

// ------------------------------------------------------------------- glow

const GLOW_P: &[ParamSpec] = &[
    float("radius", "Radius", 0.05, 0.5, 0.3, 0.005),
    float("power", "Falloff power", 0.3, 6.0, 2.0, 0.05),
    float("intensity", "Intensity", 0.1, 2.0, 1.0, 0.01),
    float("aspect", "Aspect", 0.2, 3.0, 1.0, 0.01),
];
const GLOW_C: &[ColorSpec] = &[
    color("outer", "Outer", [0.9, 0.5, 0.15, 1.0]),
    color("core", "Core", [1.0, 0.97, 0.85, 1.0]),
];

fn eval_glow(a: &Args) -> Vec<Pixels> {
    let (radius, power, intensity, aspect) = (a.f(0), a.f(1), a.f(2), a.f(3));
    let (outer, core) = (a.color(0), a.color(1));
    let [value, ..] = planes(a.res, |u, v| {
        let (dx, dy) = ((u - 0.5) / aspect.sqrt(), (v - 0.5) * aspect.sqrt());
        let r = (dx * dx + dy * dy).sqrt() / radius;
        [clamp01(pow_pos(1.0 - r, power) * intensity), 0.0, 0.0, 0.0]
    });
    let rgb: Vec<[f32; 3]> = value.iter().map(|v| mix3(outer, core, v * v)).collect();
    vec![with_alpha(rgb, &value), plane_image(&value)]
}

// ------------------------------------------------------------ colour ramp

const RAMP_P: &[ParamSpec] = &[
    choice(
        "direction",
        "Direction",
        &["Horizontal", "Vertical", "Radial"],
        0,
    ),
    float("pos_b", "Stop B", 0.0, 1.0, 0.33, 0.01),
    float("pos_c", "Stop C", 0.0, 1.0, 0.66, 0.01),
    boolean("smooth", "Smooth blend", true),
];
const RAMP_C: &[ColorSpec] = &[
    color("a", "Stop A", [0.02, 0.02, 0.05, 1.0]),
    color("b", "Stop B", [0.7, 0.1, 0.05, 1.0]),
    color("c", "Stop C", [1.0, 0.7, 0.1, 1.0]),
    color("d", "Stop D", [1.0, 1.0, 0.9, 1.0]),
];

fn eval_ramp(a: &Args) -> Vec<Pixels> {
    let (dir, pb, pc, smooth) = (a.i(0), a.f(1), a.f(2).max(a.f(1)), a.b(3));
    let stops = [a.color(0), a.color(1), a.color(2), a.color(3)];
    let positions = [0.0, pb, pc, 1.0];
    let position = par_scalar(a.res, |_, _, u, v| match dir {
        1 => v,
        2 => ((u - 0.5).powi(2) + (v - 0.5).powi(2)).sqrt() * 2.0,
        _ => u,
    });
    let color: Pixels = position
        .iter()
        .map(|t| {
            let t = clamp01(*t);
            for i in 0..3 {
                if t <= positions[i + 1] || i == 2 {
                    let span = (positions[i + 1] - positions[i]).max(1e-5);
                    let mut k = clamp01((t - positions[i]) / span);
                    if smooth {
                        k = k * k * (3.0 - 2.0 * k);
                    }
                    return mix4(stops[i], stops[i + 1], k);
                }
            }
            stops[3]
        })
        .collect();
    vec![color, plane_image(&position)]
}

// --------------------------------------------------------------- registry

pub const OPS: &[OpDef] = &[
    op!(
        "leaf",
        "Leaf",
        "VFX & Shapes",
        false,
        NO_PORTS,
        OUT_LEAF,
        LEAF_P,
        LEAF_C,
        eval_leaf
    ),
    op!(
        "feather",
        "Feather",
        "VFX & Shapes",
        false,
        NO_PORTS,
        OUT_FEATHER,
        FEATHER_P,
        FEATHER_C,
        eval_feather
    ),
    op!(
        "droplet",
        "Droplet",
        "VFX & Shapes",
        false,
        NO_PORTS,
        OUT_DROP,
        DROP_P,
        DROP_C,
        eval_droplet
    ),
    op!(
        "shockwave",
        "Shockwave Ring",
        "VFX & Shapes",
        false,
        NO_PORTS,
        OUT_FX,
        SHOCK_P,
        SHOCK_C,
        eval_shockwave
    ),
    op!(
        "flame",
        "Flame",
        "VFX & Shapes",
        false,
        NO_PORTS,
        OUT_FLAME,
        FLAME_P,
        FLAME_C,
        eval_flame
    ),
    op!(
        "smoke_puff",
        "Smoke Puff",
        "VFX & Shapes",
        false,
        NO_PORTS,
        OUT_FX,
        SMOKE_P,
        SMOKE_C,
        eval_smoke
    ),
    op!(
        "sparks",
        "Sparks",
        "VFX & Shapes",
        false,
        NO_PORTS,
        OUT_FX,
        SPARK_P,
        SPARK_C,
        eval_sparks
    ),
    op!(
        "radial_burst",
        "Radial Burst",
        "VFX & Shapes",
        false,
        NO_PORTS,
        OUT_FX,
        BURST_P,
        BURST_C,
        eval_burst
    ),
    op!(
        "soft_glow",
        "Soft Glow",
        "VFX & Shapes",
        false,
        NO_PORTS,
        OUT_FX,
        GLOW_P,
        GLOW_C,
        eval_glow
    ),
    op!(
        "color_ramp",
        "Colour Ramp (LUT)",
        "VFX & Shapes",
        false,
        NO_PORTS,
        OUT_RAMP,
        RAMP_P,
        RAMP_C,
        eval_ramp
    ),
];

//! Height and PBR helpers: normals, blends, bevels, curvature, AO, wear and
//! blurs. Everything wraps around so tiled heights stay seamless.

use super::*;

const NO_COLORS: &[ColorSpec] = &[];

// ------------------------------------------------------------ shared tools

/// Height plane (0..1) → tangent-space normal map. `depth` is the relief in
/// units of the texture width times `strength` (strength 1 ≈ 5% relief).
pub fn height_to_normal(
    height: &[f32],
    res: usize,
    strength: f32,
    radius: i32,
    directx: bool,
) -> Pixels {
    let r = radius.max(1);
    let depth = 0.05 * strength;
    let scale = depth * res as f32 / (2.0 * r as f32);
    let h = height;
    par_fill(res, |x, y, _, _| {
        let (xi, yi) = (x as i32, y as i32);
        let dx = (at_wrap(h, res, xi + r, yi) - at_wrap(h, res, xi - r, yi)) * scale;
        let dy_row = (at_wrap(h, res, xi, yi + r) - at_wrap(h, res, xi, yi - r)) * scale;
        let nx = -dx;
        let ny = if directx { -dy_row } else { dy_row };
        let len = (nx * nx + ny * ny + 1.0).sqrt();
        [
            nx / len * 0.5 + 0.5,
            ny / len * 0.5 + 0.5,
            1.0 / len * 0.5 + 0.5,
            1.0,
        ]
    })
}

/// 8-bit RGBA normal map from an 8-bit height plane (used by previews).
pub fn height_to_normal_rgba8(height: &[u8], res: usize, strength: f32, directx: bool) -> Vec<u8> {
    let plane: Vec<f32> = height.iter().map(|v| *v as f32 / 255.0).collect();
    let normal = height_to_normal(&plane, res, strength, 1, directx);
    let mut out = Vec::with_capacity(res * res * 4);
    for p in normal {
        for c in p {
            out.push((clamp01(c) * 255.0).round() as u8);
        }
    }
    out
}

fn percentile(values: &[f32], q: f32) -> f32 {
    let mut hist = [0u32; 512];
    let (mut lo, mut hi) = (f32::MAX, f32::MIN);
    for v in values {
        lo = lo.min(*v);
        hi = hi.max(*v);
    }
    if hi - lo < 1e-9 {
        return hi.max(1e-6);
    }
    for v in values {
        hist[(((v - lo) / (hi - lo)) * 511.0) as usize] += 1;
    }
    let target = (values.len() as f32 * q) as u32;
    let mut acc = 0;
    for (i, h) in hist.iter().enumerate() {
        acc += h;
        if acc >= target {
            return lo + (i as f32 / 511.0) * (hi - lo);
        }
    }
    hi
}

/// Scales a positive plane so its 99th percentile is 1.
fn normalise_positive(plane: &mut [f32]) {
    let p = percentile(plane, 0.99).max(1e-6);
    for v in plane.iter_mut() {
        *v = clamp01(*v / p);
    }
}

/// Wrap-aware chamfer distance (in pixels) from every `true` pixel to the
/// nearest `false` pixel.
pub fn distance_inside(inside: &[bool], res: usize) -> Vec<f32> {
    let inf = 1.0e9f32;
    let mut d: Vec<f32> = inside.iter().map(|i| if *i { inf } else { 0.0 }).collect();
    let r = res as i32;
    let idx = |x: i32, y: i32| wrap_i(y, r) as usize * res + wrap_i(x, r) as usize;
    let diag = std::f32::consts::SQRT_2;
    for _ in 0..3 {
        for y in 0..r {
            for x in 0..r {
                let mut best = d[idx(x, y)];
                for (dx, dy, w) in [(-1, 0, 1.0), (0, -1, 1.0), (-1, -1, diag), (1, -1, diag)] {
                    best = best.min(d[idx(x + dx, y + dy)] + w);
                }
                d[idx(x, y)] = best;
            }
        }
        for y in (0..r).rev() {
            for x in (0..r).rev() {
                let mut best = d[idx(x, y)];
                for (dx, dy, w) in [(1, 0, 1.0), (0, 1, 1.0), (1, 1, diag), (-1, 1, diag)] {
                    best = best.min(d[idx(x + dx, y + dy)] + w);
                }
                d[idx(x, y)] = best;
            }
        }
    }
    d
}

fn blur_image(image: &[[f32; 4]], res: usize, radius: f32) -> Pixels {
    let mut planes: Vec<Vec<f32>> = (0..4)
        .map(|c| image.iter().map(|p| p[c]).collect())
        .collect();
    for plane in planes.iter_mut() {
        *plane = gaussian_plane(plane, res, radius);
    }
    (0..res * res)
        .map(|k| [planes[0][k], planes[1][k], planes[2][k], planes[3][k]])
        .collect()
}

// ------------------------------------------------------------ complex FFT

#[derive(Clone, Copy)]
struct Cx(f32, f32);

fn fft1(data: &mut [Cx], inverse: bool) {
    let n = data.len();
    let mut j = 0;
    for i in 1..n {
        let mut bit = n >> 1;
        while j & bit != 0 {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if i < j {
            data.swap(i, j);
        }
    }
    let mut len = 2;
    while len <= n {
        let ang = std::f32::consts::TAU / len as f32 * if inverse { 1.0 } else { -1.0 };
        let (wr, wi) = (ang.cos(), ang.sin());
        for start in (0..n).step_by(len) {
            let (mut cr, mut ci) = (1.0f32, 0.0f32);
            for k in 0..len / 2 {
                let a = data[start + k];
                let b = data[start + k + len / 2];
                let (tr, ti) = (b.0 * cr - b.1 * ci, b.0 * ci + b.1 * cr);
                data[start + k] = Cx(a.0 + tr, a.1 + ti);
                data[start + k + len / 2] = Cx(a.0 - tr, a.1 - ti);
                let (nr, ni) = (cr * wr - ci * wi, cr * wi + ci * wr);
                cr = nr;
                ci = ni;
            }
        }
        len <<= 1;
    }
}

fn fft2(data: &mut [Cx], n: usize, inverse: bool) {
    for row in data.chunks_mut(n) {
        fft1(row, inverse);
    }
    let mut column = vec![Cx(0.0, 0.0); n];
    for x in 0..n {
        for y in 0..n {
            column[y] = data[y * n + x];
        }
        fft1(&mut column, inverse);
        for y in 0..n {
            data[y * n + x] = column[y];
        }
    }
}

/// Integrates a tangent-space normal map back to a height plane (Frankot–
/// Chellappa in the Fourier domain: exact for tileable input).
pub fn normal_to_height(normal: &[[f32; 4]], res: usize, directx: bool) -> Vec<f32> {
    if !res.is_power_of_two() || res < 4 {
        return vec![0.5; res * res];
    }
    let n = res;
    let mut gx = vec![Cx(0.0, 0.0); n * n];
    let mut gy = vec![Cx(0.0, 0.0); n * n];
    for (k, p) in normal.iter().enumerate() {
        let nx = p[0] * 2.0 - 1.0;
        let mut ny = p[1] * 2.0 - 1.0;
        if directx {
            ny = -ny;
        }
        let nz = (p[2] * 2.0 - 1.0).max(0.05);
        gx[k] = Cx(-nx / nz, 0.0); // dh/dx
        gy[k] = Cx(ny / nz, 0.0); // dh/drow
    }
    fft2(&mut gx, n, false);
    fft2(&mut gy, n, false);
    let mut h = vec![Cx(0.0, 0.0); n * n];
    for ky in 0..n {
        for kx in 0..n {
            let fx = if kx <= n / 2 {
                kx as f32
            } else {
                kx as f32 - n as f32
            };
            let fy = if ky <= n / 2 {
                ky as f32
            } else {
                ky as f32 - n as f32
            };
            let (wx, wy) = (
                std::f32::consts::TAU * fx / n as f32,
                std::f32::consts::TAU * fy / n as f32,
            );
            let denom = wx * wx + wy * wy;
            let k = ky * n + kx;
            if denom < 1e-9 {
                continue;
            }
            // H = -i (wx Gx + wy Gy) / |w|²
            let (sr, si) = (wx * gx[k].0 + wy * gy[k].0, wx * gx[k].1 + wy * gy[k].1);
            h[k] = Cx(si / denom, -sr / denom);
        }
    }
    fft2(&mut h, n, true);
    let mut out: Vec<f32> = h.iter().map(|c| c.0 / (n * n) as f32).collect();
    normalize_plane(&mut out);
    out
}

// ------------------------------------------------------------------- ops

const IN_NORMAL: &[PortDef] = &[port("Normal")];

const H2N_P: &[ParamSpec] = &[
    float("strength", "Strength", 0.0, 10.0, 1.0, 0.05),
    int("radius", "Sample distance (px)", 1, 8, 1),
    boolean("directx", "DirectX (Y down)", false),
];
fn eval_h2n(a: &Args) -> Vec<Pixels> {
    let h = a.scalar(0, 0.5);
    vec![height_to_normal(&h, a.res, a.f(0), a.i(1), a.b(2))]
}

const N2H_P: &[ParamSpec] = &[boolean("directx", "DirectX (Y down)", false)];
fn eval_n2h(a: &Args) -> Vec<Pixels> {
    let n = a.image(0, [0.5, 0.5, 1.0, 1.0]);
    let h = normal_to_height(&n, a.res, a.b(0));
    vec![h.iter().map(|v| gray(*v)).collect()]
}

const HBLEND_IN: &[PortDef] = &[
    scalar_port("Height A"),
    scalar_port("Height B"),
    scalar_port("Mask"),
];
const HBLEND_OUT: &[PortDef] = &[scalar_port("Height"), scalar_port("Mask")];
const HBLEND_P: &[ParamSpec] = &[
    float("depth", "Transition depth", 0.005, 0.5, 0.1, 0.005),
    float("mask", "Mask (when unconnected)", 0.0, 1.0, 0.5, 0.01),
];
fn eval_height_blend(a: &Args) -> Vec<Pixels> {
    let ha = a.scalar(0, 0.0);
    let hb = a.scalar(1, 1.0);
    let m = a.scalar(2, a.f(1));
    let depth = a.f(0);
    let mut out = Vec::with_capacity(ha.len());
    let mut mask = Vec::with_capacity(ha.len());
    for k in 0..ha.len() {
        let (pa, pb) = (ha[k] * (1.0 - m[k]), hb[k] * m[k]);
        let top = pa.max(pb) - depth;
        let (wa, wb) = ((pa - top).max(0.0), (pb - top).max(0.0));
        let sum = (wa + wb).max(1e-6);
        out.push(gray(clamp01((ha[k] * wa + hb[k] * wb) / sum)));
        mask.push(gray(wb / sum));
    }
    vec![out, mask]
}

const TERRACE_P: &[ParamSpec] = &[
    int("steps", "Steps", 2, 32, 6),
    float("smooth", "Smoothness", 0.0, 1.0, 0.3, 0.01),
];
fn eval_terrace(a: &Args) -> Vec<Pixels> {
    let (steps, smooth) = (a.i(0).max(2) as f32, a.f(1));
    let h = a.scalar(0, 0.5);
    vec![h
        .iter()
        .map(|v| {
            let t = clamp01(*v) * steps;
            let base = t.floor();
            let f = t - base;
            gray(clamp01(
                (base + smoothstep(0.5 - smooth * 0.5, 0.5 + smooth * 0.5 + 1e-4, f)) / steps,
            ))
        })
        .collect()]
}

const BEVEL_IN: &[PortDef] = &[scalar_port("Mask")];
const BEVEL_P: &[ParamSpec] = &[
    float("width", "Width", 0.002, 0.25, 0.03, 0.002),
    choice("profile", "Profile", &["Chamfer", "Round", "Smooth"], 1),
    float("threshold", "Mask threshold", 0.05, 0.95, 0.5, 0.01),
];
fn eval_bevel(a: &Args) -> Vec<Pixels> {
    let mask = a.scalar(0, 1.0);
    let inside: Vec<bool> = mask.iter().map(|m| *m >= a.f(2)).collect();
    let d = distance_inside(&inside, a.res);
    let w = (a.f(0) * a.res as f32).max(1.0);
    let profile = a.i(1);
    vec![d
        .iter()
        .zip(inside.iter())
        .map(|(d, ins)| {
            if !*ins {
                return gray(0.0);
            }
            let t = clamp01(*d / w);
            gray(match profile {
                0 => t,
                1 => (1.0 - (1.0 - t) * (1.0 - t)).max(0.0).sqrt(),
                _ => smoothstep(0.0, 1.0, t),
            })
        })
        .collect()]
}

const SLOPE_P: &[ParamSpec] = &[
    float("threshold", "Threshold", 0.0, 1.0, 0.45, 0.01),
    float("softness", "Softness", 0.0, 1.0, 0.25, 0.01),
    boolean("invert", "Invert (flat areas)", false),
];
fn gradient_magnitude(h: &[f32], res: usize) -> Vec<f32> {
    par_scalar(res, |x, y, _, _| {
        let (xi, yi) = (x as i32, y as i32);
        let dx = at_wrap(h, res, xi + 1, yi) - at_wrap(h, res, xi - 1, yi);
        let dy = at_wrap(h, res, xi, yi + 1) - at_wrap(h, res, xi, yi - 1);
        (dx * dx + dy * dy).sqrt()
    })
}
fn eval_slope(a: &Args) -> Vec<Pixels> {
    let h = a.scalar(0, 0.5);
    let mut g = gradient_magnitude(&h, a.res);
    normalise_positive(&mut g);
    let (t, s, inv) = (a.f(0), a.f(1), a.b(2));
    vec![g
        .iter()
        .map(|v| {
            let m = smoothstep(t - s * 0.5, t + s * 0.5 + 1e-4, *v);
            gray(if inv { 1.0 - m } else { m })
        })
        .collect()]
}

const CURV_OUT: &[PortDef] = &[
    scalar_port("Convex"),
    scalar_port("Concave"),
    scalar_port("Combined"),
];
const CURV_P: &[ParamSpec] = &[
    int("radius", "Radius (px)", 1, 32, 3),
    float("strength", "Strength", 0.1, 6.0, 1.5, 0.05),
];
fn curvature_planes(h: &[f32], res: usize, radius: i32) -> (Vec<f32>, Vec<f32>) {
    let r = radius.max(1);
    let lap = par_scalar(res, |x, y, _, _| {
        let (xi, yi) = (x as i32, y as i32);
        at_wrap(h, res, xi + r, yi)
            + at_wrap(h, res, xi - r, yi)
            + at_wrap(h, res, xi, yi + r)
            + at_wrap(h, res, xi, yi - r)
            - 4.0 * at_wrap(h, res, xi, yi)
    });
    let convex: Vec<f32> = lap.iter().map(|v| (-v).max(0.0)).collect();
    let concave: Vec<f32> = lap.iter().map(|v| v.max(0.0)).collect();
    (convex, concave)
}
fn eval_curvature(a: &Args) -> Vec<Pixels> {
    let h = a.scalar(0, 0.5);
    let (mut convex, mut concave) = curvature_planes(&h, a.res, a.i(0));
    let k = a.f(1);
    normalise_positive(&mut convex);
    normalise_positive(&mut concave);
    let c: Vec<f32> = convex.iter().map(|v| clamp01(v * k)).collect();
    let n: Vec<f32> = concave.iter().map(|v| clamp01(v * k)).collect();
    let both: Pixels = c
        .iter()
        .zip(n.iter())
        .map(|(a, b)| gray(clamp01(0.5 + (a - b) * 0.5)))
        .collect();
    vec![
        c.iter().map(|v| gray(*v)).collect(),
        n.iter().map(|v| gray(*v)).collect(),
        both,
    ]
}

const AO_P: &[ParamSpec] = &[
    float("radius", "Radius", 0.002, 0.15, 0.02, 0.002),
    float("intensity", "Intensity", 0.0, 4.0, 1.0, 0.05),
    float("spread", "Multi-scale spread", 0.0, 1.0, 0.6, 0.01),
];
/// Cavity occlusion: how far a point sits below its blurred surroundings at
/// three scales. 1 = open, 0 = fully occluded.
pub fn multiscale_ao(h: &[f32], res: usize, radius: f32, intensity: f32, spread: f32) -> Vec<f32> {
    let r = (radius * res as f32).max(1.0);
    let mut acc = vec![0.0f32; res * res];
    for (scale, weight) in [
        (1.0, 1.0),
        (2.5, 0.7 * spread + 0.15),
        (6.0, 0.5 * spread + 0.05),
    ] {
        let blurred = gaussian_plane(h, res, r * scale);
        for k in 0..acc.len() {
            acc[k] += (blurred[k] - h[k]).max(0.0) * weight;
        }
    }
    normalise_positive(&mut acc);
    acc.iter()
        .map(|v| clamp01(1.0 - v * intensity * 0.85))
        .collect()
}
fn eval_ao(a: &Args) -> Vec<Pixels> {
    let h = a.scalar(0, 0.5);
    vec![multiscale_ao(&h, a.res, a.f(0), a.f(1), a.f(2))
        .iter()
        .map(|v| gray(*v))
        .collect()]
}

const WEAR_P: &[ParamSpec] = &[
    float("width", "Edge width", 0.002, 0.1, 0.012, 0.001),
    float("amount", "Amount", 0.0, 1.0, 0.6, 0.01),
    int("scale", "Noise scale", 1, 64, 12),
    float("contrast", "Contrast", 0.2, 6.0, 2.0, 0.05),
    int("seed", "Seed", 0, 9999, 1),
];
fn eval_edge_wear(a: &Args) -> Vec<Pixels> {
    let h = a.scalar(0, 0.5);
    let r = (a.f(0) * a.res as f32).max(1.0);
    let blurred = gaussian_plane(&h, a.res, r);
    let mut edge: Vec<f32> = h
        .iter()
        .zip(blurred.iter())
        .map(|(h, b)| (h - b).max(0.0))
        .collect();
    normalise_positive(&mut edge);
    let (amount, scale, contrast, seed) = (a.f(1), a.i(2), a.f(3), a.i(4));
    let res = a.res;
    let e = &edge;
    let values = par_scalar(res, |x, y, u, v| {
        let n = fbm(Basis::Perlin, u, v, scale, seed, 4, 0.55, 2.0);
        let m = e[y * res + x] * (0.35 + 0.9 * n);
        shape_value(m * amount * 1.6, contrast, 0.0)
    });
    vec![values.iter().map(|v| gray(*v)).collect()]
}

const DUST_P: &[ParamSpec] = &[
    float("amount", "Amount", 0.0, 1.0, 0.6, 0.01),
    float("radius", "Cavity radius", 0.002, 0.15, 0.03, 0.002),
    int("scale", "Noise scale", 1, 32, 6),
    float("contrast", "Contrast", 0.2, 6.0, 1.8, 0.05),
    int("seed", "Seed", 0, 9999, 2),
];
fn eval_dust(a: &Args) -> Vec<Pixels> {
    let h = a.scalar(0, 0.5);
    let blurred = gaussian_plane(&h, a.res, (a.f(1) * a.res as f32).max(1.0));
    let mut cav: Vec<f32> = h
        .iter()
        .zip(blurred.iter())
        .map(|(h, b)| (b - h).max(0.0))
        .collect();
    normalise_positive(&mut cav);
    let (amount, scale, contrast, seed) = (a.f(0), a.i(2), a.f(3), a.i(4));
    let res = a.res;
    let c = &cav;
    let values = par_scalar(res, |x, y, u, v| {
        let n = fbm(Basis::Billow, u, v, scale, seed, 5, 0.5, 2.0);
        let low = 1.0 - clamp01(h_at(&h, res, x, y));
        let m = (c[y * res + x] * 0.8 + low * 0.25) * (0.4 + 0.8 * n);
        shape_value(m * amount * 1.5, contrast, 0.0)
    });
    vec![values.iter().map(|v| gray(*v)).collect()]
}
fn h_at(h: &[f32], res: usize, x: usize, y: usize) -> f32 {
    h[y * res + x]
}

const ROUGH_P: &[ParamSpec] = &[
    float("base", "Base roughness", 0.0, 1.0, 0.55, 0.01),
    float("range", "Height influence", 0.0, 1.0, 0.35, 0.01),
    float("cavity", "Cavity boost", 0.0, 1.0, 0.3, 0.01),
    boolean("invert", "Invert height", false),
];
fn eval_roughness(a: &Args) -> Vec<Pixels> {
    let h = a.scalar(0, 0.5);
    let ao = multiscale_ao(&h, a.res, 0.02, 1.0, 0.5);
    let (base, range, cavity, invert) = (a.f(0), a.f(1), a.f(2), a.b(3));
    vec![h
        .iter()
        .zip(ao.iter())
        .map(|(h, ao)| {
            let hv = if invert { 1.0 - h } else { *h };
            gray(clamp01(base + (hv - 0.5) * range + (1.0 - ao) * cavity))
        })
        .collect()]
}

const BLUR_P: &[ParamSpec] = &[float("radius", "Radius (px)", 0.0, 64.0, 4.0, 0.5)];
fn eval_gaussian(a: &Args) -> Vec<Pixels> {
    let image = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    vec![blur_image(&image, a.res, a.f(0))]
}

fn eval_box(a: &Args) -> Vec<Pixels> {
    let image = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let r = a.f(0).round() as i32;
    if r < 1 {
        return vec![image];
    }
    let res = a.res;
    let img = &image;
    let horizontal = par_fill(res, |x, y, _, _| {
        let mut acc = [0.0f32; 4];
        for d in -r..=r {
            let p = img[wrap_i(y as i32, res as i32) as usize * res
                + wrap_i(x as i32 + d, res as i32) as usize];
            for c in 0..4 {
                acc[c] += p[c];
            }
        }
        let n = (2 * r + 1) as f32;
        [acc[0] / n, acc[1] / n, acc[2] / n, acc[3] / n]
    });
    let h = &horizontal;
    vec![par_fill(res, |x, y, _, _| {
        let mut acc = [0.0f32; 4];
        for d in -r..=r {
            let p = h[wrap_i(y as i32 + d, res as i32) as usize * res + x];
            for c in 0..4 {
                acc[c] += p[c];
            }
        }
        let n = (2 * r + 1) as f32;
        [acc[0] / n, acc[1] / n, acc[2] / n, acc[3] / n]
    })]
}

const BILATERAL_P: &[ParamSpec] = &[
    int("radius", "Radius (px)", 1, 6, 3),
    float("range", "Edge sensitivity", 0.02, 1.0, 0.15, 0.01),
];
fn eval_bilateral(a: &Args) -> Vec<Pixels> {
    let image = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let (r, range) = (a.i(0).max(1), a.f(1).max(0.01));
    let res = a.res;
    let img = &image;
    vec![par_fill(res, |x, y, _, _| {
        let center = img[y * res + x];
        let (mut acc, mut wsum) = ([0.0f32; 4], 0.0f32);
        for dy in -r..=r {
            for dx in -r..=r {
                let p = img[wrap_i(y as i32 + dy, res as i32) as usize * res
                    + wrap_i(x as i32 + dx, res as i32) as usize];
                let spatial =
                    (-((dx * dx + dy * dy) as f32) / (2.0 * (r as f32 * 0.6).powi(2))).exp();
                let dl = luma(p) - luma(center);
                let w = spatial * (-(dl * dl) / (2.0 * range * range)).exp();
                for c in 0..4 {
                    acc[c] += p[c] * w;
                }
                wsum += w;
            }
        }
        [acc[0] / wsum, acc[1] / wsum, acc[2] / wsum, acc[3] / wsum]
    })]
}

pub static OPS: &[OpDef] = &[
    op!(
        "height_to_normal",
        "Height to Normal",
        "Height & PBR",
        true,
        IN_HEIGHT,
        OUT_COLOR,
        H2N_P,
        NO_COLORS,
        eval_h2n
    ),
    op!(
        "normal_to_height",
        "Normal to Height",
        "Height & PBR",
        true,
        IN_NORMAL,
        OUT_VALUE,
        N2H_P,
        NO_COLORS,
        eval_n2h
    ),
    op!(
        "height_blend",
        "Height Blend",
        "Height & PBR",
        true,
        HBLEND_IN,
        HBLEND_OUT,
        HBLEND_P,
        NO_COLORS,
        eval_height_blend
    ),
    op!(
        "terrace",
        "Terrace",
        "Height & PBR",
        true,
        IN_HEIGHT,
        OUT_VALUE,
        TERRACE_P,
        NO_COLORS,
        eval_terrace
    ),
    op!(
        "bevel",
        "Bevel",
        "Height & PBR",
        true,
        BEVEL_IN,
        OUT_VALUE,
        BEVEL_P,
        NO_COLORS,
        eval_bevel
    ),
    op!(
        "slope_mask",
        "Slope Mask",
        "Height & PBR",
        true,
        IN_HEIGHT,
        OUT_VALUE,
        SLOPE_P,
        NO_COLORS,
        eval_slope
    ),
    op!(
        "curvature",
        "Curvature",
        "Height & PBR",
        true,
        IN_HEIGHT,
        CURV_OUT,
        CURV_P,
        NO_COLORS,
        eval_curvature
    ),
    op!(
        "ao_multiscale",
        "Multi-scale AO",
        "Height & PBR",
        true,
        IN_HEIGHT,
        OUT_VALUE,
        AO_P,
        NO_COLORS,
        eval_ao
    ),
    op!(
        "edge_wear",
        "Edge Wear",
        "Height & PBR",
        true,
        IN_HEIGHT,
        OUT_VALUE,
        WEAR_P,
        NO_COLORS,
        eval_edge_wear
    ),
    op!(
        "dust",
        "Dust / Sediment",
        "Height & PBR",
        true,
        IN_HEIGHT,
        OUT_VALUE,
        DUST_P,
        NO_COLORS,
        eval_dust
    ),
    op!(
        "roughness_from_height",
        "Roughness from Height",
        "Height & PBR",
        true,
        IN_HEIGHT,
        OUT_VALUE,
        ROUGH_P,
        NO_COLORS,
        eval_roughness
    ),
    op!(
        "gaussian_blur",
        "Gaussian Blur",
        "Height & PBR",
        true,
        IN_IMAGE,
        OUT_COLOR,
        BLUR_P,
        NO_COLORS,
        eval_gaussian
    ),
    op!(
        "box_blur",
        "Box Blur",
        "Height & PBR",
        true,
        IN_IMAGE,
        OUT_COLOR,
        BLUR_P,
        NO_COLORS,
        eval_box
    ),
    op!(
        "bilateral_blur",
        "Bilateral Blur",
        "Height & PBR",
        true,
        IN_IMAGE,
        OUT_COLOR,
        BILATERAL_P,
        NO_COLORS,
        eval_bilateral
    ),
];

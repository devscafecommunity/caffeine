//! Natural surfaces and grunge: dirt, rust, wood, marble, fabric, metal…
//! All ops tile seamlessly.

use super::*;

const TAU: f32 = std::f32::consts::TAU;
const PI: f32 = std::f32::consts::PI;

const DIRT_COLORS: &[ColorSpec] = &[
    color("color_a", "Clean", [0.85, 0.85, 0.85, 1.0]),
    color("color_b", "Dirt", [0.12, 0.09, 0.06, 1.0]),
];
const RUST_COLORS: &[ColorSpec] = &[
    color("color_a", "Deep rust", [0.18, 0.07, 0.03, 1.0]),
    color("color_b", "Bright rust", [0.62, 0.27, 0.08, 1.0]),
];
const MOSS_COLORS: &[ColorSpec] = &[
    color("color_a", "Shadow", [0.04, 0.10, 0.03, 1.0]),
    color("color_b", "Moss", [0.28, 0.45, 0.10, 1.0]),
];
const WOOD_COLORS: &[ColorSpec] = &[
    color("color_a", "Late wood", [0.22, 0.11, 0.05, 1.0]),
    color("color_b", "Early wood", [0.55, 0.34, 0.17, 1.0]),
];
const STONE_COLORS: &[ColorSpec] = &[
    color("color_a", "Dark", [0.22, 0.21, 0.20, 1.0]),
    color("color_b", "Light", [0.66, 0.63, 0.58, 1.0]),
];
const MARBLE_COLORS: &[ColorSpec] = &[
    color("color_a", "Vein", [0.18, 0.18, 0.20, 1.0]),
    color("color_b", "Stone", [0.90, 0.89, 0.86, 1.0]),
];
const FABRIC_COLORS: &[ColorSpec] = &[
    color("color_a", "Shadow", [0.07, 0.10, 0.20, 1.0]),
    color("color_b", "Thread", [0.28, 0.38, 0.60, 1.0]),
];
const METAL_COLORS: &[ColorSpec] = &[
    color("color_a", "Dark", [0.45, 0.46, 0.48, 1.0]),
    color("color_b", "Light", [0.80, 0.81, 0.83, 1.0]),
];
const SAND_COLORS: &[ColorSpec] = &[
    color("color_a", "Shadow", [0.52, 0.40, 0.24, 1.0]),
    color("color_b", "Sand", [0.86, 0.74, 0.50, 1.0]),
];
const WATER_COLORS: &[ColorSpec] = &[
    color("color_a", "Deep", [0.01, 0.12, 0.25, 1.0]),
    color("color_b", "Light", [0.35, 0.80, 0.90, 1.0]),
];

// ---------------------------------------------------------------- params

const DIRT_P: &[ParamSpec] = &[
    int("scale", "Scale", 1, 32, 3),
    int("octaves", "Octaves", 1, 10, 6),
    int("seed", "Seed", 0, 9999, 1),
    float("coverage", "Coverage", 0.0, 1.0, 0.5, 0.01),
    float("grain", "Grain", 0.0, 1.0, 0.5, 0.01),
    float("contrast", "Contrast", 0.2, 4.0, 1.4, 0.05),
];
const SCRATCH_P: &[ParamSpec] = &[
    int("density", "Density (cells)", 2, 64, 14),
    float("length", "Length", 0.05, 2.0, 0.7, 0.01),
    float("width", "Width", 0.0005, 0.02, 0.0035, 0.0005),
    float("spread", "Angle spread", 0.0, 1.0, 1.0, 0.01),
    float("angle", "Angle", 0.0, 180.0, 20.0, 1.0),
    int("seed", "Seed", 0, 9999, 1),
    float("intensity", "Intensity", 0.0, 1.0, 0.85, 0.01),
];
const CRACK_P: &[ParamSpec] = &[
    int("scale", "Plates", 2, 32, 6),
    float("width", "Crack width", 0.005, 0.4, 0.06, 0.005),
    float("warp", "Warp", 0.0, 1.0, 0.35, 0.01),
    float("jitter", "Jitter", 0.0, 1.0, 0.95, 0.01),
    int("seed", "Seed", 0, 9999, 1),
    float("plate_var", "Plate variation", 0.0, 1.0, 0.25, 0.01),
];
const BLOTCH_P: &[ParamSpec] = &[
    int("scale", "Scale", 1, 32, 4),
    int("octaves", "Octaves", 1, 10, 6),
    int("seed", "Seed", 0, 9999, 5),
    float("coverage", "Coverage", 0.0, 1.0, 0.55, 0.01),
    float("grain", "Grain", 0.0, 1.0, 0.45, 0.01),
    float("contrast", "Contrast", 0.2, 4.0, 1.3, 0.05),
];
const STRATA_P: &[ParamSpec] = &[
    int("layers", "Layers", 2, 64, 12),
    float("warp", "Warp", 0.0, 1.0, 0.35, 0.01),
    int("scale", "Warp scale", 1, 16, 3),
    float("tone", "Layer tone variation", 0.0, 1.0, 0.7, 0.01),
    float("grain", "Grain", 0.0, 1.0, 0.3, 0.01),
    int("seed", "Seed", 0, 9999, 2),
];
const RIPPLE_P: &[ParamSpec] = &[
    int("ripples", "Ripples", 2, 64, 14),
    float("warp", "Warp", 0.0, 1.0, 0.3, 0.01),
    float("asymmetry", "Asymmetry", 0.1, 0.9, 0.7, 0.01),
    float("grain", "Grain", 0.0, 1.0, 0.25, 0.01),
    int("seed", "Seed", 0, 9999, 4),
];
const WOOD_P: &[ParamSpec] = &[
    int("rings", "Rings", 2, 64, 14),
    float("distort", "Distortion", 0.0, 1.5, 0.45, 0.01),
    float("fibre", "Fibre", 0.0, 1.0, 0.35, 0.01),
    int("scale", "Distortion scale", 1, 16, 3),
    int("seed", "Seed", 0, 9999, 1),
    float("contrast", "Contrast", 0.2, 4.0, 1.2, 0.05),
];
const MARBLE_P: &[ParamSpec] = &[
    int("freq", "Veins", 1, 16, 3),
    float("turbulence", "Turbulence", 0.0, 4.0, 1.4, 0.05),
    float("sharpness", "Sharpness", 1.0, 20.0, 5.0, 0.1),
    int("scale", "Turbulence scale", 1, 16, 3),
    int("seed", "Seed", 0, 9999, 6),
    float("mottle", "Mottling", 0.0, 1.0, 0.3, 0.01),
];
const WEAVE_P: &[ParamSpec] = &[
    int("threads", "Threads", 2, 64, 16),
    float("fuzz", "Fibre fuzz", 0.0, 1.0, 0.35, 0.01),
    float("gap", "Thread gap", 0.0, 0.4, 0.08, 0.01),
    int("seed", "Seed", 0, 9999, 1),
    float("tone", "Thread tone variation", 0.0, 1.0, 0.35, 0.01),
];
const LEATHER_P: &[ParamSpec] = &[
    int("cells", "Pores", 4, 128, 36),
    float("pore", "Pore size", 0.05, 0.9, 0.3, 0.01),
    float("wrinkle", "Wrinkles", 0.0, 1.0, 0.45, 0.01),
    int("scale", "Wrinkle scale", 1, 32, 6),
    int("seed", "Seed", 0, 9999, 1),
];
const PIT_P: &[ParamSpec] = &[
    int("cells", "Pit grid", 4, 128, 40),
    float("density", "Density", 0.0, 1.0, 0.35, 0.01),
    float("size", "Pit size", 0.05, 0.9, 0.35, 0.01),
    float("base", "Base roughness", 0.0, 1.0, 0.35, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const BRUSH_P: &[ParamSpec] = &[
    choice("direction", "Direction", &["Horizontal", "Vertical"], 0),
    int("lines", "Line density", 16, 512, 220),
    float("strength", "Strength", 0.0, 1.0, 0.8, 0.01),
    int("seed", "Seed", 0, 9999, 1),
    int("octaves", "Octaves", 1, 6, 3),
];
const DENT_P: &[ParamSpec] = &[
    int("cells", "Dents", 2, 48, 10),
    float("jitter", "Jitter", 0.0, 1.0, 0.9, 0.01),
    float("depth", "Depth variation", 0.0, 1.0, 0.6, 0.01),
    float("rim", "Rim", 0.0, 1.0, 0.3, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const GRUNGE_P: &[ParamSpec] = &[
    int("scale", "Scale", 1, 24, 4),
    float("detail", "Detail", 0.0, 1.0, 0.6, 0.01),
    float("contrast", "Contrast", 0.2, 5.0, 1.8, 0.05),
    int("seed", "Seed", 0, 9999, 1),
    float("bias", "Bias", -0.5, 0.5, 0.0, 0.01),
];
const WATER_P: &[ParamSpec] = &[
    int("sources", "Sources", 1, 12, 5),
    float("frequency", "Frequency", 2.0, 60.0, 14.0, 0.5),
    float("decay", "Decay", 0.0, 12.0, 3.0, 0.1),
    int("seed", "Seed", 0, 9999, 1),
];
const CAUSTIC_P: &[ParamSpec] = &[
    int("scale", "Scale", 1, 8, 2),
    float("phase", "Phase", 0.0, 12.0, 2.0, 0.05),
    float("sharpness", "Sharpness", 1.0, 16.0, 7.0, 0.1),
];

// ------------------------------------------------------------------ evals

fn eval_dirt(a: &Args) -> Vec<Pixels> {
    let (scale, oct, seed, coverage, grain, contrast) =
        (a.i(0), a.i(1), a.i(2), a.f(3), a.f(4), a.f(5));
    let values = par_scalar(a.res, |_, _, u, v| {
        let blotch = fbm(Basis::Perlin, u, v, scale, seed, oct, 0.55, 2.0);
        let edge = 1.0 - coverage;
        let mask = smoothstep(edge - 0.25, edge + 0.25, blotch);
        let speck = value_noise(u, v, scale * 40, seed + 9);
        let streak = fbm(Basis::Ridged, u, v, scale * 2, seed + 4, 3, 0.5, 2.0);
        let dirt = mask * (1.0 - grain + grain * (0.55 * speck + 0.45 * streak));
        shape_value(dirt, contrast, 0.0)
    });
    pattern_outputs(a, &values)
}

fn segment_distance(px: f32, py: f32, ax: f32, ay: f32, bx: f32, by: f32) -> f32 {
    let (abx, aby) = (bx - ax, by - ay);
    let len2 = (abx * abx + aby * aby).max(1e-9);
    let t = (((px - ax) * abx + (py - ay) * aby) / len2).clamp(0.0, 1.0);
    let (cx, cy) = (ax + abx * t, ay + aby * t);
    ((px - cx).powi(2) + (py - cy).powi(2)).sqrt()
}

fn eval_scratches(a: &Args) -> Vec<Pixels> {
    let (cells, length, width, spread, angle, seed, intensity) = (
        a.i(0),
        a.f(1),
        a.f(2),
        a.f(3),
        a.f(4).to_radians(),
        a.i(5),
        a.f(6),
    );
    let values = par_scalar(a.res, |_, _, u, v| {
        let (x, y) = (u * cells as f32, v * cells as f32);
        let (ix, iy) = (x.floor() as i32, y.floor() as i32);
        let mut best = 0.0f32;
        for oy in -2..=2 {
            for ox in -2..=2 {
                let (cx, cy) = (ix + ox, iy + oy);
                let (wx, wy) = (wrap_i(cx, cells), wrap_i(cy, cells));
                for k in 0..2 {
                    let s = seed.wrapping_add(k * 4001);
                    let (rx, ry) = rand2(wx, wy, s);
                    let theta = angle + (rand01(wx, wy, s + 3) - 0.5) * PI * spread;
                    let half = length * (0.4 + rand01(wx, wy, s + 5)) * 0.5;
                    let (mx, my) = (cx as f32 + rx, cy as f32 + ry);
                    let (dx, dy) = (theta.cos() * half, theta.sin() * half);
                    let d =
                        segment_distance(x, y, mx - dx, my - dy, mx + dx, my + dy) / cells as f32;
                    let w = width * (0.6 + rand01(wx, wy, s + 7));
                    let line = 1.0 - smoothstep(w * 0.4, w, d);
                    best = best.max(line * (0.45 + 0.55 * rand01(wx, wy, s + 11)));
                }
            }
        }
        // Scratches are low spots: 1 = untouched surface.
        1.0 - best * intensity
    });
    pattern_outputs(a, &values)
}

fn eval_cracks(a: &Args) -> Vec<Pixels> {
    let (scale, width, warp, jitter, seed, var) = (a.i(0), a.f(1), a.f(2), a.f(3), a.i(4), a.f(5));
    let mut cell_var = vec![0.0f32; a.res * a.res];
    let values = {
        let rows = par_fill(a.res, |_, _, u, v| {
            let dx = (perlin(u, v, scale * 2, seed + 31) - 0.5) * warp * 0.35;
            let dy = (perlin(u, v, scale * 2, seed + 57) - 0.5) * warp * 0.35;
            let c = worley(u + dx, v + dy, scale, seed, jitter, false);
            let edge = (c.f2 - c.f1) * 2.0;
            let plate = smoothstep(0.0, width, edge);
            [plate, c.id, 0.0, 1.0]
        });
        for (slot, p) in cell_var.iter_mut().zip(rows.iter()) {
            *slot = p[1];
        }
        rows.iter()
            .zip(cell_var.iter())
            .map(|(p, id)| p[0] * (1.0 - var * 0.5 + var * 0.5 * id))
            .collect::<Vec<f32>>()
    };
    pattern_outputs_var(a, &values, &cell_var)
}

fn eval_rust(a: &Args) -> Vec<Pixels> {
    let (scale, oct, seed, coverage, grain, contrast) =
        (a.i(0), a.i(1), a.i(2), a.f(3), a.f(4), a.f(5));
    let values = par_scalar(a.res, |_, _, u, v| {
        let base = fbm(Basis::Perlin, u, v, scale, seed, oct, 0.55, 2.0);
        let pits = fbm(Basis::Billow, u, v, scale * 6, seed + 3, 3, 0.5, 2.0);
        let mask = smoothstep(1.0 - coverage - 0.2, 1.0 - coverage + 0.2, base);
        shape_value(
            mask * (1.0 - grain * 0.6 + grain * 0.6 * pits),
            contrast,
            0.0,
        )
    });
    pattern_outputs(a, &values)
}

fn eval_moss(a: &Args) -> Vec<Pixels> {
    let (scale, oct, seed, coverage, grain, contrast) =
        (a.i(0), a.i(1), a.i(2), a.f(3), a.f(4), a.f(5));
    let values = par_scalar(a.res, |_, _, u, v| {
        let big = fbm(Basis::Perlin, u, v, scale, seed, oct, 0.55, 2.0);
        let clumps = worley(u, v, scale * 14, seed + 1, 1.0, false);
        let fuzz = value_noise(u, v, scale * 48, seed + 2);
        let clump = 1.0 - smoothstep(0.1, 0.75, clumps.f1);
        let mask = smoothstep(1.0 - coverage - 0.15, 1.0 - coverage + 0.15, big);
        shape_value(
            mask * (0.55 * clump + 0.45 * (1.0 - grain + grain * fuzz)),
            contrast,
            0.0,
        )
    });
    pattern_outputs(a, &values)
}

fn eval_strata(a: &Args) -> Vec<Pixels> {
    let (layers, warp, scale, tone, grain, seed) = (a.i(0), a.f(1), a.i(2), a.f(3), a.f(4), a.i(5));
    let values = par_scalar(a.res, |_, _, u, v| {
        let w = perlin2(u, v, scale, scale.max(2) * 2, seed) - 0.5;
        let t = v * layers as f32 + warp * w * 4.0;
        let layer = t.floor() as i32;
        let level = rand01(wrap_i(layer, layers), 0, seed + 11);
        let f = fract(t);
        let edge = smoothstep(0.0, 0.12, f) * (1.0 - 0.3 * smoothstep(0.85, 1.0, f));
        let g = value_noise2(u, v, 6, layers * 6, seed + 3);
        let base = 0.5 + (level - 0.5) * tone;
        clamp01(base * (0.6 + 0.4 * edge) + (g - 0.5) * grain * 0.6)
    });
    pattern_outputs(a, &values)
}

fn eval_ripples(a: &Args) -> Vec<Pixels> {
    let (ripples, warp, asym, grain, seed) = (a.i(0), a.f(1), a.f(2), a.f(3), a.i(4));
    let values = par_scalar(a.res, |_, _, u, v| {
        let w = fbm(Basis::Perlin, u, v, 3, seed, 3, 0.5, 2.0) - 0.5;
        let t = fract(v * ripples as f32 + warp * w * 3.0);
        let wave = if t < asym {
            t / asym
        } else {
            (1.0 - t) / (1.0 - asym)
        };
        let sand = value_noise(u, v, 160, seed + 2);
        clamp01(wave * (1.0 - grain * 0.5) + (sand - 0.5) * grain * 0.5 + grain * 0.25)
    });
    pattern_outputs(a, &values)
}

fn eval_wood(a: &Args) -> Vec<Pixels> {
    let (rings, distort, fibre, scale, seed, contrast) =
        (a.i(0), a.f(1), a.f(2), a.i(3), a.i(4), a.f(5));
    let values = par_scalar(a.res, |_, _, u, v| {
        let warp = perlin2(u, v, scale, rings.max(2) / 2 + 1, seed) - 0.5;
        let t = v * rings as f32 + distort * warp * 4.0;
        let p = fract(t);
        let ring = 0.5 + 0.5 * (TAU * t).sin();
        let late = smoothstep(0.65, 1.0, p);
        let rings_v = ring * 0.6 + (1.0 - late) * 0.4;
        let fibre_n = value_noise2(u, v, 4, rings * 10, seed + 1);
        let pore = value_noise2(u, v, 24, rings * 24, seed + 2);
        let base = rings_v * (1.0 - fibre) + fibre_n * fibre;
        shape_value(base - (1.0 - pore) * 0.08, contrast, 0.0)
    });
    pattern_outputs(a, &values)
}

fn eval_marble(a: &Args) -> Vec<Pixels> {
    let (freq, turb, sharp, scale, seed, mottle) = (a.i(0), a.f(1), a.f(2), a.i(3), a.i(4), a.f(5));
    let values = par_scalar(a.res, |_, _, u, v| {
        let n = fbm(Basis::Perlin, u, v, scale, seed, 5, 0.5, 2.0) - 0.5;
        let t = freq as f32 * (2.0 * u + v) + turb * n;
        let vein = (1.0 - (PI * t).sin().abs()).powf(sharp);
        let mott = fbm(Basis::Simplex, u, v, scale * 2, seed + 8, 4, 0.5, 2.0);
        clamp01(1.0 - vein * 0.85 - (1.0 - mott) * mottle * 0.4)
    });
    pattern_outputs(a, &values)
}

fn eval_weave(a: &Args) -> Vec<Pixels> {
    let (threads, fuzz, gap, seed, tone) = (a.i(0), a.f(1), a.f(2), a.i(3), a.f(4));
    let res = a.res;
    let mut variation = vec![0.0f32; res * res];
    let rows = par_fill(res, |_, _, u, v| {
        let (x, y) = (u * threads as f32, v * threads as f32);
        let (i, j) = (x.floor() as i32, y.floor() as i32);
        let (fx, fy) = (x - i as f32, y - j as f32);
        let over_horizontal = (i + j) & 1 == 0;
        let round = |f: f32| (PI * f).sin().powf(0.6);
        let thread_h = round(fy);
        let thread_v = round(fx);
        let h = if over_horizontal {
            0.55 + 0.45 * thread_h
        } else {
            0.55 + 0.45 * thread_v
        };
        let seam = smoothstep(0.0, gap + 0.001, fx.min(1.0 - fx).min(fy.min(1.0 - fy)));
        let id = if over_horizontal {
            rand01(wrap_i(j, threads), 3, seed)
        } else {
            rand01(wrap_i(i, threads), 7, seed)
        };
        let f = value_noise(u, v, threads * 24, seed + 5);
        [
            clamp01(h * (0.6 + 0.4 * seam) + (f - 0.5) * fuzz * 0.3),
            id,
            0.0,
            1.0,
        ]
    });
    let mut values = vec![0.0f32; res * res];
    for (k, p) in rows.iter().enumerate() {
        values[k] = clamp01(p[0] * (1.0 - tone * 0.4 + tone * 0.4 * p[1]));
        variation[k] = p[1];
    }
    pattern_outputs_var(a, &values, &variation)
}

fn eval_leather(a: &Args) -> Vec<Pixels> {
    let (cells, pore, wrinkle, scale, seed) = (a.i(0), a.f(1), a.f(2), a.i(3), a.i(4));
    let values = par_scalar(a.res, |_, _, u, v| {
        let c = worley(u, v, cells, seed, 0.95, false);
        let pit = 1.0 - smoothstep(pore * 0.5, pore, c.f1);
        let edge = smoothstep(0.0, 0.3, c.f2 - c.f1);
        let wr = fbm(Basis::Ridged, u, v, scale, seed + 5, 3, 0.5, 2.0);
        clamp01(0.75 * edge + 0.25 - pit * 0.45 - wr * wrinkle * 0.25 + 0.2)
    });
    pattern_outputs(a, &values)
}

fn eval_pits(a: &Args) -> Vec<Pixels> {
    let (cells, density, size, base, seed) = (a.i(0), a.f(1), a.f(2), a.f(3), a.i(4));
    let values = par_scalar(a.res, |_, _, u, v| {
        let c = worley(u, v, cells, seed, 0.9, false);
        let pit = if c.id < density {
            1.0 - smoothstep(0.0, size * (0.4 + 0.6 * c.id / density.max(0.01)), c.f1)
        } else {
            0.0
        };
        let bumps = fbm(Basis::Perlin, u, v, 6, seed + 3, 5, 0.55, 2.0);
        let fine = value_noise(u, v, cells * 3, seed + 6);
        clamp01(0.55 + (bumps - 0.5) * base + (fine - 0.5) * base * 0.5 - pit * 0.6)
    });
    pattern_outputs(a, &values)
}

fn eval_brushed(a: &Args) -> Vec<Pixels> {
    let (vertical, lines, strength, seed, oct) = (a.i(0) == 1, a.i(1), a.f(2), a.i(3), a.i(4));
    let values = par_scalar(a.res, |_, _, u, v| {
        let (s, t) = if vertical { (v, u) } else { (u, v) };
        let mut total = 0.0;
        let mut amp = 1.0;
        let mut norm = 0.0;
        for o in 0..oct.clamp(1, 6) {
            let f = 1 << o;
            total += value_noise2(t, s, lines * f / 2, 2 + o, seed + o * 17) * amp;
            norm += amp;
            amp *= 0.55;
        }
        clamp01(0.5 + (total / norm - 0.5) * strength * 1.6)
    });
    pattern_outputs(a, &values)
}

fn eval_dents(a: &Args) -> Vec<Pixels> {
    let (cells, jitter, depth, rim, seed) = (a.i(0), a.f(1), a.f(2), a.f(3), a.i(4));
    let values = par_scalar(a.res, |_, _, u, v| {
        let c = worley(u, v, cells, seed, jitter, false);
        let d = (c.f1 * 1.6).min(1.0);
        let bowl = (1.0 - d * d).max(0.0);
        let amount = 1.0 - depth + depth * c.id;
        let ridge = rim * smoothstep(0.7, 1.0, d) * (1.0 - smoothstep(1.0, 1.3, d * 1.0));
        clamp01(0.8 - bowl * amount * 0.6 + ridge * 0.25)
    });
    pattern_outputs(a, &values)
}

fn eval_grunge(a: &Args) -> Vec<Pixels> {
    let (scale, detail, contrast, seed, bias) = (a.i(0), a.f(1), a.f(2), a.i(3), a.f(4));
    let values = par_scalar(a.res, |_, _, u, v| {
        let big = fbm(Basis::Ridged, u, v, scale, seed, 5, 0.55, 2.0);
        let mid = fbm(Basis::Billow, u, v, scale * 3, seed + 1, 4, 0.5, 2.0);
        let fine = value_noise(u, v, scale * 24, seed + 2);
        let g = big * (0.55 + 0.45 * mid) * (1.0 - detail * 0.5 + detail * 0.5 * fine);
        shape_value(g * 1.5, contrast, bias)
    });
    pattern_outputs(a, &values)
}

fn eval_water(a: &Args) -> Vec<Pixels> {
    let (sources, freq, decay, seed) = (a.i(0), a.f(1), a.f(2), a.i(3));
    let freq = freq.round().max(1.0);
    let centers: Vec<(f32, f32, f32)> = (0..sources)
        .map(|k| {
            let (x, y) = rand2(k, 0, seed);
            (x, y, rand01(k, 1, seed) * TAU)
        })
        .collect();
    let values = par_scalar(a.res, |_, _, u, v| {
        let mut sum = 0.0;
        for &(cx, cy, phase) in &centers {
            for oy in -1..=1 {
                for ox in -1..=1 {
                    let d = ((u - cx - ox as f32).powi(2) + (v - cy - oy as f32).powi(2)).sqrt();
                    if d < 1.0 {
                        sum += (TAU * freq * d - phase).sin() * (-d * decay).exp();
                    }
                }
            }
        }
        clamp01(0.5 + sum / (sources as f32).sqrt() * 0.45)
    });
    pattern_outputs(a, &values)
}

fn eval_caustics(a: &Args) -> Vec<Pixels> {
    let (scale, phase, sharp) = (a.i(0) as f32, a.f(1), a.f(2));
    let values = par_scalar(a.res, |_, _, u, v| {
        let (px, py) = (
            (u * scale * TAU) % TAU - 250.0,
            (v * scale * TAU) % TAU - 250.0,
        );
        let (mut ix, mut iy) = (px, py);
        let mut c = 1.0f32;
        let inten = 0.005f32;
        for n in 0..5 {
            let t = phase * (1.0 - 3.5 / (n as f32 + 1.0));
            ix = px + (t - ix).cos() + (t + iy).sin();
            iy = py + (t - iy).sin() + (t + ix).cos();
            let lx = px / ((ix + t).sin() / inten);
            let ly = py / ((iy + t).cos() / inten);
            c += 1.0 / (lx * lx + ly * ly).sqrt().max(1e-4);
        }
        c /= 5.0;
        let c = 1.17 - c.powf(1.4);
        clamp01(c.abs().powf(sharp.max(1.0)))
    });
    pattern_outputs(a, &values)
}

pub static OPS: &[OpDef] = &[
    op!(
        "dirt",
        "Dirt",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        DIRT_P,
        DIRT_COLORS,
        eval_dirt
    ),
    op!(
        "scratches",
        "Scratches",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        SCRATCH_P,
        METAL_COLORS,
        eval_scratches
    ),
    op!(
        "cracks",
        "Cracks",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN_VAR,
        CRACK_P,
        STONE_COLORS,
        eval_cracks
    ),
    op!(
        "rust",
        "Rust",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        BLOTCH_P,
        RUST_COLORS,
        eval_rust
    ),
    op!(
        "moss",
        "Moss",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        BLOTCH_P,
        MOSS_COLORS,
        eval_moss
    ),
    op!(
        "strata",
        "Rock Strata",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        STRATA_P,
        STONE_COLORS,
        eval_strata
    ),
    op!(
        "sand_ripples",
        "Sand Ripples",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        RIPPLE_P,
        SAND_COLORS,
        eval_ripples
    ),
    op!(
        "wood_grain",
        "Wood Grain",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        WOOD_P,
        WOOD_COLORS,
        eval_wood
    ),
    op!(
        "marble_veins",
        "Marble Veins",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        MARBLE_P,
        MARBLE_COLORS,
        eval_marble
    ),
    op!(
        "fabric_weave",
        "Fabric Weave",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN_VAR,
        WEAVE_P,
        FABRIC_COLORS,
        eval_weave
    ),
    op!(
        "leather_pores",
        "Leather Pores",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        LEATHER_P,
        STONE_COLORS,
        eval_leather
    ),
    op!(
        "concrete_pits",
        "Concrete Pits",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        PIT_P,
        STONE_COLORS,
        eval_pits
    ),
    op!(
        "brushed_metal",
        "Brushed Metal",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        BRUSH_P,
        METAL_COLORS,
        eval_brushed
    ),
    op!(
        "hammered_dents",
        "Hammered Dents",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        DENT_P,
        METAL_COLORS,
        eval_dents
    ),
    op!(
        "grunge",
        "Grunge",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        GRUNGE_P,
        DIRT_COLORS,
        eval_grunge
    ),
    op!(
        "water_ripples",
        "Water Ripples",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        WATER_P,
        WATER_COLORS,
        eval_water
    ),
    op!(
        "caustics",
        "Caustics",
        "Natural",
        true,
        NO_PORTS,
        OUT_PATTERN,
        CAUSTIC_P,
        WATER_COLORS,
        eval_caustics
    ),
];

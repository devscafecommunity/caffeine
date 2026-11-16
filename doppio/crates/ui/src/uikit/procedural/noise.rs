//! Noise generators (all tileable, seeded).

use super::*;

pub const NOISE_PARAMS: &[ParamSpec] = &[
    int("scale", "Scale", 1, 64, 6),
    int("octaves", "Octaves", 1, 10, 5),
    float("persistence", "Persistence", 0.05, 1.0, 0.5, 0.01),
    float("lacunarity", "Lacunarity", 1.0, 4.0, 2.0, 0.05),
    int("seed", "Seed", 0, 9999, 1),
    float("contrast", "Contrast", 0.2, 5.0, 1.0, 0.05),
    float("bias", "Bias", -0.5, 0.5, 0.0, 0.01),
];

const WARP_PARAMS: &[ParamSpec] = &[
    int("scale", "Scale", 1, 64, 4),
    int("octaves", "Octaves", 1, 10, 5),
    float("persistence", "Persistence", 0.05, 1.0, 0.5, 0.01),
    float("lacunarity", "Lacunarity", 1.0, 4.0, 2.0, 0.05),
    int("seed", "Seed", 0, 9999, 1),
    float("contrast", "Contrast", 0.2, 5.0, 1.2, 0.05),
    float("bias", "Bias", -0.5, 0.5, 0.0, 0.01),
    float("warp", "Warp amount", 0.0, 1.0, 0.25, 0.01),
    int("warp_scale", "Warp scale", 1, 32, 3),
];

const FLOW_PARAMS: &[ParamSpec] = &[
    int("scale", "Scale", 1, 64, 4),
    int("octaves", "Octaves", 1, 10, 4),
    float("persistence", "Persistence", 0.05, 1.0, 0.5, 0.01),
    float("lacunarity", "Lacunarity", 1.0, 4.0, 2.0, 0.05),
    int("seed", "Seed", 0, 9999, 1),
    float("contrast", "Contrast", 0.2, 5.0, 1.3, 0.05),
    float("bias", "Bias", -0.5, 0.5, 0.0, 0.01),
    float("flow", "Flow amount", 0.0, 1.0, 0.3, 0.01),
    int("flow_scale", "Flow scale", 1, 16, 2),
];

const CLOUD_PARAMS: &[ParamSpec] = &[
    int("scale", "Scale", 1, 32, 3),
    int("octaves", "Octaves", 1, 10, 6),
    float("persistence", "Persistence", 0.05, 1.0, 0.55, 0.01),
    int("seed", "Seed", 0, 9999, 3),
    float("coverage", "Coverage", 0.0, 1.0, 0.5, 0.01),
    float("softness", "Softness", 0.01, 1.0, 0.35, 0.01),
];

const WORLEY_PARAMS: &[ParamSpec] = &[
    int("scale", "Cells", 1, 64, 8),
    float("jitter", "Jitter", 0.0, 1.0, 1.0, 0.01),
    int("seed", "Seed", 0, 9999, 1),
    float("contrast", "Contrast", 0.2, 6.0, 1.0, 0.05),
    float("bias", "Bias", -0.5, 0.5, 0.0, 0.01),
    boolean("invert", "Invert", false),
];

const WHITE_PARAMS: &[ParamSpec] = &[
    int("grain", "Grain (px)", 1, 16, 1),
    int("seed", "Seed", 0, 9999, 1),
    float("contrast", "Contrast", 0.2, 4.0, 1.0, 0.05),
];

const GABOR_PARAMS: &[ParamSpec] = &[
    int("scale", "Cells", 1, 32, 8),
    float("frequency", "Frequency", 1.0, 40.0, 8.0, 0.5),
    float("spread", "Orientation spread", 0.0, 1.0, 1.0, 0.01),
    float("angle", "Angle", 0.0, 180.0, 0.0, 1.0),
    int("seed", "Seed", 0, 9999, 1),
    float("contrast", "Contrast", 0.2, 5.0, 1.2, 0.05),
];

fn noise_values(a: &Args, basis: Basis) -> Vec<f32> {
    let (scale, oct, pers, lac, seed) = (a.i(0), a.i(1), a.f(2), a.f(3), a.i(4));
    let (contrast, bias) = (a.f(5), a.f(6));
    par_scalar(a.res, |_, _, u, v| {
        shape_value(
            fbm(basis, u, v, scale, seed, oct, pers, lac),
            contrast,
            bias,
        )
    })
}

macro_rules! fbm_eval {
    ($name:ident, $basis:expr) => {
        fn $name(a: &Args) -> Vec<Pixels> {
            let values = noise_values(a, $basis);
            pattern_outputs(a, &values)
        }
    };
}

fbm_eval!(eval_perlin, Basis::Perlin);
fbm_eval!(eval_simplex, Basis::Simplex);
fbm_eval!(eval_value, Basis::Value);
fbm_eval!(eval_ridged, Basis::Ridged);
fbm_eval!(eval_billow, Basis::Billow);
fbm_eval!(eval_turbulence, Basis::Turbulence);

fn eval_warped(a: &Args) -> Vec<Pixels> {
    let (scale, oct, pers, lac, seed) = (a.i(0), a.i(1), a.f(2), a.f(3), a.i(4));
    let (contrast, bias, warp, warp_scale) = (a.f(5), a.f(6), a.f(7), a.i(8));
    let values = par_scalar(a.res, |_, _, u, v| {
        let dx = fbm(Basis::Perlin, u, v, warp_scale, seed + 101, 3, 0.5, 2.0) - 0.5;
        let dy = fbm(Basis::Perlin, u, v, warp_scale, seed + 202, 3, 0.5, 2.0) - 0.5;
        let (wu, wv) = (fract(u + dx * warp * 2.0), fract(v + dy * warp * 2.0));
        shape_value(
            fbm(Basis::Perlin, wu, wv, scale, seed, oct, pers, lac),
            contrast,
            bias,
        )
    });
    pattern_outputs(a, &values)
}

fn eval_flow(a: &Args) -> Vec<Pixels> {
    let (scale, oct, pers, lac, seed) = (a.i(0), a.i(1), a.f(2), a.f(3), a.i(4));
    let (contrast, bias, flow, flow_scale) = (a.f(5), a.f(6), a.f(7), a.i(8));
    let eps = 1.0 / (a.res as f32);
    let values = par_scalar(a.res, |_, _, u, v| {
        // Curl of a periodic noise field displaces the lookup: swirling flow.
        let n = |uu: f32, vv: f32| perlin(uu, vv, flow_scale, seed + 77);
        let ddx = (n(u + eps, v) - n(u - eps, v)) / (2.0 * eps);
        let ddy = (n(u, v + eps) - n(u, v - eps)) / (2.0 * eps);
        let k = flow * 0.08 / flow_scale.max(1) as f32;
        let (wu, wv) = (fract(u + ddy * k), fract(v - ddx * k));
        shape_value(
            fbm(Basis::Simplex, wu, wv, scale, seed, oct, pers, lac),
            contrast,
            bias,
        )
    });
    pattern_outputs(a, &values)
}

fn eval_clouds(a: &Args) -> Vec<Pixels> {
    let (scale, oct, pers, seed) = (a.i(0), a.i(1), a.f(2), a.i(3));
    let (coverage, softness) = (a.f(4), a.f(5));
    let values = par_scalar(a.res, |_, _, u, v| {
        let n = fbm(Basis::Perlin, u, v, scale, seed, oct, pers, 2.0);
        let edge = 1.0 - coverage;
        smoothstep(edge - softness * 0.5, edge + softness * 0.5, n)
    });
    pattern_outputs(a, &values)
}

fn eval_white(a: &Args) -> Vec<Pixels> {
    let (grain, seed, contrast) = (a.i(0).max(1), a.i(1), a.f(2));
    let res = a.res;
    let values = par_scalar(res, |x, y, _, _| {
        let n = rand01((x as i32) / grain, (y as i32) / grain, seed);
        shape_value(n, contrast, 0.0)
    });
    pattern_outputs(a, &values)
}

fn worley_values(a: &Args, mode: usize) -> Vec<f32> {
    let (cells, jitter, seed, contrast, bias, invert) =
        (a.i(0), a.f(1), a.i(2), a.f(3), a.f(4), a.b(5));
    par_scalar(a.res, |_, _, u, v| {
        let c = worley(u, v, cells, seed, jitter, mode == 4);
        let raw = match mode {
            0 => c.f1 * 1.4,
            1 => c.f2 * 0.9,
            2 => (c.f2 - c.f1) * 1.8,
            3 => c.id,
            _ => (c.f2 - c.f1) * 1.2,
        };
        let v = shape_value(clamp01(raw), contrast, bias);
        if invert {
            1.0 - v
        } else {
            v
        }
    })
}

macro_rules! worley_eval {
    ($name:ident, $mode:expr) => {
        fn $name(a: &Args) -> Vec<Pixels> {
            let values = worley_values(a, $mode);
            pattern_outputs(a, &values)
        }
    };
}
worley_eval!(eval_worley_f1, 0);
worley_eval!(eval_worley_f2, 1);
worley_eval!(eval_worley_edges, 2);
worley_eval!(eval_cells, 3);
worley_eval!(eval_crystals, 4);

fn eval_gabor(a: &Args) -> Vec<Pixels> {
    let (cells, freq, spread, angle, seed, contrast) =
        (a.i(0), a.f(1), a.f(2), a.f(3).to_radians(), a.i(4), a.f(5));
    let values = par_scalar(a.res, |_, _, u, v| {
        let x = u * cells as f32;
        let y = v * cells as f32;
        let (ix, iy) = (x.floor() as i32, y.floor() as i32);
        let mut sum = 0.0;
        for oy in -1..=1 {
            for ox in -1..=1 {
                let (cx, cy) = (ix + ox, iy + oy);
                let (wx, wy) = (wrap_i(cx, cells), wrap_i(cy, cells));
                let (rx, ry) = rand2(wx, wy, seed);
                let theta =
                    angle + (rand01(wx, wy, seed + 5) - 0.5) * std::f32::consts::PI * spread;
                let phase = rand01(wx, wy, seed + 9) * std::f32::consts::TAU;
                let dx = x - (cx as f32 + rx);
                let dy = y - (cy as f32 + ry);
                let gauss = (-(dx * dx + dy * dy) * 2.5).exp();
                let wave = (std::f32::consts::TAU * freq / cells as f32
                    * (theta.cos() * dx + theta.sin() * dy)
                    + phase)
                    .cos();
                sum += gauss * wave;
            }
        }
        shape_value(0.5 + sum * 0.35, contrast, 0.0)
    });
    pattern_outputs(a, &values)
}

pub static OPS: &[OpDef] = &[
    op!(
        "perlin_fbm",
        "Perlin FBM",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        NOISE_PARAMS,
        COLORS_AB,
        eval_perlin
    ),
    op!(
        "simplex_fbm",
        "Simplex-like FBM",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        NOISE_PARAMS,
        COLORS_AB,
        eval_simplex
    ),
    op!(
        "value_fbm",
        "Value FBM",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        NOISE_PARAMS,
        COLORS_AB,
        eval_value
    ),
    op!(
        "ridged",
        "Ridged",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        NOISE_PARAMS,
        COLORS_AB,
        eval_ridged
    ),
    op!(
        "billow",
        "Billow",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        NOISE_PARAMS,
        COLORS_AB,
        eval_billow
    ),
    op!(
        "turbulence",
        "Turbulence",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        NOISE_PARAMS,
        COLORS_AB,
        eval_turbulence
    ),
    op!(
        "worley_f1",
        "Worley F1",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        WORLEY_PARAMS,
        COLORS_AB,
        eval_worley_f1
    ),
    op!(
        "worley_f2",
        "Worley F2",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        WORLEY_PARAMS,
        COLORS_AB,
        eval_worley_f2
    ),
    op!(
        "worley_edges",
        "Worley F2-F1",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        WORLEY_PARAMS,
        COLORS_AB,
        eval_worley_edges
    ),
    op!(
        "cells",
        "Cells",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        WORLEY_PARAMS,
        COLORS_AB,
        eval_cells
    ),
    op!(
        "crystals",
        "Crystals",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        WORLEY_PARAMS,
        COLORS_AB,
        eval_crystals
    ),
    op!(
        "warped_fbm",
        "Warped FBM",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        WARP_PARAMS,
        COLORS_AB,
        eval_warped
    ),
    op!(
        "clouds",
        "Clouds",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        CLOUD_PARAMS,
        COLORS_AB,
        eval_clouds
    ),
    op!(
        "flow_noise",
        "Flow Noise",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        FLOW_PARAMS,
        COLORS_AB,
        eval_flow
    ),
    op!(
        "white_noise",
        "White Noise",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        WHITE_PARAMS,
        COLORS_AB,
        eval_white
    ),
    op!(
        "gabor",
        "Gabor",
        "Noise",
        true,
        NO_PORTS,
        OUT_PATTERN,
        GABOR_PARAMS,
        COLORS_AB,
        eval_gabor
    ),
];

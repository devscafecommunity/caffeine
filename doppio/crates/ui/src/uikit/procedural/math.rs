//! Math, colour and channel utilities. Binary ops accept an image or a
//! constant on either side, so `Multiply` with nothing connected to B is a
//! plain scale.

use super::*;

const AB_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("b_value", "B (when unconnected)", 0.0, 1.0, 0.5, 0.01),
];
const IN_AB: &[PortDef] = &[port("A"), port("B")];
const IN_AB_MASK: &[PortDef] = &[port("A"), port("B"), scalar_port("Mask")];
const IN_A: &[PortDef] = &[port("A")];
const NO_COLORS: &[ColorSpec] = &[];

fn operand(a: &Args, input: usize, param: usize) -> Pixels {
    let v = a.f(param);
    a.image(input, [v, v, v, 1.0])
}

fn binary(a: &Args, f: impl Fn(f32, f32) -> f32 + Sync) -> Vec<Pixels> {
    let (x, y) = (operand(a, 0, 0), operand(a, 1, 1));
    let out: Pixels = x
        .iter()
        .zip(y.iter())
        .map(|(p, q)| [f(p[0], q[0]), f(p[1], q[1]), f(p[2], q[2]), p[3]])
        .collect();
    vec![out]
}

fn unary(a: &Args, f: impl Fn(f32) -> f32 + Sync) -> Vec<Pixels> {
    let x = operand(a, 0, 0);
    vec![x
        .iter()
        .map(|p| [f(p[0]), f(p[1]), f(p[2]), p[3]])
        .collect()]
}

fn eval_add(a: &Args) -> Vec<Pixels> {
    binary(a, |x, y| clamp01(x + y))
}
fn eval_sub(a: &Args) -> Vec<Pixels> {
    binary(a, |x, y| clamp01(x - y))
}
fn eval_mul(a: &Args) -> Vec<Pixels> {
    binary(a, |x, y| clamp01(x * y))
}
fn eval_div(a: &Args) -> Vec<Pixels> {
    binary(a, |x, y| clamp01(x / y.max(1e-3)))
}
fn eval_min(a: &Args) -> Vec<Pixels> {
    binary(a, f32::min)
}
fn eval_max(a: &Args) -> Vec<Pixels> {
    binary(a, f32::max)
}

const LERP_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.0, 0.01),
    float("b_value", "B (when unconnected)", 0.0, 1.0, 1.0, 0.01),
    float("factor", "Mix (when no mask)", 0.0, 1.0, 0.5, 0.01),
];

fn eval_lerp(a: &Args) -> Vec<Pixels> {
    let (x, y) = (operand(a, 0, 0), operand(a, 1, 1));
    let mask = a.scalar(2, a.f(2));
    vec![x
        .iter()
        .zip(y.iter())
        .zip(mask.iter())
        .map(|((p, q), m)| mix4(*p, *q, clamp01(*m)))
        .collect()]
}

const ABS_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("center", "Centre", 0.0, 1.0, 0.5, 0.01),
    float("scale", "Scale", 0.1, 8.0, 2.0, 0.05),
];
fn eval_abs(a: &Args) -> Vec<Pixels> {
    let (c, s) = (a.f(1), a.f(2));
    unary(a, |x| clamp01((x - c).abs() * s))
}

const POW_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("exponent", "Exponent", 0.1, 8.0, 2.0, 0.05),
];
fn eval_pow(a: &Args) -> Vec<Pixels> {
    let e = a.f(1);
    unary(a, |x| clamp01(x).powf(e))
}

const CLAMP_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("min", "Min", 0.0, 1.0, 0.2, 0.01),
    float("max", "Max", 0.0, 1.0, 0.8, 0.01),
];
fn eval_clamp(a: &Args) -> Vec<Pixels> {
    let (lo, hi) = (a.f(1).min(a.f(2)), a.f(1).max(a.f(2)));
    unary(a, |x| x.clamp(lo, hi))
}

const REMAP_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("in_min", "In min", 0.0, 1.0, 0.0, 0.01),
    float("in_max", "In max", 0.0, 1.0, 1.0, 0.01),
    float("out_min", "Out min", 0.0, 1.0, 0.0, 0.01),
    float("out_max", "Out max", 0.0, 1.0, 1.0, 0.01),
];
fn eval_remap(a: &Args) -> Vec<Pixels> {
    let (i0, i1, o0, o1) = (a.f(1), a.f(2), a.f(3), a.f(4));
    unary(a, |x| {
        let t = if (i1 - i0).abs() < 1e-6 {
            0.0
        } else {
            (x - i0) / (i1 - i0)
        };
        clamp01(o0 + (o1 - o0) * t.clamp(0.0, 1.0))
    })
}

const SMOOTH_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("edge0", "Edge 0", 0.0, 1.0, 0.3, 0.01),
    float("edge1", "Edge 1", 0.0, 1.0, 0.7, 0.01),
];
fn eval_smoothstep(a: &Args) -> Vec<Pixels> {
    let (e0, e1) = (a.f(1), a.f(2));
    unary(a, |x| smoothstep(e0, e1, x))
}

const STEP_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("threshold", "Threshold", 0.0, 1.0, 0.5, 0.01),
];
fn eval_step(a: &Args) -> Vec<Pixels> {
    let t = a.f(1);
    unary(a, |x| if x >= t { 1.0 } else { 0.0 })
}

fn eval_one_minus(a: &Args) -> Vec<Pixels> {
    unary(a, |x| 1.0 - x)
}

const SIN_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("frequency", "Frequency", 0.5, 32.0, 4.0, 0.1),
    float("phase", "Phase", 0.0, std::f32::consts::TAU, 0.0, 0.05),
];
fn eval_sin(a: &Args) -> Vec<Pixels> {
    let (f, ph) = (a.f(1), a.f(2));
    unary(a, |x| {
        0.5 + 0.5 * (std::f32::consts::TAU * f * x + ph).sin()
    })
}

const FRAC_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("multiplier", "Multiplier", 1.0, 32.0, 4.0, 0.1),
];
fn eval_frac(a: &Args) -> Vec<Pixels> {
    let m = a.f(1);
    unary(a, |x| fract(x * m))
}

const POSTER_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    int("levels", "Levels", 2, 32, 5),
];
fn eval_posterize(a: &Args) -> Vec<Pixels> {
    let n = a.i(1).max(2) as f32 - 1.0;
    unary(a, |x| (clamp01(x) * n).round() / n)
}

const INVERT_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    boolean("alpha", "Invert alpha", false),
];
fn eval_invert(a: &Args) -> Vec<Pixels> {
    let alpha = a.b(1);
    let x = operand(a, 0, 0);
    vec![x
        .iter()
        .map(|p| {
            [
                1.0 - p[0],
                1.0 - p[1],
                1.0 - p[2],
                if alpha { 1.0 - p[3] } else { p[3] },
            ]
        })
        .collect()]
}

const AUTO_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("clip", "Clip percentile", 0.0, 0.2, 0.01, 0.005),
];
fn eval_auto_levels(a: &Args) -> Vec<Pixels> {
    let clip = a.f(1);
    let x = operand(a, 0, 0);
    let mut hist = [0u32; 256];
    for p in &x {
        hist[(clamp01(luma(*p)) * 255.0) as usize] += 1;
    }
    let total = x.len() as f32;
    let (mut lo, mut hi, mut acc) = (0usize, 255usize, 0.0f32);
    for (i, h) in hist.iter().enumerate() {
        acc += *h as f32;
        if acc / total >= clip {
            lo = i;
            break;
        }
    }
    acc = 0.0;
    for i in (0..256).rev() {
        acc += hist[i] as f32;
        if acc / total >= clip {
            hi = i;
            break;
        }
    }
    let (lo, hi) = (
        lo as f32 / 255.0,
        (hi as f32 / 255.0).max(lo as f32 / 255.0 + 0.004),
    );
    vec![x
        .iter()
        .map(|p| {
            let m = |c: f32| clamp01((c - lo) / (hi - lo));
            [m(p[0]), m(p[1]), m(p[2]), p[3]]
        })
        .collect()]
}

const HIST_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("position", "Position", 0.0, 1.0, 0.5, 0.01),
    float("range", "Range", 0.0, 1.0, 0.3, 0.01),
    float("softness", "Softness", 0.0, 1.0, 0.2, 0.01),
];
fn eval_hist_range(a: &Args) -> Vec<Pixels> {
    let (pos, range, soft) = (a.f(1), a.f(2), a.f(3));
    let x = operand(a, 0, 0);
    vec![x
        .iter()
        .map(|p| {
            let d = (luma(*p) - pos).abs();
            gray(
                1.0 - smoothstep(
                    range * 0.5 - soft * 0.25,
                    range * 0.5 + soft * 0.25 + 1e-4,
                    d,
                ),
            )
        })
        .collect()]
}

const CONTRAST_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("contrast", "Contrast", 0.0, 4.0, 1.4, 0.01),
    float("brightness", "Brightness", -1.0, 1.0, 0.0, 0.01),
    float("pivot", "Pivot", 0.0, 1.0, 0.5, 0.01),
];
fn eval_contrast(a: &Args) -> Vec<Pixels> {
    let (c, b, pv) = (a.f(1), a.f(2), a.f(3));
    unary(a, |x| clamp01((x - pv) * c + pv + b))
}

const SPLIT_OUT: &[PortDef] = &[
    scalar_port("R"),
    scalar_port("G"),
    scalar_port("B"),
    scalar_port("A"),
];
fn eval_split(a: &Args) -> Vec<Pixels> {
    let x = operand(a, 0, 0);
    (0..4)
        .map(|c| x.iter().map(|p| gray(p[c])).collect())
        .collect()
}

const MERGE_IN: &[PortDef] = &[
    scalar_port("R"),
    scalar_port("G"),
    scalar_port("B"),
    scalar_port("A"),
];
fn eval_merge(a: &Args) -> Vec<Pixels> {
    let r = a.scalar(0, 0.0);
    let g = a.scalar(1, 0.0);
    let b = a.scalar(2, 0.0);
    let al = a.scalar(3, 1.0);
    vec![(0..r.len()).map(|k| [r[k], g[k], b[k], al[k]]).collect()]
}

const GRADMAP_COLORS: &[ColorSpec] = &[
    color("c0", "Stop 0", [0.02, 0.01, 0.01, 1.0]),
    color("c1", "Stop 1", [0.35, 0.12, 0.05, 1.0]),
    color("c2", "Stop 2", [0.75, 0.38, 0.12, 1.0]),
    color("c3", "Stop 3", [0.95, 0.75, 0.40, 1.0]),
    color("c4", "Stop 4", [1.0, 0.97, 0.88, 1.0]),
];
const GRADMAP_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("p0", "Position 0", 0.0, 1.0, 0.0, 0.01),
    float("p1", "Position 1", 0.0, 1.0, 0.25, 0.01),
    float("p2", "Position 2", 0.0, 1.0, 0.5, 0.01),
    float("p3", "Position 3", 0.0, 1.0, 0.75, 0.01),
    float("p4", "Position 4", 0.0, 1.0, 1.0, 0.01),
];
fn eval_gradient_map(a: &Args) -> Vec<Pixels> {
    let mut stops: Vec<(f32, [f32; 4])> = (0..5).map(|i| (a.f(1 + i), a.color(i))).collect();
    stops.sort_by(|x, y| x.0.partial_cmp(&y.0).unwrap_or(std::cmp::Ordering::Equal));
    let x = operand(a, 0, 0);
    vec![x
        .iter()
        .map(|p| {
            let t = clamp01(luma(*p));
            let mut out = stops[0].1;
            if t >= stops[4].0 {
                out = stops[4].1;
            } else if t > stops[0].0 {
                for w in stops.windows(2) {
                    if t >= w[0].0 && t <= w[1].0 {
                        let span = (w[1].0 - w[0].0).max(1e-5);
                        out = mix4(w[0].1, w[1].1, (t - w[0].0) / span);
                        break;
                    }
                }
            }
            [out[0], out[1], out[2], p[3]]
        })
        .collect()]
}

const COLOURIZE_COLORS: &[ColorSpec] = &[color("tint", "Tint", [0.9, 0.55, 0.25, 1.0])];
const COLOURIZE_P: &[ParamSpec] = &[
    float("a_value", "A (when unconnected)", 0.0, 1.0, 0.5, 0.01),
    float("strength", "Strength", 0.0, 1.0, 1.0, 0.01),
];
fn eval_colourize(a: &Args) -> Vec<Pixels> {
    let (strength, tint) = (a.f(1), a.color(0));
    let x = operand(a, 0, 0);
    vec![x
        .iter()
        .map(|p| {
            let l = luma(*p);
            // Black → tint → white, so highlights stay bright.
            let c = if l < 0.5 {
                [tint[0] * l * 2.0, tint[1] * l * 2.0, tint[2] * l * 2.0]
            } else {
                let t = (l - 0.5) * 2.0;
                [
                    tint[0] + (1.0 - tint[0]) * t,
                    tint[1] + (1.0 - tint[1]) * t,
                    tint[2] + (1.0 - tint[2]) * t,
                ]
            };
            [
                p[0] + (c[0] - p[0]) * strength,
                p[1] + (c[1] - p[1]) * strength,
                p[2] + (c[2] - p[2]) * strength,
                p[3],
            ]
        })
        .collect()]
}

pub static OPS: &[OpDef] = &[
    op!("math_add", "Add", "Math", false, IN_AB, OUT_COLOR, AB_P, NO_COLORS, eval_add),
    op!(
        "math_subtract",
        "Subtract",
        "Math",
        false,
        IN_AB,
        OUT_COLOR,
        AB_P,
        NO_COLORS,
        eval_sub
    ),
    op!(
        "math_multiply",
        "Multiply",
        "Math",
        false,
        IN_AB,
        OUT_COLOR,
        AB_P,
        NO_COLORS,
        eval_mul
    ),
    op!(
        "math_divide",
        "Divide",
        "Math",
        false,
        IN_AB,
        OUT_COLOR,
        AB_P,
        NO_COLORS,
        eval_div
    ),
    op!(
        "math_lerp",
        "Lerp",
        "Math",
        false,
        IN_AB_MASK,
        OUT_COLOR,
        LERP_P,
        NO_COLORS,
        eval_lerp
    ),
    op!("math_min", "Min", "Math", false, IN_AB, OUT_COLOR, AB_P, NO_COLORS, eval_min),
    op!("math_max", "Max", "Math", false, IN_AB, OUT_COLOR, AB_P, NO_COLORS, eval_max),
    op!("math_abs", "Abs", "Math", false, IN_A, OUT_COLOR, ABS_P, NO_COLORS, eval_abs),
    op!(
        "math_power",
        "Power",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        POW_P,
        NO_COLORS,
        eval_pow
    ),
    op!(
        "math_clamp",
        "Clamp",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        CLAMP_P,
        NO_COLORS,
        eval_clamp
    ),
    op!(
        "math_remap",
        "Remap",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        REMAP_P,
        NO_COLORS,
        eval_remap
    ),
    op!(
        "math_smoothstep",
        "Smoothstep",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        SMOOTH_P,
        NO_COLORS,
        eval_smoothstep
    ),
    op!(
        "math_step",
        "Step",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        STEP_P,
        NO_COLORS,
        eval_step
    ),
    op!(
        "math_one_minus",
        "One Minus",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        AB_P,
        NO_COLORS,
        eval_one_minus
    ),
    op!("math_sin", "Sine", "Math", false, IN_A, OUT_COLOR, SIN_P, NO_COLORS, eval_sin),
    op!(
        "math_frac",
        "Frac",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        FRAC_P,
        NO_COLORS,
        eval_frac
    ),
    op!(
        "math_posterize",
        "Posterize",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        POSTER_P,
        NO_COLORS,
        eval_posterize
    ),
    op!(
        "math_invert",
        "Invert",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        INVERT_P,
        NO_COLORS,
        eval_invert
    ),
    op!(
        "auto_levels",
        "Auto Levels",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        AUTO_P,
        NO_COLORS,
        eval_auto_levels
    ),
    op!(
        "histogram_range",
        "Histogram Range",
        "Math",
        false,
        IN_A,
        OUT_VALUE,
        HIST_P,
        NO_COLORS,
        eval_hist_range
    ),
    op!(
        "math_contrast",
        "Contrast",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        CONTRAST_P,
        NO_COLORS,
        eval_contrast
    ),
    op!(
        "channel_split",
        "Channel Split",
        "Math",
        false,
        IN_A,
        SPLIT_OUT,
        AB_P,
        NO_COLORS,
        eval_split
    ),
    op!(
        "channel_merge",
        "Channel Merge",
        "Math",
        false,
        MERGE_IN,
        OUT_COLOR,
        &[],
        NO_COLORS,
        eval_merge
    ),
    op!(
        "gradient_map",
        "Gradient Map",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        GRADMAP_P,
        GRADMAP_COLORS,
        eval_gradient_map
    ),
    op!(
        "colourize",
        "Colourize",
        "Math",
        false,
        IN_A,
        OUT_COLOR,
        COLOURIZE_P,
        COLOURIZE_COLORS,
        eval_colourize
    ),
];

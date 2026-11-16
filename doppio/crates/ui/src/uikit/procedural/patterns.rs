//! Regular patterns: tiles, bricks, planks, weaves, grids, plates…
//! All patterns tile seamlessly for integer counts.

use super::*;

const TAU: f32 = std::f32::consts::TAU;
const SQRT3: f32 = 1.732_050_8;

const TILE_COLORS: &[ColorSpec] = &[
    color("color_a", "Grout", [0.10, 0.10, 0.10, 1.0]),
    color("color_b", "Tile", [0.78, 0.76, 0.72, 1.0]),
];
const BRICK_COLORS: &[ColorSpec] = &[
    color("color_a", "Mortar", [0.55, 0.53, 0.50, 1.0]),
    color("color_b", "Brick", [0.55, 0.20, 0.13, 1.0]),
];
const WOOD_FLOOR_COLORS: &[ColorSpec] = &[
    color("color_a", "Gap", [0.08, 0.05, 0.03, 1.0]),
    color("color_b", "Wood", [0.50, 0.32, 0.18, 1.0]),
];
const METAL_PLATE_COLORS: &[ColorSpec] = &[
    color("color_a", "Base", [0.30, 0.31, 0.33, 1.0]),
    color("color_b", "Raised", [0.70, 0.72, 0.75, 1.0]),
];

/// Height of a flat rectangular tile with a bevel: 0 in the gap, rising to 1.
fn bevel_height(edge: f32, gap: f32, bevel: f32) -> f32 {
    smoothstep(gap * 0.5, gap * 0.5 + bevel.max(0.0005), edge)
}

fn even(value: i32) -> i32 {
    let v = value.max(2);
    v + (v & 1)
}

// ------------------------------------------------------------ tiles/bricks

const TILE_P: &[ParamSpec] = &[
    int("columns", "Columns", 1, 32, 6),
    int("rows", "Rows", 1, 32, 6),
    float("gap", "Gap", 0.0, 0.4, 0.06, 0.005),
    float("bevel", "Bevel", 0.0, 0.4, 0.06, 0.005),
    float("stagger", "Row stagger", 0.0, 1.0, 0.0, 0.01),
    float("variation", "Variation", 0.0, 1.0, 0.35, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const BRICK_P: &[ParamSpec] = &[
    int("columns", "Columns", 1, 32, 4),
    int("rows", "Rows", 2, 48, 12),
    float("gap", "Mortar", 0.0, 0.4, 0.08, 0.005),
    float("bevel", "Bevel", 0.0, 0.4, 0.05, 0.005),
    float("stagger", "Row stagger", 0.0, 1.0, 0.5, 0.01),
    float("variation", "Variation", 0.0, 1.0, 0.5, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn grid_tiles(a: &Args) -> Vec<Pixels> {
    let (cols, rows0, gap, bevel, stagger, var, seed) = (
        a.i(0).max(1),
        a.i(1).max(1),
        a.f(2),
        a.f(3),
        a.f(4),
        a.f(5),
        a.i(6),
    );
    let rows = if stagger > 0.0 { even(rows0) } else { rows0 };
    let res = a.res;
    let rows_px = par_fill(res, |_, _, u, v| {
        let y = v * rows as f32;
        let j = y.floor() as i32;
        let shift = if j & 1 == 1 { stagger } else { 0.0 };
        let x = u * cols as f32 + shift;
        let i = x.floor() as i32;
        let (fx, fy) = (x - i as f32, y - j as f32);
        // Edge distance in units of the smaller cell side (0..0.5), so gap and
        // bevel stay uniform for any aspect ratio.
        let edge = (fx.min(1.0 - fx) / cols as f32).min(fy.min(1.0 - fy) / rows as f32)
            * cols.max(rows) as f32;
        let id = rand01(wrap_i(i, cols), wrap_i(j, rows), seed);
        let h = bevel_height(edge, gap, bevel);
        [h * (1.0 - var * 0.5 + var * 0.5 * id), id, 0.0, 1.0]
    });
    let values: Vec<f32> = rows_px.iter().map(|p| p[0]).collect();
    let ids: Vec<f32> = rows_px.iter().map(|p| p[1]).collect();
    let mut out = pattern_outputs_var(a, &values, &ids);
    // Colour: per-tile tone variation on top of the A/B mix.
    let (ca, cb) = (a.color(0), a.color(1));
    for (k, pixel) in out[0].iter_mut().enumerate() {
        let tone = 1.0 - var * 0.35 + var * 0.35 * ids[k];
        let h = clamp01(values[k]);
        let covered = if h > 0.001 { 1.0 } else { 0.0 };
        let tile = [cb[0] * tone, cb[1] * tone, cb[2] * tone, cb[3]];
        *pixel = mix4(ca, tile, covered * smoothstep(0.0, 0.2, h));
    }
    out
}

// -------------------------------------------------------------------- hex

const HEX_P: &[ParamSpec] = &[
    int("columns", "Columns", 1, 32, 6),
    int("rows", "Rows", 1, 32, 4),
    float("gap", "Gap", 0.0, 0.4, 0.06, 0.005),
    float("bevel", "Bevel", 0.0, 0.4, 0.07, 0.005),
    float("variation", "Variation", 0.0, 1.0, 0.4, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_hex(a: &Args) -> Vec<Pixels> {
    let (cols, rows, gap, bevel, var, seed) =
        (a.i(0).max(1), a.i(1).max(1), a.f(2), a.f(3), a.f(4), a.i(5));
    let rows_px = par_fill(a.res, |_, _, u, v| {
        let (px, py) = (u * cols as f32, v * rows as f32 * SQRT3);
        let center = |ox: f32, oy: f32| {
            let i = (px - ox).round();
            let j = ((py - oy) / SQRT3).round();
            (i, j, i + ox, j * SQRT3 + oy)
        };
        let (ia, ja, ax, ay) = center(0.0, 0.0);
        let (ib, jb, bx, by) = center(0.5, SQRT3 * 0.5);
        let da = (px - ax).powi(2) + (py - ay).powi(2);
        let db = (px - bx).powi(2) + (py - by).powi(2);
        let (ci, cj, cx, cy, tag) = if da <= db {
            (ia as i32, ja as i32, ax, ay, 0)
        } else {
            (ib as i32, jb as i32, bx, by, 1)
        };
        let (rx, ry) = ((px - cx).abs(), (py - cy).abs());
        let hexdist = (rx).max(rx * 0.5 + ry * SQRT3 * 0.5);
        let edge = 0.5 - hexdist;
        let id = rand01(wrap_i(ci, cols), wrap_i(cj, rows), seed + tag * 977);
        let h = bevel_height(edge, gap, bevel);
        [h * (1.0 - var * 0.5 + var * 0.5 * id), id, 0.0, 1.0]
    });
    let values: Vec<f32> = rows_px.iter().map(|p| p[0]).collect();
    let ids: Vec<f32> = rows_px.iter().map(|p| p[1]).collect();
    pattern_outputs_var(a, &values, &ids)
}

// ------------------------------------------------------------ herringbone

const HERRING_P: &[ParamSpec] = &[
    int("cells", "Cells (x4)", 4, 64, 8),
    float("gap", "Gap", 0.0, 0.4, 0.06, 0.005),
    float("bevel", "Bevel", 0.0, 0.4, 0.05, 0.005),
    float("variation", "Variation", 0.0, 1.0, 0.45, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_herringbone(a: &Args) -> Vec<Pixels> {
    let n = ((a.i(0).max(4) + 2) / 4) * 4;
    let (gap, bevel, var, seed) = (a.f(1), a.f(2), a.f(3), a.i(4));
    let rows_px = par_fill(a.res, |_, _, u, v| {
        let (x, y) = (u * n as f32, v * n as f32);
        let (i, j) = (x.floor() as i32, y.floor() as i32);
        let s = wrap_i(i, n) + wrap_i(j, n);
        let band_even = (s / 2) & 1 == 0;
        let (ax, ay) = if s & 1 == 0 {
            (i, j)
        } else if band_even {
            (i - 1, j)
        } else {
            (i, j - 1)
        };
        let (lx, ly) = (x - ax as f32, y - ay as f32);
        let (w, h) = if band_even { (2.0, 1.0) } else { (1.0, 2.0) };
        let edge = (lx.min(w - lx)).min(ly.min(h - ly));
        let id = rand01(wrap_i(ax, n), wrap_i(ay, n), seed);
        let height = bevel_height(edge, gap, bevel);
        [height * (1.0 - var * 0.5 + var * 0.5 * id), id, 0.0, 1.0]
    });
    let values: Vec<f32> = rows_px.iter().map(|p| p[0]).collect();
    let ids: Vec<f32> = rows_px.iter().map(|p| p[1]).collect();
    pattern_outputs_var(a, &values, &ids)
}

// ------------------------------------------------------------ basket weave

const BASKET_P: &[ParamSpec] = &[
    int("blocks", "Blocks", 2, 32, 4),
    int("planks", "Planks per block", 2, 8, 3),
    float("gap", "Gap", 0.0, 0.4, 0.06, 0.005),
    float("bevel", "Bevel", 0.0, 0.4, 0.05, 0.005),
    float("variation", "Variation", 0.0, 1.0, 0.45, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_basket(a: &Args) -> Vec<Pixels> {
    let blocks = even(a.i(0));
    let (planks, gap, bevel, var, seed) = (a.i(1).max(1), a.f(2), a.f(3), a.f(4), a.i(5));
    let rows_px = par_fill(a.res, |_, _, u, v| {
        let (x, y) = (u * blocks as f32, v * blocks as f32);
        let (bi, bj) = (x.floor() as i32, y.floor() as i32);
        let (fx, fy) = (x - bi as f32, y - bj as f32);
        let horizontal = (bi + bj) & 1 == 0;
        let (lane, along, across) = if horizontal {
            (fy, fx, fy)
        } else {
            (fx, fy, fx)
        };
        let k = (lane * planks as f32).floor() as i32;
        let local = lane * planks as f32 - k as f32;
        let _ = across;
        let edge = (along.min(1.0 - along)).min(local.min(1.0 - local) / planks as f32);
        let id = rand01(
            wrap_i(bi, blocks) * 8 + k,
            wrap_i(bj, blocks) * 2 + horizontal as i32,
            seed,
        );
        let height = bevel_height(edge, gap, bevel);
        [height * (1.0 - var * 0.5 + var * 0.5 * id), id, 0.0, 1.0]
    });
    let values: Vec<f32> = rows_px.iter().map(|p| p[0]).collect();
    let ids: Vec<f32> = rows_px.iter().map(|p| p[1]).collect();
    pattern_outputs_var(a, &values, &ids)
}

// ------------------------------------------------ stripes / rings / waves

const STRIPE_P: &[ParamSpec] = &[
    int("count", "Count", 1, 64, 8),
    float("width", "Width", 0.02, 1.0, 0.5, 0.01),
    float("softness", "Softness", 0.0, 0.5, 0.04, 0.005),
    choice(
        "orientation",
        "Orientation",
        &["Horizontal", "Vertical", "Diagonal"],
        0,
    ),
    float("wobble", "Wobble", 0.0, 1.0, 0.0, 0.01),
    int("wobble_scale", "Wobble scale", 1, 16, 3),
    int("seed", "Seed", 0, 9999, 1),
];

fn line_profile(t: f32, width: f32, soft: f32) -> f32 {
    let d = (fract(t) - 0.5).abs() * 2.0;
    1.0 - smoothstep(width - soft, width + soft + 1e-4, d)
}

fn eval_stripes(a: &Args) -> Vec<Pixels> {
    let (count, width, soft, orient, wobble, wscale, seed) =
        (a.i(0), a.f(1), a.f(2), a.i(3), a.f(4), a.i(5), a.i(6));
    let values = par_scalar(a.res, |_, _, u, v| {
        let n = (perlin(u, v, wscale, seed) - 0.5) * wobble * 3.0;
        let t = match orient {
            0 => v * count as f32 + n,
            1 => u * count as f32 + n,
            _ => (u + v) * count as f32 + n,
        };
        line_profile(t, width, soft)
    });
    pattern_outputs(a, &values)
}

const RING_P: &[ParamSpec] = &[
    int("cells", "Cells", 1, 8, 1),
    int("count", "Rings", 1, 32, 6),
    float("width", "Width", 0.02, 1.0, 0.5, 0.01),
    float("softness", "Softness", 0.0, 0.5, 0.05, 0.005),
    float("distort", "Distortion", 0.0, 1.0, 0.0, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_rings(a: &Args) -> Vec<Pixels> {
    let (cells, count, width, soft, distort, seed) =
        (a.i(0), a.i(1), a.f(2), a.f(3), a.f(4), a.i(5));
    let values = par_scalar(a.res, |_, _, u, v| {
        let n = (perlin(u, v, 4 * cells.max(1), seed) - 0.5) * distort * 0.2;
        let (x, y) = (u * cells as f32, v * cells as f32);
        let (fx, fy) = (x - x.floor() - 0.5, y - y.floor() - 0.5);
        let d = (fx * fx + fy * fy).sqrt() * 2.0 + n;
        line_profile(d * count as f32 * 0.5 * 2.0, width, soft)
    });
    pattern_outputs(a, &values)
}

const WAVE_P: &[ParamSpec] = &[
    int("count", "Lines", 1, 48, 6),
    int("waves", "Waves", 1, 24, 3),
    float("amplitude", "Amplitude", 0.0, 1.0, 0.25, 0.01),
    float("width", "Width", 0.02, 1.0, 0.45, 0.01),
    float("softness", "Softness", 0.0, 0.5, 0.05, 0.005),
    float("phase", "Phase", 0.0, std::f32::consts::TAU, 0.0, 0.05),
];

fn eval_waves(a: &Args) -> Vec<Pixels> {
    let (count, waves, amp, width, soft, phase) = (a.i(0), a.i(1), a.f(2), a.f(3), a.f(4), a.f(5));
    let values = par_scalar(a.res, |_, _, u, v| {
        let t = v * count as f32 + amp * (TAU * waves as f32 * u + phase).sin();
        line_profile(t, width, soft)
    });
    pattern_outputs(a, &values)
}

// ------------------------------------------------------ dots / grid / truchet

const DOT_P: &[ParamSpec] = &[
    int("count", "Count", 1, 64, 8),
    float("radius", "Radius", 0.02, 0.7, 0.3, 0.01),
    float("softness", "Softness", 0.0, 0.5, 0.04, 0.005),
    boolean("stagger", "Stagger rows", false),
    float("variation", "Size variation", 0.0, 1.0, 0.0, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_dots(a: &Args) -> Vec<Pixels> {
    let count = a.i(0).max(1);
    let (radius, soft, stagger, var, seed) = (a.f(1), a.f(2), a.b(3), a.f(4), a.i(5));
    let count = if stagger { even(count) } else { count };
    let values = par_scalar(a.res, |_, _, u, v| {
        let y = v * count as f32;
        let j = y.floor() as i32;
        let shift = if stagger && j & 1 == 1 { 0.5 } else { 0.0 };
        let x = u * count as f32 + shift;
        let i = x.floor() as i32;
        let (fx, fy) = (x - i as f32 - 0.5, y - j as f32 - 0.5);
        let r = radius * (1.0 - var * rand01(wrap_i(i, count), wrap_i(j, count), seed));
        let d = (fx * fx + fy * fy).sqrt();
        1.0 - smoothstep(r - soft, r + soft + 1e-4, d)
    });
    pattern_outputs(a, &values)
}

const GRID_P: &[ParamSpec] = &[
    int("count", "Cells", 1, 64, 8),
    float("width", "Line width", 0.005, 0.5, 0.06, 0.005),
    float("softness", "Softness", 0.0, 0.3, 0.01, 0.005),
    boolean("invert", "Invert", false),
];

fn eval_grid(a: &Args) -> Vec<Pixels> {
    let (count, width, soft, invert) = (a.i(0).max(1), a.f(1), a.f(2), a.b(3));
    let values = par_scalar(a.res, |_, _, u, v| {
        let (x, y) = (u * count as f32, v * count as f32);
        let d = (fract(x).min(1.0 - fract(x))).min(fract(y).min(1.0 - fract(y)));
        let line = 1.0 - smoothstep(width * 0.5 - soft, width * 0.5 + soft + 1e-4, d);
        if invert {
            1.0 - line
        } else {
            line
        }
    });
    pattern_outputs(a, &values)
}

const TRUCHET_P: &[ParamSpec] = &[
    int("cells", "Cells", 2, 32, 6),
    float("width", "Line width", 0.02, 0.5, 0.14, 0.005),
    float("softness", "Softness", 0.0, 0.2, 0.015, 0.005),
    choice("style", "Style", &["Arcs", "Diagonals"], 0),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_truchet(a: &Args) -> Vec<Pixels> {
    let (cells, width, soft, style, seed) = (a.i(0).max(1), a.f(1), a.f(2), a.i(3), a.i(4));
    let values = par_scalar(a.res, |_, _, u, v| {
        let (x, y) = (u * cells as f32, v * cells as f32);
        let (i, j) = (x.floor() as i32, y.floor() as i32);
        let (fx, fy) = (x - i as f32, y - j as f32);
        let flip = rand01(wrap_i(i, cells), wrap_i(j, cells), seed) < 0.5;
        let d = if style == 0 {
            let (c1, c2) = if flip {
                ((0.0, 0.0), (1.0, 1.0))
            } else {
                ((1.0, 0.0), (0.0, 1.0))
            };
            let d1 = (((fx - c1.0).powi(2) + (fy - c1.1).powi(2)).sqrt() - 0.5).abs();
            let d2 = (((fx - c2.0).powi(2) + (fy - c2.1).powi(2)).sqrt() - 0.5).abs();
            d1.min(d2)
        } else if flip {
            (fx - fy).abs() * std::f32::consts::FRAC_1_SQRT_2
        } else {
            (fx + fy - 1.0).abs() * std::f32::consts::FRAC_1_SQRT_2
        };
        1.0 - smoothstep(width * 0.5 - soft, width * 0.5 + soft + 1e-4, d)
    });
    pattern_outputs(a, &values)
}

// ------------------------------------------------------ scales / cobbles

const SCALE_P: &[ParamSpec] = &[
    int("columns", "Columns", 2, 32, 8),
    int("rows", "Rows", 2, 64, 16),
    float("rim", "Rim", 0.0, 1.0, 0.35, 0.01),
    float("variation", "Variation", 0.0, 1.0, 0.4, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_scales(a: &Args) -> Vec<Pixels> {
    let (cols, rows, rim, var, seed) = (a.i(0).max(1), even(a.i(1)), a.f(2), a.f(3), a.i(4));
    let rows_px = par_fill(a.res, |_, _, u, v| {
        let (gx, gy) = (u * cols as f32, v * rows as f32 * 0.5);
        let base_row = (gy * 2.0).floor() as i32;
        // Lower rows overlap upper rows: the first hit from the bottom wins.
        for r in (base_row - 2..=base_row + 3).rev() {
            let off = if r & 1 == 1 { 0.5 } else { 0.0 };
            let ci = (gx - off - 0.5).round();
            let cx = ci + 0.5 + off;
            let cy = r as f32 * 0.5;
            let d = ((gx - cx).powi(2) + (gy - cy).powi(2)).sqrt();
            if d < 0.5 {
                let id = rand01(wrap_i(ci as i32, cols), wrap_i(r, rows), seed);
                let dome = 1.0 - (d / 0.5).powi(2);
                let edge = smoothstep(0.0, 0.18, 0.5 - d);
                let h = (0.35 + 0.65 * dome) * (1.0 - rim * (1.0 - edge));
                return [h * (1.0 - var * 0.4 + var * 0.4 * id), id, 0.0, 1.0];
            }
        }
        [0.0, 0.0, 0.0, 1.0]
    });
    let values: Vec<f32> = rows_px.iter().map(|p| p[0]).collect();
    let ids: Vec<f32> = rows_px.iter().map(|p| p[1]).collect();
    pattern_outputs_var(a, &values, &ids)
}

const COBBLE_P: &[ParamSpec] = &[
    int("cells", "Stones", 2, 48, 8),
    float("jitter", "Jitter", 0.0, 1.0, 0.85, 0.01),
    float("gap", "Gap", 0.0, 0.5, 0.1, 0.005),
    float("roundness", "Roundness", 0.02, 1.0, 0.45, 0.01),
    float("variation", "Height variation", 0.0, 1.0, 0.5, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_cobbles(a: &Args) -> Vec<Pixels> {
    let (cells, jitter, gap, round, var, seed) = (a.i(0), a.f(1), a.f(2), a.f(3), a.f(4), a.i(5));
    let rows_px = par_fill(a.res, |_, _, u, v| {
        let c = worley(u, v, cells, seed, jitter, false);
        let edge = c.f2 - c.f1;
        let dome = smoothstep(gap * 0.5, gap * 0.5 + round * 0.5, edge);
        let shape = dome.powf(0.7);
        [shape * (1.0 - var * 0.5 + var * 0.5 * c.id), c.id, 0.0, 1.0]
    });
    let values: Vec<f32> = rows_px.iter().map(|p| p[0]).collect();
    let ids: Vec<f32> = rows_px.iter().map(|p| p[1]).collect();
    pattern_outputs_var(a, &values, &ids)
}

// -------------------------------------------------------------------- planks

const PLANK_P: &[ParamSpec] = &[
    int("planks", "Planks (rows)", 1, 48, 8),
    int("segments", "Segments per row", 1, 16, 2),
    float("gap", "Gap", 0.0, 0.1, 0.008, 0.001),
    float("bevel", "Bevel", 0.0, 0.1, 0.006, 0.001),
    float("variation", "Tone variation", 0.0, 1.0, 0.5, 0.01),
    float("grain", "Grain", 0.0, 1.0, 0.35, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_planks(a: &Args) -> Vec<Pixels> {
    let (planks, segs, gap, bevel, var, grain, seed) = (
        a.i(0).max(1),
        a.i(1).max(1),
        a.f(2),
        a.f(3),
        a.f(4),
        a.f(5),
        a.i(6),
    );
    let rows_px = par_fill(a.res, |_, _, u, v| {
        let y = v * planks as f32;
        let j = y.floor() as i32;
        let ly = y - j as f32;
        let off = rand01(wrap_i(j, planks), 11, seed);
        let x = u * segs as f32 + off;
        let i = x.floor() as i32;
        let lx = x - i as f32;
        let edge = (lx.min(1.0 - lx) / segs as f32).min(ly.min(1.0 - ly) / planks as f32);
        let id = rand01(wrap_i(i, segs), wrap_i(j, planks), seed + 3);
        let g = value_noise2(
            u,
            v,
            2 + (id * 3.0) as i32,
            planks * 18,
            seed + 7 + (id * 50.0) as i32,
        );
        let h = bevel_height(edge, gap, bevel);
        let tone = 1.0 - var * 0.5 + var * 0.5 * id;
        [
            h * tone * (1.0 - grain * 0.5 + grain * 0.5 * g),
            id,
            0.0,
            1.0,
        ]
    });
    let values: Vec<f32> = rows_px.iter().map(|p| p[0]).collect();
    let ids: Vec<f32> = rows_px.iter().map(|p| p[1]).collect();
    pattern_outputs_var(a, &values, &ids)
}

// ------------------------------------------- corrugated / plate / perforated

const CORRUGATED_P: &[ParamSpec] = &[
    int("count", "Ridges", 1, 64, 8),
    choice("profile", "Profile", &["Sine", "Trapezoid", "Triangle"], 0),
    choice("orientation", "Orientation", &["Vertical", "Horizontal"], 0),
    float("sharpness", "Flat top", 0.0, 0.9, 0.35, 0.01),
];

fn eval_corrugated(a: &Args) -> Vec<Pixels> {
    let (count, profile, orient, flat) = (a.i(0).max(1), a.i(1), a.i(2), a.f(3));
    let values = par_scalar(a.res, |_, _, u, v| {
        let t = fract(if orient == 0 { u } else { v } * count as f32);
        match profile {
            0 => 0.5 + 0.5 * (TAU * t).cos(),
            1 => smoothstep(0.0, 0.5 - flat * 0.5 + 0.001, 0.5 - (t - 0.5).abs()),
            _ => 1.0 - (t * 2.0 - 1.0).abs(),
        }
    });
    pattern_outputs(a, &values)
}

const PLATE_P: &[ParamSpec] = &[
    int("cells", "Cells", 2, 32, 6),
    float("length", "Length", 0.2, 0.95, 0.62, 0.01),
    float("width", "Width", 0.05, 0.5, 0.18, 0.005),
    float("bevel", "Bevel", 0.005, 0.2, 0.05, 0.005),
];

fn eval_diamond_plate(a: &Args) -> Vec<Pixels> {
    let cells = even(a.i(0));
    let (len, width, bevel) = (a.f(1), a.f(2), a.f(3));
    let values = par_scalar(a.res, |_, _, u, v| {
        let (x, y) = (u * cells as f32, v * cells as f32);
        let (i, j) = (x.floor() as i32, y.floor() as i32);
        let (fx, fy) = (x - i as f32 - 0.5, y - j as f32 - 0.5);
        let angle: f32 = if (i + j) & 1 == 0 {
            std::f32::consts::FRAC_PI_4
        } else {
            -std::f32::consts::FRAC_PI_4
        };
        let (c, s) = (angle.cos(), angle.sin());
        let (rx, ry) = (fx * c + fy * s, -fx * s + fy * c);
        let half = (len * 0.5 - width * 0.5).max(0.0);
        let ax = rx.clamp(-half, half);
        let dist = ((rx - ax).powi(2) + ry * ry).sqrt() - width * 0.5;
        0.28 + 0.72 * (1.0 - smoothstep(-bevel, 0.0, dist))
    });
    pattern_outputs(a, &values)
}

const PERF_P: &[ParamSpec] = &[
    int("count", "Count", 1, 48, 8),
    float("size", "Hole size", 0.05, 0.95, 0.55, 0.01),
    choice("shape", "Shape", &["Circle", "Square", "Slot"], 0),
    boolean("stagger", "Stagger rows", true),
    float("softness", "Softness", 0.0, 0.2, 0.015, 0.005),
];

fn eval_perforated(a: &Args) -> Vec<Pixels> {
    let count = a.i(0).max(1);
    let (size, shape, stagger, soft) = (a.f(1), a.i(2), a.b(3), a.f(4));
    let count = if stagger { even(count) } else { count };
    let values = par_scalar(a.res, |_, _, u, v| {
        let y = v * count as f32;
        let j = y.floor() as i32;
        let shift = if stagger && j & 1 == 1 { 0.5 } else { 0.0 };
        let x = u * count as f32 + shift;
        let (fx, fy) = (x - x.floor() - 0.5, y - j as f32 - 0.5);
        let d = match shape {
            0 => (fx * fx + fy * fy).sqrt() - size * 0.5,
            1 => fx.abs().max(fy.abs()) - size * 0.5,
            _ => {
                let ax = fx.abs() - size * 0.3;
                let q = (ax.max(0.0).powi(2) + fy * fy).sqrt();
                q - size * 0.2
            }
        };
        smoothstep(-soft, soft + 1e-4, d)
    });
    pattern_outputs(a, &values)
}

const KNURL_P: &[ParamSpec] = &[
    int("count", "Count", 1, 64, 12),
    float("sharpness", "Sharpness", 0.3, 4.0, 1.0, 0.05),
];

fn eval_knurl(a: &Args) -> Vec<Pixels> {
    let (count, sharp) = (a.i(0).max(1) as f32, a.f(1));
    let values = par_scalar(a.res, |_, _, u, v| {
        let t1 = 1.0 - (fract(count * (u + v)) * 2.0 - 1.0).abs();
        let t2 = 1.0 - (fract(count * (u - v)) * 2.0 - 1.0).abs();
        (t1 * t2).powf(sharp)
    });
    pattern_outputs(a, &values)
}

pub static OPS: &[OpDef] = &[
    op!(
        "tiles",
        "Tiles",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN_VAR,
        TILE_P,
        TILE_COLORS,
        grid_tiles
    ),
    op!(
        "hex_tiles",
        "Hex Tiles",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN_VAR,
        HEX_P,
        TILE_COLORS,
        eval_hex
    ),
    op!(
        "bricks_pro",
        "Bricks Pro",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN_VAR,
        BRICK_P,
        BRICK_COLORS,
        grid_tiles
    ),
    op!(
        "herringbone",
        "Herringbone",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN_VAR,
        HERRING_P,
        WOOD_FLOOR_COLORS,
        eval_herringbone
    ),
    op!(
        "basket_weave",
        "Basket Weave",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN_VAR,
        BASKET_P,
        WOOD_FLOOR_COLORS,
        eval_basket
    ),
    op!(
        "stripes",
        "Stripes",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN,
        STRIPE_P,
        COLORS_AB,
        eval_stripes
    ),
    op!(
        "rings",
        "Rings",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN,
        RING_P,
        COLORS_AB,
        eval_rings
    ),
    op!(
        "waves",
        "Waves",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN,
        WAVE_P,
        COLORS_AB,
        eval_waves
    ),
    op!(
        "dots",
        "Dots",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN,
        DOT_P,
        COLORS_AB,
        eval_dots
    ),
    op!(
        "grid",
        "Grid",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN,
        GRID_P,
        COLORS_AB,
        eval_grid
    ),
    op!(
        "truchet",
        "Truchet",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN,
        TRUCHET_P,
        COLORS_AB,
        eval_truchet
    ),
    op!(
        "scales",
        "Scales",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN_VAR,
        SCALE_P,
        METAL_PLATE_COLORS,
        eval_scales
    ),
    op!(
        "cobblestones",
        "Cobblestones",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN_VAR,
        COBBLE_P,
        TILE_COLORS,
        eval_cobbles
    ),
    op!(
        "planks",
        "Planks",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN_VAR,
        PLANK_P,
        WOOD_FLOOR_COLORS,
        eval_planks
    ),
    op!(
        "corrugated",
        "Corrugated",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN,
        CORRUGATED_P,
        METAL_PLATE_COLORS,
        eval_corrugated
    ),
    op!(
        "diamond_plate",
        "Diamond Plate",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN,
        PLATE_P,
        METAL_PLATE_COLORS,
        eval_diamond_plate
    ),
    op!(
        "perforated",
        "Perforated",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN,
        PERF_P,
        METAL_PLATE_COLORS,
        eval_perforated
    ),
    op!(
        "knurl",
        "Knurl",
        "Patterns",
        true,
        NO_PORTS,
        OUT_PATTERN,
        KNURL_P,
        METAL_PLATE_COLORS,
        eval_knurl
    ),
];

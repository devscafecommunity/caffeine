//! Photoreal building blocks: rock, water, foliage, ground, weathering.
//!
//! These are the "Substance-style" nodes behind the realistic presets. Every op tiles
//! seamlessly (integer periods only), is deterministic for a seed, and exposes the same
//! `[Color, Value, ...]` ports as the other pattern ops so it can be mixed freely.

use super::*;

const TAU: f32 = std::f32::consts::TAU;
const PI: f32 = std::f32::consts::PI;

fn tint(color: [f32; 4], k: f32) -> [f32; 4] {
    [
        clamp01(color[0] * k),
        clamp01(color[1] * k),
        clamp01(color[2] * k),
        1.0,
    ]
}

/// `[Color, Value, Variation]` from separate colour / height / id planes.
fn triple(color: Pixels, height: &[f32], variation: &[f32]) -> Vec<Pixels> {
    vec![
        color,
        height.iter().map(|v| gray(clamp01(*v))).collect(),
        variation.iter().map(|v| gray(clamp01(*v))).collect(),
    ]
}

// ---------------------------------------------------------------- colours

const ROCK_COLORS: &[ColorSpec] = &[
    color("color_a", "Dark rock", [0.20, 0.19, 0.19, 1.0]),
    color("color_b", "Light rock", [0.58, 0.55, 0.50, 1.0]),
];
const PEBBLE_COLORS: &[ColorSpec] = &[
    color("color_a", "Dark pebble", [0.22, 0.21, 0.19, 1.0]),
    color("color_b", "Light pebble", [0.72, 0.68, 0.60, 1.0]),
    color("color_c", "Gap", [0.07, 0.06, 0.05, 1.0]),
];
const GRASS_COLORS: &[ColorSpec] = &[
    color("color_a", "Root", [0.03, 0.08, 0.02, 1.0]),
    color("color_b", "Tip", [0.34, 0.52, 0.13, 1.0]),
    color("color_c", "Soil", [0.07, 0.05, 0.03, 1.0]),
];
const WAVE_COLORS: &[ColorSpec] = &[
    color("color_a", "Trough", [0.02, 0.13, 0.22, 1.0]),
    color("color_b", "Crest", [0.16, 0.50, 0.60, 1.0]),
];
const FOAM_COLORS: &[ColorSpec] = &[
    color("color_a", "Water", [0.02, 0.18, 0.26, 1.0]),
    color("color_b", "Foam", [0.95, 0.97, 0.98, 1.0]),
];
const BARK_COLORS: &[ColorSpec] = &[
    color("color_a", "Fissure", [0.05, 0.035, 0.025, 1.0]),
    color("color_b", "Ridge", [0.33, 0.25, 0.18, 1.0]),
];
const SOIL_COLORS: &[ColorSpec] = &[
    color("color_a", "Shadow", [0.08, 0.055, 0.035, 1.0]),
    color("color_b", "Soil", [0.36, 0.25, 0.16, 1.0]),
];
const LEAF_COLORS: &[ColorSpec] = &[
    color("color_a", "Brown leaf", [0.30, 0.15, 0.05, 1.0]),
    color("color_b", "Golden leaf", [0.70, 0.45, 0.12, 1.0]),
    color("color_c", "Ground", [0.06, 0.04, 0.025, 1.0]),
];
const LICHEN_COLORS: &[ColorSpec] = &[
    color("color_a", "Surface", [0.45, 0.43, 0.40, 1.0]),
    color("color_b", "Lichen", [0.55, 0.62, 0.32, 1.0]),
];
const WET_COLORS: &[ColorSpec] = &[
    color("color_a", "Dry", [0.55, 0.52, 0.48, 1.0]),
    color("color_b", "Wet", [0.16, 0.14, 0.12, 1.0]),
];
const GRIME_COLORS: &[ColorSpec] = &[
    color("color_a", "Clean", [0.75, 0.73, 0.70, 1.0]),
    color("color_b", "Grime", [0.10, 0.08, 0.06, 1.0]),
];
const ICE_COLORS: &[ColorSpec] = &[
    color("color_a", "Deep ice", [0.45, 0.65, 0.78, 1.0]),
    color("color_b", "Frost", [0.92, 0.97, 1.0, 1.0]),
];
const LAVA_COLORS: &[ColorSpec] = &[
    color("color_a", "Crust", [0.03, 0.025, 0.025, 1.0]),
    color("color_b", "Magma", [1.0, 0.35, 0.04, 1.0]),
];
const ASPHALT_COLORS: &[ColorSpec] = &[
    color("color_a", "Bitumen", [0.045, 0.045, 0.05, 1.0]),
    color("color_b", "Aggregate", [0.38, 0.37, 0.36, 1.0]),
];
const CARBON_COLORS: &[ColorSpec] = &[
    color("color_a", "Resin gap", [0.01, 0.01, 0.012, 1.0]),
    color("color_b", "Tow", [0.12, 0.13, 0.15, 1.0]),
];
const PAINT_COLORS: &[ColorSpec] = &[
    color("color_a", "Paint", [0.62, 0.10, 0.08, 1.0]),
    color("color_b", "Substrate", [0.38, 0.37, 0.36, 1.0]),
];
const DROPLET_COLORS: &[ColorSpec] = &[
    color("color_a", "Surface", [0.30, 0.30, 0.32, 1.0]),
    color("color_b", "Droplet", [0.85, 0.90, 0.95, 1.0]),
];

// ----------------------------------------------------------------- params

const FRACTURE_P: &[ParamSpec] = &[
    int("cells", "Facets", 2, 48, 8),
    float("jitter", "Jitter", 0.0, 1.0, 0.9, 0.01),
    float("warp", "Edge warp", 0.0, 1.0, 0.35, 0.01),
    float("crack", "Crack width", 0.005, 0.4, 0.07, 0.005),
    float("tilt", "Facet tilt", 0.0, 1.0, 0.55, 0.01),
    int("seed", "Seed", 0, 9999, 1),
    float("detail", "Surface detail", 0.0, 1.0, 0.5, 0.01),
];
const PEBBLE_P: &[ParamSpec] = &[
    int("cells", "Pebbles", 2, 48, 9),
    float("jitter", "Jitter", 0.0, 1.0, 0.85, 0.01),
    float("round", "Roundness", 0.3, 3.0, 1.0, 0.05),
    float("gap", "Gap", 0.0, 0.5, 0.12, 0.01),
    float("size_var", "Height variation", 0.0, 1.0, 0.5, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const GRASS_P: &[ParamSpec] = &[
    int("cells", "Blade grid", 4, 48, 22),
    int("layers", "Layers", 1, 5, 3),
    float("length", "Length", 0.3, 2.5, 1.1, 0.01),
    float("width", "Width", 0.02, 0.4, 0.11, 0.005),
    float("lean", "Lean", 0.0, 1.0, 0.45, 0.01),
    float("curl", "Curl", 0.0, 1.0, 0.3, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const WAVE_P: &[ParamSpec] = &[
    int("waves", "Waves", 2, 14, 8),
    int("frequency", "Base frequency", 1, 32, 4),
    float("choppy", "Choppiness", 0.0, 1.0, 0.55, 0.01),
    float("wind", "Wind direction", 0.0, 360.0, 30.0, 1.0),
    float("spread", "Direction spread", 0.0, 1.0, 0.5, 0.01),
    float("falloff", "Amplitude falloff", 0.2, 2.5, 1.1, 0.05),
    int("seed", "Seed", 0, 9999, 1),
];
const FOAM_P: &[ParamSpec] = &[
    float("threshold", "Threshold", 0.0, 1.0, 0.62, 0.01),
    float("softness", "Softness", 0.01, 0.6, 0.2, 0.01),
    int("scale", "Bubble scale", 4, 96, 36),
    float("lace", "Lace", 0.0, 1.0, 0.55, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const BARK_P: &[ParamSpec] = &[
    int("cells", "Fissures", 2, 40, 18),
    int("stretch", "Vertical stretch", 1, 16, 8),
    float("depth", "Depth", 0.0, 1.0, 0.75, 0.01),
    float("warp", "Warp", 0.0, 1.0, 0.4, 0.01),
    int("seed", "Seed", 0, 9999, 1),
    float("fibre", "Fibre detail", 0.0, 1.0, 0.5, 0.01),
];
const SOIL_P: &[ParamSpec] = &[
    int("clumps", "Clumps", 2, 48, 12),
    float("grain", "Grain", 0.0, 1.0, 0.6, 0.01),
    float("stones", "Stones", 0.0, 1.0, 0.25, 0.01),
    int("seed", "Seed", 0, 9999, 1),
    float("contrast", "Contrast", 0.2, 4.0, 1.3, 0.05),
];
const LEAF_P: &[ParamSpec] = &[
    int("cells", "Leaf grid", 3, 32, 9),
    int("layers", "Layers", 1, 4, 3),
    float("size", "Leaf size", 0.2, 1.6, 0.85, 0.01),
    float("aspect", "Aspect", 0.2, 1.0, 0.5, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const LICHEN_P: &[ParamSpec] = &[
    int("colonies", "Colonies", 2, 48, 10),
    float("coverage", "Coverage", 0.0, 1.0, 0.45, 0.01),
    float("ragged", "Ragged edge", 0.0, 1.0, 0.6, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const WET_P: &[ParamSpec] = &[
    float("level", "Water level", 0.0, 1.0, 0.4, 0.01),
    float("softness", "Edge softness", 0.01, 0.5, 0.1, 0.01),
    float("noise", "Noise", 0.0, 1.0, 0.35, 0.01),
    int("scale", "Noise scale", 1, 24, 5),
    int("seed", "Seed", 0, 9999, 1),
];
const CAVITY_P: &[ParamSpec] = &[
    float("radius", "Radius", 0.002, 0.2, 0.03, 0.002),
    float("amount", "Amount", 0.0, 4.0, 1.4, 0.05),
    float("noise", "Noise", 0.0, 1.0, 0.4, 0.01),
    int("scale", "Noise scale", 1, 24, 6),
    int("seed", "Seed", 0, 9999, 1),
];
const ICE_P: &[ParamSpec] = &[
    int("plates", "Plates", 2, 32, 7),
    float("crack", "Crack width", 0.005, 0.2, 0.025, 0.005),
    float("frost", "Frost", 0.0, 1.0, 0.5, 0.01),
    float("bubbles", "Bubbles", 0.0, 1.0, 0.4, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const LAVA_P: &[ParamSpec] = &[
    int("plates", "Plates", 2, 32, 7),
    float("gap", "Gap width", 0.02, 0.5, 0.14, 0.005),
    float("warp", "Warp", 0.0, 1.0, 0.45, 0.01),
    float("glow", "Glow", 0.0, 2.0, 1.0, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const ASPHALT_P: &[ParamSpec] = &[
    int("cells", "Aggregate grid", 16, 160, 70),
    float("density", "Aggregate density", 0.0, 1.0, 0.55, 0.01),
    float("wear", "Surface wear", 0.0, 1.0, 0.5, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const CARBON_P: &[ParamSpec] = &[
    int("tows", "Tows", 4, 64, 16),
    float("gap", "Gap", 0.0, 0.3, 0.06, 0.005),
    float("fuzz", "Fibre fuzz", 0.0, 1.0, 0.35, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const CHIP_P: &[ParamSpec] = &[
    int("scale", "Scale", 2, 32, 7),
    float("coverage", "Chipped amount", 0.0, 1.0, 0.35, 0.01),
    float("edge", "Edge roughness", 0.0, 1.0, 0.55, 0.01),
    float("thickness", "Paint thickness", 0.0, 1.0, 0.35, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const DROPLET_P: &[ParamSpec] = &[
    int("cells", "Droplet grid", 4, 80, 22),
    float("density", "Density", 0.0, 1.0, 0.55, 0.01),
    float("size", "Size", 0.1, 0.9, 0.55, 0.01),
    float("size_var", "Size variation", 0.0, 1.0, 0.6, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

// ------------------------------------------------------------------ evals

fn eval_fracture(a: &Args) -> Vec<Pixels> {
    let (cells, jitter, warp, crack, tilt, seed, detail) =
        (a.i(0), a.f(1), a.f(2), a.f(3), a.f(4), a.i(5), a.f(6));
    let height = par_scalar(a.res, |_, _, u, v| {
        let wu = u + (perlin(u, v, 5, seed + 3) - 0.5) * warp * 0.08;
        let wv = v + (perlin(u, v, 5, seed + 4) - 0.5) * warp * 0.08;
        let cell = worley(fract(wu), fract(wv), cells, seed, jitter, false);
        let edge = smoothstep(0.0, crack, cell.f2 - cell.f1);
        // Each facet is a tilted plane: id sets the level, the offset to its centre the slope.
        let slope = (cell.dx * 0.8 + cell.dy * 0.6) * tilt;
        let facet = 0.55 + (cell.id - 0.5) * tilt * 0.7 - slope * 0.5;
        let grain = fbm(Basis::Ridged, u, v, cells * 6, seed + 9, 4, 0.5, 2.0);
        let rough = (grain - 0.5) * detail * 0.22;
        clamp01((facet + rough) * (0.25 + 0.75 * edge))
    });
    let ids = par_scalar(a.res, |_, _, u, v| {
        worley(u, v, cells, seed, jitter, false).id
    });
    let (ca, cb) = (a.color(0), a.color(1));
    let color: Pixels = height
        .iter()
        .zip(ids.iter())
        .map(|(h, id)| tint(mix4(ca, cb, clamp01(*h)), 0.82 + 0.36 * id))
        .collect();
    triple(color, &height, &ids)
}

fn eval_pebbles(a: &Args) -> Vec<Pixels> {
    let (cells, jitter, round, gap, size_var, seed) =
        (a.i(0), a.f(1), a.f(2), a.f(3), a.f(4), a.i(5));
    let height = par_scalar(a.res, |_, _, u, v| {
        let c = worley(u, v, cells, seed, jitter, false);
        // d = 0 at the pebble centre, 0.5 on the border shared with the next one.
        let d = c.f1 / (c.f1 + c.f2).max(1e-5);
        let inside = clamp01(1.0 - d / (0.5 - gap * 0.5).max(0.05));
        let dome = inside.powf(1.0 / round.max(0.1)).sqrt();
        let level = 0.55 + (c.id - 0.5) * size_var * 0.9;
        clamp01(dome * level)
    });
    let ids = par_scalar(a.res, |_, _, u, v| {
        worley(u, v, cells, seed, jitter, false).id
    });
    let (ca, cb, gapc) = (a.color(0), a.color(1), a.color(2));
    let color: Pixels = height
        .iter()
        .zip(ids.iter())
        .map(|(h, id)| {
            let stone = mix4(ca, cb, *id);
            mix4(gapc, tint(stone, 0.75 + 0.5 * h), smoothstep(0.0, 0.18, *h))
        })
        .collect();
    triple(color, &height, &ids)
}

fn eval_grass(a: &Args) -> Vec<Pixels> {
    let (cells, layers, length, width, lean, curl, seed) =
        (a.i(0), a.i(1), a.f(2), a.f(3), a.f(4), a.f(5), a.i(6));
    let (root, tipc, soil) = (a.color(0), a.color(1), a.color(2));
    let n = cells as f32;
    // Each pixel stores (height, r, g, b) of the highest blade covering it.
    let planes = par_fill(a.res, |_, _, u, v| {
        let (x, y) = (u * n, v * n);
        let (ix, iy) = (x.floor() as i32, y.floor() as i32);
        let mut best_h = -1.0f32;
        let mut col = tint(soil, 0.6);
        for layer in 0..layers {
            let lseed = seed.wrapping_add(layer * 77);
            for oy in -1..=3 {
                for ox in -3..=3 {
                    let (cx, cy) = (ix + ox, iy + oy);
                    let (wx, wy) = (wrap_i(cx, cells), wrap_i(cy, cells));
                    let (rx, ry) = rand2(wx, wy, lseed);
                    let id = rand01(wx, wy, lseed + 5);
                    let (bx, by) = (cx as f32 + rx, cy as f32 + ry);
                    let theta = (rand01(wx, wy, lseed + 8) - 0.5) * lean * 2.4;
                    let blade_len = length * (0.6 + 0.8 * id);
                    // Blades grow towards -y; bend sideways by `curl`.
                    let (dx, dy) = (theta.sin(), -theta.cos());
                    let side = if rand01(wx, wy, lseed + 12) > 0.5 {
                        1.0
                    } else {
                        -1.0
                    };
                    let (px, py) = (x - bx, y - by);
                    let along = (px * dx + py * dy) / blade_len;
                    if !(0.0..=1.0).contains(&along) {
                        continue;
                    }
                    let perp = -px * dy + py * dx;
                    let bend = curl * blade_len * along * along * side * 0.6;
                    let dist = (perp - bend).abs();
                    let w = width * (1.0 - along).powf(0.8) + 0.004;
                    if dist > w {
                        continue;
                    }
                    let h = (layer as f32 + 0.55 * along + 0.25 * (1.0 - dist / w))
                        / layers.max(1) as f32;
                    if h > best_h {
                        best_h = h;
                        let shade = 0.55 + 0.45 * (1.0 - dist / w) + 0.12 * (id - 0.5);
                        let base = mix4(root, tipc, along.powf(0.8));
                        col = tint(
                            base,
                            shade * (0.8 + 0.4 * (layer as f32 + 1.0) / layers as f32),
                        );
                    }
                }
            }
        }
        [clamp01(best_h.max(0.0)), col[0], col[1], col[2]]
    });
    let color: Pixels = planes.iter().map(|p| [p[1], p[2], p[3], 1.0]).collect();
    let height: Vec<f32> = planes.iter().map(|p| p[0]).collect();
    let ids: Vec<f32> = height.iter().map(|h| fract(h * 7.31)).collect();
    triple(color, &height, &ids)
}

fn eval_waves(a: &Args) -> Vec<Pixels> {
    let (waves, base, choppy, wind, spread, falloff, seed) =
        (a.i(0), a.i(1), a.f(2), a.f(3), a.f(4), a.f(5), a.i(6));
    // Integer wave vectors keep the field seamless.
    let mut set: Vec<(f32, f32, f32, f32)> = Vec::new();
    for i in 0..waves {
        let r = rand01(i, 1, seed);
        let angle = wind.to_radians() + (r - 0.5) * spread * PI;
        let freq = base as f32 * (1.0 + i as f32 * 0.55) * (0.8 + 0.4 * rand01(i, 2, seed));
        let kx = (freq * angle.cos()).round();
        let ky = (freq * angle.sin()).round();
        let (kx, ky) = if kx == 0.0 && ky == 0.0 {
            (1.0, 0.0)
        } else {
            (kx, ky)
        };
        let amp = 1.0 / (1.0 + i as f32).powf(falloff);
        set.push((kx, ky, amp, rand01(i, 3, seed) * TAU));
    }
    let norm: f32 = set.iter().map(|w| w.2).sum::<f32>().max(1e-4);
    let height = par_scalar(a.res, |_, _, u, v| {
        let warp = (perlin(u, v, 4, seed + 7) - 0.5) * 0.35;
        let mut sum = 0.0;
        for (kx, ky, amp, phase) in &set {
            let s = (TAU * (kx * u + ky * v) + phase + warp * TAU).sin();
            let soft = 0.5 + 0.5 * s;
            let sharp = soft.powf(1.0 + 3.0 * choppy);
            sum += amp * (soft + (sharp - soft) * choppy);
        }
        let ripple = fbm(Basis::Perlin, u, v, 26, seed + 13, 3, 0.5, 2.0) - 0.5;
        clamp01(sum / norm * 0.88 + 0.06 + ripple * 0.2)
    });
    let (ca, cb) = (a.color(0), a.color(1));
    let color: Pixels = height.iter().map(|h| mix4(ca, cb, h.powf(1.2))).collect();
    let variation: Vec<f32> = height.iter().map(|h| (h - 0.5).abs() * 2.0).collect();
    triple(color, &height, &variation)
}

fn eval_foam(a: &Args) -> Vec<Pixels> {
    let (threshold, softness, scale, lace, seed) = (a.f(0), a.f(1), a.i(2), a.f(3), a.i(4));
    let input = a
        .input(0)
        .map(|image| image.iter().map(|p| luma(*p)).collect::<Vec<f32>>());
    let own = par_scalar(a.res, |_, _, u, v| {
        fbm(Basis::Perlin, u, v, 4, seed, 5, 0.55, 2.0)
    });
    let res = a.res;
    let values = par_scalar(res, |x, y, u, v| {
        let h = input
            .as_ref()
            .map(|plane| plane[y * res + x])
            .unwrap_or(own[y * res + x]);
        let bubbles = worley(u, v, scale, seed + 2, 1.0, false);
        let lacework = 1.0 - smoothstep(0.0, 0.35, bubbles.f2 - bubbles.f1);
        let breakup = fbm(Basis::Billow, u, v, scale / 3 + 2, seed + 5, 4, 0.5, 2.0);
        let crest = smoothstep(threshold, threshold + softness, h + (breakup - 0.5) * 0.35);
        clamp01(crest * (1.0 - lace * 0.55 + lace * 0.55 * lacework))
    });
    pattern_outputs(a, &values)
}

/// Cellular distances on an `nx` x `ny` periodic grid with cells stretched vertically by
/// `nx / ny`: returns (f1, f2, id) in units of one cell width.
fn worley_tall(u: f32, v: f32, nx: i32, ny: i32, seed: i32, jitter: f32) -> (f32, f32, f32) {
    let (x, y) = (u * nx as f32, v * ny as f32);
    let (ix, iy) = (x.floor() as i32, y.floor() as i32);
    let stretch = nx as f32 / ny as f32;
    let (mut f1, mut f2, mut id) = (f32::MAX, f32::MAX, 0.0);
    // Tall cells make same-row neighbours up to three columns away the nearest candidates.
    for oy in -1..=1 {
        for ox in -3..=3 {
            let (cx, cy) = (ix + ox, iy + oy);
            let (wx, wy) = (wrap_i(cx, nx), wrap_i(cy, ny));
            let (rx, ry) = rand2(wx, wy, seed);
            // Columns stay orderly (plates side by side); rows are loose (plates of varying length).
            let dx = cx as f32 + 0.5 + (rx - 0.5) * jitter * 0.45 - x;
            let dy = (cy as f32 + 0.5 + (ry - 0.5) * jitter - y) * stretch;
            let d = (dx * dx + dy * dy).sqrt();
            if d < f1 {
                f2 = f1;
                f1 = d;
                id = rand01(wx, wy, seed + 991);
            } else if d < f2 {
                f2 = d;
            }
        }
    }
    (f1, f2, id)
}

fn eval_bark(a: &Args) -> Vec<Pixels> {
    let (cells, stretch, depth, warp, seed, fibre) =
        (a.i(0), a.i(1), a.f(2), a.f(3), a.i(4), a.f(5));
    // At least three rows / five columns so a cell never meets its own periodic copy.
    let cells = cells.max(7);
    let ny = (cells / stretch.max(1)).max(3);
    let res = a.res;
    let planes = par_fill(res, |_, _, u, v| {
        let wu = u + (perlin(u, v, 4, seed + 2) - 0.5) * warp * 0.06;
        let wv = v + (perlin(u, v, 3, seed + 3) - 0.5) * warp * 0.04;
        let (f1, f2, id) = worley_tall(fract(wu), fract(wv), cells, ny, seed, 1.0);
        let gap = smoothstep(0.0, 0.16, f2 - f1);
        // Plates are domed ridges with random heights; the gaps are the fissures.
        let dome = 1.0 - (f1 / (f2 + 1e-4)).powi(2) * 0.35;
        let level = 0.7 + (id - 0.5) * 0.35;
        let fine = perlin2(u, v, cells * 5, ny * 3, seed + 9) - 0.5;
        let ridges = perlin2(u, v, cells * 3, ny * 2, seed + 12) - 0.5;
        let h = clamp01(
            1.0 - depth + depth * gap * dome * level + fine * fibre * 0.2 + ridges * fibre * 0.12,
        );
        [h, id, 0.0, 1.0]
    });
    let height: Vec<f32> = planes.iter().map(|p| p[0]).collect();
    let ids: Vec<f32> = planes.iter().map(|p| p[1]).collect();
    let (ca, cb) = (a.color(0), a.color(1));
    let color: Pixels = height
        .iter()
        .zip(ids.iter())
        .map(|(h, id)| tint(mix4(ca, cb, smoothstep(0.1, 0.95, *h)), 0.85 + 0.3 * id))
        .collect();
    triple(color, &height, &ids)
}

fn eval_soil(a: &Args) -> Vec<Pixels> {
    let (clumps, grain, stones, seed, contrast) = (a.i(0), a.f(1), a.f(2), a.i(3), a.f(4));
    let height = par_scalar(a.res, |_, _, u, v| {
        let big = worley(u, v, clumps, seed, 1.0, false);
        let clump = clamp01(1.0 - big.f1 * 1.15);
        let mid = worley(u, v, clumps * 3, seed + 4, 1.0, false);
        let mound = clamp01(1.0 - mid.f1 * 1.3);
        let fine = value_noise(u, v, clumps * 24, seed + 8);
        let stone = worley(u, v, clumps * 2, seed + 12, 0.9, false);
        let pebble = if stone.id < stones {
            clamp01(1.0 - stone.f1 * 3.2).sqrt()
        } else {
            0.0
        };
        let base = clump * 0.5 + mound * 0.3 + (fine - 0.5) * grain * 0.25 + 0.1;
        shape_value(base.max(pebble * 0.9), contrast, 0.0)
    });
    let (ca, cb) = (a.color(0), a.color(1));
    let color: Pixels = height.iter().map(|h| mix4(ca, cb, h.powf(1.2))).collect();
    triple(color, &height, &height)
}

fn eval_leaves(a: &Args) -> Vec<Pixels> {
    let (cells, layers, size, aspect, seed) = (a.i(0), a.i(1), a.f(2), a.f(3), a.i(4));
    let (brown, gold, ground) = (a.color(0), a.color(1), a.color(2));
    let n = cells as f32;
    // (r, g, b, height) of the top-most leaf.
    let planes = par_fill(a.res, |_, _, u, v| {
        let (x, y) = (u * n, v * n);
        let (ix, iy) = (x.floor() as i32, y.floor() as i32);
        let mut best = -1.0f32;
        let mut out = [ground[0] * 0.5, ground[1] * 0.5, ground[2] * 0.5, 0.0];
        for layer in 0..layers {
            let lseed = seed.wrapping_add(layer * 131);
            for oy in -1..=1 {
                for ox in -1..=1 {
                    let (cx, cy) = (ix + ox, iy + oy);
                    let (wx, wy) = (wrap_i(cx, cells), wrap_i(cy, cells));
                    let (rx, ry) = rand2(wx, wy, lseed);
                    let id = rand01(wx, wy, lseed + 3);
                    let ang = rand01(wx, wy, lseed + 6) * TAU;
                    let (lx, ly) = (x - (cx as f32 + rx), y - (cy as f32 + ry));
                    let (c, s) = (ang.cos(), ang.sin());
                    let (ex, ey) = (lx * c + ly * s, -lx * s + ly * c);
                    let len = size * (0.7 + 0.5 * id);
                    let t = ex / len;
                    if !(-1.0..=1.0).contains(&t) {
                        continue;
                    }
                    let half = aspect * len * (1.0 - t * t).sqrt().powf(0.8);
                    if ey.abs() > half {
                        continue;
                    }
                    let vein = 1.0 - smoothstep(0.0, 0.05 * len, ey.abs());
                    let body = 1.0 - ey.abs() / half.max(1e-4);
                    let h = (layer as f32 + 0.6 + 0.3 * body + 0.1 * vein) / layers as f32;
                    if h > best {
                        best = h;
                        let base = mix4(brown, gold, id);
                        let col = tint(base, 0.7 + 0.3 * body - 0.2 * vein);
                        out = [col[0], col[1], col[2], clamp01(best)];
                    }
                }
            }
        }
        out
    });
    let color: Pixels = planes.iter().map(|p| [p[0], p[1], p[2], 1.0]).collect();
    let height: Vec<f32> = planes.iter().map(|p| p[3]).collect();
    let variation: Vec<f32> = planes.iter().map(|p| fract(p[0] * 13.7)).collect();
    triple(color, &height, &variation)
}

fn eval_lichen(a: &Args) -> Vec<Pixels> {
    let (colonies, coverage, ragged, seed) = (a.i(0), a.f(1), a.f(2), a.i(3));
    let values = par_scalar(a.res, |_, _, u, v| {
        let c = worley(u, v, colonies, seed, 1.0, false);
        let radius = 0.25 + 0.5 * c.id * coverage * 1.4;
        let edge_noise = fbm(Basis::Billow, u, v, colonies * 4, seed + 3, 4, 0.55, 2.0) - 0.5;
        let d = c.f1 / 0.7 + edge_noise * ragged * 0.55;
        let body = 1.0 - smoothstep(radius * 0.6, radius, d);
        let crust = value_noise(u, v, colonies * 28, seed + 8);
        clamp01(body * (0.65 + 0.35 * crust))
    });
    pattern_outputs(a, &values)
}

fn eval_wet(a: &Args) -> Vec<Pixels> {
    let (level, softness, noise, scale, seed) = (a.f(0), a.f(1), a.f(2), a.i(3), a.i(4));
    let h = a.scalar(0, 0.5);
    let res = a.res;
    let values = par_scalar(res, |x, y, u, v| {
        let n = (fbm(Basis::Perlin, u, v, scale, seed, 4, 0.5, 2.0) - 0.5) * noise;
        1.0 - smoothstep(level - softness, level + softness, h[y * res + x] + n)
    });
    pattern_outputs(a, &values)
}

fn eval_cavity(a: &Args) -> Vec<Pixels> {
    let (radius, amount, noise, scale, seed) = (a.f(0), a.f(1), a.f(2), a.i(3), a.i(4));
    let h = a.scalar(0, 0.5);
    let res = a.res;
    let blurred = gaussian_plane(&h, res, (radius * res as f32).max(1.0));
    let mut cav: Vec<f32> = h
        .iter()
        .zip(blurred.iter())
        .map(|(h, b)| (b - h).max(0.0))
        .collect();
    let peak = cav.iter().cloned().fold(1e-4f32, f32::max);
    for c in cav.iter_mut() {
        *c /= peak;
    }
    let values = par_scalar(res, |x, y, u, v| {
        let n = fbm(Basis::Perlin, u, v, scale, seed, 4, 0.55, 2.0);
        clamp01(cav[y * res + x] * amount * (1.0 - noise + noise * n * 2.0))
    });
    pattern_outputs(a, &values)
}

fn eval_ice(a: &Args) -> Vec<Pixels> {
    let (plates, crack, frost, bubbles, seed) = (a.i(0), a.f(1), a.f(2), a.f(3), a.i(4));
    let height = par_scalar(a.res, |_, _, u, v| {
        let c = worley(u, v, plates, seed, 0.9, false);
        let line = 1.0 - smoothstep(0.0, crack, c.f2 - c.f1);
        let f = fbm(Basis::Ridged, u, v, plates * 3, seed + 3, 4, 0.5, 2.0);
        let b = worley(u, v, plates * 6, seed + 8, 1.0, false);
        let bubble = if b.id > 1.0 - bubbles * 0.5 {
            clamp01(1.0 - b.f1 * 4.0)
        } else {
            0.0
        };
        clamp01(0.7 - line * 0.5 + (f - 0.5) * frost * 0.2 - bubble * 0.15)
    });
    let (ca, cb) = (a.color(0), a.color(1));
    let color: Pixels = height
        .iter()
        .map(|h| mix4(cb, ca, smoothstep(0.25, 0.85, *h)))
        .collect();
    triple(color, &height, &height)
}

fn eval_lava(a: &Args) -> Vec<Pixels> {
    let (plates, gap, warp, glow, seed) = (a.i(0), a.f(1), a.f(2), a.f(3), a.i(4));
    let (crust, magma) = (a.color(0), a.color(1));
    let res = a.res;
    let height = par_scalar(res, |_, _, u, v| {
        let wu = u + (perlin(u, v, 5, seed + 3) - 0.5) * warp * 0.06;
        let c = worley(fract(wu), v, plates, seed, 0.95, false);
        let plate = smoothstep(0.0, gap, c.f2 - c.f1);
        let top = fbm(Basis::Ridged, u, v, plates * 3, seed + 6, 4, 0.5, 2.0);
        clamp01(plate * (0.7 + 0.3 * top))
    });
    let emissive = par_scalar(res, |x, y, u, v| {
        let flick = 0.75 + 0.25 * fbm(Basis::Perlin, u, v, plates * 2, seed + 11, 3, 0.5, 2.0);
        clamp01((1.0 - smoothstep(0.0, 0.55, height[y * res + x])) * flick * glow)
    });
    let color: Pixels = height
        .iter()
        .zip(emissive.iter())
        .map(|(h, e)| mix4(tint(crust, 0.7 + 0.8 * h), magma, clamp01(*e)))
        .collect();
    triple(color, &height, &emissive)
}

fn eval_asphalt(a: &Args) -> Vec<Pixels> {
    let (cells, density, wear, seed) = (a.i(0), a.f(1), a.f(2), a.i(3));
    let height = par_scalar(a.res, |_, _, u, v| {
        let c = worley(u, v, cells, seed, 1.0, false);
        let stone = if c.id < density {
            clamp01(1.0 - c.f1 * (2.2 + 1.5 * c.id)).sqrt()
        } else {
            0.0
        };
        let fine = value_noise(u, v, cells * 4, seed + 5);
        let worn = fbm(Basis::Perlin, u, v, 6, seed + 9, 4, 0.5, 2.0);
        clamp01(0.25 + stone * (0.5 + 0.2 * wear * worn) + (fine - 0.5) * 0.12)
    });
    let ids = par_scalar(a.res, |_, _, u, v| worley(u, v, cells, seed, 1.0, false).id);
    let (bit, agg) = (a.color(0), a.color(1));
    let color: Pixels = height
        .iter()
        .zip(ids.iter())
        .map(|(h, id)| mix4(bit, tint(agg, 0.6 + 0.8 * id), smoothstep(0.3, 0.7, *h)))
        .collect();
    triple(color, &height, &ids)
}

fn eval_carbon(a: &Args) -> Vec<Pixels> {
    let (tows, gap, fuzz, seed) = (a.i(0), a.f(1), a.f(2), a.i(3));
    let n = tows as f32;
    let height = par_scalar(a.res, |_, _, u, v| {
        let (x, y) = (u * n, v * n);
        let (ix, iy) = (x.floor() as i32, y.floor() as i32);
        let (fx, fy) = (x - ix as f32, y - iy as f32);
        // 2x2 twill: which direction is on top alternates diagonally.
        let horizontal = wrap_i(ix - iy, 4) < 2;
        let (across, along) = if horizontal { (fy, fx) } else { (fx, fy) };
        let profile = (across * PI).sin().max(0.0).powf(0.6);
        let crown = 0.85 + 0.15 * (along * PI).sin();
        let edge = smoothstep(0.0, gap * 2.0 + 0.001, across.min(1.0 - across));
        let fibre = value_noise(u, v, tows * 32, seed) - 0.5;
        clamp01(profile * crown * edge + fibre * fuzz * 0.12)
    });
    let (ca, cb) = (a.color(0), a.color(1));
    let color: Pixels = height.iter().map(|h| mix4(ca, cb, h.powf(1.3))).collect();
    triple(color, &height, &height)
}

fn eval_paint_chips(a: &Args) -> Vec<Pixels> {
    let (scale, coverage, edge, thickness, seed) = (a.i(0), a.f(1), a.f(2), a.f(3), a.i(4));
    let mask = par_scalar(a.res, |_, _, u, v| {
        let c = worley(u, v, scale, seed, 1.0, false);
        let n = fbm(Basis::Perlin, u, v, scale * 3, seed + 4, 5, 0.55, 2.0);
        let field = (1.0 - c.f1 * 1.1) * 0.6 + n * 0.5 + (c.id - 0.5) * 0.3;
        let cut = 1.0 - coverage * 0.75;
        smoothstep(cut - 0.18, cut - 0.1 + edge * 0.1, field)
    });
    let height: Vec<f32> = mask
        .iter()
        .map(|m| clamp01(1.0 - m * thickness * 1.6))
        .collect();
    let (paint, sub) = (a.color(0), a.color(1));
    let color: Pixels = mask.iter().map(|m| mix4(paint, sub, clamp01(*m))).collect();
    triple(color, &height, &mask)
}

fn eval_droplets(a: &Args) -> Vec<Pixels> {
    let (cells, density, size, size_var, seed) = (a.i(0), a.f(1), a.f(2), a.f(3), a.i(4));
    let height = par_scalar(a.res, |_, _, u, v| {
        let mut best = 0.0f32;
        for (layer, mult) in [1i32, 2].iter().enumerate() {
            let c = worley(u, v, cells * mult, seed + layer as i32 * 31, 0.9, false);
            if c.id > density {
                continue;
            }
            // Distances are in cell units (0.5 = half a cell), so the radius is too.
            let grow = 1.0 - size_var + size_var * rand01(c.cx, c.cy, seed + 2);
            let radius = (0.5 * size * grow).max(0.02);
            let dome = clamp01(1.0 - (c.f1 / radius).powi(2)).sqrt();
            best = best.max(dome * if layer == 0 { 1.0 } else { 0.7 });
        }
        best
    });
    let (ca, cb) = (a.color(0), a.color(1));
    let color: Pixels = height
        .iter()
        .map(|h| mix4(ca, cb, smoothstep(0.0, 0.5, *h) * 0.55))
        .collect();
    triple(color, &height, &height)
}

// ------------------------------------------------------------------ table

const IN_HEIGHT: &[PortDef] = &[scalar_port("Height")];
const OUT_PBR: &[PortDef] = &[
    port("Color"),
    scalar_port("Value"),
    scalar_port("Variation"),
];
const OUT_LAVA: &[PortDef] = &[port("Color"), scalar_port("Value"), scalar_port("Emissive")];

pub static OPS: &[OpDef] = &[
    op!(
        "rock_fracture",
        "Rock Fracture",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        FRACTURE_P,
        ROCK_COLORS,
        eval_fracture
    ),
    op!(
        "river_pebbles",
        "River Pebbles",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        PEBBLE_P,
        PEBBLE_COLORS,
        eval_pebbles
    ),
    op!(
        "grass_blades",
        "Grass Blades",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        GRASS_P,
        GRASS_COLORS,
        eval_grass
    ),
    op!(
        "water_waves",
        "Water Waves",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        WAVE_P,
        WAVE_COLORS,
        eval_waves
    ),
    op!(
        "foam",
        "Foam",
        "Realistic",
        true,
        IN_HEIGHT,
        OUT_PATTERN,
        FOAM_P,
        FOAM_COLORS,
        eval_foam
    ),
    op!(
        "bark_fissures",
        "Bark Fissures",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        BARK_P,
        BARK_COLORS,
        eval_bark
    ),
    op!(
        "soil_clumps",
        "Soil Clumps",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        SOIL_P,
        SOIL_COLORS,
        eval_soil
    ),
    op!(
        "leaf_litter",
        "Leaf Litter",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        LEAF_P,
        LEAF_COLORS,
        eval_leaves
    ),
    op!(
        "lichen",
        "Lichen",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PATTERN,
        LICHEN_P,
        LICHEN_COLORS,
        eval_lichen
    ),
    op!(
        "wet_mask",
        "Wet / Puddle Mask",
        "Realistic",
        true,
        IN_HEIGHT,
        OUT_PATTERN,
        WET_P,
        WET_COLORS,
        eval_wet
    ),
    op!(
        "cavity_grime",
        "Cavity Grime",
        "Realistic",
        true,
        IN_HEIGHT,
        OUT_PATTERN,
        CAVITY_P,
        GRIME_COLORS,
        eval_cavity
    ),
    op!(
        "ice_cracks",
        "Ice",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        ICE_P,
        ICE_COLORS,
        eval_ice
    ),
    op!(
        "lava",
        "Lava",
        "Realistic",
        true,
        NO_PORTS,
        OUT_LAVA,
        LAVA_P,
        LAVA_COLORS,
        eval_lava
    ),
    op!(
        "asphalt",
        "Asphalt",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        ASPHALT_P,
        ASPHALT_COLORS,
        eval_asphalt
    ),
    op!(
        "carbon_fibre",
        "Carbon Fibre",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        CARBON_P,
        CARBON_COLORS,
        eval_carbon
    ),
    op!(
        "paint_chips",
        "Paint Chips",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        CHIP_P,
        PAINT_COLORS,
        eval_paint_chips
    ),
    op!(
        "water_droplets",
        "Water Droplets",
        "Realistic",
        true,
        NO_PORTS,
        OUT_PBR,
        DROPLET_P,
        DROPLET_COLORS,
        eval_droplets
    ),
];

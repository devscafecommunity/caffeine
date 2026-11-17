//! The **Variation** class: nodes that add controlled randomness to a pattern so a material or a
//! texture looks lived-in instead of mechanically repeated.
//!
//! | Op | Idea |
//! |---|---|
//! | Noise Variation | Perlin / Simplex noise drifts the brightness, hue and saturation of an input in large soft patches |
//! | Cell Variation | every grid or Voronoi cell gets its own random tone (and optionally its own offset into the input) |
//! | Patch Blend | two inputs are mixed in organic patches shaped by Perlin / Simplex noise |
//! | Tile Shuffle | the input is cut into cells that are swapped, rotated and flipped at random |
//! | Wave Function Collapse | a new, larger arrangement that obeys the local rules of a sample (overlapping WFC) |
//! | Diamond-Square | classic fractal heightfield, made periodic so it tiles |
//!
//! Everything is deterministic for its seed and works on integer periods, so seeds give variations of
//! the same material and results stay seamless wherever the input is.

use super::*;
use std::collections::HashMap;

const NOISE_KINDS: &[&str] = &["Perlin", "Simplex"];
const NO_COLORS: &[ColorSpec] = &[];

fn basis_of(index: i32) -> Basis {
    if index == 1 {
        Basis::Simplex
    } else {
        Basis::Perlin
    }
}

// ------------------------------------------------------------------ colour

fn rgb_to_hsv(c: [f32; 3]) -> [f32; 3] {
    let max = c[0].max(c[1]).max(c[2]);
    let min = c[0].min(c[1]).min(c[2]);
    let d = max - min;
    let h = if d < 1e-6 {
        0.0
    } else if max == c[0] {
        ((c[1] - c[2]) / d).rem_euclid(6.0)
    } else if max == c[1] {
        (c[2] - c[0]) / d + 2.0
    } else {
        (c[0] - c[1]) / d + 4.0
    };
    [h / 6.0, if max < 1e-6 { 0.0 } else { d / max }, max]
}

fn hsv_to_rgb(h: f32, s: f32, v: f32) -> [f32; 3] {
    let h = h.rem_euclid(1.0) * 6.0;
    let (i, f) = (h.floor() as i32, h - h.floor());
    let (p, q, t) = (v * (1.0 - s), v * (1.0 - s * f), v * (1.0 - s * (1.0 - f)));
    match i {
        0 => [v, t, p],
        1 => [q, v, p],
        2 => [p, v, t],
        3 => [p, q, v],
        4 => [t, p, v],
        _ => [v, p, q],
    }
}

/// Brightness factor, hue shift (degrees) and saturation factor applied to a pixel.
fn tweak(pixel: [f32; 4], brightness: f32, hue_degrees: f32, saturation: f32) -> [f32; 4] {
    let [h, s, v] = rgb_to_hsv([pixel[0], pixel[1], pixel[2]]);
    let [r, g, b] = hsv_to_rgb(
        h + hue_degrees / 360.0,
        clamp01(s * saturation),
        clamp01(v * brightness),
    );
    [r, g, b, pixel[3]]
}

/// Noise remapped to -1..1 with a contrast.
fn signed(n: f32, contrast: f32) -> f32 {
    ((n - 0.5) * 2.0 * contrast).clamp(-1.0, 1.0)
}

// ----------------------------------------------------------- noise variation

const NOISE_VAR_P: &[ParamSpec] = &[
    choice("noise", "Noise", NOISE_KINDS, 0),
    int("scale", "Patch size (cells)", 1, 32, 4),
    int("octaves", "Octaves", 1, 6, 3),
    float("brightness", "Brightness drift", 0.0, 1.0, 0.25, 0.01),
    float("hue", "Hue drift (degrees)", 0.0, 90.0, 12.0, 0.5),
    float("saturation", "Saturation drift", 0.0, 1.0, 0.2, 0.01),
    float("contrast", "Patch contrast", 0.5, 3.0, 1.3, 0.05),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_noise_variation(a: &Args) -> Vec<Pixels> {
    let basis = basis_of(a.i(0));
    let (scale, octaves) = (a.i(1).max(1), a.i(2));
    let (bright, hue, sat, contrast, seed) = (a.f(3), a.f(4), a.f(5), a.f(6), a.i(7));
    let input = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let field =
        |u: f32, v: f32, offset: i32| fbm(basis, u, v, scale, seed + offset, octaves, 0.5, 2.0);
    let color = par_fill(a.res, |x, y, u, v| {
        let p = input[y * a.res + x];
        let tb = signed(field(u, v, 0), contrast);
        let th = signed(field(u, v, 17), contrast);
        let ts = signed(field(u, v, 41), contrast);
        tweak(p, 1.0 + bright * tb, hue * th, 1.0 + sat * ts)
    });
    let value = par_fill(a.res, |_, _, u, v| gray(clamp01(field(u, v, 0))));
    vec![color, value]
}

// ------------------------------------------------------------ cell variation

const CELL_VAR_P: &[ParamSpec] = &[
    choice("mode", "Cells", &["Grid", "Voronoi"], 0),
    int("cells", "Cells per side", 2, 64, 8),
    float("brightness", "Brightness variation", 0.0, 1.0, 0.25, 0.01),
    float("hue", "Hue variation (degrees)", 0.0, 90.0, 10.0, 0.5),
    float("saturation", "Saturation variation", 0.0, 1.0, 0.2, 0.01),
    float("shift", "Texture shift per cell", 0.0, 1.0, 0.0, 0.01),
    float("jitter", "Voronoi jitter", 0.0, 1.0, 0.9, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

const CELL_VAR_IN: &[PortDef] = &[port("Input"), scalar_port("Cell ID")];

fn eval_cell_variation(a: &Args) -> Vec<Pixels> {
    let (voronoi, cells) = (a.i(0) == 1, a.i(1).max(2));
    let (bright, hue, sat, shift, jitter, seed) = (a.f(2), a.f(3), a.f(4), a.f(5), a.f(6), a.i(7));
    let input = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    // A pattern's own cell-id output (bricks, tiles, stones...) replaces the grid / Voronoi cells.
    let ids = a.input(1);
    let res = a.res;
    let planes = par_fill(res, |x, y, u, v| {
        let (cx, cy, id) = if let Some(ids) = ids {
            let id = luma(ids[y * res + x]);
            // The id is a flat grey per cell: quantise it so a cell hashes to one value.
            let key = (id * 4095.0).round() as i32;
            (key, 7, id)
        } else if voronoi {
            let cell = worley(u, v, cells, seed, jitter, false);
            (cell.cx, cell.cy, cell.id)
        } else {
            let gx = ((u * cells as f32).floor() as i32).clamp(0, cells - 1);
            let gy = ((v * cells as f32).floor() as i32).clamp(0, cells - 1);
            (gx, gy, rand01(gx, gy, seed.wrapping_add(991)))
        };
        let (r1, r2) = rand2(cx, cy, seed.wrapping_add(13));
        let r3 = rand01(cx, cy, seed.wrapping_add(29));
        let r0 = if ids.is_some() {
            rand01(cx, cy, seed.wrapping_add(991))
        } else {
            id
        };
        [r0, r1, r2, r3]
    });
    let color = par_fill(res, |x, y, u, v| {
        let p = &planes[y * res + x];
        let base = if shift > 0.0 {
            // Each cell reads the input from its own random offset (stochastic tiling).
            sample_wrap(
                &input,
                res,
                u + (p[1] - 0.5) * shift,
                v + (p[2] - 0.5) * shift,
            )
        } else {
            input[y * res + x]
        };
        tweak(
            base,
            1.0 + bright * (p[0] * 2.0 - 1.0),
            hue * (p[3] * 2.0 - 1.0),
            1.0 + sat * (p[1] * 2.0 - 1.0),
        )
    });
    let value: Pixels = planes.iter().map(|p| gray(p[0])).collect();
    vec![color, value]
}

// ------------------------------------------------------------- patch blend

const PATCH_P: &[ParamSpec] = &[
    choice("noise", "Noise", NOISE_KINDS, 0),
    int("scale", "Patch size (cells)", 1, 32, 3),
    int("octaves", "Octaves", 1, 6, 4),
    float("coverage", "Coverage of B", 0.0, 1.0, 0.5, 0.01),
    float("softness", "Edge softness", 0.01, 0.5, 0.12, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const PATCH_IN: &[PortDef] = &[port("A"), port("B")];
const PATCH_OUT: &[PortDef] = &[port("Color"), scalar_port("Mask")];

fn eval_patch_blend(a: &Args) -> Vec<Pixels> {
    let basis = basis_of(a.i(0));
    let (scale, octaves, coverage, soft, seed) = (a.i(1).max(1), a.i(2), a.f(3), a.f(4), a.i(5));
    let first = a.image(0, [0.35, 0.35, 0.35, 1.0]);
    let second = a.image(1, [0.75, 0.75, 0.75, 1.0]);
    let res = a.res;
    let mask: Vec<f32> = par_scalar(res, |_, _, u, v| {
        let n = fbm(basis, u, v, scale, seed, octaves, 0.5, 2.0);
        let threshold = 1.0 - coverage;
        smoothstep(threshold - soft, threshold + soft, n)
    });
    let color: Pixels = (0..res * res)
        .map(|i| mix4(first[i], second[i], mask[i]))
        .collect();
    vec![color, mask.iter().map(|m| gray(*m)).collect()]
}

// ------------------------------------------------------------- tile shuffle

const SHUFFLE_P: &[ParamSpec] = &[
    int("cells", "Cells per side", 2, 16, 4),
    float("shuffle", "Swap chance", 0.0, 1.0, 0.7, 0.01),
    boolean("rotate", "Rotate cells", true),
    boolean("flip", "Flip cells", true),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_tile_shuffle(a: &Args) -> Vec<Pixels> {
    let cells = a.i(0).max(2);
    let (swap, rotate, flip, seed) = (a.f(1), a.b(2), a.b(3), a.i(4));
    let input = a.image(0, [0.5, 0.5, 0.5, 1.0]);
    let res = a.res;
    let n = cells as f32;
    let planes = par_fill(res, |_, _, u, v| {
        let (gx, gy) = (u * n, v * n);
        let (cx, cy) = (
            (gx.floor() as i32).clamp(0, cells - 1),
            (gy.floor() as i32).clamp(0, cells - 1),
        );
        let (mut lx, mut ly) = (gx - cx as f32, gy - cy as f32);
        let (r1, r2) = rand2(cx, cy, seed);
        let (r3, r4) = rand2(cx, cy, seed.wrapping_add(7));
        let (mut sx, mut sy) = (cx, cy);
        if r1 < swap {
            sx = (rand01(cx, cy, seed.wrapping_add(31)) * n).floor() as i32 % cells;
            sy = (rand01(cx, cy, seed.wrapping_add(47)) * n).floor() as i32 % cells;
        }
        if flip && r2 < 0.5 {
            lx = 1.0 - lx;
        }
        if flip && r3 < 0.5 {
            ly = 1.0 - ly;
        }
        if rotate {
            for _ in 0..((r4 * 4.0).floor() as i32 % 4) {
                (lx, ly) = (1.0 - ly, lx);
            }
        }
        [(sx as f32 + lx) / n, (sy as f32 + ly) / n, r1, 0.0]
    });
    let color: Pixels = planes
        .iter()
        .map(|p| sample_wrap(&input, res, p[0], p[1]))
        .collect();
    let value: Pixels = planes.iter().map(|p| gray(p[2])).collect();
    vec![color, value]
}

// ---------------------------------------------------------------- diamond-square

const DIAMOND_P: &[ParamSpec] = &[
    int("exponent", "Grid (2^n)", 3, 9, 6),
    float("roughness", "Roughness (Hurst)", 0.1, 1.5, 0.75, 0.01),
    float("contrast", "Contrast", 0.5, 3.0, 1.4, 0.05),
    float("bias", "Bias", -0.5, 0.5, 0.0, 0.01),
    boolean("smooth", "Bicubic smoothing", true),
    int("seed", "Seed", 0, 9999, 1),
];
const DIAMOND_COLORS: &[ColorSpec] = &[
    color("color_a", "Low", [0.10, 0.09, 0.08, 1.0]),
    color("color_b", "High", [0.86, 0.80, 0.68, 1.0]),
];

/// Diamond-square on a torus: indices wrap, so the heightfield tiles. `size` is a power of two.
pub fn diamond_square(size: usize, roughness: f32, seed: i32) -> Vec<f32> {
    let size = size.next_power_of_two().max(2);
    let mut grid = vec![0.0f32; size * size];
    let at = |x: usize, y: usize| (y % size) * size + (x % size);
    let random = |x: usize, y: usize, salt: i32| {
        rand01(x as i32, y as i32, seed.wrapping_add(salt)) * 2.0 - 1.0
    };
    grid[0] = random(0, 0, 3);
    let mut step = size;
    let mut amplitude = 1.0f32;
    let decay = 0.5f32.powf(roughness);
    let mut level = 1;
    while step > 1 {
        let half = step / 2;
        // Diamond step: the centre of every square.
        for y in (0..size).step_by(step) {
            for x in (0..size).step_by(step) {
                let average = (grid[at(x, y)]
                    + grid[at(x + step, y)]
                    + grid[at(x, y + step)]
                    + grid[at(x + step, y + step)])
                    * 0.25;
                grid[at(x + half, y + half)] =
                    average + random(x + half, y + half, level) * amplitude;
            }
        }
        // Square step: the midpoints of every edge.
        for y in (0..size).step_by(half) {
            let start = if (y / half).is_multiple_of(2) {
                half
            } else {
                0
            };
            for x in (start..size).step_by(step) {
                let average = (grid[at(x + size - half, y)]
                    + grid[at(x + half, y)]
                    + grid[at(x, y + size - half)]
                    + grid[at(x, y + half)])
                    * 0.25;
                grid[at(x, y)] = average + random(x, y, level + 101) * amplitude;
            }
        }
        amplitude *= decay;
        step = half;
        level += 1;
    }
    let (min, max) = grid
        .iter()
        .fold((f32::MAX, f32::MIN), |(lo, hi), v| (lo.min(*v), hi.max(*v)));
    let span = (max - min).max(1e-6);
    grid.iter().map(|v| (v - min) / span).collect()
}

fn catmull_rom(p: [f32; 4], t: f32) -> f32 {
    0.5 * ((2.0 * p[1])
        + (-p[0] + p[2]) * t
        + (2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3]) * t * t
        + (-p[0] + 3.0 * p[1] - 3.0 * p[2] + p[3]) * t * t * t)
}

/// Periodic bicubic (or bilinear) sample of a square grid at `(u, v)` in 0..1.
fn sample_grid(grid: &[f32], size: usize, u: f32, v: f32, smooth: bool) -> f32 {
    let (x, y) = (u * size as f32 - 0.5, v * size as f32 - 0.5);
    let (x0, y0) = (x.floor(), y.floor());
    let (tx, ty) = (x - x0, y - y0);
    let get = |ix: i32, iy: i32| {
        grid[wrap_i(iy, size as i32) as usize * size + wrap_i(ix, size as i32) as usize]
    };
    let (ix, iy) = (x0 as i32, y0 as i32);
    if smooth {
        let mut rows = [0.0f32; 4];
        for (k, row) in rows.iter_mut().enumerate() {
            let yy = iy - 1 + k as i32;
            *row = catmull_rom(
                [
                    get(ix - 1, yy),
                    get(ix, yy),
                    get(ix + 1, yy),
                    get(ix + 2, yy),
                ],
                tx,
            );
        }
        catmull_rom(rows, ty)
    } else {
        let top = get(ix, iy) * (1.0 - tx) + get(ix + 1, iy) * tx;
        let bottom = get(ix, iy + 1) * (1.0 - tx) + get(ix + 1, iy + 1) * tx;
        top * (1.0 - ty) + bottom * ty
    }
}

fn eval_diamond_square(a: &Args) -> Vec<Pixels> {
    let grid_size = (1usize << a.i(0).clamp(3, 9)).min(a.res.next_power_of_two());
    let (roughness, contrast, bias, smooth, seed) = (a.f(1), a.f(2), a.f(3), a.b(4), a.i(5));
    let grid = diamond_square(grid_size, roughness, seed);
    let values = par_scalar(a.res, |_, _, u, v| {
        shape_value(
            sample_grid(&grid, grid_size, u, v, smooth).clamp(0.0, 1.0),
            contrast,
            bias,
        )
    });
    pattern_outputs(a, &values)
}

// ---------------------------------------------------- wave function collapse

const WFC_P: &[ParamSpec] = &[
    int("cells", "Output cells", 8, 64, 24),
    int("sample", "Sample cells", 6, 32, 16),
    int("pattern", "Pattern size", 2, 3, 3),
    int("levels", "Tone levels", 2, 8, 4),
    choice(
        "symmetry",
        "Symmetry",
        &["None", "Flip", "Rotate + flip"],
        1,
    ),
    float("softness", "Edge softness", 0.0, 1.0, 0.3, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
const WFC_IN: &[PortDef] = &[port("Sample")];

/// A small deterministic RNG for the solver (no global state, results depend only on the seed).
struct Rng(u64);

impl Rng {
    fn new(seed: u64) -> Self {
        Rng(seed.wrapping_mul(0x9E37_79B9_7F4A_7C15) ^ 0xD1B5_4A32_D192_ED03)
    }

    fn next(&mut self) -> f32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        ((self.0 >> 40) as f32) / 16_777_216.0
    }
}

const DX: [i32; 4] = [-1, 0, 1, 0];
const DY: [i32; 4] = [0, 1, 0, -1];

fn rotate_pattern(p: &[u8], n: usize) -> Vec<u8> {
    (0..n * n)
        .map(|i| {
            let (x, y) = (i % n, i / n);
            p[(n - 1 - x) * n + y]
        })
        .collect()
}

fn reflect_pattern(p: &[u8], n: usize) -> Vec<u8> {
    (0..n * n)
        .map(|i| p[(i / n) * n + (n - 1 - i % n)])
        .collect()
}

fn patterns_agree(p1: &[u8], p2: &[u8], n: usize, dx: i32, dy: i32) -> bool {
    let n = n as i32;
    let (xmin, xmax) = if dx < 0 { (0, dx + n) } else { (dx, n) };
    let (ymin, ymax) = if dy < 0 { (0, dy + n) } else { (dy, n) };
    for y in ymin..ymax {
        for x in xmin..xmax {
            if p1[(x + n * y) as usize] != p2[(x - dx + n * (y - dy)) as usize] {
                return false;
            }
        }
    }
    true
}

/// Overlapping-model WFC over a periodic sample and a periodic output of `out` x `out` cells.
/// Returns the tone band of every output cell, or `None` when every attempt hit a contradiction.
pub fn wfc_bands(
    sample: &[u8],
    s: usize,
    n: usize,
    symmetry: usize,
    out: usize,
    seed: u64,
) -> Option<Vec<u8>> {
    // Distinct patterns (and their weights) from every position of the periodic sample.
    let mut index: HashMap<Vec<u8>, usize> = HashMap::new();
    let mut patterns: Vec<Vec<u8>> = Vec::new();
    let mut weights: Vec<f32> = Vec::new();
    for y in 0..s {
        for x in 0..s {
            let base: Vec<u8> = (0..n * n)
                .map(|i| sample[((y + i / n) % s) * s + (x + i % n) % s])
                .collect();
            let mut variants = vec![base.clone()];
            if symmetry >= 1 {
                variants.push(reflect_pattern(&base, n));
            }
            if symmetry >= 2 {
                let mut turned = base.clone();
                for _ in 0..3 {
                    turned = rotate_pattern(&turned, n);
                    variants.push(turned.clone());
                    variants.push(reflect_pattern(&turned, n));
                }
            }
            for variant in variants {
                match index.get(&variant) {
                    Some(&at) => weights[at] += 1.0,
                    None => {
                        index.insert(variant.clone(), patterns.len());
                        patterns.push(variant);
                        weights.push(1.0);
                    }
                }
            }
        }
    }
    // Keep the solver fast: the most frequent patterns win when there are too many.
    const MAX_PATTERNS: usize = 420;
    if patterns.len() > MAX_PATTERNS {
        let mut order: Vec<usize> = (0..patterns.len()).collect();
        order.sort_by(|a, b| {
            weights[*b]
                .partial_cmp(&weights[*a])
                .unwrap_or(std::cmp::Ordering::Equal)
        });
        order.truncate(MAX_PATTERNS);
        order.sort_unstable();
        patterns = order.iter().map(|i| patterns[*i].clone()).collect();
        weights = order.iter().map(|i| weights[*i]).collect();
    }
    let count = patterns.len();
    if count == 0 {
        return None;
    }
    // propagator[d][t] = patterns allowed one step in direction d from pattern t.
    let mut propagator: Vec<Vec<Vec<usize>>> = vec![vec![Vec::new(); count]; 4];
    for (d, table) in propagator.iter_mut().enumerate() {
        for (t, allowed) in table.iter_mut().enumerate() {
            for t2 in 0..count {
                if patterns_agree(&patterns[t], &patterns[t2], n, DX[d], DY[d]) {
                    allowed.push(t2);
                }
            }
        }
    }
    let cells = out * out;
    let log_weights: Vec<f32> = weights.iter().map(|w| w * w.ln()).collect();

    'attempt: for attempt in 0..14u64 {
        let mut rng = Rng::new(seed.wrapping_add(attempt * 7919));
        let mut wave = vec![true; cells * count];
        let mut compatible = vec![[0i32; 4]; cells * count];
        let mut ones = vec![count as i32; cells];
        let mut sum_w = vec![weights.iter().sum::<f32>(); cells];
        let mut sum_wl = vec![log_weights.iter().sum::<f32>(); cells];
        for i in 0..cells {
            for t in 0..count {
                for d in 0..4 {
                    compatible[i * count + t][d] = propagator[(d + 2) % 4][t].len() as i32;
                }
            }
        }
        let mut stack: Vec<(usize, usize)> = Vec::new();
        let neighbour = |i: usize, d: usize| -> usize {
            let (x, y) = ((i % out) as i32, (i / out) as i32);
            let nx = (x + DX[d]).rem_euclid(out as i32) as usize;
            let ny = (y + DY[d]).rem_euclid(out as i32) as usize;
            ny * out + nx
        };
        // Bans a pattern in a cell and queues it for propagation.
        macro_rules! ban {
            ($i:expr, $t:expr) => {{
                let (i, t) = ($i, $t);
                if wave[i * count + t] {
                    wave[i * count + t] = false;
                    compatible[i * count + t] = [0; 4];
                    ones[i] -= 1;
                    sum_w[i] -= weights[t];
                    sum_wl[i] -= log_weights[t];
                    stack.push((i, t));
                }
            }};
        }
        loop {
            // Observe: the undecided cell with the lowest entropy (plus a little noise).
            let mut best: Option<(usize, f32)> = None;
            for i in 0..cells {
                if ones[i] == 0 {
                    continue 'attempt;
                }
                if ones[i] > 1 {
                    let entropy = sum_w[i].ln() - sum_wl[i] / sum_w[i] + rng.next() * 1e-4;
                    if best.is_none_or(|(_, e)| entropy < e) {
                        best = Some((i, entropy));
                    }
                }
            }
            let Some((cell, _)) = best else {
                // Every cell has exactly one pattern left.
                return Some(
                    (0..cells)
                        .map(|i| {
                            let t = (0..count).find(|t| wave[i * count + *t]).unwrap_or(0);
                            patterns[t][0]
                        })
                        .collect(),
                );
            };
            let mut pick = rng.next() * sum_w[cell];
            let mut chosen = 0;
            for t in 0..count {
                if wave[cell * count + t] {
                    chosen = t;
                    pick -= weights[t];
                    if pick <= 0.0 {
                        break;
                    }
                }
            }
            for t in 0..count {
                if t != chosen {
                    ban!(cell, t);
                }
            }
            // Propagate the consequences.
            while let Some((i, t)) = stack.pop() {
                for d in 0..4 {
                    let j = neighbour(i, d);
                    for &t2 in &propagator[d][t] {
                        let slot = &mut compatible[j * count + t2];
                        slot[d] -= 1;
                        if slot[d] == 0 {
                            ban!(j, t2);
                        }
                    }
                }
            }
        }
    }
    None
}

/// A running-bond brick sample used when no input is connected: mortar, light and dark bricks.
fn builtin_sample(s: usize, seed: i32) -> Pixels {
    let rows = 4usize.max(s / 3);
    let row_height = s as f32 / rows as f32;
    let brick_width = (s / 2).max(2) as f32;
    (0..s * s)
        .map(|i| {
            let (x, y) = ((i % s) as f32, (i / s) as f32);
            let row = (y / row_height).floor() as i32;
            let shifted = x + if row % 2 == 0 { 0.0 } else { brick_width * 0.5 };
            let column = (shifted / brick_width).floor() as i32;
            let in_mortar = (y % row_height) < 1.0 || (shifted % brick_width) < 1.0;
            if in_mortar {
                [0.20, 0.19, 0.18, 1.0]
            } else {
                let tone = rand01(column, row, seed);
                mix4([0.55, 0.20, 0.13, 1.0], [0.78, 0.36, 0.22, 1.0], tone)
            }
        })
        .collect()
}

fn eval_wfc(a: &Args) -> Vec<Pixels> {
    let out = a.i(0).clamp(8, 64) as usize;
    let s = a.i(1).clamp(6, 32) as usize;
    let n = a.i(2).clamp(2, 3) as usize;
    let levels = a.i(3).clamp(2, 8) as usize;
    let (symmetry, softness, seed) = (a.i(4).clamp(0, 2) as usize, a.f(5), a.i(6));
    let res = a.res;

    // Sample: the input (or a built-in brick wall) resampled to s x s cells.
    let source: Pixels = match a.input(0) {
        Some(image) => (0..s * s)
            .map(|i| {
                // Box-average the cell's footprint (4 x 4 taps) so thin lines survive the
                // reduction as a tone instead of being skipped by a single tap.
                let (x, y) = (i % s, i / s);
                let mut sum = [0.0f32; 4];
                for ty in 0..4 {
                    for tx in 0..4 {
                        let p = sample_wrap(
                            image,
                            res,
                            (x as f32 + (tx as f32 + 0.5) / 4.0) / s as f32,
                            (y as f32 + (ty as f32 + 0.5) / 4.0) / s as f32,
                        );
                        for c in 0..4 {
                            sum[c] += p[c] / 16.0;
                        }
                    }
                }
                sum
            })
            .collect(),
        None => builtin_sample(s, seed),
    };
    // Quantise the luminance into bands; each band keeps the mean colour of its pixels.
    let lumas: Vec<f32> = source.iter().map(|p| luma(*p)).collect();
    let (lo, hi) = lumas
        .iter()
        .fold((f32::MAX, f32::MIN), |(l, h), v| (l.min(*v), h.max(*v)));
    let band_of = |l: f32| {
        (((l - lo) / (hi - lo + 1e-6)) * levels as f32)
            .floor()
            .clamp(0.0, levels as f32 - 1.0) as u8
    };
    let bands: Vec<u8> = lumas.iter().map(|l| band_of(*l)).collect();
    let mut sums = vec![[0.0f32; 4]; levels];
    let mut counts = vec![0.0f32; levels];
    for (pixel, band) in source.iter().zip(bands.iter()) {
        for c in 0..4 {
            sums[*band as usize][c] += pixel[c];
        }
        counts[*band as usize] += 1.0;
    }
    let palette: Vec<[f32; 4]> = (0..levels)
        .map(|b| {
            if counts[b] > 0.0 {
                [
                    sums[b][0] / counts[b],
                    sums[b][1] / counts[b],
                    sums[b][2] / counts[b],
                    sums[b][3] / counts[b],
                ]
            } else {
                gray(b as f32 / (levels - 1).max(1) as f32)
            }
        })
        .collect();

    // Solve; if it never converges fall back to a random draw from the sample's own tones.
    let solved = wfc_bands(&bands, s, n, symmetry, out, seed as u64 * 2654435761 + 17);
    let cells: Vec<u8> = solved.unwrap_or_else(|| {
        (0..out * out)
            .map(|i| bands[(rand01(i as i32, 5, seed) * bands.len() as f32) as usize % bands.len()])
            .collect()
    });

    let half = (0.5 * softness).max(1e-4);
    let at = |x: i32, y: i32| {
        palette
            [cells[wrap_i(y, out as i32) as usize * out + wrap_i(x, out as i32) as usize] as usize]
    };
    let value_at = |x: i32, y: i32| {
        cells[wrap_i(y, out as i32) as usize * out + wrap_i(x, out as i32) as usize] as f32
            / (levels - 1).max(1) as f32
    };
    let planes = par_fill(res, |_, _, u, v| {
        let (gx, gy) = (u * out as f32 - 0.5, v * out as f32 - 0.5);
        let (x0, y0) = (gx.floor(), gy.floor());
        let bx = smoothstep(0.5 - half, 0.5 + half, gx - x0);
        let by = smoothstep(0.5 - half, 0.5 + half, gy - y0);
        let (ix, iy) = (x0 as i32, y0 as i32);
        let top = mix4(at(ix, iy), at(ix + 1, iy), bx);
        let bottom = mix4(at(ix, iy + 1), at(ix + 1, iy + 1), bx);
        let _ = value_at;
        mix4(top, bottom, by)
    });
    let value = par_fill(res, |_, _, u, v| {
        let (gx, gy) = (u * out as f32, v * out as f32);
        gray(value_at(gx.floor() as i32, gy.floor() as i32))
    });
    vec![planes, value]
}

// --------------------------------------------------------------- registry

pub const OPS: &[OpDef] = &[
    op!(
        "variation_noise",
        "Noise Variation",
        "Variation",
        true,
        IN_IMAGE,
        OUT_PATTERN,
        NOISE_VAR_P,
        NO_COLORS,
        eval_noise_variation
    ),
    op!(
        "variation_cells",
        "Cell Variation",
        "Variation",
        true,
        CELL_VAR_IN,
        OUT_PATTERN,
        CELL_VAR_P,
        NO_COLORS,
        eval_cell_variation
    ),
    op!(
        "variation_patches",
        "Patch Blend",
        "Variation",
        true,
        PATCH_IN,
        PATCH_OUT,
        PATCH_P,
        NO_COLORS,
        eval_patch_blend
    ),
    op!(
        "variation_shuffle",
        "Tile Shuffle",
        "Variation",
        true,
        IN_IMAGE,
        OUT_PATTERN,
        SHUFFLE_P,
        NO_COLORS,
        eval_tile_shuffle
    ),
    op!(
        "variation_wfc",
        "Wave Function Collapse",
        "Variation",
        true,
        WFC_IN,
        OUT_PATTERN,
        WFC_P,
        NO_COLORS,
        eval_wfc
    ),
    op!(
        "variation_diamond_square",
        "Diamond-Square",
        "Variation",
        true,
        NO_PORTS,
        OUT_PATTERN,
        DIAMOND_P,
        DIAMOND_COLORS,
        eval_diamond_square
    ),
];

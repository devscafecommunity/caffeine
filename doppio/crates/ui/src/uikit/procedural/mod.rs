//! Data-driven procedural node library.
//!
//! Every op is one row of a static table ([`OpDef`]): its title, category,
//! ports, parameter specs and an evaluation function. The node graph
//! ([`NodeKind::Proc`](crate::uikit::generators::NodeKind)), the properties
//! panel, the palette, serialization and the baker are all generated from that
//! table, so adding an op means adding one `OpDef` and its eval function.
//!
//! Conventions
//! * Images are `Vec<[f32; 4]>` (RGBA, linear 0..1), `res * res` pixels, row
//!   major, pixel centres at `(x + 0.5) / res`.
//! * Everything is deterministic for a given parameter set and seed.
//! * Ops flagged `tileable` produce seamless output when their inputs are
//!   seamless (tested in `tests`).
//! * Pattern-style ops expose `[Color, Value, …]` outputs: `Color` is the
//!   value mapped between two colours, `Value` the raw scalar mask/height.

macro_rules! op {
    ($id:expr, $title:expr, $cat:expr, $tile:expr, $inputs:expr, $outputs:expr, $params:expr, $colors:expr, $eval:expr) => {
        $crate::uikit::procedural::OpDef {
            id: $id,
            title: $title,
            category: $cat,
            tileable: $tile,
            inputs: $inputs,
            outputs: $outputs,
            params: $params,
            colors: $colors,
            eval: $eval,
        }
    };
}

pub mod geometry;
pub mod height;
pub mod math;
pub mod natural;
pub mod noise;
pub mod patterns;
pub mod realistic;
pub mod shapes;
pub mod variation;
pub mod vfx;

use crate::uikit::components::node_canvas::PortKind;

pub type Pixels = Vec<[f32; 4]>;

/// Kind of editor control a parameter gets.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ParamKind {
    Float,
    Int,
    Bool,
    Enum(&'static [&'static str]),
}

#[derive(Debug, Clone, Copy)]
pub struct ParamSpec {
    pub key: &'static str,
    pub label: &'static str,
    pub kind: ParamKind,
    pub min: f32,
    pub max: f32,
    pub step: f32,
    pub default: f32,
}

#[derive(Debug, Clone, Copy)]
pub struct ColorSpec {
    pub key: &'static str,
    pub label: &'static str,
    pub default: [f32; 4],
}

#[derive(Debug, Clone, Copy)]
pub struct PortDef {
    pub label: &'static str,
    pub scalar: bool,
}

impl PortDef {
    pub fn kind(self) -> PortKind {
        if self.scalar {
            PortKind::Scalar
        } else {
            PortKind::Color
        }
    }
}

pub type EvalFn = fn(&Args) -> Vec<Pixels>;

/// One procedural operation.
pub struct OpDef {
    pub id: &'static str,
    pub title: &'static str,
    pub category: &'static str,
    pub tileable: bool,
    pub inputs: &'static [PortDef],
    pub outputs: &'static [PortDef],
    pub params: &'static [ParamSpec],
    pub colors: &'static [ColorSpec],
    pub eval: EvalFn,
}

impl std::fmt::Debug for OpDef {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "OpDef({})", self.id)
    }
}

/// A reference to an op in the static table (compared by id).
#[derive(Debug, Clone, Copy)]
pub struct ProcOp(pub &'static OpDef);

impl PartialEq for ProcOp {
    fn eq(&self, other: &Self) -> bool {
        self.0.id == other.0.id
    }
}

impl ProcOp {
    pub fn def(&self) -> &'static OpDef {
        self.0
    }

    pub fn default_params(&self) -> Vec<f32> {
        self.0.params.iter().map(|p| p.default).collect()
    }

    pub fn default_colors(&self) -> Vec<[f32; 4]> {
        self.0.colors.iter().map(|c| c.default).collect()
    }
}

/// Arguments handed to an eval function.
pub struct Args<'a> {
    pub res: usize,
    params: &'a [f32],
    colors: &'a [[f32; 4]],
    inputs: &'a [Option<&'a [[f32; 4]]>],
}

impl<'a> Args<'a> {
    pub fn new(
        res: usize,
        params: &'a [f32],
        colors: &'a [[f32; 4]],
        inputs: &'a [Option<&'a [[f32; 4]]>],
    ) -> Self {
        Args {
            res,
            params,
            colors,
            inputs,
        }
    }

    pub fn f(&self, index: usize) -> f32 {
        self.params.get(index).copied().unwrap_or(0.0)
    }

    pub fn i(&self, index: usize) -> i32 {
        self.f(index).round() as i32
    }

    pub fn b(&self, index: usize) -> bool {
        self.f(index) >= 0.5
    }

    pub fn color(&self, index: usize) -> [f32; 4] {
        self.colors
            .get(index)
            .copied()
            .unwrap_or([0.5, 0.5, 0.5, 1.0])
    }

    pub fn input(&self, index: usize) -> Option<&'a [[f32; 4]]> {
        self.inputs.get(index).copied().flatten()
    }

    /// Luminance of an input as a scalar plane, or `fallback` everywhere.
    pub fn scalar(&self, index: usize, fallback: f32) -> Vec<f32> {
        match self.input(index) {
            Some(image) => image.iter().map(|p| luma(*p)).collect(),
            None => vec![fallback; self.res * self.res],
        }
    }

    /// Input as an image, or `fallback` everywhere.
    pub fn image(&self, index: usize, fallback: [f32; 4]) -> Pixels {
        match self.input(index) {
            Some(image) => image.to_vec(),
            None => vec![fallback; self.res * self.res],
        }
    }
}

// ---------------------------------------------------------------- registry

/// Every op, in palette order.
pub fn all() -> &'static [&'static OpDef] {
    static ALL: std::sync::OnceLock<Vec<&'static OpDef>> = std::sync::OnceLock::new();
    ALL.get_or_init(|| {
        let mut ops: Vec<&'static OpDef> = Vec::new();
        for table in [
            noise::OPS,
            natural::OPS,
            patterns::OPS,
            shapes::OPS,
            math::OPS,
            geometry::OPS,
            height::OPS,
            realistic::OPS,
            variation::OPS,
            vfx::OPS,
        ] {
            ops.extend(table.iter());
        }
        ops
    })
}

pub fn find(id: &str) -> Option<ProcOp> {
    all().iter().find(|op| op.id == id).map(|op| ProcOp(op))
}

/// Category display order.
pub const CATEGORIES: [&str; 10] = [
    "Noise",
    "Natural",
    "Patterns",
    "Shapes",
    "Math",
    "Geometry",
    "Height & PBR",
    "Realistic",
    "Variation",
    "VFX & Shapes",
];

// ----------------------------------------------------------- spec builders

pub const fn float(
    key: &'static str,
    label: &'static str,
    min: f32,
    max: f32,
    default: f32,
    step: f32,
) -> ParamSpec {
    ParamSpec {
        key,
        label,
        kind: ParamKind::Float,
        min,
        max,
        step,
        default,
    }
}

pub const fn int(
    key: &'static str,
    label: &'static str,
    min: i32,
    max: i32,
    default: i32,
) -> ParamSpec {
    ParamSpec {
        key,
        label,
        kind: ParamKind::Int,
        min: min as f32,
        max: max as f32,
        step: 1.0,
        default: default as f32,
    }
}

pub const fn boolean(key: &'static str, label: &'static str, default: bool) -> ParamSpec {
    ParamSpec {
        key,
        label,
        kind: ParamKind::Bool,
        min: 0.0,
        max: 1.0,
        step: 1.0,
        default: if default { 1.0 } else { 0.0 },
    }
}

pub const fn choice(
    key: &'static str,
    label: &'static str,
    options: &'static [&'static str],
    default: usize,
) -> ParamSpec {
    ParamSpec {
        key,
        label,
        kind: ParamKind::Enum(options),
        min: 0.0,
        max: (options.len() - 1) as f32,
        step: 1.0,
        default: default as f32,
    }
}

pub const fn color(key: &'static str, label: &'static str, default: [f32; 4]) -> ColorSpec {
    ColorSpec {
        key,
        label,
        default,
    }
}

pub const fn port(label: &'static str) -> PortDef {
    PortDef {
        label,
        scalar: false,
    }
}

pub const fn scalar_port(label: &'static str) -> PortDef {
    PortDef {
        label,
        scalar: true,
    }
}

pub const NO_PORTS: &[PortDef] = &[];
pub const OUT_COLOR: &[PortDef] = &[port("Color")];
pub const OUT_PATTERN: &[PortDef] = &[port("Color"), scalar_port("Value")];
pub const OUT_PATTERN_VAR: &[PortDef] = &[
    port("Color"),
    scalar_port("Value"),
    scalar_port("Variation"),
];
pub const OUT_VALUE: &[PortDef] = &[scalar_port("Value")];
pub const IN_IMAGE: &[PortDef] = &[port("Input")];
pub const IN_HEIGHT: &[PortDef] = &[scalar_port("Height")];

pub const COLORS_AB: &[ColorSpec] = &[
    color("color_a", "Color A", [0.05, 0.05, 0.05, 1.0]),
    color("color_b", "Color B", [0.95, 0.95, 0.95, 1.0]),
];

// ------------------------------------------------------------ pixel helpers

pub fn luma(p: [f32; 4]) -> f32 {
    0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]
}

pub fn gray(v: f32) -> [f32; 4] {
    [v, v, v, 1.0]
}

pub fn mix4(a: [f32; 4], b: [f32; 4], t: f32) -> [f32; 4] {
    [
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        a[2] + (b[2] - a[2]) * t,
        a[3] + (b[3] - a[3]) * t,
    ]
}

pub fn clamp01(v: f32) -> f32 {
    v.clamp(0.0, 1.0)
}

pub fn smoothstep(e0: f32, e1: f32, x: f32) -> f32 {
    if (e1 - e0).abs() < 1e-8 {
        return if x < e0 { 0.0 } else { 1.0 };
    }
    let t = ((x - e0) / (e1 - e0)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

pub fn fract(v: f32) -> f32 {
    v - v.floor()
}

pub fn wrap_i(value: i32, period: i32) -> i32 {
    let period = period.max(1);
    ((value % period) + period) % period
}

// -------------------------------------------------------- parallel filling

fn thread_count(res: usize) -> usize {
    if res * res < 16_384 {
        return 1;
    }
    std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(1)
        .clamp(1, 8)
}

/// Fills a `res * res` image row-parallel from `f(x, y, u, v)`.
pub fn par_fill<F>(res: usize, f: F) -> Pixels
where
    F: Fn(usize, usize, f32, f32) -> [f32; 4] + Sync,
{
    let mut out = vec![[0.0f32; 4]; res * res];
    let threads = thread_count(res);
    let inv = 1.0 / res as f32;
    if threads <= 1 {
        for y in 0..res {
            for x in 0..res {
                out[y * res + x] = f(x, y, (x as f32 + 0.5) * inv, (y as f32 + 0.5) * inv);
            }
        }
        return out;
    }
    let rows_per = res.div_ceil(threads);
    std::thread::scope(|scope| {
        for (chunk_index, chunk) in out.chunks_mut(rows_per * res).enumerate() {
            let f = &f;
            scope.spawn(move || {
                let y0 = chunk_index * rows_per;
                for (row, line) in chunk.chunks_mut(res).enumerate() {
                    let y = y0 + row;
                    for (x, pixel) in line.iter_mut().enumerate() {
                        *pixel = f(x, y, (x as f32 + 0.5) * inv, (y as f32 + 0.5) * inv);
                    }
                }
            });
        }
    });
    out
}

/// Scalar version of [`par_fill`].
pub fn par_scalar<F>(res: usize, f: F) -> Vec<f32>
where
    F: Fn(usize, usize, f32, f32) -> f32 + Sync,
{
    par_fill(res, |x, y, u, v| {
        let value = f(x, y, u, v);
        [value, 0.0, 0.0, 1.0]
    })
    .into_iter()
    .map(|p| p[0])
    .collect()
}

/// Maps a scalar plane to `[Color, Value]` using the first two colour specs.
pub fn pattern_outputs(args: &Args, values: &[f32]) -> Vec<Pixels> {
    let a = args.color(0);
    let b = args.color(1);
    let color: Pixels = values.iter().map(|v| mix4(a, b, clamp01(*v))).collect();
    let value: Pixels = values.iter().map(|v| gray(clamp01(*v))).collect();
    vec![color, value]
}

/// `[Color, Value, Variation]` for tile-like patterns.
pub fn pattern_outputs_var(args: &Args, values: &[f32], variation: &[f32]) -> Vec<Pixels> {
    let mut out = pattern_outputs(args, values);
    out.push(variation.iter().map(|v| gray(clamp01(*v))).collect());
    out
}

// ------------------------------------------------------------ random + noise

pub fn hash_u32(x: i32, y: i32, seed: i32) -> u32 {
    let mut h = (seed as u32).wrapping_mul(0x9E37_79B1);
    h ^= (x as u32).wrapping_mul(0x85EB_CA6B);
    h = h.rotate_left(13).wrapping_mul(0xC2B2_AE35);
    h ^= (y as u32).wrapping_mul(0x27D4_EB2F);
    h ^= h >> 15;
    h = h.wrapping_mul(0x85EB_CA77);
    h ^= h >> 13;
    h = h.wrapping_mul(0xC2B2_AE3D);
    h ^ (h >> 16)
}

pub fn rand01(x: i32, y: i32, seed: i32) -> f32 {
    (hash_u32(x, y, seed) >> 8) as f32 / 16_777_215.0
}

pub fn rand2(x: i32, y: i32, seed: i32) -> (f32, f32) {
    (rand01(x, y, seed), rand01(x, y, seed.wrapping_add(0x5bd1)))
}

fn quintic(t: f32) -> f32 {
    t * t * t * (t * (t * 6.0 - 15.0) + 10.0)
}

/// Periodic value noise (0..1) with `px` × `py` lattice cells over the unit square.
pub fn value_noise2(u: f32, v: f32, px: i32, py: i32, seed: i32) -> f32 {
    let px = px.max(1);
    let py = py.max(1);
    let x = u * px as f32;
    let y = v * py as f32;
    let ix = x.floor() as i32;
    let iy = y.floor() as i32;
    let fx = x - ix as f32;
    let fy = y - iy as f32;
    let sx = fx * fx * (3.0 - 2.0 * fx);
    let sy = fy * fy * (3.0 - 2.0 * fy);
    let h = |dx: i32, dy: i32| rand01(wrap_i(ix + dx, px), wrap_i(iy + dy, py), seed);
    let top = h(0, 0) + (h(1, 0) - h(0, 0)) * sx;
    let bottom = h(0, 1) + (h(1, 1) - h(0, 1)) * sx;
    top + (bottom - top) * sy
}

pub fn value_noise(u: f32, v: f32, period: i32, seed: i32) -> f32 {
    value_noise2(u, v, period, period, seed)
}

/// Periodic gradient (Perlin) noise remapped to 0..1.
pub fn perlin2(u: f32, v: f32, px: i32, py: i32, seed: i32) -> f32 {
    let px = px.max(1);
    let py = py.max(1);
    let x = u * px as f32;
    let y = v * py as f32;
    let ix = x.floor() as i32;
    let iy = y.floor() as i32;
    let fx = x - ix as f32;
    let fy = y - iy as f32;
    let sx = quintic(fx);
    let sy = quintic(fy);
    let grad = |dx: i32, dy: i32, ox: f32, oy: f32| {
        let angle = rand01(wrap_i(ix + dx, px), wrap_i(iy + dy, py), seed) * std::f32::consts::TAU;
        angle.cos() * ox + angle.sin() * oy
    };
    let n00 = grad(0, 0, fx, fy);
    let n10 = grad(1, 0, fx - 1.0, fy);
    let n01 = grad(0, 1, fx, fy - 1.0);
    let n11 = grad(1, 1, fx - 1.0, fy - 1.0);
    let top = n00 + (n10 - n00) * sx;
    let bottom = n01 + (n11 - n01) * sx;
    (0.5 + (top + (bottom - top) * sy) * 0.95).clamp(0.0, 1.0)
}

pub fn perlin(u: f32, v: f32, period: i32, seed: i32) -> f32 {
    perlin2(u, v, period, period, seed)
}

/// Basis functions usable inside [`fbm`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Basis {
    Value,
    Perlin,
    Simplex,
    Ridged,
    Billow,
    Turbulence,
}

pub fn basis_sample(basis: Basis, u: f32, v: f32, period: i32, seed: i32) -> f32 {
    match basis {
        Basis::Value => value_noise(u, v, period, seed),
        Basis::Perlin => perlin(u, v, period, seed),
        Basis::Simplex => {
            let d = 0.37 / period.max(1) as f32;
            0.5 * (perlin(u, v, period, seed) + perlin(u + d, v + d * 1.6, period, seed + 7))
        }
        Basis::Ridged => {
            let n = perlin(u, v, period, seed);
            let r = 1.0 - (n * 2.0 - 1.0).abs();
            r * r
        }
        Basis::Billow => {
            let n = perlin(u, v, period, seed);
            (n * 2.0 - 1.0).abs()
        }
        Basis::Turbulence => {
            let n = perlin(u, v, period, seed);
            (n * 2.0 - 1.0).abs().sqrt()
        }
    }
}

/// Fractal sum over an integer-period lattice; every octave keeps an integer
/// period so the result tiles seamlessly.
#[allow(clippy::too_many_arguments)]
pub fn fbm(
    basis: Basis,
    u: f32,
    v: f32,
    period: i32,
    seed: i32,
    octaves: i32,
    persistence: f32,
    lacunarity: f32,
) -> f32 {
    let mut total = 0.0;
    let mut amplitude = 1.0;
    let mut norm = 0.0;
    let mut p = period.max(1) as f32;
    for octave in 0..octaves.clamp(1, 12) {
        let period_i = (p.round() as i32).clamp(1, 4096);
        total += basis_sample(basis, u, v, period_i, seed.wrapping_add(octave * 131)) * amplitude;
        norm += amplitude;
        amplitude *= persistence.clamp(0.05, 1.0);
        p *= lacunarity.clamp(1.0, 4.0);
    }
    total / norm.max(1e-4)
}

/// Contrast around 0.5 plus bias, clamped.
pub fn shape_value(v: f32, contrast: f32, bias: f32) -> f32 {
    clamp01((v - 0.5) * contrast + 0.5 + bias)
}

/// Periodic cellular noise over `period` × `period` cells.
#[derive(Debug, Clone, Copy)]
pub struct Cell {
    pub f1: f32,
    pub f2: f32,
    /// Random 0..1 id of the nearest cell.
    pub id: f32,
    /// Vector from the pixel to the nearest feature point, in cell units.
    pub dx: f32,
    pub dy: f32,
    /// Integer cell coordinates (wrapped) of the nearest feature point.
    pub cx: i32,
    pub cy: i32,
}

pub fn worley(u: f32, v: f32, period: i32, seed: i32, jitter: f32, manhattan: bool) -> Cell {
    let period = period.max(1);
    let x = u * period as f32;
    let y = v * period as f32;
    let ix = x.floor() as i32;
    let iy = y.floor() as i32;
    let mut best = Cell {
        f1: f32::MAX,
        f2: f32::MAX,
        id: 0.0,
        dx: 0.0,
        dy: 0.0,
        cx: 0,
        cy: 0,
    };
    for oy in -1..=1 {
        for ox in -1..=1 {
            let cx = ix + ox;
            let cy = iy + oy;
            let wx = wrap_i(cx, period);
            let wy = wrap_i(cy, period);
            let (rx, ry) = rand2(wx, wy, seed);
            let px = cx as f32 + 0.5 + (rx - 0.5) * jitter;
            let py = cy as f32 + 0.5 + (ry - 0.5) * jitter;
            let dx = px - x;
            let dy = py - y;
            let d = if manhattan {
                dx.abs() + dy.abs()
            } else {
                (dx * dx + dy * dy).sqrt()
            };
            if d < best.f1 {
                best.f2 = best.f1;
                best.f1 = d;
                best.id = rand01(wx, wy, seed.wrapping_add(991));
                best.dx = dx;
                best.dy = dy;
                best.cx = wx;
                best.cy = wy;
            } else if d < best.f2 {
                best.f2 = d;
            }
        }
    }
    best
}

// ----------------------------------------------------------- image sampling

/// Bilinear sample with wrap-around.
pub fn sample_wrap(image: &[[f32; 4]], res: usize, u: f32, v: f32) -> [f32; 4] {
    let x = u * res as f32 - 0.5;
    let y = v * res as f32 - 0.5;
    let x0 = x.floor();
    let y0 = y.floor();
    let fx = x - x0;
    let fy = y - y0;
    let r = res as i32;
    let at = |ix: i32, iy: i32| image[wrap_i(iy, r) as usize * res + wrap_i(ix, r) as usize];
    let (ix, iy) = (x0 as i32, y0 as i32);
    let a = mix4(at(ix, iy), at(ix + 1, iy), fx);
    let b = mix4(at(ix, iy + 1), at(ix + 1, iy + 1), fx);
    mix4(a, b, fy)
}

/// Bilinear sample that returns transparent black outside the unit square.
pub fn sample_clip(image: &[[f32; 4]], res: usize, u: f32, v: f32) -> [f32; 4] {
    if !(0.0..1.0).contains(&u) || !(0.0..1.0).contains(&v) {
        return [0.0; 4];
    }
    let x = (u * res as f32 - 0.5).max(0.0);
    let y = (v * res as f32 - 0.5).max(0.0);
    let x0 = x.floor() as usize;
    let y0 = y.floor() as usize;
    let x1 = (x0 + 1).min(res - 1);
    let y1 = (y0 + 1).min(res - 1);
    let fx = x - x0 as f32;
    let fy = y - y0 as f32;
    let a = mix4(image[y0 * res + x0], image[y0 * res + x1], fx);
    let b = mix4(image[y1 * res + x0], image[y1 * res + x1], fx);
    mix4(a, b, fy)
}

pub fn at_wrap(plane: &[f32], res: usize, x: i32, y: i32) -> f32 {
    let r = res as i32;
    plane[wrap_i(y, r) as usize * res + wrap_i(x, r) as usize]
}

pub fn sample_plane_wrap(plane: &[f32], res: usize, u: f32, v: f32) -> f32 {
    let x = u * res as f32 - 0.5;
    let y = v * res as f32 - 0.5;
    let x0 = x.floor();
    let y0 = y.floor();
    let fx = x - x0;
    let fy = y - y0;
    let (ix, iy) = (x0 as i32, y0 as i32);
    let a = at_wrap(plane, res, ix, iy) * (1.0 - fx) + at_wrap(plane, res, ix + 1, iy) * fx;
    let b = at_wrap(plane, res, ix, iy + 1) * (1.0 - fx) + at_wrap(plane, res, ix + 1, iy + 1) * fx;
    a * (1.0 - fy) + b * fy
}

/// Separable Gaussian blur of a plane, wrap-around.
pub fn gaussian_plane(plane: &[f32], res: usize, radius: f32) -> Vec<f32> {
    if radius < 0.3 {
        return plane.to_vec();
    }
    let sigma = (radius / 2.0).max(0.4);
    let reach = (sigma * 3.0).ceil() as i32;
    let mut kernel: Vec<f32> = (-reach..=reach)
        .map(|i| (-(i * i) as f32 / (2.0 * sigma * sigma)).exp())
        .collect();
    let sum: f32 = kernel.iter().sum();
    for k in kernel.iter_mut() {
        *k /= sum;
    }
    let mut horizontal = vec![0.0f32; res * res];
    let k = &kernel;
    {
        let rows = par_scalar(res, |x, y, _, _| {
            let mut acc = 0.0;
            for (i, w) in k.iter().enumerate() {
                acc += w * at_wrap(plane, res, x as i32 + i as i32 - reach, y as i32);
            }
            acc
        });
        horizontal.copy_from_slice(&rows);
    }
    let h = &horizontal;
    par_scalar(res, |x, y, _, _| {
        let mut acc = 0.0;
        for (i, w) in k.iter().enumerate() {
            acc += w * at_wrap(h, res, x as i32, y as i32 + i as i32 - reach);
        }
        acc
    })
}

/// Normalises a plane to 0..1 (no-op for flat planes).
pub fn normalize_plane(plane: &mut [f32]) {
    let (mut lo, mut hi) = (f32::MAX, f32::MIN);
    for v in plane.iter() {
        lo = lo.min(*v);
        hi = hi.max(*v);
    }
    if hi - lo < 1e-6 {
        return;
    }
    for v in plane.iter_mut() {
        *v = (*v - lo) / (hi - lo);
    }
}

#[cfg(test)]
mod tests;

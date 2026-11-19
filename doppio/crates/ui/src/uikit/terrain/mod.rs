//! **Terrain Generator**: heightfields built with nodes.
//!
//! The graph carries one kind of data: a *field*, a square grid of 0..1 values that is either a height
//! map or a mask. Sources (Perlin, Ridged, Voronoi, Mountain, Island, Gradient) make fields,
//! modifiers reshape them (Levels, Terrace, Stratify, Warp, Blur, Combine), derivers make masks (Slope,
//! Height Range), and the simulations (Thermal, Hydraulic erosion) wear the shape down and return wear
//! and deposit masks. The **Output** node turns the final height into a terrain: size, maximum height,
//! resolution and a four-layer splat map computed from slope, height and the sea level.
//!
//! Every pattern lives on the unit square, so the 129 x 129 preview and the 513 x 513 result show the
//! same landscape. Pure Rust and deterministic for the seeds.

pub mod erosion;
pub mod field;
pub mod preview;
#[cfg(test)]
mod tests;

use crate::uikit::components::node_canvas;
use crate::uikit::procedural::{choice, float, int, ParamKind, ParamSpec};
use field::{fbm, perlin, ridged, voronoi, Field};
use iced::Point;
use std::collections::HashMap;

pub type NodeId = node_canvas::NodeId;

#[derive(Debug, Clone, Copy)]
pub struct PortDef {
    pub label: &'static str,
}

const fn port(label: &'static str) -> PortDef {
    PortDef { label }
}

pub struct Args<'a> {
    pub res: usize,
    pub params: &'a [f32],
    pub inputs: Vec<Option<&'a Field>>,
    /// Distinguishes nodes of the same kind so two Perlin nodes do not share a pattern.
    pub salt: u32,
}

impl<'a> Args<'a> {
    fn f(&self, i: usize) -> f32 {
        self.params.get(i).copied().unwrap_or(0.0)
    }
    fn u(&self, i: usize) -> u32 {
        self.f(i).round().max(0.0) as u32
    }
    fn seed(&self, i: usize) -> u32 {
        self.u(i).wrapping_mul(7919).wrapping_add(self.salt)
    }
    fn input(&self, i: usize) -> Option<&'a Field> {
        self.inputs.get(i).copied().flatten()
    }
    fn blank(&self, v: f32) -> Field {
        Field::new(self.res, v)
    }
}

pub struct NodeDef {
    pub id: &'static str,
    pub title: &'static str,
    pub category: &'static str,
    pub inputs: &'static [PortDef],
    pub outputs: &'static [PortDef],
    pub params: &'static [ParamSpec],
    /// Labels of the text fields the node has (asset paths).
    pub strings: &'static [&'static str],
    pub eval: fn(&Args) -> Vec<Field>,
}

impl std::fmt::Debug for NodeDef {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "NodeDef({})", self.id)
    }
}

const NONE: &[PortDef] = &[];
const OUT: &[PortDef] = &[port("Out")];
const IN: &[PortDef] = &[port("In")];

// ------------------------------------------------------------------ sources

const PERLIN_P: &[ParamSpec] = &[
    float("scale", "Scale", 0.5, 24.0, 3.0, 0.05),
    int("octaves", "Octaves", 1, 10, 6),
    float("persistence", "Persistence", 0.2, 0.85, 0.5, 0.01),
    float("lacunarity", "Lacunarity", 1.5, 3.5, 2.0, 0.05),
    int("seed", "Seed", 0, 9999, 1),
];
fn eval_perlin(a: &Args) -> Vec<Field> {
    let seed = a.seed(4);
    vec![Field::from_fn(a.res, |x, y| {
        fbm(x * a.f(0), y * a.f(0), seed, a.u(1), a.f(2), a.f(3))
    })
    .normalised()]
}

const RIDGED_P: &[ParamSpec] = &[
    float("scale", "Scale", 0.5, 24.0, 3.0, 0.05),
    int("octaves", "Octaves", 1, 10, 6),
    float("gain", "Ridge gain", 0.5, 4.0, 2.0, 0.05),
    float("lacunarity", "Lacunarity", 1.5, 3.5, 2.0, 0.05),
    int("seed", "Seed", 0, 9999, 1),
];
fn eval_ridged(a: &Args) -> Vec<Field> {
    let seed = a.seed(4);
    vec![Field::from_fn(a.res, |x, y| {
        ridged(x * a.f(0), y * a.f(0), seed, a.u(1), a.f(2), a.f(3))
    })
    .normalised()]
}

const VORONOI_MODES: &[&str] = &["Cell value", "Distance", "Edges", "Cracks"];
const VORONOI_P: &[ParamSpec] = &[
    float("scale", "Scale", 1.0, 40.0, 6.0, 0.1),
    float("jitter", "Jitter", 0.0, 1.0, 1.0, 0.01),
    choice("mode", "Mode", VORONOI_MODES, 1),
    int("seed", "Seed", 0, 9999, 1),
];
fn eval_voronoi(a: &Args) -> Vec<Field> {
    let seed = a.seed(3);
    let mode = a.u(2);
    vec![Field::from_fn(a.res, |x, y| {
        let (f1, f2, id) = voronoi(x * a.f(0), y * a.f(0), seed, a.f(1));
        match mode {
            0 => id,
            1 => 1.0 - (f1 * 1.2).min(1.0),
            2 => ((f2 - f1) * 2.5).min(1.0),
            _ => (1.0 - ((f2 - f1) * 6.0).min(1.0)).powi(2),
        }
    })]
}

const MOUNTAIN_P: &[ParamSpec] = &[
    float("x", "Centre X", 0.0, 1.0, 0.5, 0.005),
    float("y", "Centre Y", 0.0, 1.0, 0.5, 0.005),
    float("radius", "Radius", 0.1, 1.0, 0.45, 0.01),
    float("height", "Height", 0.1, 1.0, 0.9, 0.01),
    float("ruggedness", "Ruggedness", 0.0, 1.0, 0.55, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
/// A massif: a falloff cone broken up by ridged noise so it has ridges, spurs and valleys.
fn eval_mountain(a: &Args) -> Vec<Field> {
    let seed = a.seed(5);
    vec![Field::from_fn(a.res, |x, y| {
        let (dx, dy) = (x - a.f(0), y - a.f(1));
        let warp = perlin(x * 3.0, y * 3.0, seed ^ 0x55) * 0.07;
        let d = (((dx + warp).powi(2) + (dy - warp).powi(2)).sqrt() / a.f(2)).min(1.0);
        let cone = (1.0 - d).powf(1.35);
        let rough = ridged(x * 4.0, y * 4.0, seed, 7, 2.0, 2.1);
        (cone * (0.45 + 0.9 * a.f(4) * rough + 0.2 * (1.0 - a.f(4))) * a.f(3)).clamp(0.0, 1.0)
    })]
}

const ISLAND_P: &[ParamSpec] = &[
    float("radius", "Radius", 0.1, 0.7, 0.42, 0.005),
    float("falloff", "Falloff", 0.02, 0.6, 0.28, 0.005),
    float("shape", "Shape noise", 0.0, 0.4, 0.14, 0.005),
    int("seed", "Seed", 0, 9999, 1),
];
fn eval_island(a: &Args) -> Vec<Field> {
    let seed = a.seed(3);
    vec![Field::from_fn(a.res, |x, y| {
        let d = ((x - 0.5).powi(2) + (y - 0.5).powi(2)).sqrt()
            + (fbm(x * 4.0, y * 4.0, seed, 4, 0.55, 2.0) - 0.5) * a.f(2);
        let t = ((a.f(0) - d) / a.f(1) + 0.5).clamp(0.0, 1.0);
        t * t * (3.0 - 2.0 * t)
    })]
}

const GRADIENT_P: &[ParamSpec] = &[
    float("angle", "Direction (deg)", 0.0, 360.0, 0.0, 1.0),
    float("from", "From", 0.0, 1.0, 0.0, 0.01),
    float("to", "To", 0.0, 1.0, 1.0, 0.01),
];
fn eval_gradient(a: &Args) -> Vec<Field> {
    let (s, c) = a.f(0).to_radians().sin_cos();
    vec![Field::from_fn(a.res, |x, y| {
        let t = ((x - 0.5) * c + (y - 0.5) * s + 0.5).clamp(0.0, 1.0);
        a.f(1) + (a.f(2) - a.f(1)) * t
    })]
}

const CONSTANT_P: &[ParamSpec] = &[float("value", "Value", 0.0, 1.0, 0.3, 0.005)];
fn eval_constant(a: &Args) -> Vec<Field> {
    vec![a.blank(a.f(0))]
}

// ------------------------------------------------------------------ modifiers

const LEVELS_P: &[ParamSpec] = &[
    float("low", "Black point", 0.0, 1.0, 0.0, 0.005),
    float("high", "White point", 0.0, 1.0, 1.0, 0.005),
    float("gamma", "Gamma", 0.2, 4.0, 1.0, 0.01),
    float("out_low", "Output low", 0.0, 1.0, 0.0, 0.005),
    float("out_high", "Output high", 0.0, 1.0, 1.0, 0.005),
];
fn eval_levels(a: &Args) -> Vec<Field> {
    let Some(src) = a.input(0) else {
        return vec![a.blank(0.0)];
    };
    let span = (a.f(1) - a.f(0)).max(1e-4);
    vec![src.map(|v| {
        let t = ((v - a.f(0)) / span).clamp(0.0, 1.0).powf(1.0 / a.f(2));
        a.f(3) + (a.f(4) - a.f(3)) * t
    })]
}

const TERRACE_P: &[ParamSpec] = &[
    int("steps", "Steps", 2, 40, 8),
    float("sharpness", "Sharpness", 0.0, 1.0, 0.7, 0.01),
    float("mix", "Mix with original", 0.0, 1.0, 1.0, 0.01),
];
fn eval_terrace(a: &Args) -> Vec<Field> {
    let Some(src) = a.input(0) else {
        return vec![a.blank(0.0)];
    };
    let steps = a.f(0).max(2.0);
    vec![src.map(|v| {
        let t = v * steps;
        let (base, frac) = (t.floor(), t - t.floor());
        let k = 1.0 + a.f(1) * 14.0;
        let eased = if frac < 0.5 {
            0.5 * (2.0 * frac).powf(k)
        } else {
            1.0 - 0.5 * (2.0 * (1.0 - frac)).powf(k)
        };
        let stepped = (base + eased) / steps;
        v + (stepped - v) * a.f(2)
    })]
}

const STRATIFY_P: &[ParamSpec] = &[
    float("layers", "Layers", 2.0, 80.0, 18.0, 0.5),
    float("strength", "Strength", 0.0, 1.0, 0.5, 0.01),
    float("tilt", "Tilt noise", 0.0, 1.0, 0.35, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];
/// Sedimentary strata: the height is cut into slightly tilted layers with hard edges.
fn eval_stratify(a: &Args) -> Vec<Field> {
    let Some(src) = a.input(0) else {
        return vec![a.blank(0.0)];
    };
    let seed = a.seed(3);
    let res = a.res;
    let mut out = Field::new(res, 0.0);
    let step = 1.0 / (res - 1) as f32;
    for y in 0..res {
        for x in 0..res {
            let (u, v) = (x as f32 * step, y as f32 * step);
            let h = src.at(x, y);
            let tilt = (perlin(u * 2.0, v * 2.0, seed) * a.f(2)) / a.f(0).max(1.0);
            let t = (h + tilt) * a.f(0);
            let (base, frac) = (t.floor(), t - t.floor());
            let cliff = (1.0 - (frac * 6.0).min(1.0)).powi(2) * 0.4 + frac * 0.6;
            let layered = (base + cliff) / a.f(0) - tilt;
            out.data[y * res + x] = h + (layered - h) * a.f(1);
        }
    }
    vec![out]
}

const WARP_P: &[ParamSpec] = &[
    float("amount", "Amount", 0.0, 0.3, 0.06, 0.002),
    float("scale", "Scale", 0.5, 16.0, 3.0, 0.05),
    int("seed", "Seed", 0, 9999, 1),
];
fn eval_warp(a: &Args) -> Vec<Field> {
    let Some(src) = a.input(0) else {
        return vec![a.blank(0.0)];
    };
    let seed = a.seed(2);
    vec![Field::from_fn(a.res, |x, y| {
        let dx = perlin(x * a.f(1), y * a.f(1), seed) * a.f(0);
        let dy = perlin(x * a.f(1) + 17.3, y * a.f(1) - 9.1, seed ^ 0xA5) * a.f(0);
        src.sample(x + dx, y + dy)
    })]
}

const BLUR_P: &[ParamSpec] = &[float("radius", "Radius", 0.0, 0.08, 0.01, 0.001)];
fn eval_blur(a: &Args) -> Vec<Field> {
    let Some(src) = a.input(0) else {
        return vec![a.blank(0.0)];
    };
    vec![src.blur((a.f(0) * (a.res - 1) as f32).round() as usize)]
}

const SLOPE_P: &[ParamSpec] = &[
    float("scale", "Scale", 0.1, 8.0, 1.0, 0.05),
    float("blur", "Smoothing", 0.0, 0.03, 0.004, 0.001),
];
/// A mask of how steep the surface is (gradient magnitude, stretched by Scale).
fn eval_slope(a: &Args) -> Vec<Field> {
    let Some(src) = a.input(0) else {
        return vec![a.blank(0.0)];
    };
    let radius = (a.f(1) * (a.res - 1) as f32).round() as usize;
    vec![src
        .slope(a.f(0) * 0.35)
        .blur(radius)
        .map(|v| v.clamp(0.0, 1.0))]
}

const RANGE_P: &[ParamSpec] = &[
    float("low", "From", 0.0, 1.0, 0.3, 0.005),
    float("high", "To", 0.0, 1.0, 0.7, 0.005),
    float("falloff", "Falloff", 0.0, 0.5, 0.1, 0.005),
];
/// A mask of the heights between two values, with soft edges.
fn eval_range(a: &Args) -> Vec<Field> {
    let Some(src) = a.input(0) else {
        return vec![a.blank(0.0)];
    };
    let f = a.f(2).max(1e-4);
    vec![src.map(|v| {
        let up = ((v - (a.f(0) - f)) / f).clamp(0.0, 1.0);
        let down = (((a.f(1) + f) - v) / f).clamp(0.0, 1.0);
        let m = up.min(down);
        m * m * (3.0 - 2.0 * m)
    })]
}

const INVERT_P: &[ParamSpec] = &[];
fn eval_invert(a: &Args) -> Vec<Field> {
    vec![a
        .input(0)
        .map_or_else(|| a.blank(1.0), |s| s.map(|v| 1.0 - v))]
}

const COMBINE_MODES: &[&str] = &[
    "Add",
    "Subtract",
    "Multiply",
    "Maximum",
    "Minimum",
    "Blend",
    "Screen",
    "Difference",
];
const COMBINE_IN: &[PortDef] = &[port("A"), port("B"), port("Mask")];
const COMBINE_P: &[ParamSpec] = &[
    choice("mode", "Mode", COMBINE_MODES, 5),
    float("amount", "Amount", 0.0, 1.0, 0.5, 0.01),
];
fn eval_combine(a: &Args) -> Vec<Field> {
    let base = a.input(0).cloned().unwrap_or_else(|| a.blank(0.0));
    let Some(other) = a.input(1) else {
        return vec![base];
    };
    let mode = a.u(0);
    let amount = a.f(1);
    let mask = a.input(2);
    let mut out = Field::new(a.res, 0.0);
    for i in 0..out.data.len() {
        let (x, y) = (base.data[i], other.data[i]);
        let combined = match mode {
            0 => x + y * amount,
            1 => x - y * amount,
            2 => x * (1.0 - amount + y * amount),
            3 => x.max(y * amount.max(1e-3) + x * (1.0 - amount)),
            4 => x.min(y),
            5 => x + (y - x) * amount,
            6 => 1.0 - (1.0 - x) * (1.0 - y * amount),
            _ => (x - y).abs(),
        };
        let m = mask.map_or(1.0, |m| m.data[i]);
        out.data[i] = x + (combined - x) * m;
    }
    vec![out]
}

// ------------------------------------------------------------------ simulations

const THERMAL_P: &[ParamSpec] = &[
    int("iterations", "Iterations", 1, 120, 30),
    float("talus", "Talus (slope)", 0.05, 4.0, 0.7, 0.01),
    float("strength", "Strength", 0.0, 1.0, 0.6, 0.01),
];
fn eval_thermal(a: &Args) -> Vec<Field> {
    let Some(src) = a.input(0) else {
        return vec![a.blank(0.0)];
    };
    vec![erosion::thermal(src, a.u(0), a.f(1), a.f(2))]
}

const HYDRO_OUT: &[PortDef] = &[port("Height"), port("Wear"), port("Deposit")];
const HYDRO_P: &[ParamSpec] = &[
    int("droplets", "Droplets (thousands)", 1, 400, 40),
    float("inertia", "Inertia", 0.0, 0.6, 0.12, 0.01),
    float("capacity", "Sediment capacity", 0.2, 4.0, 1.4, 0.05),
    float("erosion", "Erosion", 0.05, 1.0, 0.35, 0.01),
    float("deposition", "Deposition", 0.05, 1.0, 0.3, 0.01),
    float("evaporation", "Evaporation", 0.002, 0.08, 0.015, 0.001),
    int("seed", "Seed", 0, 9999, 1),
];
fn eval_hydraulic(a: &Args) -> Vec<Field> {
    let Some(src) = a.input(0) else {
        return vec![a.blank(0.0), a.blank(0.0), a.blank(0.0)];
    };
    // Droplet count scales with the grid area so the preview shows the same amount of erosion.
    let area = (a.res as f32 / 257.0).powi(2);
    let droplets = (a.f(0) * 1000.0 * area).round().max(200.0) as u32;
    let eroded = erosion::hydraulic(
        src,
        erosion::Hydraulic {
            droplets,
            inertia: a.f(1),
            capacity: a.f(2),
            erosion: a.f(3),
            deposition: a.f(4),
            evaporation: a.f(5),
            seed: a.seed(6),
        },
    );
    vec![eroded.height, eroded.wear, eroded.deposit]
}

// ------------------------------------------------------------------ output

const RESOLUTIONS: &[&str] = &["129", "257", "513"];
/// The four splat layers the terrain is painted with: project materials (their albedo map is the
/// layer's picture), not built-ins.
pub const LAYER_LABELS: &[&str] = &[
    "Ground material (.mat)",
    "Rock material (.mat)",
    "Dirt material (.mat)",
    "Mud material (.mat)",
];

const OUTPUT_IN: &[PortDef] = &[port("Height")];
const OUTPUT_P: &[ParamSpec] = &[
    float("size_x", "Size X (m)", 50.0, 8000.0, 1000.0, 10.0),
    float("size_z", "Size Z (m)", 50.0, 8000.0, 1000.0, 10.0),
    float("max_height", "Max height (m)", 5.0, 3000.0, 260.0, 1.0),
    choice("resolution", "Resolution", RESOLUTIONS, 1),
    float("sea", "Sea level", 0.0, 0.9, 0.2, 0.005),
    float("beach", "Beach width", 0.0, 0.1, 0.02, 0.002),
    float("rock_slope", "Rock above slope", 0.1, 3.0, 0.7, 0.01),
    float("rock_line", "Bare rock above", 0.3, 1.0, 0.82, 0.005),
];
fn eval_output(_: &Args) -> Vec<Field> {
    Vec::new()
}

macro_rules! node {
    ($id:expr, $title:expr, $cat:expr, $in:expr, $out:expr, $params:expr, $eval:expr) => {
        NodeDef {
            id: $id,
            title: $title,
            category: $cat,
            inputs: $in,
            outputs: $out,
            params: $params,
            strings: &[],
            eval: $eval,
        }
    };
}

pub const CATEGORIES: [&str; 6] = [
    "Sources",
    "Modifiers",
    "Derive",
    "Combine",
    "Simulate",
    "Output",
];

pub static NODES: &[NodeDef] = &[
    node!(
        "perlin",
        "Perlin",
        "Sources",
        NONE,
        OUT,
        PERLIN_P,
        eval_perlin
    ),
    node!(
        "ridged",
        "Ridged",
        "Sources",
        NONE,
        OUT,
        RIDGED_P,
        eval_ridged
    ),
    node!(
        "voronoi",
        "Voronoi",
        "Sources",
        NONE,
        OUT,
        VORONOI_P,
        eval_voronoi
    ),
    node!(
        "mountain",
        "Mountain",
        "Sources",
        NONE,
        OUT,
        MOUNTAIN_P,
        eval_mountain
    ),
    node!(
        "island",
        "Island",
        "Sources",
        NONE,
        OUT,
        ISLAND_P,
        eval_island
    ),
    node!(
        "gradient",
        "Gradient",
        "Sources",
        NONE,
        OUT,
        GRADIENT_P,
        eval_gradient
    ),
    node!(
        "constant",
        "Constant",
        "Sources",
        NONE,
        OUT,
        CONSTANT_P,
        eval_constant
    ),
    node!(
        "levels",
        "Levels",
        "Modifiers",
        IN,
        OUT,
        LEVELS_P,
        eval_levels
    ),
    node!(
        "terrace",
        "Terrace",
        "Modifiers",
        IN,
        OUT,
        TERRACE_P,
        eval_terrace
    ),
    node!(
        "stratify",
        "Stratify",
        "Modifiers",
        IN,
        OUT,
        STRATIFY_P,
        eval_stratify
    ),
    node!("warp", "Warp", "Modifiers", IN, OUT, WARP_P, eval_warp),
    node!("blur", "Blur", "Modifiers", IN, OUT, BLUR_P, eval_blur),
    node!(
        "invert",
        "Invert",
        "Modifiers",
        IN,
        OUT,
        INVERT_P,
        eval_invert
    ),
    node!("slope", "Slope", "Derive", IN, OUT, SLOPE_P, eval_slope),
    node!(
        "range",
        "Height Range",
        "Derive",
        IN,
        OUT,
        RANGE_P,
        eval_range
    ),
    node!(
        "combine",
        "Combine",
        "Combine",
        COMBINE_IN,
        OUT,
        COMBINE_P,
        eval_combine
    ),
    node!(
        "thermal",
        "Thermal Erosion",
        "Simulate",
        IN,
        OUT,
        THERMAL_P,
        eval_thermal
    ),
    node!(
        "hydraulic",
        "Hydraulic Erosion",
        "Simulate",
        IN,
        HYDRO_OUT,
        HYDRO_P,
        eval_hydraulic
    ),
    NodeDef {
        id: "output",
        title: "Output",
        category: "Output",
        inputs: OUTPUT_IN,
        outputs: NONE,
        params: OUTPUT_P,
        strings: LAYER_LABELS,
        eval: eval_output,
    },
];

pub fn find(id: &str) -> Option<&'static NodeDef> {
    NODES.iter().find(|n| n.id == id)
}

// ------------------------------------------------------------------ graph

#[derive(Debug, Clone, PartialEq)]
pub struct TerrainNode {
    pub id: NodeId,
    pub op: &'static str,
    pub params: Vec<f32>,
    pub strings: Vec<String>,
    pub position: Point,
}

impl TerrainNode {
    pub fn def(&self) -> &'static NodeDef {
        find(self.op).unwrap_or_else(|| panic!("unknown terrain op {}", self.op))
    }
}

/// What the Output node makes of the graph.
#[derive(Debug, Clone, PartialEq)]
pub struct TerrainModel {
    pub height: Field,
    /// RGBA8 splat weights (ground, rock, dirt, mud), same grid as `height`.
    pub splat: Vec<u8>,
    pub size: [f32; 2],
    pub max_height: f32,
    /// Sea level in metres (for placing a water body).
    pub sea_metres: f32,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct TerrainGraph {
    pub nodes: Vec<TerrainNode>,
    pub links: Vec<node_canvas::Link>,
    pub next_id: NodeId,
}

impl TerrainGraph {
    pub fn new() -> Self {
        TerrainGraph {
            nodes: Vec::new(),
            links: Vec::new(),
            next_id: 1,
        }
    }

    pub fn add(&mut self, op: &'static str, position: Point) -> NodeId {
        let def = find(op).unwrap_or_else(|| panic!("unknown terrain op {op}"));
        let id = self.next_id.max(1);
        self.next_id = id + 1;
        self.nodes.push(TerrainNode {
            id,
            op: def.id,
            params: def.params.iter().map(|p| p.default).collect(),
            strings: def.strings.iter().map(|_| String::new()).collect(),
            position,
        });
        id
    }

    pub fn add_with(&mut self, op: &'static str, position: Point, set: &[(&str, f32)]) -> NodeId {
        let id = self.add(op, position);
        for (key, value) in set {
            self.set(id, key, *value);
        }
        id
    }

    pub fn node(&self, id: NodeId) -> Option<&TerrainNode> {
        self.nodes.iter().find(|n| n.id == id)
    }

    pub fn node_mut(&mut self, id: NodeId) -> Option<&mut TerrainNode> {
        self.nodes.iter_mut().find(|n| n.id == id)
    }

    pub fn set(&mut self, id: NodeId, key: &str, value: f32) {
        if let Some(node) = self.node_mut(id) {
            let def = node.def();
            let i = def
                .params
                .iter()
                .position(|p| p.key == key)
                .unwrap_or_else(|| panic!("op {} has no parameter {key}", node.op));
            let spec = &def.params[i];
            let v = if matches!(spec.kind, ParamKind::Float) {
                value
            } else {
                value.round()
            };
            node.params[i] = v.clamp(spec.min, spec.max);
        }
    }

    pub fn get(&self, id: NodeId, key: &str) -> Option<f32> {
        let node = self.node(id)?;
        let i = node.def().params.iter().position(|p| p.key == key)?;
        node.params.get(i).copied()
    }

    pub fn remove(&mut self, id: NodeId) {
        self.nodes.retain(|n| n.id != id);
        self.links.retain(|l| l.from != id && l.to != id);
    }

    pub fn set_string(&mut self, id: NodeId, index: usize, text: String) {
        if let Some(slot) = self
            .node_mut(id)
            .and_then(|node| node.strings.get_mut(index))
        {
            *slot = text;
        }
    }

    /// The four layer texture paths on the Output node (empty until they are written).
    pub fn layer_paths(&self) -> Vec<String> {
        self.output_node()
            .and_then(|id| self.node(id))
            .map(|n| n.strings.clone())
            .unwrap_or_default()
    }

    pub fn output_node(&self) -> Option<NodeId> {
        self.nodes.iter().find(|n| n.op == "output").map(|n| n.id)
    }

    pub fn connect(&mut self, from: NodeId, output: usize, to: NodeId, input: usize) -> bool {
        if from == to {
            return false;
        }
        let (Some(a), Some(b)) = (self.node(from), self.node(to)) else {
            return false;
        };
        if a.def().outputs.get(output).is_none() || b.def().inputs.get(input).is_none() {
            return false;
        }
        let mut stack = vec![from];
        let mut seen = std::collections::HashSet::new();
        while let Some(node) = stack.pop() {
            if node == to {
                return false;
            }
            if seen.insert(node) {
                stack.extend(self.links.iter().filter(|l| l.to == node).map(|l| l.from));
            }
        }
        self.links.retain(|l| !(l.to == to && l.input == input));
        self.links.push(node_canvas::Link {
            from,
            output,
            to,
            input,
        });
        true
    }

    fn eval_node(
        &self,
        id: NodeId,
        res: usize,
        memo: &mut HashMap<NodeId, Vec<Field>>,
        stack: &mut Vec<NodeId>,
    ) -> Result<(), String> {
        if memo.contains_key(&id) {
            return Ok(());
        }
        if stack.contains(&id) {
            return Err("The graph contains a cycle".to_owned());
        }
        stack.push(id);
        let node = self.node(id).ok_or("A node is missing")?;
        let def = node.def();
        for input in 0..def.inputs.len() {
            if let Some(link) = self.links.iter().find(|l| l.to == id && l.input == input) {
                self.eval_node(link.from, res, memo, stack)?;
            }
        }
        let inputs: Vec<Option<&Field>> = (0..def.inputs.len())
            .map(|input| {
                self.links
                    .iter()
                    .find(|l| l.to == id && l.input == input)
                    .and_then(|l| memo.get(&l.from).and_then(|v| v.get(l.output)))
            })
            .collect();
        let fields = (def.eval)(&Args {
            res,
            params: &node.params,
            inputs,
            salt: (id as u32).wrapping_mul(2_654_435_761),
        });
        stack.pop();
        memo.insert(id, fields);
        Ok(())
    }

    /// Evaluates one node's outputs at `res` (per-node previews and tests).
    pub fn evaluate(&self, id: NodeId, res: usize) -> Result<Vec<Field>, String> {
        let mut memo = HashMap::new();
        self.eval_node(id, res, &mut memo, &mut Vec::new())?;
        Ok(memo.remove(&id).unwrap_or_default())
    }

    /// The resolution the Output node asks for.
    pub fn output_resolution(&self) -> usize {
        self.output_node()
            .and_then(|id| self.get(id, "resolution"))
            .map_or(257, |i| {
                [129usize, 257, 513][(i.round().max(0.0) as usize).min(2)]
            })
    }

    /// Builds the terrain at `res` (use a small value for previews).
    pub fn build(&self, res: usize) -> Result<TerrainModel, String> {
        let output = self.output_node().ok_or("The graph needs an Output node")?;
        let mut memo = HashMap::new();
        self.eval_node(output, res, &mut memo, &mut Vec::new())?;
        let node = self.node(output).ok_or("Output node is missing")?;
        let height = self
            .links
            .iter()
            .find(|l| l.to == output && l.input == 0)
            .and_then(|l| memo.get(&l.from).and_then(|v| v.get(l.output)))
            .cloned()
            .ok_or("Wire a height into the Output node")?
            .map(|v| v.clamp(0.0, 1.0));
        let p = &node.params;
        let splat = splat_weights(&height, p[0].max(p[1]), p[2], p[4], p[5], p[6], p[7]);
        Ok(TerrainModel {
            height,
            splat,
            size: [p[0], p[1]],
            max_height: p[2],
            sea_metres: p[4] * p[2],
        })
    }

    // -------------------------------------------------------------- persistence

    pub fn to_text(&self) -> String {
        let mut text = String::from("TERRAINGRAPH1\n");
        for node in &self.nodes {
            text.push_str(&format!(
                "node {} {} {} {} {}",
                node.id,
                node.position.x,
                node.position.y,
                node.op,
                node.params.len()
            ));
            for v in &node.params {
                text.push_str(&format!(" {v}"));
            }
            if !node.strings.is_empty() {
                text.push_str(&format!(" {}", node.strings.len()));
                for s in &node.strings {
                    text.push(' ');
                    if s.is_empty() {
                        text.push_str("%00");
                    } else {
                        text.push_str(&s.replace('%', "%25").replace(' ', "%20"));
                    }
                }
            }
            text.push('\n');
        }
        for l in &self.links {
            text.push_str(&format!(
                "link {} {} {} {}\n",
                l.from, l.output, l.to, l.input
            ));
        }
        text
    }

    pub fn from_text(text: &str) -> Option<TerrainGraph> {
        let mut lines = text.lines();
        if lines.next()?.trim() != "TERRAINGRAPH1" {
            return None;
        }
        let mut graph = TerrainGraph::new();
        for line in lines {
            let w: Vec<&str> = line.split_whitespace().collect();
            match w.first().copied() {
                Some("node") if w.len() >= 6 => {
                    let id: NodeId = w[1].parse().ok()?;
                    let position = Point::new(w[2].parse().ok()?, w[3].parse().ok()?);
                    let def = find(w[4])?;
                    let n: usize = w[5].parse().ok()?;
                    let mut params: Vec<f32> = def.params.iter().map(|p| p.default).collect();
                    for (i, word) in w.get(6..6 + n)?.iter().enumerate() {
                        if let Some(slot) = params.get_mut(i) {
                            *slot = word.parse().ok()?;
                        }
                    }
                    let mut strings: Vec<String> =
                        def.strings.iter().map(|_| String::new()).collect();
                    if let Some(count) = w.get(6 + n).and_then(|c| c.parse::<usize>().ok()) {
                        for (i, word) in w.get(7 + n..7 + n + count)?.iter().enumerate() {
                            if let Some(slot) = strings.get_mut(i) {
                                *slot = if *word == "%00" {
                                    String::new()
                                } else {
                                    word.replace("%20", " ").replace("%25", "%")
                                };
                            }
                        }
                    }
                    graph.next_id = graph.next_id.max(id + 1);
                    graph.nodes.push(TerrainNode {
                        id,
                        op: def.id,
                        params,
                        strings,
                        position,
                    });
                }
                Some("link") if w.len() >= 5 => graph.links.push(node_canvas::Link {
                    from: w[1].parse().ok()?,
                    output: w[2].parse().ok()?,
                    to: w[3].parse().ok()?,
                    input: w[4].parse().ok()?,
                }),
                _ => {}
            }
        }
        graph.output_node().is_some().then_some(graph)
    }
}

fn smoothstep(a: f32, b: f32, x: f32) -> f32 {
    let t = ((x - a) / (b - a)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

/// Four splat layers from slope, height and the sea level: **ground** (grass and moss), **rock**
/// (steep faces and bare summits), **dirt** (the beach band) and **mud** (below the sea level).
fn splat_weights(
    height: &Field,
    size: f32,
    max_height: f32,
    sea: f32,
    beach: f32,
    rock_slope: f32,
    rock_line: f32,
) -> Vec<u8> {
    let n = height.res;
    // Real steepness (rise over run) from the unit-height gradient.
    let slope = height.slope(max_height / size.max(1.0));
    let mut out = vec![0u8; n * n * 4];
    for i in 0..n * n {
        let h = height.data[i];
        let s = slope.data[i];
        let rock = smoothstep(rock_slope - 0.18, rock_slope + 0.18, s).max(smoothstep(
            rock_line - 0.04,
            rock_line + 0.08,
            h,
        ));
        let mud = smoothstep(sea + 0.004, sea - 0.012, h);
        let dirt = smoothstep(sea - 0.004, sea + 0.004, h)
            * smoothstep(sea + beach + 0.01, sea + beach * 0.4, h);
        let rest = (1.0 - rock).max(0.0);
        let ground = rest * (1.0 - dirt) * (1.0 - mud);
        let rock_w = rock * (1.0 - mud);
        let dirt_w = rest * dirt * (1.0 - mud);
        let total = (ground + rock_w + dirt_w + mud).max(1e-5);
        let byte = |v: f32| ((v / total) * 255.0).round().clamp(0.0, 255.0) as u8;
        out[i * 4] = byte(ground);
        out[i * 4 + 1] = byte(rock_w);
        out[i * 4 + 2] = byte(dirt_w);
        out[i * 4 + 3] = byte(mud);
    }
    out
}

/// File header of a `.terrain-graph` file: `CAFTERRAIN1`, the name, then the graph.
const FILE_HEADER: &str = "CAFTERRAIN1";

pub fn to_text(name: &str, graph: &TerrainGraph) -> String {
    format!("{FILE_HEADER}\nname {name}\n{}", graph.to_text())
}

pub fn from_text(text: &str) -> Option<(String, TerrainGraph)> {
    let mut lines = text.splitn(3, '\n');
    if lines.next()?.trim() != FILE_HEADER {
        return None;
    }
    let name = lines.next()?.strip_prefix("name ")?.trim().to_owned();
    Some((name, TerrainGraph::from_text(lines.next()?)?))
}

// ------------------------------------------------------------------ presets

pub struct TerrainPreset {
    pub name: &'static str,
    pub category: &'static str,
    pub description: &'static str,
    build: fn() -> TerrainGraph,
}

impl TerrainPreset {
    pub fn graph(&self) -> TerrainGraph {
        (self.build)()
    }
}

fn at(col: f32, row: f32) -> Point {
    Point::new(col * 190.0, row * 130.0)
}

/// Adds a chain of `(op, params)` nodes along a row, returning the last node.
fn chain(
    g: &mut TerrainGraph,
    row: f32,
    first: NodeId,
    steps: &[(&'static str, &[(&str, f32)])],
) -> NodeId {
    let mut last = first;
    for (i, (op, set)) in steps.iter().enumerate() {
        let n = g.add_with(op, at(1.0 + i as f32, row), set);
        assert!(g.connect(last, 0, n, 0), "{op} cannot follow");
        last = n;
    }
    last
}

fn finish(g: &mut TerrainGraph, last: NodeId, col: f32, row: f32, set: &[(&str, f32)]) {
    let out = g.add_with("output", at(col, row), set);
    assert!(g.connect(last, 0, out, 0));
}

pub fn presets() -> Vec<TerrainPreset> {
    vec![
        TerrainPreset {
            name: "Rolling Hills",
            category: "Gentle",
            description: "Soft, wooded hills worn by a little thermal erosion.",
            build: || {
                let mut g = TerrainGraph::new();
                let base = g.add_with(
                    "perlin",
                    at(0.0, 0.0),
                    &[("scale", 2.2), ("octaves", 5.0), ("persistence", 0.45)],
                );
                let last = chain(
                    &mut g,
                    0.0,
                    base,
                    &[
                        (
                            "levels",
                            &[("low", 0.12), ("high", 0.92), ("out_high", 0.7)],
                        ),
                        ("thermal", &[("iterations", 25.0), ("talus", 0.5)]),
                    ],
                );
                finish(
                    &mut g,
                    last,
                    4.0,
                    0.0,
                    &[("max_height", 240.0), ("sea", 0.0), ("rock_slope", 1.1)],
                );
                g
            },
        },
        TerrainPreset {
            name: "Alpine Range",
            category: "Mountains",
            description:
                "A ridged massif cut by hydraulic erosion, with scree slopes and bare summits.",
            build: || {
                let mut g = TerrainGraph::new();
                let base = g.add_with(
                    "ridged",
                    at(0.0, 0.0),
                    &[("scale", 2.4), ("octaves", 7.0), ("gain", 2.2)],
                );
                let hills = g.add_with(
                    "perlin",
                    at(0.0, 1.2),
                    &[("scale", 1.4), ("octaves", 4.0), ("seed", 5.0)],
                );
                let mix = g.add_with("combine", at(1.0, 0.5), &[("mode", 5.0), ("amount", 0.28)]);
                assert!(g.connect(base, 0, mix, 0));
                assert!(g.connect(hills, 0, mix, 1));
                let last = chain(
                    &mut g,
                    0.5,
                    mix,
                    &[
                        ("levels", &[("low", 0.2), ("high", 0.95), ("gamma", 1.25)]),
                        ("hydraulic", &[("droplets", 60.0)]),
                        ("thermal", &[("iterations", 20.0), ("talus", 0.9)]),
                    ],
                );
                // The Hydraulic node has three outputs; the height is the first.
                finish(
                    &mut g,
                    last,
                    5.0,
                    0.5,
                    &[("max_height", 420.0), ("sea", 0.0), ("rock_line", 0.7)],
                );
                g
            },
        },
        TerrainPreset {
            name: "Volcanic Island",
            category: "Coast",
            description: "A single cone with a ridged flank, an island mask and a surrounding sea.",
            build: || {
                let mut g = TerrainGraph::new();
                let cone = g.add_with(
                    "mountain",
                    at(0.0, 0.0),
                    &[("radius", 0.5), ("height", 0.95), ("ruggedness", 0.7)],
                );
                let island = g.add_with(
                    "island",
                    at(0.0, 1.2),
                    &[("radius", 0.4), ("falloff", 0.22)],
                );
                let masked = g.add_with("combine", at(1.0, 0.5), &[("mode", 2.0), ("amount", 1.0)]);
                assert!(g.connect(cone, 0, masked, 0));
                assert!(g.connect(island, 0, masked, 1));
                let last = chain(
                    &mut g,
                    0.5,
                    masked,
                    &[
                        (
                            "hydraulic",
                            &[("droplets", 14.0), ("seed", 3.0), ("capacity", 0.8)],
                        ),
                        ("thermal", &[("iterations", 12.0)]),
                    ],
                );
                finish(
                    &mut g,
                    last,
                    4.0,
                    0.5,
                    &[("max_height", 420.0), ("sea", 0.12), ("beach", 0.025)],
                );
                g
            },
        },
        TerrainPreset {
            name: "Badlands",
            category: "Arid",
            description:
                "Layered mesas and cliffs: terraces, strata and a Voronoi mask for the mesas.",
            build: || {
                let mut g = TerrainGraph::new();
                let base = g.add_with(
                    "perlin",
                    at(0.0, 0.0),
                    &[("scale", 2.6), ("octaves", 5.0), ("seed", 9.0)],
                );
                let last = chain(
                    &mut g,
                    0.0,
                    base,
                    &[
                        ("warp", &[("amount", 0.05), ("scale", 4.0)]),
                        ("terrace", &[("steps", 7.0), ("sharpness", 0.85)]),
                        ("stratify", &[("layers", 30.0), ("strength", 0.45)]),
                        ("thermal", &[("iterations", 15.0), ("talus", 1.4)]),
                    ],
                );
                finish(
                    &mut g,
                    last,
                    6.0,
                    0.0,
                    &[("max_height", 240.0), ("sea", 0.0), ("rock_slope", 0.45)],
                );
                g
            },
        },
        TerrainPreset {
            name: "Archipelago",
            category: "Coast",
            description: "Many islands: Voronoi cells shaped by noise, with eroded shores.",
            build: || {
                let mut g = TerrainGraph::new();
                let cells = g.add_with(
                    "voronoi",
                    at(0.0, 0.0),
                    &[("scale", 4.0), ("mode", 1.0), ("jitter", 0.9)],
                );
                let noise = g.add_with("perlin", at(0.0, 1.2), &[("scale", 5.0), ("octaves", 5.0)]);
                let mix = g.add_with("combine", at(1.0, 0.5), &[("mode", 5.0), ("amount", 0.4)]);
                assert!(g.connect(cells, 0, mix, 0));
                assert!(g.connect(noise, 0, mix, 1));
                let last = chain(
                    &mut g,
                    0.5,
                    mix,
                    &[
                        (
                            "levels",
                            &[("low", 0.58), ("high", 0.98), ("out_high", 0.85)],
                        ),
                        ("hydraulic", &[("droplets", 30.0), ("seed", 7.0)]),
                    ],
                );
                finish(
                    &mut g,
                    last,
                    4.0,
                    0.5,
                    &[("max_height", 220.0), ("sea", 0.11), ("beach", 0.02)],
                );
                g
            },
        },
        TerrainPreset {
            name: "Highland Lake",
            category: "Mountains",
            description:
                "A mountain basin: an inverted island carves a lake bed into rolling highlands.",
            build: || {
                let mut g = TerrainGraph::new();
                let hills = g.add_with(
                    "ridged",
                    at(0.0, 0.0),
                    &[
                        ("scale", 2.0),
                        ("octaves", 6.0),
                        ("gain", 1.6),
                        ("seed", 4.0),
                    ],
                );
                let basin = g.add_with(
                    "island",
                    at(0.0, 1.2),
                    &[("radius", 0.28), ("falloff", 0.2)],
                );
                let inv = g.add_with("invert", at(1.0, 1.2), &[]);
                assert!(g.connect(basin, 0, inv, 0));
                let carved =
                    g.add_with("combine", at(2.0, 0.5), &[("mode", 2.0), ("amount", 0.85)]);
                assert!(g.connect(hills, 0, carved, 0));
                assert!(g.connect(inv, 0, carved, 1));
                let last = chain(
                    &mut g,
                    0.5,
                    carved,
                    &[
                        ("levels", &[("low", 0.0), ("high", 0.85), ("gamma", 1.1)]),
                        ("hydraulic", &[("droplets", 45.0), ("seed", 2.0)]),
                    ],
                );
                finish(
                    &mut g,
                    last,
                    5.0,
                    0.5,
                    &[("max_height", 340.0), ("sea", 0.07), ("rock_line", 0.74)],
                );
                g
            },
        },
    ]
}

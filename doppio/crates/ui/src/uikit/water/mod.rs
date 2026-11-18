//! **Water Generator**: water surfaces built with nodes.
//!
//! Instead of one fixed wave block, a water body is a graph. *Wave sources* (a single Gerstner
//! wave, a wind-driven sea, a distant swell, ripples) produce lists of waves; *modifiers* scale and
//! merge them; a **Look** node sets colour, clarity, roughness and the foam threshold; the **Output**
//! node sets the size, depth and mesh resolution of the body. Evaluating the graph gives a
//! [`doppio_engine::Water`]: the same data the engine animates (CPU and vertex mesh) and that
//! gameplay can sample for floating objects.
//!
//! The engine evaluates at most 16 explicit waves, which is why *Merge* keeps the strongest ones:
//! a few well-chosen waves with the right mix of wavelengths read as a sea, and the cost stays low.
//!
//! Pure Rust and deterministic: tests need no engine.

pub mod preview;
#[cfg(test)]
mod tests;

use crate::uikit::components::node_canvas;
use crate::uikit::procedural::{choice, float, int, ParamKind, ParamSpec};
use doppio_engine::{Water, WaterWave, MAX_WATER_WAVES};
use iced::Point;
use std::collections::HashMap;

pub type NodeId = node_canvas::NodeId;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Waves,
    Look,
}

#[derive(Debug, Clone, Copy)]
pub struct PortDef {
    pub label: &'static str,
    pub kind: Kind,
}

const fn port(label: &'static str, kind: Kind) -> PortDef {
    PortDef { label, kind }
}

/// Colour, clarity and shading of the water.
#[derive(Debug, Clone, PartialEq)]
pub struct Look {
    pub albedo: [f32; 4],
    pub roughness: f32,
    pub foam_threshold: f32,
    /// The `.mat` asset the water is drawn with, when the look comes from one.
    pub asset: Option<String>,
}

#[derive(Debug, Clone)]
pub enum Value {
    Waves(Vec<WaterWave>),
    Look(Look),
}

pub struct Args<'a> {
    pub params: &'a [f32],
    pub strings: &'a [String],
    pub inputs: Vec<Option<&'a Value>>,
}

impl<'a> Args<'a> {
    fn f(&self, i: usize) -> f32 {
        self.params.get(i).copied().unwrap_or(0.0)
    }
    fn u(&self, i: usize) -> u32 {
        self.f(i).round().max(0.0) as u32
    }
    fn waves(&self, i: usize) -> Vec<WaterWave> {
        match self.inputs.get(i).copied().flatten() {
            Some(Value::Waves(w)) => w.clone(),
            _ => Vec::new(),
        }
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
    pub eval: fn(&Args) -> Vec<Value>,
}

impl std::fmt::Debug for NodeDef {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "NodeDef({})", self.id)
    }
}

const GRAVITY: f32 = 9.81;

fn direction(degrees: f32) -> [f32; 2] {
    let r = degrees.to_radians();
    [r.cos(), r.sin()]
}

/// Small deterministic RNG.
struct Rng(u32);
impl Rng {
    fn new(seed: u32) -> Self {
        Rng(seed.wrapping_mul(2_654_435_761).max(1))
    }
    fn unit(&mut self) -> f32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 17;
        self.0 ^= self.0 << 5;
        self.0 as f32 / u32::MAX as f32
    }
    fn range(&mut self, lo: f32, hi: f32) -> f32 {
        lo + (hi - lo) * self.unit()
    }
}

/// Limits `sum(Q * k * A)` so crests never fold over themselves (the Gerstner stability bound).
fn limit_steepness(waves: &mut [WaterWave], choppiness: f32) {
    let n = waves.len().max(1) as f32;
    for wave in waves.iter_mut() {
        let k = std::f32::consts::TAU / wave.wavelength.max(0.05);
        let q = choppiness * 0.85 / n / (k * wave.amplitude).max(1e-5);
        wave.steepness = q.clamp(0.0, 1.2);
    }
}

// ------------------------------------------------------------------ nodes

const WAVES_OUT: &[PortDef] = &[port("Waves", Kind::Waves)];
const WAVES_IN: &[PortDef] = &[port("Waves", Kind::Waves)];
const NONE: &[PortDef] = &[];

const WAVE_P: &[ParamSpec] = &[
    float("direction", "Direction (deg)", 0.0, 360.0, 35.0, 1.0),
    float("wavelength", "Wavelength (m)", 0.2, 300.0, 12.0, 0.1),
    float("amplitude", "Amplitude (m)", 0.0, 5.0, 0.3, 0.005),
    float("steepness", "Steepness", 0.0, 1.2, 0.4, 0.01),
    float("phase", "Phase", 0.0, std::f32::consts::TAU, 0.0, 0.01),
];
fn eval_wave(a: &Args) -> Vec<Value> {
    vec![Value::Waves(vec![WaterWave {
        direction: direction(a.f(0)),
        wavelength: a.f(1),
        amplitude: a.f(2),
        steepness: a.f(3),
        phase: a.f(4),
    }])]
}

const WIND_P: &[ParamSpec] = &[
    float("wind", "Wind speed (m/s)", 0.0, 40.0, 12.0, 0.1),
    float("direction", "Direction (deg)", 0.0, 360.0, 35.0, 1.0),
    float("spread", "Directional spread (deg)", 0.0, 90.0, 40.0, 1.0),
    int("count", "Waves", 1, 12, 8),
    float("choppiness", "Choppiness", 0.0, 1.0, 0.6, 0.01),
    float("min_wavelength", "Shortest wave (m)", 0.2, 20.0, 1.5, 0.05),
    int("seed", "Seed", 0, 9999, 1),
];
/// A wind sea: wavelengths spread between the shortest wave and the longest the wind can raise,
/// with more energy in the long ones (a Pierson-Moskowitz style budget).
fn eval_wind(a: &Args) -> Vec<Value> {
    let wind = a.f(0).max(0.0);
    let count = a.u(3).clamp(1, 12) as usize;
    if wind < 0.05 {
        return vec![Value::Waves(Vec::new())];
    }
    let significant = 0.21 * wind * wind / GRAVITY;
    let longest = (std::f32::consts::TAU * wind * wind / GRAVITY * 0.45).clamp(a.f(5) * 2.0, 160.0);
    let shortest = a.f(5).min(longest);
    let mut rng = Rng::new(a.u(6) + 17);
    let spread = a.f(2).to_radians();
    let base = a.f(1).to_radians();
    let weights: Vec<f32> = (0..count)
        .map(|i| 1.0 / (1.0 + i as f32 / count.max(2) as f32 * 2.6))
        .collect();
    let total: f32 = weights.iter().sum();
    let mut waves: Vec<WaterWave> = (0..count)
        .map(|i| {
            let t = i as f32 / (count - 1).max(1) as f32;
            let wavelength = (longest + (shortest - longest) * t * t) * rng.range(0.92, 1.08);
            let angle = base + rng.range(-spread, spread);
            WaterWave {
                direction: [angle.cos(), angle.sin()],
                wavelength,
                amplitude: (significant * 0.5 * weights[i] / total).max(0.0005),
                steepness: 0.0,
                phase: rng.range(0.0, std::f32::consts::TAU),
            }
        })
        .collect();
    limit_steepness(&mut waves, a.f(4));
    vec![Value::Waves(waves)]
}

const SWELL_P: &[ParamSpec] = &[
    float("direction", "Direction (deg)", 0.0, 360.0, 20.0, 1.0),
    float("wavelength", "Wavelength (m)", 20.0, 400.0, 90.0, 1.0),
    float("height", "Height (m)", 0.0, 6.0, 0.8, 0.01),
    int("count", "Trains", 1, 4, 2),
    float("spread", "Spread (deg)", 0.0, 40.0, 8.0, 0.5),
    int("seed", "Seed", 0, 9999, 1),
];
/// Long, regular waves that travelled far from their storm.
fn eval_swell(a: &Args) -> Vec<Value> {
    let count = a.u(3).clamp(1, 4) as usize;
    let mut rng = Rng::new(a.u(5) + 31);
    let mut waves: Vec<WaterWave> = (0..count)
        .map(|i| {
            let angle = a.f(0).to_radians() + rng.range(-1.0, 1.0) * a.f(4).to_radians();
            WaterWave {
                direction: [angle.cos(), angle.sin()],
                wavelength: a.f(1) * (1.0 - 0.18 * i as f32) * rng.range(0.95, 1.05),
                amplitude: a.f(2) * 0.5 / (1.0 + i as f32 * 0.7),
                steepness: 0.0,
                phase: rng.range(0.0, std::f32::consts::TAU),
            }
        })
        .collect();
    limit_steepness(&mut waves, 0.5);
    vec![Value::Waves(waves)]
}

const RIPPLE_P: &[ParamSpec] = &[
    int("count", "Ripples", 1, 6, 4),
    float("wavelength", "Wavelength (m)", 0.1, 5.0, 0.7, 0.01),
    float("amplitude", "Amplitude (m)", 0.0, 0.3, 0.025, 0.001),
    float("direction", "Direction (deg)", 0.0, 360.0, 60.0, 1.0),
    float("chaos", "Chaos (deg)", 0.0, 180.0, 110.0, 1.0),
    int("seed", "Seed", 0, 9999, 1),
];
/// Short capillary-like ripples from many directions: the fine surface detail.
fn eval_ripples(a: &Args) -> Vec<Value> {
    let count = a.u(0).clamp(1, 6) as usize;
    let mut rng = Rng::new(a.u(5) + 53);
    let mut waves: Vec<WaterWave> = (0..count)
        .map(|_| {
            let angle = a.f(3).to_radians() + rng.range(-1.0, 1.0) * a.f(4).to_radians();
            WaterWave {
                direction: [angle.cos(), angle.sin()],
                wavelength: a.f(1) * rng.range(0.7, 1.4),
                amplitude: a.f(2) * rng.range(0.6, 1.0),
                steepness: 0.0,
                phase: rng.range(0.0, std::f32::consts::TAU),
            }
        })
        .collect();
    limit_steepness(&mut waves, 0.6);
    vec![Value::Waves(waves)]
}

const MERGE_IN: &[PortDef] = &[
    port("Waves 1", Kind::Waves),
    port("Waves 2", Kind::Waves),
    port("Waves 3", Kind::Waves),
    port("Waves 4", Kind::Waves),
];
fn eval_merge(a: &Args) -> Vec<Value> {
    let mut all: Vec<WaterWave> = (0..4).flat_map(|i| a.waves(i)).collect();
    // The engine evaluates 16 waves: keep the ones that shape the surface most.
    all.sort_by(|x, y| y.amplitude.total_cmp(&x.amplitude));
    all.truncate(MAX_WATER_WAVES);
    vec![Value::Waves(all)]
}

const SCALE_P: &[ParamSpec] = &[
    float("amplitude", "Amplitude x", 0.0, 4.0, 1.0, 0.01),
    float("wavelength", "Wavelength x", 0.1, 4.0, 1.0, 0.01),
    float("choppiness", "Choppiness x", 0.0, 3.0, 1.0, 0.01),
    float("rotate", "Rotate (deg)", -180.0, 180.0, 0.0, 1.0),
];
fn eval_scale(a: &Args) -> Vec<Value> {
    let turn = a.f(3).to_radians();
    let (sin, cos) = turn.sin_cos();
    let waves = a
        .waves(0)
        .into_iter()
        .map(|mut w| {
            w.amplitude *= a.f(0);
            w.wavelength = (w.wavelength * a.f(1)).max(0.05);
            w.steepness = (w.steepness * a.f(2)).clamp(0.0, 1.2);
            let d = w.direction;
            w.direction = [d[0] * cos - d[1] * sin, d[0] * sin + d[1] * cos];
            w
        })
        .collect();
    vec![Value::Waves(waves)]
}

const LOOK_OUT: &[PortDef] = &[port("Look", Kind::Look)];
const LOOK_P: &[ParamSpec] = &[
    float("r", "Red", 0.0, 1.0, 0.03, 0.005),
    float("g", "Green", 0.0, 1.0, 0.16, 0.005),
    float("b", "Blue", 0.0, 1.0, 0.30, 0.005),
    float("opacity", "Opacity (low = clear)", 0.05, 1.0, 0.78, 0.01),
    float("roughness", "Roughness", 0.02, 0.6, 0.05, 0.005),
    float("foam", "Foam threshold", 0.2, 1.0, 0.72, 0.01),
];
fn eval_look(a: &Args) -> Vec<Value> {
    vec![Value::Look(Look {
        albedo: [a.f(0), a.f(1), a.f(2), a.f(3)],
        roughness: a.f(4),
        foam_threshold: a.f(5),
        asset: None,
    })]
}

/// Your own `.mat`: the water is drawn with it exactly as it is.
fn eval_look_asset(a: &Args) -> Vec<Value> {
    match a.strings.first().filter(|p| !p.is_empty()) {
        Some(path) => vec![Value::Look(Look {
            albedo: [0.05, 0.25, 0.35, 0.85],
            roughness: 0.08,
            foam_threshold: 0.72,
            asset: Some(path.clone()),
        })],
        None => Vec::new(),
    }
}

const OUTPUT_IN: &[PortDef] = &[port("Waves", Kind::Waves), port("Material", Kind::Look)];
const RESOLUTIONS: &[&str] = &["32", "64", "96", "128", "192", "256"];
const OUTPUT_P: &[ParamSpec] = &[
    float("size_x", "Size X (m)", 2.0, 1000.0, 64.0, 1.0),
    float("size_z", "Size Z (m)", 2.0, 1000.0, 64.0, 1.0),
    float("level", "Water level (m)", -50.0, 200.0, 0.0, 0.05),
    float("depth", "Depth (m)", 0.1, 2000.0, 30.0, 0.1),
    choice("resolution", "Mesh resolution", RESOLUTIONS, 2),
    float("time_scale", "Time scale", 0.0, 4.0, 1.0, 0.05),
    int("seed", "Seed", 0, 9999, 1),
];
fn eval_output(_: &Args) -> Vec<Value> {
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

pub const CATEGORIES: [&str; 4] = ["Waves", "Modifiers", "Look", "Output"];

pub static NODES: &[NodeDef] = &[
    node!(
        "wave",
        "Gerstner Wave",
        "Waves",
        NONE,
        WAVES_OUT,
        WAVE_P,
        eval_wave
    ),
    node!("wind", "Wind Sea", "Waves", NONE, WAVES_OUT, WIND_P, eval_wind),
    node!("swell", "Swell", "Waves", NONE, WAVES_OUT, SWELL_P, eval_swell),
    node!(
        "ripples",
        "Ripples",
        "Waves",
        NONE,
        WAVES_OUT,
        RIPPLE_P,
        eval_ripples
    ),
    node!(
        "merge",
        "Merge Waves",
        "Modifiers",
        MERGE_IN,
        WAVES_OUT,
        &[],
        eval_merge
    ),
    node!(
        "scale",
        "Scale Waves",
        "Modifiers",
        WAVES_IN,
        WAVES_OUT,
        SCALE_P,
        eval_scale
    ),
    node!(
        "look",
        "Water Material",
        "Look",
        NONE,
        LOOK_OUT,
        LOOK_P,
        eval_look
    ),
    NodeDef {
        id: "look_asset",
        title: "Your Water Material (.mat)",
        category: "Look",
        inputs: NONE,
        outputs: LOOK_OUT,
        params: &[],
        strings: &["Your material (.mat)"],
        eval: eval_look_asset,
    },
    node!(
        "output",
        "Output",
        "Output",
        OUTPUT_IN,
        NONE,
        OUTPUT_P,
        eval_output
    ),
];

pub fn find(id: &str) -> Option<&'static NodeDef> {
    NODES.iter().find(|n| n.id == id)
}

// ------------------------------------------------------------------ graph

#[derive(Debug, Clone, PartialEq)]
pub struct WaterNode {
    pub id: NodeId,
    pub op: &'static str,
    pub params: Vec<f32>,
    pub strings: Vec<String>,
    pub position: Point,
}

impl WaterNode {
    pub fn def(&self) -> &'static NodeDef {
        find(self.op).unwrap_or_else(|| panic!("unknown water op {}", self.op))
    }
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct WaterGraph {
    pub nodes: Vec<WaterNode>,
    pub links: Vec<node_canvas::Link>,
    pub next_id: NodeId,
}

impl WaterGraph {
    pub fn new() -> Self {
        WaterGraph {
            nodes: Vec::new(),
            links: Vec::new(),
            next_id: 1,
        }
    }

    pub fn add(&mut self, op: &'static str, position: Point) -> NodeId {
        let def = find(op).unwrap_or_else(|| panic!("unknown water op {op}"));
        let id = self.next_id.max(1);
        self.next_id = id + 1;
        self.nodes.push(WaterNode {
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

    pub fn node(&self, id: NodeId) -> Option<&WaterNode> {
        self.nodes.iter().find(|n| n.id == id)
    }

    pub fn node_mut(&mut self, id: NodeId) -> Option<&mut WaterNode> {
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

    /// The `.mat` the water uses, when its material node points at one.
    pub fn look_asset(&self) -> Option<String> {
        self.nodes
            .iter()
            .find(|n| n.op == "look_asset")
            .and_then(|n| n.strings.first().cloned())
            .filter(|p| !p.is_empty())
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
        let (Some(out), Some(inp)) = (a.def().outputs.get(output), b.def().inputs.get(input))
        else {
            return false;
        };
        if out.kind != inp.kind {
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
        memo: &mut HashMap<NodeId, Vec<Value>>,
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
                self.eval_node(link.from, memo, stack)?;
            }
        }
        let inputs: Vec<Option<&Value>> = (0..def.inputs.len())
            .map(|input| {
                self.links
                    .iter()
                    .find(|l| l.to == id && l.input == input)
                    .and_then(|l| memo.get(&l.from).and_then(|v| v.get(l.output)))
            })
            .collect();
        let values = (def.eval)(&Args {
            params: &node.params,
            strings: &node.strings,
            inputs,
        });
        stack.pop();
        memo.insert(id, values);
        Ok(())
    }

    /// Evaluates the graph into the water the engine animates. Waves too fine for the mesh (the
    /// surface is vertex-displaced) are faded out: see [`build_report`](Self::build_report).
    pub fn build(&self) -> Result<Water, String> {
        self.build_report().map(|(water, _)| water)
    }

    /// Like [`build`](Self::build), plus how many waves the mesh resolution could not show.
    ///
    /// A wave needs at least about three mesh cells per wavelength; between 2.5 and 4 cells it fades
    /// out so nothing aliases into blocky patterns. Fine detail needs a higher mesh resolution or a
    /// smaller body (a normal-map layer for it is on the roadmap).
    pub fn build_report(&self) -> Result<(Water, usize), String> {
        let output = self.output_node().ok_or("The graph needs an Output node")?;
        let mut memo = HashMap::new();
        self.eval_node(output, &mut memo, &mut Vec::new())?;
        let node = self.node(output).ok_or("Output node is missing")?;
        let value = |input: usize| {
            self.links
                .iter()
                .find(|l| l.to == output && l.input == input)
                .and_then(|l| memo.get(&l.from).and_then(|v| v.get(l.output)))
        };
        let mut waves = match value(0) {
            Some(Value::Waves(w)) => w.clone(),
            _ => Vec::new(),
        };
        waves.truncate(MAX_WATER_WAVES);
        if waves.is_empty() {
            // Nothing wired: perfectly flat water (the engine would otherwise add a wind sea).
            waves.push(WaterWave {
                direction: [1.0, 0.0],
                wavelength: 10.0,
                amplitude: 0.0,
                steepness: 0.0,
                phase: 0.0,
            });
        }
        // Water is nothing without its material: it is wired in, never assumed.
        let look = match value(1) {
            Some(Value::Look(look)) => look.clone(),
            _ => {
                return Err(
                    "The water has no material: wire a Water Material node (Look category) into the Output's Material port"
                        .to_owned(),
                )
            }
        };
        let p = &node.params;
        let resolution = [32u32, 64, 96, 128, 192, 256][(p[4].round().max(0.0) as usize).min(5)];
        let cell = p[0].max(p[1]) / resolution as f32;
        let before = waves.iter().filter(|w| w.amplitude > 1e-5).count();
        for wave in &mut waves {
            let cells = wave.wavelength / cell.max(1e-4);
            let t = ((cells - 2.5) / 1.5).clamp(0.0, 1.0);
            wave.amplitude *= t * t * (3.0 - 2.0 * t);
        }
        waves.retain(|w| w.amplitude > 1e-5);
        let dropped = before.saturating_sub(waves.len());
        if waves.is_empty() {
            // Everything was too fine (or nothing is wired): flat water, not a wind sea.
            waves.push(WaterWave {
                direction: [1.0, 0.0],
                wavelength: 10.0,
                amplitude: 0.0,
                steepness: 0.0,
                phase: 0.0,
            });
        }
        let water = Water {
            enabled: true,
            size: [p[0], p[1]],
            level: p[2],
            depth: p[3],
            resolution,
            time_scale: p[5],
            albedo: look.albedo,
            roughness: look.roughness,
            foam_threshold: look.foam_threshold,
            seed: p[6].round().max(0.0) as u32,
            waves,
        };
        Ok((water, dropped))
    }

    // -------------------------------------------------------------- persistence

    pub fn to_text(&self) -> String {
        let mut text = String::from("WATERGRAPH1\n");
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

    pub fn from_text(text: &str) -> Option<WaterGraph> {
        let mut lines = text.lines();
        if lines.next()?.trim() != "WATERGRAPH1" {
            return None;
        }
        let mut graph = WaterGraph::new();
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
                    graph.nodes.push(WaterNode {
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

/// File header of a `.water` file: `CAFWATER1`, the name, then the graph.
const FILE_HEADER: &str = "CAFWATER1";

pub fn to_text(name: &str, graph: &WaterGraph) -> String {
    format!("{FILE_HEADER}\nname {name}\n{}", graph.to_text())
}

pub fn from_text(text: &str) -> Option<(String, WaterGraph)> {
    let mut lines = text.splitn(3, '\n');
    if lines.next()?.trim() != FILE_HEADER {
        return None;
    }
    let name = lines.next()?.strip_prefix("name ")?.trim().to_owned();
    Some((name, WaterGraph::from_text(lines.next()?)?))
}

// ------------------------------------------------------------------ presets

pub struct WaterPreset {
    pub name: &'static str,
    pub category: &'static str,
    pub description: &'static str,
    build: fn() -> WaterGraph,
}

impl WaterPreset {
    pub fn graph(&self) -> WaterGraph {
        (self.build)()
    }
}

fn at(col: f32, row: f32) -> Point {
    Point::new(col * 200.0, row * 140.0)
}

/// Builds `sources -> merge -> output` plus a look. `sources` are `(op, params)` pairs.
fn body(
    sources: &[(&'static str, &[(&str, f32)])],
    look: &[(&str, f32)],
    output: &[(&str, f32)],
) -> WaterGraph {
    let mut g = WaterGraph::new();
    let out = g.add_with("output", at(3.0, 0.5), output);
    let look_node = g.add_with("look", at(2.0, 2.0), look);
    assert!(g.connect(look_node, 0, out, 1));
    match sources {
        [] => {}
        [(op, set)] => {
            let n = g.add_with(op, at(0.0, 0.0), set);
            assert!(g.connect(n, 0, out, 0));
        }
        many => {
            let merge = g.add("merge", at(2.0, 0.0));
            for (i, (op, set)) in many.iter().take(4).enumerate() {
                let n = g.add_with(op, at(0.0, i as f32), set);
                assert!(g.connect(n, 0, merge, i));
            }
            assert!(g.connect(merge, 0, out, 0));
        }
    }
    g
}

pub fn presets() -> Vec<WaterPreset> {
    vec![
        WaterPreset {
            name: "Calm Lake",
            category: "Still",
            description: "Glassy water with a faint swell and fine ripples.",
            build: || {
                body(
                    &[
                        (
                            "swell",
                            &[("height", 0.12), ("wavelength", 30.0), ("count", 2.0)],
                        ),
                        (
                            "ripples",
                            &[("amplitude", 0.012), ("wavelength", 1.1), ("count", 5.0)],
                        ),
                    ],
                    &[
                        ("r", 0.04),
                        ("g", 0.20),
                        ("b", 0.26),
                        ("opacity", 0.74),
                        ("roughness", 0.03),
                    ],
                    &[
                        ("size_x", 64.0),
                        ("size_z", 64.0),
                        ("depth", 8.0),
                        ("resolution", 5.0),
                    ],
                )
            },
        },
        WaterPreset {
            name: "Pond",
            category: "Still",
            description: "Murky green water with tiny wind ripples.",
            build: || {
                body(
                    &[(
                        "ripples",
                        &[
                            ("amplitude", 0.02),
                            ("wavelength", 0.55),
                            ("count", 6.0),
                            ("chaos", 160.0),
                        ],
                    )],
                    &[
                        ("r", 0.07),
                        ("g", 0.17),
                        ("b", 0.08),
                        ("opacity", 0.9),
                        ("roughness", 0.05),
                    ],
                    &[
                        ("size_x", 24.0),
                        ("size_z", 24.0),
                        ("depth", 1.5),
                        ("resolution", 4.0),
                    ],
                )
            },
        },
        WaterPreset {
            name: "Swimming Pool",
            category: "Still",
            description: "Bright, clear, nearly flat water with small ripples.",
            build: || {
                body(
                    &[(
                        "ripples",
                        &[("amplitude", 0.01), ("wavelength", 0.55), ("count", 4.0)],
                    )],
                    &[
                        ("r", 0.10),
                        ("g", 0.55),
                        ("b", 0.75),
                        ("opacity", 0.45),
                        ("roughness", 0.02),
                    ],
                    &[
                        ("size_x", 12.0),
                        ("size_z", 25.0),
                        ("depth", 2.0),
                        ("resolution", 4.0),
                    ],
                )
            },
        },
        WaterPreset {
            name: "Tropical Lagoon",
            category: "Coast",
            description: "Turquoise, clear water with a gentle swell.",
            build: || {
                body(
                    &[
                        (
                            "swell",
                            &[("height", 0.3), ("wavelength", 40.0), ("direction", 60.0)],
                        ),
                        (
                            "wind",
                            &[
                                ("wind", 5.0),
                                ("count", 5.0),
                                ("choppiness", 0.3),
                                ("direction", 70.0),
                                ("min_wavelength", 4.0),
                            ],
                        ),
                    ],
                    &[
                        ("r", 0.02),
                        ("g", 0.45),
                        ("b", 0.50),
                        ("opacity", 0.50),
                        ("roughness", 0.04),
                        ("foam", 0.8),
                    ],
                    &[
                        ("size_x", 200.0),
                        ("size_z", 200.0),
                        ("depth", 6.0),
                        ("resolution", 5.0),
                    ],
                )
            },
        },
        WaterPreset {
            name: "River",
            category: "Inland",
            description:
                "A directional flow: stretched waves along the river and ripples across it.",
            build: || {
                body(
                    &[
                        (
                            "wave",
                            &[
                                ("direction", 0.0),
                                ("wavelength", 6.0),
                                ("amplitude", 0.07),
                                ("steepness", 0.3),
                            ],
                        ),
                        (
                            "wave",
                            &[
                                ("direction", 8.0),
                                ("wavelength", 2.4),
                                ("amplitude", 0.035),
                                ("steepness", 0.3),
                                ("phase", 1.7),
                            ],
                        ),
                        (
                            "ripples",
                            &[
                                ("direction", 0.0),
                                ("chaos", 35.0),
                                ("amplitude", 0.02),
                                ("wavelength", 1.3),
                            ],
                        ),
                    ],
                    &[
                        ("r", 0.08),
                        ("g", 0.22),
                        ("b", 0.18),
                        ("opacity", 0.7),
                        ("roughness", 0.06),
                    ],
                    &[
                        ("size_x", 80.0),
                        ("size_z", 20.0),
                        ("depth", 3.0),
                        ("resolution", 5.0),
                    ],
                )
            },
        },
        WaterPreset {
            name: "Open Sea",
            category: "Sea",
            description: "A wind sea with a long swell: crests, troughs and a little foam.",
            build: || {
                body(
                    &[
                        (
                            "wind",
                            &[
                                ("wind", 13.0),
                                ("count", 10.0),
                                ("choppiness", 0.65),
                                ("min_wavelength", 5.0),
                            ],
                        ),
                        ("swell", &[("height", 0.9), ("direction", 15.0)]),
                    ],
                    &[
                        ("r", 0.02),
                        ("g", 0.15),
                        ("b", 0.28),
                        ("opacity", 0.82),
                        ("roughness", 0.05),
                    ],
                    &[
                        ("size_x", 300.0),
                        ("size_z", 300.0),
                        ("depth", 200.0),
                        ("resolution", 5.0),
                    ],
                )
            },
        },
        WaterPreset {
            name: "Storm",
            category: "Sea",
            description: "High wind, tall steep waves, dark water and plenty of foam.",
            build: || {
                body(
                    &[
                        (
                            "wind",
                            &[
                                ("wind", 24.0),
                                ("count", 12.0),
                                ("choppiness", 0.95),
                                ("spread", 55.0),
                                ("min_wavelength", 7.0),
                            ],
                        ),
                        (
                            "swell",
                            &[("height", 2.4), ("wavelength", 140.0), ("count", 3.0)],
                        ),
                    ],
                    &[
                        ("r", 0.02),
                        ("g", 0.08),
                        ("b", 0.11),
                        ("opacity", 0.9),
                        ("roughness", 0.08),
                        ("foam", 0.5),
                    ],
                    &[
                        ("size_x", 400.0),
                        ("size_z", 400.0),
                        ("depth", 300.0),
                        ("resolution", 5.0),
                    ],
                )
            },
        },
        WaterPreset {
            name: "Swamp",
            category: "Inland",
            description: "Dark, almost opaque water that barely moves.",
            build: || {
                body(
                    &[(
                        "ripples",
                        &[("amplitude", 0.008), ("wavelength", 1.0), ("count", 3.0)],
                    )],
                    &[
                        ("r", 0.05),
                        ("g", 0.08),
                        ("b", 0.03),
                        ("opacity", 0.97),
                        ("roughness", 0.12),
                    ],
                    &[
                        ("size_x", 40.0),
                        ("size_z", 40.0),
                        ("depth", 1.0),
                        ("resolution", 5.0),
                    ],
                )
            },
        },
    ]
}

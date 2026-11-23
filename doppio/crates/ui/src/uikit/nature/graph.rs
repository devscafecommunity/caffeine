//! The **node graph** behind the Nature Generator.
//!
//! Models are built the way the material and texture generators build images: by wiring nodes. The
//! data flowing along the wires is geometry, not pixels, so it has its own port types:
//!
//! | Port | Carries |
//! |---|---|
//! | **Mesh** | a triangle mesh ([`Part`]) |
//! | **Skeleton** | branches as polylines with radii (the bones of a tree) |
//! | **Points** | positions with a normal, scale and yaw (scatter results) |
//!
//! A graph ends in an **Output** node. Every mesh wired into it passes through a **Part** node that
//! names it (`trunk`, `leaves`...) and gives it a material: a library preset, a Texture Generator
//! card, a plain colour, or **one of your own project materials / textures**. Parts become separate
//! entities in the scene because the engine gives one material to a mesh.

use super::mesh::*;
use super::Quality;
use crate::uikit::components::node_canvas;
use crate::uikit::procedural::{ParamKind, ParamSpec};
use iced::Point;
use std::collections::HashMap;

pub type NodeId = node_canvas::NodeId;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Mesh,
    Skeleton,
    Points,
    /// A material (library preset, card, solid colour or one of your own assets).
    Material,
}

impl Kind {
    pub fn label(self) -> &'static str {
        match self {
            Kind::Mesh => "mesh",
            Kind::Skeleton => "skeleton",
            Kind::Points => "points",
            Kind::Material => "material",
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct PortDef {
    pub label: &'static str,
    pub kind: Kind,
}

pub const fn port(label: &'static str, kind: Kind) -> PortDef {
    PortDef { label, kind }
}

/// A text parameter (an asset path).
#[derive(Debug, Clone, Copy)]
pub struct StringSpec {
    pub key: &'static str,
    pub label: &'static str,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PointInst {
    pub position: V3,
    pub normal: V3,
    pub scale: f32,
    pub yaw: f32,
}

/// A node output. Values live only for one graph evaluation, so the size gap between a whole
/// mesh and the other variants is not worth boxing every `Value::Mesh` match.
#[derive(Debug, Clone)]
#[allow(clippy::large_enum_variant)]
pub enum Value {
    Mesh(Part),
    Skeleton(super::tree::Skeleton),
    Points(Vec<PointInst>),
    Material(MaterialSpec),
}

/// What an op sees: its parameters, its inputs and the quality level.
pub struct Args<'a> {
    pub params: &'a [f32],
    pub strings: &'a [String],
    pub inputs: Vec<Option<&'a Value>>,
    pub quality: Quality,
    /// Distinguishes nodes of the same kind so two Noise Displace nodes do not share a pattern.
    pub salt: u32,
}

impl<'a> Args<'a> {
    pub fn f(&self, i: usize) -> f32 {
        self.params.get(i).copied().unwrap_or(0.0)
    }
    pub fn u(&self, i: usize) -> u32 {
        self.f(i).round().max(0.0) as u32
    }
    pub fn b(&self, i: usize) -> bool {
        self.f(i) >= 0.5
    }
    pub fn mesh(&self, i: usize) -> Option<&'a Part> {
        match self.inputs.get(i).copied().flatten() {
            Some(Value::Mesh(p)) => Some(p),
            _ => None,
        }
    }
    pub fn skeleton(&self, i: usize) -> Option<&'a super::tree::Skeleton> {
        match self.inputs.get(i).copied().flatten() {
            Some(Value::Skeleton(s)) => Some(s),
            _ => None,
        }
    }
    pub fn material(&self, i: usize) -> Option<&'a MaterialSpec> {
        match self.inputs.get(i).copied().flatten() {
            Some(Value::Material(m)) => Some(m),
            _ => None,
        }
    }
    pub fn points(&self, i: usize) -> Option<&'a Vec<PointInst>> {
        match self.inputs.get(i).copied().flatten() {
            Some(Value::Points(p)) => Some(p),
            _ => None,
        }
    }
    /// Scales a count by the quality: stylised halves it, detailed raises it.
    pub fn q(&self, count: f32) -> f32 {
        count
            * match self.quality {
                Quality::Stylized => 0.5,
                Quality::Balanced => 1.0,
                Quality::Detailed => 1.6,
            }
    }
    pub fn stylized(&self) -> bool {
        self.quality == Quality::Stylized
    }
}

pub type EvalFn = fn(&Args) -> Vec<Value>;

pub struct NodeDef {
    pub id: &'static str,
    pub title: &'static str,
    pub category: &'static str,
    pub inputs: &'static [PortDef],
    pub outputs: &'static [PortDef],
    pub params: &'static [ParamSpec],
    pub strings: &'static [StringSpec],
    pub eval: EvalFn,
}

impl std::fmt::Debug for NodeDef {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "NodeDef({})", self.id)
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct NatureNode {
    pub id: NodeId,
    pub op: &'static str,
    pub params: Vec<f32>,
    pub strings: Vec<String>,
    pub position: Point,
}

/// A node the Edit Library Material node may read: a library node, another edit node, or **Your
/// Material** pointing at a material the library baked into the project.
pub fn is_library_node(node: &NatureNode) -> bool {
    match node.op {
        "mat_library" | "mat_cards" | "mat_edit" => true,
        "mat_project" => node
            .strings
            .first()
            .is_some_and(|path| super::ops::is_library_baked(path)),
        _ => false,
    }
}

impl NatureNode {
    pub fn def(&self) -> &'static NodeDef {
        super::ops::find(self.op).unwrap_or_else(|| panic!("unknown nature op {}", self.op))
    }
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct NatureGraph {
    pub nodes: Vec<NatureNode>,
    pub links: Vec<node_canvas::Link>,
    pub next_id: NodeId,
}

impl NatureGraph {
    pub fn new() -> Self {
        NatureGraph {
            nodes: Vec::new(),
            links: Vec::new(),
            next_id: 1,
        }
    }

    /// Adds a node with the op's default parameters.
    pub fn add(&mut self, op: &'static str, position: Point) -> NodeId {
        let def = super::ops::find(op).unwrap_or_else(|| panic!("unknown nature op {op}"));
        let id = self.next_id.max(1);
        self.next_id = id + 1;
        self.nodes.push(NatureNode {
            id,
            op: def.id,
            params: def.params.iter().map(|p| p.default).collect(),
            strings: def.strings.iter().map(|_| String::new()).collect(),
            position,
        });
        id
    }

    /// Adds a node with parameter overrides by key (presets).
    pub fn add_with(&mut self, op: &'static str, position: Point, set: &[(&str, f32)]) -> NodeId {
        let id = self.add(op, position);
        for (key, value) in set {
            self.set(id, key, *value);
        }
        id
    }

    pub fn node(&self, id: NodeId) -> Option<&NatureNode> {
        self.nodes.iter().find(|n| n.id == id)
    }

    pub fn node_mut(&mut self, id: NodeId) -> Option<&mut NatureNode> {
        self.nodes.iter_mut().find(|n| n.id == id)
    }

    /// Sets a parameter by key (clamped to its range); unknown keys panic in debug builds' tests via `get`.
    pub fn set(&mut self, id: NodeId, key: &str, value: f32) {
        if let Some(node) = self.node_mut(id) {
            let def = node.def();
            if let Some(i) = def.params.iter().position(|p| p.key == key) {
                let spec = &def.params[i];
                let v = if matches!(spec.kind, ParamKind::Float) {
                    value
                } else {
                    value.round()
                };
                node.params[i] = v.clamp(spec.min, spec.max);
            } else {
                panic!("op {} has no parameter {key}", node.op);
            }
        }
    }

    pub fn set_string(&mut self, id: NodeId, index: usize, text: String) {
        if let Some(node) = self.node_mut(id) {
            if index < node.strings.len() {
                node.strings[index] = text;
            }
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

    pub fn output_node(&self) -> Option<NodeId> {
        self.nodes.iter().find(|n| n.op == "output").map(|n| n.id)
    }

    /// Connects `from.output` to `to.input` when the port types match and no cycle appears.
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
        // An Edit Library Material node edits library assets only: its source is a library material
        // (Library Material, Leaf / Petal Texture) or another edit node.
        if b.op == "mat_edit" && !is_library_node(a) {
            return false;
        }
        // No cycles: `to` must not already reach `from`.
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
        quality: Quality,
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
                self.eval_node(link.from, quality, memo, stack)?;
            }
        }
        let inputs: Vec<Option<&Value>> = (0..def.inputs.len())
            .map(|input| {
                self.links
                    .iter()
                    .find(|l| l.to == id && l.input == input)
                    .and_then(|l| memo.get(&l.from).and_then(|values| values.get(l.output)))
            })
            .collect();
        let args = Args {
            params: &node.params,
            strings: &node.strings,
            inputs,
            quality,
            salt: (id as u32).wrapping_mul(7919),
        };
        let mut values = (def.eval)(&args);
        // Only the instance node creates copies and the part node passes them on: any other op
        // changed the geometry, so the copy list no longer describes it.
        if !matches!(def.id, "instance" | "part" | "material" | "texture") {
            for value in values.iter_mut() {
                if let Value::Mesh(part) = value {
                    part.instances = None;
                }
            }
        }
        stack.pop();
        memo.insert(id, values);
        Ok(())
    }

    /// Evaluates one node's outputs (used for per-node previews and tests).
    pub fn evaluate(&self, id: NodeId, quality: Quality) -> Result<Vec<Value>, String> {
        let mut memo = HashMap::new();
        self.eval_node(id, quality, &mut memo, &mut Vec::new())?;
        Ok(memo.remove(&id).unwrap_or_default())
    }

    /// Builds the model: every mesh wired into the Output node, in port order. Meshes that share a
    /// name and a material are merged into one part.
    pub fn build(&self, quality: Quality) -> Result<NatureModel, String> {
        let output = self.output_node().ok_or("The graph needs an Output node")?;
        let mut memo = HashMap::new();
        let def = self.node(output).ok_or("Output node is missing")?.def();
        let mut stack = Vec::new();
        for input in 0..def.inputs.len() {
            if let Some(link) = self
                .links
                .iter()
                .find(|l| l.to == output && l.input == input)
            {
                self.eval_node(link.from, quality, &mut memo, &mut stack)?;
            }
        }
        let mut model = NatureModel::default();
        for input in 0..def.inputs.len() {
            let Some(link) = self
                .links
                .iter()
                .find(|l| l.to == output && l.input == input)
            else {
                continue;
            };
            let Some(Value::Mesh(part)) = memo.get(&link.from).and_then(|v| v.get(link.output))
            else {
                continue;
            };
            if part.is_empty() {
                continue;
            }
            // Nothing is chosen inside a Part: the material has to arrive on its port (a generated
            // surface counts, it brings its own).
            if self.node(link.from).is_some_and(|n| n.op == "part") && part.surface.is_none() {
                let wired = self
                    .links
                    .iter()
                    .find(|l| l.to == link.from && l.input == 1)
                    .and_then(|l| memo.get(&l.from).and_then(|v| v.get(l.output)));
                let explicit = self.node(link.from).is_some_and(|n| {
                    n.params.get(1).copied().is_some_and(|value| {
                        !super::ops::is_from_nodes_choice(value.round().max(0.0) as usize)
                    })
                });
                if !matches!(wired, Some(Value::Material(_)))
                    && !explicit
                    && !matches!(
                        part.material,
                        MaterialSpec::Custom(_)
                            | MaterialSpec::Project(..)
                            | MaterialSpec::ProjectTexture(..)
                    )
                {
                    return Err(format!(
                        "The {} part has no material: wire a material node (Materials category) into its Material port",
                        part.name
                    ));
                }
            }
            match model
                .parts
                .iter_mut()
                .find(|p| p.name == part.name && p.material == part.material)
            {
                Some(existing) => existing.append(part),
                None => model.parts.push(part.clone()),
            }
        }
        if model.parts.is_empty() {
            return Err("Nothing reaches the Output node: wire a Part node into it".to_owned());
        }
        Ok(model)
    }

    // -------------------------------------------------------------- persistence

    pub fn to_text(&self) -> String {
        let escape = |s: &str| {
            s.replace('%', "%25")
                .replace(' ', "%20")
                .replace('\n', "%0A")
        };
        let mut text = String::from("NATUREGRAPH2\n");
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
            text.push_str(&format!(" {}", node.strings.len()));
            for s in &node.strings {
                text.push_str(&format!(
                    " {}",
                    if s.is_empty() {
                        "%00".to_owned()
                    } else {
                        escape(s)
                    }
                ));
            }
            text.push('\n');
        }
        for link in &self.links {
            text.push_str(&format!(
                "link {} {} {} {}\n",
                link.from, link.output, link.to, link.input
            ));
        }
        text
    }

    pub fn from_text(text: &str) -> Option<NatureGraph> {
        let mut lines = text.lines();
        // Version 1 files kept the material inside the Part node: they are migrated on load.
        let legacy = match lines.next()?.trim() {
            "NATUREGRAPH2" => false,
            "NATUREGRAPH1" => true,
            _ => return None,
        };
        let mut old: Vec<(NodeId, &'static str, Vec<f32>, Vec<String>)> = Vec::new();
        let unescape = |s: &str| {
            if s == "%00" {
                String::new()
            } else {
                s.replace("%20", " ")
                    .replace("%0A", "\n")
                    .replace("%25", "%")
            }
        };
        let mut graph = NatureGraph::new();
        for line in lines {
            let w: Vec<&str> = line.split_whitespace().collect();
            match w.first().copied() {
                Some("node") if w.len() >= 7 => {
                    let id: NodeId = w[1].parse().ok()?;
                    let position = Point::new(w[2].parse().ok()?, w[3].parse().ok()?);
                    let def = super::ops::find(w[4])?;
                    let n: usize = w[5].parse().ok()?;
                    let mut params: Vec<f32> = def.params.iter().map(|p| p.default).collect();
                    for (i, word) in w.get(6..6 + n)?.iter().enumerate() {
                        if let Some(slot) = params.get_mut(i) {
                            *slot = word.parse().ok()?;
                        }
                    }
                    let sn: usize = w.get(6 + n)?.parse().ok()?;
                    let mut strings: Vec<String> =
                        def.strings.iter().map(|_| String::new()).collect();
                    for (i, word) in w.get(7 + n..7 + n + sn)?.iter().enumerate() {
                        if let Some(slot) = strings.get_mut(i) {
                            *slot = unescape(word);
                        }
                    }
                    if legacy && matches!(def.id, "part" | "leaves") {
                        let raw: Vec<f32> = w
                            .get(6..6 + n)?
                            .iter()
                            .map(|word| word.parse().unwrap_or(0.0))
                            .collect();
                        old.push((id, def.id, raw, strings.clone()));
                    }
                    graph.next_id = graph.next_id.max(id + 1);
                    graph.nodes.push(NatureNode {
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
        if legacy {
            super::migrate_legacy(&mut graph, &old);
        }
        (graph.output_node().is_some()).then_some(graph)
    }
}

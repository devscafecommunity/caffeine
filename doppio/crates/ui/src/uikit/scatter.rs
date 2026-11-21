//! The scatter plan: a small text file (`Scatter/*.scatter` in a project) that says what to scatter on a terrain.
//!
//! One `seed` line, then one block per kind of thing. A `class` line carries the thing's properties as `key value`
//! pairs; the lines under it are the biome terms (`<mask> <rise0> <rise1> <fall1> <fall0>`, a trapezoid over a
//! terrain mask, see `terrain/TerrainBiome.hpp`) that decide *where*:
//!
//! ```text
//! # Alpine forest
//! seed 7
//! class 2 name pine primitive cone color 0.12 0.32 0.14 size 1.4 5 1.4 cell 6 radius 2.2 density 0.9 priority 20
//!   slope   0.00 0.00 0.15 0.35
//!   wetness 0.00 0.00 0.30 0.60
//! class 1 name boulder primitive sphere color 0.45 0.43 0.40 cell 14 radius 2 priority 30 sink 0.25 micromesh on
//!   slope   0.30 0.45 1.00 1.00
//! ```
//!
//! A class can place a **composite** instead of one shape: indented `part` lines, one per entity of the instance
//! (a generated tree is a solid part and a cut-out foliage part). Each is `part mesh <path> [slots a.mat;b.mat]
//! [micromesh on|off] [cutout on|off]`; the instance becomes an empty entity with one child per part (the
//! Nature generator writes these lines for you: *Add to scatter plan*).
//!
//! Properties: `name`; the shape, either `primitive <cube|sphere|cone|cylinder|capsule|pyramid|torus>` or
//! `mesh <path>` (a `{v}` in the path stands for the variant number); `material <path>`; `color r g b` (multiplies the
//! tint); `size x y z` (the base size of the shape in metres, 1 1 1 by default); `cell`, `radius`, `density`,
//! `priority`, `scale <min> <max>`, `tilt`, `sink`, `tint` (the jitter), `variants`, `micromesh on|off`, `base` (the
//! biome rule's base density). A mask may not be named like a property.

use doppio_engine::ScatterClass;

/// The words a class line understands: a mask cannot be called any of these.
const KEYS: &[&str] = &[
    "name",
    "primitive",
    "mesh",
    "material",
    "color",
    "size",
    "cell",
    "radius",
    "density",
    "priority",
    "scale",
    "tilt",
    "sink",
    "tint",
    "variants",
    "micromesh",
    "base",
    "part",
];

const PRIMITIVES: &[&str] = &[
    "cube", "sphere", "cone", "cylinder", "capsule", "pyramid", "torus",
];

/// What a class is made of.
#[derive(Debug, Clone, PartialEq)]
pub enum Shape {
    /// A built-in primitive, by the engine's name for it (`Cone`).
    Primitive(String),
    /// A mesh file in the project; `{v}` is the variant number.
    Mesh(String),
}

#[derive(Debug, Clone, PartialEq)]
pub struct Term {
    pub mask: String,
    pub edges: [f32; 4],
}

/// One entity of a composite instance (see the module docs).
#[derive(Debug, Clone, PartialEq)]
pub struct PlanPart {
    pub mesh: String,
    /// Material slots (`a.mat;b.mat`), one per sub-mesh; empty = the mesh's own.
    pub slots: String,
    pub micromesh: bool,
    /// Cut-out cards (leaves): they cast no shadow of their own.
    pub cutout: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PlanClass {
    pub name: String,
    pub shape: Shape,
    pub material: Option<String>,
    pub color: [f32; 3],
    /// Base size of the shape in metres; the instance scale multiplies it.
    pub size: [f32; 3],
    pub micromesh: bool,
    pub base: f32,
    pub terms: Vec<Term>,
    /// A composite instance: when not empty, `shape` is not used and every instance is these parts.
    pub parts: Vec<PlanPart>,
    /// The engine's class (id, spacing, radius, density, scale range, tilt, sink, tint jitter, variants).
    pub engine: ScatterClass,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ScatterPlan {
    pub seed: u64,
    pub classes: Vec<PlanClass>,
}

impl ScatterPlan {
    pub fn from_text(text: &str) -> Result<ScatterPlan, String> {
        let mut plan = ScatterPlan {
            seed: 0,
            classes: Vec::new(),
        };
        for (index, raw) in text.lines().enumerate() {
            let line_number = index + 1;
            let line = raw.split('#').next().unwrap_or("").trim();
            if line.is_empty() {
                continue;
            }
            let words: Vec<&str> = line.split_whitespace().collect();
            let fail = |why: String| format!("line {line_number}: {why}");
            match words[0] {
                "seed" => {
                    plan.seed = words
                        .get(1)
                        .and_then(|w| w.parse().ok())
                        .filter(|_| words.len() == 2)
                        .ok_or_else(|| fail("`seed` takes one whole number".to_owned()))?;
                }
                "class" => {
                    let id: u32 = words.get(1).and_then(|w| w.parse().ok()).ok_or_else(|| {
                        fail("`class` needs a number: `class 2 name pine ...`".to_owned())
                    })?;
                    if plan.classes.iter().any(|c| c.engine.id == id) {
                        return Err(fail(format!("class {id} is defined twice")));
                    }
                    plan.classes
                        .push(parse_class(id, &words[2..]).map_err(fail)?);
                }
                "part" => {
                    let class = plan.classes.last_mut().ok_or_else(|| {
                        fail("a `part` must come after a `class` line".to_owned())
                    })?;
                    class.parts.push(parse_part(&words[1..]).map_err(fail)?);
                }
                mask => {
                    let class = plan
                        .classes
                        .last_mut()
                        .ok_or_else(|| fail("a term must come after a `class` line".to_owned()))?;
                    if KEYS.contains(&mask) {
                        return Err(fail(format!(
                            "`{mask}` is a property: put it on the `class` line"
                        )));
                    }
                    if words.len() != 5 {
                        return Err(fail(
                            "a term is `<mask> <rise0> <rise1> <fall1> <fall0>`".to_owned(),
                        ));
                    }
                    let mut edges = [0.0f32; 4];
                    for (slot, word) in edges.iter_mut().zip(&words[1..]) {
                        *slot = word
                            .parse()
                            .map_err(|_| fail(format!("`{word}` is not a number")))?;
                    }
                    if edges.windows(2).any(|w| w[0] > w[1]) {
                        return Err(fail(
                            "the four numbers of a term must not decrease".to_owned(),
                        ));
                    }
                    class.terms.push(Term {
                        mask: mask.to_owned(),
                        edges,
                    });
                }
            }
        }
        if plan.classes.is_empty() {
            return Err("the plan has no `class` line".to_owned());
        }
        Ok(plan)
    }

    /// The biome table the engine reads: a rule per class (`class <id> base <b>` and its terms).
    pub fn biome_text(&self) -> String {
        let mut text = String::new();
        for class in &self.classes {
            text.push_str(&format!("class {} base {}\n", class.engine.id, class.base));
            for term in &class.terms {
                text.push_str(&format!(
                    "  {} {} {} {} {}\n",
                    term.mask, term.edges[0], term.edges[1], term.edges[2], term.edges[3]
                ));
            }
        }
        text
    }

    pub fn engine_classes(&self) -> Vec<ScatterClass> {
        self.classes.iter().map(|c| c.engine).collect()
    }

    pub fn class(&self, id: u32) -> Option<&PlanClass> {
        self.classes.iter().find(|c| c.engine.id == id)
    }
}

fn parse_part(words: &[&str]) -> Result<PlanPart, String> {
    let mut part = PlanPart {
        mesh: String::new(),
        slots: String::new(),
        micromesh: false,
        cutout: false,
    };
    let mut at = 0;
    let flag = |word: Option<&&str>, key: &str| -> Result<bool, String> {
        match word.copied() {
            Some("on") => Ok(true),
            Some("off") => Ok(false),
            _ => Err(format!("`{key}` is `on` or `off`")),
        }
    };
    while at < words.len() {
        let key = words[at];
        at += 1;
        match key {
            "mesh" => part.mesh = words.get(at).ok_or("`mesh` needs a path")?.to_string(),
            "slots" => part.slots = words.get(at).ok_or("`slots` needs a list")?.to_string(),
            "micromesh" => part.micromesh = flag(words.get(at), "micromesh")?,
            "cutout" => part.cutout = flag(words.get(at), "cutout")?,
            other => return Err(format!("`{other}` is not a property of a part")),
        }
        at += 1;
    }
    if part.mesh.is_empty() {
        return Err("a `part` needs `mesh <path>`".to_owned());
    }
    Ok(part)
}

fn parse_class(id: u32, words: &[&str]) -> Result<PlanClass, String> {
    let mut class = PlanClass {
        name: format!("class{id}"),
        shape: Shape::Primitive("Sphere".to_owned()),
        material: None,
        color: [1.0, 1.0, 1.0],
        size: [1.0, 1.0, 1.0],
        micromesh: false,
        base: 1.0,
        terms: Vec::new(),
        parts: Vec::new(),
        engine: ScatterClass {
            id,
            ..ScatterClass::default()
        },
    };
    let mut at = 0;
    let number = |at: usize, key: &str| -> Result<f32, String> {
        words
            .get(at)
            .and_then(|w| w.parse::<f32>().ok())
            .filter(|v| v.is_finite())
            .ok_or_else(|| format!("`{key}` needs a number"))
    };
    while at < words.len() {
        let key = words[at];
        at += 1;
        match key {
            "name" => {
                class.name = words.get(at).ok_or("`name` needs a word")?.to_string();
                at += 1;
            }
            "primitive" => {
                let word = words
                    .get(at)
                    .ok_or("`primitive` needs a shape")?
                    .to_lowercase();
                if !PRIMITIVES.contains(&word.as_str()) {
                    return Err(format!(
                        "`{word}` is not a primitive ({})",
                        PRIMITIVES.join(", ")
                    ));
                }
                let mut name = word.clone();
                name[..1].make_ascii_uppercase();
                class.shape = Shape::Primitive(name);
                at += 1;
            }
            "mesh" => {
                class.shape = Shape::Mesh(words.get(at).ok_or("`mesh` needs a path")?.to_string());
                at += 1;
            }
            "material" => {
                class.material = Some(words.get(at).ok_or("`material` needs a path")?.to_string());
                at += 1;
            }
            "color" => {
                for k in 0..3 {
                    class.color[k] = number(at + k, "color")?.clamp(0.0, 4.0);
                }
                at += 3;
            }
            "size" => {
                for k in 0..3 {
                    class.size[k] = number(at + k, "size")?;
                    if class.size[k] <= 0.0 {
                        return Err("`size` must be positive".to_owned());
                    }
                }
                at += 3;
            }
            "scale" => {
                class.engine.min_scale = number(at, "scale")?;
                class.engine.max_scale = number(at + 1, "scale")?;
                if class.engine.min_scale <= 0.0 || class.engine.max_scale < class.engine.min_scale
                {
                    return Err("`scale <min> <max>` needs 0 < min <= max".to_owned());
                }
                at += 2;
            }
            "cell" => {
                class.engine.cell_size = number(at, "cell")?;
                if class.engine.cell_size <= 0.0 {
                    return Err("`cell` must be positive".to_owned());
                }
                at += 1;
            }
            "radius" => {
                class.engine.radius = number(at, "radius")?;
                if class.engine.radius <= 0.0 {
                    return Err("`radius` must be positive".to_owned());
                }
                at += 1;
            }
            "density" => {
                class.engine.density = number(at, "density")?.clamp(0.0, 1.0);
                at += 1;
            }
            "base" => {
                class.base = number(at, "base")?.clamp(0.0, 1.0);
                at += 1;
            }
            "priority" => {
                class.engine.priority = number(at, "priority")? as i32;
                at += 1;
            }
            "tilt" => {
                class.engine.tilt_to_normal = number(at, "tilt")?.clamp(0.0, 1.0);
                at += 1;
            }
            "sink" => {
                class.engine.sink = number(at, "sink")?;
                at += 1;
            }
            "tint" => {
                class.engine.tint_jitter = number(at, "tint")?.max(0.0);
                at += 1;
            }
            "variants" => {
                let n = number(at, "variants")?;
                if n < 1.0 || n.fract() != 0.0 {
                    return Err("`variants` is a whole number, 1 or more".to_owned());
                }
                class.engine.variants = n as u32;
                at += 1;
            }
            "micromesh" => {
                class.micromesh = match words.get(at).copied() {
                    Some("on") => true,
                    Some("off") => false,
                    _ => return Err("`micromesh` is `on` or `off`".to_owned()),
                };
                at += 1;
            }
            other => return Err(format!("`{other}` is not a property of a class")),
        }
    }
    Ok(class)
}

/// What `Scene > Scatter on Terrain` runs when the project has no plan of its own: written to
/// `Scatter/default.scatter` the first time, so it can be edited. Built-in shapes only, no assets needed.
pub const DEFAULT_PLAN: &str = "\
# A first scatter: pines on gentle dry ground, boulders on steep ground, grass tufts everywhere but on rock.
# Edit this file and run Scene > Scatter on Terrain again. Masks: height, slope, curvature (built in) and
# whatever the terrain graph exported (flow, wetness, wear, deposit).
seed 7

class 1 name boulder primitive sphere color 0.45 0.43 0.40 size 1.6 1.1 1.4 cell 16 radius 2.2 density 0.9 priority 30 scale 0.7 1.6 tilt 0.8 sink 0.3 micromesh on
  slope 0.30 0.45 1.00 1.00

class 2 name pine primitive cone color 0.12 0.32 0.15 size 1.8 6 1.8 cell 8 radius 2.4 density 0.85 priority 20 scale 0.8 1.4 tilt 0.1 tint 0.1
  slope   0.00 0.00 0.15 0.35
  wetness 0.00 0.00 0.30 0.60
  height  0.12 0.20 0.70 0.85

class 3 name tuft primitive pyramid color 0.30 0.50 0.18 size 0.5 0.6 0.5 cell 3 radius 0.5 density 0.8 priority 5 scale 0.7 1.3 tilt 1.0 tint 0.15
  slope   0.00 0.00 0.30 0.50
";

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parts_make_a_composite_class() {
        let plan = ScatterPlan::from_text(
            "class 4 name oak cell 12 radius 3 scale 0.8 1.3\n  part mesh Models/Nature/oak_solid.obj slots A.mat;B.mat micromesh on\n  part mesh Models/Nature/oak_foliage.obj slots C.mat cutout on\n  slope 0 0 0.2 0.4\n",
        )
        .unwrap();
        let oak = plan.class(4).unwrap();
        assert_eq!(oak.parts.len(), 2);
        assert_eq!(oak.parts[0].slots, "A.mat;B.mat");
        assert!(oak.parts[0].micromesh && !oak.parts[0].cutout);
        assert!(oak.parts[1].cutout);
        assert_eq!(oak.terms.len(), 1, "terms still follow");
        assert!(ScatterPlan::from_text("class 1\n  part slots x.mat\n").is_err());
        assert!(ScatterPlan::from_text("part mesh a.obj\n").is_err());
        assert!(ScatterPlan::from_text("class 1\n  part mesh a.obj cutout maybe\n").is_err());
    }

    #[test]
    fn the_default_plan_parses_and_becomes_a_biome_table() {
        let plan = ScatterPlan::from_text(DEFAULT_PLAN).unwrap();
        assert_eq!(plan.seed, 7);
        assert_eq!(plan.classes.len(), 3);
        let pine = plan.class(2).unwrap();
        assert_eq!(pine.name, "pine");
        assert_eq!(pine.shape, Shape::Primitive("Cone".to_owned()));
        assert_eq!(pine.size, [1.8, 6.0, 1.8]);
        assert_eq!(pine.terms.len(), 3);
        assert!((pine.engine.cell_size - 8.0).abs() < 1e-6);
        assert_eq!(pine.engine.min_scale, 0.8);
        assert!(plan.class(1).unwrap().micromesh);
        assert!(!pine.micromesh);
        // The biome text is what the engine parses: every class has its rule.
        let text = plan.biome_text();
        assert!(
            text.contains("class 2 base 1") && text.contains("wetness 0 0 0.3 0.6"),
            "{text}"
        );
        assert_eq!(plan.engine_classes().len(), 3);
        // Round trip: parsing the generated biome text through the plan reader would not work (it is the engine's
        // format), but its terms match the plan's.
        assert_eq!(text.matches("class ").count(), 3);
    }

    #[test]
    fn every_error_names_its_line() {
        for (text, needle) in [
            ("class 1 name a\nseed x\n", "line 2"),
            ("slope 0 0 1 1\n", "line 1"),
            ("class\n", "line 1"),
            ("class 1\nclass 1\n", "line 2"),
            ("class 1 cell 0\n", "line 1"),
            ("class 1 radius -2\n", "line 1"),
            ("class 1 scale 2 1\n", "line 1"),
            ("class 1 primitive teapot\n", "teapot"),
            ("class 1 micromesh maybe\n", "micromesh"),
            ("class 1 colour 1 1 1\n", "colour"),
            ("class 1\n slope 0 0 1\n", "term"),
            ("class 1\n slope 0.5 0.2 1 1\n", "decrease"),
            ("class 1\n cell 0 0 1 1\n", "property"),
            ("class 1 variants 1.5\n", "whole"),
            ("seed 1\n", "no `class`"),
        ] {
            let error = ScatterPlan::from_text(text).unwrap_err();
            assert!(error.contains(needle), "{text:?} gave {error:?}");
        }
    }

    #[test]
    fn meshes_materials_and_comments() {
        let plan = ScatterPlan::from_text(
            "# a comment\nclass 5 name oak mesh Nature/oak_{v}.obj material Nature/oak.mat variants 4 # trailing\n  height 0 0 1 1\n",
        )
        .unwrap();
        let oak = plan.class(5).unwrap();
        assert_eq!(oak.shape, Shape::Mesh("Nature/oak_{v}.obj".to_owned()));
        assert_eq!(oak.material.as_deref(), Some("Nature/oak.mat"));
        assert_eq!(oak.engine.variants, 4);
        assert_eq!(oak.terms[0].mask, "height");
    }
}

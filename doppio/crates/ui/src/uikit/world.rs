//! The World Composer's composition, in Rust: the `graph` text of a `<Name>.generator.world` recipe.
//!
//! The engine owns the grammar (`src/caffeine/world/WorldComposition.hpp`) and the build; this is its mirror so the
//! World tab can edit a composition field by field and print it back. The text is the source of truth: the tab
//! parses it, edits the model and prints it again, and the engine (`world_check`) validates what it prints.
//!
//! ```text
//! seed 1234
//! terrain Terrain/Alpine.generator.terrain size 1024 1024
//! water Water/River.generator.water follow flow min 0.8 carve 1.5
//! material terrain.layer0 Materials/Moss.generator.material
//! class oak ref Nature/OakTree.generator.nature variants 8 priority 20 cell 9 radius 3 density 0.8 scale 0.8 1.4
//!   slope 0.00 0.00 0.15 0.35
//!   adapt size wetness
//! override remove oak 12 -4
//! ```

use super::recipe_ref::{split_mix64, unit};

/// Masks a `where` term or an `adapt` line can read (the engine's built-in ones and those a terrain recipe bakes).
pub const MASKS: [&str; 8] = [
    "height",
    "slope",
    "curvature",
    "flow",
    "wetness",
    "wear",
    "deposit",
    "water_distance",
];

/// What an `adapt` line can drive (besides `param.<name>`, a nature recipe's exposed parameter).
pub const ADAPT_TARGETS: [&str; 5] = ["size", "lean", "tint", "wetness", "snow"];

/// Sources of an `adapt` line: any mask, or how crowded the instance is.
pub fn adapt_sources() -> Vec<String> {
    MASKS
        .iter()
        .map(|mask| (*mask).to_owned())
        .chain(std::iter::once("neighbours".to_owned()))
        .collect()
}

/// A biome term: the class grows where `mask` rises from `rise0` to `rise1` and falls from `fall1` to `fall0`.
#[derive(Debug, Clone, PartialEq)]
pub struct Term {
    pub mask: String,
    pub values: [f32; 4],
}

/// `adapt <target> <mask> [amount]`.
#[derive(Debug, Clone, PartialEq)]
pub struct Adapt {
    pub target: String,
    pub mask: String,
    /// `None`: the target's default amount.
    pub amount: Option<f32>,
}

impl Adapt {
    /// The amount the engine uses when the line gives none (`WorldAdapt::effectiveAmount`).
    pub fn effective_amount(&self) -> f32 {
        self.amount.unwrap_or(match self.target.as_str() {
            "size" => 0.25,
            "lean" => 0.5,
            "tint" => 0.2,
            _ => 1.0,
        })
    }
}

/// One class of things that grow on the terrain.
#[derive(Debug, Clone, PartialEq)]
pub struct Class {
    pub id: String,
    pub reference: String,
    pub variants: u32,
    pub priority: i32,
    pub cell: f32,
    pub radius: f32,
    pub density: f32,
    pub min_scale: f32,
    pub max_scale: f32,
    pub tilt: f32,
    pub sink: f32,
    pub tint: f32,
    pub base: f32,
    pub unique: bool,
    pub terms: Vec<Term>,
    pub adapt: Vec<Adapt>,
}

impl Class {
    pub fn new(id: &str, reference: &str) -> Self {
        Self {
            id: id.to_owned(),
            reference: reference.to_owned(),
            variants: 1,
            priority: 0,
            cell: 8.0,
            radius: 2.0,
            density: 1.0,
            min_scale: 0.8,
            max_scale: 1.2,
            tilt: 0.15,
            sink: 0.0,
            tint: 0.06,
            base: 1.0,
            unique: false,
            terms: Vec::new(),
            adapt: Vec::new(),
        }
    }

    /// Instances per hectare on ground where every term is fully on: one try per cell.
    pub fn density_hint(&self) -> f32 {
        let cell = self.cell.max(0.01);
        10_000.0 / (cell * cell) * self.density.clamp(0.0, 1.0) * self.base.clamp(0.0, 1.0)
    }

    /// Names of the nature recipe parameters this class drives (`adapt param.<name>`).
    pub fn bake_params(&self) -> Vec<String> {
        self.adapt
            .iter()
            .filter_map(|adapt| adapt.target.strip_prefix("param.").map(str::to_owned))
            .collect()
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Water {
    pub reference: String,
    /// A river: the mask it follows and the value above which water runs.
    pub follow: String,
    pub min: f32,
    /// A lake: fill the basins under a normalised level.
    pub fill_basins: bool,
    pub level: f32,
    /// Metres the river's bed is lowered.
    pub carve: f32,
}

impl Water {
    pub fn river(reference: &str) -> Self {
        Self {
            reference: reference.to_owned(),
            follow: "flow".to_owned(),
            min: 0.8,
            fill_basins: false,
            level: 0.0,
            carve: 0.0,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Material {
    pub slot: String,
    pub reference: String,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum OverrideOp {
    Remove,
    /// The new position, metres from the terrain's corner.
    Move {
        x: f32,
        z: f32,
    },
    /// The variant to keep.
    Pin {
        variant: u32,
    },
}

impl OverrideOp {
    pub fn name(self) -> &'static str {
        match self {
            OverrideOp::Remove => "remove",
            OverrideOp::Move { .. } => "move",
            OverrideOp::Pin { .. } => "pin",
        }
    }
}

/// One hand edit, keyed by the instance's identity (class id, cell).
#[derive(Debug, Clone, PartialEq)]
pub struct Override {
    pub op: OverrideOp,
    pub class: String,
    pub cell: [i32; 2],
}

/// A whole composition.
#[derive(Debug, Clone, PartialEq)]
pub struct Composition {
    pub seed: u64,
    pub terrain: String,
    /// Metres; `[0, 0]` means the baked terrain's own size.
    pub size: [f32; 2],
    pub water_distance: f32,
    pub water: Vec<Water>,
    pub materials: Vec<Material>,
    pub classes: Vec<Class>,
    pub overrides: Vec<Override>,
}

impl Default for Composition {
    fn default() -> Self {
        Self {
            seed: 1,
            terrain: String::new(),
            size: [0.0, 0.0],
            water_distance: 64.0,
            water: Vec::new(),
            materials: Vec::new(),
            classes: Vec::new(),
            overrides: Vec::new(),
        }
    }
}

fn valid_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 63
        && id
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '_' | '-' | '.'))
}

fn valid_mask(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 31
        && name
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, '_' | '-'))
}

fn valid_ref(reference: &str) -> bool {
    !reference.is_empty() && !reference.contains([' ', '\t', '#'])
}

fn is_class_key(word: &str) -> bool {
    matches!(
        word,
        "ref"
            | "variants"
            | "priority"
            | "cell"
            | "radius"
            | "density"
            | "scale"
            | "tilt"
            | "sink"
            | "tint"
            | "base"
            | "unique"
            | "adapt"
    )
}

/// True for `size`, `lean`, `tint`, `wetness`, `snow` and `param.<name>`.
pub fn is_adapt_target(target: &str) -> bool {
    match target.strip_prefix("param.") {
        Some(name) => valid_id(name),
        None => ADAPT_TARGETS.contains(&target),
    }
}

/// The placement salt of a class id: FNV-1a over its bytes, SplitMix64, folded to 32 bits (`classSalt`).
pub fn class_salt(id: &str) -> u32 {
    let mut hash = 0xcbf2_9ce4_8422_2325u64;
    for byte in id.bytes() {
        hash ^= u64::from(byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    let hash = split_mix64(hash);
    (hash ^ (hash >> 32)) as u32
}

/// A class id from a recipe path (`Nature/Oak Tree.generator.nature` → `oak_tree`), not yet used in `taken`.
pub fn class_id_for(reference: &str, taken: &[String]) -> String {
    let stem = reference
        .rsplit('/')
        .next()
        .unwrap_or(reference)
        .split(".generator")
        .next()
        .unwrap_or("class");
    let mut base: String = stem
        .to_lowercase()
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
        .collect::<String>()
        .split('_')
        .filter(|part| !part.is_empty())
        .collect::<Vec<_>>()
        .join("_");
    if base.is_empty() {
        base = "class".to_owned();
    }
    base.truncate(48);
    if !taken.contains(&base) {
        return base;
    }
    (2..)
        .map(|n| format!("{base}_{n}"))
        .find(|candidate| !taken.contains(candidate))
        .unwrap_or(base)
}

/// A number as the composition prints it: shortest form that reads back the same.
fn num(value: f32) -> String {
    format!("{value}")
}

struct Words<'a> {
    inner: std::str::SplitWhitespace<'a>,
}

impl<'a> Words<'a> {
    fn next(&mut self) -> Option<&'a str> {
        self.inner.next()
    }
    fn f32(&mut self) -> Option<f32> {
        self.next()?.parse().ok()
    }
    fn i32(&mut self) -> Option<i32> {
        self.next()?.parse().ok()
    }
}

impl Composition {
    pub fn class(&self, id: &str) -> Option<&Class> {
        self.classes.iter().find(|class| class.id == id)
    }

    pub fn class_index(&self, id: &str) -> Option<usize> {
        self.classes.iter().position(|class| class.id == id)
    }

    /// Every recipe the composition references, each once, in file order.
    pub fn references(&self) -> Vec<String> {
        let mut out: Vec<String> = Vec::new();
        let all = std::iter::once(self.terrain.clone())
            .chain(self.water.iter().map(|w| w.reference.clone()))
            .chain(self.materials.iter().map(|m| m.reference.clone()))
            .chain(self.classes.iter().map(|c| c.reference.clone()));
        for reference in all {
            if !reference.is_empty() && !out.contains(&reference) {
                out.push(reference);
            }
        }
        out
    }

    /// Problems the engine would also refuse (the same checks as `WorldComposition::validate`), for when no engine
    /// library is loaded. Empty when valid.
    pub fn validate(&self) -> Vec<String> {
        let mut errors = Vec::new();
        if self.terrain.is_empty() {
            errors.push("the composition has no `terrain`".to_owned());
        }
        if self.classes.is_empty() {
            errors.push("the composition has no `class`".to_owned());
        }
        if self.water_distance <= 0.0 {
            errors.push("`water_distance` must be above 0".to_owned());
        }
        let mut salts: Vec<(u32, &str)> = Vec::new();
        for class in &self.classes {
            let name = format!("class `{}`", class.id);
            if !valid_id(&class.id) {
                errors.push(format!(
                    "{name}: the id must be letters, digits, '_', '-' or '.'"
                ));
            }
            if class.reference.is_empty() {
                errors.push(format!("{name} has no `ref`"));
            }
            if class.variants == 0 {
                errors.push(format!("{name}: `variants` must be at least 1"));
            }
            if class.cell <= 0.0 {
                errors.push(format!("{name}: `cell` must be above 0"));
            }
            if class.min_scale > class.max_scale {
                errors.push(format!("{name}: the scale range decreases"));
            }
            if class.bake_params().len() > 4 {
                errors.push(format!("{name}: at most 4 `adapt param.<name>` lines"));
            }
            let salt = class_salt(&class.id);
            if let Some((_, other)) = salts.iter().find(|(s, _)| *s == salt) {
                errors.push(if *other == class.id {
                    format!("{name} is defined twice")
                } else {
                    format!(
                        "{name} collides with class `{other}` (same placement salt): rename one"
                    )
                });
            } else {
                salts.push((salt, &class.id));
            }
        }
        for water in &self.water {
            if water.follow.is_empty() && !water.fill_basins {
                errors.push(format!(
                    "water `{}` needs `follow <mask>` or `fill basins`",
                    water.reference
                ));
            }
        }
        for o in &self.overrides {
            match self.class(&o.class) {
                None => errors.push(format!(
                    "override {} names unknown class `{}`",
                    o.op.name(),
                    o.class
                )),
                Some(class) => {
                    if let OverrideOp::Pin { variant } = o.op {
                        if variant >= class.variants {
                            errors.push(format!(
                                "override pin {}: variant {variant} is outside the pool of {}",
                                o.class, class.variants
                            ));
                        }
                    }
                }
            }
        }
        errors
    }

    /// The canonical text (`WorldComposition::toText`).
    pub fn to_text(&self) -> String {
        let mut out = format!("seed {}\n", self.seed);
        if self.water_distance != 64.0 {
            out += &format!("water_distance {}\n", num(self.water_distance));
        }
        if !self.terrain.is_empty() {
            out += &format!("terrain {}", self.terrain);
            if self.size[0] > 0.0 || self.size[1] > 0.0 {
                out += &format!(" size {} {}", num(self.size[0]), num(self.size[1]));
            }
            out.push('\n');
        }
        for w in &self.water {
            out += &format!("water {}", w.reference);
            if !w.follow.is_empty() {
                out += &format!(" follow {} min {}", w.follow, num(w.min));
            }
            if w.carve > 0.0 {
                out += &format!(" carve {}", num(w.carve));
            }
            if w.fill_basins {
                out += &format!(" fill basins level {}", num(w.level));
            }
            out.push('\n');
        }
        for m in &self.materials {
            out += &format!("material {} {}\n", m.slot, m.reference);
        }
        for c in &self.classes {
            out += &format!(
                "class {} ref {} variants {} priority {} cell {} radius {} density {} scale {} {} tilt {} sink {} tint {} base {}",
                c.id,
                c.reference,
                c.variants,
                c.priority,
                num(c.cell),
                num(c.radius),
                num(c.density),
                num(c.min_scale),
                num(c.max_scale),
                num(c.tilt),
                num(c.sink),
                num(c.tint),
                num(c.base)
            );
            if c.unique {
                out += " unique on";
            }
            out.push('\n');
            for t in &c.terms {
                out += &format!(
                    "  {} {} {} {} {}\n",
                    t.mask,
                    num(t.values[0]),
                    num(t.values[1]),
                    num(t.values[2]),
                    num(t.values[3])
                );
            }
            for a in &c.adapt {
                out += &format!("  adapt {} {}", a.target, a.mask);
                if let Some(amount) = a.amount {
                    out += &format!(" {}", num(amount));
                }
                out.push('\n');
            }
        }
        for o in &self.overrides {
            out += &format!(
                "override {} {} {} {}",
                o.op.name(),
                o.class,
                o.cell[0],
                o.cell[1]
            );
            match o.op {
                OverrideOp::Move { x, z } => out += &format!(" {} {}", num(x), num(z)),
                OverrideOp::Pin { variant } => out += &format!(" {variant}"),
                OverrideOp::Remove => {}
            }
            out.push('\n');
        }
        out
    }

    /// Parses the text form; a syntax error says which line (`WorldComposition::fromText`). It does not validate.
    pub fn parse(text: &str) -> Result<Composition, String> {
        let mut result = Composition {
            seed: 0,
            ..Composition::default()
        };
        let mut current: Option<usize> = None;
        for (index, raw) in text.lines().enumerate() {
            let fail = |why: &str| Err(format!("line {}: {why}", index + 1));
            let raw = raw.split('#').next().unwrap_or("");
            let mut words = Words {
                inner: raw.split_whitespace(),
            };
            let Some(first) = words.next() else {
                continue;
            };
            let indented = raw.starts_with([' ', '\t']);
            if indented {
                let Some(class) = current.and_then(|i| result.classes.get_mut(i)) else {
                    return fail("an indented line must come after a `class` line");
                };
                if first == "adapt" {
                    let (Some(target), Some(mask)) = (words.next(), words.next()) else {
                        return fail("`adapt` is `adapt <size|lean|tint|wetness|snow> <mask|neighbours> [amount]`");
                    };
                    if !valid_mask(mask) {
                        return fail(&format!("`{mask}` is not a mask name"));
                    }
                    let amount = match words.next() {
                        None => None,
                        Some(word) => match word.parse::<f32>() {
                            Ok(value) if value >= 0.0 => Some(value),
                            _ => return fail("the adapt amount must be a number >= 0"),
                        },
                    };
                    class.adapt.push(Adapt {
                        target: target.to_owned(),
                        mask: mask.to_owned(),
                        amount,
                    });
                } else {
                    if is_class_key(first) {
                        return fail(&format!(
                            "`{first}` is a property: put it on the `class` line"
                        ));
                    }
                    if !valid_mask(first) {
                        return fail(&format!("`{first}` is not a mask name"));
                    }
                    let values = [words.f32(), words.f32(), words.f32(), words.f32()];
                    let [Some(a), Some(b), Some(c), Some(d)] = values else {
                        return fail("a term is `<mask> <rise0> <rise1> <fall1> <fall0>`");
                    };
                    if a > b || b > c || c > d {
                        return fail("the four numbers of a term must not decrease");
                    }
                    class.terms.push(Term {
                        mask: first.to_owned(),
                        values: [a, b, c, d],
                    });
                }
                continue;
            }
            current = None;
            match first {
                "seed" => match words.next().and_then(|w| w.parse().ok()) {
                    Some(seed) => result.seed = seed,
                    None => return fail("`seed` takes one whole number"),
                },
                "water_distance" => match words.f32() {
                    Some(value) => result.water_distance = value,
                    None => return fail("`water_distance` takes metres"),
                },
                "terrain" => {
                    match words.next() {
                        Some(reference) if valid_ref(reference) => {
                            result.terrain = reference.to_owned()
                        }
                        _ => return fail("`terrain` needs a recipe path"),
                    }
                    if let Some(key) = words.next() {
                        match (key, words.f32(), words.f32()) {
                            ("size", Some(x), Some(z)) => result.size = [x, z],
                            _ => return fail("expected `size <x> <z>` after the terrain recipe"),
                        }
                    }
                }
                "water" => {
                    let reference = match words.next() {
                        Some(reference) if valid_ref(reference) => reference,
                        _ => return fail("`water` needs a recipe path"),
                    };
                    let mut water = Water {
                        follow: String::new(),
                        min: 0.0,
                        ..Water::river(reference)
                    };
                    while let Some(key) = words.next() {
                        match key {
                            "follow" => match words.next() {
                                Some(mask) if valid_mask(mask) => water.follow = mask.to_owned(),
                                _ => return fail("`follow` needs a mask name"),
                            },
                            "min" => match words.f32() {
                                Some(v) => water.min = v,
                                None => return fail("`min` needs a number"),
                            },
                            "fill" => match words.next() {
                                Some("basins") => water.fill_basins = true,
                                _ => return fail("expected `fill basins`"),
                            },
                            "level" => match words.f32() {
                                Some(v) => water.level = v,
                                None => return fail("`level` needs a number"),
                            },
                            "carve" => match words.f32() {
                                Some(v) => water.carve = v,
                                None => return fail("`carve` needs metres"),
                            },
                            other => return fail(&format!("unknown water property `{other}`")),
                        }
                    }
                    result.water.push(water);
                }
                "material" => match (words.next(), words.next()) {
                    (Some(slot), Some(reference)) if valid_ref(reference) => {
                        result.materials.push(Material {
                            slot: slot.to_owned(),
                            reference: reference.to_owned(),
                        })
                    }
                    _ => return fail("`material` is `material <slot> <recipe>`"),
                },
                "class" => {
                    let Some(id) = words.next() else {
                        return fail("a class needs an id: `class oak ref ...`");
                    };
                    let mut class = Class::new(id, "");
                    while let Some(key) = words.next() {
                        let ok = match key {
                            "ref" => words
                                .next()
                                .filter(|r| valid_ref(r))
                                .map(|r| class.reference = r.to_owned())
                                .is_some(),
                            "variants" => words
                                .next()
                                .and_then(|w| w.parse::<u32>().ok())
                                .map(|v| class.variants = v)
                                .is_some(),
                            "priority" => words.i32().map(|v| class.priority = v).is_some(),
                            "cell" => words.f32().map(|v| class.cell = v).is_some(),
                            "radius" => words.f32().map(|v| class.radius = v).is_some(),
                            "density" => words.f32().map(|v| class.density = v).is_some(),
                            "scale" => match (words.f32(), words.f32()) {
                                (Some(a), Some(b)) => {
                                    class.min_scale = a;
                                    class.max_scale = b;
                                    true
                                }
                                _ => false,
                            },
                            "tilt" => words.f32().map(|v| class.tilt = v).is_some(),
                            "sink" => words.f32().map(|v| class.sink = v).is_some(),
                            "tint" => words.f32().map(|v| class.tint = v).is_some(),
                            "base" => words.f32().map(|v| class.base = v).is_some(),
                            "unique" => match words.next() {
                                Some("on") => {
                                    class.unique = true;
                                    true
                                }
                                Some("off") => {
                                    class.unique = false;
                                    true
                                }
                                _ => false,
                            },
                            other => return fail(&format!("unknown class property `{other}`")),
                        };
                        if !ok {
                            return fail(&format!("bad value for `{key}`"));
                        }
                    }
                    result.classes.push(class);
                    current = Some(result.classes.len() - 1);
                    continue;
                }
                "override" => {
                    let (Some(op), Some(class), Some(cx), Some(cz)) =
                        (words.next(), words.next(), words.i32(), words.i32())
                    else {
                        return fail("`override` is `override <remove|move|pin> <class> <cell x> <cell z> ...`");
                    };
                    let op = match op {
                        "remove" => OverrideOp::Remove,
                        "move" => match (words.f32(), words.f32()) {
                            (Some(x), Some(z)) => OverrideOp::Move { x, z },
                            _ => return fail("`override move` ends with the new `<x> <z>`"),
                        },
                        "pin" => match words.next().and_then(|w| w.parse::<u32>().ok()) {
                            Some(variant) => OverrideOp::Pin { variant },
                            None => return fail("`override pin` ends with the variant number"),
                        },
                        other => return fail(&format!("`{other}` is not remove, move or pin")),
                    };
                    result.overrides.push(Override {
                        op,
                        class: class.to_owned(),
                        cell: [cx, cz],
                    });
                }
                other => return fail(&format!("unknown line `{other}`")),
            }
            if let Some(extra) = words.next() {
                return fail(&format!("unexpected `{extra}`"));
            }
        }
        Ok(result)
    }

    /// A new seed derived from the current one (the Reseed button): deterministic, never the same.
    pub fn reseeded(&self) -> u64 {
        let next = super::recipe_ref::derive_seed(self.seed, 0x5EED_0001, 0, 0) % 1_000_000_000;
        if next == self.seed {
            next + 1
        } else {
            next
        }
    }
}

/// A starting composition for a terrain recipe (the World tab's New).
pub fn starter(terrain: &str) -> Composition {
    Composition {
        seed: (unit(split_mix64(terrain.len() as u64)) * 1_000_000.0) as u64 + 1,
        terrain: terrain.to_owned(),
        ..Composition::default()
    }
}

/// The models file of an instance bake (`World/<name>.models`): one line per model part.
///
/// * `<class> <variant> <mesh> [slots a.mat;b.mat] [cutout on|off]` — a part of variant `variant` of a pool class
/// * `<class> cell <cx> <cz> <mesh> ...` — a part of the model baked for one instance (wins over the variant)
#[derive(Debug, Clone, Default, PartialEq)]
pub struct ModelsFile {
    pub lines: Vec<String>,
}

impl ModelsFile {
    pub fn variant(&mut self, class: usize, variant: u32, mesh: &str, cutout: bool) {
        self.lines.push(format!(
            "{class} {variant} {mesh}{}",
            if cutout { " cutout on" } else { "" }
        ));
    }

    pub fn cell(&mut self, class: usize, cell: [i32; 2], mesh: &str, cutout: bool) {
        self.lines.push(format!(
            "{class} cell {} {} {mesh}{}",
            cell[0],
            cell[1],
            if cutout { " cutout on" } else { "" }
        ));
    }

    pub fn to_text(&self) -> String {
        let mut text = String::from(
            "# World Composer models: <class> <variant> <mesh> [cutout on] | <class> cell <cx> <cz> <mesh> [cutout on]\n",
        );
        for line in &self.lines {
            text.push_str(line);
            text.push('\n');
        }
        text
    }
}

/// True for a model part that draws as cut-out cards (leaves, petals, blades): no shadows of its own.
pub fn is_cutout_part(file: &str) -> bool {
    let name = file.to_ascii_lowercase();
    [
        "leaf", "leaves", "foliage", "petal", "blade", "frond", "needle", "card",
    ]
    .iter()
    .any(|word| name.contains(word))
}

/// The Euler angles (degrees, the engine's pitch-X / yaw-Y / roll-Z) of an instance: its yaw about its own up, then
/// up turned to `up` by the shortest arc (the same turn the engine's scatter makes).
pub fn instance_euler_degrees(up: [f32; 3], yaw: f32) -> [f32; 3] {
    type Quat = [f32; 4]; // x, y, z, w
    fn axis_angle(axis: [f32; 3], angle: f32) -> Quat {
        let length = (axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]).sqrt();
        if length <= 1e-8 {
            return [0.0, 0.0, 0.0, 1.0];
        }
        let s = (angle * 0.5).sin() / length;
        [axis[0] * s, axis[1] * s, axis[2] * s, (angle * 0.5).cos()]
    }
    fn mul(a: Quat, b: Quat) -> Quat {
        [
            a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
            a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
            a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
        ]
    }
    let length = (up[0] * up[0] + up[1] * up[1] + up[2] * up[2])
        .sqrt()
        .max(1e-8);
    let up = [up[0] / length, up[1] / length, up[2] / length];
    // (0, 1, 0) x up
    let axis = [up[2], 0.0, -up[0]];
    let align = if (axis[0] * axis[0] + axis[2] * axis[2]).sqrt() > 1e-5 {
        axis_angle(axis, up[1].clamp(-1.0, 1.0).acos())
    } else {
        [0.0, 0.0, 0.0, 1.0]
    };
    let q = mul(align, axis_angle([0.0, 1.0, 0.0], yaw));
    let n = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3])
        .sqrt()
        .max(1e-8);
    let [x, y, z, w] = [q[0] / n, q[1] / n, q[2] / n, q[3] / n];
    let yaw = (-2.0 * (x * z - w * y)).clamp(-1.0, 1.0).asin();
    let pitch = (2.0 * (y * z + w * x)).atan2(1.0 - 2.0 * (x * x + y * y));
    let roll = (2.0 * (x * y + w * z)).atan2(1.0 - 2.0 * (y * y + z * z));
    [pitch.to_degrees(), yaw.to_degrees(), roll.to_degrees()]
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE: &str = "seed 1234
terrain Terrain/Alpine.generator.terrain size 1024 1024
water_distance 48
water Water/River.generator.water follow flow min 0.8 carve 1.5
water Water/Lake.generator.water fill basins level 0.18
material terrain.layer0 Materials/Moss.generator.material
class boulder ref Nature/MossRock.generator.nature variants 6 priority 30 cell 16 radius 2.2 density 0.9 scale 0.7 1.6 tilt 0.8 sink 0.3
  slope 0.30 0.45 1.00 1.00
class oak ref Nature/OakTree.generator.nature variants 8 priority 20 cell 9 radius 3 density 0.8 scale 0.8 1.4 tilt 0.1 tint 0.1 unique on
  slope   0.00 0.00 0.15 0.35   # flat ground
  wetness 0.20 0.40 0.80 1.00
  adapt size wetness
  adapt lean slope 0.3
  adapt param.height water_distance
override remove oak 12 -4
override move oak 3 7 120.5 88.25
override pin boulder 5 5 2
";

    #[test]
    fn the_sample_parses_and_prints_back_to_the_same_composition() {
        let comp = Composition::parse(SAMPLE).expect("parses");
        assert_eq!(comp.seed, 1234);
        assert_eq!(comp.size, [1024.0, 1024.0]);
        assert_eq!(comp.water_distance, 48.0);
        assert_eq!(comp.water.len(), 2);
        assert!(comp.water[1].fill_basins);
        assert_eq!(comp.water[0].carve, 1.5);
        assert_eq!(comp.classes.len(), 2);
        let oak = comp.class("oak").unwrap();
        assert!(oak.unique);
        assert_eq!(oak.terms.len(), 2);
        assert_eq!(oak.adapt[1].amount, Some(0.3));
        assert_eq!(oak.bake_params(), vec!["height".to_owned()]);
        assert_eq!(
            comp.overrides[1].op,
            OverrideOp::Move { x: 120.5, z: 88.25 }
        );
        assert!(comp.validate().is_empty(), "{:?}", comp.validate());
        let text = comp.to_text();
        assert_eq!(Composition::parse(&text).unwrap(), comp);
        assert_eq!(comp.references().len(), 6);
    }

    #[test]
    fn syntax_errors_name_the_line() {
        let error =
            Composition::parse("seed 1\nterrain T.generator.terrain\nbogus 3\n").unwrap_err();
        assert!(error.starts_with("line 3:"), "{error}");
        let error = Composition::parse("  slope 0 0 1 1\n").unwrap_err();
        assert!(error.contains("after a `class`"), "{error}");
        let error = Composition::parse("class a ref A.generator.nature\n  slope 0.5 0.2 1 1\n")
            .unwrap_err();
        assert!(error.contains("must not decrease"), "{error}");
        let error = Composition::parse("class a ref X\n  cell 3\n").unwrap_err();
        assert!(error.contains("is a property"), "{error}");
    }

    #[test]
    fn validation_mirrors_the_engine() {
        let mut comp = Composition::parse(SAMPLE).unwrap();
        comp.overrides.push(Override {
            op: OverrideOp::Pin { variant: 99 },
            class: "boulder".to_owned(),
            cell: [0, 0],
        });
        comp.classes.push(Class::new("oak", "X.generator.nature"));
        let errors = comp.validate();
        assert!(errors.iter().any(|e| e.contains("defined twice")));
        assert!(errors.iter().any(|e| e.contains("outside the pool")));
        assert!(Composition::default()
            .validate()
            .iter()
            .any(|e| e.contains("no `terrain`")));
    }

    #[test]
    fn class_ids_come_from_the_recipe_and_stay_unique() {
        let taken = vec!["oak_tree".to_owned()];
        assert_eq!(class_id_for("Nature/Pine.generator.nature", &taken), "pine");
        assert_eq!(
            class_id_for("Nature/Oak Tree.generator.nature", &taken),
            "oak_tree_2"
        );
    }

    /// The same numbers as `tests/test_world.cpp`.
    #[test]
    fn class_salts_match_the_engine() {
        assert_eq!(class_salt(""), 0xa825_8331);
        assert_eq!(class_salt("oak"), 0x85d1_58a1);
        assert_eq!(class_salt("boulder"), 0x9cff_765f);
    }

    #[test]
    fn reseeding_is_deterministic_and_moves() {
        let comp = Composition::parse(SAMPLE).unwrap();
        assert_eq!(comp.reseeded(), comp.reseeded());
        assert_ne!(comp.reseeded(), comp.seed);
    }

    #[test]
    fn an_upright_instance_only_turns_about_y() {
        // 90 degrees of yaw is the engine's gimbal lock (`toEuler` gives an equivalent [180, 90, 180]): use 30.
        let euler = instance_euler_degrees([0.0, 1.0, 0.0], 30f32.to_radians());
        assert!(euler[0].abs() < 1e-3 && euler[2].abs() < 1e-3, "{euler:?}");
        assert!((euler[1] - 30.0).abs() < 1e-3, "{euler:?}");
        let tilted = instance_euler_degrees([0.0, 0.9, 0.3], 0.0);
        assert!(tilted[0].abs() > 1.0, "{tilted:?}");
    }

    #[test]
    fn models_files_list_variants_and_cells() {
        let mut models = ModelsFile::default();
        models.variant(0, 2, "a/oak_trunk.obj", false);
        models.cell(1, [3, -4], "b/rock.obj", true);
        let text = models.to_text();
        assert!(text.contains("\n0 2 a/oak_trunk.obj\n"));
        assert!(text.contains("\n1 cell 3 -4 b/rock.obj cutout on\n"));
        assert!(is_cutout_part("oak_leaves.obj"));
        assert!(!is_cutout_part("oak_trunk.obj"));
    }
}

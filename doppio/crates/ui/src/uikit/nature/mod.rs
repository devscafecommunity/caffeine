//! **Nature Generator**: procedural rocks, trees and plants built with **nodes**.
//!
//! A [`NatureGraph`] (see `graph.rs`) wires geometry nodes (`ops.rs`) into an Output; evaluating it at
//! a [`Quality`] gives a [`NatureModel`], a few parts each with its own material. Models export as
//! `.obj` files (one per part) and the editor places them in the scene as a group of entities.
//!
//! Everything here is pure Rust and deterministic for the seeds in the graph, so it is unit-tested
//! without an engine or a window. Presets are graphs too: open one and every node is there to edit.

#![allow(clippy::chunks_exact_to_as_chunks)]

pub mod bake;
pub mod graph;
pub mod mesh;
pub mod ops;
pub mod plant;
pub mod preview;
pub mod rock;
pub mod surface;
pub mod texlayers;
pub mod tree;

pub use graph::{Kind, NatureGraph, NatureNode, NodeId};
use mesh::NatureModel;

/// How much detail a model gets: stylised low-poly up to detailed close-ups.
///
/// The level is a property of the whole graph: nodes read it and trade polygons for fidelity
/// (counts, segments, subdivisions), so one graph serves a mobile game and a close-up.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Quality {
    /// Few polygons, flat shading, blob crowns and solid colours: mobile and stylised games.
    Stylized,
    Balanced,
    /// More segments, subdivisions and leaves for close-ups.
    Detailed,
}

impl Quality {
    pub const ALL: [Quality; 3] = [Quality::Stylized, Quality::Balanced, Quality::Detailed];

    pub fn label(self) -> &'static str {
        match self {
            Quality::Stylized => "Stylized (low poly)",
            Quality::Balanced => "Balanced",
            Quality::Detailed => "Detailed",
        }
    }

    pub fn from_index(index: usize) -> Self {
        Self::ALL.get(index).copied().unwrap_or(Quality::Balanced)
    }

    pub fn index(self) -> usize {
        Self::ALL.iter().position(|q| *q == self).unwrap_or(1)
    }
}

/// File-name friendly slug.
pub fn slug(name: &str) -> String {
    let s: String = name
        .to_lowercase()
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
        .collect();
    s.split('_')
        .filter(|p| !p.is_empty())
        .collect::<Vec<_>>()
        .join("_")
}

/// Writes one `.obj` per part into `dir` (`<slug>_<part>.obj`) and returns the file names in part order.
pub fn write_obj_files(
    model: &NatureModel,
    dir: &std::path::Path,
    name: &str,
) -> Result<Vec<String>, String> {
    std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    let mut files = Vec::new();
    for part in &model.parts {
        let file = format!("{}_{}.obj", slug(name), part.name);
        std::fs::write(dir.join(&file), part.to_obj()).map_err(|e| e.to_string())?;
        files.push(file);
    }
    Ok(files)
}

// ------------------------------------------------------------------ persistence

pub const FILE_HEADER: &str = "CAFNATURE2";
pub const EXTENSION: &str = "nature";

/// Text form of a saved model: header, name, quality and the graph.
pub fn to_text(name: &str, graph: &NatureGraph, quality: Quality) -> String {
    format!(
        "{FILE_HEADER}\nname {name}\nquality {}\n{}",
        quality.index(),
        graph.to_text()
    )
}

pub fn from_text(text: &str) -> Option<(String, NatureGraph, Quality)> {
    let mut lines = text.splitn(4, '\n');
    if lines.next()?.trim() != FILE_HEADER {
        return None;
    }
    let name = lines.next()?.strip_prefix("name ")?.to_owned();
    let quality = Quality::from_index(
        lines
            .next()?
            .strip_prefix("quality ")?
            .trim()
            .parse()
            .ok()?,
    );
    let graph = NatureGraph::from_text(lines.next()?)?;
    Some((name, graph, quality))
}

// --------------------------------------------------------------------- presets

pub struct NaturePreset {
    pub name: &'static str,
    pub category: &'static str,
    pub description: &'static str,
    pub quality: Quality,
    build: fn() -> NatureGraph,
}

impl NaturePreset {
    pub fn graph(&self) -> NatureGraph {
        (self.build)()
    }
}

use iced::Point;

fn at(col: f32, row: f32) -> Point {
    Point::new(col * 200.0 - 700.0, row * 140.0)
}

/// Connects output 0 of each node to input 0 of the next.
fn chain(g: &mut NatureGraph, ids: &[NodeId]) {
    for pair in ids.windows(2) {
        assert!(
            g.connect(pair[0], 0, pair[1], 0),
            "chain link {:?} failed",
            pair
        );
    }
}

fn part(g: &mut NatureGraph, col: f32, row: f32, name: &str, material: &str) -> NodeId {
    g.add_with(
        "part",
        at(col, row),
        &[
            ("name", ops::part_name_index(name)),
            ("material", ops::material_index(material)),
        ],
    )
}

fn finish(g: &mut NatureGraph, col: f32, parts: &[NodeId]) {
    let out = g.add("output", at(col, 1.0));
    for (i, p) in parts.iter().enumerate() {
        assert!(g.connect(*p, 0, out, i));
    }
}

/// The rock chain: lump, cut, grain, flatten, size, shade, UVs, ground. Returns the last node.
#[allow(clippy::too_many_arguments)]
fn rock_chain(
    g: &mut NatureGraph,
    row: f32,
    size: [f32; 3],
    detail: f32,
    roughness: f32,
    frequency: f32,
    planes: f32,
    depth: f32,
    flat: f32,
    faceted: bool,
    uv: f32,
    seed: f32,
) -> NodeId {
    let ico = g.add_with("icosphere", at(0.0, row), &[("detail", detail)]);
    let lump = g.add_with(
        "noise_displace",
        at(1.0, row),
        &[
            ("amount", roughness),
            ("frequency", frequency),
            ("seed", seed),
        ],
    );
    let cut = g.add_with(
        "cut_planes",
        at(2.0, row),
        &[("count", planes), ("depth", depth), ("seed", seed + 3.0)],
    );
    let grain = g.add_with(
        "noise_displace",
        at(3.0, row),
        &[
            ("amount", roughness * 0.08),
            ("frequency", frequency * 5.0),
            ("octaves", 2.0),
            ("seed", seed + 9.0),
        ],
    );
    let base = g.add_with("flat_base", at(4.0, row), &[("amount", flat)]);
    let scale = g.add_with(
        "transform",
        at(5.0, row),
        &[
            ("sx", size[0] * 0.5),
            ("sy", size[1] * 0.5),
            ("sz", size[2] * 0.5),
        ],
    );
    let shade = g.add_with(
        "shade",
        at(6.0, row),
        &[("mode", if faceted { 1.0 } else { 2.0 })],
    );
    let uvs = g.add_with("uv_box", at(7.0, row), &[("scale", uv)]);
    let ground = g.add("ground", at(8.0, row));
    chain(g, &[ico, lump, cut, grain, base, scale, shade, uvs, ground]);
    ground
}

#[allow(clippy::too_many_arguments)]
fn rock(
    size: [f32; 3],
    detail: f32,
    roughness: f32,
    planes: f32,
    depth: f32,
    flat: f32,
    faceted: bool,
    uv: f32,
    stone: &str,
    seed: f32,
) -> NatureGraph {
    let mut g = NatureGraph::new();
    let last = rock_chain(
        &mut g, 0.0, size, detail, roughness, 1.6, planes, depth, flat, faceted, uv, seed,
    );
    let p = part(&mut g, 9.0, 0.0, "rock", stone);
    assert!(g.connect(last, 0, p, 0));
    finish(&mut g, 10.0, &[p]);
    g
}

/// Moss: points scattered on the up-facing surface, a small blob instanced on each.
fn moss(
    g: &mut NatureGraph,
    surface: NodeId,
    col: f32,
    row: f32,
    count: f32,
    size: f32,
    seed: f32,
) -> NodeId {
    let scatter = g.add_with(
        "scatter",
        at(col, row),
        &[
            ("count", count),
            ("up_min", 0.35),
            ("scale_min", 0.55),
            ("scale_max", 1.2),
            ("seed", seed),
        ],
    );
    let blob = g.add_with(
        "ellipsoid",
        at(col, row + 1.0),
        &[
            ("rx", size),
            ("ry", size * 0.55),
            ("rz", size),
            ("detail", 2.0),
            ("wobble", 0.28),
            ("seed", seed + 5.0),
        ],
    );
    let inst = g.add_with(
        "instance",
        at(col + 1.0, row),
        &[("align", 0.8), ("sink", 0.25), ("seed", seed)],
    );
    assert!(g.connect(surface, 0, scatter, 0));
    assert!(g.connect(blob, 0, inst, 0));
    assert!(g.connect(scatter, 0, inst, 1));
    let p = part(g, col + 2.0, row, "moss", "Ground · Lawn Grass");
    assert!(g.connect(inst, 0, p, 0));
    p
}

/// A rock whose surface is a generated texture set instead of a library preset.
fn photoreal_rock() -> NatureGraph {
    let mut g = NatureGraph::new();
    let last = rock_chain(
        &mut g,
        0.0,
        [2.4, 1.5, 2.0],
        5.0,
        0.55,
        1.6,
        7.0,
        0.7,
        0.4,
        false,
        0.4,
        5.0,
    );
    let surface = g.add_with("surface", at(9.0, 0.0), &[("seed", 3.0)]);
    assert!(g.connect(last, 0, surface, 0));
    let rock_part = part(&mut g, 10.0, 0.0, "rock", "Rock · Cliff Granite");
    assert!(g.connect(surface, 0, rock_part, 0));
    finish(&mut g, 11.0, &[rock_part]);
    g
}

fn mossy_rock() -> NatureGraph {
    let mut g = NatureGraph::new();
    let last = rock_chain(
        &mut g,
        0.0,
        [2.4, 1.5, 2.0],
        5.0,
        0.6,
        1.6,
        6.0,
        0.8,
        0.4,
        false,
        0.4,
        9.0,
    );
    let rock_part = part(&mut g, 9.0, 0.0, "rock", "Rock · Cliff Granite");
    assert!(g.connect(last, 0, rock_part, 0));
    let moss_part = moss(&mut g, last, 9.0, 1.5, 420.0, 0.16, 4.0);
    finish(&mut g, 12.0, &[rock_part, moss_part]);
    g
}

#[allow(clippy::too_many_arguments)]
fn tree_nodes(
    g: &mut NatureGraph,
    row: f32,
    style: f32,
    height: f32,
    trunk: f32,
    levels: f32,
    branches: f32,
    angle: f32,
    length_ratio: f32,
    taper: f32,
    gnarl: f32,
    droop: f32,
    start: f32,
    seed: f32,
) -> NodeId {
    g.add_with(
        "grow",
        at(0.0, row),
        &[
            ("style", style),
            ("height", height),
            ("trunk_radius", trunk),
            ("levels", levels),
            ("branches", branches),
            ("branch_angle", angle),
            ("length_ratio", length_ratio),
            ("taper", taper),
            ("gnarl", gnarl),
            ("droop", droop),
            ("start", start),
            ("seed", seed),
        ],
    )
}

#[allow(clippy::too_many_arguments)]
fn tree(
    style: f32,
    height: f32,
    trunk: f32,
    levels: f32,
    branches: f32,
    angle: f32,
    length_ratio: f32,
    taper: f32,
    gnarl: f32,
    droop: f32,
    start: f32,
    leaf_density: f32,
    leaf_size: f32,
    bark: &str,
    foliage: &str,
    seed: f32,
) -> NatureGraph {
    let mut g = NatureGraph::new();
    let grow = tree_nodes(
        &mut g,
        1.0,
        style,
        height,
        trunk,
        levels,
        branches,
        angle,
        length_ratio,
        taper,
        gnarl,
        droop,
        start,
        seed,
    );
    let tubes = g.add("tubes", at(1.0, 0.0));
    let trunk_part = part(&mut g, 2.0, 0.0, "trunk", bark);
    chain(&mut g, &[grow, tubes, trunk_part]);
    let mut parts = vec![trunk_part];
    if leaf_density > 0.0 {
        let leaves = g.add_with(
            "leaves",
            at(1.0, 2.0),
            &[
                ("density", leaf_density),
                ("size", leaf_size),
                ("seed", seed),
                ("shapes", if foliage.contains("Mix") { 1.0 } else { 0.0 }),
            ],
        );
        let leaf_part = part(&mut g, 2.0, 2.0, "leaves", foliage);
        chain(&mut g, &[grow, leaves, leaf_part]);
        parts.push(leaf_part);
    }
    finish(&mut g, 3.0, &parts);
    g
}

fn ancient_oak() -> NatureGraph {
    let mut g = NatureGraph::new();
    let grow = tree_nodes(
        &mut g, 1.0, 0.0, 7.5, 0.42, 4.0, 4.0, 46.0, 0.6, 0.85, 0.4, 0.2, 0.33, 12.0,
    );
    let roots = g.add_with(
        "roots",
        at(1.0, 1.0),
        &[("count", 8.0), ("length", 1.4), ("seed", 3.0)],
    );
    let tubes = g.add_with("tubes", at(2.0, 0.0), &[("sides", 10.0)]);
    let trunk_part = part(&mut g, 3.0, 0.0, "trunk", "Bark · Oak");
    assert!(g.connect(grow, 0, roots, 0));
    assert!(g.connect(roots, 0, tubes, 0));
    assert!(g.connect(tubes, 0, trunk_part, 0));
    let leaves = g.add_with(
        "leaves",
        at(2.0, 2.0),
        &[
            ("density", 14.0),
            ("size", 0.75),
            ("seed", 12.0),
            ("shapes", 1.0),
        ],
    );
    let leaf_part = part(&mut g, 3.0, 2.0, "leaves", "Leaves · Broadleaf Mix (atlas)");
    assert!(g.connect(grow, 0, leaves, 0));
    assert!(g.connect(leaves, 0, leaf_part, 0));
    // Moss on the trunk and the roots (only the up-facing, lower parts get it).
    let scatter = g.add_with(
        "scatter",
        at(3.0, 3.0),
        &[
            ("count", 220.0),
            ("up_min", 0.2),
            ("scale_min", 0.5),
            ("scale_max", 1.1),
            ("seed", 6.0),
        ],
    );
    let blob = g.add_with(
        "ellipsoid",
        at(3.0, 4.0),
        &[
            ("rx", 0.07),
            ("ry", 0.04),
            ("rz", 0.07),
            ("detail", 2.0),
            ("wobble", 0.3),
            ("seed", 2.0),
        ],
    );
    let inst = g.add_with("instance", at(4.0, 3.0), &[("align", 0.7), ("sink", 0.3)]);
    let moss_part = part(&mut g, 5.0, 3.0, "moss", "Ground · Lawn Grass");
    assert!(g.connect(tubes, 0, scatter, 0));
    assert!(g.connect(blob, 0, inst, 0));
    assert!(g.connect(scatter, 0, inst, 1));
    assert!(g.connect(inst, 0, moss_part, 0));
    finish(&mut g, 6.0, &[trunk_part, leaf_part, moss_part]);
    g
}

fn single_plant(op: &'static str, set: &[(&str, f32)], name: &str, material: &str) -> NatureGraph {
    let mut g = NatureGraph::new();
    let node = g.add_with(op, at(0.0, 0.0), set);
    let p = part(&mut g, 1.0, 0.0, name, material);
    assert!(g.connect(node, 0, p, 0));
    finish(&mut g, 2.0, &[p]);
    g
}

fn flower(petal_material: &str, petals: f32, seed: f32) -> NatureGraph {
    let mut g = NatureGraph::new();
    let stem = g.add_with(
        "stem",
        at(0.0, 0.0),
        &[
            ("height", 0.5),
            ("radius", 0.014),
            ("lean", 0.3),
            ("seed", seed),
        ],
    );
    let leaves = g.add_with(
        "fronds",
        at(0.0, 1.0),
        &[
            ("count", 2.0),
            ("height", 0.18),
            ("width", 0.07),
            ("spread", 0.1),
            ("bend", 0.9),
            ("segments", 3.0),
            ("seed", seed),
        ],
    );
    let heads = g.add_with(
        "petals",
        at(0.0, 2.0),
        &[
            ("count", petals),
            ("size", 0.22),
            ("height", 0.5),
            ("lean", 0.3),
            ("seed", seed),
        ],
    );
    let center = g.add_with(
        "ellipsoid",
        at(0.0, 3.0),
        &[
            ("rx", 0.035),
            ("ry", 0.022),
            ("rz", 0.035),
            ("y", 0.5),
            ("detail", 1.0),
            ("wobble", 0.0),
        ],
    );
    let a = part(&mut g, 1.0, 0.0, "stem", "Solid · Green");
    let b = part(&mut g, 1.0, 1.0, "leaves", "Leaves · Green");
    let c = part(&mut g, 1.0, 2.0, "petals", petal_material);
    let d = part(&mut g, 1.0, 3.0, "center", "Solid · Yellow");
    for (src, dst) in [(stem, a), (leaves, b), (heads, c), (center, d)] {
        assert!(g.connect(src, 0, dst, 0));
    }
    finish(&mut g, 2.0, &[a, b, c, d]);
    g
}

pub fn presets() -> Vec<NaturePreset> {
    let p = |name, category, description, quality, build| NaturePreset {
        name,
        category,
        description,
        quality,
        build,
    };
    vec![
        p(
            "Boulder",
            "Rocks",
            "A rounded, weathered boulder.",
            Quality::Balanced,
            || {
                rock(
                    [1.6, 1.0, 1.3],
                    4.0,
                    0.5,
                    5.0,
                    0.85,
                    0.35,
                    false,
                    0.5,
                    "Rock · Cliff Granite",
                    1.0,
                )
            },
        ),
        p(
            "Cut Stone",
            "Rocks",
            "An angular rock with flat cleaved faces.",
            Quality::Balanced,
            || {
                rock(
                    [1.6, 1.0, 1.3],
                    4.0,
                    0.3,
                    16.0,
                    0.62,
                    0.35,
                    false,
                    0.5,
                    "Rock · Limestone",
                    4.0,
                )
            },
        ),
        p(
            "Low-poly Rock",
            "Rocks",
            "A faceted rock for stylised scenes.",
            Quality::Stylized,
            || {
                rock(
                    [1.6, 1.0, 1.3],
                    2.0,
                    0.4,
                    8.0,
                    0.72,
                    0.35,
                    true,
                    0.5,
                    "Rock · Sandstone",
                    7.0,
                )
            },
        ),
        p(
            "Flat Slab",
            "Rocks",
            "A wide, flat stepping stone.",
            Quality::Balanced,
            || {
                rock(
                    [2.4, 0.45, 1.8],
                    4.0,
                    0.35,
                    6.0,
                    0.72,
                    0.9,
                    false,
                    0.5,
                    "Rock · Lichen Granite",
                    3.0,
                )
            },
        ),
        p(
            "Pebble",
            "Rocks",
            "A small smooth stone.",
            Quality::Balanced,
            || {
                rock(
                    [0.35, 0.22, 0.28],
                    3.0,
                    0.2,
                    2.0,
                    0.72,
                    0.5,
                    false,
                    2.0,
                    "Rock · Cliff Granite",
                    11.0,
                )
            },
        ),
        p(
            "Crystal Spire",
            "Rocks",
            "A tall, sharply cut spire.",
            Quality::Balanced,
            || {
                rock(
                    [0.9, 2.6, 0.9],
                    4.0,
                    0.1,
                    20.0,
                    0.6,
                    0.5,
                    true,
                    0.5,
                    "Rock · Basalt",
                    5.0,
                )
            },
        ),
        p(
            "Mossy Rock",
            "Rocks",
            "High fidelity: a boulder wearing a layer of moss clumps scattered on its upper faces.",
            Quality::Detailed,
            mossy_rock,
        ),
        p(
            "Photoreal Rock",
            "Rocks",
            "High fidelity: a boulder with a generated texture set (albedo, normal, occlusion/roughness) baked from its own crevices and edges: strata, cracks, edge wear, lichen, moss, dust.",
            Quality::Detailed,
            photoreal_rock,
        ),
        p(
            "Oak",
            "Trees",
            "A broad tree with a rounded crown.",
            Quality::Balanced,
            || {
                tree(
                    0.0,
                    5.0,
                    0.24,
                    3.0,
                    4.0,
                    48.0,
                    0.62,
                    0.85,
                    0.35,
                    0.15,
                    0.35,
                    12.0,
                    0.7,
                    "Bark · Oak",
                    "Leaves · Broadleaf Mix (atlas)",
                    1.0,
                )
            },
        ),
        p(
            "Pine",
            "Trees",
            "A conifer with drooping whorls of branches.",
            Quality::Balanced,
            || {
                tree(
                    1.0,
                    7.5,
                    0.2,
                    2.0,
                    5.0,
                    70.0,
                    0.5,
                    0.9,
                    0.08,
                    0.4,
                    0.18,
                    12.0,
                    0.5,
                    "Bark · Pine",
                    "Leaves · Willow (narrow)",
                    3.0,
                )
            },
        ),
        p(
            "Birch",
            "Trees",
            "A slender, pale tree with light foliage.",
            Quality::Balanced,
            || {
                tree(
                    0.0,
                    6.5,
                    0.12,
                    3.0,
                    3.0,
                    38.0,
                    0.55,
                    0.85,
                    0.2,
                    0.2,
                    0.35,
                    12.0,
                    0.5,
                    "Bark · Birch",
                    "Leaves · Broadleaf Mix (atlas)",
                    5.0,
                )
            },
        ),
        p(
            "Autumn Maple",
            "Trees",
            "A rounded tree with orange foliage.",
            Quality::Balanced,
            || {
                tree(
                    0.0,
                    5.5,
                    0.24,
                    3.0,
                    5.0,
                    48.0,
                    0.62,
                    0.85,
                    0.35,
                    0.15,
                    0.35,
                    12.0,
                    0.75,
                    "Bark · Oak",
                    "Leaves · Autumn Mix (atlas)",
                    8.0,
                )
            },
        ),
        p(
            "Dead Tree",
            "Trees",
            "A bare, twisted tree.",
            Quality::Balanced,
            || {
                tree(
                    2.0,
                    4.5,
                    0.2,
                    4.0,
                    3.0,
                    55.0,
                    0.62,
                    0.85,
                    0.9,
                    0.25,
                    0.35,
                    0.0,
                    0.7,
                    "Bark · Oak",
                    "Leaves · Broadleaf Mix (atlas)",
                    6.0,
                )
            },
        ),
        p(
            "Low-poly Tree",
            "Trees",
            "A blob-crowned tree for stylised games.",
            Quality::Stylized,
            || {
                tree(
                    0.0,
                    4.0,
                    0.2,
                    2.0,
                    5.0,
                    48.0,
                    0.62,
                    0.85,
                    0.35,
                    0.15,
                    0.35,
                    12.0,
                    1.0,
                    "Bark · Oak",
                    "Leaves · Broadleaf Mix (atlas)",
                    2.0,
                )
            },
        ),
        p(
            "Low-poly Pine",
            "Trees",
            "A stylised conifer with chunky foliage clumps.",
            Quality::Stylized,
            || {
                tree(
                    1.0,
                    6.0,
                    0.18,
                    2.0,
                    5.0,
                    70.0,
                    0.5,
                    0.9,
                    0.05,
                    0.4,
                    0.2,
                    12.0,
                    1.0,
                    "Bark · Pine",
                    "Leaves · Broadleaf Mix (atlas)",
                    4.0,
                )
            },
        ),
        p(
            "Ancient Oak",
            "Trees",
            "High fidelity: four branch levels, flared roots and moss on the trunk.",
            Quality::Detailed,
            ancient_oak,
        ),
        p(
            "Grass Clump",
            "Plants",
            "A tuft of grass blades.",
            Quality::Balanced,
            || single_plant("blades", &[], "blades", "Leaves · Willow (narrow)"),
        ),
        p(
            "Tall Grass",
            "Plants",
            "Long, arching blades.",
            Quality::Balanced,
            || {
                single_plant(
                    "blades",
                    &[
                        ("count", 48.0),
                        ("height", 0.9),
                        ("width", 0.06),
                        ("spread", 0.35),
                        ("bend", 0.8),
                        ("seed", 4.0),
                    ],
                    "blades",
                    "Leaves · Willow (narrow)",
                )
            },
        ),
        p(
            "Fern",
            "Plants",
            "A rosette of arching fronds.",
            Quality::Balanced,
            || single_plant("fronds", &[], "fronds", "Leaves · Willow (narrow)"),
        ),
        p(
            "Fern Patch",
            "Plants",
            "High fidelity: many large fronds in a loose patch, like a forest floor.",
            Quality::Detailed,
            || {
                single_plant(
                    "fronds",
                    &[
                        ("count", 34.0),
                        ("height", 0.85),
                        ("width", 0.24),
                        ("spread", 1.1),
                        ("bend", 0.85),
                        ("segments", 6.0),
                        ("seed", 3.0),
                    ],
                    "fronds",
                    "Leaves · Willow (narrow)",
                )
            },
        ),
        p(
            "Shrub",
            "Plants",
            "A leafy low bush.",
            Quality::Balanced,
            || single_plant("bush", &[], "leaves", "Leaves · Green"),
        ),
        p(
            "Autumn Shrub",
            "Plants",
            "A bush with orange leaves.",
            Quality::Balanced,
            || single_plant("bush", &[("seed", 3.0)], "leaves", "Leaves · Autumn"),
        ),
        p(
            "Pink Flower",
            "Plants",
            "A single flower on a stem.",
            Quality::Balanced,
            || flower("Petals · Pink", 9.0, 1.0),
        ),
        p(
            "White Flower",
            "Plants",
            "A daisy-like flower.",
            Quality::Balanced,
            || flower("Petals · White", 14.0, 6.0),
        ),
    ]
}

#[cfg(test)]
mod tests;

//! The node library of the Nature Generator: every op is one row of [`NODES`].
//!
//! Categories: **Shapes** (icosphere, ellipsoid), **Modifiers** (noise, cut planes, flat base,
//! transform, shading, UVs, subdivide, merge), **Trees** (grow, roots, tubes, leaves), **Plants**
//! (blades, fronds, bushes, stems, petals), **Scatter** (points on a surface, instances) and
//! **Output** (Part, Output).

use super::graph::*;
use super::mesh::*;
use super::plant;
use super::rock;
use super::tree::{self, GrowParams, TreeStyle};
use crate::uikit::procedural::{choice, float, int};

// ---------------------------------------------------------------- materials

/// Where a part's material comes from. The index in [`MATERIAL_LABELS`] is what graphs store.
#[derive(Debug, Clone, Copy)]
enum Source {
    Library(&'static str),
    Cards(&'static str),
    Solid([f32; 3]),
    Custom,
    ProjectMaterial,
    ProjectTexture,
    /// Keep the material a Material node upstream gave the mesh.
    FromNode,
}

/// `(label, source, stylised colour)`.
const MATERIALS: &[(&str, Source, [f32; 3])] = &[
    (
        "Rock · Cliff Granite",
        Source::Library("Cliff Granite"),
        [0.50, 0.50, 0.52],
    ),
    (
        "Rock · Basalt",
        Source::Library("Basalt Columns"),
        [0.20, 0.20, 0.22],
    ),
    (
        "Rock · Limestone",
        Source::Library("Limestone Strata"),
        [0.72, 0.70, 0.62],
    ),
    (
        "Rock · Sandstone",
        Source::Library("Sandstone Canyon"),
        [0.68, 0.45, 0.30],
    ),
    (
        "Rock · Mossy Boulder",
        Source::Library("Mossy Boulder"),
        [0.40, 0.45, 0.35],
    ),
    (
        "Rock · Lichen Granite",
        Source::Library("Lichen Granite"),
        [0.55, 0.55, 0.50],
    ),
    (
        "Bark · Oak",
        Source::Library("Oak Bark"),
        [0.36, 0.25, 0.16],
    ),
    (
        "Bark · Pine",
        Source::Library("Pine Bark"),
        [0.33, 0.22, 0.15],
    ),
    (
        "Bark · Birch",
        Source::Library("Birch Bark"),
        [0.82, 0.80, 0.75],
    ),
    (
        "Ground · Lawn Grass",
        Source::Library("Lawn Grass"),
        [0.30, 0.48, 0.20],
    ),
    (
        "Ground · Pine Needles",
        Source::Library("Pine Needles"),
        [0.35, 0.25, 0.15],
    ),
    (
        "Leaves · Green",
        Source::Cards("Green Leaf"),
        [0.26, 0.50, 0.16],
    ),
    (
        "Leaves · Autumn",
        Source::Cards("Autumn Leaf"),
        [0.80, 0.42, 0.10],
    ),
    (
        "Leaves · Willow (narrow)",
        Source::Cards("Willow Leaf"),
        [0.36, 0.55, 0.22],
    ),
    (
        "Petals · Pink",
        Source::Cards("Petal Pink"),
        [0.92, 0.45, 0.62],
    ),
    (
        "Petals · White",
        Source::Cards("Petal White"),
        [0.94, 0.94, 0.92],
    ),
    (
        "Petals · Yellow",
        Source::Cards("Petal Yellow"),
        [0.97, 0.75, 0.15],
    ),
    (
        "Solid · Green",
        Source::Solid([0.24, 0.45, 0.16]),
        [0.24, 0.45, 0.16],
    ),
    (
        "Solid · Brown",
        Source::Solid([0.36, 0.25, 0.16]),
        [0.36, 0.25, 0.16],
    ),
    (
        "Solid · Grey",
        Source::Solid([0.5, 0.5, 0.52]),
        [0.5, 0.5, 0.52],
    ),
    (
        "Solid · Yellow",
        Source::Solid([0.95, 0.75, 0.1]),
        [0.95, 0.75, 0.1],
    ),
    ("Solid · Custom colour", Source::Custom, [0.5, 0.5, 0.5]),
    (
        "Your material (.mat)",
        Source::ProjectMaterial,
        [0.5, 0.4, 0.3],
    ),
    (
        "Your texture (cut-out PNG)",
        Source::ProjectTexture,
        [0.5, 0.5, 0.5],
    ),
    (
        "Leaves · Broadleaf Mix (atlas)",
        Source::Cards("Atlas · Broadleaf"),
        [0.26, 0.50, 0.16],
    ),
    (
        "Leaves · Autumn Mix (atlas)",
        Source::Cards("Atlas · Autumn"),
        [0.80, 0.42, 0.10],
    ),
    (
        "From nodes (Material / Texture)",
        Source::FromNode,
        [0.5, 0.5, 0.5],
    ),
];

const MATERIAL_LABELS: &[&str] = &[
    "Rock · Cliff Granite",
    "Rock · Basalt",
    "Rock · Limestone",
    "Rock · Sandstone",
    "Rock · Mossy Boulder",
    "Rock · Lichen Granite",
    "Bark · Oak",
    "Bark · Pine",
    "Bark · Birch",
    "Ground · Lawn Grass",
    "Ground · Pine Needles",
    "Leaves · Green",
    "Leaves · Autumn",
    "Leaves · Willow (narrow)",
    "Petals · Pink",
    "Petals · White",
    "Petals · Yellow",
    "Solid · Green",
    "Solid · Brown",
    "Solid · Grey",
    "Solid · Yellow",
    "Solid · Custom colour",
    "Your material (.mat)",
    "Your texture (cut-out PNG)",
    "Leaves · Broadleaf Mix (atlas)",
    "Leaves · Autumn Mix (atlas)",
    "From nodes (Material / Texture)",
];

/// True when the label table the editor shows matches the material table graphs index into.
pub fn material_tables_match() -> bool {
    MATERIALS.len() == MATERIAL_LABELS.len()
        && MATERIALS
            .iter()
            .zip(MATERIAL_LABELS)
            .all(|(m, l)| m.0 == *l)
}

/// Index of a part name (used by presets).
pub fn part_name_index(name: &str) -> f32 {
    PART_NAMES.iter().position(|n| *n == name).unwrap_or(0) as f32
}

/// Index of a material label (used by presets).
pub fn material_index(label: &str) -> f32 {
    MATERIALS.iter().position(|m| m.0 == label).unwrap_or(0) as f32
}

/// True when a Part's material choice is "From nodes" (keep what upstream produced).
pub fn is_from_nodes_choice(index: usize) -> bool {
    index == MATERIAL_LABELS.len() - 1
}

const PART_NAMES: &[&str] = &[
    "trunk", "branches", "roots", "leaves", "rock", "moss", "stem", "petals", "center", "blades",
    "fronds", "ground", "body",
];

/// The material for a table label at a quality level (low poly falls back to a solid colour).
pub fn spec_for_label(label: &str, quality: super::Quality) -> MaterialSpec {
    let (_, source, stylised) = MATERIALS
        .iter()
        .find(|m| m.0 == label)
        .copied()
        .unwrap_or(MATERIALS[0]);
    match source {
        Source::Library(_) | Source::Cards(_) if quality == super::Quality::Stylized => {
            MaterialSpec::Solid {
                rgb: stylised,
                roughness: 0.85,
            }
        }
        Source::Library(name) => MaterialSpec::Preset(name),
        Source::Cards(name) => MaterialSpec::Cards(name),
        Source::Solid(rgb) => MaterialSpec::Solid {
            rgb,
            roughness: 0.8,
        },
        Source::Custom | Source::ProjectMaterial | Source::ProjectTexture | Source::FromNode => {
            MaterialSpec::Solid {
                rgb: stylised,
                roughness: 0.85,
            }
        }
    }
}

/// The colour of a `Solid · ...` label.
pub fn solid_rgb(label: &str) -> [f32; 3] {
    MATERIALS
        .iter()
        .find_map(|m| match m.1 {
            Source::Solid(rgb) if m.0 == label => Some(rgb),
            _ => None,
        })
        .unwrap_or([0.4, 0.4, 0.4])
}

/// Labels the material nodes offer, by kind.
pub const LIBRARY_LABELS: &[&str] = &[
    "Rock · Cliff Granite",
    "Rock · Basalt",
    "Rock · Limestone",
    "Rock · Sandstone",
    "Rock · Mossy Boulder",
    "Rock · Lichen Granite",
    "Bark · Oak",
    "Bark · Pine",
    "Bark · Birch",
    "Ground · Lawn Grass",
    "Ground · Pine Needles",
];
pub const CARD_LABELS: &[&str] = &[
    "Leaves · Green",
    "Leaves · Autumn",
    "Leaves · Willow (narrow)",
    "Leaves · Broadleaf Mix (atlas)",
    "Leaves · Autumn Mix (atlas)",
    "Petals · Pink",
    "Petals · White",
    "Petals · Yellow",
];

/// Atlas columns and rows of a card material (1 x 1 = one picture).
pub fn atlas_of(spec: &MaterialSpec) -> (u32, u32) {
    match spec {
        MaterialSpec::Cards(name) if name.starts_with("Atlas") => (4, 2),
        MaterialSpec::Project(_, atlas) | MaterialSpec::ProjectTexture(_, atlas) => *atlas,
        _ => (1, 1),
    }
}

/// The material a Part node gives its mesh when no material is wired into its port.
fn material_for(
    index: usize,
    rgb: [f32; 3],
    roughness: f32,
    path: &str,
    quality: super::Quality,
) -> MaterialSpec {
    let (_, source, stylised) = MATERIALS[index.min(MATERIALS.len() - 1)];
    let stylized = quality == super::Quality::Stylized;
    match source {
        Source::Library(_) | Source::Cards(_) if stylized => MaterialSpec::Solid {
            rgb: stylised,
            roughness: 0.85,
        },
        Source::Library(name) => MaterialSpec::Preset(name),
        Source::Cards(name) => MaterialSpec::Cards(name),
        Source::Solid(rgb) => MaterialSpec::Solid {
            rgb,
            roughness: 0.8,
        },
        Source::Custom => MaterialSpec::Solid { rgb, roughness },
        // Your own assets are always used as they are, whatever the quality.
        Source::ProjectMaterial if !path.is_empty() => {
            MaterialSpec::Project(path.to_owned(), (1, 1))
        }
        Source::ProjectTexture if !path.is_empty() => {
            MaterialSpec::ProjectTexture(path.to_owned(), (1, 1))
        }
        Source::ProjectMaterial | Source::ProjectTexture | Source::FromNode => {
            MaterialSpec::Solid {
                rgb: stylised,
                roughness: 0.85,
            }
        }
    }
}

fn preview_colour(spec: &MaterialSpec, fallback: [f32; 3]) -> [f32; 3] {
    match spec {
        MaterialSpec::Solid { rgb, .. } => *rgb,
        MaterialSpec::Preset(name) | MaterialSpec::Cards(name) => MATERIALS
            .iter()
            .find(|m| matches!(m.1, Source::Library(n) | Source::Cards(n) if n == *name))
            .map(|m| m.2)
            .unwrap_or(fallback),
        _ => fallback,
    }
}

// --------------------------------------------------------------------- helpers

fn mesh_out(part: Part) -> Vec<Value> {
    vec![Value::Mesh(part)]
}

fn rng_for(a: &Args, seed: f32) -> Rng {
    Rng::new((seed as u32).wrapping_add(a.salt))
}

fn centroid_radius(part: &Part) -> (V3, f32) {
    let (lo, hi) = part.bounds();
    let c = lerp(lo, hi, 0.5);
    let r = part
        .positions
        .iter()
        .map(|p| length(sub(*p, c)))
        .fold(0.0f32, f32::max);
    (c, r.max(1e-4))
}

// ------------------------------------------------------------------- shapes

const ICO_P: &[crate::uikit::procedural::ParamSpec] = &[
    int("detail", "Subdivisions", 1, 5, 4),
    float("radius", "Radius (m)", 0.05, 10.0, 1.0, 0.05),
];

fn eval_icosphere(a: &Args) -> Vec<Value> {
    let detail = if a.stylized() {
        a.u(0).min(2)
    } else if a.quality == super::Quality::Detailed {
        a.u(0) + 1
    } else {
        a.u(0)
    };
    let radius = a.f(1);
    let (directions, faces) = rock::icosphere(detail.clamp(1, 5));
    let mut part = Part::new(
        "mesh",
        MaterialSpec::Solid {
            rgb: [0.5; 3],
            roughness: 0.9,
        },
        [0.5, 0.5, 0.52],
    );
    for d in &directions {
        part.vertex(mul(*d, radius), *d, [0.0, 0.0]);
    }
    for f in &faces {
        part.triangle(f[0], f[1], f[2]);
    }
    mesh_out(part)
}

const ELLIPSOID_P: &[crate::uikit::procedural::ParamSpec] = &[
    float("rx", "Radius X (m)", 0.005, 5.0, 0.2, 0.005),
    float("ry", "Radius Y (m)", 0.005, 5.0, 0.2, 0.005),
    float("rz", "Radius Z (m)", 0.005, 5.0, 0.2, 0.005),
    float("x", "Offset X", -10.0, 10.0, 0.0, 0.01),
    float("y", "Offset Y", -10.0, 10.0, 0.0, 0.01),
    float("z", "Offset Z", -10.0, 10.0, 0.0, 0.01),
    int("detail", "Subdivisions", 1, 4, 2),
    float("wobble", "Wobble", 0.0, 0.6, 0.15, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_ellipsoid(a: &Args) -> Vec<Value> {
    let detail = if a.stylized() { a.u(6).min(1) } else { a.u(6) };
    mesh_out(plant::ellipsoid(
        [a.f(0), a.f(1), a.f(2)],
        [a.f(3), a.f(4), a.f(5)],
        detail,
        a.f(7),
        a.u(8).wrapping_add(a.salt),
    ))
}

// ---------------------------------------------------------------- modifiers

const SURFACE_RES: &[&str] = &["256", "512", "1024", "2048"];

const SURFACE_P: &[crate::uikit::procedural::ParamSpec] = &[
    float("moss", "Moss", 0.0, 1.0, 0.5, 0.01),
    float("lichen", "Lichen", 0.0, 1.0, 0.4, 0.01),
    float("cracks", "Cracks", 0.0, 1.0, 0.5, 0.01),
    float("wear", "Edge wear", 0.0, 1.0, 0.5, 0.01),
    float("dust", "Dust", 0.0, 1.0, 0.3, 0.01),
    float("scale", "Pattern scale", 0.2, 4.0, 1.0, 0.05),
    float("warmth", "Warmth (grey to sandstone)", 0.0, 1.0, 0.4, 0.01),
    choice("resolution", "Texture size", SURFACE_RES, 2),
    int("seed", "Seed", 0, 9999, 1),
];

/// Attaches a generated rock texture set (baked from this mesh's own crevices and edges).
fn eval_surface(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    let mut part = super::bake::atlas_unwrap(mesh);
    let resolution = [256u32, 512, 1024, 2048][(a.u(7) as usize).min(3)];
    // The stylised range never needs big textures.
    let resolution = if a.stylized() {
        resolution.min(512)
    } else {
        resolution
    };
    part.surface = Some(super::surface::SurfaceSpec {
        moss: a.f(0),
        lichen: a.f(1),
        cracks: a.f(2),
        wear: a.f(3),
        dust: a.f(4),
        scale: a.f(5),
        warmth: a.f(6),
        resolution,
        seed: a.u(8),
    });
    mesh_out(part)
}

const DISPLACE_P: &[crate::uikit::procedural::ParamSpec] = &[
    float("amount", "Amount", 0.0, 2.0, 0.5, 0.01),
    float("frequency", "Frequency", 0.2, 12.0, 1.6, 0.05),
    int("octaves", "Octaves", 1, 6, 4),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_displace(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    let (center, radius) = centroid_radius(mesh);
    let offset = {
        let mut rng = rng_for(a, a.f(3));
        [
            rng.range(-50.0, 50.0),
            rng.range(-50.0, 50.0),
            rng.range(-50.0, 50.0),
        ]
    };
    let mut part = mesh.clone();
    for (i, p) in part.positions.iter_mut().enumerate() {
        // Along the normal, so the shape is whatever the previous node made.
        let normal = if length(mesh.normals[i]) > 0.5 {
            mesh.normals[i]
        } else {
            normalize(sub(*p, center))
        };
        let noise = fbm3(
            add(mul(sub(*p, center), a.f(1) / radius), offset),
            a.u(3),
            a.u(2).max(1),
        );
        *p = add(*p, mul(normal, a.f(0) * 0.9 * (noise - 0.5) * radius));
    }
    mesh_out(part)
}

const CUT_P: &[crate::uikit::procedural::ParamSpec] = &[
    int("count", "Planes", 0, 40, 7),
    float("depth", "Depth", 0.3, 1.0, 0.72, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_cut(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    let (lo, hi) = mesh.bounds();
    let center = lerp(lo, hi, 0.5);
    let half = mul(sub(hi, lo), 0.5);
    let mut rng = rng_for(a, a.f(2));
    let planes: Vec<(V3, f32)> = (0..a.u(0).min(40))
        .map(|_| (rng.direction(), rng.range(a.f(1), 1.0)))
        .collect();
    let mut part = mesh.clone();
    for p in &mut part.positions {
        let mut q = [
            (p[0] - center[0]) / half[0].max(1e-4),
            (p[1] - center[1]) / half[1].max(1e-4),
            (p[2] - center[2]) / half[2].max(1e-4),
        ];
        for (n, d) in &planes {
            let over = dot(q, *n) - d;
            if over > 0.0 {
                q = sub(q, mul(*n, over));
            }
        }
        *p = [
            center[0] + q[0] * half[0],
            center[1] + q[1] * half[1],
            center[2] + q[2] * half[2],
        ];
    }
    mesh_out(part)
}

const FLAT_P: &[crate::uikit::procedural::ParamSpec] =
    &[float("amount", "Flatten", 0.0, 1.0, 0.35, 0.01)];

fn eval_flat_base(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    let (lo, hi) = mesh.bounds();
    let center_y = (lo[1] + hi[1]) * 0.5;
    let floor = center_y + (lo[1] - center_y) * (1.0 - a.f(0) * 0.8);
    let mut part = mesh.clone();
    for p in &mut part.positions {
        p[1] = p[1].max(floor);
    }
    mesh_out(part)
}

const TRANSFORM_P: &[crate::uikit::procedural::ParamSpec] = &[
    float("sx", "Scale X", 0.01, 30.0, 1.0, 0.01),
    float("sy", "Scale Y", 0.01, 30.0, 1.0, 0.01),
    float("sz", "Scale Z", 0.01, 30.0, 1.0, 0.01),
    float("rotate", "Rotate Y (deg)", -180.0, 180.0, 0.0, 1.0),
    float("x", "Move X", -30.0, 30.0, 0.0, 0.01),
    float("y", "Move Y", -30.0, 30.0, 0.0, 0.01),
    float("z", "Move Z", -30.0, 30.0, 0.0, 0.01),
];

fn rotate_y(v: V3, radians: f32) -> V3 {
    let (s, c) = radians.sin_cos();
    [v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c]
}

fn eval_transform(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    let (scale, turn) = ([a.f(0), a.f(1), a.f(2)], a.f(3).to_radians());
    let mut part = mesh.clone();
    for (i, p) in part.positions.iter_mut().enumerate() {
        let q = rotate_y([p[0] * scale[0], p[1] * scale[1], p[2] * scale[2]], turn);
        *p = add(q, [a.f(4), a.f(5), a.f(6)]);
        let n = mesh.normals[i];
        part.normals[i] = normalize(rotate_y(
            [n[0] / scale[0], n[1] / scale[1], n[2] / scale[2]],
            turn,
        ));
    }
    mesh_out(part)
}

fn eval_ground(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    let (lo, _) = mesh.bounds();
    let mut part = mesh.clone();
    part.translate([0.0, -lo[1], 0.0]);
    mesh_out(part)
}

const SHADE_P: &[crate::uikit::procedural::ParamSpec] = &[
    choice(
        "mode",
        "Shading",
        &["Smooth", "Flat (low poly)", "Crease"],
        2,
    ),
    float("angle", "Crease angle", 5.0, 90.0, 38.0, 1.0),
];

fn eval_shade(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    // Stylised models are always faceted.
    let mode = if a.stylized() { 1 } else { a.u(0) };
    let part = match mode {
        0 => {
            let mut welded = mesh.weld(1e-5);
            welded.recompute_normals();
            welded
        }
        1 => {
            let mut p = mesh.clone();
            p.flat_shade();
            p
        }
        _ => mesh.crease_normals(a.f(1)),
    };
    mesh_out(part)
}

const UVBOX_P: &[crate::uikit::procedural::ParamSpec] =
    &[float("scale", "Texture repeats / m", 0.05, 8.0, 0.5, 0.05)];

fn eval_uv_box(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    mesh_out(rock::box_uv_unweld(mesh, a.f(0)))
}

const SUBDIV_P: &[crate::uikit::procedural::ParamSpec] = &[int("levels", "Levels", 1, 3, 1)];

fn eval_subdivide(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    let mut part = mesh.clone();
    for _ in 0..a.u(0).clamp(1, 3) {
        if part.triangle_count() > 60_000 {
            break;
        }
        part = part.subdivide();
    }
    mesh_out(part)
}

const MERGE_IN: &[PortDef] = &[
    port("A", Kind::Mesh),
    port("B", Kind::Mesh),
    port("C", Kind::Mesh),
    port("D", Kind::Mesh),
];

fn eval_merge(a: &Args) -> Vec<Value> {
    let mut out: Option<Part> = None;
    for i in 0..4 {
        if let Some(mesh) = a.mesh(i) {
            match out.as_mut() {
                Some(part) => part.append(mesh),
                None => out = Some(mesh.clone()),
            }
        }
    }
    mesh_out(out.unwrap_or_else(empty))
}

fn empty() -> Part {
    Part::new(
        "mesh",
        MaterialSpec::Solid {
            rgb: [0.5; 3],
            roughness: 0.9,
        },
        [0.5; 3],
    )
}

// ------------------------------------------------------------------- trees

const GROW_P: &[crate::uikit::procedural::ParamSpec] = &[
    choice("style", "Style", &TreeStyle::LABELS, 0),
    float("height", "Height (m)", 0.8, 40.0, 5.0, 0.1),
    float("trunk_radius", "Trunk radius (m)", 0.02, 2.0, 0.24, 0.01),
    int("levels", "Branch levels", 1, 4, 3),
    int("branches", "Branches per limb", 1, 8, 4),
    float("branch_angle", "Branch angle", 10.0, 85.0, 48.0, 1.0),
    float("length_ratio", "Child length", 0.25, 0.95, 0.62, 0.01),
    float("radius_ratio", "Child thickness", 0.2, 0.9, 0.55, 0.01),
    float("taper", "Taper", 0.0, 1.0, 0.85, 0.01),
    float("gnarl", "Gnarl", 0.0, 1.0, 0.35, 0.01),
    float("droop", "Droop", -0.5, 1.0, 0.15, 0.01),
    float("start", "Branches start at", 0.0, 0.9, 0.35, 0.01),
    int("max_segments", "Segment budget", 100, 6000, 1800),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_grow(a: &Args) -> Vec<Value> {
    let (levels, budget) = if a.stylized() {
        (a.u(3).min(2), a.u(12).min(260))
    } else {
        (a.u(3), a.u(12))
    };
    let params = GrowParams {
        style: TreeStyle::from_index(a.u(0) as usize),
        height: a.f(1),
        trunk_radius: a.f(2),
        levels: levels.clamp(1, 4),
        branches: a.u(4).clamp(1, 8),
        branch_angle: a.f(5),
        length_ratio: a.f(6),
        radius_ratio: a.f(7),
        taper: a.f(8),
        gnarl: a.f(9),
        droop: a.f(10),
        start: a.f(11),
        max_segments: if a.quality == super::Quality::Detailed {
            budget * 3 / 2
        } else {
            budget
        },
        seed: a.u(13).wrapping_add(a.salt),
    };
    vec![Value::Skeleton(tree::grow(&params))]
}

const ROOTS_P: &[crate::uikit::procedural::ParamSpec] = &[
    int("count", "Roots", 0, 14, 6),
    float("length", "Length (m)", 0.1, 5.0, 0.9, 0.05),
    float("thickness", "Thickness", 0.1, 1.0, 0.45, 0.01),
    float("sink", "Dive below ground", 0.0, 1.0, 0.35, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_roots(a: &Args) -> Vec<Value> {
    let Some(skeleton) = a.skeleton(0) else {
        return vec![Value::Skeleton(Default::default())];
    };
    let mut out = skeleton.clone();
    let Some(trunk) = skeleton.branches.first() else {
        return vec![Value::Skeleton(out)];
    };
    let base_radius = trunk.radii[0];
    let mut rng = rng_for(a, a.f(4));
    let count = a.q(a.f(0)).round().max(0.0) as u32;
    for k in 0..count {
        let angle = k as f32 / count.max(1) as f32 * std::f32::consts::TAU + rng.range(-0.3, 0.3);
        let out_dir = [angle.cos(), 0.0, angle.sin()];
        let length_m = a.f(1) * rng.range(0.7, 1.2);
        let steps = 5;
        let mut points = Vec::new();
        for i in 0..=steps {
            let t = i as f32 / steps as f32;
            // Flares outwards first, then dives into the ground.
            let horizontal = length_m * (t * (1.2 - 0.5 * t));
            let height = base_radius * 0.9 * (1.0 - t).powi(2) - length_m * a.f(3) * t * t;
            points.push(add(
                mul(out_dir, base_radius * 0.5 + horizontal),
                [0.0, height + 0.02, 0.0],
            ));
        }
        let radii: Vec<f32> = (0..=steps)
            .map(|i| (base_radius * a.f(2) * (1.0 - i as f32 / steps as f32 * 0.9)).max(0.006))
            .collect();
        out.branches.push(tree::Branch {
            points,
            radii,
            level: 1,
            terminal: false,
            uv_start: rng.range(0.0, 3.0),
            kind: 1,
        });
    }
    vec![Value::Skeleton(out)]
}

const TUBES_P: &[crate::uikit::procedural::ParamSpec] = &[
    int("sides", "Sides", 3, 12, 8),
    float("bark_scale", "Bark repeats / m", 0.1, 3.0, 0.6, 0.05),
    int("min_level", "From level", 0, 4, 0),
    int("max_level", "To level", 0, 4, 4),
];

fn eval_tubes(a: &Args) -> Vec<Value> {
    let Some(skeleton) = a.skeleton(0) else {
        return mesh_out(empty());
    };
    let sides = match a.quality {
        super::Quality::Stylized => a.u(0).min(5),
        super::Quality::Balanced => a.u(0),
        super::Quality::Detailed => a.u(0) + 4,
    };
    let mut part = tree::tubes(skeleton, sides, a.f(1), a.u(2), a.u(3));
    if a.stylized() {
        part.flat_shade();
    }
    mesh_out(part)
}

const LEAVES_IN: &[PortDef] = &[
    port("Skeleton", Kind::Skeleton),
    port("Texture", Kind::Material),
];
const LEAVES_P: &[crate::uikit::procedural::ParamSpec] = &[
    choice("style", "Style", &["Cards", "Blobs"], 0),
    float("density", "Leaves per metre", 0.0, 30.0, 12.0, 0.5),
    float("size", "Leaf size (% of tree height)", 0.5, 40.0, 7.0, 0.1),
    float("inner", "Inner limbs", 0.0, 1.0, 0.45, 0.01),
    int("budget", "Triangle budget", 200, 30000, 9000),
    int("seed", "Seed", 0, 9999, 1),
    float("fold", "Fold along the midrib", 0.0, 1.0, 0.35, 0.01),
    float("droop", "Droop", 0.0, 1.0, 0.3, 0.01),
];

fn eval_leaves(a: &Args) -> Vec<Value> {
    let Some(skeleton) = a.skeleton(0) else {
        return mesh_out(empty());
    };
    let seed = a.u(5).wrapping_add(a.salt);
    // The leaf size is a share of the tree's height, so it scales with the tree.
    let leaf = a.f(2) / 100.0 * skeleton.height.max(0.1);
    if a.u(0) == 1 || a.stylized() {
        let mut part = tree::leaf_blobs(
            skeleton,
            leaf.max(if a.stylized() { 0.9 } else { 0.0 }),
            1,
            seed,
        );
        part.flat_shade();
        return mesh_out(part);
    }
    let rows = match a.quality {
        super::Quality::Stylized => 1,
        super::Quality::Balanced => 2,
        super::Quality::Detailed => 4,
    };
    // The texture wired into the node tells it how many leaf shapes its atlas holds.
    let atlas = a.material(1).map_or((1, 1), atlas_of);
    mesh_out(tree::leaf_cards(
        skeleton,
        tree::LeafCards {
            // Atlas leaves are drawn smaller and sparser per card: more of them fill the crown.
            density: a.q(a.f(1)) * if atlas != (1, 1) { 1.5 } else { 1.0 },
            size: leaf,
            inner_share: a.f(3),
            max_triangles: a.u(4) as usize,
            atlas,
            fold: a.f(6),
            droop: a.f(7),
            rows,
            shade_cell: (atlas.0 * atlas.1 > 1).then_some(4),
        },
        seed,
    ))
}

// ------------------------------------------------------------------ plants

const BLADES_P: &[crate::uikit::procedural::ParamSpec] = &[
    int("count", "Blades", 1, 400, 36),
    float("height", "Height (m)", 0.05, 3.0, 0.45, 0.01),
    float("width", "Width (m)", 0.005, 0.5, 0.05, 0.005),
    float("spread", "Spread (m)", 0.03, 3.0, 0.28, 0.01),
    float("bend", "Bend", 0.0, 1.0, 0.55, 0.01),
    int("segments", "Segments", 2, 8, 4),
    int("seed", "Seed", 0, 9999, 1),
];

fn segments(a: &Args, i: usize) -> u32 {
    match a.quality {
        super::Quality::Stylized => 2,
        super::Quality::Balanced => a.u(i),
        super::Quality::Detailed => a.u(i) + 2,
    }
}

fn eval_blades(a: &Args) -> Vec<Value> {
    mesh_out(plant::blade_clump(
        a.q(a.f(0)).round().max(1.0) as u32,
        a.f(1),
        a.f(2),
        a.f(3),
        a.f(4),
        segments(a, 5),
        a.u(6).wrapping_add(a.salt),
    ))
}

const FRONDS_P: &[crate::uikit::procedural::ParamSpec] = &[
    int("count", "Fronds", 1, 60, 11),
    float("height", "Length (m)", 0.05, 3.0, 0.7, 0.01),
    float("width", "Width (m)", 0.01, 1.0, 0.2, 0.005),
    float("spread", "Spread (m)", 0.03, 3.0, 0.5, 0.01),
    float("bend", "Arch", 0.0, 1.0, 0.8, 0.01),
    int("segments", "Segments", 2, 8, 6),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_fronds(a: &Args) -> Vec<Value> {
    mesh_out(plant::fronds(
        a.q(a.f(0)).round().max(1.0) as u32,
        a.f(1),
        a.f(2),
        a.f(3),
        a.f(4),
        segments(a, 5),
        a.u(6).wrapping_add(a.salt),
    ))
}

const BUSH_P: &[crate::uikit::procedural::ParamSpec] = &[
    int("count", "Leaves", 5, 500, 120),
    float("height", "Height (m)", 0.1, 4.0, 0.9, 0.01),
    float("width", "Leaf size (m)", 0.03, 1.0, 0.26, 0.005),
    float("spread", "Spread (m)", 0.1, 3.0, 0.7, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_bush(a: &Args) -> Vec<Value> {
    mesh_out(plant::leaf_bush(
        a.q(a.f(0)).round().max(5.0) as u32,
        a.f(1),
        a.f(2),
        a.f(3),
        a.u(4).wrapping_add(a.salt),
    ))
}

const STEM_P: &[crate::uikit::procedural::ParamSpec] = &[
    float("height", "Height (m)", 0.05, 4.0, 0.5, 0.01),
    float("radius", "Radius (m)", 0.002, 0.2, 0.015, 0.001),
    float("lean", "Lean", 0.0, 1.0, 0.3, 0.01),
    int("sides", "Sides", 3, 12, 5),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_stem(a: &Args) -> Vec<Value> {
    mesh_out(plant::stem_tube(
        a.f(0),
        a.f(1),
        a.f(2),
        if a.stylized() { 3 } else { a.u(3) },
        a.u(4).wrapping_add(a.salt),
    ))
}

const PETALS_P: &[crate::uikit::procedural::ParamSpec] = &[
    int("count", "Petals", 3, 40, 9),
    float("size", "Size (m)", 0.02, 1.0, 0.22, 0.005),
    float("cup", "Cup", 0.0, 1.0, 0.45, 0.01),
    float("height", "Height (m)", 0.05, 4.0, 0.5, 0.01),
    float("lean", "Lean", 0.0, 1.0, 0.3, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_petals(a: &Args) -> Vec<Value> {
    mesh_out(plant::petals(
        a.u(0),
        a.f(1),
        a.f(2),
        a.f(3),
        a.f(4),
        a.u(5).wrapping_add(a.salt),
    ))
}

// ----------------------------------------------------------------- scatter

const SCATTER_IN: &[PortDef] = &[port("Surface", Kind::Mesh)];
const POINTS_OUT: &[PortDef] = &[port("Points", Kind::Points)];
const SCATTER_P: &[crate::uikit::procedural::ParamSpec] = &[
    int("count", "Points", 1, 3000, 150),
    float("up_min", "Min. up-facing", -1.0, 1.0, 0.3, 0.01),
    float("scale_min", "Scale min", 0.05, 3.0, 0.5, 0.01),
    float("scale_max", "Scale max", 0.05, 3.0, 1.0, 0.01),
    int("seed", "Seed", 0, 9999, 1),
];

fn eval_scatter(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return vec![Value::Points(Vec::new())];
    };
    let triangles: Vec<(V3, V3, V3)> = mesh
        .indices
        .chunks_exact(3)
        .map(|t| {
            (
                mesh.positions[t[0] as usize],
                mesh.positions[t[1] as usize],
                mesh.positions[t[2] as usize],
            )
        })
        .collect();
    let areas: Vec<f32> = triangles
        .iter()
        .map(|(p, q, r)| 0.5 * length(cross(sub(*q, *p), sub(*r, *p))))
        .collect();
    let total: f32 = areas.iter().sum();
    let mut points = Vec::new();
    if total <= 0.0 {
        return vec![Value::Points(points)];
    }
    let wanted = a.q(a.f(0)).round().max(1.0) as usize;
    let mut rng = rng_for(a, a.f(4));
    let (lo, hi) = (a.f(2).min(a.f(3)), a.f(2).max(a.f(3)));
    for _ in 0..wanted * 12 {
        if points.len() >= wanted {
            break;
        }
        let mut pick = rng.next() * total;
        let mut index = 0;
        for (i, area) in areas.iter().enumerate() {
            index = i;
            pick -= area;
            if pick <= 0.0 {
                break;
            }
        }
        let (p, q, r) = triangles[index];
        let normal = normalize(cross(sub(q, p), sub(r, p)));
        if normal[1] < a.f(1) {
            continue;
        }
        let (mut u, mut v) = (rng.next(), rng.next());
        if u + v > 1.0 {
            u = 1.0 - u;
            v = 1.0 - v;
        }
        let position = add(p, add(mul(sub(q, p), u), mul(sub(r, p), v)));
        points.push(PointInst {
            position,
            normal,
            scale: rng.range(lo, hi),
            yaw: rng.range(0.0, std::f32::consts::TAU),
        });
    }
    vec![Value::Points(points)]
}

const INSTANCE_IN: &[PortDef] = &[port("Template", Kind::Mesh), port("Points", Kind::Points)];
const INSTANCE_P: &[crate::uikit::procedural::ParamSpec] = &[
    float("align", "Align to surface", 0.0, 1.0, 0.7, 0.01),
    float("tilt", "Random tilt", 0.0, 1.0, 0.15, 0.01),
    float("sink", "Sink into surface", 0.0, 1.0, 0.15, 0.01),
    int("budget", "Triangle budget", 500, 200000, 40000),
    int("seed", "Seed", 0, 9999, 1),
];

/// Rotates `v` so that +Y becomes `target`.
fn rotate_up_to(v: V3, target: V3) -> V3 {
    let up = [0.0, 1.0, 0.0];
    let axis = cross(up, target);
    let s = length(axis);
    let c = dot(up, target);
    if s < 1e-5 {
        return if c > 0.0 { v } else { [v[0], -v[1], v[2]] };
    }
    let k = mul(axis, 1.0 / s);
    // Rodrigues' rotation formula.
    add(
        add(mul(v, c), mul(cross(k, v), s)),
        mul(k, dot(k, v) * (1.0 - c)),
    )
}

fn eval_instance(a: &Args) -> Vec<Value> {
    let (Some(template), Some(points)) = (a.mesh(0), a.points(1)) else {
        return mesh_out(empty());
    };
    if template.is_empty() {
        return mesh_out(empty());
    }
    let budget = a.u(3) as usize * 3;
    let (lo, hi) = template.bounds();
    let height = (hi[1] - lo[1]).max(1e-4);
    let mut rng = rng_for(a, a.f(4));
    let mut out = Part::new(&template.name, template.material.clone(), template.preview);
    let mut transforms = Vec::new();
    for point in points {
        if out.indices.len() + template.indices.len() > budget {
            break;
        }
        let jitter = add(point.normal, mul(rng.direction(), a.f(1)));
        let dir = normalize(lerp([0.0, 1.0, 0.0], normalize(jitter), a.f(0)));
        let base = out.positions.len() as u32;
        let offset = mul(point.normal, -a.f(2) * height * point.scale);
        let turn = |v: V3| rotate_up_to(rotate_y(v, point.yaw), dir);
        transforms.push(super::mesh::InstanceTransform {
            position: add(point.position, offset),
            euler_degrees: super::mesh::euler_from_columns(
                turn([1.0, 0.0, 0.0]),
                turn([0.0, 1.0, 0.0]),
                turn([0.0, 0.0, 1.0]),
            ),
            scale: point.scale,
        });
        for i in 0..template.positions.len() {
            let local = rotate_y(mul(template.positions[i], point.scale), point.yaw);
            let placed = add(
                rotate_up_to(local, dir),
                add(
                    point.position,
                    mul(point.normal, -a.f(2) * height * point.scale),
                ),
            );
            let normal = rotate_up_to(rotate_y(template.normals[i], point.yaw), dir);
            out.vertex(placed, normal, template.uvs[i]);
        }
        out.indices
            .extend(template.indices.iter().map(|i| i + base));
    }
    // The merged geometry above stays for the preview and export; the copies let the scene
    // placement instance the template instead.
    let mut single = template.clone();
    single.instances = None;
    out.instances = Some(super::mesh::Instances {
        template: Box::new(single),
        transforms,
    });
    mesh_out(out)
}

// ------------------------------------------------------------------ output

const PART_IN: &[PortDef] = &[port("Mesh", Kind::Mesh)];
const MESH_OUT: &[PortDef] = &[port("Mesh", Kind::Mesh)];
const PART_STRINGS: &[StringSpec] = &[StringSpec {
    key: "path",
    label: "Your asset (.mat or .png)",
}];
const MAT_OUT: &[PortDef] = &[port("Material", Kind::Material)];
/// A part is a mesh and its material: the material can be wired into its port or picked on the
/// node (the Texture / Material nodes upstream still keep their layers when "From nodes" is used).
const PART_NODE_IN: &[PortDef] = &[port("Mesh", Kind::Mesh), port("Material", Kind::Material)];
const PART_P: &[crate::uikit::procedural::ParamSpec] = &[
    choice("name", "Part name", PART_NAMES, 0),
    choice("material", "Material", MATERIAL_LABELS, 26),
    float("r", "Colour R", 0.0, 1.0, 0.5, 0.01),
    float("g", "Colour G", 0.0, 1.0, 0.5, 0.01),
    float("b", "Colour B", 0.0, 1.0, 0.5, 0.01),
    float("roughness", "Roughness", 0.0, 1.0, 0.8, 0.01),
];

fn eval_part(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    let mut part = mesh.clone();
    part.name = PART_NAMES[a.u(0).min(PART_NAMES.len() as u32 - 1) as usize].to_owned();
    // The material preferably arrives on its port; otherwise the node's own pick is used
    // ("From nodes" keeps whatever the Texture / Material nodes upstream gave the mesh).
    if let Some(material) = a.material(1) {
        part.material = material.clone();
    } else if !is_from_nodes_choice(a.u(1) as usize) {
        let rgb = [a.f(2), a.f(3), a.f(4)];
        let path = a.strings.first().map(String::as_str).unwrap_or("");
        part.material = material_for(a.u(1) as usize, rgb, a.f(5), path, a.quality);
    }
    part.preview = preview_colour(&part.material, part.preview);
    mesh_out(part)
}

// ------------------------------------------------------------ material nodes

const LIBRARY_P: &[crate::uikit::procedural::ParamSpec] =
    &[choice("preset", "Material", LIBRARY_LABELS, 6)];
fn eval_mat_library(a: &Args) -> Vec<Value> {
    let label = LIBRARY_LABELS[(a.u(0) as usize).min(LIBRARY_LABELS.len() - 1)];
    vec![Value::Material(spec_for_label(label, a.quality))]
}

const CARDS_P: &[crate::uikit::procedural::ParamSpec] =
    &[choice("card", "Texture", CARD_LABELS, 0)];
fn eval_mat_cards(a: &Args) -> Vec<Value> {
    let label = CARD_LABELS[(a.u(0) as usize).min(CARD_LABELS.len() - 1)];
    vec![Value::Material(spec_for_label(label, a.quality))]
}

const SOLID_P: &[crate::uikit::procedural::ParamSpec] = &[
    float("r", "Red", 0.0, 1.0, 0.3, 0.01),
    float("g", "Green", 0.0, 1.0, 0.45, 0.01),
    float("b", "Blue", 0.0, 1.0, 0.2, 0.01),
    float("roughness", "Roughness", 0.0, 1.0, 0.8, 0.01),
];
fn eval_mat_solid(a: &Args) -> Vec<Value> {
    vec![Value::Material(MaterialSpec::Solid {
        rgb: [a.f(0), a.f(1), a.f(2)],
        roughness: a.f(3),
    })]
}

/// Leaf shapes across and down the picture of your material (1 x 1 = one picture).
const ATLAS_P: &[crate::uikit::procedural::ParamSpec] = &[
    float("columns", "Shapes across", 1.0, 8.0, 1.0, 1.0),
    float("rows", "Shapes down", 1.0, 8.0, 1.0, 1.0),
];
fn atlas_args(a: &Args) -> (u32, u32) {
    (
        a.f(0).round().max(1.0) as u32,
        a.f(1).round().max(1.0) as u32,
    )
}

const PROJECT_MAT_STRINGS: &[StringSpec] = &[StringSpec {
    key: "path",
    label: "Your material (.mat)",
}];
const PROJECT_TEX_STRINGS: &[StringSpec] = &[StringSpec {
    key: "path",
    label: "Your image (cut-out PNG)",
}];
/// Your own `.mat`, used exactly as it is. With no path it gives no material (the Part reports it).
fn eval_mat_project(a: &Args) -> Vec<Value> {
    match a
        .strings
        .first()
        .map(String::as_str)
        .filter(|p| !p.is_empty())
    {
        Some(path) => vec![Value::Material(MaterialSpec::Project(
            path.to_owned(),
            atlas_args(a),
        ))],
        None => Vec::new(),
    }
}
fn eval_mat_texture(a: &Args) -> Vec<Value> {
    match a
        .strings
        .first()
        .map(String::as_str)
        .filter(|p| !p.is_empty())
    {
        Some(path) => vec![Value::Material(MaterialSpec::ProjectTexture(
            path.to_owned(),
            atlas_args(a),
        ))],
        None => Vec::new(),
    }
}

const MATERIAL_STRINGS: &[StringSpec] = &[StringSpec {
    key: "base",
    label: "Base material (.mat)",
}];
const MATERIAL_P: &[crate::uikit::procedural::ParamSpec] = &[
    float("r", "Colour R", 0.0, 1.0, 0.5, 0.01),
    float("g", "Colour G", 0.0, 1.0, 0.5, 0.01),
    float("b", "Colour B", 0.0, 1.0, 0.5, 0.01),
    float("roughness", "Roughness", 0.0, 1.0, 0.8, 0.01),
    float("metallic", "Metallic", 0.0, 1.0, 0.0, 0.01),
    choice("alpha", "Transparency", &["Opaque", "Cut-out"], 0),
    float("cutoff", "Cut-out threshold", 0.05, 0.95, 0.45, 0.01),
    float("normal_strength", "Normal strength", 0.0, 3.0, 1.0, 0.01),
    float("translucency", "Translucency", 0.0, 1.0, 0.0, 0.01),
    float("sway", "Wind sway (per m)", 0.0, 0.5, 0.0, 0.001),
    float("flutter", "Leaf flutter", 0.0, 1.0, 0.0, 0.01),
];

/// The custom material a mesh already carries, or a plain one made from its current material.
fn custom_of(part: &Part) -> CustomMaterial {
    match &part.material {
        MaterialSpec::Custom(c) => c.clone(),
        MaterialSpec::Solid { rgb, roughness } => CustomMaterial::plain(*rgb, *roughness),
        _ => CustomMaterial::plain(part.preview, 0.8),
    }
}

/// A **Material node**: the surface settings of the mesh wired in (colour, roughness, metallic,
/// cut-out, translucency, wind). Textures come from **Texture nodes**, before or after it: the
/// layers they added stay. Wire it into a Part that keeps "From nodes".
fn eval_material(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    let mut part = mesh.clone();
    let mut c = custom_of(&part);
    c.base = a.strings.first().cloned().unwrap_or_default();
    c.rgb = [a.f(0), a.f(1), a.f(2)];
    c.roughness = a.f(3);
    c.metallic = a.f(4);
    c.cutout = a.u(5) == 1;
    c.cutoff = a.f(6);
    c.normal_strength = a.f(7);
    c.translucency = a.f(8);
    c.sway = a.f(9);
    c.flutter = a.f(10);
    part.preview = c.rgb;
    part.material = MaterialSpec::Custom(c);
    mesh_out(part)
}

const TEXTURE_STRINGS: &[StringSpec] = &[StringSpec {
    key: "image",
    label: "Image",
}];
const TEXTURE_P: &[crate::uikit::procedural::ParamSpec] = &[
    choice(
        "slot",
        "Map",
        &["Albedo (colour)", "Normal", "Occlusion / Roughness / Metal"],
        0,
    ),
    choice(
        "blend",
        "Blend with the layers below",
        &["Replace", "Add", "Multiply", "Overlay"],
        0,
    ),
    float("strength", "Strength", 0.0, 2.0, 1.0, 0.01),
    float("tile_u", "Repeats U", 0.1, 32.0, 1.0, 0.1),
    float("tile_v", "Repeats V", 0.1, 32.0, 1.0, 0.1),
    float("r", "Tint R", 0.0, 1.0, 1.0, 0.01),
    float("g", "Tint G", 0.0, 1.0, 1.0, 0.01),
    float("b", "Tint B", 0.0, 1.0, 1.0, 0.01),
    choice(
        "mapping",
        "Fit to the object",
        &["Keep mesh UVs", "Fit to object", "Per metre"],
        0,
    ),
    float(
        "uv_scale",
        "Repeats (object / metre)",
        0.05,
        16.0,
        1.0,
        0.05,
    ),
];

/// A **Texture node**: puts an image on the mesh wired in. Several Texture nodes in a row are
/// **summed** (per map, in order, with their blend modes) into one material. *Fit to the object*
/// re-projects the UVs from the part's own size, so the texture adapts to the object: once across
/// it, or a number of repeats per metre.
fn eval_texture(a: &Args) -> Vec<Value> {
    let Some(mesh) = a.mesh(0) else {
        return mesh_out(empty());
    };
    let mut part = match a.u(8) {
        1 => {
            let (lo, hi) = mesh.bounds();
            let size = (hi[0] - lo[0])
                .max(hi[1] - lo[1])
                .max(hi[2] - lo[2])
                .max(1e-3);
            rock::box_uv_unweld(mesh, a.f(9) / size)
        }
        2 => rock::box_uv_unweld(mesh, a.f(9)),
        _ => mesh.clone(),
    };
    let mut c = custom_of(&part);
    let path = a.strings.first().cloned().unwrap_or_default();
    if !path.is_empty() {
        c.layers.push(TextureLayer {
            slot: TextureSlot::ALL[a.u(0).min(2) as usize],
            path,
            blend: LayerBlend::ALL[a.u(1).min(3) as usize],
            strength: a.f(2),
            tiling: [a.f(3), a.f(4)],
            tint: [a.f(5), a.f(6), a.f(7)],
        });
    }
    part.material = MaterialSpec::Custom(c);
    mesh_out(part)
}

const OUTPUT_IN: &[PortDef] = &[
    port("Part 1", Kind::Mesh),
    port("Part 2", Kind::Mesh),
    port("Part 3", Kind::Mesh),
    port("Part 4", Kind::Mesh),
    port("Part 5", Kind::Mesh),
    port("Part 6", Kind::Mesh),
];

fn eval_output(_: &Args) -> Vec<Value> {
    Vec::new()
}

// ------------------------------------------------------------------ registry

const NONE: &[PortDef] = &[];
const SKEL_IN: &[PortDef] = &[port("Skeleton", Kind::Skeleton)];
const SKEL_OUT: &[PortDef] = &[port("Skeleton", Kind::Skeleton)];
const NO_STRINGS: &[StringSpec] = &[];

macro_rules! node {
    ($id:expr, $title:expr, $cat:expr, $in:expr, $out:expr, $params:expr, $eval:expr) => {
        NodeDef {
            id: $id,
            title: $title,
            category: $cat,
            inputs: $in,
            outputs: $out,
            params: $params,
            strings: NO_STRINGS,
            eval: $eval,
        }
    };
}

pub const CATEGORIES: [&str; 9] = [
    "Materials",
    "Surface",
    "Shapes",
    "Modifiers",
    "Trees",
    "Plants",
    "Scatter",
    "Output",
    "Combine",
];

pub static NODES: &[NodeDef] = &[
    node!(
        "icosphere",
        "Icosphere",
        "Shapes",
        NONE,
        MESH_OUT,
        ICO_P,
        eval_icosphere
    ),
    node!(
        "ellipsoid",
        "Ellipsoid",
        "Shapes",
        NONE,
        MESH_OUT,
        ELLIPSOID_P,
        eval_ellipsoid
    ),
    node!(
        "noise_displace",
        "Noise Displace",
        "Modifiers",
        PART_IN,
        MESH_OUT,
        DISPLACE_P,
        eval_displace
    ),
    node!(
        "surface",
        "Rock Surface",
        "Surface",
        PART_IN,
        MESH_OUT,
        SURFACE_P,
        eval_surface
    ),
    node!(
        "cut_planes",
        "Cut Planes",
        "Modifiers",
        PART_IN,
        MESH_OUT,
        CUT_P,
        eval_cut
    ),
    node!(
        "flat_base",
        "Flat Base",
        "Modifiers",
        PART_IN,
        MESH_OUT,
        FLAT_P,
        eval_flat_base
    ),
    node!(
        "transform",
        "Transform",
        "Modifiers",
        PART_IN,
        MESH_OUT,
        TRANSFORM_P,
        eval_transform
    ),
    node!(
        "ground",
        "Place on Ground",
        "Modifiers",
        PART_IN,
        MESH_OUT,
        &[],
        eval_ground
    ),
    node!(
        "subdivide",
        "Subdivide",
        "Modifiers",
        PART_IN,
        MESH_OUT,
        SUBDIV_P,
        eval_subdivide
    ),
    node!(
        "shade",
        "Shade",
        "Modifiers",
        PART_IN,
        MESH_OUT,
        SHADE_P,
        eval_shade
    ),
    node!(
        "uv_box",
        "UV Box Projection",
        "Modifiers",
        PART_IN,
        MESH_OUT,
        UVBOX_P,
        eval_uv_box
    ),
    node!(
        "merge",
        "Merge",
        "Combine",
        MERGE_IN,
        MESH_OUT,
        &[],
        eval_merge
    ),
    node!(
        "grow",
        "Grow Branches",
        "Trees",
        NONE,
        SKEL_OUT,
        GROW_P,
        eval_grow
    ),
    node!("roots", "Roots", "Trees", SKEL_IN, SKEL_OUT, ROOTS_P, eval_roots),
    node!(
        "tubes",
        "Branch Tubes",
        "Trees",
        SKEL_IN,
        MESH_OUT,
        TUBES_P,
        eval_tubes
    ),
    node!(
        "leaves",
        "Leaves",
        "Trees",
        LEAVES_IN,
        MESH_OUT,
        LEAVES_P,
        eval_leaves
    ),
    node!(
        "blades",
        "Grass Blades",
        "Plants",
        NONE,
        MESH_OUT,
        BLADES_P,
        eval_blades
    ),
    node!(
        "fronds",
        "Fern Fronds",
        "Plants",
        NONE,
        MESH_OUT,
        FRONDS_P,
        eval_fronds
    ),
    node!(
        "bush",
        "Leafy Bush",
        "Plants",
        NONE,
        MESH_OUT,
        BUSH_P,
        eval_bush
    ),
    node!("stem", "Stem", "Plants", NONE, MESH_OUT, STEM_P, eval_stem),
    node!(
        "petals",
        "Petals",
        "Plants",
        NONE,
        MESH_OUT,
        PETALS_P,
        eval_petals
    ),
    node!(
        "scatter",
        "Scatter Points",
        "Scatter",
        SCATTER_IN,
        POINTS_OUT,
        SCATTER_P,
        eval_scatter
    ),
    node!(
        "instance",
        "Instance on Points",
        "Scatter",
        INSTANCE_IN,
        MESH_OUT,
        INSTANCE_P,
        eval_instance
    ),
    NodeDef {
        id: "material",
        title: "Material",
        category: "Output",
        inputs: PART_IN,
        outputs: MESH_OUT,
        params: MATERIAL_P,
        strings: MATERIAL_STRINGS,
        eval: eval_material,
    },
    NodeDef {
        id: "texture",
        title: "Texture",
        category: "Output",
        inputs: PART_IN,
        outputs: MESH_OUT,
        params: TEXTURE_P,
        strings: TEXTURE_STRINGS,
        eval: eval_texture,
    },
    NodeDef {
        id: "part",
        title: "Part",
        category: "Output",
        inputs: PART_NODE_IN,
        outputs: MESH_OUT,
        params: PART_P,
        strings: PART_STRINGS,
        eval: eval_part,
    },
    node!(
        "mat_library",
        "Library Material",
        "Materials",
        NONE,
        MAT_OUT,
        LIBRARY_P,
        eval_mat_library
    ),
    node!(
        "mat_cards",
        "Leaf / Petal Texture",
        "Materials",
        NONE,
        MAT_OUT,
        CARDS_P,
        eval_mat_cards
    ),
    node!(
        "mat_solid",
        "Solid Colour",
        "Materials",
        NONE,
        MAT_OUT,
        SOLID_P,
        eval_mat_solid
    ),
    NodeDef {
        id: "mat_project",
        title: "Your Material (.mat)",
        category: "Materials",
        inputs: NONE,
        outputs: MAT_OUT,
        params: ATLAS_P,
        strings: PROJECT_MAT_STRINGS,
        eval: eval_mat_project,
    },
    NodeDef {
        id: "mat_texture",
        title: "Your Texture (PNG)",
        category: "Materials",
        inputs: NONE,
        outputs: MAT_OUT,
        params: ATLAS_P,
        strings: PROJECT_TEX_STRINGS,
        eval: eval_mat_texture,
    },
    node!(
        "output",
        "Output",
        "Output",
        OUTPUT_IN,
        NONE,
        &[],
        eval_output
    ),
];

pub fn find(id: &str) -> Option<&'static NodeDef> {
    NODES.iter().find(|n| n.id == id)
}

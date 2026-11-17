//! The **Texture Generator** model: graphs that produce one texture (a leaf, a flame, a
//! packed VFX mask...) instead of a PBR material.
//!
//! It deliberately lives apart from the Material Generator:
//!
//! * its own file type (`.texgraph`, header `CAFTEXGRAPH1`), which the material loader refuses
//!   and vice versa;
//! * its own result node, [`NodeKind::TextureOutput`], and its own palette and presets;
//! * its own export (a single PNG, 8 or 16 bit, with alpha) and flipbook baking.
//!
//! The node store, the evaluator and the 133 procedural ops are shared with the material tool
//! (`generators::MaterialGraph` is only the container of nodes and links), so every improvement
//! to the library benefits both without duplicating any code.

use iced::Point;
use std::path::Path;

use super::generators::{
    BakedTexture, GeneratorNode, MaterialGraph, NodeId, NodeKind, TextureOutputMode,
};
use super::procedural::{self, ProcOp};

/// First line of a saved texture graph.
pub const FILE_HEADER: &str = "CAFTEXGRAPH1";
/// File extension of a saved texture graph.
pub const EXTENSION: &str = "texgraph";
/// Category shown first in the Texture Generator palette.
pub const VFX_CATEGORY: &str = "VFX & Shapes";

// ------------------------------------------------------------------- graph

/// A new graph: a leaf feeding a Texture Output.
pub fn new_graph() -> MaterialGraph {
    let mut graph = empty_graph();
    let leaf = add_op(&mut graph, "leaf", &[], &[], Point::new(-200.0, -80.0));
    let out = graph.add_node(
        NodeKind::TextureOutput {
            mode: TextureOutputMode::ColorAlpha,
        },
        Point::new(120.0, -60.0),
    );
    graph.connect(leaf, 0, out, 0);
    graph
}

pub fn empty_graph() -> MaterialGraph {
    MaterialGraph {
        nodes: Vec::new(),
        links: Vec::new(),
        next_id: 1,
        resolution: 512,
    }
}

/// True when the graph can be baked as a texture.
pub fn is_texture_graph(graph: &MaterialGraph) -> bool {
    graph.texture_output_node().is_some()
}

/// Adds a library op with parameter and colour overrides (by key / index).
pub fn add_op(
    graph: &mut MaterialGraph,
    id: &str,
    params: &[(&str, f32)],
    colors: &[(usize, [f32; 4])],
    position: Point,
) -> NodeId {
    let op = procedural::find(id).unwrap_or_else(|| panic!("unknown op {id}"));
    let NodeKind::Proc {
        op,
        params: mut values,
        colors: mut palette,
    } = NodeKind::proc_default(op)
    else {
        unreachable!("proc_default returns Proc");
    };
    for (key, value) in params {
        let index =
            op.0.params
                .iter()
                .position(|spec| spec.key == *key)
                .unwrap_or_else(|| panic!("op {id} has no parameter {key}"));
        values[index] = *value;
    }
    for (index, color) in colors {
        palette[*index] = *color;
    }
    graph.add_node(
        NodeKind::Proc {
            op,
            params: values,
            colors: palette,
        },
        position,
    )
}

// -------------------------------------------------------------------- file

pub fn to_text(graph: &MaterialGraph) -> String {
    let body = graph.to_text();
    let rest = body.split_once('\n').map_or("", |(_, rest)| rest);
    format!("{FILE_HEADER}\n{rest}")
}

/// Parses a saved texture graph. Material graphs and files without a Texture Output are refused.
pub fn from_text(text: &str) -> Option<MaterialGraph> {
    let (header, rest) = text.split_once('\n')?;
    if header.trim() != FILE_HEADER {
        return None;
    }
    let graph = MaterialGraph::from_text(&format!("CAFMGRAPH2\n{rest}"))?;
    is_texture_graph(&graph).then_some(graph)
}

pub fn save(graph: &MaterialGraph, path: &Path) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent).map_err(|error| error.to_string())?;
    }
    std::fs::write(path, to_text(graph)).map_err(|error| error.to_string())
}

pub fn load(path: &Path) -> Result<MaterialGraph, String> {
    let text = std::fs::read_to_string(path).map_err(|error| error.to_string())?;
    from_text(&text).ok_or_else(|| {
        "Not a texture graph (a material graph cannot be opened as a texture)".to_owned()
    })
}

// ----------------------------------------------------------------- palette

/// Nodes offered by the Texture Generator: the VFX & Shapes ops first, then every
/// resolution-independent node. Material-only nodes (the PBR Output, image and normal-map
/// readers, imported nodes) are left out.
pub fn palette() -> Vec<NodeKind> {
    let mut vfx = Vec::new();
    let mut rest = Vec::new();
    for kind in super::generators::palette() {
        match &kind {
            NodeKind::Output { .. }
            | NodeKind::Image { .. }
            | NodeKind::NormalMap { .. }
            | NodeKind::NormalBlend { .. }
            | NodeKind::Unknown { .. } => {}
            _ => rest.push(kind),
        }
    }
    for def in procedural::all()
        .iter()
        .filter(|def| def.category == VFX_CATEGORY)
    {
        vfx.push(NodeKind::proc_default(ProcOp(def)));
    }
    vfx.extend(rest);
    vfx
}

// ------------------------------------------------------------------ export

/// Output formats of a texture export.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BitDepth {
    Eight,
    Sixteen,
}

/// Writes the baked texture as a single PNG (straight alpha).
pub fn export_png(baked: &BakedTexture, path: &Path, depth: BitDepth) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent).map_err(|error| error.to_string())?;
    }
    // Flipbook sheets are `columns * size` wide; the height follows from the buffer length.
    let width = baked.resolution;
    let height = (baked.rgba8.len() as u32 / 4) / width.max(1);
    match depth {
        BitDepth::Eight => {
            let image = image::RgbaImage::from_raw(width, height, baked.rgba8.clone())
                .ok_or("texture buffer has the wrong size")?;
            image.save(path).map_err(|error| error.to_string())
        }
        BitDepth::Sixteen => {
            let image: image::ImageBuffer<image::Rgba<u16>, Vec<u16>> =
                image::ImageBuffer::from_raw(width, height, baked.rgba16.clone())
                    .ok_or("texture buffer has the wrong size")?;
            image.save(path).map_err(|error| error.to_string())
        }
    }
}

/// Bakes `frames` copies of the graph, advancing every `phase` parameter from 0 to just under 1,
/// and lays them out row by row in a `columns`-wide sheet. Each frame is `graph.resolution`
/// pixels square. Graphs without a phase parameter produce identical frames.
pub fn bake_flipbook(
    graph: &MaterialGraph,
    columns: u32,
    rows: u32,
) -> Result<BakedTexture, String> {
    let columns = columns.clamp(1, 16);
    let rows = rows.clamp(1, 16);
    let frames = columns * rows;
    let textures = std::collections::HashMap::new();
    let mut sheet: Option<BakedTexture> = None;
    for frame in 0..frames {
        let mut step = graph.clone();
        let phase = frame as f32 / frames as f32;
        for node in step.nodes.iter_mut() {
            if let NodeKind::Proc { op, params, .. } = &mut node.kind {
                if let Some(index) = op.0.params.iter().position(|spec| spec.key == "phase") {
                    params[index] = phase;
                }
            }
        }
        let baked = step.bake_texture_with(&textures, None, None)?;
        let size = baked.resolution as usize;
        let target = sheet.get_or_insert_with(|| BakedTexture {
            resolution: baked.resolution * columns,
            rgba8: Vec::new(),
            rgba16: Vec::new(),
            has_alpha: false,
            mode: baked.mode,
        });
        let sheet_w = size * columns as usize;
        if target.rgba8.is_empty() {
            target.rgba8 = vec![0; sheet_w * size * rows as usize * 4];
            target.rgba16 = vec![0; sheet_w * size * rows as usize * 4];
        }
        let (cx, cy) = ((frame % columns) as usize, (frame / columns) as usize);
        for y in 0..size {
            let src = y * size * 4;
            let dst = ((cy * size + y) * sheet_w + cx * size) * 4;
            target.rgba8[dst..dst + size * 4].copy_from_slice(&baked.rgba8[src..src + size * 4]);
            target.rgba16[dst..dst + size * 4].copy_from_slice(&baked.rgba16[src..src + size * 4]);
        }
        target.has_alpha |= baked.has_alpha;
    }
    let mut sheet = sheet.ok_or("empty flipbook")?;
    // The sheet is `columns * size` wide and `rows * size` tall; keep the width in
    // `resolution` and let callers derive the height from the buffer length.
    sheet.resolution = sheet.resolution.max(1);
    Ok(sheet)
}

// ----------------------------------------------------------------- presets

/// A ready-made texture graph.
pub struct TexturePreset {
    pub name: &'static str,
    pub category: &'static str,
    pub description: &'static str,
    build: fn() -> MaterialGraph,
}

impl TexturePreset {
    pub fn build(&self) -> MaterialGraph {
        (self.build)()
    }
}

fn finish(
    mut graph: MaterialGraph,
    from: NodeId,
    output: usize,
    alpha: Option<(NodeId, usize)>,
) -> MaterialGraph {
    let out = graph.add_node(
        NodeKind::TextureOutput {
            mode: TextureOutputMode::ColorAlpha,
        },
        Point::new(600.0, -40.0),
    );
    graph.connect(from, output, out, 0);
    if let Some((node, port)) = alpha {
        graph.connect(node, port, out, 1);
    }
    graph
}

fn single(id: &str, params: &[(&str, f32)], colors: &[(usize, [f32; 4])]) -> MaterialGraph {
    let mut graph = empty_graph();
    let node = add_op(&mut graph, id, params, colors, Point::new(100.0, -60.0));
    finish(graph, node, 0, None)
}

fn green_leaf() -> MaterialGraph {
    single("leaf", &[], &[])
}
fn autumn_leaf() -> MaterialGraph {
    single(
        "leaf",
        &[
            ("width", 0.27),
            ("serration", 0.55),
            ("teeth", 18.0),
            ("bend", -0.35),
            ("veins", 11.0),
            ("seed", 7.0),
        ],
        &[
            (0, [0.62, 0.18, 0.04, 1.0]),
            (1, [0.80, 0.42, 0.06, 1.0]),
            (2, [0.95, 0.75, 0.25, 1.0]),
        ],
    )
}
fn narrow_leaf() -> MaterialGraph {
    single(
        "leaf",
        &[
            ("width", 0.16),
            ("tip", 0.9),
            ("base", 0.2),
            ("veins", 14.0),
            ("sweep", 0.7),
            ("bend", 0.5),
            ("seed", 3.0),
        ],
        &[
            (0, [0.38, 0.52, 0.14, 1.0]),
            (1, [0.14, 0.34, 0.07, 1.0]),
            (2, [0.7, 0.8, 0.4, 1.0]),
        ],
    )
}
fn petal(edge: [f32; 4], body: [f32; 4], vein: [f32; 4]) -> MaterialGraph {
    single(
        "leaf",
        &[
            ("width", 0.36),
            ("tip", 0.15),
            ("base", 0.55),
            ("veins", 7.0),
            ("vein_width", 0.05),
            ("midrib", 0.006),
            ("sweep", 0.3),
            ("bend", 0.0),
            ("texture", 0.25),
            ("seed", 2.0),
        ],
        &[(0, edge), (1, body), (2, vein)],
    )
}
fn petal_pink() -> MaterialGraph {
    petal(
        [0.98, 0.62, 0.74, 1.0],
        [0.92, 0.36, 0.55, 1.0],
        [0.99, 0.78, 0.85, 1.0],
    )
}
fn petal_white() -> MaterialGraph {
    petal(
        [0.97, 0.97, 0.95, 1.0],
        [0.92, 0.92, 0.90, 1.0],
        [1.0, 0.98, 0.7, 1.0],
    )
}
fn petal_yellow() -> MaterialGraph {
    petal(
        [1.0, 0.86, 0.35, 1.0],
        [0.97, 0.72, 0.12, 1.0],
        [1.0, 0.93, 0.55, 1.0],
    )
}
fn blue_feather() -> MaterialGraph {
    single("feather", &[], &[])
}
fn white_feather() -> MaterialGraph {
    single(
        "feather",
        &[
            ("width", 0.24),
            ("barbs", 120.0),
            ("split", 0.6),
            ("bend", -0.3),
            ("seed", 5.0),
        ],
        &[
            (0, [0.98, 0.97, 0.94, 1.0]),
            (1, [0.86, 0.86, 0.9, 1.0]),
            (2, [0.95, 0.93, 0.88, 1.0]),
        ],
    )
}
fn droplet() -> MaterialGraph {
    single("droplet", &[], &[])
}
fn torch_flame() -> MaterialGraph {
    single("flame", &[], &[])
}
fn blue_flame() -> MaterialGraph {
    single(
        "flame",
        &[
            ("width", 0.18),
            ("height", 0.7),
            ("turbulence", 0.45),
            ("seed", 4.0),
        ],
        &[
            (0, [0.05, 0.12, 0.6, 1.0]),
            (1, [0.2, 0.5, 1.0, 1.0]),
            (2, [0.85, 0.95, 1.0, 1.0]),
        ],
    )
}
fn smoke_puff() -> MaterialGraph {
    single("smoke_puff", &[], &[])
}
fn dark_smoke() -> MaterialGraph {
    single(
        "smoke_puff",
        &[
            ("radius", 0.4),
            ("turbulence", 0.9),
            ("density", 1.2),
            ("seed", 9.0),
        ],
        &[(0, [0.02, 0.02, 0.02, 1.0]), (1, [0.35, 0.34, 0.33, 1.0])],
    )
}
fn spark_shower() -> MaterialGraph {
    single("sparks", &[], &[])
}
fn shock_ring() -> MaterialGraph {
    single("shockwave", &[], &[])
}
fn soft_glow() -> MaterialGraph {
    single(
        "soft_glow",
        &[("radius", 0.42), ("power", 1.6)],
        &[(0, [0.35, 0.6, 1.0, 1.0]), (1, [0.95, 0.98, 1.0, 1.0])],
    )
}
fn wfc_pattern() -> MaterialGraph {
    single("variation_wfc", &[("cells", 32.0), ("softness", 0.15)], &[])
}
fn diamond_square_clouds() -> MaterialGraph {
    single(
        "variation_diamond_square",
        &[("exponent", 7.0), ("roughness", 0.7), ("contrast", 1.5)],
        &[(0, [0.30, 0.42, 0.62, 1.0]), (1, [0.96, 0.97, 1.0, 1.0])],
    )
}
fn fire_ramp() -> MaterialGraph {
    single("color_ramp", &[], &[])
}

/// Burst + ring + glow, summed and mapped through a fire gradient.
fn explosion() -> MaterialGraph {
    let mut graph = empty_graph();
    let burst = add_op(
        &mut graph,
        "radial_burst",
        &[("rays", 28.0)],
        &[],
        Point::new(-260.0, -220.0),
    );
    let ring = add_op(
        &mut graph,
        "shockwave",
        &[("radius", 0.36)],
        &[],
        Point::new(-260.0, -20.0),
    );
    let glow = add_op(
        &mut graph,
        "soft_glow",
        &[("radius", 0.3)],
        &[],
        Point::new(-260.0, 180.0),
    );
    let sum_a = add_op(&mut graph, "math_add", &[], &[], Point::new(-20.0, -120.0));
    let sum_b = add_op(&mut graph, "math_add", &[], &[], Point::new(180.0, 20.0));
    graph.connect(burst, 1, sum_a, 0);
    graph.connect(ring, 1, sum_a, 1);
    graph.connect(sum_a, 0, sum_b, 0);
    graph.connect(glow, 1, sum_b, 1);
    let map = add_op(
        &mut graph,
        "gradient_map",
        &[],
        &[
            (0, [0.0, 0.0, 0.0, 1.0]),
            (1, [0.45, 0.04, 0.0, 1.0]),
            (2, [1.0, 0.35, 0.03, 1.0]),
            (3, [1.0, 0.85, 0.35, 1.0]),
            (4, [1.0, 1.0, 0.9, 1.0]),
        ],
        Point::new(380.0, 20.0),
    );
    graph.connect(sum_b, 0, map, 0);
    finish(graph, map, 0, Some((sum_b, 0)))
}

/// Four different masks packed into R, G, B and A (the usual VFX data texture).
fn packed_vfx_masks() -> MaterialGraph {
    let mut graph = empty_graph();
    let smoke = add_op(
        &mut graph,
        "smoke_puff",
        &[],
        &[],
        Point::new(-260.0, -260.0),
    );
    let burst = add_op(
        &mut graph,
        "radial_burst",
        &[],
        &[],
        Point::new(-260.0, -80.0),
    );
    let glow = add_op(&mut graph, "soft_glow", &[], &[], Point::new(-260.0, 100.0));
    let flame = add_op(&mut graph, "flame", &[], &[], Point::new(-260.0, 280.0));
    let out = graph.add_node(
        NodeKind::TextureOutput {
            mode: TextureOutputMode::Channels,
        },
        Point::new(140.0, 0.0),
    );
    graph.connect(smoke, 1, out, 0);
    graph.connect(burst, 1, out, 1);
    graph.connect(glow, 1, out, 2);
    graph.connect(flame, 1, out, 3);
    graph
}

const PRESETS: &[TexturePreset] = &[
    TexturePreset {
        name: "Green Leaf",
        category: "Foliage",
        description: "Broad leaf with midrib and curved side veins.",
        build: green_leaf,
    },
    TexturePreset {
        name: "Autumn Leaf",
        category: "Foliage",
        description: "Serrated, bent leaf in autumn colours.",
        build: autumn_leaf,
    },
    TexturePreset {
        name: "Willow Leaf",
        category: "Foliage",
        description: "Long narrow leaf with a sharp tip.",
        build: narrow_leaf,
    },
    TexturePreset {
        name: "Petal Pink",
        category: "Foliage",
        description: "A rounded flower petal; used by the Nature Generator's flowers.",
        build: petal_pink,
    },
    TexturePreset {
        name: "Petal White",
        category: "Foliage",
        description: "A white daisy petal.",
        build: petal_white,
    },
    TexturePreset {
        name: "Petal Yellow",
        category: "Foliage",
        description: "A yellow petal.",
        build: petal_yellow,
    },
    TexturePreset {
        name: "Blue Feather",
        category: "Organic",
        description: "Barbed feather with split edges and a pale shaft.",
        build: blue_feather,
    },
    TexturePreset {
        name: "Down Feather",
        category: "Organic",
        description: "Soft white feather.",
        build: white_feather,
    },
    TexturePreset {
        name: "Water Droplet",
        category: "Organic",
        description: "Glassy droplet with a window reflection.",
        build: droplet,
    },
    TexturePreset {
        name: "Torch Flame",
        category: "Fire & Smoke",
        description: "Orange flame; Phase loops for flipbooks.",
        build: torch_flame,
    },
    TexturePreset {
        name: "Blue Flame",
        category: "Fire & Smoke",
        description: "Gas-burner flame.",
        build: blue_flame,
    },
    TexturePreset {
        name: "Smoke Puff",
        category: "Fire & Smoke",
        description: "Soft light smoke sprite; Phase loops.",
        build: smoke_puff,
    },
    TexturePreset {
        name: "Dark Smoke",
        category: "Fire & Smoke",
        description: "Heavy black smoke.",
        build: dark_smoke,
    },
    TexturePreset {
        name: "Spark Shower",
        category: "Effects",
        description: "Streaking embers radiating from the centre.",
        build: spark_shower,
    },
    TexturePreset {
        name: "Shock Ring",
        category: "Effects",
        description: "Distorted expanding ring.",
        build: shock_ring,
    },
    TexturePreset {
        name: "Magic Glow",
        category: "Effects",
        description: "Soft blue energy glow.",
        build: soft_glow,
    },
    TexturePreset {
        name: "Explosion Burst",
        category: "Effects",
        description: "Rays, shock ring and core glow through a fire gradient.",
        build: explosion,
    },
    TexturePreset {
        name: "Wave Function Collapse Bricks",
        category: "Procedural",
        description:
            "A brick wall rebuilt by Wave Function Collapse: a new arrangement for every seed.",
        build: wfc_pattern,
    },
    TexturePreset {
        name: "Diamond-Square Clouds",
        category: "Procedural",
        description: "Fractal clouds from the Diamond-Square algorithm; tiles seamlessly.",
        build: diamond_square_clouds,
    },
    TexturePreset {
        name: "Fire Colour Ramp",
        category: "Data",
        description: "1D gradient strip used as a colour lookup table.",
        build: fire_ramp,
    },
    TexturePreset {
        name: "Packed VFX Masks",
        category: "Data",
        description: "Smoke, burst, glow and flame packed into R, G, B and A.",
        build: packed_vfx_masks,
    },
];

pub fn presets() -> &'static [TexturePreset] {
    PRESETS
}

/// Node ids of a graph, for tests and tools.
pub fn node_ids(graph: &MaterialGraph) -> Vec<NodeId> {
    graph
        .nodes
        .iter()
        .map(|node: &GeneratorNode| node.id)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bake(graph: &MaterialGraph, size: u32) -> BakedTexture {
        let mut graph = graph.clone();
        graph.resolution = size;
        graph
            .bake_texture_with(&std::collections::HashMap::new(), None, None)
            .expect("bake")
    }

    #[test]
    fn every_preset_bakes_a_real_image_with_a_transparent_margin() {
        assert!(presets().len() >= 14);
        for preset in presets() {
            let graph = preset.build();
            assert!(is_texture_graph(&graph), "{}", preset.name);
            let baked = bake(&graph, 128);
            assert_eq!(baked.rgba8.len(), 128 * 128 * 4, "{}", preset.name);
            let lit = baked
                .rgba8
                .chunks(4)
                .filter(|p| p[3] > 8 || p[0] > 8)
                .count();
            assert!(lit > 128, "{} is empty", preset.name);
            if !matches!(preset.category, "Data" | "Procedural") {
                assert!(baked.has_alpha, "{} has no transparency", preset.name);
            }
        }
    }

    #[test]
    fn packed_channels_keep_each_mask_in_its_own_channel() {
        let graph = packed_vfx_masks();
        let baked = bake(&graph, 128);
        let mean = |channel: usize| {
            baked
                .rgba8
                .chunks(4)
                .map(|p| p[channel] as f32)
                .sum::<f32>()
                / (128.0 * 128.0)
        };
        assert!(mean(0) > 1.0 && mean(1) > 1.0 && mean(2) > 1.0 && mean(3) > 1.0);
        let r: Vec<u8> = baked.rgba8.chunks(4).map(|p| p[0]).collect();
        let g: Vec<u8> = baked.rgba8.chunks(4).map(|p| p[1]).collect();
        assert_ne!(r, g, "R and G must hold different masks");
    }

    #[test]
    fn texture_graphs_round_trip_and_stay_apart_from_material_graphs() {
        for preset in presets() {
            let graph = preset.build();
            let back = from_text(&to_text(&graph)).expect(preset.name);
            assert_eq!(graph, back, "{}", preset.name);
        }
        let texture = to_text(&new_graph());
        assert!(texture.starts_with(FILE_HEADER));
        assert!(
            MaterialGraph::from_text(&texture).is_none(),
            "material loader must refuse it"
        );
        assert!(
            from_text(&MaterialGraph::default().to_text()).is_none(),
            "texture loader must refuse a material"
        );
    }

    #[test]
    fn palettes_are_separate() {
        let texture = palette();
        assert!(texture.iter().any(|k| k.category() == VFX_CATEGORY));
        assert!(!texture
            .iter()
            .any(|k| matches!(k, NodeKind::Output { .. } | NodeKind::Image { .. })));
        let material = crate::uikit::generators::palette();
        assert!(!material.iter().any(|k| k.category() == VFX_CATEGORY));
        assert!(!material
            .iter()
            .any(|k| matches!(k, NodeKind::TextureOutput { .. })));
    }

    #[test]
    fn the_material_baker_ignores_a_texture_output_and_vice_versa() {
        assert!(new_graph().bake().is_err());
        assert!(MaterialGraph::default()
            .bake_texture_with(&std::collections::HashMap::new(), None, None)
            .is_err());
    }

    #[test]
    fn exports_8_and_16_bit_pngs_with_alpha() {
        let baked = bake(&new_graph(), 64);
        let dir = std::env::temp_dir().join(format!("doppio-texgen-{}", std::process::id()));
        for (depth, name) in [(BitDepth::Eight, "a8.png"), (BitDepth::Sixteen, "a16.png")] {
            let path = dir.join(name);
            export_png(&baked, &path, depth).expect("export");
            let image = image::open(&path).expect("reopen");
            assert_eq!(image.width(), 64);
            assert!(image.color().has_alpha());
            assert_eq!(
                image.color().bits_per_pixel(),
                if depth == BitDepth::Eight { 32 } else { 64 }
            );
        }
        let _ = std::fs::remove_dir_all(dir);
    }

    #[test]
    fn flipbooks_loop_a_phase_parameter_across_frames() {
        let mut graph = torch_flame();
        graph.resolution = 64;
        let sheet = bake_flipbook(&graph, 2, 2).expect("flipbook");
        assert_eq!(sheet.resolution, 128);
        assert_eq!(sheet.rgba8.len(), 128 * 128 * 4);
        let frame = |cx: usize, cy: usize| -> Vec<u8> {
            let mut out = Vec::new();
            for y in 0..64 {
                let start = ((cy * 64 + y) * 128 + cx * 64) * 4;
                out.extend_from_slice(&sheet.rgba8[start..start + 64 * 4]);
            }
            out
        };
        assert_ne!(frame(0, 0), frame(1, 0), "frames must animate");
    }
}

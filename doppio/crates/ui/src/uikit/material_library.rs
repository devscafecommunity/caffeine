//! Parameterisable material presets built from the procedural node library.
//!
//! Every preset is a *recipe*: a base layer (pattern / noise, providing colour
//! and relief), a detail layer (grunge / noise, providing albedo variation,
//! roughness variation and micro relief) and a few PBR scalars. `build` turns
//! a recipe into a regular [`MaterialGraph`] so the user can keep editing it.
//! All ops involved are tileable, so every preset tiles seamlessly.

use super::generators::{MaterialGraph, NodeId, NodeKind, OutputOptions};
use super::procedural;
use iced::Point;

/// Knobs shared by every preset.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PresetParams {
    /// Added to every `seed` parameter in the recipe.
    pub seed: i32,
    /// Multiplies the density parameters (`scale`, `count`, `cells`, ...).
    pub scale: f32,
    /// Scales the amount of detail / relief (0 = clean, 2 = very worn).
    pub wear: f32,
}

impl Default for PresetParams {
    fn default() -> Self {
        Self {
            seed: 0,
            scale: 1.0,
            wear: 1.0,
        }
    }
}

type Overrides = &'static [(&'static str, f32)];
type Colors = &'static [[f32; 4]];

#[derive(Debug, Clone, Copy)]
struct Layer {
    op: &'static str,
    params: Overrides,
    colors: Colors,
}

#[derive(Debug, Clone, Copy)]
pub struct Preset {
    pub name: &'static str,
    pub category: &'static str,
    pub description: &'static str,
    base: Layer,
    detail: Layer,
    /// How strongly the detail darkens / varies the albedo (0..1).
    detail_mix: f32,
    /// Share of the detail layer in the height (0..1).
    relief: f32,
    roughness: (f32, f32),
    metallic: f32,
}

const fn c(r: f32, g: f32, b: f32) -> [f32; 4] {
    [r, g, b, 1.0]
}

const GRIME: Layer = Layer {
    op: "grunge",
    params: &[("scale", 6.0)],
    colors: &[c(0.45, 0.43, 0.40), c(1.0, 1.0, 1.0)],
};
const FINE: Layer = Layer {
    op: "perlin_fbm",
    params: &[("scale", 14.0), ("octaves", 5.0)],
    colors: &[c(0.62, 0.62, 0.62), c(1.0, 1.0, 1.0)],
};
const COARSE: Layer = Layer {
    op: "perlin_fbm",
    params: &[("scale", 4.0), ("octaves", 6.0)],
    colors: &[c(0.5, 0.5, 0.5), c(1.0, 1.0, 1.0)],
};
const DIRT: Layer = Layer {
    op: "dirt",
    params: &[("scale", 5.0)],
    colors: &[c(0.38, 0.33, 0.28), c(1.0, 1.0, 1.0)],
};
const SCRATCHED: Layer = Layer {
    op: "scratches",
    params: &[],
    colors: &[c(0.55, 0.55, 0.55), c(1.0, 1.0, 1.0)],
};

const fn layer(op: &'static str, params: Overrides, colors: Colors) -> Layer {
    Layer { op, params, colors }
}

#[allow(clippy::too_many_arguments)]
const fn preset(
    name: &'static str,
    category: &'static str,
    description: &'static str,
    base: Layer,
    detail: Layer,
    detail_mix: f32,
    relief: f32,
    roughness: (f32, f32),
    metallic: f32,
) -> Preset {
    Preset {
        name,
        category,
        description,
        base,
        detail,
        detail_mix,
        relief,
        roughness,
        metallic,
    }
}

static PRESETS: &[Preset] = &[
    // ------------------------------------------------------------- Stone
    preset(
        "Granite",
        "Stone",
        "Speckled igneous rock.",
        layer(
            "cells",
            &[("scale", 40.0), ("jitter", 1.0), ("contrast", 1.4)],
            &[c(0.30, 0.29, 0.30), c(0.72, 0.68, 0.66)],
        ),
        FINE,
        0.5,
        0.35,
        (0.35, 0.6),
        0.0,
    ),
    preset(
        "Marble",
        "Stone",
        "Polished marble with turbulent veins.",
        layer(
            "marble_veins",
            &[],
            &[c(0.90, 0.89, 0.87), c(0.22, 0.22, 0.25)],
        ),
        COARSE,
        0.2,
        0.1,
        (0.08, 0.25),
        0.0,
    ),
    preset(
        "Cobblestone",
        "Stone",
        "Rounded street cobbles with deep joints.",
        layer(
            "cobblestones",
            &[],
            &[c(0.07, 0.07, 0.06), c(0.52, 0.50, 0.47)],
        ),
        GRIME,
        0.6,
        0.3,
        (0.6, 0.9),
        0.0,
    ),
    preset(
        "Slate",
        "Stone",
        "Layered dark slate.",
        layer(
            "strata",
            &[("layers", 14.0)],
            &[c(0.14, 0.15, 0.17), c(0.36, 0.38, 0.41)],
        ),
        FINE,
        0.4,
        0.3,
        (0.45, 0.75),
        0.0,
    ),
    preset(
        "Cracked Stone",
        "Stone",
        "Stone plates split by cracks.",
        layer(
            "cracks",
            &[("scale", 6.0)],
            &[c(0.05, 0.05, 0.05), c(0.55, 0.53, 0.50)],
        ),
        GRIME,
        0.5,
        0.25,
        (0.55, 0.85),
        0.0,
    ),
    preset(
        "Sandstone",
        "Stone",
        "Soft sedimentary sandstone.",
        layer(
            "strata",
            &[("layers", 9.0), ("warp", 0.4)],
            &[c(0.55, 0.40, 0.25), c(0.82, 0.68, 0.46)],
        ),
        FINE,
        0.35,
        0.3,
        (0.7, 0.95),
        0.0,
    ),
    preset(
        "Flagstone",
        "Stone",
        "Irregular paving flags.",
        layer(
            "tiles",
            &[
                ("columns", 5.0),
                ("rows", 5.0),
                ("gap", 0.07),
                ("variation", 0.7),
            ],
            &[c(0.06, 0.06, 0.06), c(0.45, 0.45, 0.43)],
        ),
        GRIME,
        0.55,
        0.3,
        (0.6, 0.9),
        0.0,
    ),
    // ------------------------------------------------------------- Brick
    preset(
        "Red Brick",
        "Brick",
        "Classic running-bond brick wall.",
        layer(
            "bricks_pro",
            &[],
            &[c(0.55, 0.53, 0.50), c(0.55, 0.20, 0.13)],
        ),
        GRIME,
        0.5,
        0.3,
        (0.7, 0.95),
        0.0,
    ),
    preset(
        "Old Brick",
        "Brick",
        "Weathered brick, dirty and uneven.",
        layer(
            "bricks_pro",
            &[("variation", 0.9), ("rows", 14.0)],
            &[c(0.42, 0.40, 0.37), c(0.42, 0.20, 0.15)],
        ),
        DIRT,
        0.8,
        0.35,
        (0.75, 1.0),
        0.0,
    ),
    preset(
        "Stone Tiles",
        "Brick",
        "Square stone floor tiles.",
        layer("tiles", &[], &[c(0.08, 0.08, 0.08), c(0.68, 0.66, 0.62)]),
        FINE,
        0.35,
        0.2,
        (0.3, 0.6),
        0.0,
    ),
    preset(
        "Hex Tiles",
        "Brick",
        "Hexagonal ceramic tiles.",
        layer(
            "hex_tiles",
            &[],
            &[c(0.65, 0.65, 0.65), c(0.14, 0.34, 0.45)],
        ),
        FINE,
        0.25,
        0.15,
        (0.1, 0.35),
        0.0,
    ),
    // ---------------------------------------------------------- Concrete
    preset(
        "Concrete",
        "Concrete",
        "Cast concrete with air pockets.",
        layer(
            "concrete_pits",
            &[],
            &[c(0.34, 0.34, 0.33), c(0.62, 0.61, 0.60)],
        ),
        GRIME,
        0.5,
        0.3,
        (0.7, 0.95),
        0.0,
    ),
    preset(
        "Worn Concrete",
        "Concrete",
        "Stained, rough concrete.",
        layer(
            "grunge",
            &[("scale", 3.0)],
            &[c(0.30, 0.30, 0.29), c(0.66, 0.65, 0.63)],
        ),
        DIRT,
        0.7,
        0.4,
        (0.75, 1.0),
        0.0,
    ),
    // ------------------------------------------------------------ Ground
    preset(
        "Dirt",
        "Ground",
        "Loose dry soil.",
        layer(
            "dirt",
            &[("scale", 7.0)],
            &[c(0.14, 0.09, 0.05), c(0.42, 0.30, 0.19)],
        ),
        FINE,
        0.4,
        0.5,
        (0.85, 1.0),
        0.0,
    ),
    preset(
        "Sand",
        "Ground",
        "Wind-blown dune ripples.",
        layer(
            "sand_ripples",
            &[],
            &[c(0.62, 0.50, 0.32), c(0.86, 0.74, 0.52)],
        ),
        FINE,
        0.2,
        0.3,
        (0.8, 1.0),
        0.0,
    ),
    preset(
        "Gravel",
        "Ground",
        "Small angular gravel.",
        layer(
            "cells",
            &[("scale", 24.0), ("contrast", 1.6)],
            &[c(0.18, 0.17, 0.16), c(0.62, 0.60, 0.57)],
        ),
        FINE,
        0.5,
        0.45,
        (0.7, 0.95),
        0.0,
    ),
    preset(
        "Mud",
        "Ground",
        "Wet churned mud.",
        layer(
            "warped_fbm",
            &[("scale", 5.0)],
            &[c(0.07, 0.05, 0.03), c(0.24, 0.17, 0.10)],
        ),
        FINE,
        0.3,
        0.5,
        (0.2, 0.5),
        0.0,
    ),
    preset(
        "Cracked Earth",
        "Ground",
        "Dried, cracked clay.",
        layer(
            "cracks",
            &[("scale", 5.0), ("width", 0.04)],
            &[c(0.08, 0.05, 0.03), c(0.52, 0.38, 0.25)],
        ),
        FINE,
        0.35,
        0.3,
        (0.8, 1.0),
        0.0,
    ),
    preset(
        "Snow",
        "Ground",
        "Wind-packed snow.",
        layer(
            "perlin_fbm",
            &[("scale", 6.0), ("octaves", 6.0)],
            &[c(0.80, 0.84, 0.90), c(0.98, 0.99, 1.0)],
        ),
        COARSE,
        0.1,
        0.6,
        (0.35, 0.6),
        0.0,
    ),
    // -------------------------------------------------------------- Wood
    preset(
        "Oak Planks",
        "Wood",
        "Floorboards with visible grain.",
        layer("planks", &[], &[c(0.04, 0.03, 0.02), c(0.52, 0.34, 0.18)]),
        FINE,
        0.3,
        0.2,
        (0.45, 0.7),
        0.0,
    ),
    preset(
        "Herringbone Parquet",
        "Wood",
        "Herringbone wooden floor.",
        layer(
            "herringbone",
            &[],
            &[c(0.07, 0.04, 0.02), c(0.45, 0.28, 0.14)],
        ),
        FINE,
        0.3,
        0.2,
        (0.35, 0.6),
        0.0,
    ),
    preset(
        "Wood Grain",
        "Wood",
        "Raw timber surface.",
        layer(
            "wood_grain",
            &[],
            &[c(0.22, 0.12, 0.06), c(0.62, 0.42, 0.22)],
        ),
        FINE,
        0.3,
        0.25,
        (0.5, 0.8),
        0.0,
    ),
    preset(
        "Basket Parquet",
        "Wood",
        "Basket-weave parquet.",
        layer(
            "basket_weave",
            &[],
            &[c(0.06, 0.04, 0.02), c(0.58, 0.40, 0.22)],
        ),
        FINE,
        0.3,
        0.2,
        (0.35, 0.6),
        0.0,
    ),
    // ------------------------------------------------------------- Metal
    preset(
        "Brushed Steel",
        "Metal",
        "Anisotropic brushed steel.",
        layer(
            "brushed_metal",
            &[],
            &[c(0.55, 0.56, 0.58), c(0.82, 0.83, 0.85)],
        ),
        SCRATCHED,
        0.3,
        0.3,
        (0.25, 0.5),
        1.0,
    ),
    preset(
        "Diamond Plate",
        "Metal",
        "Raised diamond tread plate.",
        layer(
            "diamond_plate",
            &[],
            &[c(0.45, 0.46, 0.48), c(0.78, 0.79, 0.82)],
        ),
        SCRATCHED,
        0.4,
        0.2,
        (0.3, 0.6),
        1.0,
    ),
    preset(
        "Hammered Copper",
        "Metal",
        "Hand-hammered copper sheet.",
        layer(
            "hammered_dents",
            &[],
            &[c(0.55, 0.25, 0.13), c(0.95, 0.52, 0.32)],
        ),
        FINE,
        0.3,
        0.25,
        (0.25, 0.5),
        1.0,
    ),
    preset(
        "Rusted Iron",
        "Metal",
        "Pitted iron covered in rust.",
        layer("rust", &[], &[c(0.22, 0.22, 0.24), c(0.55, 0.24, 0.09)]),
        DIRT,
        0.5,
        0.5,
        (0.55, 0.95),
        0.6,
    ),
    preset(
        "Corrugated Metal",
        "Metal",
        "Corrugated sheet metal.",
        layer(
            "corrugated",
            &[],
            &[c(0.38, 0.40, 0.42), c(0.68, 0.70, 0.72)],
        ),
        DIRT,
        0.5,
        0.2,
        (0.4, 0.75),
        1.0,
    ),
    preset(
        "Perforated Steel",
        "Metal",
        "Steel plate with round holes.",
        layer(
            "perforated",
            &[],
            &[c(0.04, 0.04, 0.05), c(0.62, 0.64, 0.67)],
        ),
        SCRATCHED,
        0.3,
        0.2,
        (0.3, 0.55),
        1.0,
    ),
    // ------------------------------------------------------------ Fabric
    preset(
        "Woven Fabric",
        "Fabric",
        "Plain-weave cloth.",
        layer(
            "fabric_weave",
            &[],
            &[c(0.10, 0.12, 0.20), c(0.30, 0.36, 0.55)],
        ),
        FINE,
        0.25,
        0.3,
        (0.85, 1.0),
        0.0,
    ),
    preset(
        "Leather",
        "Fabric",
        "Grained leather with pores.",
        layer(
            "leather_pores",
            &[],
            &[c(0.10, 0.05, 0.03), c(0.36, 0.20, 0.12)],
        ),
        COARSE,
        0.3,
        0.3,
        (0.4, 0.65),
        0.0,
    ),
    // ----------------------------------------------------------- Organic
    preset(
        "Moss",
        "Organic",
        "Dense moss carpet.",
        layer("moss", &[], &[c(0.07, 0.14, 0.04), c(0.30, 0.45, 0.12)]),
        FINE,
        0.4,
        0.5,
        (0.85, 1.0),
        0.0,
    ),
    preset(
        "Bark",
        "Organic",
        "Deeply furrowed tree bark.",
        layer(
            "ridged",
            &[("scale", 5.0), ("octaves", 6.0)],
            &[c(0.08, 0.05, 0.03), c(0.34, 0.24, 0.16)],
        ),
        FINE,
        0.3,
        0.4,
        (0.8, 1.0),
        0.0,
    ),
    preset(
        "Dragon Scales",
        "Organic",
        "Overlapping reptile scales.",
        layer("scales", &[], &[c(0.05, 0.14, 0.06), c(0.30, 0.55, 0.22)]),
        FINE,
        0.25,
        0.2,
        (0.35, 0.6),
        0.0,
    ),
    // ------------------------------------------------------------ Sci-fi
    preset(
        "Sci-fi Panels",
        "Sci-fi",
        "Armoured hull panels.",
        layer(
            "grid",
            &[("count", 6.0)],
            &[c(0.08, 0.09, 0.11), c(0.50, 0.54, 0.60)],
        ),
        SCRATCHED,
        0.4,
        0.25,
        (0.3, 0.6),
        0.9,
    ),
    preset(
        "Hex Armour",
        "Sci-fi",
        "Hexagonal armour plating.",
        layer(
            "hex_tiles",
            &[("columns", 8.0), ("rows", 8.0)],
            &[c(0.06, 0.07, 0.09), c(0.32, 0.38, 0.45)],
        ),
        SCRATCHED,
        0.4,
        0.25,
        (0.25, 0.5),
        0.9,
    ),
    preset(
        "Knurled Grip",
        "Sci-fi",
        "Machined knurled grip.",
        layer("knurl", &[], &[c(0.20, 0.21, 0.23), c(0.62, 0.64, 0.68)]),
        SCRATCHED,
        0.3,
        0.2,
        (0.3, 0.55),
        1.0,
    ),
    // -------------------------------------------------------------- Misc
    preset(
        "Water Ripples",
        "Misc",
        "Ripples on a water surface.",
        layer(
            "water_ripples",
            &[],
            &[c(0.02, 0.10, 0.16), c(0.30, 0.55, 0.65)],
        ),
        COARSE,
        0.1,
        0.1,
        (0.02, 0.12),
        0.0,
    ),
    preset(
        "Caustics",
        "Misc",
        "Light caustics on a pool floor.",
        layer("caustics", &[], &[c(0.05, 0.25, 0.35), c(0.85, 0.98, 1.0)]),
        COARSE,
        0.1,
        0.1,
        (0.2, 0.4),
        0.0,
    ),
    preset(
        "Truchet Maze",
        "Misc",
        "Truchet tile maze.",
        layer("truchet", &[], &[c(0.06, 0.06, 0.07), c(0.78, 0.74, 0.62)]),
        FINE,
        0.2,
        0.15,
        (0.4, 0.7),
        0.0,
    ),
    preset(
        "Polka Dots",
        "Misc",
        "Embossed dots.",
        layer("dots", &[], &[c(0.14, 0.12, 0.18), c(0.85, 0.80, 0.30)]),
        FINE,
        0.2,
        0.15,
        (0.4, 0.7),
        0.0,
    ),
];

pub fn presets() -> &'static [Preset] {
    PRESETS
}

pub fn categories() -> Vec<&'static str> {
    let mut out: Vec<&'static str> = Vec::new();
    for preset in PRESETS {
        if !out.contains(&preset.category) {
            out.push(preset.category);
        }
    }
    out
}

const DENSITY_KEYS: [&str; 5] = ["scale", "count", "columns", "rows", "cells"];

fn add_layer(
    graph: &mut MaterialGraph,
    layer: &Layer,
    params: &PresetParams,
    position: Point,
    density: bool,
) -> NodeId {
    let op = procedural::find(layer.op).unwrap_or_else(|| panic!("unknown op {}", layer.op));
    let NodeKind::Proc {
        op,
        params: mut values,
        mut colors,
    } = NodeKind::proc_default(op)
    else {
        unreachable!("proc_default returns Proc");
    };
    for (key, value) in layer.params {
        let index =
            op.0.params
                .iter()
                .position(|spec| spec.key == *key)
                .unwrap_or_else(|| panic!("op {} has no parameter {key}", layer.op));
        values[index] = *value;
    }
    for (index, color) in layer.colors.iter().enumerate() {
        if let Some(slot) = colors.get_mut(index) {
            *slot = *color;
        }
    }
    for (index, spec) in op.0.params.iter().enumerate() {
        if spec.key == "seed" {
            values[index] = (values[index] + params.seed as f32).clamp(spec.min, spec.max);
        } else if density && DENSITY_KEYS.contains(&spec.key) {
            let scaled = if matches!(spec.kind, procedural::ParamKind::Int) {
                (values[index] * params.scale).round().max(1.0)
            } else {
                values[index] * params.scale
            };
            values[index] = scaled.clamp(spec.min, spec.max);
        }
    }
    graph.add_node(
        NodeKind::Proc {
            op,
            params: values,
            colors,
        },
        position,
    )
}

fn math(graph: &mut MaterialGraph, id: &str, set: &[(&str, f32)], position: Point) -> NodeId {
    let op = procedural::find(id).unwrap_or_else(|| panic!("unknown op {id}"));
    let NodeKind::Proc {
        op,
        mut params,
        colors,
    } = NodeKind::proc_default(op)
    else {
        unreachable!("proc_default returns Proc");
    };
    for (key, value) in set {
        let index =
            op.0.params
                .iter()
                .position(|spec| spec.key == *key)
                .unwrap_or_else(|| panic!("op {id} has no parameter {key}"));
        params[index] = *value;
    }
    graph.add_node(NodeKind::Proc { op, params, colors }, position)
}

impl Preset {
    pub fn build(&self, params: &PresetParams) -> MaterialGraph {
        let mut graph = MaterialGraph {
            nodes: Vec::new(),
            links: Vec::new(),
            next_id: 1,
            resolution: 512,
        };
        let wear = params.wear.clamp(0.0, 3.0);
        let mix = (self.detail_mix * wear).clamp(0.0, 1.0);
        let relief = (self.relief * wear).clamp(0.0, 0.9);

        let base = add_layer(
            &mut graph,
            &self.base,
            params,
            Point::new(-520.0, -120.0),
            true,
        );
        let detail = add_layer(
            &mut graph,
            &self.detail,
            params,
            Point::new(-520.0, 180.0),
            false,
        );
        let shade = math(&mut graph, "math_multiply", &[], Point::new(-260.0, -150.0));
        let colour = math(
            &mut graph,
            "math_lerp",
            &[("factor", mix)],
            Point::new(-20.0, -150.0),
        );
        let height = math(
            &mut graph,
            "math_lerp",
            &[("factor", relief)],
            Point::new(-20.0, 20.0),
        );
        let rough = math(
            &mut graph,
            "math_remap",
            &[("out_min", self.roughness.0), ("out_max", self.roughness.1)],
            Point::new(-20.0, 190.0),
        );
        let output = graph.add_node(
            NodeKind::Output {
                roughness: 0.5,
                metallic: self.metallic,
                height: 0.5,
                ao: 1.0,
                options: OutputOptions::default(),
            },
            Point::new(260.0, -20.0),
        );
        // Pattern ops: 0 = colour, 1 = value (relief).
        graph.connect(base, 0, shade, 0);
        graph.connect(detail, 0, shade, 1);
        graph.connect(base, 0, colour, 0);
        graph.connect(shade, 0, colour, 1);
        graph.connect(base, 1, height, 0);
        graph.connect(detail, 1, height, 1);
        graph.connect(detail, 1, rough, 0);
        graph.connect(colour, 0, output, 0);
        graph.connect(rough, 0, output, 1);
        graph.connect(height, 0, output, 3);
        graph
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn std_dev(values: &[u8]) -> f32 {
        let n = values.len() as f32;
        let mean = values.iter().map(|v| *v as f32 / 255.0).sum::<f32>() / n;
        (values
            .iter()
            .map(|v| (*v as f32 / 255.0 - mean).powi(2))
            .sum::<f32>()
            / n)
            .sqrt()
    }

    fn bake(preset: &Preset, params: &PresetParams) -> crate::uikit::generators::BakedMaterial {
        let mut graph = preset.build(params);
        graph.resolution = 64;
        graph.bake().expect("preset bakes")
    }

    #[test]
    fn library_has_thirty_presets_across_categories() {
        assert!(presets().len() >= 30, "{}", presets().len());
        assert!(categories().len() >= 9);
        let mut names: Vec<_> = presets().iter().map(|p| p.name).collect();
        names.sort_unstable();
        names.dedup();
        assert_eq!(names.len(), presets().len(), "preset names are unique");
    }

    #[test]
    fn every_preset_bakes_with_relief() {
        for preset in presets() {
            let baked = bake(preset, &PresetParams::default());
            assert_eq!(baked.base.len(), 64 * 64 * 4, "{}", preset.name);
            let first = &baked.base[0..3];
            assert!(
                baked.base.chunks(4).any(|px| &px[0..3] != first),
                "{} has a constant base colour",
                preset.name
            );
            let deviation = std_dev(&baked.height);
            assert!(deviation > 0.02, "{} height std {deviation}", preset.name);
            assert!(std_dev(&baked.roughness) >= 0.0);
        }
    }

    #[test]
    fn presets_are_deterministic_and_seedable() {
        for preset in presets() {
            let a = bake(preset, &PresetParams::default());
            let b = bake(preset, &PresetParams::default());
            assert_eq!(a.base, b.base, "{}", preset.name);
            assert_eq!(a.height, b.height, "{}", preset.name);
        }
        let preset = &presets()[0];
        let other = bake(
            preset,
            &PresetParams {
                seed: 7,
                ..PresetParams::default()
            },
        );
        assert_ne!(bake(preset, &PresetParams::default()).base, other.base);
    }

    #[test]
    fn presets_survive_text_roundtrip() {
        for preset in presets() {
            let graph = preset.build(&PresetParams::default());
            let text = graph.to_text();
            let loaded = MaterialGraph::from_text(&text).expect("parses");
            assert_eq!(loaded.nodes.len(), graph.nodes.len(), "{}", preset.name);
            assert_eq!(loaded.links.len(), graph.links.len(), "{}", preset.name);
        }
    }

    #[test]
    fn output_options_roundtrip_and_channels_render() {
        use crate::uikit::generators::PreviewChannel;
        let mut graph = presets()[0].build(&PresetParams::default());
        let output = graph.output_node().expect("output");
        if let Some(NodeKind::Output { options, .. }) = graph.node_mut(output).map(|n| &mut n.kind)
        {
            options.auto_height = false;
            options.height_contrast = 2.5;
        }
        let loaded = MaterialGraph::from_text(&graph.to_text()).expect("parses");
        let output = loaded.output_node().expect("output");
        assert!(matches!(
            loaded.node(output).map(|n| &n.kind),
            Some(NodeKind::Output { options, .. })
                if !options.auto_height && (options.height_contrast - 2.5).abs() < 1e-4
        ));
        let baked = bake(&presets()[0], &PresetParams::default());
        for channel in PreviewChannel::ALL {
            assert!(!channel.label().is_empty());
            assert_eq!(baked.channel_rgba(channel).len(), 64 * 64 * 4);
        }
    }
}

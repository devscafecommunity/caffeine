//! Portable MaterialCraft (`.mcmat`) and SBS import/export.
//!
//! Clean-room implementation from the public package format: `.mcmat` is a ZIP
//! with `material.json`, cooked map PNGs and an optional `source.sbs`; SBS is
//! XML text with `compNode` graphs. Imported nodes we do not evaluate are kept
//! as `NodeKind::Unknown` placeholders so links and graph structure survive.

use crate::uikit::generators::{
    BlendMode, ImageFilter, MaterialGraph, NodeId, NodeKind, NoiseType, NormalBlendMode,
    Projection, WrapMode,
};
use serde::{Deserialize, Serialize};
use serde_json::Value as JsonValue;
use std::collections::HashMap;
use std::io::{Cursor, Read, Write};

pub const MCMAT_FORMAT: &str = "craft-material";
pub const MCMAT_VERSION: u32 = 2;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Manifest {
    pub format: String,
    pub version: u32,
    pub name: String,
    pub graph: String,
    pub size: [u32; 2],
    pub maps: Vec<MapEntry>,
    #[serde(default)]
    pub parameters: Vec<JsonValue>,
    #[serde(default)]
    pub inputs: Vec<ImageInput>,
    #[serde(default)]
    pub presets: Vec<JsonValue>,
    #[serde(default)]
    pub source: String,
}

impl Manifest {
    pub fn new(name: &str, graph: &str, size: [u32; 2], maps: Vec<MapEntry>) -> Self {
        Self {
            format: MCMAT_FORMAT.to_owned(),
            version: MCMAT_VERSION,
            name: name.to_owned(),
            graph: graph.to_owned(),
            size,
            maps,
            parameters: Vec::new(),
            inputs: Vec::new(),
            presets: Vec::new(),
            source: "source.sbs".to_owned(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct MapEntry {
    pub usage: String,
    pub file: String,
    pub color_space: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct ImageInput {
    pub identifier: String,
    pub kind: String,
}

#[derive(Debug, Clone)]
pub struct PortableMap {
    pub entry: MapEntry,
    pub bytes: Vec<u8>,
}

#[derive(Debug, Clone)]
pub struct PortableMaterial {
    pub manifest: Manifest,
    pub maps: Vec<PortableMap>,
    pub source: String,
}

const MAX_JSON: u64 = 8 * 1024 * 1024;
const MAX_ENTRY: u64 = 128 * 1024 * 1024;

/// Reads a `.mcmat` archive. Map bytes stay in memory; callers choose where to
/// write them. Unknown extra entries are ignored.
pub fn read_mcmat(bytes: &[u8]) -> Result<PortableMaterial, String> {
    let mut archive = zip::ZipArchive::new(Cursor::new(bytes))
        .map_err(|error| format!("not a portable material: {error}"))?;
    let manifest_bytes = read_entry(&mut archive, "material.json", MAX_JSON)?;
    let mut manifest: Manifest = serde_json::from_slice(&manifest_bytes)
        .map_err(|error| format!("material.json: {error}"))?;
    if manifest.format != MCMAT_FORMAT {
        return Err(format!("unsupported material format {}", manifest.format));
    }
    if manifest.version > MCMAT_VERSION {
        return Err(format!(
            "material version {} is newer than supported {}",
            manifest.version, MCMAT_VERSION
        ));
    }
    let source = if manifest.source.is_empty() {
        String::new()
    } else {
        match read_entry(&mut archive, &manifest.source, MAX_ENTRY) {
            Ok(bytes) => String::from_utf8_lossy(&bytes).into_owned(),
            Err(_) => String::new(),
        }
    };
    let mut maps = Vec::with_capacity(manifest.maps.len());
    for entry in &manifest.maps {
        let bytes = read_entry(&mut archive, &entry.file, MAX_ENTRY)?;
        maps.push(PortableMap {
            entry: entry.clone(),
            bytes,
        });
    }
    manifest.source = "source.sbs".to_owned();
    Ok(PortableMaterial {
        manifest,
        maps,
        source,
    })
}

fn read_entry(
    archive: &mut zip::ZipArchive<Cursor<&[u8]>>,
    name: &str,
    limit: u64,
) -> Result<Vec<u8>, String> {
    let entry = archive
        .by_name(name)
        .map_err(|_| format!("{name} is missing"))?;
    if entry.size() > limit {
        return Err(format!("{name} exceeds {limit} bytes"));
    }
    let mut bytes = Vec::new();
    entry
        .take(limit + 1)
        .read_to_end(&mut bytes)
        .map_err(|error| error.to_string())?;
    if bytes.len() as u64 > limit {
        return Err(format!("{name} exceeds {limit} bytes"));
    }
    Ok(bytes)
}

/// Writes a portable `.mcmat` archive with `material.json`, the cooked maps and
/// `source.sbs`.
pub fn write_mcmat(material: &PortableMaterial) -> Result<Vec<u8>, String> {
    let mut cursor = Cursor::new(Vec::new());
    {
        let mut writer = zip::ZipWriter::new(&mut cursor);
        let manifest = Manifest {
            source: "source.sbs".to_owned(),
            ..material.manifest.clone()
        };
        let json = serde_json::to_vec_pretty(&manifest).map_err(|error| error.to_string())?;
        writer
            .start_file(
                "material.json",
                zip::write::SimpleFileOptions::default()
                    .compression_method(zip::CompressionMethod::Deflated),
            )
            .map_err(|error| error.to_string())?;
        writer.write_all(&json).map_err(|error| error.to_string())?;
        for map in &material.maps {
            writer
                .start_file(
                    map.entry.file.as_str(),
                    zip::write::SimpleFileOptions::default()
                        .compression_method(zip::CompressionMethod::Stored),
                )
                .map_err(|error| error.to_string())?;
            writer
                .write_all(&map.bytes)
                .map_err(|error| error.to_string())?;
        }
        if !material.source.is_empty() {
            writer
                .start_file(
                    "source.sbs",
                    zip::write::SimpleFileOptions::default()
                        .compression_method(zip::CompressionMethod::Stored),
                )
                .map_err(|error| error.to_string())?;
            writer
                .write_all(material.source.as_bytes())
                .map_err(|error| error.to_string())?;
        }
        writer.finish().map_err(|error| error.to_string())?;
    }
    Ok(cursor.into_inner())
}

#[derive(Debug, Clone)]
pub struct SbsImport {
    pub graph: MaterialGraph,
    pub graph_name: String,
    pub warnings: Vec<String>,
    pub unknown_nodes: usize,
}

#[derive(Debug, Clone)]
enum SbsValue {
    Float(f32),
    Vec2([f32; 2]),
    Vec4([f32; 4]),
    Int(i32),
    Bool(bool),
    Text(String),
}

impl SbsValue {
    fn float(&self) -> Option<f32> {
        match self {
            SbsValue::Float(value) => Some(*value),
            SbsValue::Int(value) => Some(*value as f32),
            SbsValue::Bool(value) => Some(if *value { 1.0 } else { 0.0 }),
            _ => None,
        }
    }

    fn int(&self) -> Option<i32> {
        match self {
            SbsValue::Int(value) => Some(*value),
            SbsValue::Float(value) => Some(*value as i32),
            SbsValue::Bool(value) => Some(if *value { 1 } else { 0 }),
            _ => None,
        }
    }

    fn bool(&self) -> Option<bool> {
        match self {
            SbsValue::Bool(value) => Some(*value),
            SbsValue::Int(value) => Some(*value != 0),
            SbsValue::Float(value) => Some(*value != 0.0),
            _ => None,
        }
    }

    fn text(&self) -> Option<&str> {
        match self {
            SbsValue::Text(value) => Some(value),
            _ => None,
        }
    }
}

fn xml_child<'a, 'input>(
    node: roxmltree::Node<'a, 'input>,
    name: &str,
) -> Option<roxmltree::Node<'a, 'input>> {
    node.children()
        .find(|child| child.is_element() && child.tag_name().name() == name)
}

fn xml_children<'a, 'input>(
    node: roxmltree::Node<'a, 'input>,
    name: &'a str,
) -> impl Iterator<Item = roxmltree::Node<'a, 'input>> + 'a {
    node.children()
        .filter(move |child| child.is_element() && child.tag_name().name() == name)
}

fn xml_value<'a>(node: roxmltree::Node<'a, 'a>, name: &str) -> Option<String> {
    let child = xml_child(node, name)?;
    child
        .attribute("v")
        .map(str::to_owned)
        .or_else(|| child.text().map(|text| text.trim().to_owned()))
        .filter(|value| !value.is_empty())
}

fn attr_value(node: roxmltree::Node<'_, '_>, name: &str) -> Option<String> {
    node.attribute(name).map(str::to_owned)
}

fn param<'a>(component: roxmltree::Node<'a, 'a>, names: &[&str]) -> Option<SbsValue> {
    let filter = xml_child(component, "compImplementation")
        .and_then(|implementation| xml_child(implementation, "compFilter"))?;
    let parameters = xml_child(filter, "parameters")?;
    for parameter in xml_children(parameters, "parameter") {
        let name = xml_value(parameter, "name").unwrap_or_default();
        if !names
            .iter()
            .any(|candidate| candidate.eq_ignore_ascii_case(&name))
        {
            continue;
        }
        let value = xml_child(parameter, "paramValue")?;
        let constant = value.children().find(|child| child.is_element())?;
        let tag = constant.tag_name().name();
        let raw = attr_value(constant, "v").unwrap_or_default();
        let numbers: Vec<f32> = raw
            .split_whitespace()
            .filter_map(|word| word.parse().ok())
            .collect();
        return match tag {
            "constantValueFloat1" => Some(SbsValue::Float(numbers.first().copied()?)),
            "constantValueFloat2" => Some(SbsValue::Vec2([*numbers.first()?, *numbers.get(1)?])),
            "constantValueFloat3" => Some(SbsValue::Vec4([
                *numbers.first()?,
                *numbers.get(1)?,
                *numbers.get(2)?,
                1.0,
            ])),
            "constantValueFloat4" => Some(SbsValue::Vec4([
                *numbers.first()?,
                *numbers.get(1)?,
                *numbers.get(2)?,
                *numbers.get(3)?,
            ])),
            "constantValueInt32" | "constantValueInt16" => {
                Some(SbsValue::Int(numbers.first().copied()? as i32))
            }
            "constantValueBool" => Some(SbsValue::Bool(raw.trim() != "0")),
            "constantValueString" => Some(SbsValue::Text(raw)),
            _ => None,
        };
    }
    None
}

fn param_f32(node: roxmltree::Node<'_, '_>, names: &[&str], fallback: f32) -> f32 {
    param(node, names)
        .and_then(|value| value.float())
        .unwrap_or(fallback)
}

fn param_i32(node: roxmltree::Node<'_, '_>, names: &[&str], fallback: i32) -> i32 {
    param(node, names)
        .and_then(|value| value.int())
        .unwrap_or(fallback)
}

fn param_bool(node: roxmltree::Node<'_, '_>, names: &[&str], fallback: bool) -> bool {
    param(node, names)
        .and_then(|value| value.bool())
        .unwrap_or(fallback)
}

fn param_color(node: roxmltree::Node<'_, '_>, names: &[&str], fallback: [f32; 4]) -> [f32; 4] {
    param(node, names)
        .and_then(|value| match value {
            SbsValue::Vec4(color) => Some(color),
            SbsValue::Vec2(color) => Some([color[0], color[1], 0.0, 1.0]),
            SbsValue::Float(value) => Some([value, value, value, 1.0]),
            _ => None,
        })
        .unwrap_or(fallback)
}

fn param_text(node: roxmltree::Node<'_, '_>, names: &[&str], fallback: &str) -> String {
    param(node, names)
        .and_then(|value| value.text().map(str::to_owned))
        .unwrap_or_else(|| fallback.to_owned())
}

fn blend_mode(value: i32) -> BlendMode {
    match value {
        0 => BlendMode::Normal,
        1 => BlendMode::Multiply,
        2 => BlendMode::Screen,
        3 => BlendMode::Overlay,
        4 => BlendMode::Add,
        5 => BlendMode::Subtract,
        6 => BlendMode::Lighten,
        7 => BlendMode::Darken,
        8 => BlendMode::Difference,
        9 => BlendMode::SoftLight,
        10 => BlendMode::HardLight,
        11 => BlendMode::ColorDodge,
        12 => BlendMode::ColorBurn,
        _ => BlendMode::Normal,
    }
}

fn filter_kind(filter: &str, component: roxmltree::Node<'_, '_>) -> NodeKind {
    let lower = filter.to_lowercase();
    match lower.as_str() {
        "output" => NodeKind::Output {
            roughness: param_f32(component, &["roughness"], 0.5),
            metallic: param_f32(component, &["metallic"], 0.0),
            height: param_f32(component, &["height"], 0.5),
            ao: param_f32(component, &["ambientocclusion", "ao"], 1.0),
            options: super::generators::OutputOptions::default(),
        },
        "image" | "normalmap" => {
            let projection =
                Projection::from_index(param_i32(component, &["projection"], 0).max(0) as usize);
            let tiling = param(component, &["tiling"])
                .and_then(|value| match value {
                    SbsValue::Vec2(value) => Some(value),
                    SbsValue::Float(value) => Some([value, value]),
                    _ => None,
                })
                .unwrap_or([1.0, 1.0]);
            let offset = param(component, &["offset"])
                .and_then(|value| match value {
                    SbsValue::Vec2(value) => Some(value),
                    _ => None,
                })
                .unwrap_or([0.0, 0.0]);
            let filter = if param_i32(component, &["filter"], 0) == 1 {
                ImageFilter::Nearest
            } else {
                ImageFilter::Linear
            };
            let wrap = if param_i32(component, &["wrap"], 0) == 1 {
                WrapMode::Clamp
            } else {
                WrapMode::Repeat
            };
            let path = param_text(component, &["path"], "");
            if lower == "normalmap" {
                NodeKind::NormalMap {
                    path,
                    projection,
                    tiling,
                    offset,
                    filter,
                    wrap,
                }
            } else {
                NodeKind::Image {
                    path,
                    projection,
                    tiling,
                    offset,
                    filter,
                    wrap,
                }
            }
        }
        "voronoi" => NodeKind::Voronoi {
            scale: param_f32(component, &["scale"], 6.0),
            jitter: param_f32(component, &["jitter"], 0.8),
            seed: param_i32(component, &["seed"], 0),
            color_a: param_color(component, &["color1"], [0.0, 0.0, 0.0, 1.0]),
            color_b: param_color(component, &["color2"], [1.0, 1.0, 1.0, 1.0]),
            mode: if param_i32(component, &["mode"], 0) == 1 {
                crate::uikit::generators::VoronoiMode::Distance
            } else {
                crate::uikit::generators::VoronoiMode::Cells
            },
        },
        "curve" => {
            let mut points = [0.0; 5];
            for (index, point) in points.iter_mut().enumerate() {
                *point = param_f32(component, &[&format!("point{index}")], 0.5);
            }
            NodeKind::Curve { points }
        }
        "dot" => NodeKind::Dot,
        "uniformcolor" | "solidcolor" | "solid_color" => NodeKind::Color {
            color: param_color(
                component,
                &["outputcolor", "color", "basecolor"],
                [0.5, 0.5, 0.5, 1.0],
            ),
        },
        "blur" => NodeKind::Blur {
            radius: (param_f32(component, &["intensity", "radius"], 1.0) * 4.0).clamp(0.0, 64.0),
            wrap: WrapMode::Clamp,
        },
        "blend" => NodeKind::Blend {
            factor: param_f32(component, &["opacitymult", "opacity"], 1.0).clamp(0.0, 1.0),
            mode: blend_mode(param_i32(component, &["blendingmode", "mode"], 0)),
        },
        "checker" | "checkerboard" => NodeKind::Checker {
            scale: param_f32(component, &["scale", "tiling"], 4.0).max(1.0),
            color_a: param_color(component, &["color1", "color"], [0.0, 0.0, 0.0, 1.0]),
            color_b: param_color(component, &["color2"], [1.0, 1.0, 1.0, 1.0]),
        },
        "bricks" | "brick" => NodeKind::Bricks {
            rows: param_i32(component, &["numrows", "rows"], 4).max(1),
            columns: param_i32(component, &["numcols", "columns"], 2).max(1),
            mortar: param_f32(component, &["mortar", "bevel"], 0.05).clamp(0.0, 0.4),
            color_a: param_color(component, &["brickcolor1", "color1"], [0.5, 0.2, 0.1, 1.0]),
            color_b: param_color(component, &["brickcolor2", "color2"], [0.4, 0.15, 0.1, 1.0]),
            mortar_color: param_color(component, &["mortarcolor"], [0.7, 0.7, 0.7, 1.0]),
        },
        "noise" | "perlin_noise" | "fractal_noise" | "fractal_sum_base" => NodeKind::Noise {
            noise_type: if lower.contains("perlin") {
                NoiseType::Perlin
            } else {
                NoiseType::Value
            },
            scale: param_f32(component, &["scale", "size"], 6.0).max(1.0),
            octaves: param_i32(component, &["octaves"], 4).max(1),
            persistence: param_f32(component, &["persistence", "falloff"], 0.5),
            lacunarity: param_f32(component, &["lacunarity"], 2.0),
            seed: param_i32(component, &["seed", "randomseed"], 0),
            color_a: param_color(component, &["color1", "background"], [0.0, 0.0, 0.0, 1.0]),
            color_b: param_color(component, &["color2", "foreground"], [1.0, 1.0, 1.0, 1.0]),
        },
        "gradient" | "gradient_linear_1" | "gradient_linear" => NodeKind::Gradient {
            angle: param_f32(component, &["angle", "rotation"], 0.0),
            color_a: param_color(component, &["color1", "color"], [0.0, 0.0, 0.0, 1.0]),
            color_b: param_color(component, &["color2"], [1.0, 1.0, 1.0, 1.0]),
        },
        "levels" => NodeKind::Levels {
            brightness: param_f32(component, &["brightness"], 0.0),
            contrast: param_f32(component, &["contrast"], 1.0),
            gamma: param_f32(component, &["gamma"], 1.0),
            invert: param_bool(component, &["invert"], false),
        },
        "hsl" => NodeKind::Hsl {
            hue: param_f32(component, &["hue"], 0.0),
            saturation: param_f32(component, &["saturation"], 1.0),
            lightness: param_f32(component, &["lightness", "luminosity"], 0.0),
        },
        "threshold" | "mask" => NodeKind::Threshold {
            threshold: param_f32(component, &["threshold"], 0.5),
            softness: param_f32(component, &["softness", "contrast"], 0.1),
            invert: param_bool(component, &["invert"], false),
        },
        "sharpen" => NodeKind::Sharpen {
            amount: param_f32(component, &["amount", "intensity"], 0.5),
            radius: param_f32(component, &["radius"], 1.0).max(1.0),
        },
        "emboss" => NodeKind::Emboss {
            strength: param_f32(component, &["strength", "intensity"], 1.0),
            angle: param_f32(component, &["angle"], 45.0),
        },
        "directional_blur" | "directionalblur" => NodeKind::DirectionalBlur {
            radius: param_f32(component, &["radius", "intensity"], 4.0).max(1.0),
            angle: param_f32(component, &["angle"], 0.0),
            wrap: WrapMode::Clamp,
        },
        "warp" => NodeKind::Warp {
            amount: param_f32(component, &["amount", "intensity"], 0.05),
            scale: param_f32(component, &["scale"], 6.0),
            seed: param_i32(component, &["seed", "randomseed"], 0),
        },
        "edge_detect" | "edgedetect" | "edge" => NodeKind::EdgeMask {
            strength: param_f32(component, &["strength", "intensity"], 1.0),
            invert: param_bool(component, &["invert"], false),
        },
        "curvature" | "cavity" | "cavity_mask" => NodeKind::CavityMask {
            radius: param_f32(component, &["radius"], 4.0).max(1.0),
            strength: param_f32(component, &["strength", "intensity"], 1.0),
            invert: param_bool(component, &["invert"], false),
        },
        "distance" | "distance_mask" => NodeKind::DistanceMask {
            threshold: param_f32(component, &["threshold"], 0.5),
            radius: param_f32(component, &["radius"], 16.0).max(1.0),
            invert: param_bool(component, &["invert"], false),
        },
        "normal_blend" | "normalblend" => NodeKind::NormalBlend {
            strength: param_f32(component, &["strength", "opacity"], 1.0),
            mode: if param_i32(component, &["mode"], 0) == 1 {
                NormalBlendMode::Linear
            } else {
                NormalBlendMode::Whiteout
            },
        },
        "value" | "constant" => NodeKind::Value {
            value: param_f32(component, &["value", "output"], 0.5),
        },
        "luminance" => NodeKind::Luminance,
        "doppio_proc" => {
            let op_id = param(component, &["op"])
                .and_then(|value| value.text().map(str::to_owned))
                .unwrap_or_default();
            let numbers = |name: &str| -> Vec<f32> {
                param(component, &[name])
                    .and_then(|value| value.text().map(str::to_owned))
                    .map(|text| {
                        text.split_whitespace()
                            .filter_map(|w| w.parse().ok())
                            .collect()
                    })
                    .unwrap_or_default()
            };
            let params = numbers("params");
            let colors: Vec<[f32; 4]> = numbers("colors").as_chunks::<4>().0.to_vec();
            match super::procedural::find(&op_id) {
                Some(op) => NodeKind::proc_from_saved(op, &params, &colors),
                None => NodeKind::Unknown {
                    filter: op_id,
                    inputs: Vec::new(),
                    outputs: Vec::new(),
                    note: "Missing procedural op".to_owned(),
                },
            }
        }
        "unknown" => {
            let inputs = param(component, &["inputs"])
                .and_then(|value| value.text().map(split_labels))
                .unwrap_or_default();
            let outputs = param(component, &["outputs"])
                .and_then(|value| value.text().map(split_labels))
                .unwrap_or_default();
            NodeKind::Unknown {
                filter: param(component, &["originalfilter"])
                    .and_then(|value| value.text().map(str::to_owned))
                    .unwrap_or_else(|| "unknown".to_owned()),
                inputs,
                outputs,
                note: param(component, &["note"])
                    .and_then(|value| value.text().map(str::to_owned))
                    .unwrap_or_default(),
            }
        }
        _ => NodeKind::Unknown {
            filter: filter.to_owned(),
            inputs: Vec::new(),
            outputs: Vec::new(),
            note: "Imported placeholder".to_owned(),
        },
    }
}

fn split_labels(text: &str) -> Vec<String> {
    if text.is_empty() {
        Vec::new()
    } else {
        text.split(';').map(str::to_owned).collect()
    }
}

fn usage_port(usage: &str) -> Option<usize> {
    match usage.to_lowercase().as_str() {
        "basecolor" | "base_color" | "diffuse" | "albedo" => Some(0),
        "roughness" => Some(1),
        "metallic" => Some(2),
        "height" | "displacement" => Some(3),
        "ambientocclusion" | "ao" | "occlusion" => Some(4),
        _ => None,
    }
}

fn input_index(kind: &NodeKind, identifier: &str) -> Option<usize> {
    if let Some(index) = identifier
        .strip_prefix("input")
        .and_then(|index| index.parse::<usize>().ok())
    {
        return Some(index);
    }
    let lower = identifier.to_lowercase();
    match kind {
        NodeKind::Blend { .. } => match lower.as_str() {
            "destination" | "base" => Some(0),
            "source" | "blend" => Some(1),
            "opacity" | "mask" => Some(2),
            _ => None,
        },
        NodeKind::NormalBlend { .. } => match lower.as_str() {
            "base" | "input1" => Some(0),
            "detail" | "input2" => Some(1),
            _ => None,
        },
        _ => kind
            .inputs()
            .iter()
            .position(|port| port.label.eq_ignore_ascii_case(identifier)),
    }
}

struct ImportedNode {
    id: NodeId,
    uid: u64,
    kind: NodeKind,
    output_uids: Vec<u64>,
    bridge_port: Option<usize>,
}

/// Minimal SBS reader: reads the first material graph, maps known filters and
/// preserves every other component as an `Unknown` placeholder.
pub fn import_sbs(xml: &str, fallback_name: &str) -> Result<SbsImport, String> {
    let document = roxmltree::Document::parse(xml).map_err(|error| format!("SBS XML: {error}"))?;
    let root = document.root_element();
    if root.tag_name().name() != "package" {
        return Err("not an SBS package".to_owned());
    }
    let content = xml_child(root, "content").ok_or_else(|| "SBS has no content".to_owned())?;
    let graph_element = xml_children(content, "graph")
        .next()
        .ok_or_else(|| "SBS has no graph".to_owned())?;
    let graph_name =
        xml_value(graph_element, "identifier").unwrap_or_else(|| fallback_name.to_owned());

    let graph_outputs = xml_child(graph_element, "graphOutputs");
    let output_usage: HashMap<u64, String> = graph_outputs
        .into_iter()
        .flat_map(|outputs| xml_children(outputs, "graphoutput"))
        .filter_map(|output| {
            let uid = xml_value(output, "uid")?.parse().ok()?;
            let usage = xml_child(output, "usages")
                .and_then(|usages| xml_children(usages, "usage").next())
                .and_then(|usage| xml_value(usage, "name"))?;
            Some((uid, usage))
        })
        .collect();

    let mut graph = MaterialGraph {
        nodes: Vec::new(),
        links: Vec::new(),
        next_id: 1,
        resolution: 512,
    };
    let mut warnings = Vec::new();
    let mut imported = Vec::new();
    let mut unknown_nodes = 0;
    let comp_nodes = xml_child(graph_element, "compNodes");
    for component in comp_nodes
        .into_iter()
        .flat_map(|nodes| xml_children(nodes, "compNode"))
    {
        let uid = xml_value(component, "uid")
            .and_then(|uid| uid.parse().ok())
            .unwrap_or(0);
        let position = xml_child(component, "GUILayout")
            .and_then(|layout| xml_value(layout, "gpos"))
            .and_then(|position| {
                let mut words = position.split_whitespace();
                Some(iced::Point::new(
                    words.next()?.parse().ok()?,
                    words.next()?.parse().ok()?,
                ))
            })
            .unwrap_or(iced::Point::ORIGIN);
        let output_uids: Vec<u64> = xml_child(component, "compOutputs")
            .into_iter()
            .flat_map(|outputs| xml_children(outputs, "compOutput"))
            .filter_map(|output| xml_value(output, "uid")?.parse().ok())
            .collect();
        let implementation = xml_child(component, "compImplementation");
        let input_bridge = implementation.and_then(|node| xml_child(node, "compInputBridge"));
        let output_bridge = implementation.and_then(|node| xml_child(node, "compOutputBridge"));
        let filter = implementation
            .and_then(|node| xml_child(node, "compFilter"))
            .and_then(|filter| xml_value(filter, "filter"));

        let (kind, bridge_port) = if let Some(bridge) = output_bridge {
            let output_uid = xml_value(bridge, "output")
                .and_then(|uid| uid.parse::<u64>().ok())
                .unwrap_or(0);
            let usage = output_usage
                .get(&output_uid)
                .cloned()
                .unwrap_or_else(|| "any".to_owned());
            if let Some(port) = usage_port(&usage) {
                (
                    NodeKind::Output {
                        roughness: 0.5,
                        metallic: 0.0,
                        height: 0.5,
                        ao: 1.0,
                        options: super::generators::OutputOptions::default(),
                    },
                    Some(port),
                )
            } else {
                warnings.push(format!("SBS output `{usage}` kept as a placeholder"));
                (
                    NodeKind::Unknown {
                        filter: format!("output:{usage}"),
                        inputs: Vec::new(),
                        outputs: vec!["Out".to_owned()],
                        note: "Unsupported SBS graph output".to_owned(),
                    },
                    None,
                )
            }
        } else if input_bridge.is_some() {
            (
                NodeKind::Unknown {
                    filter: "input".to_owned(),
                    inputs: Vec::new(),
                    outputs: vec!["Out".to_owned()],
                    note: "SBS graph input".to_owned(),
                },
                None,
            )
        } else if let Some(filter) = filter {
            if filter.eq_ignore_ascii_case("unknown") {
                warnings.push("SBS unknown placeholder restored".to_owned());
            } else if !is_known_filter(&filter) {
                warnings.push(format!("SBS filter `{filter}` imported as a placeholder"));
            }
            (filter_kind(&filter, component), None)
        } else {
            warnings.push("SBS component without a filter imported as a placeholder".to_owned());
            (
                NodeKind::Unknown {
                    filter: "component".to_owned(),
                    inputs: Vec::new(),
                    outputs: Vec::new(),
                    note: "Missing compFilter".to_owned(),
                },
                None,
            )
        };

        let kind = match kind {
            NodeKind::Unknown {
                filter,
                mut inputs,
                mut outputs,
                note,
            } => {
                if outputs.is_empty() {
                    outputs = (0..output_uids.len().max(1))
                        .map(|index| {
                            if index == 0 {
                                "Out".to_owned()
                            } else {
                                format!("Out {}", index + 1)
                            }
                        })
                        .collect();
                }
                inputs.shrink_to_fit();
                unknown_nodes += 1;
                NodeKind::Unknown {
                    filter,
                    inputs,
                    outputs,
                    note,
                }
            }
            kind => kind,
        };

        let id = graph.add_node(kind, position);
        graph.next_id = graph.next_id.max(id + 1);
        imported.push(ImportedNode {
            id,
            uid,
            kind: graph.node(id).map(|node| node.kind.clone()).unwrap(),
            output_uids,
            bridge_port,
        });
    }

    let by_uid: HashMap<u64, usize> = imported
        .iter()
        .enumerate()
        .map(|(index, node)| (node.uid, index))
        .collect();

    for component in comp_nodes
        .into_iter()
        .flat_map(|nodes| xml_children(nodes, "compNode"))
    {
        let target_uid: u64 = xml_value(component, "uid")
            .and_then(|uid| uid.parse().ok())
            .unwrap_or(0);
        let Some(&target_index) = by_uid.get(&target_uid) else {
            continue;
        };
        let connections = xml_child(component, "connections");
        for connection in connections
            .into_iter()
            .flat_map(|connections| xml_children(connections, "connection"))
        {
            let identifier = xml_value(connection, "identifier").unwrap_or_default();
            let source_uid: u64 = xml_value(connection, "connRef")
                .and_then(|uid| uid.parse().ok())
                .unwrap_or(0);
            let source_output_uid: u64 = xml_value(connection, "connRefOutput")
                .and_then(|uid| uid.parse().ok())
                .unwrap_or(0);
            let Some(&source_index) = by_uid.get(&source_uid) else {
                continue;
            };
            let source_id = imported[source_index].id;
            let output = imported[source_index]
                .output_uids
                .iter()
                .position(|uid| *uid == source_output_uid)
                .unwrap_or(0);
            let target = &mut imported[target_index];
            let input = if let Some(port) = target.bridge_port {
                port
            } else if let NodeKind::Unknown { inputs, .. } = &mut target.kind {
                if let Some(index) = inputs
                    .iter()
                    .position(|label| label.eq_ignore_ascii_case(&identifier))
                {
                    index
                } else {
                    inputs.push(if identifier.is_empty() {
                        format!("In {}", inputs.len() + 1)
                    } else {
                        identifier
                    });
                    inputs.len() - 1
                }
            } else {
                input_index(&target.kind, &identifier).unwrap_or(0)
            };
            graph
                .links
                .retain(|link| !(link.to == target.id && link.input == input));
            graph
                .links
                .push(crate::uikit::components::node_canvas::Link {
                    from: source_id,
                    output,
                    to: target.id,
                    input,
                });
        }
    }

    // Push the patched unknown inputs back into the graph.
    for node in &imported {
        if let Some(target) = graph.node_mut(node.id) {
            target.kind = node.kind.clone();
        }
    }

    Ok(SbsImport {
        graph,
        graph_name,
        warnings,
        unknown_nodes,
    })
}

fn is_known_filter(filter: &str) -> bool {
    matches!(
        filter.to_lowercase().as_str(),
        "output"
            | "uniformcolor"
            | "solidcolor"
            | "solid_color"
            | "blur"
            | "blend"
            | "checker"
            | "checkerboard"
            | "bricks"
            | "brick"
            | "noise"
            | "perlin_noise"
            | "fractal_noise"
            | "fractal_sum_base"
            | "gradient"
            | "gradient_linear_1"
            | "gradient_linear"
            | "levels"
            | "hsl"
            | "threshold"
            | "mask"
            | "sharpen"
            | "emboss"
            | "directional_blur"
            | "directionalblur"
            | "warp"
            | "edge_detect"
            | "edgedetect"
            | "edge"
            | "curvature"
            | "cavity"
            | "cavity_mask"
            | "distance"
            | "distance_mask"
            | "normal_blend"
            | "normalblend"
            | "value"
            | "constant"
            | "luminance"
            | "image"
            | "normalmap"
            | "voronoi"
            | "curve"
            | "dot"
            | "unknown"
    )
}

/// Writes a minimal SBS package for the graph subset Doppio understands. This
/// is the source embedded in portable `.mcmat` exports and round-trips through
/// [`import_sbs`]; it is not presented as full Substance graph parity.
pub fn export_sbs(graph: &MaterialGraph, name: &str) -> String {
    let mut xml = String::from("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    xml.push_str("<package><identifier v=\"");
    xml.push_str(&xml_escape(name));
    xml.push_str("\"/><content><graph><identifier v=\"");
    xml.push_str(&xml_escape(name));
    xml.push_str("\"/><graphtype v=\"material\"/><compNodes>");

    let uid = |id: NodeId| 1000 + id * 16;
    for node in &graph.nodes {
        if matches!(node.kind, NodeKind::Output { .. }) {
            continue;
        }
        let node_uid = uid(node.id);
        xml.push_str("<compNode><uid v=\"");
        xml.push_str(&node_uid.to_string());
        xml.push_str("\"/><GUILayout><gpos v=\"");
        xml.push_str(&format!("{} {} 0", node.position.x, node.position.y));
        xml.push_str("\"/></GUILayout><compOutputs>");
        for index in 0..node.kind.outputs().len() {
            xml.push_str(&format!(
                "<compOutput><uid v=\"{}\"/><comptype v=\"2\"/></compOutput>",
                node_uid + 1 + index as u64
            ));
        }
        xml.push_str("</compOutputs><connections>");
        for link in graph.links.iter().filter(|link| link.to == node.id) {
            xml.push_str("<connection><identifier v=\"input");
            xml.push_str(&link.input.to_string());
            xml.push_str("\"/><connRef v=\"");
            xml.push_str(&uid(link.from).to_string());
            xml.push_str("\"/><connRefOutput v=\"");
            xml.push_str(&(uid(link.from) + 1 + link.output as u64).to_string());
            xml.push_str("\"/></connection>");
        }
        xml.push_str("</connections><compImplementation><compFilter><filter v=\"");
        xml.push_str(filter_name(&node.kind));
        xml.push_str("\"/><parameters>");
        write_parameters(&mut xml, &node.kind);
        xml.push_str("</parameters></compFilter></compImplementation></compNode>");
    }

    // Output nodes become a Doppio `output` component so the round-trip keeps
    // roughness/metallic/height/AO constants in one node.
    for node in &graph.nodes {
        if !matches!(node.kind, NodeKind::Output { .. }) {
            continue;
        }
        let node_uid = uid(node.id);
        xml.push_str("<compNode><uid v=\"");
        xml.push_str(&node_uid.to_string());
        xml.push_str("\"/><GUILayout><gpos v=\"");
        xml.push_str(&format!("{} {} 0", node.position.x, node.position.y));
        xml.push_str("\"/></GUILayout><connections>");
        for link in graph.links.iter().filter(|link| link.to == node.id) {
            xml.push_str("<connection><identifier v=\"input");
            xml.push_str(&link.input.to_string());
            xml.push_str("\"/><connRef v=\"");
            xml.push_str(&uid(link.from).to_string());
            xml.push_str("\"/><connRefOutput v=\"");
            xml.push_str(&(uid(link.from) + 1 + link.output as u64).to_string());
            xml.push_str("\"/></connection>");
        }
        xml.push_str(
            "</connections><compImplementation><compFilter><filter v=\"output\"/><parameters>",
        );
        write_parameters(&mut xml, &node.kind);
        xml.push_str("</parameters></compFilter></compImplementation></compNode>");
    }

    xml.push_str("</compNodes></graph></content></package>");
    xml
}

fn filter_name(kind: &NodeKind) -> &'static str {
    match kind {
        NodeKind::Color { .. } => "uniformcolor",
        NodeKind::NormalMap { .. } => "normalmap",
        NodeKind::Image { .. } => "image",
        NodeKind::NormalBlend { .. } => "normal_blend",
        NodeKind::Noise { noise_type, .. } => match noise_type {
            NoiseType::Perlin => "perlin_noise",
            NoiseType::Value => "noise",
        },
        NodeKind::Voronoi { .. } => "voronoi",
        NodeKind::Checker { .. } => "checker",
        NodeKind::Bricks { .. } => "bricks",
        NodeKind::Gradient { .. } => "gradient",
        NodeKind::Blend { .. } => "blend",
        NodeKind::Levels { .. } => "levels",
        NodeKind::Curve { .. } => "curve",
        NodeKind::Hsl { .. } => "hsl",
        NodeKind::Threshold { .. } => "threshold",
        NodeKind::Blur { .. } => "blur",
        NodeKind::Sharpen { .. } => "sharpen",
        NodeKind::Emboss { .. } => "emboss",
        NodeKind::DirectionalBlur { .. } => "directional_blur",
        NodeKind::EdgeMask { .. } => "edge_detect",
        NodeKind::CavityMask { .. } => "curvature",
        NodeKind::DistanceMask { .. } => "distance",
        NodeKind::Warp { .. } => "warp",
        NodeKind::Value { .. } => "value",
        NodeKind::Luminance => "luminance",
        NodeKind::Dot => "dot",
        NodeKind::Unknown { .. } => "unknown",
        NodeKind::Proc { .. } => "doppio_proc",
        NodeKind::Output { .. } => "output",
        NodeKind::TextureOutput { .. } => "output",
    }
}

fn write_parameters(xml: &mut String, kind: &NodeKind) {
    let mut write = |name: &str, value: String| {
        xml.push_str("<parameter><name v=\"");
        xml.push_str(name);
        xml.push_str("\"/><paramValue>");
        xml.push_str(&value);
        xml.push_str("</paramValue></parameter>");
    };
    match kind {
        NodeKind::Color { color } => write("outputcolor", float4(*color)),
        NodeKind::Image {
            path,
            projection,
            tiling,
            offset,
            filter,
            wrap,
        }
        | NodeKind::NormalMap {
            path,
            projection,
            tiling,
            offset,
            filter,
            wrap,
        } => {
            write("path", text(path));
            write("projection", int(projection_index(*projection)));
            write("tiling", float2(*tiling));
            write("offset", float2(*offset));
            write(
                "filter",
                int(if *filter == ImageFilter::Nearest {
                    1
                } else {
                    0
                }),
            );
            write("wrap", int(if *wrap == WrapMode::Clamp { 1 } else { 0 }));
        }
        NodeKind::NormalBlend { strength, mode } => {
            write("strength", float(*strength));
            write(
                "mode",
                int(if *mode == NormalBlendMode::Linear {
                    1
                } else {
                    0
                }),
            );
        }
        NodeKind::Noise {
            noise_type,
            scale,
            octaves,
            persistence,
            lacunarity,
            seed,
            color_a,
            color_b,
        } => {
            write(
                "noise_type",
                int(if *noise_type == NoiseType::Perlin {
                    1
                } else {
                    0
                }),
            );
            write("scale", float(*scale));
            write("octaves", int(*octaves));
            write("persistence", float(*persistence));
            write("lacunarity", float(*lacunarity));
            write("seed", int(*seed));
            write("color1", float4(*color_a));
            write("color2", float4(*color_b));
        }
        NodeKind::Voronoi {
            scale,
            jitter,
            seed,
            color_a,
            color_b,
            mode,
        } => {
            write("scale", float(*scale));
            write("jitter", float(*jitter));
            write("seed", int(*seed));
            write(
                "mode",
                int(
                    if *mode == crate::uikit::generators::VoronoiMode::Distance {
                        1
                    } else {
                        0
                    },
                ),
            );
            write("color1", float4(*color_a));
            write("color2", float4(*color_b));
        }
        NodeKind::Checker {
            scale,
            color_a,
            color_b,
        } => {
            write("scale", float(*scale));
            write("color1", float4(*color_a));
            write("color2", float4(*color_b));
        }
        NodeKind::Bricks {
            rows,
            columns,
            mortar,
            color_a,
            color_b,
            mortar_color,
        } => {
            write("numrows", int(*rows));
            write("numcols", int(*columns));
            write("mortar", float(*mortar));
            write("brickcolor1", float4(*color_a));
            write("brickcolor2", float4(*color_b));
            write("mortarcolor", float4(*mortar_color));
        }
        NodeKind::Gradient {
            angle,
            color_a,
            color_b,
        } => {
            write("angle", float(*angle));
            write("color1", float4(*color_a));
            write("color2", float4(*color_b));
        }
        NodeKind::Blend { factor, mode } => {
            write("opacitymult", float(*factor));
            write("blendingmode", int(blend_mode_index(*mode)));
        }
        NodeKind::Levels {
            brightness,
            contrast,
            gamma,
            invert,
        } => {
            write("brightness", float(*brightness));
            write("contrast", float(*contrast));
            write("gamma", float(*gamma));
            write("invert", bool_value(*invert));
        }
        NodeKind::Curve { points } => {
            for (index, point) in points.iter().enumerate() {
                write(&format!("point{index}"), float(*point));
            }
        }
        NodeKind::Hsl {
            hue,
            saturation,
            lightness,
        } => {
            write("hue", float(*hue));
            write("saturation", float(*saturation));
            write("lightness", float(*lightness));
        }
        NodeKind::Threshold {
            threshold,
            softness,
            invert,
        } => {
            write("threshold", float(*threshold));
            write("softness", float(*softness));
            write("invert", bool_value(*invert));
        }
        NodeKind::Blur { radius, wrap } => {
            write("radius", float(*radius));
            write("wrap", int(if *wrap == WrapMode::Clamp { 1 } else { 0 }));
        }
        NodeKind::Sharpen { amount, radius } => {
            write("amount", float(*amount));
            write("radius", float(*radius));
        }
        NodeKind::Emboss { strength, angle } => {
            write("strength", float(*strength));
            write("angle", float(*angle));
        }
        NodeKind::DirectionalBlur {
            radius,
            angle,
            wrap,
        } => {
            write("radius", float(*radius));
            write("angle", float(*angle));
            write("wrap", int(if *wrap == WrapMode::Clamp { 1 } else { 0 }));
        }
        NodeKind::EdgeMask { strength, invert } => {
            write("strength", float(*strength));
            write("invert", bool_value(*invert));
        }
        NodeKind::CavityMask {
            radius,
            strength,
            invert,
        } => {
            write("radius", float(*radius));
            write("strength", float(*strength));
            write("invert", bool_value(*invert));
        }
        NodeKind::DistanceMask {
            threshold,
            radius,
            invert,
        } => {
            write("threshold", float(*threshold));
            write("radius", float(*radius));
            write("invert", bool_value(*invert));
        }
        NodeKind::Warp {
            amount,
            scale,
            seed,
        } => {
            write("amount", float(*amount));
            write("scale", float(*scale));
            write("seed", int(*seed));
        }
        NodeKind::Value { value } => write("value", float(*value)),
        NodeKind::Unknown {
            filter,
            inputs,
            outputs,
            note,
        } => {
            write("originalfilter", text(filter));
            write("inputs", text(&inputs.join(";")));
            write("outputs", text(&outputs.join(";")));
            write("note", text(note));
        }
        NodeKind::Output {
            roughness,
            metallic,
            height,
            ao,
            ..
        } => {
            write("roughness", float(*roughness));
            write("metallic", float(*metallic));
            write("height", float(*height));
            write("ambientocclusion", float(*ao));
        }
        NodeKind::TextureOutput { .. } => {}
        NodeKind::Proc { op, params, colors } => {
            write("op", text(op.def().id));
            let numbers: Vec<String> = params.iter().map(|v| v.to_string()).collect();
            write("params", text(&numbers.join(" ")));
            let flat: Vec<String> = colors
                .iter()
                .flat_map(|c| c.iter().map(|v| v.to_string()))
                .collect();
            write("colors", text(&flat.join(" ")));
        }
        NodeKind::Luminance | NodeKind::Dot => {}
    }
}

fn projection_index(projection: Projection) -> i32 {
    Projection::ALL
        .iter()
        .position(|candidate| *candidate == projection)
        .unwrap_or(0) as i32
}

fn blend_mode_index(mode: BlendMode) -> i32 {
    BlendMode::ALL
        .iter()
        .position(|candidate| *candidate == mode)
        .unwrap_or(0) as i32
}

fn float(value: f32) -> String {
    format!("<constantValueFloat1 v=\"{value}\"/>")
}

fn float2(value: [f32; 2]) -> String {
    format!("<constantValueFloat2 v=\"{} {}\"/>", value[0], value[1])
}

fn float4(value: [f32; 4]) -> String {
    format!(
        "<constantValueFloat4 v=\"{} {} {} {}\"/>",
        value[0], value[1], value[2], value[3]
    )
}

fn int(value: i32) -> String {
    format!("<constantValueInt32 v=\"{value}\"/>")
}

fn bool_value(value: bool) -> String {
    format!("<constantValueBool v=\"{}\"/>", if value { 1 } else { 0 })
}

fn text(value: &str) -> String {
    format!("<constantValueString v=\"{}\"/>", xml_escape(value))
}

fn xml_escape(value: &str) -> String {
    value
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::uikit::components::node_canvas::Link;
    use iced::Point;

    fn sample_graph() -> MaterialGraph {
        let mut graph = MaterialGraph {
            resolution: 256,
            ..MaterialGraph::default()
        };
        graph.nodes[0].kind = NodeKind::Image {
            path: "textures/brick.png".to_owned(),
            projection: Projection::Cylindrical,
            tiling: [2.0, 3.0],
            offset: [0.1, -0.2],
            filter: ImageFilter::Nearest,
            wrap: WrapMode::Clamp,
        };
        let blur = graph.add_node_auto(NodeKind::Blur {
            radius: 3.0,
            wrap: WrapMode::Repeat,
        });
        let output = graph.output_node().expect("output");
        graph
            .links
            .retain(|link| link.to != output || link.input != 0);
        graph.links.push(Link {
            from: graph.nodes[0].id,
            output: 0,
            to: blur,
            input: 0,
        });
        graph.links.push(Link {
            from: blur,
            output: 0,
            to: output,
            input: 0,
        });
        graph.add_node_auto(NodeKind::Unknown {
            filter: "custom_substance_filter".to_owned(),
            inputs: vec!["Input".to_owned(), "Mask".to_owned()],
            outputs: vec!["Out".to_owned()],
            note: "preserved".to_owned(),
        });
        graph.add_node_auto(NodeKind::NormalMap {
            path: "textures/normal.png".to_owned(),
            projection: Projection::Spherical,
            tiling: [1.0, 1.0],
            offset: [0.0, 0.0],
            filter: ImageFilter::Linear,
            wrap: WrapMode::Repeat,
        });
        graph
    }

    #[test]
    fn sbs_round_trip_preserves_known_and_unknown_nodes() {
        let graph = sample_graph();
        let xml = export_sbs(&graph, "Brick");
        let imported = import_sbs(&xml, "Brick").expect("import");
        assert_eq!(imported.graph.nodes.len(), graph.nodes.len());
        assert_eq!(imported.graph.links.len(), graph.links.len());
        let mut titles: Vec<_> = graph.nodes.iter().map(|node| node.kind.title()).collect();
        let mut imported_titles: Vec<_> = imported
            .graph
            .nodes
            .iter()
            .map(|node| node.kind.title())
            .collect();
        titles.sort_unstable();
        imported_titles.sort_unstable();
        assert_eq!(titles, imported_titles);
        assert!(imported
            .graph
            .nodes
            .iter()
            .any(|node| matches!(node.kind, NodeKind::Unknown { .. })));
        assert!(imported
            .graph
            .nodes
            .iter()
            .any(|node| matches!(node.kind, NodeKind::NormalMap { .. })));
    }

    #[test]
    fn portable_material_round_trips_manifest_maps_and_source() {
        let manifest = Manifest::new(
            "Brick",
            "Brick",
            [2, 2],
            vec![MapEntry {
                usage: "baseColor".to_owned(),
                file: "maps/baseColor.png".to_owned(),
                color_space: "srgb".to_owned(),
            }],
        );
        let material = PortableMaterial {
            manifest,
            maps: vec![PortableMap {
                entry: MapEntry {
                    usage: "baseColor".to_owned(),
                    file: "maps/baseColor.png".to_owned(),
                    color_space: "srgb".to_owned(),
                },
                bytes: vec![1, 2, 3, 4],
            }],
            source: "<package/>".to_owned(),
        };
        let bytes = write_mcmat(&material).expect("write");
        let back = read_mcmat(&bytes).expect("read");
        assert_eq!(back.manifest.name, "Brick");
        assert_eq!(back.manifest.size, [2, 2]);
        assert_eq!(back.maps.len(), 1);
        assert_eq!(back.maps[0].bytes, vec![1, 2, 3, 4]);
        assert_eq!(back.source, "<package/>");
        assert!(read_mcmat(b"not a zip").is_err());
    }

    #[test]
    fn sbs_input_and_output_bridges_keep_links_and_usage_ports() {
        let xml = r#"<?xml version="1.0"?><package><content><graph><identifier v="Bridge"/><graphOutputs>
        <graphoutput><identifier v="height"/><uid v="300"/><usages><usage><name v="height"/></usage></usages></graphoutput>
        </graphOutputs><compNodes>
        <compNode><uid v="100"/><GUILayout><gpos v="0 0 0"/></GUILayout><compOutputs><compOutput><uid v="101"/></compOutput></compOutputs><compImplementation><compInputBridge><entry v="1000"/></compInputBridge></compImplementation></compNode>
        <compNode><uid v="200"/><GUILayout><gpos v="200 0 0"/></GUILayout><compOutputs><compOutput><uid v="201"/></compOutput></compOutputs><connections><connection><identifier v="input1"/><connRef v="100"/><connRefOutput v="101"/></connection></connections><compImplementation><compFilter><filter v="blur"/><parameters><parameter><name v="intensity"/><paramValue><constantValueFloat1 v="1"/></paramValue></parameter></parameters></compFilter></compImplementation></compNode>
        <compNode><uid v="400"/><GUILayout><gpos v="400 0 0"/></GUILayout><connections><connection><identifier v="inputNodeOutput"/><connRef v="200"/><connRefOutput v="201"/></connection></connections><compImplementation><compOutputBridge><output v="300"/></compOutputBridge></compImplementation></compNode>
        </compNodes></graph></content></package>"#;
        let imported = import_sbs(xml, "Bridge").expect("import");
        assert_eq!(imported.graph.nodes.len(), 3);
        assert_eq!(imported.graph.links.len(), 2);
        let output = imported.graph.output_node().expect("output");
        assert!(imported
            .graph
            .links
            .iter()
            .any(|link| link.to == output && link.input == 3));
        assert!(matches!(
            imported.graph.nodes[0].kind,
            NodeKind::Unknown { .. }
        ));
    }

    #[test]
    fn unknown_sbs_filters_are_placeholders_with_warning() {
        let xml = r#"<?xml version="1.0"?><package><content><graph><identifier v="X"/><compNodes>
        <compNode><uid v="1"/><GUILayout><gpos v="10 20 0"/></GUILayout><compImplementation><compFilter><filter v="mystery"/></compFilter></compImplementation></compNode>
        </compNodes></graph></content></package>"#;
        let imported = import_sbs(xml, "X").expect("import");
        assert_eq!(imported.unknown_nodes, 1);
        assert!(!imported.warnings.is_empty());
        let node = imported.graph.nodes.first().expect("node");
        assert_eq!(node.position, Point::new(10.0, 20.0));
        assert!(matches!(
            &node.kind,
            NodeKind::Unknown { filter, .. } if filter == "mystery"
        ));
    }
}

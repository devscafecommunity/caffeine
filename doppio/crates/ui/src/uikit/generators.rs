//! Procedural material graph used by the node generator.
//!
//! This is a clean-room, CPU evaluator for a small set of atomic pattern
//! generators and filters. It is intentionally independent from the canvas
//! component: the canvas only knows nodes, ports and links.

use iced::{Point, Vector};
use std::collections::{HashMap, HashSet};
use std::rc::Rc;

use crate::uikit::components::node_canvas::{self, Port, PortKind};
use crate::uikit::procedural::{self, ProcOp};

pub type NodeId = u64;
type Image = Rc<Vec<[f32; 4]>>;

/// Decoded RGBA8 texture used by Image nodes. The app preloads these on the
/// UI thread because the engine handle is not sent to the bake worker.
#[derive(Debug, Clone, PartialEq)]
pub struct TextureData {
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
}

pub type TextureMap = HashMap<String, TextureData>;

/// Decodes an image from disk into RGBA8. Runs on the bake worker so large
/// textures never stall the UI thread.
pub fn decode_texture(path: &str) -> Result<TextureData, String> {
    let bytes = std::fs::read(path).map_err(|error| format!("{path}: {error}"))?;
    let image = image::load_from_memory(&bytes)
        .map_err(|error| format!("{path}: {error}"))?
        .to_rgba8();
    Ok(TextureData {
        width: image.width(),
        height: image.height(),
        rgba: image.into_raw(),
    })
}

type CancelFn<'a> = dyn Fn() -> bool + 'a;
type ProgressFn<'a> = dyn FnMut(f32, &str) + 'a;

struct BakeContext<'a> {
    textures: &'a TextureMap,
    cancel: Option<&'a CancelFn<'a>>,
    progress: Option<&'a mut ProgressFn<'a>>,
    total: usize,
    done: usize,
    /// Secondary outputs of multi-output nodes, keyed by (node, output).
    extra: HashMap<(NodeId, usize), Image>,
}

impl<'a> BakeContext<'a> {
    fn check(&self) -> Result<(), String> {
        if self.cancel.is_some_and(|cancel| cancel()) {
            Err("Cancelled".to_owned())
        } else {
            Ok(())
        }
    }

    fn node_done(&mut self, label: &str) {
        self.done += 1;
        if let Some(progress) = self.progress.as_deref_mut() {
            let fraction = (self.done as f32 / self.total.max(1) as f32) * 0.9;
            progress(fraction, label);
        }
    }

    fn finish(&mut self) -> Result<(), String> {
        self.check()?;
        if let Some(progress) = self.progress.as_deref_mut() {
            progress(1.0, "Done");
        }
        Ok(())
    }

    /// Reports a named finalisation step after the graph nodes have run.
    fn stage(&mut self, fraction: f32, label: &str) {
        if let Some(progress) = self.progress.as_deref_mut() {
            progress(fraction, label);
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BlendMode {
    Normal,
    Multiply,
    Screen,
    Add,
    Overlay,
    Darken,
    Lighten,
    Difference,
    Subtract,
    SoftLight,
    HardLight,
    ColorDodge,
    ColorBurn,
}

impl BlendMode {
    pub const ALL: [BlendMode; 13] = [
        BlendMode::Normal,
        BlendMode::Multiply,
        BlendMode::Screen,
        BlendMode::Add,
        BlendMode::Overlay,
        BlendMode::Darken,
        BlendMode::Lighten,
        BlendMode::Difference,
        BlendMode::Subtract,
        BlendMode::SoftLight,
        BlendMode::HardLight,
        BlendMode::ColorDodge,
        BlendMode::ColorBurn,
    ];

    pub fn label(self) -> &'static str {
        match self {
            BlendMode::Normal => "Normal",
            BlendMode::Multiply => "Multiply",
            BlendMode::Screen => "Screen",
            BlendMode::Add => "Add",
            BlendMode::Overlay => "Overlay",
            BlendMode::Darken => "Darken",
            BlendMode::Lighten => "Lighten",
            BlendMode::Difference => "Difference",
            BlendMode::Subtract => "Subtract",
            BlendMode::SoftLight => "Soft light",
            BlendMode::HardLight => "Hard light",
            BlendMode::ColorDodge => "Color dodge",
            BlendMode::ColorBurn => "Color burn",
        }
    }

    pub fn from_index(index: usize) -> Self {
        Self::ALL.get(index).copied().unwrap_or(Self::Normal)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NoiseType {
    Value,
    Perlin,
}

impl NoiseType {
    pub fn label(self) -> &'static str {
        match self {
            NoiseType::Value => "Value",
            NoiseType::Perlin => "Perlin",
        }
    }

    pub fn from_index(index: usize) -> Self {
        if index == 1 {
            NoiseType::Perlin
        } else {
            NoiseType::Value
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VoronoiMode {
    Cells,
    Distance,
}

impl VoronoiMode {
    pub fn label(self) -> &'static str {
        match self {
            VoronoiMode::Cells => "Cells",
            VoronoiMode::Distance => "Distance",
        }
    }

    pub fn from_index(index: usize) -> Self {
        if index == 1 {
            VoronoiMode::Distance
        } else {
            VoronoiMode::Cells
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Projection {
    Uv,
    Planar,
    TriPlanar,
    Cylindrical,
    Spherical,
}

impl Projection {
    pub const ALL: [Projection; 5] = [
        Projection::Uv,
        Projection::Planar,
        Projection::TriPlanar,
        Projection::Cylindrical,
        Projection::Spherical,
    ];

    pub fn label(self) -> &'static str {
        match self {
            Projection::Uv => "UV",
            Projection::Planar => "Planar",
            Projection::TriPlanar => "Tri-planar",
            Projection::Cylindrical => "Cylindrical",
            Projection::Spherical => "Spherical",
        }
    }

    pub fn from_index(index: usize) -> Self {
        Self::ALL.get(index).copied().unwrap_or(Projection::Uv)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImageFilter {
    Linear,
    Nearest,
}

impl ImageFilter {
    pub fn label(self) -> &'static str {
        match self {
            ImageFilter::Linear => "Linear",
            ImageFilter::Nearest => "Nearest",
        }
    }

    pub fn from_index(index: usize) -> Self {
        if index == 1 {
            ImageFilter::Nearest
        } else {
            ImageFilter::Linear
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WrapMode {
    Repeat,
    Clamp,
}

impl WrapMode {
    pub fn label(self) -> &'static str {
        match self {
            WrapMode::Repeat => "Repeat",
            WrapMode::Clamp => "Clamp",
        }
    }

    pub fn from_index(index: usize) -> Self {
        if index == 1 {
            WrapMode::Clamp
        } else {
            WrapMode::Repeat
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NormalBlendMode {
    Whiteout,
    Linear,
}

impl NormalBlendMode {
    pub const ALL: [NormalBlendMode; 2] = [NormalBlendMode::Whiteout, NormalBlendMode::Linear];

    pub fn label(self) -> &'static str {
        match self {
            NormalBlendMode::Whiteout => "Whiteout",
            NormalBlendMode::Linear => "Linear",
        }
    }

    pub fn from_index(index: usize) -> Self {
        if index == 1 {
            NormalBlendMode::Linear
        } else {
            NormalBlendMode::Whiteout
        }
    }
}

/// Extra Output-node settings that decide how the height map is produced.
#[derive(Debug, Clone, PartialEq)]
pub struct OutputOptions {
    /// When no Height is connected, derive it from the base-colour luminance
    /// so the baked height (and the normal map built from it) is never flat.
    pub auto_height: bool,
    /// Stretch the final height to the full 0..1 range.
    pub normalize_height: bool,
    /// Contrast around mid-grey applied after normalising.
    pub height_contrast: f32,
}

impl Default for OutputOptions {
    fn default() -> Self {
        OutputOptions {
            auto_height: true,
            normalize_height: true,
            height_contrast: 1.0,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum NodeKind {
    Color {
        color: [f32; 4],
    },
    Image {
        path: String,
        projection: Projection,
        tiling: [f32; 2],
        offset: [f32; 2],
        filter: ImageFilter,
        wrap: WrapMode,
    },
    NormalMap {
        path: String,
        projection: Projection,
        tiling: [f32; 2],
        offset: [f32; 2],
        filter: ImageFilter,
        wrap: WrapMode,
    },
    NormalBlend {
        strength: f32,
        mode: NormalBlendMode,
    },
    Noise {
        noise_type: NoiseType,
        scale: f32,
        octaves: i32,
        persistence: f32,
        lacunarity: f32,
        seed: i32,
        color_a: [f32; 4],
        color_b: [f32; 4],
    },
    Voronoi {
        scale: f32,
        jitter: f32,
        seed: i32,
        color_a: [f32; 4],
        color_b: [f32; 4],
        mode: VoronoiMode,
    },
    Checker {
        scale: f32,
        color_a: [f32; 4],
        color_b: [f32; 4],
    },
    Bricks {
        rows: i32,
        columns: i32,
        mortar: f32,
        color_a: [f32; 4],
        color_b: [f32; 4],
        mortar_color: [f32; 4],
    },
    Gradient {
        angle: f32,
        color_a: [f32; 4],
        color_b: [f32; 4],
    },
    Blend {
        factor: f32,
        mode: BlendMode,
    },
    Levels {
        brightness: f32,
        contrast: f32,
        gamma: f32,
        invert: bool,
    },
    Curve {
        points: [f32; 5],
    },
    Hsl {
        hue: f32,
        saturation: f32,
        lightness: f32,
    },
    Threshold {
        threshold: f32,
        softness: f32,
        invert: bool,
    },
    Blur {
        radius: f32,
        wrap: WrapMode,
    },
    Sharpen {
        amount: f32,
        radius: f32,
    },
    Emboss {
        strength: f32,
        angle: f32,
    },
    DirectionalBlur {
        radius: f32,
        angle: f32,
        wrap: WrapMode,
    },
    EdgeMask {
        strength: f32,
        invert: bool,
    },
    CavityMask {
        radius: f32,
        strength: f32,
        invert: bool,
    },
    DistanceMask {
        threshold: f32,
        radius: f32,
        invert: bool,
    },
    Warp {
        amount: f32,
        scale: f32,
        seed: i32,
    },
    Value {
        value: f32,
    },
    Luminance,
    Dot,
    /// Imported MaterialCraft/SBS node the editor does not evaluate yet.
    Unknown {
        filter: String,
        inputs: Vec<String>,
        outputs: Vec<String>,
        note: String,
    },
    /// A node from the data-driven procedural library (`uikit::procedural`).
    Proc {
        op: ProcOp,
        params: Vec<f32>,
        colors: Vec<[f32; 4]>,
    },
    Output {
        roughness: f32,
        metallic: f32,
        height: f32,
        ao: f32,
        options: OutputOptions,
    },
    /// Result of a **Texture Generator** graph: one image, not a material. It never
    /// appears in the Material Generator's palette and the material baker ignores it.
    TextureOutput {
        mode: TextureOutputMode,
    },
}

/// How a Texture Output node turns its inputs into one RGBA image.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TextureOutputMode {
    /// Port 0 = colour (its own alpha is kept), port 1 = optional alpha mask.
    ColorAlpha,
    /// Ports 0..4 = R, G, B, A, each a greyscale mask (packed VFX / data textures).
    Channels,
}

impl TextureOutputMode {
    pub const ALL: [TextureOutputMode; 2] =
        [TextureOutputMode::ColorAlpha, TextureOutputMode::Channels];

    pub fn label(self) -> &'static str {
        match self {
            TextureOutputMode::ColorAlpha => "Colour + alpha",
            TextureOutputMode::Channels => "Packed channels (RGBA)",
        }
    }

    pub fn from_index(index: usize) -> Self {
        Self::ALL
            .get(index)
            .copied()
            .unwrap_or(TextureOutputMode::ColorAlpha)
    }

    pub fn index(self) -> usize {
        Self::ALL.iter().position(|mode| *mode == self).unwrap_or(0)
    }
}

impl NodeKind {
    pub fn title(&self) -> &'static str {
        match self {
            NodeKind::Color { .. } => "Color",
            NodeKind::Image { .. } => "Image",
            NodeKind::NormalMap { .. } => "Normal Map",
            NodeKind::NormalBlend { .. } => "Normal Blend",
            NodeKind::Noise { .. } => "Noise",
            NodeKind::Voronoi { .. } => "Voronoi",
            NodeKind::Checker { .. } => "Checker",
            NodeKind::Bricks { .. } => "Bricks",
            NodeKind::Gradient { .. } => "Gradient",
            NodeKind::Blend { .. } => "Blend",
            NodeKind::Levels { .. } => "Levels",
            NodeKind::Curve { .. } => "Curve",
            NodeKind::Hsl { .. } => "HSL",
            NodeKind::Threshold { .. } => "Threshold",
            NodeKind::Blur { .. } => "Blur",
            NodeKind::Sharpen { .. } => "Sharpen",
            NodeKind::Emboss { .. } => "Emboss",
            NodeKind::DirectionalBlur { .. } => "Directional Blur",
            NodeKind::EdgeMask { .. } => "Edge Mask",
            NodeKind::CavityMask { .. } => "Cavity Mask",
            NodeKind::DistanceMask { .. } => "Distance Mask",
            NodeKind::Warp { .. } => "Warp",
            NodeKind::Value { .. } => "Value",
            NodeKind::Luminance => "Luminance",
            NodeKind::Dot => "Dot",
            NodeKind::Unknown { .. } => "Unknown",
            NodeKind::Proc { op, .. } => op.def().title,
            NodeKind::Output { .. } => "Output",
            NodeKind::TextureOutput { .. } => "Texture Output",
        }
    }

    pub fn category(&self) -> &'static str {
        match self {
            NodeKind::Image { .. } | NodeKind::NormalMap { .. } => "Textures",
            NodeKind::Color { .. }
            | NodeKind::Noise { .. }
            | NodeKind::Voronoi { .. }
            | NodeKind::Checker { .. }
            | NodeKind::Bricks { .. }
            | NodeKind::Gradient { .. } => "Generators",
            NodeKind::Blend { .. }
            | NodeKind::NormalBlend { .. }
            | NodeKind::Levels { .. }
            | NodeKind::Curve { .. }
            | NodeKind::Hsl { .. }
            | NodeKind::Threshold { .. }
            | NodeKind::Blur { .. }
            | NodeKind::Sharpen { .. }
            | NodeKind::Emboss { .. }
            | NodeKind::DirectionalBlur { .. }
            | NodeKind::EdgeMask { .. }
            | NodeKind::CavityMask { .. }
            | NodeKind::DistanceMask { .. }
            | NodeKind::Warp { .. } => "Filters",
            NodeKind::Value { .. } | NodeKind::Luminance | NodeKind::Dot => "Utilities",
            NodeKind::Unknown { .. } => "Imported",
            NodeKind::Proc { op, .. } => op.def().category,
            NodeKind::Output { .. } => "Output",
            NodeKind::TextureOutput { .. } => "Output",
        }
    }

    pub fn accent(&self) -> iced::Color {
        match self {
            NodeKind::Color { .. } => crate::uikit::theme::ICON_PURPLE,
            NodeKind::Image { .. } => crate::uikit::theme::ICON_CYAN,
            NodeKind::NormalMap { .. } => crate::uikit::theme::ICON_PURPLE,
            NodeKind::NormalBlend { .. } => crate::uikit::theme::ICON_TEAL,
            NodeKind::Noise { .. } => crate::uikit::theme::ACCENT,
            NodeKind::Voronoi { .. } => crate::uikit::theme::WARNING,
            NodeKind::Checker { .. } => crate::uikit::theme::ICON_TEAL,
            NodeKind::Bricks { .. } => crate::uikit::theme::WARNING,
            NodeKind::Gradient { .. } => crate::uikit::theme::ICON_CYAN,
            NodeKind::Blend { .. } => crate::uikit::theme::SUCCESS,
            NodeKind::Levels { .. }
            | NodeKind::Curve { .. }
            | NodeKind::Hsl { .. }
            | NodeKind::Threshold { .. }
            | NodeKind::Blur { .. }
            | NodeKind::Sharpen { .. }
            | NodeKind::Emboss { .. }
            | NodeKind::DirectionalBlur { .. }
            | NodeKind::EdgeMask { .. }
            | NodeKind::CavityMask { .. }
            | NodeKind::DistanceMask { .. }
            | NodeKind::Warp { .. }
            | NodeKind::Value { .. }
            | NodeKind::Luminance
            | NodeKind::Dot => crate::uikit::theme::TEXT_MUTED,
            NodeKind::Unknown { .. } => crate::uikit::theme::WARNING,
            NodeKind::Proc { op, .. } => match op.def().category {
                "Noise" => crate::uikit::theme::ACCENT,
                "Natural" => crate::uikit::theme::SUCCESS,
                "Patterns" => crate::uikit::theme::WARNING,
                "Shapes" => crate::uikit::theme::ICON_CYAN,
                "Math" => crate::uikit::theme::TEXT_MUTED,
                "Geometry" => crate::uikit::theme::ICON_PURPLE,
                _ => crate::uikit::theme::ICON_TEAL,
            },
            NodeKind::Output { .. } => crate::uikit::theme::SUCCESS,
            NodeKind::TextureOutput { .. } => crate::uikit::theme::ICON_ORANGE,
        }
    }

    pub fn inputs(&self) -> Vec<Port> {
        let color = |label: &str| Port {
            label: label.to_owned(),
            kind: PortKind::Color,
        };
        let scalar = |label: &str| Port {
            label: label.to_owned(),
            kind: PortKind::Scalar,
        };
        match self {
            NodeKind::Blend { .. } => {
                vec![color("Base"), color("Blend"), scalar("Mask")]
            }
            NodeKind::NormalBlend { .. } => vec![color("Base"), color("Detail")],
            NodeKind::Levels { .. }
            | NodeKind::Curve { .. }
            | NodeKind::Hsl { .. }
            | NodeKind::Threshold { .. }
            | NodeKind::Blur { .. }
            | NodeKind::Sharpen { .. }
            | NodeKind::Emboss { .. }
            | NodeKind::DirectionalBlur { .. }
            | NodeKind::EdgeMask { .. }
            | NodeKind::CavityMask { .. }
            | NodeKind::DistanceMask { .. }
            | NodeKind::Warp { .. }
            | NodeKind::Luminance => vec![color("Input")],
            NodeKind::Dot => vec![color("In")],
            NodeKind::Unknown { inputs, .. } => inputs.iter().map(|label| color(label)).collect(),
            NodeKind::Proc { op, .. } => op
                .def()
                .inputs
                .iter()
                .map(|port| Port {
                    label: port.label.to_owned(),
                    kind: port.kind(),
                })
                .collect(),
            // Ports 0..4 keep their order for older graphs; new ones are appended.
            NodeKind::Output { .. } => vec![
                color("Color"),
                scalar("Roughness"),
                scalar("Metallic"),
                scalar("Height"),
                scalar("AO"),
                color("Emissive"),
                scalar("Opacity"),
                color("Normal"),
            ],
            NodeKind::TextureOutput { mode } => match mode {
                TextureOutputMode::ColorAlpha => vec![color("Color"), scalar("Alpha")],
                TextureOutputMode::Channels => {
                    vec![scalar("R"), scalar("G"), scalar("B"), scalar("A")]
                }
            },
            _ => Vec::new(),
        }
    }

    pub fn outputs(&self) -> Vec<Port> {
        match self {
            NodeKind::Value { .. }
            | NodeKind::Luminance
            | NodeKind::Threshold { .. }
            | NodeKind::EdgeMask { .. }
            | NodeKind::CavityMask { .. }
            | NodeKind::DistanceMask { .. } => {
                vec![Port {
                    label: "Value".to_owned(),
                    kind: PortKind::Scalar,
                }]
            }
            NodeKind::Dot => vec![Port {
                label: "Out".to_owned(),
                kind: PortKind::Color,
            }],
            NodeKind::Unknown { outputs, .. } => outputs
                .iter()
                .map(|label| Port {
                    label: label.clone(),
                    kind: PortKind::Color,
                })
                .collect(),
            NodeKind::Proc { op, .. } => op
                .def()
                .outputs
                .iter()
                .map(|port| Port {
                    label: port.label.to_owned(),
                    kind: port.kind(),
                })
                .collect(),
            NodeKind::Output { .. } | NodeKind::TextureOutput { .. } => Vec::new(),
            _ => vec![Port {
                label: "Color".to_owned(),
                kind: PortKind::Color,
            }],
        }
    }

    pub fn subtitle(&self) -> String {
        match self {
            NodeKind::Color { .. } => "constant".to_owned(),
            NodeKind::Image {
                path, projection, ..
            }
            | NodeKind::NormalMap {
                path, projection, ..
            } => {
                let name = std::path::Path::new(path)
                    .file_name()
                    .and_then(|name| name.to_str())
                    .unwrap_or("no texture");
                format!("{} · {}", short_name(name), projection.label())
            }
            NodeKind::NormalBlend { strength, mode } => {
                format!("{} {strength:.2}", mode.label())
            }
            NodeKind::Noise {
                noise_type,
                scale,
                octaves,
                ..
            } => format!("{} · {scale:.1} · {octaves}", noise_type.label()),
            NodeKind::Voronoi { scale, mode, .. } => {
                format!("{} · {scale:.1}", mode.label())
            }
            NodeKind::Checker { scale, .. } => format!("{scale:.1}"),
            NodeKind::Bricks { rows, columns, .. } => format!("{columns}×{rows}"),
            NodeKind::Gradient { angle, .. } => format!("{angle:.0}°"),
            NodeKind::Blend { factor, mode } => {
                format!("{} {factor:.2}", mode.label())
            }
            NodeKind::Levels {
                brightness,
                contrast,
                gamma,
                ..
            } => format!("{brightness:+.1} {contrast:.1} γ{gamma:.1}"),
            NodeKind::Curve { .. } => "5-point".to_owned(),
            NodeKind::Hsl {
                hue,
                saturation,
                lightness,
            } => format!("{hue:.0}° {saturation:.1} {lightness:.1}"),
            NodeKind::Threshold {
                threshold,
                softness,
                ..
            } => format!("{threshold:.2} ±{softness:.2}"),
            NodeKind::Blur { radius, .. } => format!("{radius:.0}px"),
            NodeKind::Sharpen { amount, radius } => format!("{amount:.2} r{radius:.0}"),
            NodeKind::Emboss { strength, angle } => format!("{strength:.2} {angle:.0}°"),
            NodeKind::DirectionalBlur { radius, angle, .. } => {
                format!("{radius:.0}px {angle:.0}°")
            }
            NodeKind::EdgeMask { strength, .. } => format!("{strength:.2}"),
            NodeKind::CavityMask {
                radius, strength, ..
            } => {
                format!("{radius:.0}px {strength:.2}")
            }
            NodeKind::DistanceMask {
                threshold, radius, ..
            } => format!("{threshold:.2} · {radius:.0}px"),
            NodeKind::Warp { amount, .. } => format!("{amount:.2}"),
            NodeKind::Value { value } => format!("{value:.2}"),
            NodeKind::Luminance => "luma".to_owned(),
            NodeKind::Dot => "reroute".to_owned(),
            NodeKind::Unknown { filter, note, .. } => {
                if note.is_empty() {
                    short_name(filter)
                } else {
                    format!("{} · {}", short_name(filter), short_name(note))
                }
            }
            NodeKind::Proc { op, params, .. } => proc_subtitle(*op, params),
            NodeKind::Output {
                roughness,
                metallic,
                height,
                ..
            } => format!("R{roughness:.2} M{metallic:.2} H{height:.2}"),
            NodeKind::TextureOutput { mode } => mode.label().to_owned(),
        }
    }

    /// A procedural node with the op's default parameters and colours.
    pub fn proc_default(op: ProcOp) -> NodeKind {
        NodeKind::Proc {
            op,
            params: op.default_params(),
            colors: op.default_colors(),
        }
    }

    /// Builds a procedural node from saved values, padding/truncating to the
    /// op's current parameter list so older files keep loading.
    pub fn proc_from_saved(op: ProcOp, params: &[f32], colors: &[[f32; 4]]) -> NodeKind {
        let mut p = op.default_params();
        for (slot, value) in p.iter_mut().zip(params.iter()) {
            *slot = *value;
        }
        let mut c = op.default_colors();
        for (slot, value) in c.iter_mut().zip(colors.iter()) {
            *slot = *value;
        }
        NodeKind::Proc {
            op,
            params: p,
            colors: c,
        }
    }

    pub fn default_color() -> [f32; 4] {
        [0.75, 0.75, 0.75, 1.0]
    }
}

fn proc_subtitle(op: ProcOp, params: &[f32]) -> String {
    let mut parts = Vec::new();
    for (spec, value) in op.def().params.iter().zip(params.iter()) {
        if parts.len() >= 2 {
            break;
        }
        match spec.kind {
            procedural::ParamKind::Float => parts.push(format!("{} {:.2}", spec.key, value)),
            procedural::ParamKind::Int => {
                parts.push(format!("{} {}", spec.key, value.round() as i32))
            }
            procedural::ParamKind::Enum(options) => parts.push(
                options
                    .get(value.round() as usize)
                    .copied()
                    .unwrap_or("?")
                    .to_owned(),
            ),
            procedural::ParamKind::Bool => {}
        }
    }
    parts.join(" · ")
}

#[allow(clippy::too_many_arguments)]
fn node_input(
    graph: &MaterialGraph,
    node: NodeId,
    port: usize,
    resolution: usize,
    memo: &mut HashMap<NodeId, Image>,
    stack: &mut Vec<NodeId>,
    ctx: &mut BakeContext,
    fallback: [f32; 4],
) -> Result<Image, String> {
    Ok(graph
        .input_image(node, port, resolution, memo, stack, ctx)?
        .unwrap_or_else(|| Rc::new(vec![fallback; resolution * resolution])))
}

#[derive(Debug, Clone, PartialEq)]
pub struct GeneratorNode {
    pub id: NodeId,
    pub kind: NodeKind,
    pub position: Point,
}

#[derive(Debug, Clone, PartialEq)]
pub struct MaterialGraph {
    pub nodes: Vec<GeneratorNode>,
    pub links: Vec<node_canvas::Link>,
    pub next_id: NodeId,
    pub resolution: u32,
}

impl Default for MaterialGraph {
    fn default() -> Self {
        let mut graph = Self {
            nodes: Vec::new(),
            links: Vec::new(),
            next_id: 1,
            resolution: 512,
        };
        let noise = graph.add_node(
            NodeKind::Noise {
                noise_type: NoiseType::Value,
                scale: 6.0,
                octaves: 4,
                persistence: 0.5,
                lacunarity: 2.0,
                seed: 1,
                color_a: [0.08, 0.06, 0.04, 1.0],
                color_b: [0.72, 0.58, 0.38, 1.0],
            },
            Point::new(-170.0, -40.0),
        );
        let output = graph.add_node(
            NodeKind::Output {
                roughness: 0.5,
                metallic: 0.0,
                height: 0.5,
                ao: 1.0,
                options: OutputOptions::default(),
            },
            Point::new(110.0, -60.0),
        );
        graph.links.push(node_canvas::Link {
            from: noise,
            output: 0,
            to: output,
            input: 0,
        });
        graph
    }
}

impl MaterialGraph {
    pub fn add_node(&mut self, kind: NodeKind, position: Point) -> NodeId {
        let id = self.next_id;
        self.next_id += 1;
        self.nodes.push(GeneratorNode { id, kind, position });
        id
    }

    pub fn add_node_auto(&mut self, kind: NodeKind) -> NodeId {
        let index = self.nodes.len() as f32;
        self.add_node(kind, Point::new(-60.0 + index * 26.0, 40.0 + index * 20.0))
    }

    pub fn node(&self, id: NodeId) -> Option<&GeneratorNode> {
        self.nodes.iter().find(|node| node.id == id)
    }

    pub fn node_mut(&mut self, id: NodeId) -> Option<&mut GeneratorNode> {
        self.nodes.iter_mut().find(|node| node.id == id)
    }

    pub fn output_node(&self) -> Option<NodeId> {
        self.nodes
            .iter()
            .find(|node| matches!(node.kind, NodeKind::Output { .. }))
            .map(|node| node.id)
    }

    /// The Texture Generator's result node, if the graph has one.
    pub fn texture_output_node(&self) -> Option<NodeId> {
        self.nodes
            .iter()
            .find(|node| matches!(node.kind, NodeKind::TextureOutput { .. }))
            .map(|node| node.id)
    }

    pub fn remove_node(&mut self, id: NodeId) {
        self.nodes.retain(|node| node.id != id);
        self.links.retain(|link| link.from != id && link.to != id);
    }

    pub fn connect(&mut self, from: NodeId, output: usize, to: NodeId, input: usize) -> bool {
        if from == to || self.would_cycle(from, to) {
            return false;
        }
        let Some(source) = self.node(from) else {
            return false;
        };
        if output >= source.kind.outputs().len() {
            return false;
        }
        let Some(target) = self.node(to) else {
            return false;
        };
        if input >= target.kind.inputs().len() {
            return false;
        }
        self.links
            .retain(|link| !(link.to == to && link.input == input));
        self.links.push(node_canvas::Link {
            from,
            output,
            to,
            input,
        });
        true
    }

    fn would_cycle(&self, from: NodeId, to: NodeId) -> bool {
        let mut stack = vec![to];
        let mut seen = HashSet::new();
        while let Some(node) = stack.pop() {
            if node == from {
                return true;
            }
            if !seen.insert(node) {
                continue;
            }
            stack.extend(
                self.links
                    .iter()
                    .filter(|link| link.from == node)
                    .map(|link| link.to),
            );
        }
        false
    }

    pub fn canvas_nodes(
        &self,
        selected: Option<NodeId>,
        previews: &HashMap<NodeId, iced::widget::image::Handle>,
    ) -> Vec<node_canvas::Node> {
        self.nodes
            .iter()
            .map(|node| node_canvas::Node {
                id: node.id,
                title: node.kind.title().to_owned(),
                subtitle: node.kind.subtitle(),
                position: node.position,
                inputs: node.kind.inputs(),
                outputs: node.kind.outputs(),
                accent: node.kind.accent(),
                selected: selected == Some(node.id),
                output: matches!(
                    node.kind,
                    NodeKind::Output { .. } | NodeKind::TextureOutput { .. }
                ),
                preview: previews.get(&node.id).cloned(),
            })
            .collect()
    }

    pub fn to_text(&self) -> String {
        let mut text = format!("CAFMGRAPH2\nresolution {}\n", self.resolution);
        for node in &self.nodes {
            let color = |c: [f32; 4]| format!("{:.5} {:.5} {:.5} {:.5}", c[0], c[1], c[2], c[3]);
            let line = match &node.kind {
                NodeKind::Color { color: c } => {
                    format!(
                        "node {} {} {} color {}",
                        node.id,
                        node.position.x,
                        node.position.y,
                        color(*c)
                    )
                }
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
                    let label = if matches!(node.kind, NodeKind::NormalMap { .. }) {
                        "normalmap"
                    } else {
                        "image"
                    };
                    format!(
                        "node {} {} {} {} {} {} {} {} {} {} {} {}",
                        node.id,
                        node.position.x,
                        node.position.y,
                        label,
                        Projection::ALL
                            .iter()
                            .position(|item| *item == *projection)
                            .unwrap_or(0),
                        tiling[0],
                        tiling[1],
                        offset[0],
                        offset[1],
                        if *filter == ImageFilter::Nearest {
                            1
                        } else {
                            0
                        },
                        if *wrap == WrapMode::Clamp { 1 } else { 0 },
                        path.replace(' ', "%20")
                    )
                }
                NodeKind::NormalBlend { strength, mode } => format!(
                    "node {} {} {} normalblend {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    strength,
                    if *mode == NormalBlendMode::Linear {
                        1
                    } else {
                        0
                    }
                ),
                NodeKind::Noise {
                    noise_type,
                    scale,
                    octaves,
                    persistence,
                    lacunarity,
                    seed,
                    color_a,
                    color_b,
                } => format!(
                    "node {} {} {} noise {} {} {} {} {} {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    if *noise_type == NoiseType::Perlin {
                        1
                    } else {
                        0
                    },
                    scale,
                    octaves,
                    persistence,
                    lacunarity,
                    seed,
                    color(*color_a),
                    color(*color_b)
                ),
                NodeKind::Voronoi {
                    scale,
                    jitter,
                    seed,
                    color_a,
                    color_b,
                    mode,
                } => format!(
                    "node {} {} {} voronoi {} {} {} {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    scale,
                    jitter,
                    seed,
                    if *mode == VoronoiMode::Distance { 1 } else { 0 },
                    color(*color_a),
                    color(*color_b)
                ),
                NodeKind::Checker {
                    scale,
                    color_a,
                    color_b,
                } => format!(
                    "node {} {} {} checker {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    scale,
                    color(*color_a),
                    color(*color_b)
                ),
                NodeKind::Bricks {
                    rows,
                    columns,
                    mortar,
                    color_a,
                    color_b,
                    mortar_color,
                } => format!(
                    "node {} {} {} bricks {} {} {} {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    rows,
                    columns,
                    mortar,
                    color(*color_a),
                    color(*color_b),
                    color(*mortar_color)
                ),
                NodeKind::Gradient {
                    angle,
                    color_a,
                    color_b,
                } => format!(
                    "node {} {} {} gradient {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    angle,
                    color(*color_a),
                    color(*color_b)
                ),
                NodeKind::Blend { factor, mode } => format!(
                    "node {} {} {} blend {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    factor,
                    blend_index(*mode)
                ),
                NodeKind::Levels {
                    brightness,
                    contrast,
                    gamma,
                    invert,
                } => format!(
                    "node {} {} {} levels {} {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    brightness,
                    contrast,
                    gamma,
                    if *invert { 1 } else { 0 }
                ),
                NodeKind::Curve { points } => format!(
                    "node {} {} {} curve {} {} {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    points[0],
                    points[1],
                    points[2],
                    points[3],
                    points[4]
                ),
                NodeKind::Hsl {
                    hue,
                    saturation,
                    lightness,
                } => format!(
                    "node {} {} {} hsl {} {} {}",
                    node.id, node.position.x, node.position.y, hue, saturation, lightness
                ),
                NodeKind::Threshold {
                    threshold,
                    softness,
                    invert,
                } => format!(
                    "node {} {} {} threshold {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    threshold,
                    softness,
                    if *invert { 1 } else { 0 }
                ),
                NodeKind::Blur { radius, wrap } => format!(
                    "node {} {} {} blur {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    radius,
                    if *wrap == WrapMode::Clamp { 1 } else { 0 }
                ),
                NodeKind::Sharpen { amount, radius } => format!(
                    "node {} {} {} sharpen {} {}",
                    node.id, node.position.x, node.position.y, amount, radius
                ),
                NodeKind::Emboss { strength, angle } => format!(
                    "node {} {} {} emboss {} {}",
                    node.id, node.position.x, node.position.y, strength, angle
                ),
                NodeKind::DirectionalBlur {
                    radius,
                    angle,
                    wrap,
                } => format!(
                    "node {} {} {} directionalblur {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    radius,
                    angle,
                    if *wrap == WrapMode::Clamp { 1 } else { 0 }
                ),
                NodeKind::EdgeMask { strength, invert } => format!(
                    "node {} {} {} edgemask {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    strength,
                    if *invert { 1 } else { 0 }
                ),
                NodeKind::CavityMask {
                    radius,
                    strength,
                    invert,
                } => format!(
                    "node {} {} {} cavitymask {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    radius,
                    strength,
                    if *invert { 1 } else { 0 }
                ),
                NodeKind::DistanceMask {
                    threshold,
                    radius,
                    invert,
                } => format!(
                    "node {} {} {} distancemask {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    threshold,
                    radius,
                    if *invert { 1 } else { 0 }
                ),
                NodeKind::Warp {
                    amount,
                    scale,
                    seed,
                } => format!(
                    "node {} {} {} warp {} {} {}",
                    node.id, node.position.x, node.position.y, amount, scale, seed
                ),
                NodeKind::Value { value } => format!(
                    "node {} {} {} value {}",
                    node.id, node.position.x, node.position.y, value
                ),
                NodeKind::Luminance => format!(
                    "node {} {} {} luminance",
                    node.id, node.position.x, node.position.y
                ),
                NodeKind::Dot => format!(
                    "node {} {} {} dot",
                    node.id, node.position.x, node.position.y
                ),
                NodeKind::Unknown {
                    filter,
                    inputs,
                    outputs,
                    note,
                } => format!(
                    "node {} {} {} unknown {} {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    escape_token(filter),
                    escape_list(inputs),
                    escape_list(outputs),
                    escape_token(note)
                ),
                NodeKind::Proc { op, params, colors } => {
                    let mut text = format!(
                        "node {} {} {} proc {} {}",
                        node.id,
                        node.position.x,
                        node.position.y,
                        op.def().id,
                        params.len()
                    );
                    for value in params {
                        text.push_str(&format!(" {value}"));
                    }
                    text.push_str(&format!(" {}", colors.len()));
                    for c in colors {
                        text.push_str(&format!(" {} {} {} {}", c[0], c[1], c[2], c[3]));
                    }
                    text
                }
                NodeKind::Output {
                    roughness,
                    metallic,
                    height,
                    ao,
                    options,
                } => format!(
                    "node {} {} {} output {} {} {} {} {} {} {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    roughness,
                    metallic,
                    height,
                    ao,
                    options.auto_height as i32,
                    options.normalize_height as i32,
                    options.height_contrast
                ),
                NodeKind::TextureOutput { mode } => format!(
                    "node {} {} {} texoutput {}",
                    node.id,
                    node.position.x,
                    node.position.y,
                    mode.index()
                ),
            };
            text.push_str(&line);
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

    pub fn from_text(text: &str) -> Option<Self> {
        let mut lines = text.lines();
        let header = lines.next()?;
        if !header.starts_with("CAFMGRAPH") {
            return None;
        }
        let version2 = header != "CAFMGRAPH1";
        let mut graph = Self {
            nodes: Vec::new(),
            links: Vec::new(),
            next_id: 1,
            resolution: 512,
        };
        let color = |index: usize, words: &[&str]| -> Option<[f32; 4]> {
            Some([
                words.get(index)?.parse().ok()?,
                words.get(index + 1)?.parse().ok()?,
                words.get(index + 2)?.parse().ok()?,
                words.get(index + 3)?.parse().ok()?,
            ])
        };
        for line in lines {
            let words: Vec<&str> = line.split_whitespace().collect();
            if words.is_empty() {
                continue;
            }
            match words[0] {
                "resolution" => {
                    if let Some(value) = words.get(1).and_then(|value| value.parse::<u32>().ok()) {
                        graph.resolution = value.clamp(64, 2048);
                    }
                }
                "node" if words.len() >= 5 => {
                    let id = words[1].parse::<NodeId>().ok()?;
                    let position =
                        Point::new(words[2].parse::<f32>().ok()?, words[3].parse::<f32>().ok()?);
                    let kind = match words[4] {
                        "color" => NodeKind::Color {
                            color: color(5, &words)?,
                        },
                        "image" | "normalmap" => {
                            let kind = NodeKind::Image {
                                projection: Projection::from_index(
                                    words.get(5)?.parse::<usize>().ok()?,
                                ),
                                tiling: [words.get(6)?.parse().ok()?, words.get(7)?.parse().ok()?],
                                offset: [words.get(8)?.parse().ok()?, words.get(9)?.parse().ok()?],
                                filter: ImageFilter::from_index(
                                    words.get(10)?.parse::<usize>().ok()?,
                                ),
                                wrap: WrapMode::from_index(words.get(11)?.parse::<usize>().ok()?),
                                path: words
                                    .get(12..)
                                    .unwrap_or_default()
                                    .join(" ")
                                    .replace("%20", " "),
                            };
                            if words[4] == "normalmap" {
                                let NodeKind::Image {
                                    path,
                                    projection,
                                    tiling,
                                    offset,
                                    filter,
                                    wrap,
                                } = kind
                                else {
                                    unreachable!()
                                };
                                NodeKind::NormalMap {
                                    path,
                                    projection,
                                    tiling,
                                    offset,
                                    filter,
                                    wrap,
                                }
                            } else {
                                kind
                            }
                        }
                        "normalblend" => NodeKind::NormalBlend {
                            strength: words.get(5)?.parse().ok()?,
                            mode: NormalBlendMode::from_index(words.get(6)?.parse::<usize>().ok()?),
                        },
                        "noise" if version2 => NodeKind::Noise {
                            noise_type: NoiseType::from_index(words.get(5)?.parse::<usize>().ok()?),
                            scale: words.get(6)?.parse().ok()?,
                            octaves: words.get(7)?.parse().ok()?,
                            persistence: words.get(8)?.parse().ok()?,
                            lacunarity: words.get(9)?.parse().ok()?,
                            seed: words.get(10)?.parse().ok()?,
                            color_a: color(11, &words)?,
                            color_b: color(15, &words)?,
                        },
                        "noise" => NodeKind::Noise {
                            noise_type: NoiseType::Value,
                            scale: words.get(5)?.parse().ok()?,
                            octaves: words.get(6)?.parse().ok()?,
                            persistence: 0.5,
                            lacunarity: 2.0,
                            seed: words.get(7)?.parse().ok()?,
                            color_a: color(8, &words)?,
                            color_b: color(12, &words)?,
                        },
                        "voronoi" => NodeKind::Voronoi {
                            scale: words.get(5)?.parse().ok()?,
                            jitter: words.get(6)?.parse().ok()?,
                            seed: words.get(7)?.parse().ok()?,
                            mode: VoronoiMode::from_index(words.get(8)?.parse::<usize>().ok()?),
                            color_a: color(9, &words)?,
                            color_b: color(13, &words)?,
                        },
                        "checker" => NodeKind::Checker {
                            scale: words.get(5)?.parse().ok()?,
                            color_a: color(6, &words)?,
                            color_b: color(10, &words)?,
                        },
                        "bricks" => NodeKind::Bricks {
                            rows: words.get(5)?.parse().ok()?,
                            columns: words.get(6)?.parse().ok()?,
                            mortar: words.get(7)?.parse().ok()?,
                            color_a: color(8, &words)?,
                            color_b: color(12, &words)?,
                            mortar_color: color(16, &words)?,
                        },
                        "gradient" => NodeKind::Gradient {
                            angle: words.get(5)?.parse().ok()?,
                            color_a: color(6, &words)?,
                            color_b: color(10, &words)?,
                        },
                        "blend" => NodeKind::Blend {
                            factor: words.get(5)?.parse().ok()?,
                            mode: BlendMode::from_index(words.get(6)?.parse().ok()?),
                        },
                        "levels" => NodeKind::Levels {
                            brightness: words.get(5)?.parse().ok()?,
                            contrast: words.get(6)?.parse().ok()?,
                            gamma: words
                                .get(7)
                                .and_then(|value| value.parse().ok())
                                .unwrap_or(1.0),
                            invert: words.get(8).is_some_and(|value| *value == "1"),
                        },
                        "curve" => {
                            let mut points = [0.0f32; 5];
                            for (index, point) in points.iter_mut().enumerate() {
                                *point = words.get(5 + index)?.parse().ok()?;
                            }
                            NodeKind::Curve { points }
                        }
                        "hsl" => NodeKind::Hsl {
                            hue: words.get(5)?.parse().ok()?,
                            saturation: words.get(6)?.parse().ok()?,
                            lightness: words.get(7)?.parse().ok()?,
                        },
                        "threshold" => NodeKind::Threshold {
                            threshold: words.get(5)?.parse().ok()?,
                            softness: words.get(6)?.parse().ok()?,
                            invert: words.get(7).is_some_and(|value| *value == "1"),
                        },
                        "blur" => NodeKind::Blur {
                            radius: words.get(5)?.parse().ok()?,
                            wrap: WrapMode::from_index(words.get(6)?.parse::<usize>().ok()?),
                        },
                        "sharpen" => NodeKind::Sharpen {
                            amount: words.get(5)?.parse().ok()?,
                            radius: words.get(6)?.parse().ok()?,
                        },
                        "emboss" => NodeKind::Emboss {
                            strength: words.get(5)?.parse().ok()?,
                            angle: words.get(6)?.parse().ok()?,
                        },
                        "directionalblur" => NodeKind::DirectionalBlur {
                            radius: words.get(5)?.parse().ok()?,
                            angle: words.get(6)?.parse().ok()?,
                            wrap: WrapMode::from_index(words.get(7)?.parse::<usize>().ok()?),
                        },
                        "edgemask" => NodeKind::EdgeMask {
                            strength: words.get(5)?.parse().ok()?,
                            invert: words.get(6).is_some_and(|value| *value == "1"),
                        },
                        "cavitymask" => NodeKind::CavityMask {
                            radius: words.get(5)?.parse().ok()?,
                            strength: words.get(6)?.parse().ok()?,
                            invert: words.get(7).is_some_and(|value| *value == "1"),
                        },
                        "distancemask" => NodeKind::DistanceMask {
                            threshold: words.get(5)?.parse().ok()?,
                            radius: words.get(6)?.parse().ok()?,
                            invert: words.get(7).is_some_and(|value| *value == "1"),
                        },
                        "warp" => NodeKind::Warp {
                            amount: words.get(5)?.parse().ok()?,
                            scale: words.get(6)?.parse().ok()?,
                            seed: words.get(7)?.parse().ok()?,
                        },
                        "value" => NodeKind::Value {
                            value: words.get(5)?.parse().ok()?,
                        },
                        "luminance" => NodeKind::Luminance,
                        "dot" => NodeKind::Dot,
                        "unknown" => NodeKind::Unknown {
                            filter: unescape_token(words.get(5).copied().unwrap_or("-")),
                            inputs: unescape_list(words.get(6).copied().unwrap_or("-")),
                            outputs: unescape_list(words.get(7).copied().unwrap_or("-")),
                            note: unescape_token(words.get(8).copied().unwrap_or("-")),
                        },
                        "proc" => {
                            let op_id = *words.get(5)?;
                            let count: usize = words.get(6)?.parse().ok()?;
                            let mut params = Vec::with_capacity(count);
                            for k in 0..count {
                                params.push(words.get(7 + k)?.parse::<f32>().ok()?);
                            }
                            let color_count: usize = words.get(7 + count)?.parse().ok()?;
                            let mut colors = Vec::with_capacity(color_count);
                            for k in 0..color_count {
                                colors.push(color(8 + count + k * 4, &words)?);
                            }
                            match procedural::find(op_id) {
                                Some(op) => NodeKind::proc_from_saved(op, &params, &colors),
                                None => NodeKind::Unknown {
                                    filter: op_id.to_owned(),
                                    inputs: Vec::new(),
                                    outputs: Vec::new(),
                                    note: "Missing procedural op".to_owned(),
                                },
                            }
                        }
                        "output" => {
                            let defaults = OutputOptions::default();
                            NodeKind::Output {
                                roughness: words.get(5)?.parse().ok()?,
                                metallic: words.get(6)?.parse().ok()?,
                                height: words.get(7)?.parse().ok()?,
                                ao: words
                                    .get(8)
                                    .and_then(|value| value.parse().ok())
                                    .unwrap_or(1.0),
                                options: OutputOptions {
                                    auto_height: words
                                        .get(9)
                                        .and_then(|v| v.parse::<i32>().ok())
                                        .map_or(defaults.auto_height, |v| v != 0),
                                    normalize_height: words
                                        .get(10)
                                        .and_then(|v| v.parse::<i32>().ok())
                                        .map_or(defaults.normalize_height, |v| v != 0),
                                    height_contrast: words
                                        .get(11)
                                        .and_then(|v| v.parse().ok())
                                        .unwrap_or(defaults.height_contrast),
                                },
                            }
                        }
                        "texoutput" => NodeKind::TextureOutput {
                            mode: TextureOutputMode::from_index(
                                words.get(5).and_then(|v| v.parse().ok()).unwrap_or(0),
                            ),
                        },
                        _ => continue,
                    };
                    graph.next_id = graph.next_id.max(id + 1);
                    graph.nodes.push(GeneratorNode { id, kind, position });
                }
                "link" if words.len() >= 5 => {
                    graph.links.push(node_canvas::Link {
                        from: words[1].parse().ok()?,
                        output: words[2].parse().ok()?,
                        to: words[3].parse().ok()?,
                        input: words[4].parse().ok()?,
                    });
                }
                _ => {}
            }
        }
        Some(graph)
    }

    /// Evaluate every node at a small resolution for its in-canvas preview.
    ///
    /// The shared memo makes this much cheaper than evaluating each node from
    /// scratch. Nodes whose inputs cannot be evaluated (for example an Image
    /// node without a decoded texture) are simply omitted.
    pub fn node_previews(
        &self,
        textures: &TextureMap,
        size: u32,
        cancel: Option<&CancelFn>,
    ) -> Vec<(NodeId, Vec<u8>)> {
        let resolution = size.clamp(32, 128) as usize;
        let mut memo = HashMap::new();
        let mut previews = Vec::new();
        let mut ctx = BakeContext {
            textures,
            cancel,
            progress: None,
            total: self.nodes.len().max(1),
            done: 0,
            extra: HashMap::new(),
        };
        for node in &self.nodes {
            let mut stack = Vec::new();
            let Ok(image) = self.eval_node(node.id, resolution, &mut memo, &mut stack, &mut ctx)
            else {
                continue;
            };
            let mut bytes = Vec::with_capacity(resolution * resolution * 4);
            for pixel in image.iter() {
                bytes.extend_from_slice(&[
                    to_byte(pixel[0]),
                    to_byte(pixel[1]),
                    to_byte(pixel[2]),
                    to_byte(pixel[3]),
                ]);
            }
            previews.push((node.id, bytes));
        }
        previews
    }

    pub fn bake(&self) -> Result<BakedMaterial, String> {
        self.bake_with(&TextureMap::new(), None, None)
    }

    pub fn bake_with<'a>(
        &self,
        textures: &'a TextureMap,
        progress: Option<&'a mut ProgressFn<'a>>,
        cancel: Option<&'a CancelFn<'a>>,
    ) -> Result<BakedMaterial, String> {
        let output = self
            .output_node()
            .ok_or_else(|| "The graph needs an Output node".to_owned())?;
        let output_kind = self
            .node(output)
            .map(|node| node.kind.clone())
            .ok_or_else(|| "Output node is missing".to_owned())?;
        let NodeKind::Output {
            roughness,
            metallic,
            height,
            ao,
            options,
        } = output_kind
        else {
            return Err("The output node is not an Output".to_owned());
        };
        let resolution = self.resolution.clamp(64, 2048) as usize;
        let count = resolution * resolution;
        let mut memo = HashMap::new();
        // Only nodes upstream of the output are evaluated; counting the Output
        // node itself (or disconnected nodes) left the bar stuck short of 90%
        // for the whole finalisation phase.
        let mut reachable = HashSet::new();
        let mut pending = vec![output];
        while let Some(id) = pending.pop() {
            if !reachable.insert(id) {
                continue;
            }
            for link in self.links.iter().filter(|link| link.to == id) {
                pending.push(link.from);
            }
        }
        let total = reachable
            .iter()
            .filter(|id| {
                !matches!(
                    self.node(**id).map(|node| &node.kind),
                    Some(NodeKind::Output { .. })
                )
            })
            .count()
            .max(1);
        let mut ctx = BakeContext {
            textures,
            cancel,
            progress,
            total,
            done: 0,
            extra: HashMap::new(),
        };
        ctx.check()?;
        let color = self
            .input_image(output, 0, resolution, &mut memo, &mut Vec::new(), &mut ctx)?
            .unwrap_or_else(|| Rc::new(vec![[0.5, 0.5, 0.5, 1.0]; count]));
        let mut input = |port: usize| {
            self.input_image(
                output,
                port,
                resolution,
                &mut memo,
                &mut Vec::new(),
                &mut ctx,
            )
        };
        let roughness_image = input(1)?;
        let metallic_image = input(2)?;
        let height_image = input(3)?;
        let ao_image = input(4)?;
        let emissive_image = input(5)?;
        let opacity_image = input(6)?;
        let normal_image = input(7)?;
        ctx.stage(0.9, "Packing maps");

        let plane = |image: &Option<Image>, fallback: f32| -> Vec<f32> {
            match image {
                Some(image) => image.iter().map(|pixel| luminance(*pixel)).collect(),
                None => vec![fallback; count],
            }
        };
        let to_bytes = |values: &[f32]| -> (Vec<u8>, Vec<u16>) {
            (
                values.iter().map(|value| to_byte(*value)).collect(),
                values.iter().map(|value| to_u16(*value)).collect(),
            )
        };

        // Height: authored, else derived from the colour, else the constant.
        let mut height_plane = match &height_image {
            Some(_) => plane(&height_image, height),
            None if options.auto_height => color.iter().map(|pixel| luminance(*pixel)).collect(),
            None => vec![height; count],
        };
        if options.normalize_height {
            procedural::normalize_plane(&mut height_plane);
        }
        if (options.height_contrast - 1.0).abs() > 1e-3 {
            for value in height_plane.iter_mut() {
                *value = ((*value - 0.5) * options.height_contrast + 0.5).clamp(0.0, 1.0);
            }
        }

        let opacity_plane = opacity_image.as_ref().map(|_| plane(&opacity_image, 1.0));
        let mut base = Vec::with_capacity(count * 4);
        let mut base16 = Vec::with_capacity(count * 4);
        for (index, pixel) in color.iter().enumerate() {
            for (channel, value) in pixel.iter().enumerate() {
                let value = if channel == 3 {
                    opacity_plane.as_ref().map_or(*value, |plane| plane[index])
                } else {
                    *value
                };
                base.push(to_byte(value));
                base16.push(to_u16(value));
            }
        }
        let (roughness_pixels, roughness_pixels16) = to_bytes(&plane(&roughness_image, roughness));
        let (metallic_pixels, metallic_pixels16) = to_bytes(&plane(&metallic_image, metallic));
        let (height_pixels, height_pixels16) = to_bytes(&height_plane);
        ctx.stage(0.95, "Baking occlusion");
        let ao_values = match &ao_image {
            Some(_) => plane(&ao_image, ao),
            None => procedural::height::multiscale_ao(&height_plane, resolution, 0.02, 1.0, 0.6)
                .into_iter()
                .map(|value| value * ao)
                .collect(),
        };
        let (ao_pixels, ao_pixels16) = to_bytes(&ao_values);
        ctx.stage(0.98, "Writing maps");

        let has_emissive = emissive_image.is_some();
        let mut emissive = vec![0u8; count * 4];
        let mut emissive16 = vec![0u16; count * 4];
        if let Some(image) = &emissive_image {
            for (index, pixel) in image.iter().enumerate() {
                for channel in 0..4 {
                    let value = if channel == 3 { 1.0 } else { pixel[channel] };
                    emissive[index * 4 + channel] = to_byte(value);
                    emissive16[index * 4 + channel] = to_u16(value);
                }
            }
        }
        let has_opacity = opacity_plane.is_some();
        let (opacity, opacity16) = match &opacity_plane {
            Some(values) => to_bytes(values),
            None => (vec![255u8; count], vec![u16::MAX; count]),
        };
        let normal = normal_image.map(|image| {
            image
                .iter()
                .map(|pixel| [to_byte(pixel[0]), to_byte(pixel[1]), to_byte(pixel[2]), 255])
                .collect::<Vec<[u8; 4]>>()
        });
        ctx.finish()?;
        Ok(BakedMaterial {
            resolution: resolution as u32,
            base,
            roughness: roughness_pixels,
            metallic: metallic_pixels,
            height: height_pixels,
            ao: ao_pixels,
            base16,
            roughness16: roughness_pixels16,
            metallic16: metallic_pixels16,
            height16: height_pixels16,
            ao16: ao_pixels16,
            emissive,
            emissive16,
            has_emissive,
            opacity,
            opacity16,
            has_opacity,
            normal,
        })
    }

    /// Bakes a Texture Generator graph into one RGBA image. Only nodes upstream of the
    /// Texture Output are evaluated. Colour + alpha keeps the colour's own alpha unless an
    /// Alpha mask is connected; packed channels default to 0 (R, G, B) and 1 (A).
    pub fn bake_texture_with<'a>(
        &self,
        textures: &'a TextureMap,
        progress: Option<&'a mut ProgressFn<'a>>,
        cancel: Option<&'a CancelFn<'a>>,
    ) -> Result<BakedTexture, String> {
        let output = self
            .texture_output_node()
            .ok_or_else(|| "The graph needs a Texture Output node".to_owned())?;
        let Some(NodeKind::TextureOutput { mode }) = self.node(output).map(|n| n.kind.clone())
        else {
            return Err("The output node is not a Texture Output".to_owned());
        };
        let resolution = self.resolution.clamp(64, 2048) as usize;
        let count = resolution * resolution;
        let mut reachable = HashSet::new();
        let mut pending = vec![output];
        while let Some(id) = pending.pop() {
            if !reachable.insert(id) {
                continue;
            }
            for link in self.links.iter().filter(|link| link.to == id) {
                pending.push(link.from);
            }
        }
        let total = reachable.len().saturating_sub(1).max(1);
        let mut ctx = BakeContext {
            textures,
            cancel,
            progress,
            total,
            done: 0,
            extra: HashMap::new(),
        };
        ctx.check()?;
        let mut memo = HashMap::new();
        let mut input = |port: usize, ctx: &mut BakeContext| {
            self.input_image(output, port, resolution, &mut memo, &mut Vec::new(), ctx)
        };
        let plane = |image: &Option<Image>, fallback: f32| -> Vec<f32> {
            match image {
                Some(image) => image.iter().map(|pixel| luminance(*pixel)).collect(),
                None => vec![fallback; count],
            }
        };
        let mut pixels: Vec<[f32; 4]> = Vec::with_capacity(count);
        match mode {
            TextureOutputMode::ColorAlpha => {
                let color = input(0, &mut ctx)?;
                let alpha = input(1, &mut ctx)?;
                let alpha_plane = alpha.as_ref().map(|_| plane(&alpha, 1.0));
                match color {
                    Some(color) => {
                        for (index, pixel) in color.iter().enumerate() {
                            let a = alpha_plane.as_ref().map_or(pixel[3], |p| p[index]);
                            pixels.push([pixel[0], pixel[1], pixel[2], a]);
                        }
                    }
                    None => {
                        for index in 0..count {
                            let a = alpha_plane.as_ref().map_or(0.0, |p| p[index]);
                            pixels.push([0.0, 0.0, 0.0, a]);
                        }
                    }
                }
            }
            TextureOutputMode::Channels => {
                let r = input(0, &mut ctx)?;
                let g = input(1, &mut ctx)?;
                let b = input(2, &mut ctx)?;
                let a = input(3, &mut ctx)?;
                let (r, g, b, a) = (
                    plane(&r, 0.0),
                    plane(&g, 0.0),
                    plane(&b, 0.0),
                    plane(&a, 1.0),
                );
                for index in 0..count {
                    pixels.push([r[index], g[index], b[index], a[index]]);
                }
            }
        }
        ctx.stage(0.95, "Packing texture");
        let mut rgba8 = Vec::with_capacity(count * 4);
        let mut rgba16 = Vec::with_capacity(count * 4);
        for pixel in &pixels {
            for value in pixel {
                rgba8.push(to_byte(*value));
                rgba16.push(to_u16(*value));
            }
        }
        let has_alpha = pixels.iter().any(|pixel| pixel[3] < 0.999);
        ctx.finish()?;
        Ok(BakedTexture {
            resolution: resolution as u32,
            rgba8,
            rgba16,
            has_alpha,
            mode,
        })
    }

    fn input_image(
        &self,
        node: NodeId,
        input: usize,
        resolution: usize,
        memo: &mut HashMap<NodeId, Image>,
        stack: &mut Vec<NodeId>,
        ctx: &mut BakeContext,
    ) -> Result<Option<Image>, String> {
        let Some(link) = self
            .links
            .iter()
            .find(|link| link.to == node && link.input == input)
        else {
            return Ok(None);
        };
        let image = self.eval_node(link.from, resolution, memo, stack, ctx)?;
        if link.output > 0 {
            if let Some(extra) = ctx.extra.get(&(link.from, link.output)) {
                return Ok(Some(extra.clone()));
            }
        }
        Ok(Some(image))
    }

    fn eval_node(
        &self,
        id: NodeId,
        resolution: usize,
        memo: &mut HashMap<NodeId, Image>,
        stack: &mut Vec<NodeId>,
        ctx: &mut BakeContext,
    ) -> Result<Image, String> {
        if let Some(image) = memo.get(&id) {
            return Ok(image.clone());
        }
        if stack.contains(&id) {
            return Err("The material graph contains a cycle".to_owned());
        }
        stack.push(id);
        let node = self
            .node(id)
            .ok_or_else(|| format!("Node {id} is missing"))?;
        ctx.check()?;
        let image = match &node.kind {
            NodeKind::Color { color } => Rc::new(vec![*color; resolution * resolution]),
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
                if path.is_empty() {
                    return Err("Image node has no texture".to_owned());
                }
                let texture = ctx
                    .textures
                    .get(path)
                    .ok_or_else(|| format!("Image texture not loaded: {path}"))?;
                let mut pixels = Vec::with_capacity(resolution * resolution);
                for y in 0..resolution {
                    if y % 32 == 0 {
                        ctx.check()?;
                    }
                    for x in 0..resolution {
                        let u = (x as f32 + 0.5) / resolution as f32 * tiling[0] + offset[0];
                        let v = (y as f32 + 0.5) / resolution as f32 * tiling[1] + offset[1];
                        let color = match projection {
                            Projection::Uv => sample_texture(texture, u, v, *filter, *wrap),
                            Projection::Planar => sample_texture(texture, v, u, *filter, *wrap),
                            Projection::Cylindrical => {
                                let angle = (u - 0.5).atan2(v - 0.5) / std::f32::consts::TAU + 0.5;
                                sample_texture(texture, angle, v, *filter, *wrap)
                            }
                            Projection::Spherical => {
                                let mut nx = u * 2.0 - 1.0;
                                let mut ny = v * 2.0 - 1.0;
                                let radius = (nx * nx + ny * ny).sqrt();
                                if radius > 1.0 {
                                    nx /= radius;
                                    ny /= radius;
                                }
                                let nz = (1.0 - nx * nx - ny * ny).max(0.0).sqrt();
                                let longitude = nx.atan2(nz) / std::f32::consts::TAU + 0.5;
                                let latitude = 0.5 - ny.asin() / std::f32::consts::PI;
                                sample_texture(texture, longitude, latitude, *filter, *wrap)
                            }
                            Projection::TriPlanar => {
                                let nx = u * 2.0 - 1.0;
                                let ny = v * 2.0 - 1.0;
                                let nz = (1.0 - nx * nx - ny * ny).max(0.0).sqrt();
                                let weights = [nx.abs(), ny.abs(), nz.abs()];
                                let total = (weights[0] + weights[1] + weights[2]).max(1e-4);
                                let a = sample_texture(texture, u, v, *filter, *wrap);
                                let b = sample_texture(texture, v, u, *filter, *wrap);
                                let c = sample_texture(
                                    texture,
                                    0.5 + (u - 0.5) * 0.5,
                                    0.5 + (v - 0.5) * 0.5,
                                    *filter,
                                    *wrap,
                                );
                                let mut color = [0.0; 4];
                                for channel in 0..4 {
                                    color[channel] = (a[channel] * weights[2]
                                        + b[channel] * weights[0]
                                        + c[channel] * weights[1])
                                        / total;
                                }
                                color
                            }
                        };
                        pixels.push(color);
                    }
                }
                Rc::new(pixels)
            }
            NodeKind::NormalBlend { strength, mode } => {
                let base = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 1.0, 1.0],
                )?;
                let detail = node_input(
                    self,
                    id,
                    1,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 1.0, 1.0],
                )?;
                let mut pixels = Vec::with_capacity(resolution * resolution);
                for (base, detail) in base.iter().zip(detail.iter()) {
                    pixels.push(blend_normal(*base, *detail, *strength, *mode));
                }
                Rc::new(pixels)
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
                let period = scale.round().max(1.0) as i32;
                let mut pixels = Vec::with_capacity(resolution * resolution);
                for y in 0..resolution {
                    if y % 32 == 0 {
                        ctx.check()?;
                    }
                    for x in 0..resolution {
                        let u = (x as f32 + 0.5) / resolution as f32;
                        let v = (y as f32 + 0.5) / resolution as f32;
                        let value = fbm_typed(
                            u,
                            v,
                            period,
                            *seed,
                            *octaves,
                            *persistence,
                            *lacunarity,
                            *noise_type,
                        );
                        pixels.push(mix_color(*color_a, *color_b, value));
                    }
                }
                Rc::new(pixels)
            }
            NodeKind::Voronoi {
                scale,
                jitter,
                seed,
                color_a,
                color_b,
                mode,
            } => {
                let cells = scale.round().max(1.0) as i32;
                let jitter = jitter.clamp(0.0, 1.0);
                let mut pixels = Vec::with_capacity(resolution * resolution);
                for y in 0..resolution {
                    if y % 32 == 0 {
                        ctx.check()?;
                    }
                    for x in 0..resolution {
                        let u = (x as f32 + 0.5) / resolution as f32;
                        let v = (y as f32 + 0.5) / resolution as f32;
                        let px = u * cells as f32;
                        let py = v * cells as f32;
                        let base = (px.floor() as i32, py.floor() as i32);
                        let mut nearest = f32::MAX;
                        let mut nearest_cell = base;
                        for oy in -1..=1 {
                            for ox in -1..=1 {
                                let cell = (base.0 + ox, base.1 + oy);
                                let point = (
                                    cell.0 as f32
                                        + 0.5
                                        + (hash2(cell.0, cell.1, *seed) - 0.5) * jitter,
                                    cell.1 as f32
                                        + 0.5
                                        + (hash2(cell.0, cell.1, *seed + 7) - 0.5) * jitter,
                                );
                                let distance = (px - point.0).powi(2) + (py - point.1).powi(2);
                                if distance < nearest {
                                    nearest = distance;
                                    nearest_cell = cell;
                                }
                            }
                        }
                        let value = match mode {
                            VoronoiMode::Cells => hash2(nearest_cell.0, nearest_cell.1, *seed + 17),
                            VoronoiMode::Distance => nearest.sqrt().clamp(0.0, 1.0),
                        };
                        pixels.push(mix_color(*color_a, *color_b, value));
                    }
                }
                Rc::new(pixels)
            }
            NodeKind::Checker {
                scale,
                color_a,
                color_b,
            } => {
                let scale = scale.round().max(1.0) as i32;
                let mut pixels = Vec::with_capacity(resolution * resolution);
                for y in 0..resolution {
                    for x in 0..resolution {
                        let u = x as i32 * scale / resolution as i32;
                        let v = y as i32 * scale / resolution as i32;
                        pixels.push(if (u + v) % 2 == 0 { *color_a } else { *color_b });
                    }
                }
                Rc::new(pixels)
            }
            NodeKind::Bricks {
                rows,
                columns,
                mortar,
                color_a,
                color_b,
                mortar_color,
            } => {
                let rows = (*rows).max(1);
                let columns = (*columns).max(1);
                let mortar = mortar.clamp(0.0, 0.4);
                let mut pixels = Vec::with_capacity(resolution * resolution);
                for y in 0..resolution {
                    for x in 0..resolution {
                        let u = (x as f32 + 0.5) / resolution as f32;
                        let v = (y as f32 + 0.5) / resolution as f32;
                        let row = (v * rows as f32).floor() as i32;
                        let offset = if row % 2 == 0 { 0.0 } else { 0.5 };
                        let fx = (u * columns as f32 + offset).fract().abs();
                        let fy = (v * rows as f32).fract().abs();
                        let edge = fx < mortar || fy < mortar;
                        let variant = if hash2(row, (u * columns as f32).floor() as i32, 11) > 0.5 {
                            *color_a
                        } else {
                            *color_b
                        };
                        pixels.push(if edge { *mortar_color } else { variant });
                    }
                }
                Rc::new(pixels)
            }
            NodeKind::Gradient {
                angle,
                color_a,
                color_b,
            } => {
                let radians = angle.to_radians();
                let direction = [radians.cos(), radians.sin()];
                let mut pixels = Vec::with_capacity(resolution * resolution);
                for y in 0..resolution {
                    for x in 0..resolution {
                        let u = (x as f32 + 0.5) / resolution as f32 - 0.5;
                        let v = (y as f32 + 0.5) / resolution as f32 - 0.5;
                        let t = (0.5 + u * direction[0] + v * direction[1]).clamp(0.0, 1.0);
                        pixels.push(mix_color(*color_a, *color_b, t));
                    }
                }
                Rc::new(pixels)
            }
            NodeKind::Blend { factor, mode } => {
                let base = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.0, 0.0, 0.0, 1.0],
                )?;
                let blend = node_input(
                    self,
                    id,
                    1,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [1.0, 1.0, 1.0, 1.0],
                )?;
                let mask = node_input(
                    self,
                    id,
                    2,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [1.0, 1.0, 1.0, 1.0],
                )?;
                let mut pixels = Vec::with_capacity(base.len());
                for ((a, b), m) in base.iter().zip(blend.iter()).zip(mask.iter()) {
                    let mixed = blend_channel(*mode, *a, *b);
                    let amount = factor.clamp(0.0, 1.0) * luminance(*m);
                    pixels.push(mix_color(*a, mixed, amount));
                }
                Rc::new(pixels)
            }
            NodeKind::Levels {
                brightness,
                contrast,
                gamma,
                invert,
            } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                let mut pixels = Vec::with_capacity(source.len());
                for pixel in source.iter() {
                    let mut out = *pixel;
                    for channel in 0..3 {
                        let mut value =
                            ((pixel[channel] - 0.5) * contrast + 0.5 + brightness).clamp(0.0, 1.0);
                        if (*gamma - 1.0).abs() > 1e-4 {
                            value = value.powf(1.0 / gamma.clamp(0.1, 4.0));
                        }
                        if *invert {
                            value = 1.0 - value;
                        }
                        out[channel] = value;
                    }
                    pixels.push(out);
                }
                Rc::new(pixels)
            }
            NodeKind::Curve { points } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                let mut pixels = Vec::with_capacity(source.len());
                for pixel in source.iter() {
                    let mut out = *pixel;
                    for channel in 0..3 {
                        out[channel] = curve_value(points, pixel[channel]);
                    }
                    pixels.push(out);
                }
                Rc::new(pixels)
            }
            NodeKind::Hsl {
                hue,
                saturation,
                lightness,
            } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                let mut pixels = Vec::with_capacity(source.len());
                for pixel in source.iter() {
                    let mut hsl = rgb_to_hsl(*pixel);
                    hsl[0] += *hue;
                    hsl[1] = (hsl[1] * saturation).clamp(0.0, 1.0);
                    hsl[2] = (hsl[2] + lightness).clamp(0.0, 1.0);
                    pixels.push(hsl_to_rgb(hsl, pixel[3]));
                }
                Rc::new(pixels)
            }
            NodeKind::Threshold {
                threshold,
                softness,
                invert,
            } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                let mut pixels = Vec::with_capacity(source.len());
                for pixel in source.iter() {
                    let mut value = smoothstep(
                        threshold - softness,
                        threshold + softness,
                        luminance(*pixel),
                    );
                    if *invert {
                        value = 1.0 - value;
                    }
                    pixels.push([value, value, value, 1.0]);
                }
                Rc::new(pixels)
            }
            NodeKind::Blur { radius, wrap } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                Rc::new(blur_image(
                    &source,
                    resolution,
                    radius.round() as i32,
                    *wrap,
                ))
            }
            NodeKind::Sharpen { amount, radius } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                Rc::new(sharpen_image(
                    &source,
                    resolution,
                    radius.round().max(1.0) as i32,
                    *amount,
                ))
            }
            NodeKind::Emboss { strength, angle } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                Rc::new(emboss_image(&source, resolution, *angle, *strength))
            }
            NodeKind::DirectionalBlur {
                radius,
                angle,
                wrap,
            } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                Rc::new(directional_blur(
                    &source,
                    resolution,
                    radius.round().max(1.0) as i32,
                    *angle,
                    *wrap,
                ))
            }
            NodeKind::EdgeMask { strength, invert } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                Rc::new(edge_mask(&source, resolution, *strength, *invert))
            }
            NodeKind::CavityMask {
                radius,
                strength,
                invert,
            } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                Rc::new(cavity_mask(
                    &source,
                    resolution,
                    radius.round().max(1.0) as i32,
                    *strength,
                    *invert,
                ))
            }
            NodeKind::DistanceMask {
                threshold,
                radius,
                invert,
            } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                Rc::new(distance_mask(
                    &source,
                    resolution,
                    *threshold,
                    radius.max(1.0),
                    *invert,
                ))
            }
            NodeKind::Warp {
                amount,
                scale,
                seed,
            } => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                let period = scale.round().max(1.0) as i32;
                let mut pixels = Vec::with_capacity(resolution * resolution);
                for y in 0..resolution {
                    if y % 32 == 0 {
                        ctx.check()?;
                    }
                    for x in 0..resolution {
                        let u = (x as f32 + 0.5) / resolution as f32;
                        let v = (y as f32 + 0.5) / resolution as f32;
                        let dx = (fbm_typed(u, v, period, *seed, 2, 0.5, 2.0, NoiseType::Value)
                            - 0.5)
                            * amount;
                        let dy =
                            (fbm_typed(u, v, period, *seed + 19, 2, 0.5, 2.0, NoiseType::Value)
                                - 0.5)
                                * amount;
                        pixels.push(sample_image(
                            &source,
                            resolution,
                            u + dx,
                            v + dy,
                            WrapMode::Repeat,
                        ));
                    }
                }
                Rc::new(pixels)
            }
            NodeKind::Value { value } => Rc::new(vec![
                [
                    value.clamp(0.0, 1.0),
                    value.clamp(0.0, 1.0),
                    value.clamp(0.0, 1.0),
                    1.0
                ];
                resolution * resolution
            ]),
            NodeKind::Luminance => {
                let source = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                Rc::new(
                    source
                        .iter()
                        .map(|pixel| {
                            let value = luminance(*pixel);
                            [value, value, value, 1.0]
                        })
                        .collect(),
                )
            }
            NodeKind::Dot => node_input(
                self,
                id,
                0,
                resolution,
                memo,
                stack,
                ctx,
                [0.5, 0.5, 0.5, 1.0],
            )?,
            NodeKind::Unknown { inputs, .. } => {
                if inputs.is_empty() {
                    Rc::new(vec![[0.5, 0.5, 0.5, 1.0]; resolution * resolution])
                } else {
                    node_input(
                        self,
                        id,
                        0,
                        resolution,
                        memo,
                        stack,
                        ctx,
                        [0.5, 0.5, 0.5, 1.0],
                    )?
                }
            }
            NodeKind::Proc { op, params, colors } => {
                let def = op.def();
                let mut inputs: Vec<Option<Image>> = Vec::with_capacity(def.inputs.len());
                for port in 0..def.inputs.len() {
                    inputs.push(self.input_image(id, port, resolution, memo, stack, ctx)?);
                }
                let refs: Vec<Option<&[[f32; 4]]>> = inputs
                    .iter()
                    .map(|input| input.as_ref().map(|image| image.as_slice()))
                    .collect();
                let (params, colors) = match NodeKind::proc_from_saved(*op, params, colors) {
                    NodeKind::Proc { params, colors, .. } => (params, colors),
                    _ => unreachable!(),
                };
                let args = procedural::Args::new(resolution, &params, &colors, &refs);
                let mut outputs = (def.eval)(&args);
                ctx.check()?;
                let mut iter = outputs.drain(..);
                let first = iter
                    .next()
                    .unwrap_or_else(|| vec![[0.5, 0.5, 0.5, 1.0]; resolution * resolution]);
                for (index, extra) in iter.enumerate() {
                    ctx.extra.insert((id, index + 1), Rc::new(extra));
                }
                Rc::new(first)
            }
            NodeKind::Output {
                roughness,
                metallic,
                height,
                ao,
                ..
            } => {
                let color = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.5, 0.5, 0.5, 1.0],
                )?;
                let mut pixels = Vec::with_capacity(color.len());
                for pixel in color.iter() {
                    pixels.push([pixel[0], pixel[1], pixel[2], pixel[3]]);
                }
                let _ = (roughness, metallic, height, ao);
                Rc::new(pixels)
            }
            NodeKind::TextureOutput { .. } => {
                let color = node_input(
                    self,
                    id,
                    0,
                    resolution,
                    memo,
                    stack,
                    ctx,
                    [0.0, 0.0, 0.0, 0.0],
                )?;
                Rc::new(color.to_vec())
            }
        };
        stack.pop();
        ctx.node_done(node.kind.title());
        memo.insert(id, image.clone());
        Ok(image)
    }
}

pub fn blend_index(mode: BlendMode) -> usize {
    BlendMode::ALL
        .iter()
        .position(|candidate| *candidate == mode)
        .unwrap_or(0)
}

#[derive(Debug, Clone)]
struct GraphStep {
    name: String,
    key: Option<String>,
}

/// Small snapshot timeline for generator graph edits. Scene history and graph
/// history are separate: undoing a graph never rolls back scene entities, and
/// undoing a scene edit never rewrites a material graph.
#[derive(Debug, Clone)]
pub struct GraphHistory {
    states: Vec<MaterialGraph>,
    steps: Vec<GraphStep>,
    cursor: usize,
    bytes: usize,
}

impl GraphHistory {
    const MAX_STEPS: usize = 100;
    const MAX_BYTES: usize = 32 * 1024 * 1024;

    pub fn new(initial: MaterialGraph) -> Self {
        let mut history = Self {
            states: vec![initial],
            steps: Vec::new(),
            cursor: 0,
            bytes: 0,
        };
        history.recount();
        history
    }

    pub fn reset(&mut self, initial: MaterialGraph) {
        self.states.clear();
        self.steps.clear();
        self.states.push(initial);
        self.cursor = 0;
        self.recount();
    }

    pub fn can_undo(&self) -> bool {
        self.cursor > 0
    }

    pub fn can_redo(&self) -> bool {
        self.cursor + 1 < self.states.len()
    }

    pub fn undo_name(&self) -> Option<&str> {
        self.can_undo()
            .then(|| {
                self.steps
                    .get(self.cursor - 1)
                    .map(|step| step.name.as_str())
            })
            .flatten()
    }

    pub fn redo_name(&self) -> Option<&str> {
        self.can_redo()
            .then(|| self.steps.get(self.cursor).map(|step| step.name.as_str()))
            .flatten()
    }

    pub fn push(&mut self, after: MaterialGraph, name: impl Into<String>, key: Option<String>) {
        if self.states.get(self.cursor) == Some(&after) {
            return;
        }
        let name = name.into();
        let coalesce = key.is_some()
            && self.cursor > 0
            && self.cursor == self.steps.len()
            && self.steps[self.cursor - 1].key == key;
        if coalesce {
            self.states[self.cursor] = after;
            if let Some(step) = self.steps.get_mut(self.cursor - 1) {
                step.name = name;
            }
        } else {
            self.states.truncate(self.cursor + 1);
            self.steps.truncate(self.cursor);
            self.states.push(after);
            self.steps.push(GraphStep { name, key });
            self.cursor += 1;
        }
        self.recount();
        self.trim();
    }

    pub fn undo(&mut self) -> Option<(String, MaterialGraph)> {
        if !self.can_undo() {
            return None;
        }
        self.cursor -= 1;
        let name = self.steps[self.cursor].name.clone();
        Some((name, self.states[self.cursor].clone()))
    }

    pub fn redo(&mut self) -> Option<(String, MaterialGraph)> {
        if !self.can_redo() {
            return None;
        }
        let name = self.steps[self.cursor].name.clone();
        self.cursor += 1;
        Some((name, self.states[self.cursor].clone()))
    }

    pub fn len(&self) -> usize {
        self.steps.len()
    }

    pub fn is_empty(&self) -> bool {
        self.steps.is_empty()
    }

    pub fn cursor(&self) -> usize {
        self.cursor
    }

    pub fn bytes(&self) -> usize {
        self.bytes
    }

    fn recount(&mut self) {
        self.bytes = self.states.iter().map(graph_bytes).sum();
    }

    fn trim(&mut self) {
        while self.cursor > 0
            && (self.steps.len() > Self::MAX_STEPS
                || (self.bytes > Self::MAX_BYTES && !self.steps.is_empty()))
        {
            self.states.remove(0);
            self.steps.remove(0);
            self.cursor -= 1;
            self.recount();
        }
    }
}

fn graph_bytes(graph: &MaterialGraph) -> usize {
    96 + graph.nodes.len() * 256 + graph.links.len() * 32
}

/// Result of baking a graph. All planes are `resolution * resolution`
/// unless noted.
#[derive(Debug)]
pub struct BakedMaterial {
    pub resolution: u32,
    /// RGBA8 base colour (4 bytes/pixel). Alpha carries the Opacity input
    /// when one is connected.
    pub base: Vec<u8>,
    pub roughness: Vec<u8>,
    pub metallic: Vec<u8>,
    /// 8-bit height, never flat by default: authored, or derived from the base
    /// colour when no Height is connected (`OutputOptions::auto_height`), and
    /// normalised to the full 0..1 range (`normalize_height`).
    pub height: Vec<u8>,
    pub ao: Vec<u8>,
    /// Full-precision copies used by 16-bit exports.
    pub base16: Vec<u16>,
    pub roughness16: Vec<u16>,
    pub metallic16: Vec<u16>,
    pub height16: Vec<u16>,
    pub ao16: Vec<u16>,
    /// RGBA8 emissive colour (4 bytes/pixel, alpha 255); all zero unless
    /// `has_emissive`.
    pub emissive: Vec<u8>,
    pub emissive16: Vec<u16>,
    /// True when an Emissive input was connected.
    pub has_emissive: bool,
    /// Opacity plane (255 everywhere unless `has_opacity`).
    pub opacity: Vec<u8>,
    pub opacity16: Vec<u16>,
    /// True when an Opacity input was connected (also written to `base` alpha).
    pub has_opacity: bool,
    /// Optional authored tangent-space normal map (RGBA8, OpenGL convention)
    /// from the Output's Normal input. `None` means "derive the normal from
    /// `height`".
    pub normal: Option<Vec<[u8; 4]>>,
}

/// Result of baking a Texture Generator graph.
#[derive(Debug, Clone)]
pub struct BakedTexture {
    pub resolution: u32,
    pub rgba8: Vec<u8>,
    pub rgba16: Vec<u16>,
    /// True when some pixel is translucent.
    pub has_alpha: bool,
    pub mode: TextureOutputMode,
}

/// Map shown in the generator's output preview.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PreviewChannel {
    Base,
    Normal,
    Height,
    Roughness,
    Metallic,
    Ao,
    Emissive,
    Opacity,
}

impl PreviewChannel {
    pub const ALL: [PreviewChannel; 8] = [
        PreviewChannel::Base,
        PreviewChannel::Normal,
        PreviewChannel::Height,
        PreviewChannel::Roughness,
        PreviewChannel::Metallic,
        PreviewChannel::Ao,
        PreviewChannel::Emissive,
        PreviewChannel::Opacity,
    ];

    pub fn label(self) -> &'static str {
        match self {
            PreviewChannel::Base => "Base",
            PreviewChannel::Normal => "Normal",
            PreviewChannel::Height => "Height",
            PreviewChannel::Roughness => "Rough",
            PreviewChannel::Metallic => "Metal",
            PreviewChannel::Ao => "AO",
            PreviewChannel::Emissive => "Emissive",
            PreviewChannel::Opacity => "Opacity",
        }
    }

    pub fn from_index(index: usize) -> Self {
        Self::ALL
            .get(index)
            .copied()
            .unwrap_or(PreviewChannel::Base)
    }
}

impl BakedMaterial {
    /// RGBA8 pixels of one channel for display. The normal map is the authored
    /// one when present, otherwise derived from the height (strength 2).
    pub fn channel_rgba(&self, channel: PreviewChannel) -> Vec<u8> {
        let gray =
            |plane: &[u8]| -> Vec<u8> { plane.iter().flat_map(|v| [*v, *v, *v, 255]).collect() };
        match channel {
            PreviewChannel::Base => self
                .base
                .chunks(4)
                .flat_map(|p| [p[0], p[1], p[2], 255])
                .collect(),
            PreviewChannel::Normal => match &self.normal {
                Some(normal) => normal.iter().flat_map(|p| *p).collect(),
                None => procedural::height::height_to_normal_rgba8(
                    &self.height,
                    self.resolution as usize,
                    2.0,
                    false,
                ),
            },
            PreviewChannel::Height => gray(&self.height),
            PreviewChannel::Roughness => gray(&self.roughness),
            PreviewChannel::Metallic => gray(&self.metallic),
            PreviewChannel::Ao => gray(&self.ao),
            PreviewChannel::Emissive => self
                .emissive
                .chunks(4)
                .flat_map(|p| [p[0], p[1], p[2], 255])
                .collect(),
            PreviewChannel::Opacity => gray(&self.opacity),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PackSource {
    Zero,
    One,
    Ao,
    Roughness,
    Metallic,
    Height,
    Smoothness,
    BaseLuminance,
}

impl PackSource {
    pub const ALL: [PackSource; 8] = [
        PackSource::Zero,
        PackSource::One,
        PackSource::Ao,
        PackSource::Roughness,
        PackSource::Metallic,
        PackSource::Height,
        PackSource::Smoothness,
        PackSource::BaseLuminance,
    ];

    pub fn label(self) -> &'static str {
        match self {
            PackSource::Zero => "0",
            PackSource::One => "1",
            PackSource::Ao => "AO",
            PackSource::Roughness => "Roughness",
            PackSource::Metallic => "Metallic",
            PackSource::Height => "Height",
            PackSource::Smoothness => "Smoothness",
            PackSource::BaseLuminance => "Base luma",
        }
    }

    pub fn from_index(index: usize) -> Self {
        Self::ALL.get(index).copied().unwrap_or(PackSource::Zero)
    }

    fn value16(self, baked: &BakedMaterial, index: usize) -> u16 {
        match self {
            PackSource::Zero => 0,
            PackSource::One => u16::MAX,
            PackSource::Ao => baked.ao16.get(index).copied().unwrap_or(u16::MAX),
            PackSource::Roughness => baked.roughness16.get(index).copied().unwrap_or(32768),
            PackSource::Metallic => baked.metallic16.get(index).copied().unwrap_or(0),
            PackSource::Height => baked.height16.get(index).copied().unwrap_or(32768),
            PackSource::Smoothness => {
                u16::MAX - baked.roughness16.get(index).copied().unwrap_or(32768)
            }
            PackSource::BaseLuminance => {
                let base = baked
                    .base16
                    .get(index * 4..index * 4 + 3)
                    .unwrap_or(&[32768, 32768, 32768]);
                to_u16(
                    (0.2126 * base[0] as f32 + 0.7152 * base[1] as f32 + 0.0722 * base[2] as f32)
                        / 65535.0,
                )
            }
        }
    }

    fn value(self, baked: &BakedMaterial, index: usize) -> f32 {
        match self {
            PackSource::Zero => 0.0,
            PackSource::One => 1.0,
            PackSource::Ao => baked.ao.get(index).copied().unwrap_or(255) as f32 / 255.0,
            PackSource::Roughness => {
                baked.roughness.get(index).copied().unwrap_or(128) as f32 / 255.0
            }
            PackSource::Metallic => baked.metallic.get(index).copied().unwrap_or(0) as f32 / 255.0,
            PackSource::Height => baked.height.get(index).copied().unwrap_or(128) as f32 / 255.0,
            PackSource::Smoothness => {
                1.0 - baked.roughness.get(index).copied().unwrap_or(128) as f32 / 255.0
            }
            PackSource::BaseLuminance => {
                let base = baked
                    .base
                    .get(index * 4..index * 4 + 3)
                    .unwrap_or(&[128, 128, 128]);
                (0.2126 * base[0] as f32 + 0.7152 * base[1] as f32 + 0.0722 * base[2] as f32)
                    / 255.0
            }
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PackingPreset {
    Orm,
    UnityHdrp,
    UnityMetallicSmoothness,
    Custom,
}

impl PackingPreset {
    pub const ALL: [PackingPreset; 4] = [
        PackingPreset::Orm,
        PackingPreset::UnityHdrp,
        PackingPreset::UnityMetallicSmoothness,
        PackingPreset::Custom,
    ];

    pub fn label(self) -> &'static str {
        match self {
            PackingPreset::Orm => "ORM (glTF/Unreal/Godot)",
            PackingPreset::UnityHdrp => "Unity HDRP mask",
            PackingPreset::UnityMetallicSmoothness => "Unity metallic-smoothness",
            PackingPreset::Custom => "Custom channels",
        }
    }

    pub fn from_index(index: usize) -> Self {
        Self::ALL.get(index).copied().unwrap_or(PackingPreset::Orm)
    }

    pub fn channels(self) -> [PackSource; 4] {
        match self {
            PackingPreset::Orm => [
                PackSource::Ao,
                PackSource::Roughness,
                PackSource::Metallic,
                PackSource::One,
            ],
            PackingPreset::UnityHdrp => [
                PackSource::Metallic,
                PackSource::Ao,
                PackSource::Zero,
                PackSource::Smoothness,
            ],
            PackingPreset::UnityMetallicSmoothness => [
                PackSource::Metallic,
                PackSource::Zero,
                PackSource::Zero,
                PackSource::Smoothness,
            ],
            PackingPreset::Custom => [
                PackSource::Ao,
                PackSource::Roughness,
                PackSource::Metallic,
                PackSource::One,
            ],
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct ChannelPacking {
    pub preset: PackingPreset,
    pub channels: [PackSource; 4],
    /// Name of a user preset applied on top of `channels`, when any.
    pub custom_name: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NamedPackingPreset {
    pub name: String,
    pub channels: [PackSource; 4],
}

impl NamedPackingPreset {
    fn to_line(&self) -> String {
        let channels: Vec<&str> = self.channels.iter().map(|source| source.label()).collect();
        format!("{}|{}", self.name.replace('|', " "), channels.join("|"))
    }

    fn from_line(line: &str) -> Option<Self> {
        let mut parts = line.split('|');
        let name = parts.next()?.trim().to_owned();
        if name.is_empty() {
            return None;
        }
        let mut channels = [PackSource::Zero; 4];
        for (index, channel) in channels.iter_mut().enumerate() {
            let label = parts.next()?.trim();
            *channel = PackSource::ALL
                .iter()
                .find(|source| source.label().eq_ignore_ascii_case(label))
                .copied()?;
            if index == 3 {
                break;
            }
        }
        Some(Self { name, channels })
    }
}

/// `~/.config/caffeine/packing-presets.txt` (or `$XDG_CONFIG_HOME`).
pub fn user_packing_preset_path() -> Option<std::path::PathBuf> {
    let base = std::env::var_os("XDG_CONFIG_HOME")
        .map(std::path::PathBuf::from)
        .or_else(|| {
            std::env::var_os("HOME").map(|home| std::path::PathBuf::from(home).join(".config"))
        })?;
    Some(base.join("caffeine").join("packing-presets.txt"))
}

pub fn load_user_packing_presets(path: &std::path::Path) -> Vec<NamedPackingPreset> {
    let Ok(text) = std::fs::read_to_string(path) else {
        return Vec::new();
    };
    text.lines()
        .filter_map(NamedPackingPreset::from_line)
        .collect()
}

pub fn save_user_packing_presets(
    path: &std::path::Path,
    presets: &[NamedPackingPreset],
) -> std::io::Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let mut text = String::new();
    for preset in presets {
        text.push_str(&preset.to_line());
        text.push('\n');
    }
    std::fs::write(path, text)
}

impl Default for ChannelPacking {
    fn default() -> Self {
        Self {
            preset: PackingPreset::Orm,
            channels: PackingPreset::Orm.channels(),
            custom_name: None,
        }
    }
}

impl ChannelPacking {
    pub fn set_preset(&mut self, preset: PackingPreset) {
        self.preset = preset;
        self.custom_name = None;
        if preset != PackingPreset::Custom {
            self.channels = preset.channels();
        }
    }

    pub fn apply_user_preset(&mut self, preset: &NamedPackingPreset) {
        self.preset = PackingPreset::Custom;
        self.channels = preset.channels;
        self.custom_name = Some(preset.name.clone());
    }

    pub fn label(&self) -> &str {
        self.custom_name
            .as_deref()
            .unwrap_or_else(|| self.preset.label())
    }

    pub fn packed8(&self, baked: &BakedMaterial) -> Vec<u8> {
        let count = (baked.resolution as usize).pow(2);
        let mut out = Vec::with_capacity(count * 4);
        for index in 0..count {
            for channel in 0..4 {
                out.push(to_byte(self.channels[channel].value(baked, index)));
            }
        }
        out
    }

    pub fn packed16(&self, baked: &BakedMaterial) -> Vec<u16> {
        let count = (baked.resolution as usize).pow(2);
        let mut out = Vec::with_capacity(count * 4);
        for index in 0..count {
            for channel in 0..4 {
                out.push(pack_value16(self.channels[channel], baked, index));
            }
        }
        out
    }
}

fn pack_value16(source: PackSource, baked: &BakedMaterial, index: usize) -> u16 {
    source.value16(baked, index)
}

fn escape_token(value: &str) -> String {
    if value.is_empty() {
        return "-".to_owned();
    }
    value
        .replace('%', "%25")
        .replace(' ', "%20")
        .replace(';', "%3B")
        .replace('-', "%2D")
}

fn unescape_token(value: &str) -> String {
    if value == "-" {
        return String::new();
    }
    value
        .replace("%2D", "-")
        .replace("%3B", ";")
        .replace("%20", " ")
        .replace("%25", "%")
}

fn escape_list(items: &[String]) -> String {
    if items.is_empty() {
        "-".to_owned()
    } else {
        items
            .iter()
            .map(|item| escape_token(item))
            .collect::<Vec<_>>()
            .join(";")
    }
}

fn unescape_list(value: &str) -> Vec<String> {
    if value == "-" {
        Vec::new()
    } else {
        value.split(';').map(unescape_token).collect()
    }
}

fn short_name(name: &str) -> String {
    if name.chars().count() > 16 {
        let mut short: String = name.chars().take(15).collect();
        short.push('…');
        short
    } else {
        name.to_owned()
    }
}

fn to_u16(value: f32) -> u16 {
    (value.clamp(0.0, 1.0) * 65535.0 + 0.5) as u16
}

fn to_byte(value: f32) -> u8 {
    (value.clamp(0.0, 1.0) * 255.0 + 0.5) as u8
}

fn luminance(color: [f32; 4]) -> f32 {
    0.2126 * color[0] + 0.7152 * color[1] + 0.0722 * color[2]
}

fn mix_color(a: [f32; 4], b: [f32; 4], t: f32) -> [f32; 4] {
    let t = t.clamp(0.0, 1.0);
    [
        (a[0] + (b[0] - a[0]) * t).clamp(0.0, 1.0),
        (a[1] + (b[1] - a[1]) * t).clamp(0.0, 1.0),
        (a[2] + (b[2] - a[2]) * t).clamp(0.0, 1.0),
        (a[3] + (b[3] - a[3]) * t).clamp(0.0, 1.0),
    ]
}

fn blend_channel(mode: BlendMode, base: [f32; 4], blend: [f32; 4]) -> [f32; 4] {
    let mut out = blend;
    for channel in 0..3 {
        out[channel] = match mode {
            BlendMode::Normal => blend[channel],
            BlendMode::Multiply => base[channel] * blend[channel],
            BlendMode::Screen => 1.0 - (1.0 - base[channel]) * (1.0 - blend[channel]),
            BlendMode::Add => base[channel] + blend[channel],
            BlendMode::Overlay => {
                if base[channel] < 0.5 {
                    2.0 * base[channel] * blend[channel]
                } else {
                    1.0 - 2.0 * (1.0 - base[channel]) * (1.0 - blend[channel])
                }
            }
            BlendMode::Darken => base[channel].min(blend[channel]),
            BlendMode::Lighten => base[channel].max(blend[channel]),
            BlendMode::Difference => (base[channel] - blend[channel]).abs(),
            BlendMode::Subtract => base[channel] - blend[channel],
            BlendMode::SoftLight => {
                if blend[channel] < 0.5 {
                    base[channel]
                        - (1.0 - 2.0 * blend[channel]) * base[channel] * (1.0 - base[channel])
                } else {
                    let d = if base[channel] < 0.25 {
                        ((16.0 * base[channel] - 12.0) * base[channel] + 4.0) * base[channel]
                    } else {
                        base[channel].sqrt()
                    };
                    base[channel] + (2.0 * blend[channel] - 1.0) * (d - base[channel])
                }
            }
            BlendMode::HardLight => {
                if blend[channel] < 0.5 {
                    2.0 * base[channel] * blend[channel]
                } else {
                    1.0 - 2.0 * (1.0 - base[channel]) * (1.0 - blend[channel])
                }
            }
            BlendMode::ColorDodge => {
                if blend[channel] >= 1.0 {
                    1.0
                } else {
                    (base[channel] / (1.0 - blend[channel])).min(1.0)
                }
            }
            BlendMode::ColorBurn => {
                if blend[channel] <= 0.0 {
                    0.0
                } else {
                    1.0 - ((1.0 - base[channel]) / blend[channel]).min(1.0)
                }
            }
        }
        .clamp(0.0, 1.0);
    }
    out
}

fn hash2(x: i32, y: i32, seed: i32) -> f32 {
    let mut h = (seed as u32)
        .wrapping_mul(374_761_393)
        .wrapping_add((x as u32).wrapping_mul(668_265_263))
        .wrapping_add((y as u32).wrapping_mul(2_147_483_647));
    h = (h ^ (h >> 13)).wrapping_mul(1_274_126_177);
    ((h ^ (h >> 16)) & 0xffff) as f32 / 65_535.0
}

fn value_noise(u: f32, v: f32, period: i32, seed: i32) -> f32 {
    let x = u * period as f32;
    let y = v * period as f32;
    let ix = x.floor() as i32;
    let iy = y.floor() as i32;
    let fx = x - ix as f32;
    let fy = y - iy as f32;
    let sx = fx * fx * (3.0 - 2.0 * fx);
    let sy = fy * fy * (3.0 - 2.0 * fy);
    let wrap = |value: i32| ((value % period) + period) % period;
    let a = hash2(wrap(ix), wrap(iy), seed);
    let b = hash2(wrap(ix + 1), wrap(iy), seed);
    let c = hash2(wrap(ix), wrap(iy + 1), seed);
    let d = hash2(wrap(ix + 1), wrap(iy + 1), seed);
    let top = a + (b - a) * sx;
    let bottom = c + (d - c) * sx;
    top + (bottom - top) * sy
}

fn perlin_noise(u: f32, v: f32, period: i32, seed: i32) -> f32 {
    let period = period.max(1);
    let x = u * period as f32;
    let y = v * period as f32;
    let ix = x.floor() as i32;
    let iy = y.floor() as i32;
    let fx = x - ix as f32;
    let fy = y - iy as f32;
    let fade = |t: f32| t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    let smooth_x = fade(fx);
    let smooth_y = fade(fy);
    let wrap = |value: i32| ((value % period) + period) % period;
    let gradient = |x: i32, y: i32| {
        let angle = hash2(wrap(x), wrap(y), seed) * std::f32::consts::TAU;
        [angle.cos(), angle.sin()]
    };
    let dot = |g: [f32; 2], dx: f32, dy: f32| g[0] * dx + g[1] * dy;
    let n00 = dot(gradient(ix, iy), fx, fy);
    let n10 = dot(gradient(ix + 1, iy), fx - 1.0, fy);
    let n01 = dot(gradient(ix, iy + 1), fx, fy - 1.0);
    let n11 = dot(gradient(ix + 1, iy + 1), fx - 1.0, fy - 1.0);
    let top = n00 + (n10 - n00) * smooth_x;
    let bottom = n01 + (n11 - n01) * smooth_x;
    let value = top + (bottom - top) * smooth_y;
    (value * 1.4 + 0.5).clamp(0.0, 1.0)
}

#[allow(clippy::too_many_arguments)]
fn fbm_typed(
    u: f32,
    v: f32,
    period: i32,
    seed: i32,
    octaves: i32,
    persistence: f32,
    lacunarity: f32,
    noise_type: NoiseType,
) -> f32 {
    let mut total = 0.0;
    let mut amplitude = 0.5;
    let mut normalization = 0.0;
    let mut period = period.max(1);
    let persistence = persistence.clamp(0.05, 1.0);
    let lacunarity = lacunarity.clamp(1.0, 8.0);
    for octave in 0..octaves.max(1) {
        let value = match noise_type {
            NoiseType::Value => value_noise(u, v, period, seed.wrapping_add(octave * 31)),
            NoiseType::Perlin => perlin_noise(u, v, period, seed.wrapping_add(octave * 31)),
        };
        total += value * amplitude;
        normalization += amplitude;
        amplitude *= persistence;
        period = ((period as f32 * lacunarity) as i32).max(1);
    }
    total / normalization.max(0.001)
}

fn wrap_uv(value: f32, wrap: WrapMode) -> f32 {
    match wrap {
        WrapMode::Repeat => value.rem_euclid(1.0),
        WrapMode::Clamp => value.clamp(0.0, 1.0),
    }
}

fn sample_texture(
    texture: &TextureData,
    u: f32,
    v: f32,
    filter: ImageFilter,
    wrap: WrapMode,
) -> [f32; 4] {
    if texture.width == 0 || texture.height == 0 || texture.rgba.is_empty() {
        return [0.5, 0.5, 0.5, 1.0];
    }
    let u = wrap_uv(u, wrap);
    let v = wrap_uv(v, wrap);
    let fetch = |x: u32, y: u32| {
        let x = x.min(texture.width - 1);
        let y = y.min(texture.height - 1);
        let index = ((y * texture.width + x) * 4) as usize;
        [
            texture.rgba[index] as f32 / 255.0,
            texture.rgba[index + 1] as f32 / 255.0,
            texture.rgba[index + 2] as f32 / 255.0,
            texture.rgba[index + 3] as f32 / 255.0,
        ]
    };
    match filter {
        ImageFilter::Nearest => {
            let x = (u * texture.width as f32) as u32;
            let y = (v * texture.height as f32) as u32;
            fetch(x, y)
        }
        ImageFilter::Linear => {
            let x = u * texture.width as f32 - 0.5;
            let y = v * texture.height as f32 - 0.5;
            let x0 = x.floor() as i32;
            let y0 = y.floor() as i32;
            let tx = x - x0 as f32;
            let ty = y - y0 as f32;
            let wrap_index = |value: i32, size: u32| {
                if wrap == WrapMode::Repeat {
                    value.rem_euclid(size as i32) as u32
                } else {
                    value.clamp(0, size as i32 - 1) as u32
                }
            };
            let x0 = wrap_index(x0, texture.width);
            let y0 = wrap_index(y0, texture.height);
            let x1 = wrap_index(x0 as i32 + 1, texture.width);
            let y1 = wrap_index(y0 as i32 + 1, texture.height);
            let a = fetch(x0, y0);
            let b = fetch(x1, y0);
            let c = fetch(x0, y1);
            let d = fetch(x1, y1);
            let mut out = [0.0; 4];
            for channel in 0..4 {
                let top = a[channel] + (b[channel] - a[channel]) * tx;
                let bottom = c[channel] + (d[channel] - c[channel]) * tx;
                out[channel] = top + (bottom - top) * ty;
            }
            out
        }
    }
}

fn sample_image(image: &[[f32; 4]], resolution: usize, u: f32, v: f32, wrap: WrapMode) -> [f32; 4] {
    if resolution < 2 || image.len() < resolution * resolution {
        return [0.5, 0.5, 0.5, 1.0];
    }
    let u = wrap_uv(u, wrap);
    let v = wrap_uv(v, wrap);
    let x = u * resolution as f32 - 0.5;
    let y = v * resolution as f32 - 0.5;
    let x0 = x.floor() as i32;
    let y0 = y.floor() as i32;
    let tx = x - x0 as f32;
    let ty = y - y0 as f32;
    let wrap_index = |value: i32| {
        if wrap == WrapMode::Repeat {
            value.rem_euclid(resolution as i32) as usize
        } else {
            value.clamp(0, resolution as i32 - 1) as usize
        }
    };
    let x0 = wrap_index(x0);
    let y0 = wrap_index(y0);
    let x1 = wrap_index(x0 as i32 + 1);
    let y1 = wrap_index(y0 as i32 + 1);
    let fetch = |x: usize, y: usize| image[y * resolution + x];
    let a = fetch(x0, y0);
    let b = fetch(x1, y0);
    let c = fetch(x0, y1);
    let d = fetch(x1, y1);
    let mut out = [0.0; 4];
    for channel in 0..4 {
        let top = a[channel] + (b[channel] - a[channel]) * tx;
        let bottom = c[channel] + (d[channel] - c[channel]) * tx;
        out[channel] = top + (bottom - top) * ty;
    }
    out
}

fn rgb_to_hsl(color: [f32; 4]) -> [f32; 3] {
    let max = color[0].max(color[1]).max(color[2]);
    let min = color[0].min(color[1]).min(color[2]);
    let lightness = (max + min) * 0.5;
    if (max - min).abs() < 1e-5 {
        return [0.0, 0.0, lightness];
    }
    let delta = max - min;
    let saturation = if lightness > 0.5 {
        delta / (2.0 - max - min)
    } else {
        delta / (max + min)
    };
    let hue = if max == color[0] {
        ((color[1] - color[2]) / delta) % 6.0
    } else if max == color[1] {
        (color[2] - color[0]) / delta + 2.0
    } else {
        (color[0] - color[1]) / delta + 4.0
    };
    [hue * 60.0, saturation, lightness]
}

fn hsl_to_rgb(hsl: [f32; 3], alpha: f32) -> [f32; 4] {
    let hue = hsl[0].rem_euclid(360.0) / 360.0;
    let saturation = hsl[1].clamp(0.0, 1.0);
    let lightness = hsl[2].clamp(0.0, 1.0);
    if saturation <= 1e-5 {
        return [lightness, lightness, lightness, alpha];
    }
    let q = if lightness < 0.5 {
        lightness * (1.0 + saturation)
    } else {
        lightness + saturation - lightness * saturation
    };
    let p = 2.0 * lightness - q;
    let channel = |mut t: f32| {
        if t < 0.0 {
            t += 1.0;
        }
        if t > 1.0 {
            t -= 1.0;
        }
        if t < 1.0 / 6.0 {
            p + (q - p) * 6.0 * t
        } else if t < 0.5 {
            q
        } else if t < 2.0 / 3.0 {
            p + (q - p) * (2.0 / 3.0 - t) * 6.0
        } else {
            p
        }
    };
    [
        channel(hue + 1.0 / 3.0),
        channel(hue),
        channel(hue - 1.0 / 3.0),
        alpha,
    ]
}

fn blur_image(image: &[[f32; 4]], resolution: usize, radius: i32, wrap: WrapMode) -> Vec<[f32; 4]> {
    let radius = radius.clamp(0, 64) as isize;
    if radius == 0 || image.len() < resolution * resolution {
        return image.to_vec();
    }
    let sample = |x: isize, y: isize| -> [f32; 4] {
        let (x, y) = if wrap == WrapMode::Repeat {
            (
                x.rem_euclid(resolution as isize) as usize,
                y.rem_euclid(resolution as isize) as usize,
            )
        } else {
            (
                x.clamp(0, resolution as isize - 1) as usize,
                y.clamp(0, resolution as isize - 1) as usize,
            )
        };
        image[y * resolution + x]
    };
    let mut horizontal = vec![[0.0; 4]; resolution * resolution];
    for y in 0..resolution {
        for x in 0..resolution {
            let mut sum = [0.0; 4];
            let mut count = 0.0;
            for offset in -radius..=radius {
                let pixel = sample(x as isize + offset, y as isize);
                for channel in 0..4 {
                    sum[channel] += pixel[channel];
                }
                count += 1.0;
            }
            for channel in 0..4 {
                horizontal[y * resolution + x][channel] = sum[channel] / count;
            }
        }
    }
    let mut vertical = vec![[0.0; 4]; resolution * resolution];
    for y in 0..resolution {
        for x in 0..resolution {
            let mut sum = [0.0; 4];
            let mut count = 0.0;
            for offset in -radius..=radius {
                let sy = y as isize + offset;
                let (sy, sx) = if wrap == WrapMode::Repeat {
                    (sy.rem_euclid(resolution as isize) as usize, x)
                } else {
                    (sy.clamp(0, resolution as isize - 1) as usize, x)
                };
                let pixel = horizontal[sy * resolution + sx];
                for channel in 0..4 {
                    sum[channel] += pixel[channel];
                }
                count += 1.0;
            }
            for channel in 0..4 {
                vertical[y * resolution + x][channel] = sum[channel] / count;
            }
        }
    }
    vertical
}

fn decode_normal(pixel: [f32; 4]) -> [f32; 3] {
    let mut normal = [
        pixel[0] * 2.0 - 1.0,
        pixel[1] * 2.0 - 1.0,
        pixel[2] * 2.0 - 1.0,
    ];
    let length = (normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]).sqrt();
    if length < 1e-5 {
        [0.0, 0.0, 1.0]
    } else {
        normal[0] /= length;
        normal[1] /= length;
        normal[2] /= length;
        normal
    }
}

fn encode_normal(normal: [f32; 3]) -> [f32; 4] {
    [
        normal[0] * 0.5 + 0.5,
        normal[1] * 0.5 + 0.5,
        normal[2] * 0.5 + 0.5,
        1.0,
    ]
}

fn blend_normal(
    base: [f32; 4],
    detail: [f32; 4],
    strength: f32,
    mode: NormalBlendMode,
) -> [f32; 4] {
    let base = decode_normal(base);
    let detail = decode_normal(detail);
    let strength = strength.clamp(0.0, 2.0);
    let mut combined = match mode {
        NormalBlendMode::Whiteout => [
            base[0] + detail[0] * strength,
            base[1] + detail[1] * strength,
            base[2] * (1.0 - strength + detail[2] * strength),
        ],
        NormalBlendMode::Linear => [
            base[0] + detail[0] * strength,
            base[1] + detail[1] * strength,
            base[2] + detail[2] * strength,
        ],
    };
    let length =
        (combined[0] * combined[0] + combined[1] * combined[1] + combined[2] * combined[2]).sqrt();
    if length > 1e-5 {
        combined[0] /= length;
        combined[1] /= length;
        combined[2] /= length;
    }
    encode_normal(combined)
}

fn sharpen_image(image: &[[f32; 4]], resolution: usize, radius: i32, amount: f32) -> Vec<[f32; 4]> {
    let blurred = blur_image(image, resolution, radius, WrapMode::Clamp);
    let amount = amount.clamp(0.0, 4.0);
    image
        .iter()
        .zip(blurred.iter())
        .map(|(source, blur)| {
            let mut pixel = *source;
            for channel in 0..3 {
                pixel[channel] =
                    (source[channel] + (source[channel] - blur[channel]) * amount).clamp(0.0, 1.0);
            }
            pixel
        })
        .collect()
}

fn directional_blur(
    image: &[[f32; 4]],
    resolution: usize,
    radius: i32,
    angle: f32,
    wrap: WrapMode,
) -> Vec<[f32; 4]> {
    let radius = radius.clamp(1, 64);
    let radians = angle.to_radians();
    let dx = radians.cos() / resolution as f32;
    let dy = radians.sin() / resolution as f32;
    let mut out = Vec::with_capacity(image.len());
    for y in 0..resolution {
        for x in 0..resolution {
            let u = (x as f32 + 0.5) / resolution as f32;
            let v = (y as f32 + 0.5) / resolution as f32;
            let mut sum = [0.0; 4];
            let mut count = 0.0;
            for offset in -radius..=radius {
                let offset = offset as f32;
                let sample =
                    sample_image(image, resolution, u + dx * offset, v + dy * offset, wrap);
                for channel in 0..4 {
                    sum[channel] += sample[channel];
                }
                count += 1.0;
            }
            out.push([
                sum[0] / count,
                sum[1] / count,
                sum[2] / count,
                sum[3] / count,
            ]);
        }
    }
    out
}

fn emboss_image(image: &[[f32; 4]], resolution: usize, angle: f32, strength: f32) -> Vec<[f32; 4]> {
    let radians = angle.to_radians();
    let dx = radians.cos() / resolution as f32;
    let dy = radians.sin() / resolution as f32;
    let strength = strength.clamp(0.0, 8.0);
    let mut out = Vec::with_capacity(image.len());
    for y in 0..resolution {
        for x in 0..resolution {
            let u = (x as f32 + 0.5) / resolution as f32;
            let v = (y as f32 + 0.5) / resolution as f32;
            let a = sample_image(image, resolution, u + dx, v + dy, WrapMode::Clamp);
            let b = sample_image(image, resolution, u - dx, v - dy, WrapMode::Clamp);
            let difference = luminance(a) - luminance(b);
            let value = (0.5 + difference * strength).clamp(0.0, 1.0);
            out.push([value, value, value, 1.0]);
        }
    }
    out
}

fn edge_mask(image: &[[f32; 4]], resolution: usize, strength: f32, invert: bool) -> Vec<[f32; 4]> {
    let lum = |x: isize, y: isize| {
        let x = x.clamp(0, resolution as isize - 1) as usize;
        let y = y.clamp(0, resolution as isize - 1) as usize;
        luminance(image[y * resolution + x])
    };
    let strength = strength.clamp(0.0, 8.0);
    let mut out = Vec::with_capacity(resolution * resolution);
    for y in 0..resolution {
        for x in 0..resolution {
            let x = x as isize;
            let y = y as isize;
            let gx = lum(x + 1, y - 1) + 2.0 * lum(x + 1, y) + lum(x + 1, y + 1)
                - lum(x - 1, y - 1)
                - 2.0 * lum(x - 1, y)
                - lum(x - 1, y + 1);
            let gy = lum(x - 1, y + 1) + 2.0 * lum(x, y + 1) + lum(x + 1, y + 1)
                - lum(x - 1, y - 1)
                - 2.0 * lum(x, y - 1)
                - lum(x + 1, y - 1);
            let mut value = ((gx * gx + gy * gy).sqrt() * strength).clamp(0.0, 1.0);
            if invert {
                value = 1.0 - value;
            }
            out.push([value, value, value, 1.0]);
        }
    }
    out
}

fn cavity_mask(
    image: &[[f32; 4]],
    resolution: usize,
    radius: i32,
    strength: f32,
    invert: bool,
) -> Vec<[f32; 4]> {
    let blurred = blur_image(image, resolution, radius.clamp(1, 64), WrapMode::Clamp);
    let strength = strength.clamp(0.0, 10.0);
    let mut out = Vec::with_capacity(image.len());
    for (source, average) in image.iter().zip(blurred.iter()) {
        let center = luminance(*source);
        let average = luminance(*average);
        let cavity = (average - center).max(0.0);
        let mut value = 1.0 - (cavity * strength * 8.0).clamp(0.0, 1.0);
        if invert {
            value = 1.0 - value;
        }
        out.push([value, value, value, 1.0]);
    }
    out
}

fn distance_mask(
    image: &[[f32; 4]],
    resolution: usize,
    threshold: f32,
    radius: f32,
    invert: bool,
) -> Vec<[f32; 4]> {
    let infinity = resolution as f32 * 2.0;
    let mut distance = Vec::with_capacity(resolution * resolution);
    for pixel in image.iter() {
        distance.push(if luminance(*pixel) > threshold {
            infinity
        } else {
            0.0
        });
    }
    for y in 0..resolution {
        for x in 0..resolution {
            let index = y * resolution + x;
            if distance[index] == 0.0 {
                continue;
            }
            let mut best = distance[index];
            if x > 0 {
                best = best.min(distance[index - 1] + 1.0);
            }
            if y > 0 {
                best = best.min(distance[index - resolution] + 1.0);
                if x > 0 {
                    best = best.min(distance[index - resolution - 1] + std::f32::consts::SQRT_2);
                }
                if x + 1 < resolution {
                    best = best.min(distance[index - resolution + 1] + std::f32::consts::SQRT_2);
                }
            }
            distance[index] = best;
        }
    }
    for y in (0..resolution).rev() {
        for x in (0..resolution).rev() {
            let index = y * resolution + x;
            if distance[index] == 0.0 {
                continue;
            }
            let mut best = distance[index];
            if x + 1 < resolution {
                best = best.min(distance[index + 1] + 1.0);
            }
            if y + 1 < resolution {
                best = best.min(distance[index + resolution] + 1.0);
                if x + 1 < resolution {
                    best = best.min(distance[index + resolution + 1] + std::f32::consts::SQRT_2);
                }
                if x > 0 {
                    best = best.min(distance[index + resolution - 1] + std::f32::consts::SQRT_2);
                }
            }
            distance[index] = best;
        }
    }
    let radius = radius.max(1.0);
    distance
        .into_iter()
        .map(|value| {
            let mut value = (value / radius).clamp(0.0, 1.0);
            if invert {
                value = 1.0 - value;
            }
            [value, value, value, 1.0]
        })
        .collect()
}

fn smoothstep(edge0: f32, edge1: f32, value: f32) -> f32 {
    if (edge1 - edge0).abs() < 1e-6 {
        return if value >= edge1 { 1.0 } else { 0.0 };
    }
    let t = ((value - edge0) / (edge1 - edge0)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

fn curve_value(points: &[f32; 5], value: f32) -> f32 {
    let scaled = value.clamp(0.0, 1.0) * 4.0;
    let index = scaled.floor() as usize;
    if index >= 4 {
        return points[4].clamp(0.0, 1.0);
    }
    let t = scaled - index as f32;
    (points[index] + (points[index + 1] - points[index]) * t).clamp(0.0, 1.0)
}

pub fn offset_position(position: Point, delta: Vector) -> Point {
    Point::new(position.x + delta.x, position.y + delta.y)
}

pub fn palette() -> Vec<NodeKind> {
    vec![
        NodeKind::Image {
            path: String::new(),
            projection: Projection::Uv,
            tiling: [1.0, 1.0],
            offset: [0.0, 0.0],
            filter: ImageFilter::Linear,
            wrap: WrapMode::Repeat,
        },
        NodeKind::NormalMap {
            path: String::new(),
            projection: Projection::Uv,
            tiling: [1.0, 1.0],
            offset: [0.0, 0.0],
            filter: ImageFilter::Linear,
            wrap: WrapMode::Repeat,
        },
        NodeKind::Color {
            color: NodeKind::default_color(),
        },
        NodeKind::NormalBlend {
            strength: 1.0,
            mode: NormalBlendMode::Whiteout,
        },
        NodeKind::Noise {
            noise_type: NoiseType::Value,
            scale: 6.0,
            octaves: 4,
            persistence: 0.5,
            lacunarity: 2.0,
            seed: 1,
            color_a: [0.05, 0.05, 0.05, 1.0],
            color_b: [0.95, 0.95, 0.95, 1.0],
        },
        NodeKind::Voronoi {
            scale: 6.0,
            jitter: 0.8,
            seed: 1,
            color_a: [0.1, 0.1, 0.1, 1.0],
            color_b: [0.9, 0.9, 0.9, 1.0],
            mode: VoronoiMode::Cells,
        },
        NodeKind::Checker {
            scale: 6.0,
            color_a: [0.8, 0.8, 0.8, 1.0],
            color_b: [0.1, 0.1, 0.1, 1.0],
        },
        NodeKind::Bricks {
            rows: 6,
            columns: 3,
            mortar: 0.08,
            color_a: [0.55, 0.18, 0.12, 1.0],
            color_b: [0.48, 0.16, 0.1, 1.0],
            mortar_color: [0.72, 0.7, 0.64, 1.0],
        },
        NodeKind::Gradient {
            angle: 90.0,
            color_a: [0.02, 0.02, 0.04, 1.0],
            color_b: [0.9, 0.85, 0.65, 1.0],
        },
        NodeKind::Blend {
            factor: 0.5,
            mode: BlendMode::Normal,
        },
        NodeKind::Levels {
            brightness: 0.0,
            contrast: 1.0,
            gamma: 1.0,
            invert: false,
        },
        NodeKind::Curve {
            points: [0.0, 0.25, 0.5, 0.75, 1.0],
        },
        NodeKind::Hsl {
            hue: 0.0,
            saturation: 1.0,
            lightness: 0.0,
        },
        NodeKind::Threshold {
            threshold: 0.5,
            softness: 0.1,
            invert: false,
        },
        NodeKind::Blur {
            radius: 4.0,
            wrap: WrapMode::Repeat,
        },
        NodeKind::Sharpen {
            amount: 0.5,
            radius: 1.0,
        },
        NodeKind::Emboss {
            strength: 1.0,
            angle: 45.0,
        },
        NodeKind::DirectionalBlur {
            radius: 6.0,
            angle: 45.0,
            wrap: WrapMode::Clamp,
        },
        NodeKind::EdgeMask {
            strength: 1.0,
            invert: false,
        },
        NodeKind::CavityMask {
            radius: 4.0,
            strength: 1.0,
            invert: false,
        },
        NodeKind::DistanceMask {
            threshold: 0.5,
            radius: 16.0,
            invert: false,
        },
        NodeKind::Warp {
            amount: 0.08,
            scale: 6.0,
            seed: 1,
        },
        NodeKind::Value { value: 0.5 },
        NodeKind::Luminance,
        NodeKind::Dot,
        NodeKind::Output {
            roughness: 0.5,
            metallic: 0.0,
            height: 0.5,
            ao: 1.0,
            options: OutputOptions::default(),
        },
    ]
    .into_iter()
    .chain(
        procedural::all()
            .iter()
            // VFX & Shapes belongs to the Texture Generator, not to materials.
            .filter(|def| def.category != crate::uikit::texture_graph::VFX_CATEGORY)
            .map(|def| NodeKind::proc_default(ProcOp(def))),
    )
    .collect()
}

#[cfg(test)]
mod tests {
    #[test]
    fn node_previews_cover_each_node_and_are_not_flat() {
        let graph = super::MaterialGraph::default();
        let previews = graph.node_previews(&super::TextureMap::new(), 32, None);
        assert_eq!(
            previews.len(),
            graph.nodes.len(),
            "every evaluable node should get a thumbnail"
        );
        for (id, pixels) in &previews {
            assert_eq!(
                pixels.len(),
                32 * 32 * 4,
                "thumbnail {id} should be 32px RGBA"
            );
            let first = pixels[0];
            assert!(
                pixels.iter().any(|value| *value != first),
                "thumbnail {id} should not be a single flat colour"
            );
        }
    }

    use super::*;

    #[test]
    fn default_graph_bakes_a_colored_texture() {
        let graph = MaterialGraph::default();
        let baked = graph.bake().expect("bake");
        assert_eq!(baked.base.len(), 512 * 512 * 4);
        assert_eq!(baked.roughness.len(), 512 * 512);
        let unique: HashSet<u8> = baked.base.chunks(4).map(|pixel| pixel[0]).collect();
        assert!(unique.len() > 1, "noise should vary across the image");
    }

    #[test]
    fn bake_reports_finalisation_progress() {
        let graph = MaterialGraph::default();
        let mut stages: Vec<(f32, String)> = Vec::new();
        let mut progress = |fraction: f32, stage: &str| {
            stages.push((fraction, stage.to_owned()));
        };
        graph
            .bake_with(&TextureMap::new(), Some(&mut progress), None)
            .expect("bake");
        assert!(
            stages
                .iter()
                .any(|(fraction, stage)| stage == "Baking occlusion" && *fraction >= 0.95),
            "AO should report itself instead of freezing at the last node: {stages:?}"
        );
        assert!(stages.iter().any(|(_, stage)| stage == "Writing maps"));
        assert_eq!(stages.last().map(|(fraction, _)| *fraction), Some(1.0));
    }

    #[test]
    fn output_constants_drive_scalar_maps() {
        let mut graph = MaterialGraph::default();
        let output = graph.output_node().expect("output");
        if let Some(node) = graph.node_mut(output) {
            node.kind = NodeKind::Output {
                roughness: 0.2,
                metallic: 0.8,
                height: 0.1,
                ao: 1.0,
                options: OutputOptions {
                    auto_height: false,
                    normalize_height: false,
                    height_contrast: 1.0,
                },
            };
        }
        let baked = graph.bake().expect("bake");
        assert_eq!(baked.roughness[0], to_byte(0.2));
        assert_eq!(baked.metallic[0], to_byte(0.8));
        assert_eq!(baked.height[0], to_byte(0.1));
    }

    #[test]
    fn graph_text_round_trips_nodes_and_links() {
        let mut graph = MaterialGraph::default();
        graph.add_node_auto(NodeKind::Bricks {
            rows: 4,
            columns: 2,
            mortar: 0.1,
            color_a: [0.6, 0.2, 0.1, 1.0],
            color_b: [0.4, 0.1, 0.1, 1.0],
            mortar_color: [0.8, 0.8, 0.8, 1.0],
        });
        let text = graph.to_text();
        let loaded = MaterialGraph::from_text(&text).expect("graph text");
        assert_eq!(loaded.resolution, graph.resolution);
        assert_eq!(loaded.nodes.len(), graph.nodes.len());
        assert_eq!(loaded.links.len(), graph.links.len());
        assert!(matches!(
            loaded.nodes.last().map(|node| &node.kind),
            Some(NodeKind::Bricks { .. })
        ));
    }

    #[test]
    fn graph_history_coalesces_drags_and_drops_redo_on_new_edits() {
        let mut history = GraphHistory::new(MaterialGraph::default());
        let mut graph = MaterialGraph::default();
        let first = graph.nodes[0].id;
        graph.node_mut(first).unwrap().position.x += 8.0;
        history.push(graph.clone(), "Move node", Some(format!("move:{first}")));
        graph.node_mut(first).unwrap().position.x += 8.0;
        history.push(graph.clone(), "Move node", Some(format!("move:{first}")));
        assert_eq!(history.len(), 1);
        assert_eq!(history.undo_name(), Some("Move node"));

        let (_, initial) = history.undo().expect("undo");
        assert_eq!(
            initial.nodes[0].position.x,
            MaterialGraph::default().nodes[0].position.x
        );
        graph = history.redo().expect("redo").1;
        assert_eq!(
            graph.nodes[0].position.x,
            MaterialGraph::default().nodes[0].position.x + 16.0
        );

        graph = MaterialGraph::default();
        graph.add_node_auto(NodeKind::Checker {
            scale: 3.0,
            color_a: [1.0; 4],
            color_b: [0.0; 4],
        });
        history.push(graph, "Add Checker", None);
        assert!(!history.can_redo());
    }

    fn small_graph() -> MaterialGraph {
        MaterialGraph {
            resolution: 64,
            ..MaterialGraph::default()
        }
    }

    fn bake_scalar_texture(kind: NodeKind) -> BakedMaterial {
        let mut graph = small_graph();
        graph.nodes[0].kind = kind;
        graph.bake().expect("bake")
    }

    fn bake_filter(filter: NodeKind, source: NodeKind) -> BakedMaterial {
        let mut graph = small_graph();
        let output = graph.output_node().expect("output");
        let source_id = graph.nodes[0].id;
        graph.nodes[0].kind = source;
        let filter_id = graph.add_node_auto(filter);
        assert!(graph.connect(source_id, 0, filter_id, 0));
        assert!(graph.connect(filter_id, 0, output, 0));
        graph.bake().expect("bake")
    }

    fn checker_texture() -> TextureData {
        TextureData {
            width: 2,
            height: 2,
            rgba: vec![
                255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
            ],
        }
    }

    #[test]
    fn image_nodes_sample_uv_planar_and_triplanar() {
        let mut graph = small_graph();
        let path = "textures/checker.png".to_owned();
        graph.nodes[0].kind = NodeKind::Image {
            path: path.clone(),
            projection: Projection::Uv,
            tiling: [1.0, 1.0],
            offset: [0.0, 0.0],
            filter: ImageFilter::Nearest,
            wrap: WrapMode::Repeat,
        };
        let mut textures = TextureMap::new();
        textures.insert(path, checker_texture());
        let baked = graph.bake_with(&textures, None, None).expect("bake");
        let pixel = |x: usize, y: usize| {
            let index = (y * 64 + x) * 4;
            [
                baked.base[index],
                baked.base[index + 1],
                baked.base[index + 2],
            ]
        };
        assert_eq!(pixel(0, 0), [255, 0, 0]);
        assert_eq!(pixel(63, 0), [0, 255, 0]);
        assert_eq!(pixel(0, 63), [0, 0, 255]);
        assert_eq!(pixel(63, 63), [255, 255, 255]);

        if let NodeKind::Image { projection, .. } = &mut graph.nodes[0].kind {
            *projection = Projection::Planar;
        }
        let planar = graph.bake_with(&textures, None, None).expect("planar");
        assert_eq!(&planar.base[63 * 4..63 * 4 + 3], &[0, 0, 255]);

        if let NodeKind::Image { projection, .. } = &mut graph.nodes[0].kind {
            *projection = Projection::TriPlanar;
        }
        let triplanar = graph.bake_with(&textures, None, None).expect("triplanar");
        assert_ne!(triplanar.base, planar.base);

        if let NodeKind::Image { projection, .. } = &mut graph.nodes[0].kind {
            *projection = Projection::Cylindrical;
        }
        let cylindrical = graph.bake_with(&textures, None, None).expect("cylindrical");
        assert_ne!(cylindrical.base, planar.base);
        assert!(cylindrical.base.chunks(4).any(|pixel| pixel[0] > 200));

        if let NodeKind::Image { projection, .. } = &mut graph.nodes[0].kind {
            *projection = Projection::Spherical;
        }
        let spherical = graph.bake_with(&textures, None, None).expect("spherical");
        assert_ne!(spherical.base, cylindrical.base);

        if let NodeKind::Image { path, .. } = &mut graph.nodes[0].kind {
            *path = "textures/missing.png".to_owned();
        }
        assert!(graph
            .bake_with(&textures, None, None)
            .expect_err("missing texture")
            .contains("not loaded"));
    }

    #[test]
    fn linear_sampling_clamps_instead_of_wrapping_at_edges() {
        let texture = checker_texture();
        let edge = sample_texture(&texture, 1.0, 0.25, ImageFilter::Linear, WrapMode::Clamp);
        assert!(
            edge[1] > 0.9 && edge[0] < 0.1,
            "right edge should stay green"
        );
        let wrapped = sample_texture(&texture, 1.0, 0.25, ImageFilter::Linear, WrapMode::Repeat);
        assert!(
            wrapped[0] > 0.4,
            "repeat should blend back into the first texel"
        );
    }

    #[test]
    fn normal_maps_blend_and_new_filters_produce_expected_masks() {
        let mut graph = small_graph();
        let output = graph.output_node().expect("output");
        let base = graph.add_node_auto(NodeKind::Color {
            color: [0.5, 0.5, 1.0, 1.0],
        });
        let detail = graph.add_node_auto(NodeKind::Color {
            color: [1.0, 0.5, 0.5, 1.0],
        });
        let blend = graph.add_node_auto(NodeKind::NormalBlend {
            strength: 1.0,
            mode: NormalBlendMode::Whiteout,
        });
        assert!(graph.connect(base, 0, blend, 0));
        assert!(graph.connect(detail, 0, blend, 1));
        assert!(graph.connect(blend, 0, output, 0));
        let blended = graph.bake().expect("normal blend");
        assert!(blended.base[0] > 140, "whiteout should tilt the normal");
        assert!(
            blended.base[2] < 140,
            "whiteout should bend the normal down"
        );
        if let Some(node) = graph.node_mut(blend) {
            if let NodeKind::NormalBlend { mode, .. } = &mut node.kind {
                *mode = NormalBlendMode::Linear;
            }
        }
        let linear = graph.bake().expect("linear normal blend");
        assert_ne!(linear.base, blended.base);

        let checker = NodeKind::Checker {
            scale: 8.0,
            color_a: [0.0, 0.0, 0.0, 1.0],
            color_b: [1.0, 1.0, 1.0, 1.0],
        };
        let sharpened = bake_filter(
            NodeKind::Sharpen {
                amount: 1.0,
                radius: 2.0,
            },
            checker.clone(),
        );
        assert!(sharpened
            .base
            .chunks(4)
            .any(|pixel| pixel[0] > 200 || pixel[0] < 55));

        let embossed = bake_filter(
            NodeKind::Emboss {
                strength: 2.0,
                angle: 45.0,
            },
            checker.clone(),
        );
        assert!(embossed
            .base
            .chunks(4)
            .any(|pixel| pixel[0] > 30 && pixel[0] < 225));

        let directional = bake_filter(
            NodeKind::DirectionalBlur {
                radius: 6.0,
                angle: 0.0,
                wrap: WrapMode::Clamp,
            },
            checker.clone(),
        );
        assert!(directional
            .base
            .chunks(4)
            .any(|pixel| pixel[0] > 30 && pixel[0] < 225));

        let edges = bake_filter(
            NodeKind::EdgeMask {
                strength: 2.0,
                invert: false,
            },
            checker.clone(),
        );
        assert!(edges.base.chunks(4).any(|pixel| pixel[0] > 100));
        assert!(edges.base.chunks(4).any(|pixel| pixel[0] < 10));

        let cavity = bake_filter(
            NodeKind::CavityMask {
                radius: 4.0,
                strength: 2.0,
                invert: false,
            },
            checker.clone(),
        );
        let cavity_values: HashSet<u8> = cavity.base.chunks(4).map(|pixel| pixel[0]).collect();
        assert!(cavity_values.len() > 1);

        let distance = bake_filter(
            NodeKind::DistanceMask {
                threshold: 0.5,
                radius: 4.0,
                invert: false,
            },
            checker,
        );
        assert!(distance.base.chunks(4).any(|pixel| pixel[0] > 200));
        assert!(distance.base.chunks(4).any(|pixel| pixel[0] < 20));
    }

    #[test]
    fn noise_voronoi_and_filters_vary_or_flatten_as_expected() {
        let value = bake_scalar_texture(NodeKind::Noise {
            noise_type: NoiseType::Value,
            scale: 8.0,
            octaves: 4,
            persistence: 0.5,
            lacunarity: 2.0,
            seed: 3,
            color_a: [0.0; 4],
            color_b: [1.0; 4],
        });
        let perlin = bake_scalar_texture(NodeKind::Noise {
            noise_type: NoiseType::Perlin,
            scale: 8.0,
            octaves: 4,
            persistence: 0.5,
            lacunarity: 2.0,
            seed: 3,
            color_a: [0.0; 4],
            color_b: [1.0; 4],
        });
        assert_ne!(value.base, perlin.base);

        let cells = bake_scalar_texture(NodeKind::Voronoi {
            scale: 6.0,
            jitter: 0.9,
            seed: 2,
            color_a: [0.0; 4],
            color_b: [1.0; 4],
            mode: VoronoiMode::Cells,
        });
        let distance = bake_scalar_texture(NodeKind::Voronoi {
            scale: 6.0,
            jitter: 0.9,
            seed: 2,
            color_a: [0.0; 4],
            color_b: [1.0; 4],
            mode: VoronoiMode::Distance,
        });
        assert_ne!(cells.base, distance.base);

        let checker = NodeKind::Checker {
            scale: 8.0,
            color_a: [0.0, 0.0, 0.0, 1.0],
            color_b: [1.0, 1.0, 1.0, 1.0],
        };
        let sharp = bake_filter(
            NodeKind::Threshold {
                threshold: 0.5,
                softness: 0.0,
                invert: false,
            },
            checker.clone(),
        );
        let levels: HashSet<u8> = sharp.base.chunks(4).map(|pixel| pixel[0]).collect();
        assert!(levels.iter().all(|value| *value == 0 || *value == 255));

        let blurred = bake_filter(
            NodeKind::Blur {
                radius: 4.0,
                wrap: WrapMode::Clamp,
            },
            checker,
        );
        assert!(blurred
            .base
            .chunks(4)
            .any(|pixel| pixel[0] > 20 && pixel[0] < 235));

        let luminance = bake_filter(
            NodeKind::Luminance,
            NodeKind::Color {
                color: [1.0, 0.0, 0.0, 1.0],
            },
        );
        assert_eq!(&luminance.base[0..4], &[54, 54, 54, 255]);

        let constant = bake_scalar_texture(NodeKind::Value { value: 0.25 });
        assert_eq!(&constant.base[0..4], &[64, 64, 64, 255]);
    }

    #[test]
    fn bricks_and_complex_masks_bake_into_the_material_color() {
        let mut graph = small_graph();
        let output = graph.output_node().expect("output");
        let source = graph.nodes[0].id;
        graph.nodes[0].kind = NodeKind::Bricks {
            rows: 6,
            columns: 3,
            mortar: 0.08,
            color_a: [0.55, 0.18, 0.12, 1.0],
            color_b: [0.48, 0.16, 0.1, 1.0],
            mortar_color: [0.72, 0.7, 0.64, 1.0],
        };
        let baked = graph.bake().expect("bricks");
        assert!(baked
            .base
            .chunks(4)
            .any(|pixel| pixel[0] > 170 && pixel[1] > 160));
        assert!(baked
            .base
            .chunks(4)
            .any(|pixel| pixel[0] < 150 && pixel[1] < 100));

        let cavity = graph.add_node_auto(NodeKind::CavityMask {
            radius: 8.0,
            strength: 2.0,
            invert: false,
        });
        assert!(graph.connect(source, 0, cavity, 0));
        assert!(graph.connect(cavity, 0, output, 0));
        let mask = graph.bake().expect("cavity mask");
        assert!(mask.base.chunks(4).all(|pixel| {
            (pixel[0] as i16 - pixel[1] as i16).abs() <= 1
                && (pixel[1] as i16 - pixel[2] as i16).abs() <= 1
        }));
        assert!(mask.base.chunks(4).any(|pixel| pixel[0] < 250));
    }

    #[test]
    fn levels_curve_hsl_warp_and_blend_mask_change_output() {
        let base = NodeKind::Color {
            color: [0.2, 0.4, 0.6, 1.0],
        };
        let inverted = bake_filter(
            NodeKind::Levels {
                brightness: 0.0,
                contrast: 1.0,
                gamma: 1.0,
                invert: true,
            },
            base.clone(),
        );
        assert!(inverted.base[0] > 200);
        assert!(inverted.base[1] > 150);
        assert!(inverted.base[2] < 110);

        let curved = bake_filter(
            NodeKind::Curve {
                points: [1.0, 1.0, 1.0, 1.0, 1.0],
            },
            base.clone(),
        );
        assert_eq!(&curved.base[0..3], &[255, 255, 255]);

        let desaturated = bake_filter(
            NodeKind::Hsl {
                hue: 0.0,
                saturation: 0.0,
                lightness: 0.0,
            },
            base.clone(),
        );
        assert!(
            desaturated.base[0] == desaturated.base[1]
                && desaturated.base[1] == desaturated.base[2]
        );

        let warped = bake_filter(
            NodeKind::Warp {
                amount: 0.2,
                scale: 8.0,
                seed: 4,
            },
            NodeKind::Gradient {
                angle: 0.0,
                color_a: [0.0, 0.0, 0.0, 1.0],
                color_b: [1.0, 1.0, 1.0, 1.0],
            },
        );
        assert!(warped
            .base
            .chunks(4)
            .any(|pixel| pixel[0] > 0 && pixel[0] < 255));

        let mut graph = small_graph();
        let output = graph.output_node().expect("output");
        let a = graph.add_node_auto(NodeKind::Color {
            color: [1.0, 0.0, 0.0, 1.0],
        });
        let b = graph.add_node_auto(NodeKind::Color {
            color: [0.0, 0.0, 1.0, 1.0],
        });
        let blend = graph.add_node_auto(NodeKind::Blend {
            factor: 1.0,
            mode: BlendMode::Multiply,
        });
        assert!(graph.connect(a, 0, blend, 0));
        assert!(graph.connect(b, 0, blend, 1));
        assert!(graph.connect(blend, 0, output, 0));
        let multiplied = graph.bake().expect("multiply");
        assert_eq!(&multiplied.base[0..3], &[0, 0, 0]);

        let mask = graph.add_node_auto(NodeKind::Value { value: 0.0 });
        assert!(graph.connect(mask, 0, blend, 2));
        let masked = graph.bake().expect("masked");
        assert_eq!(&masked.base[0..3], &[255, 0, 0]);
    }

    #[test]
    fn phase_two_graph_text_round_trips_and_reads_old_headers() {
        let mut graph = MaterialGraph::default();
        graph.nodes[0].kind = NodeKind::Image {
            path: "textures/paint set/albedo map.png".to_owned(),
            projection: Projection::TriPlanar,
            tiling: [2.0, 3.0],
            offset: [0.25, -0.5],
            filter: ImageFilter::Nearest,
            wrap: WrapMode::Clamp,
        };
        graph.add_node_auto(NodeKind::Noise {
            noise_type: NoiseType::Perlin,
            scale: 7.0,
            octaves: 5,
            persistence: 0.4,
            lacunarity: 2.5,
            seed: 9,
            color_a: [0.1, 0.2, 0.3, 1.0],
            color_b: [0.9, 0.8, 0.7, 1.0],
        });
        graph.add_node_auto(NodeKind::Voronoi {
            scale: 9.0,
            jitter: 0.3,
            seed: 5,
            color_a: [0.0, 0.0, 0.0, 1.0],
            color_b: [1.0, 1.0, 1.0, 1.0],
            mode: VoronoiMode::Distance,
        });
        for kind in [
            NodeKind::Curve {
                points: [0.1, 0.2, 0.3, 0.4, 0.5],
            },
            NodeKind::Hsl {
                hue: 30.0,
                saturation: 0.5,
                lightness: 0.1,
            },
            NodeKind::Threshold {
                threshold: 0.4,
                softness: 0.2,
                invert: true,
            },
            NodeKind::Blur {
                radius: 3.0,
                wrap: WrapMode::Repeat,
            },
            NodeKind::Warp {
                amount: 0.1,
                scale: 4.0,
                seed: 2,
            },
            NodeKind::Value { value: 0.7 },
            NodeKind::Luminance,
            NodeKind::NormalMap {
                path: "textures/normal.png".to_owned(),
                projection: Projection::Spherical,
                tiling: [1.0, 2.0],
                offset: [0.1, 0.2],
                filter: ImageFilter::Linear,
                wrap: WrapMode::Repeat,
            },
            NodeKind::NormalBlend {
                strength: 0.75,
                mode: NormalBlendMode::Whiteout,
            },
            NodeKind::Sharpen {
                amount: 0.5,
                radius: 2.0,
            },
            NodeKind::Emboss {
                strength: 1.5,
                angle: 30.0,
            },
            NodeKind::DirectionalBlur {
                radius: 5.0,
                angle: 90.0,
                wrap: WrapMode::Clamp,
            },
            NodeKind::EdgeMask {
                strength: 1.0,
                invert: true,
            },
            NodeKind::CavityMask {
                radius: 3.0,
                strength: 2.0,
                invert: false,
            },
            NodeKind::DistanceMask {
                threshold: 0.25,
                radius: 12.0,
                invert: true,
            },
        ] {
            graph.add_node_auto(kind);
        }
        if let Some(node) = graph.output_node() {
            if let NodeKind::Output { ao, .. } = &mut graph.node_mut(node).unwrap().kind {
                *ao = 0.6;
            }
        }
        let text = graph.to_text();
        assert!(text.starts_with("CAFMGRAPH2"));
        let loaded = MaterialGraph::from_text(&text).expect("roundtrip");
        assert_eq!(loaded.nodes, graph.nodes);
        assert_eq!(loaded.links, graph.links);

        let old = "CAFMGRAPH1\nresolution 64\nnode 1 0 0 noise 4 2 1 0 0 0 1 1 1 1 1\nnode 2 0 0 output 0.5 0 0.5\nlink 1 0 2 0\n";
        let loaded = MaterialGraph::from_text(old).expect("old header");
        assert!(matches!(
            loaded.nodes[0].kind,
            NodeKind::Noise {
                noise_type: NoiseType::Value,
                persistence,
                lacunarity,
                ..
            } if (persistence - 0.5).abs() < 1e-5 && (lacunarity - 2.0).abs() < 1e-5
        ));
        assert!(matches!(
            loaded.nodes[1].kind,
            NodeKind::Output { ao, .. } if (ao - 1.0).abs() < 1e-5
        ));
    }

    #[test]
    fn channel_packing_and_typed_ports_are_stable() {
        let baked = BakedMaterial {
            resolution: 1,
            base: vec![51, 102, 204, 255],
            roughness: vec![100],
            metallic: vec![200],
            height: vec![150],
            ao: vec![50],
            base16: vec![51 * 257, 102 * 257, 204 * 257, 65535],
            roughness16: vec![100 * 257],
            metallic16: vec![200 * 257],
            height16: vec![150 * 257],
            ao16: vec![50 * 257],
            emissive: vec![0, 0, 0, 255],
            emissive16: vec![0, 0, 0, 65535],
            has_emissive: false,
            opacity: vec![255],
            opacity16: vec![65535],
            has_opacity: false,
            normal: None,
        };
        let mut packing = ChannelPacking::default();
        let orm = packing.packed8(&baked);
        assert_eq!(orm, vec![50, 100, 200, 255]);
        let orm16 = packing.packed16(&baked);
        assert_eq!(orm16, vec![50 * 257, 100 * 257, 200 * 257, 65535]);
        packing.set_preset(PackingPreset::UnityHdrp);
        assert_eq!(packing.packed8(&baked), vec![200, 50, 0, 155]);
        packing.set_preset(PackingPreset::UnityMetallicSmoothness);
        assert_eq!(packing.packed8(&baked), vec![200, 0, 0, 155]);
        packing.set_preset(PackingPreset::Custom);
        packing.channels[3] = PackSource::BaseLuminance;
        let packed = packing.packed16(&baked);
        let luminance = (0.2126 * 51.0 + 0.7152 * 102.0 + 0.0722 * 204.0) / 255.0;
        assert!((packed[3] as f32 / 65535.0 - luminance).abs() < 0.001);

        let noise = NodeKind::Noise {
            noise_type: NoiseType::Value,
            scale: 2.0,
            octaves: 1,
            persistence: 0.5,
            lacunarity: 2.0,
            seed: 1,
            color_a: [0.0; 4],
            color_b: [1.0; 4],
        };
        assert_eq!(noise.outputs()[0].kind, PortKind::Color);
        assert_eq!(NodeKind::Luminance.outputs()[0].kind, PortKind::Scalar);
        let output = NodeKind::Output {
            roughness: 0.5,
            metallic: 0.0,
            height: 0.5,
            ao: 1.0,
            options: OutputOptions::default(),
        };
        assert_eq!(output.inputs()[1].kind, PortKind::Scalar);
    }

    #[test]
    fn user_packing_presets_and_texture_decode_round_trip() {
        let directory = std::env::temp_dir().join(format!(
            "doppio-packing-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|duration| duration.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&directory).expect("temp dir");
        let path = directory.join("presets.txt");
        let presets = vec![
            NamedPackingPreset {
                name: "My ORM".to_owned(),
                channels: [
                    PackSource::Ao,
                    PackSource::Roughness,
                    PackSource::Metallic,
                    PackSource::One,
                ],
            },
            NamedPackingPreset {
                name: "Masks".to_owned(),
                channels: [
                    PackSource::Height,
                    PackSource::BaseLuminance,
                    PackSource::Smoothness,
                    PackSource::Zero,
                ],
            },
        ];
        save_user_packing_presets(&path, &presets).expect("save presets");
        assert_eq!(load_user_packing_presets(&path), presets);

        let mut packing = ChannelPacking::default();
        packing.apply_user_preset(&presets[1]);
        assert_eq!(packing.label(), "Masks");
        assert_eq!(packing.channels, presets[1].channels);
        packing.set_preset(PackingPreset::Orm);
        assert_eq!(packing.label(), "ORM (glTF/Unreal/Godot)");

        let texture_path = directory.join("texture.png");
        let image = image::RgbaImage::from_pixel(2, 3, image::Rgba([10, 20, 30, 255]));
        image.save(&texture_path).expect("write png");
        let decoded = decode_texture(&texture_path.to_string_lossy()).expect("decode");
        assert_eq!((decoded.width, decoded.height), (2, 3));
        assert_eq!(&decoded.rgba[0..4], &[10, 20, 30, 255]);
        assert!(decode_texture("/definitely/missing.png").is_err());
        std::fs::remove_dir_all(directory).ok();
    }

    #[test]
    fn bake_reports_progress_and_honours_cancellation() {
        let graph = small_graph();
        let mut fractions = Vec::new();
        let mut stages = Vec::new();
        let textures = TextureMap::new();
        let baked = graph
            .bake_with(
                &textures,
                Some(&mut |fraction, stage| {
                    fractions.push(fraction);
                    stages.push(stage.to_owned());
                }),
                None,
            )
            .expect("bake");
        assert_eq!(baked.resolution, 64);
        assert!(!fractions.is_empty());
        assert!(fractions.windows(2).all(|pair| pair[0] <= pair[1]));
        assert_eq!(fractions.last().copied(), Some(1.0));
        assert_eq!(stages.last().map(String::as_str), Some("Done"));

        let cancelled = std::sync::atomic::AtomicBool::new(false);
        let cancel = || cancelled.load(std::sync::atomic::Ordering::Relaxed);
        let mut reported = 0;
        let result = graph.bake_with(
            &textures,
            Some(&mut |_, _| {
                reported += 1;
                cancelled.store(true, std::sync::atomic::Ordering::Relaxed);
            }),
            Some(&cancel),
        );
        assert_eq!(result.expect_err("cancelled"), "Cancelled");
        assert!(reported > 0);
    }

    #[test]
    fn cycles_are_rejected_and_inputs_are_single() {
        let mut graph = MaterialGraph::default();
        let output = graph.output_node().expect("output");
        let noise = graph.nodes[0].id;
        assert!(!graph.connect(output, 0, noise, 0));
        let a = graph.add_node_auto(NodeKind::Blend {
            factor: 0.5,
            mode: BlendMode::Normal,
        });
        let b = graph.add_node_auto(NodeKind::Levels {
            brightness: 0.0,
            contrast: 1.0,
            gamma: 1.0,
            invert: false,
        });
        assert!(graph.connect(a, 0, b, 0));
        assert!(!graph.connect(b, 0, a, 0));
        assert_eq!(
            graph
                .links
                .iter()
                .filter(|link| link.to == b && link.input == 0)
                .count(),
            1
        );
    }
}

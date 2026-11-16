//! Texture sets: UDIM tiles, layer stacks, masks, painting, compositing and
//! the `.texset` project format.
//!
//! This is a clean-room layer-stack implementation for Doppio. It stores 8-bit
//! RGBA paint tiles and 8-bit masks; export presets turn the composite into
//! conventional channel maps.

use crate::uikit::generators::{BlendMode, TextureData};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::io::{Cursor, Read, Write};

pub const DEFAULT_TILE_SIZE: u32 = 512;
pub const MIN_UDIM: u32 = 1001;
pub const MAX_UDIM: u32 = 1010;
const MAX_TILE_BYTES: u64 = 64 * 1024 * 1024;

pub const CHANNEL_BASE: &str = "baseColor";
pub const CHANNEL_ROUGHNESS: &str = "roughness";
pub const CHANNEL_METALLIC: &str = "metallic";
pub const CHANNEL_HEIGHT: &str = "height";
pub const CHANNEL_AO: &str = "ambientOcclusion";
pub const CHANNEL_CURVATURE: &str = "curvature";
pub const CHANNEL_NORMAL: &str = "normal";
pub const CHANNEL_POSITION: &str = "position";
pub const CHANNEL_OBJECT_ID: &str = "objectId";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum ChannelKind {
    Color,
    Scalar,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Channel {
    pub name: String,
    pub kind: ChannelKind,
    pub srgb: bool,
    pub bit_depth: u8,
}

impl Channel {
    pub fn color(name: &str, srgb: bool) -> Self {
        Self {
            name: name.to_owned(),
            kind: ChannelKind::Color,
            srgb,
            bit_depth: 8,
        }
    }

    pub fn scalar(name: &str) -> Self {
        Self {
            name: name.to_owned(),
            kind: ChannelKind::Scalar,
            srgb: false,
            bit_depth: 8,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum LayerKind {
    Paint,
    Fill([f32; 4]),
    Texture {
        path: String,
        data: Option<TextureData>,
    },
}

#[derive(Debug, Clone, PartialEq)]
pub struct Layer {
    pub name: String,
    pub kind: LayerKind,
    pub visible: bool,
    pub opacity: f32,
    pub blend: BlendMode,
    /// RGBA pixels per UDIM tile and channel.
    pub pixels: BTreeMap<u32, BTreeMap<String, Vec<u8>>>,
    /// 8-bit layer masks per UDIM tile.
    pub masks: BTreeMap<u32, Vec<u8>>,
}

impl Layer {
    pub fn paint(name: &str) -> Self {
        Self {
            name: name.to_owned(),
            kind: LayerKind::Paint,
            visible: true,
            opacity: 1.0,
            blend: BlendMode::Normal,
            pixels: BTreeMap::new(),
            masks: BTreeMap::new(),
        }
    }

    pub fn fill(name: &str, color: [f32; 4]) -> Self {
        Self {
            name: name.to_owned(),
            kind: LayerKind::Fill(color),
            visible: true,
            opacity: 1.0,
            blend: BlendMode::Normal,
            pixels: BTreeMap::new(),
            masks: BTreeMap::new(),
        }
    }

    fn pixel(&self, tile: u32, channel: &str, index: usize, tile_size: u32) -> [f32; 4] {
        match &self.kind {
            LayerKind::Fill(color) => *color,
            LayerKind::Paint => self
                .pixels
                .get(&tile)
                .and_then(|channels| channels.get(channel))
                .and_then(|pixels| {
                    let offset = index * 4;
                    pixels.get(offset..offset + 4).map(|pixel| {
                        [
                            pixel[0] as f32 / 255.0,
                            pixel[1] as f32 / 255.0,
                            pixel[2] as f32 / 255.0,
                            pixel[3] as f32 / 255.0,
                        ]
                    })
                })
                .unwrap_or([0.0, 0.0, 0.0, 0.0]),
            LayerKind::Texture { data, .. } => data
                .as_ref()
                .map(|data| sample_texture(data, index, tile_size))
                .unwrap_or([0.5, 0.5, 0.5, 1.0]),
        }
    }

    fn mask(&self, tile: u32, index: usize) -> f32 {
        self.masks
            .get(&tile)
            .and_then(|mask| mask.get(index))
            .map_or(1.0, |value| *value as f32 / 255.0)
    }
}

fn sample_texture(data: &TextureData, index: usize, tile_size: u32) -> [f32; 4] {
    let width = data.width.max(1) as usize;
    let height = data.height.max(1) as usize;
    let resolution = tile_size.max(1) as usize;
    let x = index % resolution;
    let y = index / resolution;
    let sx = (x * width / resolution).min(width - 1);
    let sy = (y * height / resolution).min(height - 1);
    let offset = (sy * width + sx) * 4;
    [
        data.rgba[offset] as f32 / 255.0,
        data.rgba[offset + 1] as f32 / 255.0,
        data.rgba[offset + 2] as f32 / 255.0,
        data.rgba[offset + 3] as f32 / 255.0,
    ]
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Brush {
    pub radius: f32,
    pub hardness: f32,
    pub opacity: f32,
    pub color: [f32; 4],
}

impl Default for Brush {
    fn default() -> Self {
        Self {
            radius: 24.0,
            hardness: 0.7,
            opacity: 1.0,
            color: [0.8, 0.35, 0.2, 1.0],
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct TextureSet {
    pub name: String,
    pub tile_size: u32,
    pub channels: Vec<Channel>,
    pub layers: Vec<Layer>,
    pub active_layer: usize,
    pub active_channel: usize,
    pub active_tile: u32,
    pub tiles: BTreeSet<u32>,
    pub brush: Brush,
    pub paint_mask: bool,
}

impl Default for TextureSet {
    fn default() -> Self {
        Self {
            name: "Texture Set".to_owned(),
            tile_size: DEFAULT_TILE_SIZE,
            channels: default_channels(),
            layers: vec![Layer::paint("Paint")],
            active_layer: 0,
            active_channel: 0,
            active_tile: MIN_UDIM,
            tiles: [MIN_UDIM].into_iter().collect(),
            brush: Brush::default(),
            paint_mask: false,
        }
    }
}

pub fn default_channels() -> Vec<Channel> {
    vec![
        Channel::color(CHANNEL_BASE, true),
        Channel::scalar(CHANNEL_ROUGHNESS),
        Channel::scalar(CHANNEL_METALLIC),
        Channel::scalar(CHANNEL_HEIGHT),
        Channel::scalar(CHANNEL_AO),
        Channel::scalar(CHANNEL_CURVATURE),
        Channel::color(CHANNEL_NORMAL, false),
        Channel::color(CHANNEL_POSITION, false),
        Channel::color(CHANNEL_OBJECT_ID, false),
    ]
}

fn tile_pixels(tile_size: u32) -> usize {
    (tile_size as usize).pow(2)
}

fn ensure_layer_pixels(layer: &mut Layer, tile: u32, channel: &str) {
    let size = match layer.kind {
        LayerKind::Paint => layer
            .pixels
            .get(&tile)
            .and_then(|map| map.get(channel))
            .map_or(0, Vec::len),
        _ => 0,
    };
    if size == 0 {
        layer
            .pixels
            .entry(tile)
            .or_default()
            .entry(channel.to_owned())
            .or_default();
    }
}

impl TextureSet {
    pub fn channel(&self) -> Option<&Channel> {
        self.channels.get(self.active_channel)
    }

    pub fn layer(&self) -> Option<&Layer> {
        self.layers.get(self.active_layer)
    }

    pub fn layer_mut(&mut self) -> Option<&mut Layer> {
        self.layers.get_mut(self.active_layer)
    }

    pub fn tiles(&self) -> Vec<u32> {
        let mut tiles: BTreeSet<u32> = self.tiles.clone();
        for layer in &self.layers {
            tiles.extend(layer.pixels.keys().copied());
            tiles.extend(layer.masks.keys().copied());
        }
        tiles.insert(self.active_tile);
        tiles.into_iter().collect()
    }

    pub fn ensure_tile(&mut self, tile: u32) {
        let tile = tile.clamp(MIN_UDIM, MAX_UDIM);
        self.tiles.insert(tile);
        self.active_tile = tile;
    }

    pub fn add_tile(&mut self) -> u32 {
        let used = self.tiles();
        for tile in MIN_UDIM..=MAX_UDIM {
            if !used.contains(&tile) {
                self.tiles.insert(tile);
                self.active_tile = tile;
                return tile;
            }
        }
        self.active_tile
    }

    pub fn add_paint_layer(&mut self) {
        self.layers
            .push(Layer::paint(&format!("Paint {}", self.layers.len() + 1)));
        self.active_layer = self.layers.len() - 1;
    }

    pub fn add_fill_layer(&mut self, color: [f32; 4]) {
        self.layers.push(Layer::fill(
            &format!("Fill {}", self.layers.len() + 1),
            color,
        ));
        self.active_layer = self.layers.len() - 1;
    }

    pub fn remove_layer(&mut self, index: usize) {
        if self.layers.len() <= 1 || index >= self.layers.len() {
            return;
        }
        self.layers.remove(index);
        self.active_layer = self.active_layer.min(self.layers.len() - 1);
    }

    pub fn move_layer(&mut self, index: usize, delta: i32) {
        if index >= self.layers.len() {
            return;
        }
        let target = (index as i32 + delta).clamp(0, self.layers.len() as i32 - 1) as usize;
        if target == index {
            return;
        }
        let layer = self.layers.remove(index);
        self.layers.insert(target, layer);
        self.active_layer = target;
    }

    pub fn composite(&self, tile: u32, channel: &str) -> Vec<u8> {
        let count = tile_pixels(self.tile_size);
        let mut output = vec![0u8; count * 4];
        for layer in &self.layers {
            if !layer.visible || layer.opacity <= 0.0 {
                continue;
            }
            for index in 0..count {
                let source = layer.pixel(tile, channel, index, self.tile_size);
                let source_alpha = source[3] * layer.opacity * layer.mask(tile, index);
                if source_alpha <= 0.0 {
                    continue;
                }
                let offset = index * 4;
                let destination = [
                    output[offset] as f32 / 255.0,
                    output[offset + 1] as f32 / 255.0,
                    output[offset + 2] as f32 / 255.0,
                    output[offset + 3] as f32 / 255.0,
                ];
                let blended = blend_color(layer.blend, destination, source);
                let alpha = (source_alpha + destination[3] * (1.0 - source_alpha)).clamp(0.0, 1.0);
                for component in 0..3 {
                    let value = destination[component] * (1.0 - source_alpha)
                        + blended[component] * source_alpha;
                    output[offset + component] = (value.clamp(0.0, 1.0) * 255.0 + 0.5) as u8;
                }
                output[offset + 3] = (alpha * 255.0 + 0.5) as u8;
            }
        }
        output
    }

    pub fn composite_all(&self) -> HashMap<(u32, String), Vec<u8>> {
        let mut maps = HashMap::new();
        for tile in self.tiles() {
            for channel in &self.channels {
                maps.insert(
                    (tile, channel.name.clone()),
                    self.composite(tile, &channel.name),
                );
            }
        }
        maps
    }

    /// Paints one dab. Returns false when the active layer is not paintable or
    /// the UV is outside the tile.
    pub fn paint(&mut self, tile: u32, uv: [f32; 2]) -> bool {
        if !(0.0..=1.0).contains(&uv[0]) || !(0.0..=1.0).contains(&uv[1]) {
            return false;
        }
        let Some(layer) = self.layers.get_mut(self.active_layer) else {
            return false;
        };
        if !matches!(layer.kind, LayerKind::Paint) {
            return false;
        }
        let channel = self
            .channels
            .get(self.active_channel)
            .map(|channel| channel.name.clone())
            .unwrap_or_else(|| CHANNEL_BASE.to_owned());
        let count = tile_pixels(self.tile_size);
        let center_x = (uv[0] * self.tile_size as f32) as i32;
        let center_y = ((1.0 - uv[1]) * self.tile_size as f32) as i32;
        let radius = self.brush.radius.max(1.0);
        let radius_i = radius.ceil() as i32;
        let opacity = self.brush.opacity.clamp(0.0, 1.0);
        let hardness = self.brush.hardness.clamp(0.0, 1.0);
        if self.paint_mask {
            let mask = layer.masks.entry(tile).or_insert_with(|| vec![255; count]);
            for y in
                (center_y - radius_i).max(0)..=(center_y + radius_i).min(self.tile_size as i32 - 1)
            {
                for x in (center_x - radius_i).max(0)
                    ..=(center_x + radius_i).min(self.tile_size as i32 - 1)
                {
                    let distance = (((x - center_x).pow(2) + (y - center_y).pow(2)) as f32).sqrt();
                    if distance > radius {
                        continue;
                    }
                    let falloff = brush_falloff(distance / radius, hardness) * opacity;
                    let index = y as usize * self.tile_size as usize + x as usize;
                    let value = (mask[index] as f32 / 255.0 * (1.0 - falloff)
                        + falloff * self.brush.color[3])
                        * 255.0;
                    mask[index] = value.clamp(0.0, 255.0) as u8;
                }
            }
            return true;
        }
        ensure_layer_pixels(layer, tile, &channel);
        let pixels = layer
            .pixels
            .get_mut(&tile)
            .and_then(|channels| channels.get_mut(&channel))
            .expect("paint pixels");
        if pixels.len() < count * 4 {
            pixels.resize(count * 4, 0);
        }
        let base = [
            self.brush.color[0],
            self.brush.color[1],
            self.brush.color[2],
            self.brush.color[3],
        ];
        for y in (center_y - radius_i).max(0)..=(center_y + radius_i).min(self.tile_size as i32 - 1)
        {
            for x in
                (center_x - radius_i).max(0)..=(center_x + radius_i).min(self.tile_size as i32 - 1)
            {
                let distance = (((x - center_x).pow(2) + (y - center_y).pow(2)) as f32).sqrt();
                if distance > radius {
                    continue;
                }
                let falloff = brush_falloff(distance / radius, hardness) * opacity;
                let index = y as usize * self.tile_size as usize + x as usize;
                let offset = index * 4;
                let source_alpha = base[3] * falloff;
                for component in 0..3 {
                    let existing = pixels[offset + component] as f32 / 255.0;
                    let value = existing * (1.0 - source_alpha) + base[component] * source_alpha;
                    pixels[offset + component] = (value.clamp(0.0, 1.0) * 255.0 + 0.5) as u8;
                }
                pixels[offset + 3] = ((pixels[offset + 3] as f32 / 255.0
                    + source_alpha * (1.0 - pixels[offset + 3] as f32 / 255.0))
                    .clamp(0.0, 1.0)
                    * 255.0
                    + 0.5) as u8;
            }
        }
        true
    }

    /// Bakes a height-derived AO or curvature channel into a new paint layer.
    /// `cavity` selects the signed cavity/curvature response; otherwise the
    /// result is a white ambient-occlusion map.
    pub fn bake_from_height(&mut self, channel: &str, cavity: bool, radius: f32, strength: f32) {
        let mut layer = Layer::paint(if cavity { "Curvature bake" } else { "AO bake" });
        for tile in self.tiles() {
            let height = self.composite(tile, CHANNEL_HEIGHT);
            let count = tile_pixels(self.tile_size);
            let values: Vec<f32> = height
                .chunks(4)
                .map(|pixel| pixel[0] as f32 / 255.0)
                .collect();
            let blurred = blur_scalar(&values, self.tile_size as usize, radius.max(1.0) as usize);
            let mut pixels = vec![0u8; count * 4];
            for index in 0..count {
                let center = values[index];
                let average = blurred[index];
                let value = if cavity {
                    (0.5 + (center - average) * strength.clamp(0.0, 10.0) * 8.0).clamp(0.0, 1.0)
                } else {
                    (1.0 - (average - center).max(0.0) * strength.clamp(0.0, 10.0) * 8.0)
                        .clamp(0.0, 1.0)
                };
                let byte = (value * 255.0 + 0.5) as u8;
                pixels[index * 4..index * 4 + 4].copy_from_slice(&[byte, byte, byte, 255]);
            }
            layer
                .pixels
                .entry(tile)
                .or_default()
                .insert(channel.to_owned(), pixels);
        }
        self.layers.push(layer);
        self.active_layer = self.layers.len() - 1;
    }

    /// Imports a full RGBA image into a channel (normal/position/ID bakes).
    pub fn import_rgba_channel(&mut self, tile: u32, channel: &str, data: &TextureData) {
        let mut layer = Layer::paint(&format!("Imported {channel}"));
        let size = self.tile_size as usize;
        let mut pixels = vec![0u8; size * size * 4];
        for y in 0..size {
            for x in 0..size {
                let sx =
                    (x * data.width as usize / size).min(data.width.saturating_sub(1) as usize);
                let sy =
                    (y * data.height as usize / size).min(data.height.saturating_sub(1) as usize);
                let source = (sy * data.width as usize + sx) * 4;
                let target = (y * size + x) * 4;
                for component in 0..4 {
                    pixels[target + component] =
                        data.rgba.get(source + component).copied().unwrap_or(255);
                }
            }
        }
        layer
            .pixels
            .entry(tile)
            .or_default()
            .insert(channel.to_owned(), pixels);
        self.layers.push(layer);
        self.active_layer = self.layers.len() - 1;
        self.tiles.insert(tile);
    }

    /// Imports one channel from a decoded image (for example Photon's AO/ORM
    /// bake) into a new paint layer for a tile.
    pub fn import_channel(
        &mut self,
        tile: u32,
        channel: &str,
        data: &TextureData,
        component: usize,
    ) {
        let mut layer = Layer::paint(&format!("Imported {channel}"));
        let size = self.tile_size as usize;
        let mut pixels = vec![0u8; size * size * 4];
        for y in 0..size {
            for x in 0..size {
                let sx =
                    (x * data.width as usize / size).min(data.width.saturating_sub(1) as usize);
                let sy =
                    (y * data.height as usize / size).min(data.height.saturating_sub(1) as usize);
                let offset = (sy * data.width as usize + sx) * 4;
                let value = data
                    .rgba
                    .get(offset + component.min(3))
                    .copied()
                    .unwrap_or(255);
                let index = (y * size + x) * 4;
                pixels[index..index + 4].copy_from_slice(&[value, value, value, 255]);
            }
        }
        layer
            .pixels
            .entry(tile)
            .or_default()
            .insert(channel.to_owned(), pixels);
        self.layers.push(layer);
        self.active_layer = self.layers.len() - 1;
        self.tiles.insert(tile);
    }

    pub fn snapshot_tile(&self, tile: u32) -> TileSnapshot {
        TileSnapshot {
            tile,
            layers: self
                .layers
                .iter()
                .map(|layer| LayerTileSnapshot {
                    pixels: layer.pixels.get(&tile).cloned().unwrap_or_default(),
                    mask: layer.masks.get(&tile).cloned(),
                })
                .collect(),
        }
    }

    pub fn restore_tile(&mut self, snapshot: &TileSnapshot) {
        for (layer, data) in self.layers.iter_mut().zip(snapshot.layers.iter()) {
            if data.pixels.is_empty() {
                layer.pixels.remove(&snapshot.tile);
            } else {
                layer.pixels.insert(snapshot.tile, data.pixels.clone());
            }
            match &data.mask {
                Some(mask) => {
                    layer.masks.insert(snapshot.tile, mask.clone());
                }
                None => {
                    layer.masks.remove(&snapshot.tile);
                }
            }
        }
    }

    pub fn save(&self, path: &std::path::Path) -> Result<(), String> {
        let mut cursor = Cursor::new(Vec::new());
        {
            let mut writer = zip::ZipWriter::new(&mut cursor);
            let manifest = self.manifest();
            let json = serde_json::to_vec_pretty(&manifest).map_err(|error| error.to_string())?;
            writer
                .start_file(
                    "texture-set.json",
                    zip::write::SimpleFileOptions::default()
                        .compression_method(zip::CompressionMethod::Deflated),
                )
                .map_err(|error| error.to_string())?;
            writer.write_all(&json).map_err(|error| error.to_string())?;
            for (layer_index, layer) in self.layers.iter().enumerate() {
                for (tile, channels) in &layer.pixels {
                    for (channel, pixels) in channels {
                        let file = format!("tiles/{tile}/{layer_index}_{channel}.png");
                        let png = encode_rgba(pixels, self.tile_size)?;
                        writer
                            .start_file(
                                &file,
                                zip::write::SimpleFileOptions::default()
                                    .compression_method(zip::CompressionMethod::Stored),
                            )
                            .map_err(|error| error.to_string())?;
                        writer.write_all(&png).map_err(|error| error.to_string())?;
                    }
                }
                for (tile, mask) in &layer.masks {
                    let file = format!("tiles/{tile}/{layer_index}_mask.png");
                    let png = encode_gray(mask, self.tile_size)?;
                    writer
                        .start_file(
                            &file,
                            zip::write::SimpleFileOptions::default()
                                .compression_method(zip::CompressionMethod::Stored),
                        )
                        .map_err(|error| error.to_string())?;
                    writer.write_all(&png).map_err(|error| error.to_string())?;
                }
            }
            writer.finish().map_err(|error| error.to_string())?;
        }
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).map_err(|error| error.to_string())?;
        }
        std::fs::write(path, cursor.into_inner()).map_err(|error| error.to_string())
    }

    pub fn load(path: &std::path::Path) -> Result<Self, String> {
        let bytes = std::fs::read(path).map_err(|error| format!("{}: {error}", path.display()))?;
        let mut archive = zip::ZipArchive::new(Cursor::new(bytes))
            .map_err(|error| format!("{} is not a .texset: {error}", path.display()))?;
        let manifest_bytes = read_entry(&mut archive, "texture-set.json")?;
        let manifest: TextureSetManifest =
            serde_json::from_slice(&manifest_bytes).map_err(|error| error.to_string())?;
        if manifest.version > 1 {
            return Err(format!("texture set version {} is newer", manifest.version));
        }
        let mut set = TextureSet {
            name: manifest.name,
            tile_size: manifest.tile_size.clamp(64, 4096),
            channels: manifest.channels,
            layers: manifest
                .layers
                .into_iter()
                .map(|meta| Layer {
                    name: meta.name,
                    kind: match meta.kind {
                        TextureSetLayerKind::Paint => LayerKind::Paint,
                        TextureSetLayerKind::Fill => {
                            LayerKind::Fill(meta.color.unwrap_or([0.5, 0.5, 0.5, 1.0]))
                        }
                        TextureSetLayerKind::Texture => LayerKind::Texture {
                            path: meta.path.unwrap_or_default(),
                            data: None,
                        },
                    },
                    visible: meta.visible,
                    opacity: meta.opacity.clamp(0.0, 1.0),
                    blend: BlendMode::from_index(meta.blend),
                    pixels: BTreeMap::new(),
                    masks: BTreeMap::new(),
                })
                .collect(),
            active_layer: 0,
            active_channel: 0,
            active_tile: manifest.active_tile,
            tiles: manifest.tiles.into_iter().collect(),
            brush: manifest.brush,
            paint_mask: false,
        };
        for entry in manifest.entries {
            set.tiles.insert(entry.tile);
            let bytes = read_entry(&mut archive, &entry.file)?;
            let image = image::load_from_memory(&bytes)
                .map_err(|error| format!("{}: {error}", entry.file))?
                .to_rgba8();
            if entry.mask {
                let gray: Vec<u8> = image.pixels().map(|pixel| pixel[0]).collect();
                if let Some(layer) = set.layers.get_mut(entry.layer) {
                    layer.masks.insert(entry.tile, gray);
                }
            } else if let Some(layer) = set.layers.get_mut(entry.layer) {
                layer
                    .pixels
                    .entry(entry.tile)
                    .or_default()
                    .insert(entry.channel, image.into_raw());
            }
        }
        if set.layers.is_empty() {
            set.layers.push(Layer::paint("Paint"));
        }
        set.active_layer = set.active_layer.min(set.layers.len() - 1);
        set.active_channel = set.active_channel.min(set.channels.len().saturating_sub(1));
        set.tiles.insert(set.active_tile);
        Ok(set)
    }

    fn manifest(&self) -> TextureSetManifest {
        let mut entries = Vec::new();
        for (layer_index, layer) in self.layers.iter().enumerate() {
            for (tile, channels) in &layer.pixels {
                for channel in channels.keys() {
                    entries.push(TextureSetEntry {
                        layer: layer_index,
                        tile: *tile,
                        channel: channel.clone(),
                        file: format!("tiles/{tile}/{layer_index}_{channel}.png"),
                        mask: false,
                    });
                }
            }
            for tile in layer.masks.keys() {
                entries.push(TextureSetEntry {
                    layer: layer_index,
                    tile: *tile,
                    channel: String::new(),
                    file: format!("tiles/{tile}/{layer_index}_mask.png"),
                    mask: true,
                });
            }
        }
        TextureSetManifest {
            version: 1,
            name: self.name.clone(),
            tile_size: self.tile_size,
            active_tile: self.active_tile,
            tiles: self.tiles(),
            channels: self.channels.clone(),
            layers: self
                .layers
                .iter()
                .map(|layer| TextureSetLayerMeta {
                    name: layer.name.clone(),
                    kind: match &layer.kind {
                        LayerKind::Paint => TextureSetLayerKind::Paint,
                        LayerKind::Fill(_) => TextureSetLayerKind::Fill,
                        LayerKind::Texture { .. } => TextureSetLayerKind::Texture,
                    },
                    color: match &layer.kind {
                        LayerKind::Fill(color) => Some(*color),
                        _ => None,
                    },
                    path: match &layer.kind {
                        LayerKind::Texture { path, .. } => Some(path.clone()),
                        _ => None,
                    },
                    visible: layer.visible,
                    opacity: layer.opacity,
                    blend: blend_index(layer.blend),
                })
                .collect(),
            brush: self.brush.clone(),
            entries,
        }
    }
}

fn read_entry(
    archive: &mut zip::ZipArchive<Cursor<Vec<u8>>>,
    name: &str,
) -> Result<Vec<u8>, String> {
    let entry = archive
        .by_name(name)
        .map_err(|_| format!("{name} is missing"))?;
    if entry.size() > MAX_TILE_BYTES {
        return Err(format!("{name} is too large"));
    }
    let mut bytes = Vec::new();
    entry
        .take(MAX_TILE_BYTES + 1)
        .read_to_end(&mut bytes)
        .map_err(|error| error.to_string())?;
    Ok(bytes)
}

fn encode_rgba(pixels: &[u8], size: u32) -> Result<Vec<u8>, String> {
    let expected = tile_pixels(size) * 4;
    let mut data = pixels.to_vec();
    data.resize(expected, 0);
    let image = image::RgbaImage::from_raw(size, size, data)
        .ok_or_else(|| "invalid tile pixel buffer".to_owned())?;
    let mut bytes = Cursor::new(Vec::new());
    image
        .write_to(&mut bytes, image::ImageFormat::Png)
        .map_err(|error| error.to_string())?;
    Ok(bytes.into_inner())
}

fn encode_gray(pixels: &[u8], size: u32) -> Result<Vec<u8>, String> {
    let expected = tile_pixels(size);
    let mut data = pixels.to_vec();
    data.resize(expected, 255);
    let image = image::GrayImage::from_raw(size, size, data)
        .ok_or_else(|| "invalid tile mask buffer".to_owned())?;
    let mut bytes = Cursor::new(Vec::new());
    image
        .write_to(&mut bytes, image::ImageFormat::Png)
        .map_err(|error| error.to_string())?;
    Ok(bytes.into_inner())
}

fn blur_scalar(values: &[f32], resolution: usize, radius: usize) -> Vec<f32> {
    if radius == 0 || values.len() < resolution * resolution {
        return values.to_vec();
    }
    let mut out = vec![0.0; values.len()];
    for y in 0..resolution {
        for x in 0..resolution {
            let mut sum = 0.0f32;
            let mut count = 0.0f32;
            for oy in -(radius as isize)..=(radius as isize) {
                for ox in -(radius as isize)..=(radius as isize) {
                    let sx = (x as isize + ox).clamp(0, resolution as isize - 1) as usize;
                    let sy = (y as isize + oy).clamp(0, resolution as isize - 1) as usize;
                    sum += values[sy * resolution + sx];
                    count += 1.0;
                }
            }
            out[y * resolution + x] = sum / count.max(1.0);
        }
    }
    out
}

fn brush_falloff(distance: f32, hardness: f32) -> f32 {
    let edge = 1.0 - hardness.clamp(0.0, 0.99);
    if distance <= hardness {
        1.0
    } else {
        let t = ((distance - hardness) / edge.max(0.01)).clamp(0.0, 1.0);
        (1.0 - t * t * (3.0 - 2.0 * t)).clamp(0.0, 1.0)
    }
}

fn blend_color(mode: BlendMode, base: [f32; 4], blend: [f32; 4]) -> [f32; 4] {
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
        };
        out[channel] = out[channel].clamp(0.0, 1.0);
    }
    out
}

fn blend_index(mode: BlendMode) -> usize {
    BlendMode::ALL
        .iter()
        .position(|candidate| *candidate == mode)
        .unwrap_or(0)
}

#[derive(Debug, Clone)]
pub struct LayerTileSnapshot {
    pixels: BTreeMap<String, Vec<u8>>,
    mask: Option<Vec<u8>>,
}

#[derive(Debug, Clone)]
pub struct TileSnapshot {
    pub tile: u32,
    layers: Vec<LayerTileSnapshot>,
}

#[derive(Debug, Clone, Default)]
pub struct TileHistory {
    undo: Vec<TileSnapshot>,
    redo: Vec<TileSnapshot>,
}

impl TileHistory {
    pub fn push(&mut self, snapshot: TileSnapshot) {
        self.undo.push(snapshot);
        if self.undo.len() > 64 {
            self.undo.remove(0);
        }
        self.redo.clear();
    }

    pub fn undo(&mut self, set: &mut TextureSet) -> bool {
        let Some(snapshot) = self.undo.pop() else {
            return false;
        };
        let current = set.snapshot_tile(snapshot.tile);
        set.restore_tile(&snapshot);
        self.redo.push(current);
        true
    }

    pub fn redo(&mut self, set: &mut TextureSet) -> bool {
        let Some(snapshot) = self.redo.pop() else {
            return false;
        };
        let current = set.snapshot_tile(snapshot.tile);
        set.restore_tile(&snapshot);
        self.undo.push(current);
        true
    }

    pub fn can_undo(&self) -> bool {
        !self.undo.is_empty()
    }

    pub fn can_redo(&self) -> bool {
        !self.redo.is_empty()
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct ExportPreset {
    pub name: &'static str,
    pub description: &'static str,
    pub outputs: Vec<ExportOutput>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct ExportOutput {
    pub file_suffix: String,
    pub channels: Vec<(usize, usize)>,
}

pub fn export_presets() -> Vec<ExportPreset> {
    vec![
        ExportPreset {
            name: "Separate maps",
            description: "baseColor, roughness, metallic, height and AO",
            outputs: vec![
                ExportOutput {
                    file_suffix: CHANNEL_BASE.to_owned(),
                    channels: vec![(0, 0)],
                },
                ExportOutput {
                    file_suffix: CHANNEL_ROUGHNESS.to_owned(),
                    channels: vec![(1, 1)],
                },
                ExportOutput {
                    file_suffix: CHANNEL_METALLIC.to_owned(),
                    channels: vec![(2, 2)],
                },
                ExportOutput {
                    file_suffix: CHANNEL_HEIGHT.to_owned(),
                    channels: vec![(3, 3)],
                },
                ExportOutput {
                    file_suffix: CHANNEL_AO.to_owned(),
                    channels: vec![(4, 4)],
                },
            ],
        },
        ExportPreset {
            name: "glTF ORM",
            description: "AO/roughness/metallic packed into R/G/B",
            outputs: vec![ExportOutput {
                file_suffix: "orm".to_owned(),
                channels: vec![(0, 4), (1, 1), (2, 2)],
            }],
        },
        ExportPreset {
            name: "Mesh maps",
            description: "world normal, world position and object ID",
            outputs: vec![
                ExportOutput {
                    file_suffix: CHANNEL_NORMAL.to_owned(),
                    channels: vec![(6, 0)],
                },
                ExportOutput {
                    file_suffix: CHANNEL_POSITION.to_owned(),
                    channels: vec![(7, 0)],
                },
                ExportOutput {
                    file_suffix: CHANNEL_OBJECT_ID.to_owned(),
                    channels: vec![(8, 0)],
                },
            ],
        },
        ExportPreset {
            name: "Unity HDRP mask",
            description: "metallic/AO/smoothness mask map",
            outputs: vec![ExportOutput {
                file_suffix: "mask".to_owned(),
                channels: vec![(0, 2), (1, 4), (2, 1)],
            }],
        },
    ]
}

impl TextureSet {
    pub fn export(
        &self,
        directory: &std::path::Path,
        preset: &ExportPreset,
        stem: &str,
    ) -> Result<Vec<std::path::PathBuf>, String> {
        std::fs::create_dir_all(directory).map_err(|error| error.to_string())?;
        let tiles = self.tiles();
        let mut written = Vec::new();
        for output in &preset.outputs {
            for tile in &tiles {
                let mut pixels = vec![0u8; tile_pixels(self.tile_size) * 4];
                let full_color = output.channels.len() == 1
                    && self
                        .channels
                        .get(output.channels[0].0)
                        .is_some_and(|channel| channel.kind == ChannelKind::Color);
                if full_color {
                    let channel = self
                        .channels
                        .get(output.channels[0].0)
                        .map(|channel| channel.name.as_str())
                        .unwrap_or(CHANNEL_BASE);
                    pixels = self.composite(*tile, channel);
                } else {
                    let sources: Vec<Vec<u8>> = output
                        .channels
                        .iter()
                        .map(|(channel_index, _)| {
                            let channel = self
                                .channels
                                .get(*channel_index)
                                .map(|channel| channel.name.as_str())
                                .unwrap_or(CHANNEL_BASE);
                            self.composite(*tile, channel)
                        })
                        .collect();
                    for index in 0..tile_pixels(self.tile_size) {
                        let mut scalar = None;
                        for (target, (_, source_index)) in output.channels.iter().enumerate() {
                            let value = sources[target][index * 4 + source_index % 4];
                            if output.channels.len() == 1 {
                                scalar = Some(value);
                            } else if target < 4 {
                                pixels[index * 4 + target] = value;
                            }
                        }
                        if let Some(value) = scalar {
                            pixels[index * 4] = value;
                            pixels[index * 4 + 1] = value;
                            pixels[index * 4 + 2] = value;
                        }
                        pixels[index * 4 + 3] = 255;
                    }
                }
                let file = directory.join(format!("{stem}_{tile}_{}.png", output.file_suffix));
                let image = image::RgbaImage::from_raw(self.tile_size, self.tile_size, pixels)
                    .ok_or_else(|| "invalid export buffer".to_owned())?;
                image.save(&file).map_err(|error| error.to_string())?;
                written.push(file);
            }
        }
        Ok(written)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct TextureSetManifest {
    version: u32,
    name: String,
    tile_size: u32,
    active_tile: u32,
    #[serde(default)]
    tiles: Vec<u32>,
    channels: Vec<Channel>,
    layers: Vec<TextureSetLayerMeta>,
    brush: Brush,
    entries: Vec<TextureSetEntry>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct TextureSetLayerMeta {
    name: String,
    kind: TextureSetLayerKind,
    color: Option<[f32; 4]>,
    path: Option<String>,
    visible: bool,
    opacity: f32,
    blend: usize,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
enum TextureSetLayerKind {
    Paint,
    Fill,
    Texture,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct TextureSetEntry {
    layer: usize,
    tile: u32,
    channel: String,
    file: String,
    mask: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn small_set() -> TextureSet {
        TextureSet {
            tile_size: 64,
            brush: Brush {
                radius: 8.0,
                ..Brush::default()
            },
            ..TextureSet::default()
        }
    }

    #[test]
    fn painting_composites_layers_masks_and_undo_snapshots() {
        let mut set = small_set();
        let empty = set.composite(MIN_UDIM, CHANNEL_BASE);
        assert!(empty.iter().all(|value| *value == 0));
        let snapshot = set.snapshot_tile(MIN_UDIM);
        assert!(set.paint(MIN_UDIM, [0.5, 0.5]));
        let painted = set.composite(MIN_UDIM, CHANNEL_BASE);
        assert!(painted.chunks(4).any(|pixel| pixel[3] > 0));
        assert!(painted.chunks(4).any(|pixel| pixel[0] > 0));

        let mut history = TileHistory::default();
        history.push(snapshot);
        assert!(history.undo(&mut set));
        let restored = set.composite(MIN_UDIM, CHANNEL_BASE);
        assert!(restored.iter().all(|value| *value == 0));
        assert!(history.redo(&mut set));
        assert!(set
            .composite(MIN_UDIM, CHANNEL_BASE)
            .chunks(4)
            .any(|pixel| pixel[3] > 0));

        let mut masked = small_set();
        assert!(masked.paint(MIN_UDIM, [0.5, 0.5]));
        masked.paint_mask = true;
        masked.brush.color = [1.0, 1.0, 1.0, 1.0];
        assert!(masked.paint(MIN_UDIM, [0.5, 0.5]));
        let with_mask = masked.composite(MIN_UDIM, CHANNEL_BASE);
        assert!(with_mask.chunks(4).any(|pixel| pixel[3] > 0));

        let mut fills = small_set();
        fills.layers[0].visible = false;
        fills.add_fill_layer([0.8, 0.2, 0.1, 1.0]);
        let fill = fills.composite(MIN_UDIM, CHANNEL_BASE);
        assert_eq!(&fill[0..4], &[204, 51, 26, 255]);
        fills.layers[1].opacity = 0.5;
        let half = fills.composite(MIN_UDIM, CHANNEL_BASE);
        assert!(half[0] > 0 && half[0] < 204);
    }

    #[test]
    fn texture_set_round_trips_own_project_format_and_presets() {
        let root = std::env::temp_dir().join(format!(
            "doppio-texset-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|duration| duration.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&root).expect("temp dir");
        let mut set = small_set();
        set.name = "Brick".to_owned();
        assert!(set.paint(MIN_UDIM, [0.4, 0.6]));
        set.paint_mask = true;
        assert!(set.paint(MIN_UDIM, [0.4, 0.6]));
        set.paint_mask = false;
        let path = root.join("brick.texset");
        set.save(&path).expect("save");
        let loaded = TextureSet::load(&path).expect("load");
        assert_eq!(loaded.name, "Brick");
        assert_eq!(loaded.tile_size, 64);
        assert_eq!(loaded.layers.len(), set.layers.len());
        assert_eq!(
            loaded.composite(MIN_UDIM, CHANNEL_BASE),
            set.composite(MIN_UDIM, CHANNEL_BASE)
        );
        assert!(!loaded.layers[0].masks.is_empty());

        let presets = export_presets();
        assert!(presets.len() >= 3);
        let written = set
            .export(&root.join("export"), &presets[1], "brick")
            .expect("export");
        assert!(!written.is_empty());
        assert!(written[0].exists());
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn height_bakes_and_imported_scene_channels_add_layers() {
        let mut set = small_set();
        set.active_channel = 3;
        set.brush.color = [0.5, 0.5, 0.5, 1.0];
        assert!(set.paint(MIN_UDIM, [0.5, 0.5]));
        set.bake_from_height(CHANNEL_AO, false, 4.0, 1.0);
        assert!(set
            .composite(MIN_UDIM, CHANNEL_AO)
            .chunks(4)
            .any(|pixel| pixel[0] > 0));
        set.bake_from_height(CHANNEL_CURVATURE, true, 4.0, 1.0);
        assert!(set
            .layers
            .iter()
            .any(|layer| layer.name == "Curvature bake"));

        let data = TextureData {
            width: 2,
            height: 2,
            rgba: vec![
                10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255, 100, 110, 120, 255,
            ],
        };
        let before = set.layers.len();
        set.import_channel(MIN_UDIM, CHANNEL_AO, &data, 0);
        assert_eq!(set.layers.len(), before + 1);
        assert!(set
            .layers
            .last()
            .is_some_and(|layer| layer.pixels.contains_key(&MIN_UDIM)));
    }

    #[test]
    fn udim_tiles_and_layer_ordering_are_independent() {
        let mut set = small_set();
        set.add_tile();
        assert_ne!(set.active_tile, MIN_UDIM);
        assert_eq!(set.tiles().len(), 2);
        assert!(set.paint(set.active_tile, [0.2, 0.2]));
        assert!(set
            .composite(set.active_tile, CHANNEL_BASE)
            .chunks(4)
            .any(|pixel| pixel[3] > 0));
        assert!(set
            .composite(MIN_UDIM, CHANNEL_BASE)
            .iter()
            .all(|value| *value == 0));
        set.add_paint_layer();
        assert_eq!(set.layers.len(), 2);
        set.move_layer(1, -1);
        assert_eq!(set.active_layer, 0);
        set.remove_layer(0);
        assert_eq!(set.layers.len(), 1);
    }
}

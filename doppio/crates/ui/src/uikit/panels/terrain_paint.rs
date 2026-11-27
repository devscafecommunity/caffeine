//! Terrain Paint panel: the material palette of a terrain (up to 64 layers), the paint brush and auto-layering.
//!
//! A pure view. The palette lives in the engine (`doppio_engine::PaletteLayer`), the app (`windows/main/terrainpaint.rs`)
//! reads it, applies every edit through the engine's palette calls and records one undo step; a drag of a slider is one
//! step, not one per tick. The panel draws what it is given and reports an [`Edit`].

use crate::uikit::components::{
    button, checkbox, dropdown, palette_row, scroll, slider, status, text_field,
};
use crate::uikit::id::{self, UiId};
use crate::uikit::theme::{self, ChipTone};
use doppio_engine::{Falloff, PaintMode, PaletteLayer, TerrainLayers, TerrainMaskInfo};
use iced::widget::{column, container, row, text};
use iced::{Element, Length};
use std::rc::Rc;

/// The masks an auto-layering rule can name.
pub const RULE_MASKS: [&str; 3] = ["slope", "height", "curvature"];
/// The most rules the panel holds.
pub const MAX_RULES: usize = 8;
pub const RESOLUTIONS: [u32; 5] = [256, 512, 1024, 2048, 4096];

/// A number of the paint brush.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BrushAmount {
    Radius,
    Strength,
    Spacing,
}

impl BrushAmount {
    pub fn key(self) -> &'static str {
        match self {
            BrushAmount::Radius => "radius",
            BrushAmount::Strength => "strength",
            BrushAmount::Spacing => "spacing",
        }
    }

    pub fn from_key(key: &str) -> Option<BrushAmount> {
        [
            BrushAmount::Radius,
            BrushAmount::Strength,
            BrushAmount::Spacing,
        ]
        .into_iter()
        .find(|amount| amount.key() == key.to_lowercase())
    }

    pub fn range(self) -> std::ops::RangeInclusive<f32> {
        match self {
            BrushAmount::Radius => 0.1..=500.0,
            BrushAmount::Strength => 0.0..=1.0,
            BrushAmount::Spacing => 0.0..=50.0,
        }
    }

    pub fn step(self) -> f32 {
        match self {
            BrushAmount::Radius => 0.1,
            BrushAmount::Strength => 0.01,
            BrushAmount::Spacing => 0.1,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            BrushAmount::Radius => "Radius (m)",
            BrushAmount::Strength => "Strength",
            BrushAmount::Spacing => "Spacing (m)",
        }
    }
}

/// A number of the selected layer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LayerNumber {
    Tile,
    Rotation,
    TintR,
    TintG,
    TintB,
}

impl LayerNumber {
    pub const ALL: [LayerNumber; 5] = [
        LayerNumber::Tile,
        LayerNumber::Rotation,
        LayerNumber::TintR,
        LayerNumber::TintG,
        LayerNumber::TintB,
    ];

    pub fn key(self) -> &'static str {
        match self {
            LayerNumber::Tile => "tile",
            LayerNumber::Rotation => "rotation",
            LayerNumber::TintR => "tint_r",
            LayerNumber::TintG => "tint_g",
            LayerNumber::TintB => "tint_b",
        }
    }

    pub fn from_key(key: &str) -> Option<LayerNumber> {
        LayerNumber::ALL
            .into_iter()
            .find(|number| number.key() == key.to_lowercase())
    }

    pub fn label(self) -> &'static str {
        match self {
            LayerNumber::Tile => "Tile (m)",
            LayerNumber::Rotation => "Rotation",
            LayerNumber::TintR => "Tint red",
            LayerNumber::TintG => "Tint green",
            LayerNumber::TintB => "Tint blue",
        }
    }

    pub fn range(self) -> std::ops::RangeInclusive<f32> {
        match self {
            LayerNumber::Tile => 0.5..=100.0,
            LayerNumber::Rotation => -180.0..=180.0,
            _ => 0.0..=1.0,
        }
    }

    pub fn step(self) -> f32 {
        match self {
            LayerNumber::Tile => 0.1,
            LayerNumber::Rotation => 1.0,
            _ => 0.01,
        }
    }

    pub fn id(self) -> UiId {
        match self {
            LayerNumber::Tile => id::TP_TILE,
            LayerNumber::Rotation => id::TP_ROTATION,
            LayerNumber::TintR => id::TP_TINT_R,
            LayerNumber::TintG => id::TP_TINT_G,
            LayerNumber::TintB => id::TP_TINT_B,
        }
    }

    pub fn get(self, layer: &PaletteLayer) -> f32 {
        match self {
            LayerNumber::Tile => layer.tile_size_m,
            LayerNumber::Rotation => layer.rotation_degrees,
            LayerNumber::TintR => layer.tint[0],
            LayerNumber::TintG => layer.tint[1],
            LayerNumber::TintB => layer.tint[2],
        }
    }

    pub fn set(self, layer: &mut PaletteLayer, value: f32) {
        let range = self.range();
        let value = value.clamp(*range.start(), *range.end());
        match self {
            LayerNumber::Tile => layer.tile_size_m = value,
            LayerNumber::Rotation => layer.rotation_degrees = value,
            LayerNumber::TintR => layer.tint[0] = value,
            LayerNumber::TintG => layer.tint[1] = value,
            LayerNumber::TintB => layer.tint[2] = value,
        }
    }
}

/// One of the four numbers of an auto-layering rule's trapezoid (0 to 1).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RuleCorner {
    Rise0,
    Rise1,
    Fall1,
    Fall0,
}

impl RuleCorner {
    pub const ALL: [RuleCorner; 4] = [
        RuleCorner::Rise0,
        RuleCorner::Rise1,
        RuleCorner::Fall1,
        RuleCorner::Fall0,
    ];
    pub fn key(self) -> &'static str {
        match self {
            RuleCorner::Rise0 => "rise0",
            RuleCorner::Rise1 => "rise1",
            RuleCorner::Fall1 => "fall1",
            RuleCorner::Fall0 => "fall0",
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            RuleCorner::Rise0 => "From",
            RuleCorner::Rise1 => "Full from",
            RuleCorner::Fall1 => "Full to",
            RuleCorner::Fall0 => "To",
        }
    }
    pub fn index(self) -> usize {
        self as usize
    }
}

/// An auto-layering rule: where `layer` goes, by a mask and a trapezoid over it.
#[derive(Debug, Clone, PartialEq)]
pub struct AutoRule {
    pub layer: usize,
    pub mask: String,
    pub corners: [f32; 4],
}

/// A change the panel asks for.
#[derive(Debug, Clone, PartialEq)]
pub enum Edit {
    Active(bool),
    /// Layered painting on or off for the terrain.
    EnableLayers(bool),
    SelectLayer(usize),
    AddLayer,
    /// Adds a layer with these settings (the agent action; the panel's button adds a plain one).
    AddLayerWith {
        name: String,
        albedo: String,
        tint: [f32; 3],
        tile_size_m: f32,
    },
    /// Adds a layer whose picture is the asset selected in the Assets panel.
    AddLayerFromAsset,
    RemoveLayer,
    /// Moves the selected layer by this many places.
    MoveLayer(i32),
    LayerName(String),
    LayerAlbedo(String),
    LayerNumber(LayerNumber, f32),
    LayerResolution(u32),
    LayerLocked(bool),
    Mode(PaintMode),
    FromLayer(usize),
    Brush(BrushAmount, f32),
    Falloff(Falloff),
    SharedShape(bool),
    AddRule(String),
    RemoveRule(usize),
    RuleLayer(usize, usize),
    RuleCorner(usize, RuleCorner, f32),
    Fallback(usize),
    ApplyRules,
    /// Strokes paint the selected mask instead of the layers.
    MaskTarget(bool),
    SelectMask(String),
    /// The name the next new mask gets (empty: the next free `maskN`).
    MaskName(String),
    /// Makes a mask from the typed name: empty, or a copy of a built-in or stored mask.
    NewMask(Option<String>),
    /// Makes a mask with this name (the agent action; the panel uses the typed name), empty or a copy of `from`.
    MakeMask {
        name: String,
        from: Option<String>,
    },
    /// The level a Paint stroke moves the selected mask toward, 0 to 1.
    MaskValue(f32),
    MaskFill,
    MaskInvert,
    MaskDelete,
}

pub struct View<'a> {
    pub active: bool,
    /// The terrain being painted, by name; `None` when the scene has none.
    pub terrain: Option<&'a str>,
    pub layers: Option<TerrainLayers>,
    pub palette: &'a [PaletteLayer],
    pub selected: usize,
    pub mode: PaintMode,
    pub from_layer: usize,
    pub radius: f32,
    pub strength: f32,
    pub spacing: f32,
    pub falloff: Falloff,
    pub shared_shape: bool,
    pub rules: &'a [AutoRule],
    pub fallback: usize,
    pub status: Option<&'a str>,
    /// The stored masks of the terrain (painted ones and those a generator sent), not the built-in ones.
    pub masks: &'a [TerrainMaskInfo],
    pub mask_selected: Option<&'a str>,
    /// Strokes paint the selected mask instead of the layers.
    pub mask_target: bool,
    pub mask_value: f32,
    pub new_mask_name: &'a str,
}

/// The one-line summary under the panel title.
pub fn summary(active: bool, layers: Option<TerrainLayers>) -> String {
    match layers {
        Some(layers) if layers.enabled => format!(
            "{} · {} layers",
            if active { "On" } else { "Off" },
            layers.palette_count
        ),
        _ => "layers off".to_owned(),
    }
}

fn card<'a, Message: 'a>(title: &'a str, body: Element<'a, Message>) -> Element<'a, Message> {
    container(column![text(title).size(11).color(theme::TEXT), body].spacing(5))
        .padding([6, 8])
        .width(Length::Fill)
        .style(theme::component_card)
        .into()
}

fn megabytes(bytes: u64) -> String {
    format!("{:.1} MB", bytes as f64 / (1024.0 * 1024.0))
}

/// The masks a terrain holds as stored data: painted by hand here, or sent by a generator. Species rules and
/// adaptations, auto-layering rules and brush protection all read them by name.
fn masks_card<'a, Message: Clone + 'a>(
    view: &View<'a>,
    emit: &Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let mut list = column![].spacing(2);
    for (index, mask) in view.masks.iter().enumerate() {
        let select_emit = emit.clone();
        let name = mask.name.clone();
        list = list.push(palette_row::view(
            id::terrain_paint_mask(index),
            index,
            &mask.name,
            [0.7, 0.7, 0.7],
            &format!("{} x {}", mask.resolution[0], mask.resolution[1]),
            view.mask_selected == Some(mask.name.as_str()),
            select_emit(Edit::SelectMask(name)),
        ));
    }
    if view.masks.is_empty() {
        list = list.push(
            text("No stored masks yet. Make one, then paint it: a species can grow only where it is high, and a layer can go where it is.")
                .size(9)
                .color(theme::TEXT_MUTED),
        );
    }
    let name_emit = emit.clone();
    let new_emit = emit.clone();
    let from_emit = emit.clone();
    let target_emit = emit.clone();
    let value_emit = emit.clone();
    let fill_emit = emit.clone();
    let invert_emit = emit.clone();
    let delete_emit = emit.clone();
    let mut copy_sources: Vec<String> = RULE_MASKS.iter().map(|m| (*m).to_owned()).collect();
    copy_sources.extend(view.masks.iter().map(|m| m.name.clone()));
    let mut body = column![
        list,
        text_field::property(id::TP_MASK_NAME, "New mask", view.new_mask_name, move |t| {
            name_emit(Edit::MaskName(t))
        }),
        row![
            button::primary(id::TP_MASK_NEW, "Make empty", new_emit(Edit::NewMask(None))),
            dropdown::property(
                id::TP_MASK_COPY,
                "Copy of",
                copy_sources,
                None,
                move |picked: String| from_emit(Edit::NewMask(Some(picked))),
            ),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    ]
    .spacing(5);
    if let Some(selected) = view.mask_selected {
        body = body.push(checkbox::property(
            id::TP_MASK_TARGET,
            format!("The brush paints {selected} (not the layers)"),
            view.mask_target,
            move |on| target_emit(Edit::MaskTarget(on)),
        ));
        body = body.push(slider::property_step(
            id::TP_MASK_VALUE,
            "Paint level",
            0.0..=1.0,
            view.mask_value,
            0.01,
            move |v| value_emit(Edit::MaskValue(v)),
        ));
        body = body.push(
            row![
                button::compact(id::TP_MASK_FILL, "Fill", fill_emit(Edit::MaskFill)),
                button::compact(id::TP_MASK_INVERT, "Invert", invert_emit(Edit::MaskInvert)),
                button::danger(id::TP_MASK_DELETE, "Delete", delete_emit(Edit::MaskDelete)),
            ]
            .spacing(6),
        );
    }
    card("Masks", body.into())
}

fn brush_card<'a, Message: Clone + 'a>(
    view: &View<'a>,
    emit: &Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let names: Vec<String> = view
        .palette
        .iter()
        .enumerate()
        .map(|(i, l)| format!("{i}  {}", l.name))
        .collect();
    let mode_options: Vec<String> = PaintMode::ALL
        .iter()
        .map(|m| m.label().to_owned())
        .collect();
    let mode_lookup = mode_options.clone();
    let mode_emit = emit.clone();
    let falloff_options: Vec<String> = Falloff::ALL.iter().map(|f| f.label().to_owned()).collect();
    let falloff_lookup = falloff_options.clone();
    let falloff_emit = emit.clone();
    let shared_emit = emit.clone();
    let mut brush = column![dropdown::property(
        id::TP_MODE,
        "Mode",
        mode_options,
        Some(view.mode.label().to_owned()),
        move |picked: String| {
            mode_emit(Edit::Mode(
                mode_lookup
                    .iter()
                    .position(|o| *o == picked)
                    .map_or(PaintMode::Paint, |i| PaintMode::ALL[i]),
            ))
        },
    )]
    .spacing(4);
    if view.mode == PaintMode::Replace && !view.mask_target {
        let from_emit = emit.clone();
        let from_lookup = names.clone();
        brush = brush.push(dropdown::property(
            id::TP_FROM,
            "Replace",
            names.clone(),
            names.get(view.from_layer).cloned(),
            move |picked: String| {
                from_emit(Edit::FromLayer(
                    from_lookup.iter().position(|n| *n == picked).unwrap_or(0),
                ))
            },
        ));
    }
    for (amount, value, slider_id) in [
        (BrushAmount::Radius, view.radius, id::TP_RADIUS),
        (BrushAmount::Strength, view.strength, id::TP_STRENGTH),
        (BrushAmount::Spacing, view.spacing, id::TP_SPACING),
    ] {
        let amount_emit = emit.clone();
        brush = brush.push(slider::property_step(
            slider_id,
            amount.label(),
            amount.range(),
            value,
            amount.step(),
            move |v| amount_emit(Edit::Brush(amount, v)),
        ));
    }
    brush = brush.push(dropdown::property(
        id::TP_FALLOFF,
        "Falloff",
        falloff_options,
        Some(view.falloff.label().to_owned()),
        move |picked: String| {
            falloff_emit(Edit::Falloff(
                falloff_lookup
                    .iter()
                    .position(|o| *o == picked)
                    .map_or(Falloff::Smooth, |i| Falloff::ALL[i]),
            ))
        },
    ));
    brush = brush.push(checkbox::property(
        id::TP_SHARED,
        "Shape from the Terrain Brush (stamp, jitter, symmetry, protection)",
        view.shared_shape,
        move |on| shared_emit(Edit::SharedShape(on)),
    ));
    card("Paint brush", brush.into())
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let mut content = column![].spacing(6).width(Length::Fill);

    let Some(terrain) = view.terrain else {
        content = content.push(status::note(
            "No terrain in the scene. Add one from the Hierarchy's + menu, then select it.",
        ));
        return scroll::vertical(container(content).padding(8)).into();
    };
    let enabled = view.layers.is_some_and(|layers| layers.enabled);

    // The switch and the layered mode.
    let active_emit = emit.clone();
    content = content.push(
        row![
            status::chip(
                if view.active { "Painting" } else { "Off" },
                if view.active {
                    ChipTone::Success
                } else {
                    ChipTone::Neutral
                },
            ),
            checkbox::inline(
                id::TP_ACTIVE,
                "Brush on (click or drag on the terrain)",
                view.active,
                move |on| active_emit(Edit::Active(on)),
            ),
        ]
        .spacing(8)
        .align_y(iced::Alignment::Center),
    );
    if !enabled {
        let enable_emit = emit.clone();
        content = content.push(card(
            "Layers",
            column![
                text(format!(
                    "{terrain} is painted with four layers. Layered painting gives it a palette of up to 64 layers (four blend at any point), each with its own picture, tiling and sharpness."
                ))
                .size(10)
                .color(theme::TEXT_MUTED),
                button::primary(
                    id::TP_ENABLE,
                    "Turn on layered painting",
                    enable_emit(Edit::EnableLayers(true)),
                ),
            ]
            .spacing(6)
            .into(),
        ));
        content = content.push(masks_card(&view, &emit));
        content = content.push(brush_card(&view, &emit));
        if let Some(note) = view.status {
            content = content.push(status::note(note));
        }
        return scroll::vertical(container(content).padding(8)).into();
    }

    // The palette.
    let layers_info = view.layers.unwrap_or_default();
    let total: f64 = f64::from(layers_info.map[0]) * f64::from(layers_info.map[1]);
    let mut list = column![].spacing(2);
    for (index, layer) in view.palette.iter().enumerate() {
        let select_emit = emit.clone();
        let share = if total > 0.0 {
            format!("{:.0} %", f64::from(layer.texels) / total * 100.0)
        } else {
            String::new()
        };
        let detail = if layer.locked {
            format!("locked · {share}")
        } else {
            share
        };
        list = list.push(palette_row::view(
            id::terrain_paint_layer(index),
            index,
            &layer.name,
            layer.tint,
            &detail,
            view.selected == index,
            select_emit(Edit::SelectLayer(index)),
        ));
    }
    let add_emit = emit.clone();
    let add_asset_emit = emit.clone();
    let disable_emit = emit.clone();
    let mut palette = column![
        list,
        row![
            button::primary(id::TP_ADD, "Add layer", add_emit(Edit::AddLayer)),
            button::compact(
                id::TP_ADD_ASSET,
                "From selected asset",
                add_asset_emit(Edit::AddLayerFromAsset)
            ),
        ]
        .spacing(6),
        text(format!(
            "{} of {} layers · map {} x {} · {}",
            view.palette.len(),
            doppio_engine::PALETTE_MAX,
            layers_info.map[0],
            layers_info.map[1],
            if layers_info.gpu_slices > 0 {
                format!(
                    "GPU {} slices of {} px, {}",
                    layers_info.gpu_slices,
                    layers_info.gpu_array_size,
                    megabytes(layers_info.gpu_array_bytes)
                )
            } else {
                "not on the GPU yet".to_owned()
            }
        ))
        .size(9)
        .color(theme::TEXT_MUTED),
    ]
    .spacing(5);
    palette = palette.push(button::compact(
        id::TP_DISABLE,
        "Back to four layers",
        disable_emit(Edit::EnableLayers(false)),
    ));
    content = content.push(card("Palette", palette.into()));

    // The selected layer.
    if let Some(layer) = view.palette.get(view.selected) {
        let name_emit = emit.clone();
        let albedo_emit = emit.clone();
        let res_emit = emit.clone();
        let locked_emit = emit.clone();
        let up_emit = emit.clone();
        let down_emit = emit.clone();
        let remove_emit = emit.clone();
        let resolutions: Vec<String> = RESOLUTIONS.iter().map(|r| format!("{r}")).collect();
        let res_lookup = RESOLUTIONS;
        let res_options = resolutions.clone();
        let mut body = column![
            text_field::property(id::TP_NAME, "Name", &layer.name, move |t| name_emit(
                Edit::LayerName(t)
            )),
            text_field::property(id::TP_ALBEDO, "Picture", &layer.albedo, move |t| {
                albedo_emit(Edit::LayerAlbedo(t))
            }),
        ]
        .spacing(4);
        for number in LayerNumber::ALL {
            let number_emit = emit.clone();
            body = body.push(slider::property_step(
                number.id(),
                number.label(),
                number.range(),
                number.get(layer),
                number.step(),
                move |v| number_emit(Edit::LayerNumber(number, v)),
            ));
        }
        body = body.push(dropdown::property(
            id::TP_RESOLUTION,
            "Sharpness",
            res_options,
            Some(format!("{}", layer.resolution)),
            move |picked: String| {
                let index = resolutions.iter().position(|r| *r == picked).unwrap_or(2);
                res_emit(Edit::LayerResolution(res_lookup[index]))
            },
        ));
        body = body.push(checkbox::property(
            id::TP_LOCKED,
            "Locked (painting never changes it)",
            layer.locked,
            move |on| locked_emit(Edit::LayerLocked(on)),
        ));
        body = body.push(
            row![
                button::compact(id::TP_UP, "Up", up_emit(Edit::MoveLayer(-1))),
                button::compact(id::TP_DOWN, "Down", down_emit(Edit::MoveLayer(1))),
                button::danger(
                    id::TP_REMOVE,
                    "Remove layer",
                    remove_emit(Edit::RemoveLayer)
                ),
            ]
            .spacing(6),
        );
        content = content.push(card("Selected layer", body.into()));
    }

    content = content.push(masks_card(&view, &emit));
    content = content.push(brush_card(&view, &emit));

    // Auto-layering.
    let names: Vec<String> = view
        .palette
        .iter()
        .enumerate()
        .map(|(i, l)| format!("{i}  {}", l.name))
        .collect();
    let rule_add_emit = emit.clone();
    let fallback_emit = emit.clone();
    let fallback_lookup = names.clone();
    let apply_emit = emit.clone();
    let mut rules = column![
        dropdown::property(
            id::TP_RULE_ADD,
            "Add rule on",
            RULE_MASKS
                .iter()
                .map(|m| (*m).to_owned())
                .collect::<Vec<_>>(),
            None,
            move |picked: String| rule_add_emit(Edit::AddRule(picked)),
        ),
        dropdown::property(
            id::TP_FALLBACK,
            "Otherwise",
            names.clone(),
            names.get(view.fallback).cloned(),
            move |picked: String| {
                fallback_emit(Edit::Fallback(
                    fallback_lookup
                        .iter()
                        .position(|n| *n == picked)
                        .unwrap_or(0),
                ))
            },
        ),
    ]
    .spacing(4);
    if view.rules.is_empty() {
        rules = rules.push(
            text("Rules decide a layer by slope, height or curvature (0 to 1 on each). Add one, set its range, then apply.")
                .size(9)
                .color(theme::TEXT_MUTED),
        );
    }
    for (index, rule) in view.rules.iter().enumerate() {
        let layer_emit = emit.clone();
        let layer_lookup = names.clone();
        let remove_emit = emit.clone();
        let mut block = column![row![
            dropdown::property(
                id::terrain_paint_rule(index, "layer"),
                format!("{} on", rule.mask),
                names.clone(),
                names.get(rule.layer).cloned(),
                move |picked: String| {
                    layer_emit(Edit::RuleLayer(
                        index,
                        layer_lookup.iter().position(|n| *n == picked).unwrap_or(0),
                    ))
                },
            ),
            button::compact(
                id::terrain_paint_rule(index, "remove"),
                "Remove",
                remove_emit(Edit::RemoveRule(index))
            ),
        ]
        .spacing(4)
        .align_y(iced::Alignment::Center)]
        .spacing(3);
        for corner in RuleCorner::ALL {
            let corner_emit = emit.clone();
            block = block.push(slider::property_step(
                id::terrain_paint_rule(index, corner.key()),
                corner.label(),
                0.0..=1.0,
                rule.corners[corner.index()],
                0.01,
                move |v| corner_emit(Edit::RuleCorner(index, corner, v)),
            ));
        }
        rules = rules.push(block);
    }
    rules = rules.push(button::primary(
        id::TP_APPLY,
        "Apply rules",
        apply_emit(Edit::ApplyRules),
    ));
    content = content.push(card("Auto-layering", rules.into()));

    if let Some(note) = view.status {
        content = content.push(status::note(note));
    }
    scroll::vertical(container(content).padding(8)).into()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn layer() -> PaletteLayer {
        PaletteLayer {
            name: "Rock".to_owned(),
            albedo: String::new(),
            normal: String::new(),
            tile_size_m: 8.0,
            rotation_degrees: 0.0,
            tint: [1.0, 1.0, 1.0],
            resolution: 1024,
            locked: false,
            texels: 0,
        }
    }

    #[test]
    fn layer_numbers_round_trip_and_stay_in_range() {
        for number in LayerNumber::ALL {
            let mut l = layer();
            let range = number.range();
            number.set(&mut l, *range.end() + 1000.0);
            assert_eq!(number.get(&l), *range.end(), "{}", number.label());
            number.set(&mut l, *range.start() - 1000.0);
            assert_eq!(number.get(&l), *range.start(), "{}", number.label());
            assert_eq!(LayerNumber::from_key(number.key()), Some(number));
        }
    }

    #[test]
    fn every_widget_id_is_catalogued() {
        for number in LayerNumber::ALL {
            assert!(
                id::description(number.id().key()).is_some(),
                "{}",
                number.key()
            );
        }
        for key in [
            "terrain_paint.palette.layer.7",
            "terrain_paint.auto.rule.2.rise0",
            "terrain_paint.mask.row.3",
            "terrain_paint.mask.name",
            "terrain_paint.mask.new",
            "terrain_paint.mask.copy",
            "terrain_paint.mask.target",
            "terrain_paint.mask.value",
            "terrain_paint.mask.fill",
            "terrain_paint.mask.invert",
            "terrain_paint.mask.delete",
        ] {
            assert!(id::description(key).is_some(), "{key}");
        }
    }

    #[test]
    fn brush_amounts_are_named_by_key() {
        assert_eq!(BrushAmount::from_key("Radius"), Some(BrushAmount::Radius));
        assert_eq!(BrushAmount::from_key("nope"), None);
    }

    #[test]
    fn the_summary_says_when_layers_are_off() {
        assert_eq!(summary(true, None), "layers off");
        let on = TerrainLayers {
            enabled: true,
            palette_count: 6,
            ..TerrainLayers::default()
        };
        assert_eq!(summary(true, Some(on)), "On · 6 layers");
        assert_eq!(summary(false, Some(on)), "Off · 6 layers");
    }
}

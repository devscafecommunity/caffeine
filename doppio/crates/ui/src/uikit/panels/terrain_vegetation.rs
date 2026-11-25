//! Terrain Vegetation panel: the species palette of a terrain, the vegetation brush and the procedural source.
//!
//! A pure view. The vegetation lives in the engine (`doppio_engine::VegSpecies`); the app
//! (`windows/main/terrainvegetation.rs`) reads it, applies every edit through the engine's species calls and records one
//! undo step (a slider drag folds into one), and a stroke goes through the same mouse path as sculpting and painting. The
//! panel draws what it is given and reports an [`Edit`].

use super::terrain_paint::{BrushAmount, RuleCorner};
use crate::uikit::components::{
    button, checkbox, dropdown, palette_row, scroll, slider, status, text_field,
};
use crate::uikit::id::{self, UiId};
use crate::uikit::theme::{self, ChipTone};
use doppio_engine::{VegInfo, VegSpecies, VegTool};
use iced::widget::{column, container, row, text};
use iced::{Element, Length};
use std::rc::Rc;

/// The masks a species rule can name.
pub const RULE_MASKS: [&str; 3] = ["slope", "height", "curvature"];

/// A number of the selected species.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpeciesNumber {
    Density,
    Radius,
    Priority,
    MinScale,
    MaxScale,
    Tilt,
    Sink,
    Jitter,
    TintR,
    TintG,
    TintB,
    Distance,
}

impl SpeciesNumber {
    pub const ALL: [SpeciesNumber; 12] = [
        SpeciesNumber::Density,
        SpeciesNumber::Radius,
        SpeciesNumber::Priority,
        SpeciesNumber::MinScale,
        SpeciesNumber::MaxScale,
        SpeciesNumber::Tilt,
        SpeciesNumber::Sink,
        SpeciesNumber::Jitter,
        SpeciesNumber::TintR,
        SpeciesNumber::TintG,
        SpeciesNumber::TintB,
        SpeciesNumber::Distance,
    ];

    pub fn key(self) -> &'static str {
        match self {
            SpeciesNumber::Density => "density",
            SpeciesNumber::Radius => "radius",
            SpeciesNumber::Priority => "priority",
            SpeciesNumber::MinScale => "min_scale",
            SpeciesNumber::MaxScale => "max_scale",
            SpeciesNumber::Tilt => "tilt",
            SpeciesNumber::Sink => "sink",
            SpeciesNumber::Jitter => "tint_jitter",
            SpeciesNumber::TintR => "tint_r",
            SpeciesNumber::TintG => "tint_g",
            SpeciesNumber::TintB => "tint_b",
            SpeciesNumber::Distance => "distance",
        }
    }

    pub fn from_key(key: &str) -> Option<SpeciesNumber> {
        let wanted = key.to_lowercase().replace('-', "_");
        SpeciesNumber::ALL.into_iter().find(|n| n.key() == wanted)
    }

    pub fn label(self) -> &'static str {
        match self {
            SpeciesNumber::Density => "Per hectare",
            SpeciesNumber::Radius => "Room (m)",
            SpeciesNumber::Priority => "Priority",
            SpeciesNumber::MinScale => "Smallest",
            SpeciesNumber::MaxScale => "Largest",
            SpeciesNumber::Tilt => "Lean to ground",
            SpeciesNumber::Sink => "Sink (m)",
            SpeciesNumber::Jitter => "Colour jitter",
            SpeciesNumber::TintR => "Tint red",
            SpeciesNumber::TintG => "Tint green",
            SpeciesNumber::TintB => "Tint blue",
            SpeciesNumber::Distance => "Drawn to (m)",
        }
    }

    pub fn range(self) -> std::ops::RangeInclusive<f32> {
        match self {
            SpeciesNumber::Density => 0.0..=20000.0,
            SpeciesNumber::Radius => 0.0..=30.0,
            SpeciesNumber::Priority => -10.0..=10.0,
            SpeciesNumber::MinScale | SpeciesNumber::MaxScale => 0.05..=20.0,
            SpeciesNumber::Tilt | SpeciesNumber::Jitter => 0.0..=1.0,
            SpeciesNumber::Sink => -2.0..=5.0,
            SpeciesNumber::TintR | SpeciesNumber::TintG | SpeciesNumber::TintB => 0.0..=1.0,
            SpeciesNumber::Distance => 10.0..=2000.0,
        }
    }

    pub fn step(self) -> f32 {
        match self {
            SpeciesNumber::Density => 5.0,
            SpeciesNumber::Priority => 1.0,
            SpeciesNumber::Distance => 5.0,
            SpeciesNumber::Radius
            | SpeciesNumber::MinScale
            | SpeciesNumber::MaxScale
            | SpeciesNumber::Sink => 0.05,
            _ => 0.01,
        }
    }

    pub fn id(self) -> UiId {
        match self {
            SpeciesNumber::Density => id::TV_DENSITY,
            SpeciesNumber::Radius => id::TV_RADIUS,
            SpeciesNumber::Priority => id::TV_PRIORITY,
            SpeciesNumber::MinScale => id::TV_SCALE_MIN,
            SpeciesNumber::MaxScale => id::TV_SCALE_MAX,
            SpeciesNumber::Tilt => id::TV_TILT,
            SpeciesNumber::Sink => id::TV_SINK,
            SpeciesNumber::Jitter => id::TV_JITTER,
            SpeciesNumber::TintR => id::TV_TINT_R,
            SpeciesNumber::TintG => id::TV_TINT_G,
            SpeciesNumber::TintB => id::TV_TINT_B,
            SpeciesNumber::Distance => id::TV_DISTANCE,
        }
    }

    pub fn get(self, s: &VegSpecies) -> f32 {
        match self {
            SpeciesNumber::Density => s.density_per_ha,
            SpeciesNumber::Radius => s.radius_m,
            SpeciesNumber::Priority => s.priority as f32,
            SpeciesNumber::MinScale => s.min_scale,
            SpeciesNumber::MaxScale => s.max_scale,
            SpeciesNumber::Tilt => s.tilt,
            SpeciesNumber::Sink => s.sink_m,
            SpeciesNumber::Jitter => s.tint_jitter,
            SpeciesNumber::TintR => s.tint[0],
            SpeciesNumber::TintG => s.tint[1],
            SpeciesNumber::TintB => s.tint[2],
            SpeciesNumber::Distance => s.max_distance_m,
        }
    }

    /// Writes `value` kept inside the range; the largest scale never goes under the smallest.
    pub fn set(self, s: &mut VegSpecies, value: f32) {
        let range = self.range();
        let value = value.clamp(*range.start(), *range.end());
        match self {
            SpeciesNumber::Density => s.density_per_ha = value,
            SpeciesNumber::Radius => s.radius_m = value,
            SpeciesNumber::Priority => s.priority = value.round() as i32,
            SpeciesNumber::MinScale => {
                s.min_scale = value;
                s.max_scale = s.max_scale.max(value);
            }
            SpeciesNumber::MaxScale => {
                s.max_scale = value;
                s.min_scale = s.min_scale.min(value);
            }
            SpeciesNumber::Tilt => s.tilt = value,
            SpeciesNumber::Sink => s.sink_m = value,
            SpeciesNumber::Jitter => s.tint_jitter = value,
            SpeciesNumber::TintR => s.tint[0] = value,
            SpeciesNumber::TintG => s.tint[1] = value,
            SpeciesNumber::TintB => s.tint[2] = value,
            SpeciesNumber::Distance => s.max_distance_m = value,
        }
    }
}

/// A change the panel asks for.
#[derive(Debug, Clone, PartialEq)]
pub enum Edit {
    Active(bool),
    /// Vegetation on or off for the terrain.
    Enable(bool),
    Select(usize),
    AddSpecies,
    /// Adds a species with these settings (the agent action; the panel's button adds a plain one).
    AddSpeciesWith {
        name: String,
        meshes: Vec<String>,
        tint: [f32; 3],
        density_per_ha: f32,
        radius_m: f32,
    },
    AddSpeciesFromAsset,
    AddVariantFromAsset,
    /// Appends a variant by project path (the agent action).
    AddVariant(String),
    RemoveVariant(usize),
    RemoveSpecies,
    Name(String),
    Material(String),
    Number(SpeciesNumber, f32),
    Procedural(bool),
    Visible(bool),
    AddRule(String),
    RemoveRule(usize),
    Rule(usize, RuleCorner, f32),
    Tool(VegTool),
    Brush(BrushAmount, f32),
    Per100(f32),
    Amount(f32),
    /// Erase, scale...: act on this species only (-1 for all).
    Only(i32),
    Respect(bool),
    Shared(bool),
    Seed(u64),
    Reseed,
    Bake,
    Rederive,
}

pub struct View<'a> {
    pub active: bool,
    pub terrain: Option<&'a str>,
    pub info: Option<VegInfo>,
    pub species: &'a [VegSpecies],
    pub selected: usize,
    pub tool: VegTool,
    pub radius: f32,
    pub strength: f32,
    pub spacing: f32,
    pub per_100m2: f32,
    pub amount: f32,
    pub only: i32,
    pub respect_rules: bool,
    pub shared_shape: bool,
    pub seed: u64,
    pub status: Option<&'a str>,
}

/// The one-line summary under the panel title.
pub fn summary(active: bool, info: Option<VegInfo>) -> String {
    match info {
        Some(info) if info.enabled => format!(
            "{} · {} species",
            if active { "On" } else { "Off" },
            info.species_count
        ),
        _ => "vegetation off".to_owned(),
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
    let enabled = view.info.is_some_and(|info| info.enabled);

    let active_emit = emit.clone();
    content = content.push(
        row![
            status::chip(
                if view.active { "Planting" } else { "Off" },
                if view.active {
                    ChipTone::Success
                } else {
                    ChipTone::Neutral
                },
            ),
            checkbox::inline(
                id::TV_ACTIVE,
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
            "Vegetation",
            column![
                text(format!(
                    "{terrain} has no vegetation yet. Turning it on gives it a palette of species (trees, plants, rocks) that grow by themselves by rules and that you can paint, erase and edit by hand. Plants are terrain data, not entities."
                ))
                .size(10)
                .color(theme::TEXT_MUTED),
                button::primary(id::TV_ENABLE, "Turn on vegetation", enable_emit(Edit::Enable(true))),
            ]
            .spacing(6)
            .into(),
        ));
        if let Some(note) = view.status {
            content = content.push(status::note(note));
        }
        return scroll::vertical(container(content).padding(8)).into();
    }

    // The palette.
    let info = view.info.unwrap_or_default();
    let mut list = column![].spacing(2);
    for (index, species) in view.species.iter().enumerate() {
        let select_emit = emit.clone();
        let detail = format!(
            "{}{}{}",
            if species.procedural {
                "grows"
            } else {
                "manual"
            },
            if species.manual_instances > 0 {
                format!(" · {} placed", species.manual_instances)
            } else {
                String::new()
            },
            if species.visible { "" } else { " · hidden" }
        );
        list = list.push(palette_row::view(
            id::terrain_vegetation_species(index),
            index,
            &species.name,
            species.tint,
            &detail,
            view.selected == index,
            select_emit(Edit::Select(index)),
        ));
    }
    let add_emit = emit.clone();
    let add_asset_emit = emit.clone();
    let disable_emit = emit.clone();
    let palette = column![
        list,
        row![
            button::primary(id::TV_ADD, "Add species", add_emit(Edit::AddSpecies)),
            button::compact(
                id::TV_ADD_ASSET,
                "From selected asset",
                add_asset_emit(Edit::AddSpeciesFromAsset)
            ),
        ]
        .spacing(6),
        text(format!(
            "{} species · {} placed by hand · {} edits of grown plants · {} on the GPU",
            view.species.len(),
            info.manual_instances,
            info.overrides,
            megabytes(info.gpu_bytes)
        ))
        .size(9)
        .color(theme::TEXT_MUTED),
        button::compact(
            id::TV_DISABLE,
            "Stop drawing vegetation",
            disable_emit(Edit::Enable(false))
        ),
    ]
    .spacing(5);
    content = content.push(card("Species", palette.into()));

    // The selected species.
    if let Some(species) = view.species.get(view.selected) {
        let name_emit = emit.clone();
        let material_emit = emit.clone();
        let procedural_emit = emit.clone();
        let visible_emit = emit.clone();
        let variant_emit = emit.clone();
        let remove_emit = emit.clone();
        let mut body = column![
            text_field::property(id::TV_NAME, "Name", &species.name, move |t| name_emit(
                Edit::Name(t)
            )),
            text_field::property(id::TV_MATERIAL, "Material", &species.material, move |t| {
                material_emit(Edit::Material(t))
            }),
        ]
        .spacing(4);
        // Variants: the meshes the species is drawn with (a recipe is baked into them by the Nature Generator).
        for (index, mesh) in species.meshes.iter().enumerate() {
            let drop_emit = emit.clone();
            body = body.push(
                row![
                    text(format!("Variant {index}"))
                        .size(9)
                        .color(theme::TEXT_MUTED)
                        .width(Length::Fixed(60.0)),
                    text(mesh.clone()).size(10),
                    iced::widget::space::horizontal(),
                    button::compact(
                        id::terrain_vegetation_variant(index, "remove"),
                        "Remove",
                        drop_emit(Edit::RemoveVariant(index))
                    ),
                ]
                .spacing(4)
                .align_y(iced::Alignment::Center),
            );
        }
        if species.meshes.is_empty() {
            body = body.push(
                text("No mesh yet: the species is not drawn. Select a mesh in the Assets panel and add it as a variant.")
                    .size(9)
                    .color(theme::TEXT_MUTED),
            );
        }
        body = body.push(button::compact(
            id::TV_VARIANT_ADD,
            "Add variant from selected asset",
            variant_emit(Edit::AddVariantFromAsset),
        ));
        for number in SpeciesNumber::ALL {
            let number_emit = emit.clone();
            body = body.push(slider::property_step(
                number.id(),
                number.label(),
                number.range(),
                number.get(species),
                number.step(),
                move |v| number_emit(Edit::Number(number, v)),
            ));
        }
        body = body.push(checkbox::property(
            id::TV_PROCEDURAL,
            "Grows by itself (density, rules and the seed)",
            species.procedural,
            move |on| procedural_emit(Edit::Procedural(on)),
        ));
        body = body.push(checkbox::property(
            id::TV_VISIBLE,
            "Drawn",
            species.visible,
            move |on| visible_emit(Edit::Visible(on)),
        ));
        body = body.push(button::danger(
            id::TV_REMOVE,
            "Remove species",
            remove_emit(Edit::RemoveSpecies),
        ));
        content = content.push(card("Selected species", body.into()));

        // Where it may stand.
        let rule_add_emit = emit.clone();
        let mut rules = column![dropdown::property(
            id::TV_RULE_ADD,
            "Add rule on",
            RULE_MASKS
                .iter()
                .map(|m| (*m).to_owned())
                .collect::<Vec<_>>(),
            None,
            move |picked: String| rule_add_emit(Edit::AddRule(picked)),
        )]
        .spacing(4);
        if species.rules.is_empty() {
            rules = rules.push(
                text("The species grows everywhere. Add a rule to keep it off steep ground, off a height band...")
                    .size(9)
                    .color(theme::TEXT_MUTED),
            );
        }
        for (index, rule) in species.rules.iter().enumerate() {
            let remove_rule_emit = emit.clone();
            let mut block = column![row![
                text(format!("{} (0..1)", rule.mask))
                    .size(10)
                    .color(theme::TEXT),
                iced::widget::space::horizontal(),
                button::compact(
                    id::terrain_vegetation_rule(index, "remove"),
                    "Remove",
                    remove_rule_emit(Edit::RemoveRule(index))
                ),
            ]
            .align_y(iced::Alignment::Center)]
            .spacing(3);
            for corner in RuleCorner::ALL {
                let corner_emit = emit.clone();
                let value = match corner {
                    RuleCorner::Rise0 => rule.rise0,
                    RuleCorner::Rise1 => rule.rise1,
                    RuleCorner::Fall1 => rule.fall1,
                    RuleCorner::Fall0 => rule.fall0,
                };
                block = block.push(slider::property_step(
                    id::terrain_vegetation_rule(index, corner.key()),
                    corner.label(),
                    0.0..=1.0,
                    value,
                    0.01,
                    move |v| corner_emit(Edit::Rule(index, corner, v)),
                ));
            }
            rules = rules.push(block);
        }
        content = content.push(card("Where it grows", rules.into()));
    }

    // The brush.
    let names: Vec<String> = std::iter::once("All species".to_owned())
        .chain(
            view.species
                .iter()
                .enumerate()
                .map(|(i, s)| format!("{i}  {}", s.name)),
        )
        .collect();
    let only_lookup = names.clone();
    let only_emit = emit.clone();
    let tool_options: Vec<String> = VegTool::ALL.iter().map(|t| t.label().to_owned()).collect();
    let tool_lookup = tool_options.clone();
    let tool_emit = emit.clone();
    let respect_emit = emit.clone();
    let shared_emit = emit.clone();
    let mut brush = column![dropdown::property(
        id::TV_TOOL,
        "Tool",
        tool_options,
        Some(view.tool.label().to_owned()),
        move |picked: String| {
            tool_emit(Edit::Tool(
                tool_lookup
                    .iter()
                    .position(|o| *o == picked)
                    .map_or(VegTool::Paint, |i| VegTool::ALL[i]),
            ))
        },
    )]
    .spacing(4);
    if !matches!(view.tool, VegTool::Paint) {
        brush = brush.push(dropdown::property(
            id::TV_ONLY,
            "Acts on",
            names.clone(),
            names.get((view.only + 1).max(0) as usize).cloned(),
            move |picked: String| {
                only_emit(Edit::Only(
                    only_lookup
                        .iter()
                        .position(|n| *n == picked)
                        .map_or(-1, |i| i as i32 - 1),
                ))
            },
        ));
    }
    for (amount, value, slider_id) in [
        (BrushAmount::Radius, view.radius, id::TV_BRADIUS),
        (BrushAmount::Strength, view.strength, id::TV_BSTRENGTH),
        (BrushAmount::Spacing, view.spacing, id::TV_BSPACING),
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
    if matches!(view.tool, VegTool::Paint) {
        let per_emit = emit.clone();
        brush = brush.push(slider::property_step(
            id::TV_PER100,
            "Per 100 m2",
            0.0..=100.0,
            view.per_100m2,
            0.1,
            move |v| per_emit(Edit::Per100(v)),
        ));
        brush = brush.push(checkbox::property(
            id::TV_RESPECT,
            "Only where the species' rules allow",
            view.respect_rules,
            move |on| respect_emit(Edit::Respect(on)),
        ));
    }
    if matches!(view.tool, VegTool::Scale | VegTool::Rotate) {
        let amount_emit = emit.clone();
        let (label, range) = if matches!(view.tool, VegTool::Scale) {
            ("Scale by", -1.0..=2.0)
        } else {
            ("Turn (deg)", -180.0..=180.0)
        };
        brush = brush.push(slider::property_step(
            id::TV_AMOUNT,
            label,
            range,
            view.amount,
            0.05,
            move |v| amount_emit(Edit::Amount(v)),
        ));
    }
    brush = brush.push(checkbox::property(
        id::TV_SHARED,
        "Shape from the Terrain Brush (stamp, jitter, symmetry, protection)",
        view.shared_shape,
        move |on| shared_emit(Edit::Shared(on)),
    ));
    content = content.push(card("Brush", brush.into()));

    // The procedural source.
    let seed_emit = emit.clone();
    let reseed_emit = emit.clone();
    let bake_emit = emit.clone();
    let rederive_emit = emit.clone();
    let source = column![
        slider::property_step(id::TV_SEED, "Seed", 0.0..=9999.0, (view.seed % 10000) as f32, 1.0, move |v| {
            seed_emit(Edit::Seed(v.round() as u64))
        }),
        row![
            button::compact(id::TV_RESEED, "Reseed", reseed_emit(Edit::Reseed)),
            button::compact(id::TV_BAKE, "Bake selected", bake_emit(Edit::Bake)),
            button::compact(id::TV_REDERIVE, "Re-derive selected", rederive_emit(Edit::Rederive)),
        ]
        .spacing(6),
        text("Baking turns a species' grown plants into manual ones (it stops growing by itself) so you can edit them one by one; re-deriving takes them back.")
            .size(9)
            .color(theme::TEXT_MUTED),
    ]
    .spacing(5);
    content = content.push(card("Procedural source", source.into()));

    if let Some(note) = view.status {
        content = content.push(status::note(note));
    }
    scroll::vertical(container(content).padding(8)).into()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn species() -> VegSpecies {
        VegSpecies::default()
    }

    #[test]
    fn species_numbers_round_trip_and_stay_in_range() {
        for number in SpeciesNumber::ALL {
            let mut s = species();
            let range = number.range();
            number.set(&mut s, *range.end() + 1.0e6);
            assert_eq!(number.get(&s), *range.end(), "{}", number.label());
            number.set(&mut s, *range.start() - 1.0e6);
            assert_eq!(number.get(&s), *range.start(), "{}", number.label());
            assert_eq!(SpeciesNumber::from_key(number.key()), Some(number));
        }
    }

    #[test]
    fn the_largest_scale_never_goes_under_the_smallest() {
        let mut s = species();
        SpeciesNumber::MinScale.set(&mut s, 5.0);
        assert!(s.max_scale >= 5.0);
        SpeciesNumber::MaxScale.set(&mut s, 2.0);
        assert!(s.min_scale <= 2.0);
        assert!(s.min_scale <= s.max_scale);
    }

    #[test]
    fn every_widget_id_is_catalogued() {
        for number in SpeciesNumber::ALL {
            assert!(
                id::description(number.id().key()).is_some(),
                "{}",
                number.key()
            );
        }
        for key in [
            "terrain_vegetation.palette.species.3",
            "terrain_vegetation.species.variant.1.remove",
            "terrain_vegetation.species.rule.0.rise0",
        ] {
            assert!(id::description(key).is_some(), "{key}");
        }
    }

    #[test]
    fn the_summary_says_when_vegetation_is_off() {
        assert_eq!(summary(true, None), "vegetation off");
        let on = VegInfo {
            enabled: true,
            species_count: 3,
            ..VegInfo::default()
        };
        assert_eq!(summary(true, Some(on)), "On · 3 species");
        assert_eq!(summary(false, Some(on)), "Off · 3 species");
    }
}

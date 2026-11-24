//! Terrain Brush panel: the brush library, the selected brush's settings and the switch that turns the sculpting tool on.
//!
//! A pure view. The brushes themselves (the engine's built-in library and the project's `.brush` files), the working
//! copy being edited and the stroke engine live in the app (`windows/main/terraineditor.rs`); the panel only draws them
//! and reports what the user asks for as an [`Edit`]. Every number is a row of the [`Amount`] table, shown only for the
//! modes it applies to, so a brush that raises does not offer a talus angle.

use crate::uikit::components::{
    brush_card, button, checkbox, dropdown, scroll, slider, status, text_field, toggle_chip,
};
use crate::uikit::id::{self, UiId};
use crate::uikit::theme::{self, ChipTone};
use doppio_engine::{BrushMode, BrushPreset, Falloff, SymmetryKind};
use iced::widget::image::Handle;
use iced::widget::{column, container, row, text};
use iced::{Element, Length};
use std::ops::RangeInclusive;
use std::rc::Rc;

/// The masks a protection term can name (the stored ones a generator sends are written in the `.brush` file).
pub const PROTECT_MASKS: [&str; 3] = ["slope", "height", "curvature"];

#[derive(Debug, Clone)]
pub struct AmountSpec {
    pub label: &'static str,
    pub min: f32,
    pub max: f32,
    pub step: f32,
    pub id: UiId,
}

impl AmountSpec {
    pub fn range(&self) -> RangeInclusive<f32> {
        self.min..=self.max
    }
}

macro_rules! amounts {
    ($( $name:ident => ($label:expr, $min:expr, $max:expr, $step:expr, $id:expr) ),* $(,)?) => {
        /// A number of the brush.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum Amount { $($name),* }
        impl Amount {
            pub const ALL: &'static [Amount] = &[$(Amount::$name),*];
            pub fn spec(self) -> AmountSpec {
                match self { $(Amount::$name => AmountSpec { label: $label, min: $min, max: $max, step: $step, id: $id }),* }
            }
            /// The name an agent uses (`radius`, `strength`...): the variant in snake case.
            pub fn key(self) -> &'static str {
                match self { $(Amount::$name => stringify!($name)),* }
            }
        }
    };
}

amounts! {
    Radius => ("Radius (m)", 0.1, 500.0, 0.1, id::TB_RADIUS),
    Strength => ("Strength", 0.0, 4.0, 0.01, id::TB_STRENGTH),
    Spacing => ("Spacing (m)", 0.0, 50.0, 0.1, id::TB_SPACING),
    Rotation => ("Rotation", -180.0, 180.0, 1.0, id::TB_ROTATION),
    TargetHeight => ("Flatten height", -1.0, 1000.0, 0.5, id::TB_TARGET),
    Step => ("Step height (m)", 0.1, 100.0, 0.1, id::TB_STEP),
    Talus => ("Talus angle", 5.0, 80.0, 1.0, id::TB_TALUS),
    Iterations => ("Passes", 1.0, 16.0, 1.0, id::TB_ITERATIONS),
    Droplets => ("Droplets", 1.0, 200.0, 1.0, id::TB_DROPLETS),
    OffsetX => ("Source X (m)", -200.0, 200.0, 0.5, id::TB_OFFSET_X),
    OffsetZ => ("Source Z (m)", -200.0, 200.0, 0.5, id::TB_OFFSET_Z),
    PositionJitter => ("Scatter", 0.0, 1.0, 0.01, id::TB_POSITION_JITTER),
    RotationJitter => ("Turn jitter", 0.0, 180.0, 1.0, id::TB_ROTATION_JITTER),
    SizeJitter => ("Size jitter", 0.0, 1.0, 0.01, id::TB_SIZE_JITTER),
    OpacityJitter => ("Strength jitter", 0.0, 1.0, 0.01, id::TB_OPACITY_JITTER),
    RadialCount => ("Copies", 2.0, 16.0, 1.0, id::TB_RADIAL_COUNT),
    CenterX => ("Centre X (m)", -2000.0, 2000.0, 1.0, id::TB_CENTER_X),
    CenterZ => ("Centre Z (m)", -2000.0, 2000.0, 1.0, id::TB_CENTER_Z),
    Pressure => ("Pen pressure", 0.0, 1.0, 0.05, id::TB_PRESSURE),
}

impl Amount {
    /// The amount named `key` (`radius`, `target_height`...; case and underscores do not matter).
    pub fn from_key(key: &str) -> Option<Amount> {
        let wanted: String = key
            .chars()
            .filter(|c| *c != '_')
            .collect::<String>()
            .to_lowercase();
        Amount::ALL
            .iter()
            .copied()
            .find(|amount| amount.key().to_lowercase() == wanted)
    }

    pub fn get(self, brush: &BrushPreset) -> f32 {
        match self {
            Amount::Radius => brush.radius_m,
            Amount::Strength => brush.strength,
            Amount::Spacing => brush.spacing_m,
            Amount::Rotation => brush.rotation_degrees,
            Amount::TargetHeight => brush.target_height_m,
            Amount::Step => brush.step_m,
            Amount::Talus => brush.talus_degrees,
            Amount::Iterations => brush.iterations as f32,
            Amount::Droplets => brush.droplets as f32,
            Amount::OffsetX => brush.offset[0],
            Amount::OffsetZ => brush.offset[1],
            Amount::PositionJitter => brush.position_jitter,
            Amount::RotationJitter => brush.rotation_jitter_degrees,
            Amount::SizeJitter => brush.size_jitter,
            Amount::OpacityJitter => brush.opacity_jitter,
            Amount::RadialCount => brush.radial_count as f32,
            Amount::CenterX => brush.symmetry_center[0],
            Amount::CenterZ => brush.symmetry_center[1],
            Amount::Pressure => brush.pressure_to_strength,
        }
    }

    /// Writes `value`, kept inside the slider's range (a whole number where the brush holds one).
    pub fn set(self, brush: &mut BrushPreset, value: f32) {
        let spec = self.spec();
        let value = value.clamp(spec.min, spec.max);
        let whole = value.round() as u32;
        match self {
            Amount::Radius => brush.radius_m = value,
            Amount::Strength => brush.strength = value,
            Amount::Spacing => brush.spacing_m = value,
            Amount::Rotation => brush.rotation_degrees = value,
            Amount::TargetHeight => brush.target_height_m = value,
            Amount::Step => brush.step_m = value,
            Amount::Talus => brush.talus_degrees = value,
            Amount::Iterations => brush.iterations = whole,
            Amount::Droplets => brush.droplets = whole,
            Amount::OffsetX => brush.offset[0] = value,
            Amount::OffsetZ => brush.offset[1] = value,
            Amount::PositionJitter => brush.position_jitter = value,
            Amount::RotationJitter => brush.rotation_jitter_degrees = value,
            Amount::SizeJitter => brush.size_jitter = value,
            Amount::OpacityJitter => brush.opacity_jitter = value,
            Amount::RadialCount => brush.radial_count = whole,
            Amount::CenterX => brush.symmetry_center[0] = value,
            Amount::CenterZ => brush.symmetry_center[1] = value,
            Amount::Pressure => brush.pressure_to_strength = value,
        }
    }

    /// Whether the brush's mode uses this number.
    pub fn applies_to(self, mode: BrushMode) -> bool {
        match self {
            Amount::TargetHeight => mode == BrushMode::Flatten,
            Amount::Step => mode == BrushMode::Terrace,
            Amount::Talus | Amount::Iterations => mode == BrushMode::ErodeThermal,
            Amount::Droplets => mode == BrushMode::ErodeHydraulic,
            Amount::OffsetX | Amount::OffsetZ => mode == BrushMode::Clone,
            _ => true,
        }
    }
}

/// One of the four numbers of a protection term's trapezoid.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Corner {
    Rise0,
    Rise1,
    Fall1,
    Fall0,
}

impl Corner {
    pub const ALL: [Corner; 4] = [Corner::Rise0, Corner::Rise1, Corner::Fall1, Corner::Fall0];
    pub fn key(self) -> &'static str {
        match self {
            Corner::Rise0 => "rise0",
            Corner::Rise1 => "rise1",
            Corner::Fall1 => "fall1",
            Corner::Fall0 => "fall0",
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Corner::Rise0 => "From",
            Corner::Rise1 => "Full from",
            Corner::Fall1 => "Full to",
            Corner::Fall0 => "To",
        }
    }
}

/// A change the panel asks for.
#[derive(Debug, Clone, PartialEq)]
pub enum Edit {
    /// Turns the sculpting tool on or off.
    Active(bool),
    Search(String),
    /// A category chip; `None` shows every category.
    Category(Option<String>),
    /// A brush of the library, by its index there.
    Select(usize),
    Mode(BrushMode),
    Falloff(Falloff),
    Signed(bool),
    Symmetry(SymmetryKind),
    Amount(Amount, f32),
    /// Adds a protection term on a mask, across its whole range.
    AddProtect(String),
    /// Adds a protection term with its four numbers (`rise0`, `rise1`, `fall1`, `fall0`).
    AddProtectRange(String, [f32; 4]),
    /// Removes every protection term.
    ClearProtect,
    RemoveProtect(usize),
    Protect(usize, Corner, f32),
    Name(String),
    /// Saves the working brush as a `.brush` file of the project.
    Save,
    /// Puts the selected brush back as the library has it.
    Revert,
    /// Reads the project's `Brushes` folder again.
    Reload,
}

/// A brush of the library as the grid shows it.
pub struct Entry<'a> {
    /// The brush's index in the library.
    pub index: usize,
    pub name: &'a str,
    pub detail: &'a str,
    pub thumbnail: Option<&'a Handle>,
}

pub struct View<'a> {
    pub active: bool,
    /// A terrain is in the scene: without one the tool has nothing to sculpt.
    pub has_terrain: bool,
    pub search: &'a str,
    pub categories: Vec<String>,
    pub category: Option<&'a str>,
    pub entries: Vec<Entry<'a>>,
    pub selected: Option<usize>,
    pub brush: &'a BrushPreset,
    pub status: Option<&'a str>,
}

/// The one-line summary under the panel title.
pub fn summary(active: bool, brush: &BrushPreset) -> String {
    format!("{} · {}", if active { "On" } else { "Off" }, brush.name)
}

fn card<'a, Message: 'a>(title: &'a str, body: Element<'a, Message>) -> Element<'a, Message> {
    container(column![text(title).size(11).color(theme::TEXT), body].spacing(5))
        .padding([6, 8])
        .width(Length::Fill)
        .style(theme::component_card)
        .into()
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let brush = view.brush;
    let mut content = column![].spacing(6).width(Length::Fill);

    // The switch.
    let active_emit = emit.clone();
    content = content.push(
        row![
            status::chip(
                if view.active { "Sculpting" } else { "Off" },
                if view.active {
                    ChipTone::Success
                } else {
                    ChipTone::Neutral
                },
            ),
            checkbox::inline(
                id::TB_ACTIVE,
                "Brush on (click or drag on the terrain)",
                view.active,
                move |on| active_emit(Edit::Active(on)),
            ),
        ]
        .spacing(8)
        .align_y(iced::Alignment::Center),
    );
    if !view.has_terrain {
        content = content.push(status::note(
            "No terrain in the scene. Add one from the Hierarchy's + menu, then select it.",
        ));
    }

    // The library: search, categories and the grid.
    let search_emit = emit.clone();
    let mut chips = row![].spacing(4);
    let all_emit = emit.clone();
    chips = chips.push(toggle_chip::view(
        id::terrain_brush_category("all"),
        "All",
        view.category.is_none(),
        theme::ACCENT,
        all_emit(Edit::Category(None)),
    ));
    for name in &view.categories {
        let chip_emit = emit.clone();
        chips = chips.push(toggle_chip::view(
            id::terrain_brush_category(name),
            name.clone(),
            view.category == Some(name.as_str()),
            theme::ACCENT,
            chip_emit(Edit::Category(Some(name.clone()))),
        ));
    }
    let mut grid = row![].spacing(4);
    for entry in &view.entries {
        let card_emit = emit.clone();
        grid = grid.push(brush_card::view(
            id::terrain_brush_card(entry.index),
            entry.name,
            entry.detail,
            entry.thumbnail,
            view.selected == Some(entry.index),
            card_emit(Edit::Select(entry.index)),
        ));
    }
    let empty = view.entries.is_empty();
    let library: Element<'a, Message> = column![
        text_field::search(id::TB_SEARCH, "Search brushes", view.search, move |t| {
            search_emit(Edit::Search(t))
        }),
        chips.wrap(),
        if empty {
            Element::from(text("No brush matches.").size(10).color(theme::TEXT_MUTED))
        } else {
            grid.wrap().into()
        },
    ]
    .spacing(5)
    .into();
    content = content.push(card("Brushes", library));

    // The brush: mode, falloff and the numbers the mode uses.
    let mode_options: Vec<String> = BrushMode::ALL
        .iter()
        .map(|m| m.label().to_owned())
        .collect();
    let mode_lookup = mode_options.clone();
    let mode_selected = Some(brush.mode.label().to_owned());
    let mode_emit = emit.clone();
    let falloff_options: Vec<String> = Falloff::ALL.iter().map(|f| f.label().to_owned()).collect();
    let falloff_lookup = falloff_options.clone();
    let falloff_selected = Some(brush.falloff.label().to_owned());
    let falloff_emit = emit.clone();
    let signed_emit = emit.clone();
    let mut settings = column![
        dropdown::property(
            id::TB_MODE,
            "Mode",
            mode_options,
            mode_selected,
            move |picked: String| {
                mode_emit(Edit::Mode(
                    mode_lookup
                        .iter()
                        .position(|o| *o == picked)
                        .map_or(BrushMode::Raise, |i| BrushMode::ALL[i]),
                ))
            }
        ),
        dropdown::property(
            id::TB_FALLOFF,
            "Falloff",
            falloff_options,
            falloff_selected,
            move |picked: String| {
                falloff_emit(Edit::Falloff(
                    falloff_lookup
                        .iter()
                        .position(|o| *o == picked)
                        .map_or(Falloff::Smooth, |i| Falloff::ALL[i]),
                ))
            },
        ),
    ]
    .spacing(4);
    let amount = |which: Amount| {
        let spec = which.spec();
        let emit = emit.clone();
        let range = spec.range();
        slider::property_step(
            spec.id,
            spec.label,
            range,
            which.get(brush),
            spec.step,
            move |v| emit(Edit::Amount(which, v)),
        )
    };
    for which in [
        Amount::Radius,
        Amount::Strength,
        Amount::Spacing,
        Amount::Rotation,
    ] {
        settings = settings.push(amount(which));
    }
    for which in [
        Amount::TargetHeight,
        Amount::Step,
        Amount::Talus,
        Amount::Iterations,
        Amount::Droplets,
        Amount::OffsetX,
        Amount::OffsetZ,
    ] {
        if which.applies_to(brush.mode) {
            settings = settings.push(amount(which));
        }
    }
    if brush.stamp_kind != doppio_engine::StampKind::None {
        settings = settings.push(checkbox::property(
            id::TB_SIGNED,
            "Signed stamp (grey is neutral: lighter raises, darker lowers)",
            brush.signed_stamp,
            move |on| signed_emit(Edit::Signed(on)),
        ));
        settings = settings.push(
            text(format!("Stamp: {}", brush.stamp))
                .size(9)
                .color(theme::TEXT_MUTED),
        );
    }
    settings = settings.push(amount(Amount::Pressure));
    content = content.push(card("Brush", settings.into()));

    // Variation.
    content = content.push(card(
        "Variation",
        column![
            amount(Amount::PositionJitter),
            amount(Amount::RotationJitter),
            amount(Amount::SizeJitter),
            amount(Amount::OpacityJitter),
        ]
        .spacing(4)
        .into(),
    ));

    // Symmetry.
    let symmetry_options: Vec<String> = SymmetryKind::ALL
        .iter()
        .map(|k| k.label().to_owned())
        .collect();
    let symmetry_lookup = symmetry_options.clone();
    let symmetry_emit = emit.clone();
    let mut symmetry = column![dropdown::property(
        id::TB_SYMMETRY,
        "Symmetry",
        symmetry_options,
        Some(brush.symmetry.label().to_owned()),
        move |picked: String| symmetry_emit(Edit::Symmetry(
            symmetry_lookup
                .iter()
                .position(|o| *o == picked)
                .map_or(SymmetryKind::None, |i| SymmetryKind::ALL[i]),
        )),
    )]
    .spacing(4);
    if brush.symmetry == SymmetryKind::Radial {
        symmetry = symmetry.push(amount(Amount::RadialCount));
    }
    if brush.symmetry != SymmetryKind::None {
        symmetry = symmetry.push(amount(Amount::CenterX));
        symmetry = symmetry.push(amount(Amount::CenterZ));
    }
    content = content.push(card("Symmetry", symmetry.into()));

    // Protection.
    let add_emit = emit.clone();
    let mut protection = column![dropdown::property(
        id::TB_PROTECT_ADD,
        "Protect by",
        PROTECT_MASKS
            .iter()
            .map(|m| (*m).to_owned())
            .collect::<Vec<_>>(),
        None,
        move |picked: String| add_emit(Edit::AddProtect(picked)),
    )]
    .spacing(4);
    if brush.protect.is_empty() {
        protection = protection.push(
            text("The brush acts everywhere. Add a mask to keep it off steep ground, off a height band...")
                .size(9)
                .color(theme::TEXT_MUTED),
        );
    }
    for (index, term) in brush.protect.iter().enumerate() {
        let remove_emit = emit.clone();
        let mut block = column![row![
            text(format!("{} (0..1)", term.mask))
                .size(10)
                .color(theme::TEXT),
            iced::widget::space::horizontal(),
            button::compact(
                id::terrain_brush_protect(index, "remove"),
                "Remove",
                remove_emit(Edit::RemoveProtect(index)),
            ),
        ]
        .align_y(iced::Alignment::Center)]
        .spacing(3);
        for corner in Corner::ALL {
            let corner_emit = emit.clone();
            let value = match corner {
                Corner::Rise0 => term.rise0,
                Corner::Rise1 => term.rise1,
                Corner::Fall1 => term.fall1,
                Corner::Fall0 => term.fall0,
            };
            block = block.push(slider::property_step(
                id::terrain_brush_protect(index, corner.key()),
                corner.label(),
                0.0..=1.0,
                value,
                0.01,
                move |v| corner_emit(Edit::Protect(index, corner, v)),
            ));
        }
        protection = protection.push(block);
    }
    content = content.push(card("Protection", protection.into()));

    // The file.
    let name_emit = emit.clone();
    let save_emit = emit.clone();
    let revert_emit = emit.clone();
    let reload_emit = emit.clone();
    content = content.push(card(
        "Brush file",
        column![
            text_field::search(id::TB_NAME, "Name", &brush.name, move |t| name_emit(Edit::Name(t))),
            row![
                button::primary(id::TB_SAVE, "Save .brush", save_emit(Edit::Save)),
                button::compact(id::TB_REVERT, "Revert", revert_emit(Edit::Revert)),
                button::compact(id::TB_RELOAD, "Reload folder", reload_emit(Edit::Reload)),
            ]
            .spacing(6),
            text("Brushes are saved in the project's Brushes folder and appear in the library under Custom.")
                .size(9)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(5)
        .into(),
    ));
    if let Some(note) = view.status {
        content = content.push(status::note(note));
    }
    scroll::vertical(container(content).padding(8)).into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_amount_round_trips_through_the_brush() {
        for amount in Amount::ALL.iter().copied() {
            let spec = amount.spec();
            let mut brush = BrushPreset::default();
            let value = (spec.min + spec.max) * 0.5;
            amount.set(&mut brush, value);
            let back = amount.get(&brush);
            assert!(
                (back - value).abs() <= 0.5,
                "{} wrote {value} and read {back}",
                spec.label
            );
        }
    }

    #[test]
    fn amounts_are_kept_inside_their_range() {
        let mut brush = BrushPreset::default();
        Amount::Radius.set(&mut brush, 1.0e9);
        assert_eq!(brush.radius_m, Amount::Radius.spec().max);
        Amount::Radius.set(&mut brush, -5.0);
        assert_eq!(brush.radius_m, Amount::Radius.spec().min);
        Amount::Iterations.set(&mut brush, 3.4);
        assert_eq!(brush.iterations, 3);
    }

    #[test]
    fn keys_find_their_amount() {
        assert_eq!(Amount::from_key("radius"), Some(Amount::Radius));
        assert_eq!(
            Amount::from_key("target_height"),
            Some(Amount::TargetHeight)
        );
        assert_eq!(Amount::from_key("OffsetX"), Some(Amount::OffsetX));
        assert_eq!(Amount::from_key("nope"), None);
    }

    #[test]
    fn a_mode_only_offers_its_own_numbers() {
        assert!(Amount::Step.applies_to(BrushMode::Terrace));
        assert!(!Amount::Step.applies_to(BrushMode::Raise));
        assert!(Amount::Talus.applies_to(BrushMode::ErodeThermal));
        assert!(!Amount::Talus.applies_to(BrushMode::ErodeHydraulic));
        assert!(Amount::Radius.applies_to(BrushMode::Smudge));
    }

    #[test]
    fn every_amount_has_a_catalogued_id() {
        for amount in Amount::ALL {
            let key = amount.spec().id.key().to_owned();
            assert!(
                id::description(&key).is_some(),
                "{key} is not in the catalog"
            );
        }
    }
}

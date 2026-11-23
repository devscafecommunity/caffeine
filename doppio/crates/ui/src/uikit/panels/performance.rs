//! Performance panel: the switches that decide what the renderer spends its time on.
//!
//! The scene side is one reflected component, `Forward Render Features` (instancing, occlusion culling, reflections,
//! volumetric light, image-based light), found on any entity or added on a new one; the editor side is the viewport's
//! own settings (render scale, anti-aliasing, shadows, grid). Photon has its own tab. The panel is a pure view: the app
//! (`windows/main/performance.rs`) reads the component, applies every edit through the engine's reflected component API
//! and records an undo step. Like the Environment and Audio panels, every control is an entry of a table
//! ([`Flag`], [`Amount`]).

use crate::uikit::components::{button, checkbox, dropdown, scroll, slider, status};
use crate::uikit::id::{self, UiId};
use crate::uikit::theme::{self, ChipTone};
use iced::widget::{column, container, row, text};
use iced::{Element, Length};
use std::ops::RangeInclusive;
use std::rc::Rc;

/// The reflected component the panel edits.
pub const COMPONENT: &str = "Forward Render Features";

#[derive(Debug, Clone)]
pub struct FlagSpec {
    pub field: &'static str,
    pub key: &'static str,
    pub label: &'static str,
    pub default: bool,
    pub id: UiId,
}

#[derive(Debug, Clone)]
pub struct AmountSpec {
    pub field: &'static str,
    pub key: &'static str,
    pub label: &'static str,
    pub min: f32,
    pub max: f32,
    pub step: f32,
    pub default: f32,
    /// A whole number in the component (written as an integer).
    pub integer: bool,
    pub id: UiId,
}

impl AmountSpec {
    pub fn range(&self) -> RangeInclusive<f32> {
        self.min..=self.max
    }
}

macro_rules! flags {
    ($( $name:ident => ($field:expr, $key:expr, $label:expr, $default:expr, $id:expr) ),* $(,)?) => {
        /// A switch of the component.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum Flag { $($name),* }
        impl Flag {
            pub const ALL: &'static [Flag] = &[$(Flag::$name),*];
            pub const COUNT: usize = Flag::ALL.len();
            pub fn index(self) -> usize { self as usize }
            pub fn spec(self) -> FlagSpec {
                match self { $(Flag::$name => FlagSpec { field: $field, key: $key, label: $label, default: $default, id: $id }),* }
            }
        }
    };
}

macro_rules! amounts {
    ($( $name:ident => ($field:expr, $key:expr, $label:expr, $min:expr, $max:expr, $step:expr, $default:expr, $integer:expr, $id:expr) ),* $(,)?) => {
        /// A number of the component.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum Amount { $($name),* }
        impl Amount {
            pub const ALL: &'static [Amount] = &[$(Amount::$name),*];
            pub const COUNT: usize = Amount::ALL.len();
            pub fn index(self) -> usize { self as usize }
            pub fn spec(self) -> AmountSpec {
                match self {
                    $(Amount::$name => AmountSpec { field: $field, key: $key, label: $label, min: $min, max: $max, step: $step, default: $default, integer: $integer, id: $id }),*
                }
            }
        }
    };
}

flags! {
    Enabled => ("Enabled", "enabled", "Render features on", true, id::PERF_ENABLED),
    Instancing => ("Instancing: Enabled", "instancing", "Instancing: copies drawn together", true, id::PERF_INSTANCING),
    Occlusion => ("Occlusion: Enabled", "occlusion", "Occlusion culling: skip what a big object hides", false, id::PERF_OCCLUSION),
    Reflections => ("Reflections: Enabled", "reflections", "Reflections (probes and planes)", true, id::PERF_REFLECTIONS),
    ScreenSpaceReflections => ("Reflections: Screen Space Trace", "reflections_ssr", "Screen-space reflections", true, id::PERF_SCREENSPACEREFLECTIONS),
    ReflectionsWhenSettled => ("Reflections: Expensive Only When Settled", "reflections_settled", "Reflections only while the view is still", true, id::PERF_REFLECTIONSWHENSETTLED),
    Volumetrics => ("Volumetrics: Enabled", "volumetrics", "Volumetric light", false, id::PERF_VOLUMETRICS),
    Ibl => ("Ibl: Enabled", "ibl", "Sky as diffuse fill light", false, id::PERF_IBL),
}

amounts! {
    InstancingBatch => ("Instancing: Max Instances Per Batch", "instancing_batch", "Instances per batch", 16.0, 1024.0, 16.0, 256.0, true, id::PERF_INSTANCINGBATCH),
    Occluders => ("Occlusion: Max Occluders", "occluders", "Occluders tested", 1.0, 32.0, 1.0, 8.0, true, id::PERF_OCCLUDERS),
    OccluderRadius => ("Occlusion: Min Radius", "occluder_radius", "Smallest occluder (m)", 0.1, 5.0, 0.05, 0.75, false, id::PERF_OCCLUDERRADIUS),
    ReflectionStrength => ("Reflections: Intensity", "reflection_strength", "Reflection strength", 0.0, 2.0, 0.05, 0.45, false, id::PERF_REFLECTIONSTRENGTH),
    ReflectionScale => ("Reflections: Resolution Scale", "reflection_scale", "Planar reflection resolution", 0.25, 1.0, 0.05, 0.5, false, id::PERF_REFLECTIONSCALE),
    ProbeResolution => ("Reflections: Probe Resolution", "probe_resolution", "Probe resolution (px)", 32.0, 512.0, 32.0, 128.0, true, id::PERF_PROBERESOLUTION),
    MaxProbes => ("Reflections: Max Probes", "max_probes", "Reflection probes", 0.0, 16.0, 1.0, 6.0, true, id::PERF_MAXPROBES),
    SsrSteps => ("Reflections: Ssr Max Steps", "ssr_steps", "SSR march steps", 4.0, 64.0, 1.0, 24.0, true, id::PERF_SSRSTEPS),
    FogDensity => ("Volumetrics: Density", "fog_density", "Volumetric density", 0.0, 0.1, 0.001, 0.012, false, id::PERF_FOGDENSITY),
}

/// The render scales the viewport offers.
pub const RENDER_SCALES: [f32; 6] = [0.5, 0.75, 1.0, 1.25, 1.5, 2.0];
pub const ANTI_ALIASING: [&str; 3] = ["Off", "FXAA", "TAA (temporal)"];

/// Every field the panel edits, as typed values.
#[derive(Debug, Clone, PartialEq)]
pub struct Settings {
    pub flags: [bool; Flag::COUNT],
    pub amounts: [f32; Amount::COUNT],
}

impl Default for Settings {
    /// What the component starts with in the engine.
    fn default() -> Self {
        let mut flags = [false; Flag::COUNT];
        for flag in Flag::ALL {
            flags[flag.index()] = flag.spec().default;
        }
        let mut amounts = [0.0; Amount::COUNT];
        for amount in Amount::ALL {
            amounts[amount.index()] = amount.spec().default;
        }
        Settings { flags, amounts }
    }
}

impl Settings {
    pub fn flag(&self, flag: Flag) -> bool {
        self.flags[flag.index()]
    }
    pub fn amount(&self, amount: Amount) -> f32 {
        self.amounts[amount.index()]
    }
    /// How many of the five optional features are switched on.
    pub fn features_on(&self) -> usize {
        [
            Flag::Instancing,
            Flag::Occlusion,
            Flag::Reflections,
            Flag::Volumetrics,
            Flag::Ibl,
        ]
        .iter()
        .filter(|flag| self.flag(**flag))
        .count()
    }
}

/// A change the panel asks for.
#[derive(Debug, Clone)]
pub enum Edit {
    /// Adds the component on a new entity (when the scene has none).
    Add,
    Flag(Flag, bool),
    Amount(Amount, f32),
    /// An index into [`RENDER_SCALES`].
    RenderScale(usize),
    /// An index into [`ANTI_ALIASING`].
    AntiAliasing(usize),
    Shadows(bool),
    Grid(bool),
}

pub struct View<'a> {
    /// `None` while the scene has no `Forward Render Features` component.
    pub settings: Option<&'a Settings>,
    pub entity: Option<&'a str>,
    pub status: Option<&'a str>,
    pub viewport: doppio_engine::ViewportSettings,
}

/// The one-line summary under the panel title.
pub fn summary(settings: Option<&Settings>) -> String {
    match settings {
        None => "engine defaults".to_owned(),
        Some(s) if !s.flag(Flag::Enabled) => "Off".to_owned(),
        Some(s) => format!("{} of 5 on", s.features_on()),
    }
}

/// The index of the render scale nearest to `scale`.
pub fn scale_index(scale: f32) -> usize {
    RENDER_SCALES
        .iter()
        .enumerate()
        .min_by(|a, b| (a.1 - scale).abs().total_cmp(&(b.1 - scale).abs()))
        .map(|(index, _)| index)
        .unwrap_or(2)
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
    let mut content = column![].spacing(6).width(Length::Fill);

    // The editor's own picture: always there.
    let scale_options: Vec<String> = RENDER_SCALES.iter().map(|s| format!("{s:.2}x")).collect();
    let scale_lookup = scale_options.clone();
    let scale_selected = scale_options
        .get(scale_index(view.viewport.render_scale))
        .cloned();
    let scale_emit = emit.clone();
    let aa_options: Vec<String> = ANTI_ALIASING.iter().map(|s| (*s).to_owned()).collect();
    let aa_lookup = aa_options.clone();
    let aa_selected = Some(
        match view.viewport.anti_aliasing {
            doppio_engine::AntiAliasing::Off => ANTI_ALIASING[0],
            doppio_engine::AntiAliasing::Fxaa => ANTI_ALIASING[1],
            doppio_engine::AntiAliasing::Taa => ANTI_ALIASING[2],
        }
        .to_owned(),
    );
    let aa_emit = emit.clone();
    let shadows_emit = emit.clone();
    let grid_emit = emit.clone();
    content = content.push(card(
        "Editor viewport",
        column![
            dropdown::property(
                id::PERF_RENDER_SCALE,
                "Render scale",
                scale_options,
                scale_selected,
                move |picked: String| scale_emit(Edit::RenderScale(
                    scale_lookup.iter().position(|o| *o == picked).unwrap_or(2)
                )),
            ),
            dropdown::property(
                id::PERF_ANTI_ALIASING,
                "Anti-aliasing",
                aa_options,
                aa_selected,
                move |picked: String| aa_emit(Edit::AntiAliasing(
                    aa_lookup.iter().position(|o| *o == picked).unwrap_or(1)
                )),
            ),
            checkbox::property(
                id::PERF_SHADOWS,
                "Light shadows in the viewport",
                view.viewport.shadows,
                move |on| shadows_emit(Edit::Shadows(on)),
            ),
            checkbox::property(
                id::PERF_GRID,
                "Ground grid",
                view.viewport.show_grid,
                move |on| grid_emit(Edit::Grid(on)),
            ),
            text("The viewport draws only when something changes, or while the game plays. A lower render scale or no anti-aliasing is the quickest way to a lighter frame.")
                .size(9)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(4)
        .into(),
    ));

    let Some(settings) = view.settings else {
        content = content.push(card(
            "Scene render features",
            column![
                text("This scene uses the engine's defaults: instancing and reflection probes on; occlusion culling, volumetric light and diffuse sky light off.")
                    .size(10)
                    .color(theme::TEXT_MUTED),
                button::primary(id::PERF_ADD, "Add render features", emit(Edit::Add)),
            ]
            .spacing(6)
            .into(),
        ));
        if let Some(note) = view.status {
            content = content.push(status::note(note));
        }
        return scroll::vertical(container(content).padding(8)).into();
    };

    let on = settings.flag(Flag::Enabled);
    content = content.push(
        row![
            status::chip(
                if on { "On" } else { "Off" },
                if on {
                    ChipTone::Success
                } else {
                    ChipTone::Neutral
                }
            ),
            text(view.entity.unwrap_or("Forward Render Features"))
                .size(10)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    );

    let flag = |which: Flag| {
        let spec = which.spec();
        let emit = emit.clone();
        checkbox::property(spec.id, spec.label, settings.flag(which), move |on| {
            emit(Edit::Flag(which, on))
        })
    };
    let amount = |which: Amount| {
        let spec = which.spec();
        let range = spec.range();
        let emit = emit.clone();
        slider::property_step(
            spec.id,
            spec.label,
            range,
            settings.amount(which),
            spec.step,
            move |v| emit(Edit::Amount(which, v)),
        )
    };

    content = content.push(card(
        "Drawing",
        column![
            flag(Flag::Enabled),
            flag(Flag::Instancing),
            amount(Amount::InstancingBatch),
            flag(Flag::Occlusion),
            amount(Amount::Occluders),
            amount(Amount::OccluderRadius),
        ]
        .spacing(4)
        .into(),
    ));
    content = content.push(card(
        "Reflections",
        column![
            flag(Flag::Reflections),
            amount(Amount::ReflectionStrength),
            amount(Amount::MaxProbes),
            amount(Amount::ProbeResolution),
            amount(Amount::ReflectionScale),
            flag(Flag::ScreenSpaceReflections),
            amount(Amount::SsrSteps),
            flag(Flag::ReflectionsWhenSettled),
        ]
        .spacing(4)
        .into(),
    ));
    content = content.push(card(
        "Light",
        column![
            flag(Flag::Volumetrics),
            amount(Amount::FogDensity),
            flag(Flag::Ibl),
        ]
        .spacing(4)
        .into(),
    ));

    if let Some(note) = view.status {
        content = content.push(status::note(note));
    }
    scroll::vertical(container(content).padding(8)).into()
}

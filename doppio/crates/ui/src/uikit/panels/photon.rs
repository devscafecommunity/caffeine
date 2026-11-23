//! Photon panel: the global-illumination settings of the scene and what its voxel far field is doing.
//!
//! Photon lives in the scene as one `Photon GI` component (the renderer reads the first one it finds). This
//! panel is a pure view over that component's fields plus the engine's voxel statistics: the app
//! (`windows/main/photon.rs`) reads the component, applies every edit through the engine and records an undo
//! step, so the panel and the Inspector always show the same values.
//!
//! Sections: lighting (switch, path, quality, strengths), the geometry the voxel volume is built from,
//! presets, the debug view and the live voxel statistics (objects, triangles, region updates, the
//! 1M-triangle budget).

use crate::uikit::components::{button, checkbox, dropdown, scroll, slider, status};
use crate::uikit::id;
use crate::uikit::theme::{self, ChipTone};
use doppio_engine::PhotonStats;
use iced::widget::{column, container, row, text};
use iced::{Element, Length};
use std::rc::Rc;

pub const PATHS: [&str; 2] = ["Screen fallback", "Compute"];
pub const QUALITIES: [&str; 5] = ["Off", "Low", "Balanced", "High", "Eco"];
pub const DEBUG_VIEWS: [&str; 3] = ["Lit scene", "GI only", "Fallback only"];

/// The Photon component's editable fields, as the panel shows them.
#[derive(Debug, Clone, PartialEq)]
pub struct Settings {
    pub enabled: bool,
    /// Index into [`PATHS`].
    pub path: usize,
    /// Index into [`QUALITIES`].
    pub quality: usize,
    pub intensity: f32,
    pub radius: f32,
    pub sky: f32,
    pub ambient: f32,
    /// Index into [`DEBUG_VIEWS`].
    pub debug_view: usize,
    pub bounce_gain: f32,
    pub voxel_skinned: bool,
    pub voxel_cutout: bool,
    pub voxel_terrain: bool,
    pub full_rebuilds: bool,
}

impl Default for Settings {
    /// What a component starts with in the engine (`PhotonGIComponent`).
    fn default() -> Self {
        Settings {
            enabled: false,
            path: 0,
            quality: 2,
            intensity: 1.0,
            radius: 14.0,
            sky: 0.65,
            ambient: 0.2,
            debug_view: 0,
            bounce_gain: 1.0,
            voxel_skinned: true,
            voxel_cutout: false,
            voxel_terrain: true,
            full_rebuilds: false,
        }
    }
}

impl Settings {
    /// A new Photon component the panel adds: on, on the compute path, at High quality.
    pub fn recommended() -> Self {
        Settings {
            enabled: true,
            path: 1,
            quality: 3,
            ..Settings::default()
        }
    }
}

/// A named starting point for the settings.
#[derive(Debug, Clone, PartialEq)]
pub struct Preset {
    pub name: &'static str,
    pub settings: Settings,
}

/// The presets, in the order the dropdown lists them. Every one switches Photon on, on the compute path.
pub fn presets() -> Vec<Preset> {
    let base = Settings::recommended();
    vec![
        Preset {
            name: "Balanced",
            settings: Settings {
                quality: 2,
                ..base.clone()
            },
        },
        Preset {
            name: "Interior (strong bounce)",
            settings: Settings {
                radius: 10.0,
                sky: 0.35,
                ambient: 0.1,
                bounce_gain: 2.0,
                ..base.clone()
            },
        },
        Preset {
            name: "Outdoor",
            settings: Settings {
                radius: 40.0,
                sky: 0.8,
                ambient: 0.25,
                ..base.clone()
            },
        },
        Preset {
            name: "Fast (Eco)",
            settings: Settings {
                quality: 4,
                voxel_skinned: false,
                voxel_terrain: false,
                ..base.clone()
            },
        },
        Preset {
            name: "Diagnostic (GI only)",
            settings: Settings {
                debug_view: 1,
                full_rebuilds: true,
                ..base
            },
        },
    ]
}

/// A switch of the component.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Flag {
    Enabled,
    VoxelSkinned,
    VoxelCutout,
    VoxelTerrain,
    FullRebuilds,
}

impl Flag {
    /// The reflected field name.
    pub fn field(self) -> &'static str {
        match self {
            Flag::Enabled => "Enabled",
            Flag::VoxelSkinned => "Voxel skinned meshes",
            Flag::VoxelCutout => "Voxel cut-out meshes",
            Flag::VoxelTerrain => "Voxel terrain",
            Flag::FullRebuilds => "Full rebuilds",
        }
    }
}

/// A number of the component.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Amount {
    Intensity,
    BounceGain,
    Sky,
    Ambient,
    Radius,
}

impl Amount {
    /// The reflected field name.
    pub fn field(self) -> &'static str {
        match self {
            Amount::Intensity => "Intensity",
            Amount::BounceGain => "Bounce gain",
            Amount::Sky => "Sky intensity",
            Amount::Ambient => "Ambient",
            Amount::Radius => "Radius",
        }
    }

    /// The range the component accepts.
    pub fn range(self) -> std::ops::RangeInclusive<f32> {
        match self {
            Amount::Intensity | Amount::Sky => 0.0..=4.0,
            Amount::BounceGain => 0.0..=8.0,
            Amount::Ambient => 0.0..=2.0,
            Amount::Radius => 1.0..=200.0,
        }
    }

    pub fn step(self) -> f32 {
        match self {
            Amount::Radius => 0.5,
            _ => 0.05,
        }
    }
}

/// A choice of the component (an enum field).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Choice {
    Path,
    Quality,
    DebugView,
}

impl Choice {
    /// The reflected field name.
    pub fn field(self) -> &'static str {
        match self {
            Choice::Path => "Path",
            Choice::Quality => "Quality",
            Choice::DebugView => "Debug view",
        }
    }

    pub fn options(self) -> &'static [&'static str] {
        match self {
            Choice::Path => &PATHS,
            Choice::Quality => &QUALITIES,
            Choice::DebugView => &DEBUG_VIEWS,
        }
    }
}

#[derive(Debug, Clone)]
pub enum Edit {
    /// Adds the Photon GI component to a new entity (when the scene has none).
    Add,
    Flag(Flag, bool),
    Amount(Amount, f32),
    Choice(Choice, usize),
    /// Index into [`presets`].
    Preset(usize),
    /// Throws the voxel volume away; the next frame voxelises the scene again.
    Rebuild,
}

pub struct View<'a> {
    /// `None` while the scene has no Photon GI component.
    pub settings: Option<&'a Settings>,
    /// The entity that carries it.
    pub entity: Option<&'a str>,
    /// The voxel statistics of the latest frame (`None` without an engine).
    pub stats: Option<PhotonStats>,
    pub status: Option<&'a str>,
}

/// The preset the settings equal, if any.
pub fn current_preset(settings: &Settings) -> Option<&'static str> {
    presets()
        .into_iter()
        .find(|preset| preset.settings == *settings)
        .map(|preset| preset.name)
}

fn card<'a, Message: 'a>(title: &'a str, body: Element<'a, Message>) -> Element<'a, Message> {
    container(column![text(title).size(11).color(theme::TEXT), body].spacing(5))
        .padding([6, 8])
        .width(Length::Fill)
        .style(theme::component_card)
        .into()
}

/// One line of the statistics card.
fn stat<'a, Message: 'a>(label: &'a str, value: String) -> Element<'a, Message> {
    row![
        text(label)
            .size(10)
            .color(theme::TEXT_MUTED)
            .width(Length::Fixed(120.0)),
        text(value).size(10).color(theme::TEXT),
    ]
    .spacing(6)
    .into()
}

/// 1 234 -> "1.2k", 5 600 000 -> "5.6M".
pub fn compact(value: u32) -> String {
    match value {
        0..=999 => value.to_string(),
        1_000..=999_999 => format!("{:.1}k", f64::from(value) / 1_000.0),
        _ => format!("{:.1}M", f64::from(value) / 1_000_000.0),
    }
}

/// The one-line summary under the panel title.
pub fn summary(settings: Option<&Settings>) -> String {
    match settings {
        None => "no Photon GI in the scene".to_owned(),
        Some(s) => format!(
            "{} · {} · {}",
            if s.enabled { "On" } else { "Off" },
            PATHS[s.path.min(PATHS.len() - 1)],
            QUALITIES[s.quality.min(QUALITIES.len() - 1)],
        ),
    }
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let mut content = column![].spacing(6).width(Length::Fill);

    let Some(settings) = view.settings else {
        content = content.push(
            column![
                text("No Photon GI in this scene")
                    .size(12)
                    .color(theme::TEXT),
                text("Photon is the global illumination system: indirect light, colour bleeding and sky occlusion. It lives in one component on an entity.")
                    .size(10)
                    .color(theme::TEXT_MUTED),
                button::primary(id::PHOTON_ADD, "Add Photon GI", emit(Edit::Add)),
            ]
            .spacing(6),
        );
        if let Some(note) = view.status {
            content = content.push(status::note(note));
        }
        return scroll::vertical(container(content).padding(8)).into();
    };

    // Header: where the component is, and whether Photon is running.
    let tone = if settings.enabled {
        ChipTone::Success
    } else {
        ChipTone::Neutral
    };
    content = content.push(
        row![
            status::chip(if settings.enabled { "On" } else { "Off" }, tone),
            text(view.entity.unwrap_or("Photon"))
                .size(10)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    );

    // Presets.
    let preset_names: Vec<String> = presets().iter().map(|p| p.name.to_owned()).collect();
    let preset_lookup = preset_names.clone();
    content = content.push(dropdown::property(
        id::PHOTON_PRESET,
        "Preset",
        preset_names,
        current_preset(settings).map(str::to_owned),
        {
            let emit = emit.clone();
            move |choice| {
                emit(Edit::Preset(
                    preset_lookup.iter().position(|p| *p == choice).unwrap_or(0),
                ))
            }
        },
    ));

    // Lighting.
    let choice = |id: crate::uikit::id::UiId, label: &'static str, which: Choice, value: usize| {
        let options: Vec<String> = which.options().iter().map(|o| (*o).to_owned()).collect();
        let lookup = options.clone();
        let selected = options.get(value).cloned();
        let emit = emit.clone();
        dropdown::property(id, label, options, selected, move |picked: String| {
            emit(Edit::Choice(
                which,
                lookup.iter().position(|o| *o == picked).unwrap_or(0),
            ))
        })
    };
    let amount = |id: crate::uikit::id::UiId, label: &'static str, which: Amount, value: f32| {
        let emit = emit.clone();
        slider::property_step(id, label, which.range(), value, which.step(), move |v| {
            emit(Edit::Amount(which, v))
        })
    };
    let flag = |id: crate::uikit::id::UiId, label: &'static str, which: Flag, value: bool| {
        let emit = emit.clone();
        checkbox::property(id, label, value, move |on| emit(Edit::Flag(which, on)))
    };
    content = content.push(card(
        "Lighting",
        column![
            flag(
                id::PHOTON_ENABLED,
                "Enabled",
                Flag::Enabled,
                settings.enabled
            ),
            choice(id::PHOTON_PATH, "Path", Choice::Path, settings.path),
            choice(
                id::PHOTON_QUALITY,
                "Quality",
                Choice::Quality,
                settings.quality
            ),
            amount(
                id::PHOTON_INTENSITY,
                "Intensity",
                Amount::Intensity,
                settings.intensity
            ),
            amount(
                id::PHOTON_BOUNCE_GAIN,
                "Bounce gain",
                Amount::BounceGain,
                settings.bounce_gain
            ),
            amount(id::PHOTON_SKY, "Sky", Amount::Sky, settings.sky),
            amount(
                id::PHOTON_AMBIENT,
                "Ambient",
                Amount::Ambient,
                settings.ambient
            ),
            amount(id::PHOTON_RADIUS, "Radius", Amount::Radius, settings.radius),
        ]
        .spacing(4)
        .into(),
    ));

    // The geometry of the voxel volume.
    content = content.push(card(
        "Voxel geometry",
        column![
            flag(
                id::PHOTON_VOXEL_SKINNED,
                "Skinned meshes (current pose)",
                Flag::VoxelSkinned,
                settings.voxel_skinned
            ),
            flag(
                id::PHOTON_VOXEL_CUTOUT,
                "Cut-out meshes as solid",
                Flag::VoxelCutout,
                settings.voxel_cutout
            ),
            flag(
                id::PHOTON_VOXEL_TERRAIN,
                "Terrain",
                Flag::VoxelTerrain,
                settings.voxel_terrain
            ),
            flag(
                id::PHOTON_FULL_REBUILDS,
                "Full rebuilds (diagnostic)",
                Flag::FullRebuilds,
                settings.full_rebuilds
            ),
            button::toolbar(id::PHOTON_REBUILD, "Rebuild voxels", emit(Edit::Rebuild)),
        ]
        .spacing(4)
        .into(),
    ));

    // Debug view.
    content = content.push(card(
        "Debug",
        choice(
            id::PHOTON_DEBUG_VIEW,
            "View",
            Choice::DebugView,
            settings.debug_view,
        ),
    ));

    // Voxel statistics.
    if let Some(stats) = view.stats {
        let mut lines = column![
            stat("Objects", compact(stats.objects)),
            stat("Triangles", compact(stats.triangles)),
            stat(
                "Levels rebuilt (last)",
                format!("{:04b}", stats.last_level_mask & 0xF)
            ),
            stat(
                "Regions (last / total)",
                format!("{} / {}", stats.last_regions, stats.region_rebuilds)
            ),
            stat("Whole levels (total)", stats.full_rebuilds.to_string()),
            stat("Update time", format!("{:.2} ms", stats.last_update_ms)),
        ]
        .spacing(2);
        let mut chips = row![status::chip(
            if stats.far_enabled {
                "Far field running"
            } else {
                "Far field idle"
            },
            if stats.far_enabled {
                ChipTone::Success
            } else {
                ChipTone::Neutral
            },
        )]
        .spacing(6);
        if stats.over_budget {
            chips = chips.push(status::chip(
                "Over 1M triangles: screen fill only",
                ChipTone::Warning,
            ));
        }
        lines = lines.push(chips);
        content = content.push(card(
            "Voxel volume",
            container(lines).id(id::PHOTON_STATS).into(),
        ));
    }

    if let Some(note) = view.status {
        content = content.push(status::note(note));
    }
    scroll::vertical(container(content).padding(8)).into()
}

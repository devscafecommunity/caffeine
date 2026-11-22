//! Audio panel: the scene's volumetric audio.
//!
//! The mixer is set by one reflected component, `Volumetric Audio`; the sounds are the scene's `Audio Emitter`
//! entities and the things sound cannot cross are its `Acoustic Obstacle` entities. This panel is a pure view: the
//! app (`windows/main/audio.rs`) reads the component, applies every edit through the engine's reflected component
//! API (so the Inspector and scripts never disagree) and records an undo step. Under the settings it shows what the
//! mixer decides, live: the room round the listener and one row per emitter (distance, level, what the walls leave,
//! the Doppler shift, whether it is blocked or heard round a corner).
//!
//! Like the Environment panel, every control is an entry of a table ([`Flag`], [`Amount`]).

use crate::uikit::components::{button, checkbox, dropdown, scroll, slider, status};
use crate::uikit::id::{self, UiId};
use crate::uikit::theme::{self, ChipTone};
use iced::widget::{column, container, row, text};
use iced::{Element, Length};
use std::ops::RangeInclusive;
use std::rc::Rc;

/// The reflected component the panel edits.
pub const COMPONENT: &str = "Volumetric Audio";

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
    ($( $name:ident => ($field:expr, $key:expr, $label:expr, $min:expr, $max:expr, $step:expr, $default:expr, $id:expr) ),* $(,)?) => {
        /// A number of the component.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum Amount { $($name),* }
        impl Amount {
            pub const ALL: &'static [Amount] = &[$(Amount::$name),*];
            pub const COUNT: usize = Amount::ALL.len();
            pub fn index(self) -> usize { self as usize }
            pub fn spec(self) -> AmountSpec {
                match self {
                    $(Amount::$name => AmountSpec { field: $field, key: $key, label: $label, min: $min, max: $max, step: $step, default: $default, id: $id }),*
                }
            }
        }
    };
}

flags! {
    Enabled => ("Enabled", "enabled", "Volumetric audio on", true, id::AUDIO_ENABLED),
    Occlusion => ("Occlusion", "occlusion", "Walls muffle sound", true, id::AUDIO_OCCLUSION),
    Diffraction => ("Diffraction", "diffraction", "Sound bends round corners", true, id::AUDIO_DIFFRACTION),
    Reverb => ("Reverb", "reverb", "Room reverb and echoes", true, id::AUDIO_REVERB),
    AirAbsorption => ("Air Absorption", "air_absorption", "Air takes the highs", true, id::AUDIO_AIR_ABSORPTION),
    PropagationDelay => ("Propagation Delay", "propagation_delay", "Sound takes time to arrive", true, id::AUDIO_PROPAGATION_DELAY),
}

amounts! {
    Master => ("Master Volume", "master_volume", "Master", 0.0, 2.0, 0.01, 1.0, id::AUDIO_MASTER),
    World => ("World Volume", "world_volume", "World layer", 0.0, 2.0, 0.01, 1.0, id::AUDIO_WORLD),
    Ambient => ("Ambient Volume", "ambient_volume", "Ambient layer", 0.0, 2.0, 0.01, 1.0, id::AUDIO_AMBIENT),
    Music => ("Music Volume", "music_volume", "Music layer", 0.0, 2.0, 0.01, 1.0, id::AUDIO_MUSIC),
    Doppler => ("Doppler Scale", "doppler_scale", "Doppler", 0.0, 3.0, 0.05, 1.0, id::AUDIO_DOPPLER),
    SpeedOfSound => ("Speed Of Sound", "speed_of_sound", "Speed of sound", 100.0, 1500.0, 1.0, 343.0, id::AUDIO_SPEED_OF_SOUND),
}

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
}

/// A change the panel asks for.
#[derive(Debug, Clone)]
pub enum Edit {
    /// Adds the component on a new entity (when the scene has none).
    Add,
    Flag(Flag, bool),
    Amount(Amount, f32),
    /// Plays on this computer's sound device (or only monitors, silently).
    Output(bool),
    /// The device to play on: 0 is the system default, `n` the device `n - 1` of the list.
    Device(usize),
    /// A new entity with an `Audio Emitter`.
    AddSource,
    /// A new entity with an `Acoustic Obstacle`, a 6 x 3 x 0.3 m wall.
    AddWall,
}

/// What the mixer is doing, as the panel shows it (read from the engine by the app).
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Live {
    pub device_open: bool,
    pub obstacles: u32,
    pub clips_missing: u32,
    pub openness: f32,
    pub sky_exposure: f32,
    pub rt60_s: f32,
    pub wet: f32,
    pub echoes: u32,
    pub voices: Vec<VoiceRow>,
}

#[derive(Debug, Clone, Default, PartialEq)]
pub struct VoiceRow {
    pub name: String,
    /// 0 World, 1 Ambient, 2 Music, 3 Ui.
    pub layer: i32,
    pub loaded: bool,
    pub playing: bool,
    pub distance_m: f32,
    pub gain_db: f32,
    pub cutoff_hz: f32,
    pub pitch_ratio: f32,
    pub blocked: bool,
    pub diffracted: bool,
}

pub const LAYERS: [&str; 4] = ["World", "Ambient", "Music", "Ui"];

pub struct View<'a> {
    /// `None` while the scene has no `Volumetric Audio` component.
    pub settings: Option<&'a Settings>,
    pub entity: Option<&'a str>,
    pub status: Option<&'a str>,
    pub output_on: bool,
    /// The sound devices of the computer and the chosen one (0 = system default).
    pub devices: &'a [String],
    pub device: usize,
    pub live: &'a Live,
}

/// The one-line summary under the panel title.
pub fn summary(settings: Option<&Settings>, live: &Live) -> String {
    match settings {
        None => "no volumetric audio in the scene".to_owned(),
        Some(s) if !s.flag(Flag::Enabled) => "Off".to_owned(),
        Some(_) => format!(
            "{} voices · {}",
            live.voices.len(),
            if live.device_open {
                "playing"
            } else {
                "monitoring"
            }
        ),
    }
}

/// One line about a voice: what the mixer made of it.
pub fn voice_line(row: &VoiceRow) -> String {
    if !row.loaded {
        return format!("{} · clip not found", row.name);
    }
    if !row.playing {
        return format!("{} · stopped", row.name);
    }
    let mut line = format!(
        "{} · {} · {:.0} m · {:.0} dB · {:.1} kHz",
        row.name,
        LAYERS[row.layer.clamp(0, 3) as usize],
        row.distance_m,
        row.gain_db,
        row.cutoff_hz / 1000.0
    );
    if (row.pitch_ratio - 1.0).abs() > 0.003 {
        line.push_str(&format!(" · pitch x{:.2}", row.pitch_ratio));
    }
    if row.diffracted {
        line.push_str(" · round a corner");
    } else if row.blocked {
        line.push_str(" · behind a wall");
    }
    line
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

    let Some(settings) = view.settings else {
        content = content.push(
            column![
                text("No volumetric audio in this scene").size(12).color(theme::TEXT),
                text("Volumetric audio simulates how sound travels: it arrives late, is Doppler shifted, is muffled by walls, bends round corners and rings with the room. Audio Emitter entities are the sounds and Acoustic Obstacle entities are the walls. It lives in one component, so scripts and the Inspector can change it too.")
                    .size(10)
                    .color(theme::TEXT_MUTED),
                button::primary(id::AUDIO_ADD, "Add volumetric audio", emit(Edit::Add)),
            ]
            .spacing(6),
        );
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
            status::chip(
                if view.live.device_open {
                    "Playing"
                } else {
                    "Monitoring"
                },
                if view.live.device_open {
                    ChipTone::Success
                } else {
                    ChipTone::Neutral
                }
            ),
            text(view.entity.unwrap_or("Volumetric Audio"))
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

    let device = {
        let mut options = vec!["System default".to_owned()];
        options.extend(view.devices.iter().cloned());
        let selected = options.get(view.device).cloned();
        let lookup = options.clone();
        let emit = emit.clone();
        dropdown::property(
            id::AUDIO_DEVICE,
            "Device",
            options,
            selected,
            move |picked: String| {
                emit(Edit::Device(
                    lookup.iter().position(|o| *o == picked).unwrap_or(0),
                ))
            },
        )
    };

    content = content.push(card(
        "Output",
        column![
            checkbox::property(
                id::AUDIO_OUTPUT,
                "Play on this computer's speakers",
                view.output_on,
                {
                    let emit = emit.clone();
                    move |on| emit(Edit::Output(on))
                }
            ),
            text(if view.output_on && !view.live.device_open {
                "No sound device could be opened: the mixer keeps monitoring silently."
            } else {
                "Off: the mixer runs silently so the numbers below still move."
            })
            .size(9)
            .color(theme::TEXT_MUTED),
            device,
            flag(Flag::Enabled),
            amount(Amount::Master),
            amount(Amount::World),
            amount(Amount::Ambient),
            amount(Amount::Music),
        ]
        .spacing(4)
        .into(),
    ));

    content = content.push(card(
        "How sound travels",
        column![
            flag(Flag::PropagationDelay),
            amount(Amount::Doppler),
            amount(Amount::SpeedOfSound),
            flag(Flag::AirAbsorption),
            flag(Flag::Occlusion),
            flag(Flag::Diffraction),
            flag(Flag::Reverb),
        ]
        .spacing(4)
        .into(),
    ));

    let live = view.live;
    content = content.push(card(
        "Room round the listener",
        column![
            text(format!(
                "open {:.0}% · sky {:.0}% · reverb {:.2} s · wet {:.0}% · {} echoes",
                live.openness * 100.0,
                live.sky_exposure * 100.0,
                live.rt60_s,
                live.wet * 100.0,
                live.echoes
            ))
            .size(10)
            .color(theme::TEXT),
            text(format!(
                "{} acoustic obstacles{}",
                live.obstacles,
                if live.clips_missing > 0 {
                    format!(" · {} clips not found", live.clips_missing)
                } else {
                    String::new()
                }
            ))
            .size(10)
            .color(theme::TEXT_MUTED),
        ]
        .spacing(3)
        .into(),
    ));

    let mut voices = column![].spacing(3);
    if live.voices.is_empty() {
        voices = voices.push(
            text(
                "No Audio Emitter in the scene: add a source, then pick its clip in the Inspector.",
            )
            .size(10)
            .color(theme::TEXT_MUTED),
        );
    }
    for row in &live.voices {
        voices = voices.push(text(voice_line(row)).size(10).color(if row.blocked {
            theme::TEXT_MUTED
        } else {
            theme::TEXT
        }));
    }
    voices = voices.push(
        row![
            button::compact(id::AUDIO_ADD_SOURCE, "Add source", emit(Edit::AddSource)),
            button::compact(id::AUDIO_ADD_WALL, "Add wall", emit(Edit::AddWall)),
        ]
        .spacing(4),
    );
    content = content.push(card("Sounds", voices.into()));

    if let Some(note) = view.status {
        content = content.push(status::note(note));
    }
    scroll::vertical(container(content).padding(8)).into()
}

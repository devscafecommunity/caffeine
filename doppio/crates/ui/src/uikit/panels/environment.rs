//! Environment panel: the clouds, the fog and the rain of the scene.
//!
//! The scene's weather lives in two reflected components on one entity: `Volumetric Clouds` (the sky) and
//! `Weather` (fog, rain, and the coupling that makes them agree with the sun and Photon's sky light). This
//! panel is a pure view over their fields: the app (`windows/main/environment.rs`) reads the components,
//! applies every edit through the engine's reflected component API and records an undo step, so the panel
//! and the Inspector always show the same values.
//!
//! Every control is one entry of a table ([`Flag`], [`Amount`], [`Tint`] and the cloud quality choice): the
//! table carries the component, the reflected field name, the range, the default and the widget id, so the
//! panel, the presets, the agent actions and the tests all read the same list.

use crate::uikit::components::{button, checkbox, color_picker, dropdown, scroll, slider, status};
use crate::uikit::id::{self, UiId};
use crate::uikit::theme::{self, ChipTone};
use iced::widget::{column, container, row, text};
use iced::{Element, Length};
use std::ops::RangeInclusive;
use std::rc::Rc;

/// The reflected components the panel edits.
pub const CLOUDS: &str = "Volumetric Clouds";
pub const WEATHER: &str = "Weather";

/// Options of the cloud quality field (`Volumetric Clouds` → `Quality`, a number 0..3).
pub const QUALITIES: [&str; 4] = ["Off", "Low", "Medium", "High"];

/// Which component a field belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Target {
    Clouds,
    Weather,
}

impl Target {
    pub fn component(self) -> &'static str {
        match self {
            Target::Clouds => CLOUDS,
            Target::Weather => WEATHER,
        }
    }
}

/// One switch of a component.
#[derive(Debug, Clone)]
pub struct FlagSpec {
    pub target: Target,
    /// The reflected field name.
    pub field: &'static str,
    /// The name an agent uses (snake case, unique across the panel).
    pub key: &'static str,
    pub label: &'static str,
    pub default: bool,
    pub id: UiId,
}

/// One number of a component.
#[derive(Debug, Clone)]
pub struct AmountSpec {
    pub target: Target,
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
    ($( $name:ident => ($target:ident, $field:expr, $key:expr, $label:expr, $default:expr, $id:expr) ),* $(,)?) => {
        /// A switch of the components.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum Flag { $($name),* }

        impl Flag {
            pub const ALL: &'static [Flag] = &[$(Flag::$name),*];
            pub const COUNT: usize = Flag::ALL.len();

            pub fn index(self) -> usize { self as usize }

            pub fn spec(self) -> FlagSpec {
                match self {
                    $(Flag::$name => FlagSpec {
                        target: Target::$target,
                        field: $field,
                        key: $key,
                        label: $label,
                        default: $default,
                        id: $id,
                    }),*
                }
            }
        }
    };
}

macro_rules! amounts {
    ($( $name:ident => ($target:ident, $field:expr, $key:expr, $label:expr, $min:expr, $max:expr, $step:expr, $default:expr, $id:expr) ),* $(,)?) => {
        /// A number of the components.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        pub enum Amount { $($name),* }

        impl Amount {
            pub const ALL: &'static [Amount] = &[$(Amount::$name),*];
            pub const COUNT: usize = Amount::ALL.len();

            pub fn index(self) -> usize { self as usize }

            pub fn spec(self) -> AmountSpec {
                match self {
                    $(Amount::$name => AmountSpec {
                        target: Target::$target,
                        field: $field,
                        key: $key,
                        label: $label,
                        min: $min,
                        max: $max,
                        step: $step,
                        default: $default,
                        id: $id,
                    }),*
                }
            }
        }
    };
}

flags! {
    CloudsEnabled => (Clouds, "Enabled", "clouds_enabled", "Enabled", false, id::ENV_CLOUDS_ENABLED),
    CloudsDimSun => (Clouds, "Cast Shadows", "clouds_dim_sun", "Clouds dim the sun", true, id::ENV_CLOUDS_DIM_SUN),
    WeatherEnabled => (Weather, "Enabled", "weather_enabled", "Weather on", true, id::ENV_WEATHER_ENABLED),
    LinkSky => (Weather, "Link Sky", "link_sky", "Link sky, sun and fog", true, id::ENV_WEATHER_LINK_SKY),
    FogEnabled => (Weather, "Fog Enabled", "fog_enabled", "Fog", false, id::ENV_FOG_ENABLED),
    RainEnabled => (Weather, "Rain Enabled", "rain_enabled", "Rain", false, id::ENV_RAIN_ENABLED),
    CloudShadows => (Clouds, "Ground Shadows", "cloud_ground_shadows", "Shadows on the ground", false, id::ENV_CLOUDS_GROUND_SHADOWS),
    LightningEnabled => (Weather, "Lightning Enabled", "lightning_enabled", "Lightning", false, id::ENV_LIGHTNING_ENABLED),
    Thunder => (Weather, "Thunder", "thunder", "Thunder events", true, id::ENV_LIGHTNING_THUNDER),
    AudioEnabled => (Weather, "Audio Enabled", "audio_enabled", "Weather sound", false, id::ENV_AUDIO_ENABLED),
}

amounts! {
    Coverage => (Clouds, "Coverage", "coverage", "Coverage", 0.0, 1.0, 0.01, 0.55, id::ENV_CLOUDS_COVERAGE),
    CloudDensity => (Clouds, "Density", "cloud_density", "Density", 0.0, 2.0, 0.01, 0.35, id::ENV_CLOUDS_DENSITY),
    BaseAltitude => (Clouds, "Base Altitude", "base_altitude", "Base altitude", 100.0, 6000.0, 50.0, 1400.0, id::ENV_CLOUDS_BASE_ALTITUDE),
    TopAltitude => (Clouds, "Top Altitude", "top_altitude", "Top altitude", 200.0, 12000.0, 50.0, 2800.0, id::ENV_CLOUDS_TOP_ALTITUDE),
    Detail => (Clouds, "Detail", "detail", "Detail", 0.0, 1.0, 0.01, 0.55, id::ENV_CLOUDS_DETAIL),
    CloudWindSpeed => (Clouds, "Wind Speed", "cloud_wind_speed", "Wind speed", 0.0, 100.0, 0.5, 8.0, id::ENV_CLOUDS_WIND_SPEED),
    CloudWindDirection => (Clouds, "Wind Direction", "cloud_wind_direction", "Wind dir", 0.0, 360.0, 1.0, 35.0, id::ENV_CLOUDS_WIND_DIRECTION),
    SunScatter => (Clouds, "Sun Scatter", "sun_scatter", "Sun scatter", 0.0, 3.0, 0.05, 1.0, id::ENV_CLOUDS_SUN_SCATTER),
    Darkness => (Clouds, "Darkness", "darkness", "Darkness", 0.0, 1.0, 0.01, 0.0, id::ENV_CLOUDS_DARKNESS),
    SkyLight => (Clouds, "Sky Light", "sky_light", "Sky light", 0.0, 3.0, 0.05, 1.0, id::ENV_CLOUDS_SKY_LIGHT),
    MistDensity => (Clouds, "Mist Density", "mist_density", "Mist", 0.0, 1.0, 0.01, 0.12, id::ENV_CLOUDS_MIST_DENSITY),
    MistHeight => (Clouds, "Mist Height", "mist_height", "Mist height", 1.0, 500.0, 1.0, 40.0, id::ENV_CLOUDS_MIST_HEIGHT),
    FogDensity => (Weather, "Fog Density", "fog_density", "Density", 0.0, 0.2, 0.001, 0.012, id::ENV_FOG_DENSITY),
    FogHeightFalloff => (Weather, "Fog Height Falloff", "fog_height_falloff", "Falloff", 0.0, 0.5, 0.005, 0.06, id::ENV_FOG_HEIGHT_FALLOFF),
    FogBaseHeight => (Weather, "Fog Base Height", "fog_base_height", "Base height", -100.0, 500.0, 1.0, 0.0, id::ENV_FOG_BASE_HEIGHT),
    FogStart => (Weather, "Fog Start", "fog_start", "Start", 0.0, 500.0, 1.0, 0.0, id::ENV_FOG_START),
    FogMaxOpacity => (Weather, "Fog Max Opacity", "fog_max_opacity", "Max opacity", 0.0, 1.0, 0.01, 0.92, id::ENV_FOG_MAX_OPACITY),
    FogSkyBlend => (Weather, "Fog Sky Blend", "fog_sky_blend", "Sky blend", 0.0, 1.0, 0.01, 0.5, id::ENV_FOG_SKY_BLEND),
    FogSunScatter => (Weather, "Fog Sun Scatter", "fog_sun_scatter", "Sun glow", 0.0, 4.0, 0.05, 0.35, id::ENV_FOG_SUN_SCATTER),
    FogSunExponent => (Weather, "Fog Sun Exponent", "fog_sun_exponent", "Glow tight", 1.0, 64.0, 0.5, 6.0, id::ENV_FOG_SUN_EXPONENT),
    RainIntensity => (Weather, "Rain Intensity", "rain_intensity", "Intensity", 0.0, 1.0, 0.01, 0.5, id::ENV_RAIN_INTENSITY),
    RainWindSpeed => (Weather, "Rain Wind Speed", "rain_wind_speed", "Wind speed", 0.0, 40.0, 0.5, 4.0, id::ENV_RAIN_WIND_SPEED),
    RainWindDirection => (Weather, "Rain Wind Direction", "rain_wind_direction", "Wind dir", 0.0, 360.0, 1.0, 35.0, id::ENV_RAIN_WIND_DIRECTION),
    RainDropLength => (Weather, "Rain Drop Length", "rain_drop_length", "Drop length", 0.0, 1.0, 0.01, 0.5, id::ENV_RAIN_DROP_LENGTH),
    RainSpeed => (Weather, "Rain Speed", "rain_speed", "Fall speed", 0.0, 3.0, 0.05, 1.0, id::ENV_RAIN_SPEED),
    RainBrightness => (Weather, "Rain Brightness", "rain_brightness", "Brightness", 0.0, 2.0, 0.05, 0.6, id::ENV_RAIN_BRIGHTNESS),
    RainWetness => (Weather, "Rain Wetness", "rain_wetness", "Wetness", 0.0, 1.0, 0.01, 0.8, id::ENV_RAIN_WETNESS),
    CloudShadowStrength => (Clouds, "Shadow Strength", "cloud_shadow_strength", "Shadow strength", 0.0, 1.0, 0.01, 0.7, id::ENV_CLOUDS_SHADOW_STRENGTH),
    SilverLining => (Clouds, "Silver Lining", "silver_lining", "Silver lining", 0.0, 2.0, 0.05, 0.0, id::ENV_CLOUDS_SILVER_LINING),
    SunShafts => (Clouds, "Sun Shafts", "sun_shafts", "Sun shafts", 0.0, 1.0, 0.01, 0.0, id::ENV_CLOUDS_SUN_SHAFTS),
    RainSplashes => (Weather, "Rain Splashes", "rain_splashes", "Splashes", 0.0, 1.0, 0.01, 0.6, id::ENV_RAIN_SPLASHES),
    WindGust => (Weather, "Wind Gust", "wind_gust", "Gusts", 0.0, 1.0, 0.01, 0.35, id::ENV_WEATHER_WIND_GUST),
    WindFoliage => (Weather, "Wind Foliage", "wind_foliage", "Foliage sway", 0.0, 2.0, 0.05, 0.0, id::ENV_WEATHER_WIND_FOLIAGE),
    LightningRate => (Weather, "Lightning Rate", "lightning_rate", "Strikes / min", 0.5, 60.0, 0.5, 6.0, id::ENV_LIGHTNING_RATE),
    LightningIntensity => (Weather, "Lightning Intensity", "lightning_intensity", "Flash", 0.0, 2.0, 0.05, 1.0, id::ENV_LIGHTNING_INTENSITY),
    LightningDistanceMin => (Weather, "Lightning Distance Min", "lightning_distance_min", "Nearest", 50.0, 2000.0, 10.0, 300.0, id::ENV_LIGHTNING_DISTANCE_MIN),
    AudioVolume => (Weather, "Audio Volume", "audio_volume", "Volume", 0.0, 1.0, 0.01, 0.8, id::ENV_AUDIO_VOLUME),
    LightningDistanceMax => (Weather, "Lightning Distance Max", "lightning_distance_max", "Farthest", 500.0, 8000.0, 50.0, 3500.0, id::ENV_LIGHTNING_DISTANCE_MAX),
}

/// A colour (three channels) of the components.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Tint {
    /// `Volumetric Clouds` → `Tint`: multiplies the sun-lit side of the clouds.
    Clouds,
    /// `Weather` → `Fog Color` (sRGB).
    Fog,
}

impl Tint {
    pub const ALL: [Tint; 2] = [Tint::Clouds, Tint::Fog];

    pub fn target(self) -> Target {
        match self {
            Tint::Clouds => Target::Clouds,
            Tint::Fog => Target::Weather,
        }
    }

    pub fn field(self) -> &'static str {
        match self {
            Tint::Clouds => "Tint",
            Tint::Fog => "Fog Color",
        }
    }

    pub fn key(self) -> &'static str {
        match self {
            Tint::Clouds => "cloud_tint",
            Tint::Fog => "fog_color",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Tint::Clouds => "Sun-lit tint",
            Tint::Fog => "Fog colour",
        }
    }

    pub fn default(self) -> [f32; 3] {
        match self {
            Tint::Clouds => [1.0, 1.0, 1.0],
            Tint::Fog => [0.62, 0.68, 0.76],
        }
    }

    pub fn id(self, axis: usize) -> UiId {
        match (self, axis) {
            (Tint::Clouds, 0) => id::ENV_CLOUDS_TINT_R,
            (Tint::Clouds, 1) => id::ENV_CLOUDS_TINT_G,
            (Tint::Clouds, _) => id::ENV_CLOUDS_TINT_B,
            (Tint::Fog, 0) => id::ENV_FOG_COLOR_R,
            (Tint::Fog, 1) => id::ENV_FOG_COLOR_G,
            (Tint::Fog, _) => id::ENV_FOG_COLOR_B,
        }
    }
}

/// Every field the panel edits, as typed values.
#[derive(Debug, Clone, PartialEq)]
pub struct Settings {
    pub flags: [bool; Flag::COUNT],
    pub amounts: [f32; Amount::COUNT],
    /// Index into [`QUALITIES`].
    pub quality: usize,
    pub cloud_tint: [f32; 3],
    pub fog_color: [f32; 3],
}

impl Default for Settings {
    /// What the two components start with in the engine.
    fn default() -> Self {
        let mut flags = [false; Flag::COUNT];
        for flag in Flag::ALL {
            flags[flag.index()] = flag.spec().default;
        }
        let mut amounts = [0.0; Amount::COUNT];
        for amount in Amount::ALL {
            amounts[amount.index()] = amount.spec().default;
        }
        Settings {
            flags,
            amounts,
            quality: 2,
            cloud_tint: Tint::Clouds.default(),
            fog_color: Tint::Fog.default(),
        }
    }
}

impl Settings {
    pub fn flag(&self, flag: Flag) -> bool {
        self.flags[flag.index()]
    }

    pub fn amount(&self, amount: Amount) -> f32 {
        self.amounts[amount.index()]
    }

    pub fn tint(&self, tint: Tint) -> [f32; 3] {
        match tint {
            Tint::Clouds => self.cloud_tint,
            Tint::Fog => self.fog_color,
        }
    }

    /// These settings with the weather's sound (the switch and the volume) taken from `other`.
    pub fn keep_audio_of(mut self, other: &Settings) -> Self {
        self.flags[Flag::AudioEnabled.index()] = other.flags[Flag::AudioEnabled.index()];
        self.amounts[Amount::AudioVolume.index()] = other.amounts[Amount::AudioVolume.index()];
        self
    }

    pub fn with_flag(mut self, flag: Flag, on: bool) -> Self {
        self.flags[flag.index()] = on;
        self
    }

    pub fn with(mut self, amount: Amount, value: f32) -> Self {
        self.amounts[amount.index()] = value;
        self
    }

    pub fn with_tint(mut self, tint: Tint, value: [f32; 3]) -> Self {
        match tint {
            Tint::Clouds => self.cloud_tint = value,
            Tint::Fog => self.fog_color = value,
        }
        self
    }

    /// The preset with every section off (what adding the components alone gives).
    pub fn off() -> Self {
        Settings::default()
    }
}

/// A named starting point for the settings.
#[derive(Debug, Clone, PartialEq)]
pub struct Preset {
    pub name: &'static str,
    pub settings: Settings,
}

/// The presets, in the order the dropdown lists them.
pub fn presets() -> Vec<Preset> {
    use Amount::*;
    use Flag::*;
    let base = Settings::off();
    // Mist is left at 0 in every preset: a cloud mist above 0 also switches on the forward pass's volumetric fog
    // (a per-pixel ray march through the lights, a few milliseconds at 720p). The height fog covers the haze.
    let clouds = base
        .clone()
        .with_flag(CloudsEnabled, true)
        .with_flag(CloudShadows, true)
        .with(SilverLining, 0.6)
        .with(SunShafts, 0.35)
        .with(WindFoliage, 1.0)
        .with(MistDensity, 0.0);
    vec![
        Preset {
            name: "Off",
            settings: base.clone(),
        },
        Preset {
            name: "Clear sky",
            settings: clouds.clone().with(Coverage, 0.25).with(CloudDensity, 0.3),
        },
        Preset {
            name: "Cloudy",
            settings: clouds
                .clone()
                .with(Coverage, 0.75)
                .with(CloudDensity, 0.5)
                .with(Darkness, 0.15)
                .with(CloudWindSpeed, 14.0),
        },
        Preset {
            name: "Fog",
            settings: clouds
                .clone()
                .with(Coverage, 0.55)
                .with(CloudDensity, 0.4)
                .with_flag(FogEnabled, true)
                .with(FogDensity, 0.03)
                .with(FogHeightFalloff, 0.035)
                .with(FogMaxOpacity, 0.96)
                .with(FogSkyBlend, 0.7)
                .with(FogSunScatter, 0.5),
        },
        Preset {
            name: "Light rain",
            settings: clouds
                .clone()
                .with(Coverage, 0.8)
                .with(CloudDensity, 0.5)
                .with(Darkness, 0.3)
                .with_flag(RainEnabled, true)
                .with(RainIntensity, 0.35)
                .with(RainWindSpeed, 3.0)
                .with(RainDropLength, 0.4)
                .with(RainWetness, 0.7)
                .with_flag(FogEnabled, true)
                .with(FogDensity, 0.006)
                .with(FogHeightFalloff, 0.05),
        },
        Preset {
            name: "Storm",
            settings: clouds
                .with(Coverage, 0.95)
                .with(CloudDensity, 0.8)
                .with(Darkness, 0.6)
                .with(CloudWindSpeed, 30.0)
                .with(SunShafts, 0.0)
                .with(WindGust, 0.7)
                .with_flag(LightningEnabled, true)
                .with(LightningRate, 9.0)
                .with_flag(RainEnabled, true)
                .with(RainIntensity, 0.95)
                .with(RainWindSpeed, 14.0)
                .with(RainDropLength, 0.7)
                .with(RainBrightness, 0.8)
                .with(RainWetness, 1.0)
                .with_flag(FogEnabled, true)
                .with(FogDensity, 0.01)
                .with(FogHeightFalloff, 0.04)
                .with_tint(Tint::Fog, [0.48, 0.52, 0.58])
                .with_tint(Tint::Clouds, [0.8, 0.82, 0.88]),
        },
    ]
}

/// A change the panel asks for.
#[derive(Debug, Clone)]
pub enum Edit {
    /// Adds both components on a new entity (when the scene has none), starting from Clear sky.
    Add,
    Flag(Flag, bool),
    Amount(Amount, f32),
    /// Index into [`QUALITIES`].
    Quality(usize),
    /// One channel (0..3) of a colour.
    Tint(Tint, usize, f32),
    /// A whole colour at once (agents, scripts).
    Colour(Tint, [f32; 3]),
    /// Index into [`presets`].
    Preset(usize),
}

pub struct View<'a> {
    /// `None` while the scene has neither component.
    pub settings: Option<&'a Settings>,
    /// The entity that carries them.
    pub entity: Option<&'a str>,
    pub status: Option<&'a str>,
}

/// The preset the settings equal, if any.
pub fn current_preset(settings: &Settings) -> Option<&'static str> {
    // The weather's sound is a choice of the editor, not part of a look: a preset matches with it on or off.
    let looks = settings.clone().keep_audio_of(&Settings::default());
    presets()
        .into_iter()
        .find(|preset| preset.settings == looks)
        .map(|preset| preset.name)
}

/// The one-line summary under the panel title.
pub fn summary(settings: Option<&Settings>) -> String {
    let Some(s) = settings else {
        return "no weather in the scene".to_owned();
    };
    if !s.flag(Flag::WeatherEnabled) && !s.flag(Flag::CloudsEnabled) {
        return "Off".to_owned();
    }
    let mut parts = Vec::new();
    if s.flag(Flag::CloudsEnabled) {
        parts.push(format!("clouds {:.0}%", s.amount(Amount::Coverage) * 100.0));
    }
    if s.flag(Flag::WeatherEnabled) && s.flag(Flag::FogEnabled) {
        parts.push("fog".to_owned());
    }
    if s.flag(Flag::WeatherEnabled) && s.flag(Flag::RainEnabled) {
        parts.push(format!(
            "rain {:.0}%",
            s.amount(Amount::RainIntensity) * 100.0
        ));
    }
    if parts.is_empty() {
        "Off".to_owned()
    } else {
        parts.join(" · ")
    }
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
                text("No weather in this scene")
                    .size(12)
                    .color(theme::TEXT),
                text("The environment is clouds in the sky, fog and rain. It lives in two components on one entity, so scripts and the Inspector can change it too.")
                    .size(10)
                    .color(theme::TEXT_MUTED),
                button::primary(id::ENV_ADD, "Add environment", emit(Edit::Add)),
            ]
            .spacing(6),
        );
        if let Some(note) = view.status {
            content = content.push(status::note(note));
        }
        return scroll::vertical(container(content).padding(8)).into();
    };

    let any_on = settings.flag(Flag::CloudsEnabled)
        || (settings.flag(Flag::WeatherEnabled)
            && (settings.flag(Flag::FogEnabled) || settings.flag(Flag::RainEnabled)));
    content = content.push(
        row![
            status::chip(
                if any_on { "On" } else { "Off" },
                if any_on {
                    ChipTone::Success
                } else {
                    ChipTone::Neutral
                }
            ),
            text(view.entity.unwrap_or("Environment"))
                .size(10)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    );

    let preset_names: Vec<String> = presets().iter().map(|p| p.name.to_owned()).collect();
    let preset_lookup = preset_names.clone();
    content = content.push(dropdown::property(
        id::ENV_PRESET,
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
    let tint = |which: Tint| {
        let emit = emit.clone();
        color_picker::rgb(
            which.label(),
            settings.tint(which),
            move |axis| which.id(axis).into(),
            move |axis, value| emit(Edit::Tint(which, axis, value)),
        )
    };
    let quality = {
        let options: Vec<String> = QUALITIES.iter().map(|o| (*o).to_owned()).collect();
        let lookup = options.clone();
        let selected = options.get(settings.quality).cloned();
        let emit = emit.clone();
        dropdown::property(
            id::ENV_CLOUDS_QUALITY,
            "Quality",
            options,
            selected,
            move |picked: String| {
                emit(Edit::Quality(
                    lookup.iter().position(|o| *o == picked).unwrap_or(0),
                ))
            },
        )
    };

    content = content.push(card(
        "Clouds",
        column![
            flag(Flag::CloudsEnabled),
            quality,
            amount(Amount::Coverage),
            amount(Amount::CloudDensity),
            amount(Amount::BaseAltitude),
            amount(Amount::TopAltitude),
            amount(Amount::Detail),
            amount(Amount::CloudWindSpeed),
            amount(Amount::CloudWindDirection),
            amount(Amount::SunScatter),
            amount(Amount::Darkness),
            amount(Amount::SkyLight),
            amount(Amount::MistDensity),
            amount(Amount::MistHeight),
            tint(Tint::Clouds),
            flag(Flag::CloudsDimSun),
            flag(Flag::CloudShadows),
            amount(Amount::CloudShadowStrength),
            amount(Amount::SilverLining),
            amount(Amount::SunShafts),
        ]
        .spacing(4)
        .into(),
    ));

    content = content.push(card(
        "Fog",
        column![
            flag(Flag::FogEnabled),
            amount(Amount::FogDensity),
            amount(Amount::FogHeightFalloff),
            amount(Amount::FogBaseHeight),
            amount(Amount::FogStart),
            amount(Amount::FogMaxOpacity),
            amount(Amount::FogSkyBlend),
            amount(Amount::FogSunScatter),
            amount(Amount::FogSunExponent),
            tint(Tint::Fog),
        ]
        .spacing(4)
        .into(),
    ));

    content = content.push(card(
        "Rain",
        column![
            flag(Flag::RainEnabled),
            amount(Amount::RainIntensity),
            amount(Amount::RainWindSpeed),
            amount(Amount::RainWindDirection),
            amount(Amount::RainDropLength),
            amount(Amount::RainSpeed),
            amount(Amount::RainBrightness),
            amount(Amount::RainWetness),
            amount(Amount::RainSplashes),
        ]
        .spacing(4)
        .into(),
    ));

    content = content.push(card(
        "Wind",
        column![
            amount(Amount::WindGust),
            amount(Amount::WindFoliage),
            text("The rain's slant, the foliage and the clouds' drift surge together; the wind's speed and direction come from Rain and Clouds.")
                .size(9)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(4)
        .into(),
    ));

    content = content.push(card(
        "Sound",
        column![
            flag(Flag::AudioEnabled),
            amount(Amount::AudioVolume),
            text("Rain, wind and thunder, made from noise: the rain follows the intensity, the wind follows its speed and gusts, and each strike plays a clap panned to where it struck, once the sound has travelled.")
                .size(9)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(4)
        .into(),
    ));

    content = content.push(card(
        "Lightning",
        column![
            flag(Flag::LightningEnabled),
            flag(Flag::Thunder),
            amount(Amount::LightningRate),
            amount(Amount::LightningIntensity),
            amount(Amount::LightningDistanceMin),
            amount(Amount::LightningDistanceMax),
        ]
        .spacing(4)
        .into(),
    ));

    content = content.push(card(
        "Sky, sun and light",
        column![
            flag(Flag::WeatherEnabled),
            flag(Flag::LinkSky),
            text("Linked: rain thickens the clouds and the fog; an overcast sky dims the sun and greys the sky light Photon bounces; fog tints it.")
                .size(9)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(4)
        .into(),
    ));

    if let Some(note) = view.status {
        content = content.push(status::note(note));
    }
    scroll::vertical(container(content).padding(8)).into()
}

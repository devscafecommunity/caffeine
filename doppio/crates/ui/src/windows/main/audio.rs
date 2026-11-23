//! Editor state and message handling of the **Audio** pane (the scene's volumetric audio).
//!
//! The settings are one reflected component, `Volumetric Audio`. This module finds the entity that carries it, reads it
//! into the panel's typed [`Settings`], applies every edit through the engine's reflected component API and records an
//! undo step per edit. Under the settings it reads the mixer's live state (the room round the listener and one row per
//! emitter) while the tab shows.

use super::{add_component, component_type_id, set_component_field, App, LogLevel, PaneKind};
use crate::uikit::panels::audio::{Amount, Edit, Flag, Live, Settings, VoiceRow, COMPONENT};
use doppio_engine::FieldValue;

#[derive(Default)]
pub(super) struct AudioState {
    /// The entity that carries the component, when the scene has it.
    pub entity: Option<u32>,
    pub settings: Option<Settings>,
    pub status: Option<String>,
    /// The speakers switch of the panel (an editor choice, not part of the scene).
    pub output_on: bool,
    /// The computer's sound devices and the chosen one (0 = system default).
    pub devices: Vec<String>,
    pub device: usize,
    pub live: Live,
}

fn has_component(engine: &doppio_engine::Engine, entity: u32) -> bool {
    engine
        .entity_components(entity)
        .iter()
        .any(|component| component.name == COMPONENT)
}

/// Reads the component of `entity` into the panel's settings; a field the engine library does not have keeps its default.
fn read_settings(engine: &doppio_engine::Engine, entity: u32) -> Option<Settings> {
    let components = engine.entity_components(entity);
    let index = components
        .iter()
        .position(|component| component.name == COMPONENT)?;
    let names: Vec<String> = engine
        .component_types()
        .into_iter()
        .find(|kind| kind.name == COMPONENT)?
        .fields
        .into_iter()
        .map(|field| field.name)
        .collect();
    let values = engine.component_values(entity, index as u32);
    let mut settings = Settings::default();
    for (name, value) in names.iter().zip(values.iter()) {
        for flag in Flag::ALL {
            if flag.spec().field == name {
                if let FieldValue::Bool(on) = value {
                    settings.flags[flag.index()] = *on;
                }
            }
        }
        for amount in Amount::ALL {
            if amount.spec().field == name {
                if let FieldValue::F32(v) = value {
                    settings.amounts[amount.index()] = *v;
                }
            }
        }
    }
    Some(settings)
}

fn ensure_component(engine: &mut doppio_engine::Engine, entity: u32) -> Result<u32, String> {
    if has_component(engine, entity) {
        component_type_id(engine, COMPONENT)
            .ok_or_else(|| format!("component {COMPONENT} is not available"))
    } else {
        add_component(engine, entity, COMPONENT)
    }
}

fn write_flag(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    flag: Flag,
    on: bool,
) -> Result<(), String> {
    let type_id = ensure_component(engine, entity)?;
    set_component_field(
        engine,
        entity,
        type_id,
        flag.spec().field,
        FieldValue::Bool(on),
    )
}

fn write_amount(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    amount: Amount,
    value: f32,
) -> Result<(), String> {
    let type_id = ensure_component(engine, entity)?;
    let spec = amount.spec();
    set_component_field(
        engine,
        entity,
        type_id,
        spec.field,
        FieldValue::F32(value.clamp(spec.min, spec.max)),
    )
}

impl App {
    /// Whether the Audio tab is the one showing in its pane.
    pub(super) fn audio_visible(&self) -> bool {
        self.panes
            .iter()
            .any(|(_, group)| group.pages.get(group.active) == Some(&PaneKind::Audio))
    }

    /// Reads the scene's volumetric audio component again (after an edit, an undo, a scene load).
    pub(super) fn refresh_audio(&mut self) {
        let found = self.engine.as_ref().and_then(|engine| {
            let entity = self
                .entities
                .iter()
                .map(|e| e.id)
                .find(|id| has_component(engine, *id))?;
            Some((entity, read_settings(engine, entity)?))
        });
        match found {
            Some((entity, settings)) => {
                self.audio.entity = Some(entity);
                self.audio.settings = Some(settings);
            }
            None => {
                self.audio.entity = None;
                self.audio.settings = None;
            }
        }
    }

    /// Reads the mixer's live state; called for every frame the viewport finishes, and only does work while the tab shows.
    pub(super) fn audio_tick(&mut self) {
        if !self.audio_visible() {
            return;
        }
        let Some(engine) = self.engine.as_ref() else {
            return;
        };
        let Ok(state) = engine.volumetric_state() else {
            return;
        };
        let mut live = Live {
            device_open: state.device_open,
            obstacles: state.obstacles,
            clips_missing: state.clips_missing,
            openness: state.openness,
            sky_exposure: state.sky_exposure,
            rt60_s: state.rt60_s,
            wet: state.wet,
            echoes: state.echoes,
            voices: Vec::new(),
        };
        for index in 0..state.voices {
            let Ok(voice) = engine.volumetric_voice(index) else {
                continue;
            };
            let name = self
                .entities
                .iter()
                .find(|entity| entity.id == voice.entity)
                .map(|entity| entity.name.clone())
                .unwrap_or_else(|| format!("Entity {}", voice.entity));
            live.voices.push(VoiceRow {
                name,
                layer: voice.layer,
                loaded: voice.loaded,
                playing: voice.playing,
                distance_m: voice.distance_m,
                gain_db: voice.gain_db,
                cutoff_hz: voice.cutoff_hz,
                pitch_ratio: voice.pitch_ratio,
                blocked: voice.blocked,
                diffracted: voice.diffracted,
            });
        }
        self.audio.live = live;
    }

    pub(super) fn audio_entity_name(&self) -> Option<&str> {
        let id = self.audio.entity?;
        self.entities
            .iter()
            .find(|entity| entity.id == id)
            .map(|entity| entity.name.as_str())
    }

    pub(super) fn edit_audio(&mut self, edit: Edit) {
        self.audio.status = None;
        match edit {
            Edit::Add => self.add_audio(),
            Edit::Flag(flag, on) => {
                let Some(entity) = self.audio.entity else {
                    return;
                };
                let result = match self.engine.as_mut() {
                    Some(engine) => write_flag(engine, entity, flag, on),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_audio_edit(
                    result,
                    format!("Audio {}", flag.spec().label),
                    Some(flag.spec().key),
                );
            }
            Edit::Amount(amount, value) => {
                let Some(entity) = self.audio.entity else {
                    return;
                };
                let result = match self.engine.as_mut() {
                    Some(engine) => write_amount(engine, entity, amount, value),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_audio_edit(
                    result,
                    format!("Audio {}", amount.spec().label),
                    Some(amount.spec().key),
                );
            }
            Edit::Output(on) => {
                self.audio.output_on = on;
                let result = match self.engine.as_mut() {
                    Some(engine) => engine.set_audio_output(on),
                    None => Ok(()),
                };
                if result.is_err() && on {
                    self.audio.status = Some(
                        "No sound device could be opened; the mixer keeps monitoring silently."
                            .to_owned(),
                    );
                }
            }
            Edit::Device(index) => {
                self.audio.device = index.min(self.audio.devices.len());
                let pick = self.audio.device.checked_sub(1);
                let result = match self.engine.as_mut() {
                    Some(engine) => engine.set_audio_output_device(pick),
                    None => Ok(()),
                };
                if result.is_err() && self.audio.output_on {
                    self.audio.status = Some(
                        "That device could not be opened; the mixer keeps monitoring silently."
                            .to_owned(),
                    );
                }
            }
            Edit::AddSource => self.add_audio_source(),
            Edit::AddWall => self.add_audio_wall(),
        }
    }

    fn finish_audio_edit(&mut self, result: Result<(), String>, name: String, key: Option<&str>) {
        match result {
            Ok(()) => {
                self.refresh_audio();
                self.refresh_components();
                // Sliders send a stream of values: one undo step per field, not per value.
                self.commit(name, key.map(|key| format!("audio:{key}")));
            }
            Err(error) => {
                self.audio.status = Some(format!("Could not change the audio: {error}"));
                self.log(LogLevel::Error, format!("Audio: {error}"));
                self.refresh_audio();
            }
        }
    }

    /// Adds the component on a new entity (when the scene has none).
    fn add_audio(&mut self) {
        if self.audio.entity.is_some() {
            return;
        }
        let selected = self.selected;
        self.add_entity_named("Volumetric Audio".to_owned());
        let Some(entity) = self.entities.last().map(|entity| entity.id) else {
            return;
        };
        let result = match self.engine.as_mut() {
            Some(engine) => ensure_component(engine, entity).map(|_| ()),
            None => Err("The engine is not available".to_owned()),
        };
        // The new entity is a tool, not a selection the user made.
        self.selected = selected.min(self.entities.len().saturating_sub(1));
        self.refresh_components();
        self.finish_audio_edit(result, "Add volumetric audio".to_owned(), None);
    }

    /// A new entity with an `Audio Emitter`, placed in front of the viewport's focus, selected so its clip can be picked.
    fn add_audio_source(&mut self) {
        self.add_entity_named("Audio Source".to_owned());
        let Some(entity) = self.entities.last().map(|entity| entity.id) else {
            return;
        };
        let focus = self.camera.focus;
        let result = match self.engine.as_mut() {
            Some(engine) => add_component(engine, entity, "Audio Emitter").and_then(|_| {
                engine
                    .set_position(entity, focus)
                    .map_err(|error| format!("{error:?}"))
            }),
            None => Err("The engine is not available".to_owned()),
        };
        self.refresh_components();
        self.refresh_audio();
        self.finish_audio_edit(result, "Add audio source".to_owned(), None);
    }

    /// A new entity with an `Acoustic Obstacle` on a 6 x 3 x 0.3 m box, at the viewport's focus.
    fn add_audio_wall(&mut self) {
        self.add_entity_named("Acoustic Wall".to_owned());
        let Some(entity) = self.entities.last().map(|entity| entity.id) else {
            return;
        };
        let focus = self.camera.focus;
        let result = match self.engine.as_mut() {
            Some(engine) => add_component(engine, entity, "Acoustic Obstacle").and_then(|_| {
                engine
                    .set_position(entity, [focus[0], focus[1] + 1.5, focus[2]])
                    .and_then(|_| engine.set_scale(entity, [6.0, 3.0, 0.3]))
                    .map_err(|error| format!("{error:?}"))
            }),
            None => Err("The engine is not available".to_owned()),
        };
        if let Some(view) = self.entities.last_mut() {
            view.position = [focus[0], focus[1] + 1.5, focus[2]];
            view.scale = [6.0, 3.0, 0.3];
        }
        self.refresh_components();
        self.finish_audio_edit(result, "Add acoustic wall".to_owned(), None);
    }

    pub(super) fn show_audio_pane(&mut self) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group.pages.iter().position(|page| *page == PaneKind::Audio) {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Inspector) {
                    group.pages.push(PaneKind::Audio);
                    group.active = group.pages.len() - 1;
                    break;
                }
            }
        }
        self.refresh_audio();
        if let Some(engine) = self.engine.as_ref() {
            self.audio.devices = engine.audio_output_devices();
            self.audio.device = self.audio.device.min(self.audio.devices.len());
        }
        self.audio_tick();
    }
}

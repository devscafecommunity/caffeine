//! Editor state and message handling of the **Environment** pane (clouds, fog, rain).
//!
//! The weather is two reflected components on one entity: `Volumetric Clouds` and `Weather`. This module
//! finds that entity, reads both components into the panel's typed [`Settings`], applies every edit through
//! the engine's reflected component API (so the Inspector, scripts and the panel never disagree) and
//! records an undo step per edit. A preset writes every field of both components in one step.

use super::{add_component, component_type_id, set_component_field, App, LogLevel, PaneKind};
use crate::uikit::panels::environment::{
    self as panel, Amount, Edit, Flag, Settings, Target, Tint, CLOUDS, WEATHER,
};
use doppio_engine::FieldValue;

#[derive(Default)]
pub(super) struct EnvironmentState {
    /// The entity that carries the components, when the scene has them.
    pub entity: Option<u32>,
    pub settings: Option<Settings>,
    pub status: Option<String>,
}

fn has_component(engine: &doppio_engine::Engine, entity: u32, name: &str) -> bool {
    engine
        .entity_components(entity)
        .iter()
        .any(|component| component.name == name)
}

/// The first entity of `ids` that carries either weather component.
fn find_entity(engine: &doppio_engine::Engine, ids: impl Iterator<Item = u32>) -> Option<u32> {
    let ids: Vec<u32> = ids.collect();
    // The entity with both is the one the panel made; otherwise any that has one.
    ids.iter()
        .copied()
        .find(|id| has_component(engine, *id, WEATHER) && has_component(engine, *id, CLOUDS))
        .or_else(|| {
            ids.iter().copied().find(|id| {
                has_component(engine, *id, WEATHER) || has_component(engine, *id, CLOUDS)
            })
        })
}

/// The values of one component by field name (`None` when the entity does not carry it).
fn component_values(
    engine: &doppio_engine::Engine,
    entity: u32,
    name: &str,
) -> Option<Vec<(String, FieldValue)>> {
    let components = engine.entity_components(entity);
    let index = components
        .iter()
        .position(|component| component.name == name)?;
    let names: Vec<String> = engine
        .component_types()
        .into_iter()
        .find(|kind| kind.name == name)?
        .fields
        .into_iter()
        .map(|field| field.name)
        .collect();
    let values = engine.component_values(entity, index as u32);
    Some(names.into_iter().zip(values).collect())
}

/// Reads both components of `entity` into the panel's settings; a field the engine library does not have
/// (or a component the entity lacks) keeps its default.
pub(super) fn read_settings(engine: &doppio_engine::Engine, entity: u32) -> Option<Settings> {
    let clouds = component_values(engine, entity, CLOUDS);
    let weather = component_values(engine, entity, WEATHER);
    if clouds.is_none() && weather.is_none() {
        return None;
    }
    let get = |target: Target, field: &str| -> Option<&FieldValue> {
        let values = match target {
            Target::Clouds => clouds.as_ref(),
            Target::Weather => weather.as_ref(),
        }?;
        values
            .iter()
            .find(|(name, _)| name == field)
            .map(|(_, value)| value)
    };
    let mut settings = Settings::default();
    for flag in Flag::ALL {
        let spec = flag.spec();
        if let Some(FieldValue::Bool(on)) = get(spec.target, spec.field) {
            settings.flags[flag.index()] = *on;
        }
    }
    for amount in Amount::ALL {
        let spec = amount.spec();
        if let Some(FieldValue::F32(v)) = get(spec.target, spec.field) {
            settings.amounts[amount.index()] = *v;
        }
    }
    match get(Target::Clouds, "Quality") {
        Some(FieldValue::U32(v)) => {
            settings.quality = (*v as usize).min(panel::QUALITIES.len() - 1)
        }
        Some(FieldValue::I32(v)) | Some(FieldValue::Enum(v)) => {
            settings.quality = (*v).max(0) as usize
        }
        _ => {}
    }
    for tint in Tint::ALL {
        if let Some(FieldValue::Vec3(v)) = get(tint.target(), tint.field()) {
            match tint {
                Tint::Clouds => settings.cloud_tint = *v,
                Tint::Fog => settings.fog_color = *v,
            }
        }
    }
    Some(settings)
}

/// The type id of `name`, adding the component to `entity` first when it is not there.
fn ensure_component(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    name: &str,
) -> Result<u32, String> {
    if has_component(engine, entity, name) {
        component_type_id(engine, name).ok_or_else(|| format!("component {name} is not available"))
    } else {
        add_component(engine, entity, name)
    }
}

fn type_id_of(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    target: Target,
) -> Result<u32, String> {
    ensure_component(engine, entity, target.component())
}

fn write_flag(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    flag: Flag,
    on: bool,
) -> Result<(), String> {
    let spec = flag.spec();
    let type_id = type_id_of(engine, entity, spec.target)?;
    set_component_field(engine, entity, type_id, spec.field, FieldValue::Bool(on))
}

fn write_amount(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    amount: Amount,
    value: f32,
) -> Result<(), String> {
    let spec = amount.spec();
    let type_id = type_id_of(engine, entity, spec.target)?;
    set_component_field(engine, entity, type_id, spec.field, FieldValue::F32(value))
}

fn write_quality(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    index: usize,
) -> Result<(), String> {
    let type_id = type_id_of(engine, entity, Target::Clouds)?;
    set_component_field(
        engine,
        entity,
        type_id,
        "Quality",
        FieldValue::U32(index.min(panel::QUALITIES.len() - 1) as u32),
    )
}

fn write_tint(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    tint: Tint,
    value: [f32; 3],
) -> Result<(), String> {
    let type_id = type_id_of(engine, entity, tint.target())?;
    set_component_field(
        engine,
        entity,
        type_id,
        tint.field(),
        FieldValue::Vec3(value),
    )
}

/// Writes every field of `settings` into the two components of `entity` (adding them when missing).
fn write_settings(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    settings: &Settings,
) -> Result<(), String> {
    for flag in Flag::ALL {
        write_flag(engine, entity, *flag, settings.flag(*flag))?;
    }
    for amount in Amount::ALL {
        write_amount(engine, entity, *amount, settings.amount(*amount))?;
    }
    write_quality(engine, entity, settings.quality)?;
    for tint in Tint::ALL {
        write_tint(engine, entity, tint, settings.tint(tint))?;
    }
    Ok(())
}

impl App {
    /// Reads the scene's weather components again (after an edit, an undo, a scene load).
    pub(super) fn refresh_environment(&mut self) {
        let ids = self.entities.iter().map(|entity| entity.id);
        let found = self.engine.as_ref().and_then(|engine| {
            let entity = find_entity(engine, ids)?;
            Some((entity, read_settings(engine, entity)?))
        });
        match found {
            Some((entity, settings)) => {
                self.environment.entity = Some(entity);
                self.environment.settings = Some(settings);
            }
            None => {
                self.environment.entity = None;
                self.environment.settings = None;
            }
        }
    }

    /// Name of the entity that carries the components.
    /// Called for every frame the viewport finishes: logs the thunder that has reached the listener since the
    /// last frame ("Thunder, 1.2 km"). The engine makes no sound; scripts and the runtime turn the events into
    /// one. Returns how many claps arrived (zero while there is no lightning).
    pub(super) fn thunder_tick(&mut self) -> usize {
        let Some(engine) = self.engine.as_mut() else {
            return 0;
        };
        let events = engine.poll_thunder().unwrap_or_default();
        for event in &events {
            let compass = (event.azimuth_rad.to_degrees().rem_euclid(360.0)).round();
            self.log(
                LogLevel::Engine,
                format!(
                    "Thunder, {:.1} km away (bearing {compass:.0}°, {:.0}% loud)",
                    event.distance_m / 1000.0,
                    event.volume * 100.0
                ),
            );
        }
        events.len()
    }

    pub(super) fn environment_entity_name(&self) -> Option<&str> {
        let id = self.environment.entity?;
        self.entities
            .iter()
            .find(|entity| entity.id == id)
            .map(|entity| entity.name.as_str())
    }

    pub(super) fn edit_environment(&mut self, edit: Edit) {
        self.environment.status = None;
        match edit {
            Edit::Add => self.add_environment(),
            Edit::Flag(which, on) => {
                let Some(entity) = self.environment.entity else {
                    return;
                };
                let result = match self.engine.as_mut() {
                    Some(engine) => write_flag(engine, entity, which, on),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_environment_edit(
                    result,
                    format!("Environment {}", which.spec().label),
                    Some(which.spec().key),
                );
            }
            Edit::Amount(which, value) => {
                let Some(entity) = self.environment.entity else {
                    return;
                };
                let spec = which.spec();
                let value = value.clamp(spec.min, spec.max);
                let result = match self.engine.as_mut() {
                    Some(engine) => write_amount(engine, entity, which, value),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_environment_edit(
                    result,
                    format!("Environment {}", spec.label),
                    Some(spec.key),
                );
            }
            Edit::Quality(index) => {
                let Some(entity) = self.environment.entity else {
                    return;
                };
                let result = match self.engine.as_mut() {
                    Some(engine) => write_quality(engine, entity, index),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_environment_edit(
                    result,
                    "Environment cloud quality".to_owned(),
                    Some("quality"),
                );
            }
            Edit::Tint(which, axis, value) => {
                let Some(entity) = self.environment.entity else {
                    return;
                };
                let mut colour = self
                    .environment
                    .settings
                    .as_ref()
                    .map(|settings| settings.tint(which))
                    .unwrap_or_else(|| which.default());
                colour[axis.min(2)] = value.clamp(0.0, 1.0);
                let result = match self.engine.as_mut() {
                    Some(engine) => write_tint(engine, entity, which, colour),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_environment_edit(
                    result,
                    format!("Environment {}", which.label()),
                    Some(which.key()),
                );
            }
            Edit::Colour(which, value) => {
                let Some(entity) = self.environment.entity else {
                    return;
                };
                let colour = value.map(|channel| channel.clamp(0.0, 1.0));
                let result = match self.engine.as_mut() {
                    Some(engine) => write_tint(engine, entity, which, colour),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_environment_edit(
                    result,
                    format!("Environment {}", which.label()),
                    Some(which.key()),
                );
            }
            Edit::Preset(index) => {
                let presets = panel::presets();
                let Some(preset) = presets.get(index) else {
                    return;
                };
                let Some(entity) = self.environment.entity else {
                    return;
                };
                // A preset is a look: the weather's sound stays as the user left it.
                let settings = match self.environment.settings.as_ref() {
                    Some(current) => preset.settings.clone().keep_audio_of(current),
                    None => preset.settings.clone(),
                };
                let result = match self.engine.as_mut() {
                    Some(engine) => write_settings(engine, entity, &settings),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_environment_edit(
                    result,
                    format!("Environment preset {}", preset.name),
                    None,
                );
            }
        }
    }

    fn finish_environment_edit(
        &mut self,
        result: Result<(), String>,
        name: String,
        key: Option<&str>,
    ) {
        match result {
            Ok(()) => {
                self.refresh_environment();
                self.refresh_components();
                // Sliders send a stream of values: one undo step per field, not per value.
                self.commit(name, key.map(|key| format!("environment:{key}")));
            }
            Err(error) => {
                self.environment.status =
                    Some(format!("Could not change the environment: {error}"));
                self.log(LogLevel::Error, format!("Environment: {error}"));
                self.refresh_environment();
            }
        }
    }

    /// Adds both components on a new entity (when the scene has none), starting from Clear sky.
    fn add_environment(&mut self) {
        if self.environment.entity.is_some() {
            return;
        }
        let selected = self.selected;
        self.add_entity_named("Environment".to_owned());
        let Some(entity) = self.entities.last().map(|entity| entity.id) else {
            return;
        };
        let clear_sky = panel::presets()
            .into_iter()
            .find(|preset| preset.name == "Clear sky")
            .map(|preset| preset.settings)
            .unwrap_or_default();
        let result = match self.engine.as_mut() {
            Some(engine) => write_settings(engine, entity, &clear_sky),
            None => Err("The engine is not available".to_owned()),
        };
        // The new entity is a tool, not a selection the user made.
        self.selected = selected.min(self.entities.len().saturating_sub(1));
        self.refresh_components();
        self.finish_environment_edit(result, "Add environment".to_owned(), None);
    }

    /// The palette's Environment entry: selects the scene's environment, creating it first when it has none.
    pub(super) fn create_environment_prefab(&mut self) {
        if self.environment.entity.is_none() {
            self.add_environment();
        }
        let Some(id) = self.environment.entity else {
            return;
        };
        if let Some(position) = self.entities.iter().position(|entity| entity.id == id) {
            self.selected = position;
            self.name_input = self.entities[position].name.clone();
            self.refresh_components();
        }
        self.show_environment_pane();
    }

    /// Brings the Environment tab to the front (it joins the Inspector's pane when it is not open).
    pub(super) fn show_environment_pane(&mut self) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::Environment)
            {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Inspector) {
                    group.pages.push(PaneKind::Environment);
                    group.active = group.pages.len() - 1;
                    break;
                }
            }
        }
        self.refresh_environment();
    }
}

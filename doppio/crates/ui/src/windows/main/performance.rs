//! Editor side of the **Performance** pane: the scene's render features and the editor viewport's own settings.
//!
//! The scene settings are one reflected component, `Forward Render Features`. This module finds the entity that carries
//! it, reads it into the panel's typed [`Settings`], writes every edit through the engine's reflected component API
//! (so the Inspector, scripts and the panel never disagree) and records an undo step per edit. The viewport settings
//! (render scale, anti-aliasing, shadows, grid) are the editor's, not the scene's: they go to the engine at once and
//! leave no undo step.

use super::{add_component, component_type_id, set_component_field, App, LogLevel, PaneKind};
use crate::uikit::panels::performance::{
    Amount, Edit, Flag, Settings, ANTI_ALIASING, COMPONENT, RENDER_SCALES,
};
use doppio_engine::{FieldKind, FieldValue};

#[derive(Default)]
pub(super) struct PerformanceState {
    /// The entity that carries the component, when the scene has it.
    pub entity: Option<u32>,
    pub settings: Option<Settings>,
    pub status: Option<String>,
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
                let number = match value {
                    FieldValue::F32(v) => Some(*v),
                    FieldValue::U32(v) => Some(*v as f32),
                    FieldValue::I32(v) => Some(*v as f32),
                    _ => None,
                };
                if let Some(number) = number {
                    settings.amounts[amount.index()] = number;
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

/// Writes a number as the kind the component's field has (some are whole numbers).
fn write_amount(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    amount: Amount,
    value: f32,
) -> Result<(), String> {
    let type_id = ensure_component(engine, entity)?;
    let spec = amount.spec();
    let value = value.clamp(spec.min, spec.max);
    let kind = engine
        .component_types()
        .into_iter()
        .find(|kind| kind.id == type_id)
        .and_then(|kind| {
            kind.fields
                .into_iter()
                .find(|field| field.name == spec.field)
        })
        .map(|field| field.kind);
    let written = match kind {
        Some(FieldKind::U32) => FieldValue::U32(value.round().max(0.0) as u32),
        Some(FieldKind::I32) => FieldValue::I32(value.round() as i32),
        _ if spec.integer => FieldValue::U32(value.round().max(0.0) as u32),
        _ => FieldValue::F32(value),
    };
    set_component_field(engine, entity, type_id, spec.field, written)
}

impl App {
    /// Reads the scene's render features component again (after an edit, an undo, a scene load).
    pub(super) fn refresh_performance(&mut self) {
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
                self.performance.entity = Some(entity);
                self.performance.settings = Some(settings);
            }
            None => {
                self.performance.entity = None;
                self.performance.settings = None;
            }
        }
    }

    pub(super) fn performance_entity_name(&self) -> Option<&str> {
        let entity = self.performance.entity?;
        self.entities
            .iter()
            .find(|candidate| candidate.id == entity)
            .map(|candidate| candidate.name.as_str())
    }

    pub(super) fn edit_performance(&mut self, edit: Edit) {
        self.performance.status = None;
        match edit {
            Edit::Add => self.add_performance(),
            Edit::Flag(flag, on) => {
                let Some(entity) = self.performance.entity else {
                    return;
                };
                let result = match self.engine.as_mut() {
                    Some(engine) => write_flag(engine, entity, flag, on),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_performance_edit(
                    result,
                    format!("Render {}", flag.spec().label),
                    Some(flag.spec().key),
                );
            }
            Edit::Amount(amount, value) => {
                let Some(entity) = self.performance.entity else {
                    return;
                };
                let result = match self.engine.as_mut() {
                    Some(engine) => write_amount(engine, entity, amount, value),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_performance_edit(
                    result,
                    format!("Render {}", amount.spec().label),
                    Some(amount.spec().key),
                );
            }
            Edit::RenderScale(index) => {
                self.viewport_settings.render_scale =
                    RENDER_SCALES[index.min(RENDER_SCALES.len() - 1)];
                self.apply_viewport_settings();
            }
            Edit::AntiAliasing(index) => {
                self.viewport_settings.anti_aliasing = match index.min(ANTI_ALIASING.len() - 1) {
                    0 => doppio_engine::AntiAliasing::Off,
                    1 => doppio_engine::AntiAliasing::Fxaa,
                    _ => doppio_engine::AntiAliasing::Taa,
                };
                self.apply_viewport_settings();
            }
            Edit::Shadows(on) => {
                self.viewport_settings.shadows = on;
                self.apply_viewport_settings();
            }
            Edit::Grid(on) => {
                self.viewport_settings.show_grid = on;
                self.apply_viewport_settings();
            }
        }
    }

    fn finish_performance_edit(
        &mut self,
        result: Result<(), String>,
        name: String,
        key: Option<&str>,
    ) {
        match result {
            Ok(()) => {
                self.refresh_performance();
                self.refresh_components();
                // Sliders send a stream of values: one undo step per field, not per value.
                self.commit(name, key.map(|key| format!("performance:{key}")));
            }
            Err(error) => {
                self.performance.status =
                    Some(format!("Could not change the render features: {error}"));
                self.log(LogLevel::Error, format!("Performance: {error}"));
                self.refresh_performance();
            }
        }
    }

    /// Adds the component on a new entity (when the scene has none).
    fn add_performance(&mut self) {
        if self.performance.entity.is_some() {
            return;
        }
        let selected = self.selected;
        self.add_entity_named("Render Features".to_owned());
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
        self.finish_performance_edit(result, "Add render features".to_owned(), None);
    }

    pub(super) fn show_performance_pane(&mut self) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::Performance)
            {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Inspector) {
                    group.pages.push(PaneKind::Performance);
                    group.active = group.pages.len() - 1;
                    break;
                }
            }
        }
        self.refresh_performance();
    }
}

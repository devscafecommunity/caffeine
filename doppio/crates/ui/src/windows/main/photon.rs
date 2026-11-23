//! Editor state and message handling of the **Photon** pane (global illumination).
//!
//! Photon is one `Photon GI` component in the scene (the renderer reads the first it finds). This module
//! finds it, reads its fields into the panel's typed [`Settings`], applies every edit through the engine's
//! reflected component API (so the Inspector and the panel never disagree) and records an undo step per
//! edit. The voxel statistics (C ABI v17) are read live by the view; *Rebuild voxels* asks the engine to
//! voxelise the scene again.

use super::{
    add_component, component_type_id, enum_field, set_component_field, App, LogLevel, PaneKind,
};
use crate::uikit::panels::photon::{self as panel, Amount, Choice, Edit, Flag, Settings};
use doppio_engine::FieldValue;

/// The component's name in the engine's reflection table.
pub(super) const COMPONENT: &str = "Photon GI";

#[derive(Default)]
pub(super) struct PhotonState {
    /// The entity that carries the component, when the scene has one.
    pub entity: Option<u32>,
    pub settings: Option<Settings>,
    pub status: Option<String>,
}

/// The first entity of `ids` that carries a Photon GI component.
fn find_entity(engine: &doppio_engine::Engine, ids: impl Iterator<Item = u32>) -> Option<u32> {
    ids.into_iter().find(|id| {
        engine
            .entity_components(*id)
            .iter()
            .any(|component| component.name == COMPONENT)
    })
}

/// Reads the component of `entity` into the panel's settings; a field the engine library does not have
/// keeps its default.
pub(super) fn read_settings(engine: &doppio_engine::Engine, entity: u32) -> Option<Settings> {
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
    let get = |name: &str| {
        names
            .iter()
            .position(|field| field == name)
            .and_then(|i| values.get(i))
    };
    let flag = |name: &str, fallback: bool| match get(name) {
        Some(FieldValue::Bool(on)) => *on,
        _ => fallback,
    };
    let number = |name: &str, fallback: f32| match get(name) {
        Some(FieldValue::F32(v)) => *v,
        _ => fallback,
    };
    let choice = |name: &str, fallback: usize| match get(name) {
        Some(FieldValue::Enum(i)) => (*i).max(0) as usize,
        _ => fallback,
    };
    let d = Settings::default();
    Some(Settings {
        enabled: flag("Enabled", d.enabled),
        path: choice("Path", d.path),
        quality: choice("Quality", d.quality),
        intensity: number("Intensity", d.intensity),
        radius: number("Radius", d.radius),
        sky: number("Sky intensity", d.sky),
        ambient: number("Ambient", d.ambient),
        debug_view: choice("Debug view", d.debug_view),
        bounce_gain: number("Bounce gain", d.bounce_gain),
        voxel_skinned: flag("Voxel skinned meshes", d.voxel_skinned),
        voxel_cutout: flag("Voxel cut-out meshes", d.voxel_cutout),
        voxel_terrain: flag("Voxel terrain", d.voxel_terrain),
        full_rebuilds: flag("Full rebuilds", d.full_rebuilds),
    })
}

/// Writes every field of `settings` into the component of `entity`.
fn write_settings(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    type_id: u32,
    settings: &Settings,
) -> Result<(), String> {
    for (which, value) in [
        (Flag::Enabled, settings.enabled),
        (Flag::VoxelSkinned, settings.voxel_skinned),
        (Flag::VoxelCutout, settings.voxel_cutout),
        (Flag::VoxelTerrain, settings.voxel_terrain),
        (Flag::FullRebuilds, settings.full_rebuilds),
    ] {
        set_component_field(
            engine,
            entity,
            type_id,
            which.field(),
            FieldValue::Bool(value),
        )?;
    }
    for (which, value) in [
        (Amount::Intensity, settings.intensity),
        (Amount::BounceGain, settings.bounce_gain),
        (Amount::Sky, settings.sky),
        (Amount::Ambient, settings.ambient),
        (Amount::Radius, settings.radius),
    ] {
        set_component_field(
            engine,
            entity,
            type_id,
            which.field(),
            FieldValue::F32(value),
        )?;
    }
    for (which, index) in [
        (Choice::Path, settings.path),
        (Choice::Quality, settings.quality),
        (Choice::DebugView, settings.debug_view),
    ] {
        write_choice(engine, entity, type_id, which, index)?;
    }
    Ok(())
}

fn write_choice(
    engine: &mut doppio_engine::Engine,
    entity: u32,
    type_id: u32,
    which: Choice,
    index: usize,
) -> Result<(), String> {
    let option = which
        .options()
        .get(index)
        .ok_or_else(|| format!("{} has no option {index}", which.field()))?;
    let value = enum_field(engine, COMPONENT, which.field(), option)?;
    set_component_field(engine, entity, type_id, which.field(), value)
}

impl App {
    /// Reads the scene's Photon component again (after an edit, an undo, a scene load).
    pub(super) fn refresh_photon(&mut self) {
        let ids = self.entities.iter().map(|entity| entity.id);
        let found = self.engine.as_ref().and_then(|engine| {
            let entity = find_entity(engine, ids)?;
            Some((entity, read_settings(engine, entity)?))
        });
        match found {
            Some((entity, settings)) => {
                self.photon.entity = Some(entity);
                self.photon.settings = Some(settings);
            }
            None => {
                self.photon.entity = None;
                self.photon.settings = None;
            }
        }
    }

    /// The voxel statistics of the latest frame, read live (cheap: no GPU read-back).
    pub(super) fn photon_stats(&self) -> Option<doppio_engine::PhotonStats> {
        self.engine.as_ref()?.photon_stats().ok()
    }

    /// Name of the entity that carries the component.
    pub(super) fn photon_entity_name(&self) -> Option<&str> {
        let id = self.photon.entity?;
        self.entities
            .iter()
            .find(|entity| entity.id == id)
            .map(|entity| entity.name.as_str())
    }

    pub(super) fn edit_photon(&mut self, edit: Edit) {
        self.photon.status = None;
        match edit {
            Edit::Add => self.add_photon(),
            Edit::Flag(which, on) => self.set_photon_field(which.field(), FieldValue::Bool(on)),
            Edit::Amount(which, value) => {
                let range = which.range();
                let value = value.clamp(*range.start(), *range.end());
                self.set_photon_field(which.field(), FieldValue::F32(value));
            }
            Edit::Choice(which, index) => {
                let Some((entity, type_id)) = self.photon_target() else {
                    return;
                };
                let result = match self.engine.as_mut() {
                    Some(engine) => write_choice(engine, entity, type_id, which, index),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_photon_edit(
                    result,
                    format!("Photon {}", which.field()),
                    Some(which.field()),
                );
            }
            Edit::Preset(index) => {
                let presets = panel::presets();
                let Some(preset) = presets.get(index) else {
                    return;
                };
                let Some((entity, type_id)) = self.photon_target() else {
                    return;
                };
                let result = match self.engine.as_mut() {
                    Some(engine) => write_settings(engine, entity, type_id, &preset.settings),
                    None => Err("The engine is not available".to_owned()),
                };
                self.finish_photon_edit(result, format!("Photon preset {}", preset.name), None);
            }
            Edit::Rebuild => {
                let result = match self.engine.as_mut() {
                    Some(engine) => engine
                        .photon_rebuild()
                        .map_err(|error| format!("{error:?}")),
                    None => Err("The engine is not available".to_owned()),
                };
                match result {
                    Ok(()) => {
                        self.photon.status =
                            Some("Voxel volume rebuilds on the next frame".to_owned());
                        self.log(LogLevel::Action, "Photon: rebuilt the voxel volume");
                    }
                    Err(error) => {
                        self.photon.status = Some(format!("Could not rebuild the voxels: {error}"));
                    }
                }
            }
        }
    }

    /// The entity and component type an edit writes to.
    fn photon_target(&mut self) -> Option<(u32, u32)> {
        let entity = self.photon.entity?;
        let type_id = self
            .engine
            .as_ref()
            .and_then(|engine| component_type_id(engine, COMPONENT))?;
        Some((entity, type_id))
    }

    /// Sets a bool or f32 field, then records the undo step.
    fn set_photon_field(&mut self, field: &str, value: FieldValue) {
        let Some((entity, type_id)) = self.photon_target() else {
            return;
        };
        let result = match self.engine.as_mut() {
            Some(engine) => set_component_field(engine, entity, type_id, field, value),
            None => Err("The engine is not available".to_owned()),
        };
        self.finish_photon_edit(result, format!("Photon {field}"), Some(field));
    }

    fn finish_photon_edit(&mut self, result: Result<(), String>, name: String, key: Option<&str>) {
        match result {
            Ok(()) => {
                self.refresh_photon();
                // Sliders send a stream of values: one undo step per field, not per value.
                self.commit(name, key.map(|key| format!("photon:{key}")));
            }
            Err(error) => {
                self.photon.status = Some(format!("Could not change Photon: {error}"));
                self.log(LogLevel::Error, format!("Photon: {error}"));
                self.refresh_photon();
            }
        }
    }

    /// Adds the component on a new entity (when the scene has none), on, on the compute path.
    fn add_photon(&mut self) {
        if self.photon.entity.is_some() {
            return;
        }
        let selected = self.selected;
        self.add_entity_named("Photon GI".to_owned());
        let Some(entity) = self.entities.last().map(|entity| entity.id) else {
            return;
        };
        let result = match self.engine.as_mut() {
            Some(engine) => add_component(engine, entity, COMPONENT).and_then(|type_id| {
                write_settings(engine, entity, type_id, &Settings::recommended())
            }),
            None => Err("The engine is not available".to_owned()),
        };
        // The new entity is a tool, not a selection the user made.
        self.selected = selected.min(self.entities.len().saturating_sub(1));
        self.refresh_components();
        self.finish_photon_edit(result, "Add Photon GI".to_owned(), None);
    }

    /// Brings the Photon tab to the front (it joins the Inspector's pane when it is not open).
    pub(super) fn show_photon_pane(&mut self) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::Photon)
            {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Inspector) {
                    group.pages.push(PaneKind::Photon);
                    group.active = group.pages.len() - 1;
                    break;
                }
            }
        }
        self.refresh_photon();
    }
}

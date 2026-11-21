//! Saving an entity tree as a `.prefab` and placing one again (the file format is `uikit::prefab_file`).
//!
//! The tree is read and rebuilt through the engine's reflection, so everything the Inspector shows travels:
//! the Nature generator writes a prefab next to every tree it bakes, and double-clicking the file in the Assets pane
//! drops a fresh copy of it at the camera focus.

use super::{App, LogLevel};
use crate::uikit::prefab_file::{self, Component, Entity, Field, Prefab, Value};
use std::path::Path;

impl App {
    /// The tree under `root` (the root first, each parent before its children) as a prefab. The root is saved at the
    /// origin: where the copy lands is decided when it is placed.
    pub(super) fn capture_prefab(
        &self,
        root: u32,
        name: &str,
        generator: &str,
    ) -> Result<Prefab, String> {
        let engine = self.engine.as_ref().ok_or("The engine is not available")?;
        let count = engine.entity_count();
        let mut parents = Vec::new();
        for index in 0..count {
            if let Ok(info) = engine.entity_at(index) {
                parents.push((info.id, engine.entity_parent(info.id).ok().flatten()));
            }
        }
        let mut order = vec![root];
        let mut next = 0;
        while next < order.len() {
            let current = order[next];
            next += 1;
            for (id, parent) in &parents {
                if *parent == Some(current) && !order.contains(id) {
                    order.push(*id);
                }
            }
        }
        let types = engine.component_types();
        let mut entities = Vec::with_capacity(order.len());
        for (position, id) in order.iter().enumerate() {
            let parent = if position == 0 {
                None
            } else {
                let owner = parents
                    .iter()
                    .find(|(entity, _)| entity == id)
                    .and_then(|e| e.1);
                owner.and_then(|owner| order.iter().position(|candidate| *candidate == owner))
            };
            let mut components = Vec::new();
            for data in engine.entity_component_data(*id) {
                let Some(kind) = types.iter().find(|kind| kind.id == data.info.type_id) else {
                    continue;
                };
                let mut fields: Vec<Field> = kind
                    .fields
                    .iter()
                    .zip(data.values.iter())
                    .map(|(info, value)| Field {
                        name: info.name.clone(),
                        value: Value::from(value),
                    })
                    .collect();
                if position == 0 && kind.name == "Transform" {
                    for field in &mut fields {
                        if field.name == "Position" {
                            field.value = Value::Vec3([0.0; 3]);
                        }
                    }
                }
                components.push(Component {
                    name: kind.name.clone(),
                    fields,
                });
            }
            entities.push(Entity {
                name: engine.entity_name(*id).map_err(|e| format!("{e:?}"))?,
                parent,
                components,
            });
        }
        Ok(Prefab::new(name, generator, entities))
    }

    /// Creates the prefab's entities in the open scene with its root at `at`; returns the new root.
    pub(super) fn instantiate_prefab(
        &mut self,
        prefab: &Prefab,
        at: [f32; 3],
    ) -> Result<u32, String> {
        let engine = self.engine.as_mut().ok_or("The engine is not available")?;
        let types = engine.component_types();
        let mut created: Vec<u32> = Vec::with_capacity(prefab.entities.len());
        let mut problems = Vec::new();
        for (index, entity) in prefab.entities.iter().enumerate() {
            let id = engine
                .create_entity(&entity.name)
                .map_err(|error| format!("{}: {error:?}", entity.name))?;
            if let Some(parent) = entity.parent.and_then(|parent| created.get(parent)) {
                let _ = engine.set_entity_parent(id, Some(*parent));
            }
            created.push(id);
            for component in &entity.components {
                let Some(kind) = types.iter().find(|kind| kind.name == component.name) else {
                    problems.push(format!(
                        "{}: unknown component {}",
                        entity.name, component.name
                    ));
                    continue;
                };
                if !engine
                    .entity_components(id)
                    .iter()
                    .any(|present| present.type_id == kind.id)
                {
                    if let Err(error) = engine.add_component(id, kind.id) {
                        problems.push(format!("{}: {}: {error:?}", entity.name, component.name));
                        continue;
                    }
                }
                let Some(slot) = engine
                    .entity_components(id)
                    .iter()
                    .position(|present| present.type_id == kind.id)
                else {
                    continue;
                };
                for field in &component.fields {
                    let Some(field_index) =
                        kind.fields.iter().position(|info| info.name == field.name)
                    else {
                        continue;
                    };
                    let value = match (index, component.name.as_str(), field.name.as_str()) {
                        (0, "Transform", "Position") => Value::Vec3(at),
                        _ => field.value.clone(),
                    };
                    if let Err(error) = engine.set_component_field(
                        id,
                        slot as u32,
                        field_index as u32,
                        &value.to_engine(),
                    ) {
                        problems.push(format!(
                            "{}: {}.{}: {error:?}",
                            entity.name, component.name, field.name
                        ));
                    }
                }
            }
        }
        let root = *created.first().ok_or("the prefab is empty")?;
        if problems.is_empty() {
            Ok(root)
        } else {
            self.log(
                LogLevel::Log,
                format!(
                    "Prefab {} placed with problems: {}",
                    prefab.name,
                    problems.join("; ")
                ),
            );
            Ok(root)
        }
    }

    /// Reads a `.prefab` and places it at the camera focus, selecting the new root. Undoable.
    pub(super) fn place_prefab_file(&mut self, path: &Path) {
        let prefab = match prefab_file::read(path) {
            Ok(prefab) => prefab,
            Err(error) => {
                self.log(LogLevel::Error, format!("Prefab: {error}"));
                return;
            }
        };
        let focus = self.camera.focus;
        match self.instantiate_prefab(&prefab, [focus[0], 0.0, focus[2]]) {
            Ok(root) => {
                if let Some(engine) = self.engine.as_ref() {
                    self.entities = super::projectloader::read_entities(engine);
                }
                if let Some(index) = self.entities.iter().position(|entity| entity.id == root) {
                    self.select_index(index);
                }
                self.refresh_components();
                self.commit(format!("Place prefab {}", prefab.name), None);
                self.log(LogLevel::Action, format!("Placed prefab {}", prefab.name));
            }
            Err(error) => self.log(LogLevel::Error, format!("Prefab {}: {error}", prefab.name)),
        }
    }

    /// Reopens the graph behind an asset in its generator. `path` may be a recipe, a prefab (it names its recipe), or
    /// anything a generator baked (a model, a material, a texture).
    pub(super) fn open_generator_for(&mut self, path: &Path) {
        let recipe = if prefab_file::is_prefab(path) {
            match prefab_file::read(path) {
                Ok(prefab) if !prefab.generator.is_empty() => self
                    .asset_root_dir()
                    .map(|root| root.join(&prefab.generator)),
                Ok(prefab) => {
                    self.log(
                        LogLevel::Log,
                        format!("Prefab {} was not made by a generator", prefab.name),
                    );
                    return;
                }
                Err(error) => {
                    self.log(LogLevel::Error, format!("Prefab: {error}"));
                    return;
                }
            }
        } else if crate::uikit::recipe::is_recipe(path) {
            Some(path.to_path_buf())
        } else {
            crate::uikit::recipe::recipe_for_product(path)
        };
        match recipe.filter(|recipe| recipe.is_file()) {
            Some(recipe) => self.open_recipe_file(&recipe),
            None => self.log(
                LogLevel::Log,
                format!(
                    "{} has no generator recipe: it was imported",
                    path.display()
                ),
            ),
        }
    }
}

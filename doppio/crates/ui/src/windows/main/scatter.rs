//! The **Scatter** action: places a scatter plan (`uikit::scatter`) on a terrain.
//!
//! The engine decides where (`caffeine_session_scatter_terrain`, C ABI v16: deterministic, by the terrain's height,
//! normal and masks and the plan's biome table); this module makes one entity per instance under a holder entity,
//! as one undo step. Same seed, same terrain, same plan: the same forest.

use super::{App, LogLevel};
use crate::uikit::scatter::{PlanClass, ScatterPlan, Shape, DEFAULT_PLAN};
use std::collections::BTreeMap;

/// Entities per action: each is a dozen engine calls, and past a few thousand the hierarchy is the problem.
pub(super) const DEFAULT_MAX_INSTANCES: u32 = 5000;

/// What to scatter and where.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct ScatterRequest {
    /// Plan text. When absent, the plan file below is read.
    pub plan_text: Option<String>,
    /// Project-relative plan file (`Scatter/default.scatter` when absent; written from the built-in plan if missing).
    pub plan_file: Option<String>,
    /// Overrides the plan's `seed`.
    pub seed: Option<u64>,
    pub max_instances: Option<u32>,
    /// The terrain entity; the selected entity when absent.
    pub terrain: Option<u32>,
}

impl App {
    pub(super) fn scatter_on_terrain(&mut self, request: ScatterRequest) {
        match self.run_scatter(&request) {
            Ok(summary) => {
                self.log(LogLevel::Action, summary.clone());
                self.scatter_status = Some(summary);
            }
            Err(error) => {
                self.log(LogLevel::Error, format!("Scatter: {error}"));
                self.scatter_status = Some(format!("Scatter: {error}"));
            }
        }
    }

    fn run_scatter(&mut self, request: &ScatterRequest) -> Result<String, String> {
        // The terrain: the one named, or the selection.
        let terrain_id = request
            .terrain
            .or_else(|| self.entities.get(self.selected).map(|e| e.id))
            .ok_or("select a terrain first")?;
        let plan_name;
        let text = match (&request.plan_text, self.asset_root_dir()) {
            (Some(text), _) => {
                plan_name = "plan".to_owned();
                text.clone()
            }
            (None, Some(root)) => {
                let relative = request
                    .plan_file
                    .clone()
                    .unwrap_or_else(|| "Scatter/default.scatter".to_owned());
                let file = root.join(&relative);
                if !file.exists() && request.plan_file.is_none() {
                    std::fs::create_dir_all(file.parent().unwrap_or(&root))
                        .map_err(|e| format!("could not create the Scatter folder: {e}"))?;
                    std::fs::write(&file, DEFAULT_PLAN)
                        .map_err(|e| format!("could not write {relative}: {e}"))?;
                }
                plan_name = file
                    .file_stem()
                    .map_or_else(|| "plan".to_owned(), |s| s.to_string_lossy().into_owned());
                std::fs::read_to_string(&file)
                    .map_err(|e| format!("could not read {relative}: {e}"))?
            }
            (None, None) => {
                plan_name = "default".to_owned();
                DEFAULT_PLAN.to_owned()
            }
        };
        let plan = ScatterPlan::from_text(&text)?;
        let seed = request.seed.unwrap_or(plan.seed);
        let max = request.max_instances.unwrap_or(DEFAULT_MAX_INSTANCES);
        let engine = self.engine.as_mut().ok_or("the engine is not available")?;
        engine
            .terrain(terrain_id)
            .map_err(|_| "the selected entity is not a terrain".to_owned())?;
        engine.biome_check(&plan.biome_text())?;
        let result = engine
            .scatter_terrain(
                terrain_id,
                seed,
                &plan.engine_classes(),
                &plan.biome_text(),
                None,
                max,
            )
            .map_err(|error| {
                format!("the engine could not scatter ({error:?}): it needs C ABI v16")
            })?;

        let holder = engine
            .create_entity(&format!("Scatter {plan_name}"))
            .map_err(|e| format!("could not create the holder: {e:?}"))?;
        let mut made: BTreeMap<u32, u32> = BTreeMap::new();
        let mut problems: Vec<String> = Vec::new();
        for (index, instance) in result.instances.iter().enumerate() {
            let Some(class) = plan.class(instance.class_id) else {
                continue;
            };
            match place_instance(engine, holder, class, instance, index) {
                Ok(()) => *made.entry(instance.class_id).or_default() += 1,
                Err(error) => {
                    if problems.len() < 3 {
                        problems.push(error);
                    }
                }
            }
        }
        self.entities = super::projectloader::read_entities(self.engine.as_ref().expect("engine"));
        if let Some(position) = self.entities.iter().position(|e| e.id == holder) {
            self.select_index(position);
        }
        self.refresh_components();
        self.commit("Scatter on terrain", None);

        let counts: Vec<String> = made
            .iter()
            .filter_map(|(id, n)| plan.class(*id).map(|c| format!("{n} {}", c.name)))
            .collect();
        let placed: u32 = made.values().sum();
        let mut summary = format!(
            "Scattered {placed} instances ({}), seed {seed}",
            counts.join(", ")
        );
        if result.total > max {
            summary.push_str(&format!(
                "; the terrain holds {} in all, only the first {max} were placed (raise max_instances or thin the plan)",
                result.total
            ));
        }
        if !problems.is_empty() {
            summary.push_str(&format!("; problems: {}", problems.join(" | ")));
        }
        Ok(summary)
    }
}

/// One instance: an entity under the holder with its shape, material, tint and transform.
fn place_instance(
    engine: &mut doppio_engine::Engine,
    holder: u32,
    class: &PlanClass,
    instance: &doppio_engine::ScatterInstance,
    index: usize,
) -> Result<(), String> {
    let id = engine
        .create_entity(&format!("{} {index}", class.name))
        .map_err(|e| format!("create: {e:?}"))?;
    engine
        .set_entity_parent(id, Some(holder))
        .map_err(|e| format!("parent: {e:?}"))?;
    engine
        .set_position(id, instance.position)
        .map_err(|e| format!("position: {e:?}"))?;
    engine
        .set_rotation(id, instance.rotation_degrees)
        .map_err(|e| format!("rotation: {e:?}"))?;
    let s = instance.scale;
    engine
        .set_scale(
            id,
            [class.size[0] * s, class.size[1] * s, class.size[2] * s],
        )
        .map_err(|e| format!("scale: {e:?}"))?;

    let filter = super::add_component(engine, id, "Mesh Filter")?;
    match &class.shape {
        Shape::Primitive(name) => {
            let value = super::enum_field(engine, "Mesh Filter", "Primitive", name)?;
            super::set_component_field(engine, id, filter, "Primitive", value)?;
        }
        Shape::Mesh(path) => {
            let value = super::enum_field(engine, "Mesh Filter", "Primitive", "Custom")?;
            super::set_component_field(engine, id, filter, "Primitive", value)?;
            let path = path.replace("{v}", &instance.variant.to_string());
            super::set_component_field(
                engine,
                id,
                filter,
                "Mesh",
                doppio_engine::FieldValue::String(path),
            )?;
        }
    }
    let renderer = super::add_component(engine, id, "Mesh Renderer")?;
    if let Some(material) = &class.material {
        super::set_component_field(
            engine,
            id,
            renderer,
            "Material",
            doppio_engine::FieldValue::String(material.clone()),
        )?;
    }
    // The class colour times the instance's tint jitter.
    let tint = [
        class.color[0] * instance.tint[0],
        class.color[1] * instance.tint[1],
        class.color[2] * instance.tint[2],
        1.0,
    ];
    super::set_component_field(
        engine,
        id,
        renderer,
        "Tint",
        doppio_engine::FieldValue::Color(tint),
    )?;
    if class.micromesh {
        super::set_component_field(
            engine,
            id,
            renderer,
            "Micromesh",
            doppio_engine::FieldValue::Bool(true),
        )?;
    }
    Ok(())
}

//! Editor side of the **Terrain Vegetation** pane (terrain editing roadmap, phase 3): the species palette of a terrain, the
//! vegetation brush and the procedural source.
//!
//! Vegetation is terrain data, not entities: a palette of species (`doppio_engine::VegSpecies`) and two stacking sources,
//! the procedural one (every species with a density grows by itself, deterministic for the seed) and the manual one
//! (what the brush places). The palette is edited through the engine (`veg_species_apply`), every edit is one engine record
//! and one undo step (a slider drag folds into one), and the brush is a stroke on the same mouse path as sculpting and
//! painting (`terraineditor.rs`). Every change arrives as an [`Edit`] message, so an agent can do what the mouse can.

use super::{App, LogLevel, PaneKind};
use crate::uikit::asset_folders;
use crate::uikit::panels::terrain_paint::BrushAmount;
use crate::uikit::panels::terrain_vegetation::{Edit, RULE_MASKS};
use crate::uikit::vegetation_recipe;
use doppio_engine::{
    BrushPreset, VegAdapt, VegAdaptTarget, VegBrush, VegInfo, VegRule, VegSpecies, VegTool,
};
use std::time::{Duration, Instant};

/// The most rules the panel (and the C ABI's struct) shows on one species.
const MAX_RULES: usize = doppio_engine::VEG_RULES_MAX;

pub(super) struct State {
    pub active: bool,
    pub entity: Option<u32>,
    pub terrain_name: Option<String>,
    pub info: Option<VegInfo>,
    pub species: Vec<VegSpecies>,
    pub selected: usize,
    pub tool: VegTool,
    pub radius: f32,
    pub strength: f32,
    pub spacing: f32,
    pub per_100m2: f32,
    pub amount: f32,
    pub only: i32,
    pub respect_rules: bool,
    pub shared_shape: bool,
    pub seed: u64,
    /// The terrain's masks, for the adaptations.
    pub masks: Vec<String>,
    /// How many variants a bake from a recipe makes.
    pub recipe_variants: u32,
    last_recipe_check: Option<Instant>,
    /// The seed was typed or dragged and not applied yet: reading the engine must not put the old one back.
    seed_edited: bool,
    pub status: Option<String>,
    /// The species edit that may still absorb the next one with the same key: (key, record, history length, cursor).
    merge: Option<(String, u32, usize, usize)>,
}

impl Default for State {
    fn default() -> Self {
        State {
            active: false,
            entity: None,
            terrain_name: None,
            info: None,
            species: Vec::new(),
            selected: 0,
            tool: VegTool::Paint,
            radius: 10.0,
            strength: 0.5,
            spacing: 0.0,
            per_100m2: 2.0,
            amount: 0.2,
            only: -1,
            respect_rules: true,
            shared_shape: false,
            seed: 1,
            masks: Vec::new(),
            recipe_variants: 4,
            last_recipe_check: None,
            seed_edited: false,
            status: None,
            merge: None,
        }
    }
}

impl App {
    /// Reads the terrain's vegetation into the panel's state.
    pub(super) fn refresh_vegetation_state(&mut self) {
        let entity = self.paint_terrain();
        let Some(engine) = self.engine.as_ref() else {
            return;
        };
        let state = &mut self.terrain_vegetation;
        state.entity = entity;
        state.terrain_name = entity.and_then(|id| {
            self.entities
                .iter()
                .find(|e| e.id == id)
                .map(|e| e.name.clone())
        });
        state.info = entity.and_then(|id| engine.terrain_vegetation(id));
        state.masks = entity
            .and_then(|id| engine.terrain_masks(id).ok())
            .map(|masks| masks.into_iter().map(|m| m.name).collect())
            .unwrap_or_default();
        state.species = match (entity, state.info) {
            (Some(id), Some(info)) if info.enabled => engine.veg_species_list(id),
            _ => Vec::new(),
        };
        if let Some(info) = state.info.filter(|i| i.enabled) {
            if !state.seed_edited {
                state.seed = info.seed;
            }
        }
        let last = state.species.len().saturating_sub(1);
        state.selected = state.selected.min(last);
        if state.only >= state.species.len() as i32 {
            state.only = -1;
        }
    }

    pub(super) fn show_terrain_vegetation_pane(&mut self) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::TerrainVegetation)
            {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Inspector) {
                    group.pages.push(PaneKind::TerrainVegetation);
                    group.active = group.pages.len() - 1;
                    break;
                }
            }
        }
        self.ensure_terrain_library();
        self.refresh_vegetation_state();
    }

    fn set_vegetation_tool(&mut self, on: bool) {
        if on {
            self.ensure_terrain_library();
            // One tool at a time: sculpting, painting layers and planting all use the left button.
            if self.terrain_editor.active {
                self.set_terrain_tool(false);
            }
            if self.terrain_paint.active {
                self.terrain_paint.active = false;
            }
            self.terrain_path.active = false;
            self.texture_set_paint_mode = false;
            self.refresh_vegetation_state();
        } else {
            self.finish_terrain_stroke();
        }
        self.terrain_vegetation.active = on;
        let radius = self.terrain_vegetation.radius;
        self.set_terrain_brush_cursor(on, radius);
        self.log(
            LogLevel::Action,
            if on {
                "Terrain vegetation on"
            } else {
                "Terrain vegetation off"
            },
        );
    }

    /// Applies a changed palette as one undo step; an edit with the same `key` as the one just before it folds into it.
    fn apply_species(&mut self, species: &[VegSpecies], name: &str, key: Option<String>) -> bool {
        let Some(entity) = self.terrain_vegetation.entity else {
            return false;
        };
        let merge = key
            .as_ref()
            .and_then(|key| {
                self.terrain_vegetation
                    .merge
                    .as_ref()
                    .filter(|(k, _, length, cursor)| {
                        k == key
                            && *length == self.history.len()
                            && *cursor == self.history.cursor()
                    })
                    .map(|(_, record, _, _)| *record)
            })
            .unwrap_or(0);
        let Some(engine) = self.engine.as_mut() else {
            return false;
        };
        match engine.veg_species_apply(entity, species, merge) {
            Ok(0) => {
                self.refresh_vegetation_state();
                true
            }
            Ok(record) => {
                if record != merge {
                    self.commit_terrain_stroke(record, name.to_owned());
                }
                self.terrain_vegetation.merge =
                    key.map(|key| (key, record, self.history.len(), self.history.cursor()));
                self.terrain_vegetation.status = None;
                self.refresh_vegetation_state();
                true
            }
            Err(error) => {
                self.log(LogLevel::Error, format!("Vegetation: {error}"));
                self.terrain_vegetation.status = Some(format!("Vegetation: {error}"));
                false
            }
        }
    }

    /// An edit of the selected species.
    fn edit_selected_species(
        &mut self,
        name: &str,
        key: &str,
        change: impl FnOnce(&mut VegSpecies),
    ) {
        let index = self.terrain_vegetation.selected;
        let mut species = self.terrain_vegetation.species.clone();
        let Some(selected) = species.get_mut(index) else {
            return;
        };
        let before = selected.clone();
        change(selected);
        if *selected == before {
            return;
        }
        let uid = selected.uid;
        self.apply_species(&species, name, Some(format!("species{uid}.{key}")));
    }

    fn new_species(name: String) -> VegSpecies {
        VegSpecies {
            name,
            ..VegSpecies::default()
        }
    }

    /// The project path and name of the asset selected in the Assets panel.
    fn selected_asset_for_vegetation(&self) -> Option<(String, String)> {
        self.asset_selected
            .and_then(|index| self.assets.get(index))
            .map(|asset| {
                let stem = std::path::Path::new(&asset.name)
                    .file_stem()
                    .map_or(asset.name.clone(), |s| s.to_string_lossy().into_owned());
                (stem, asset.relative_path.clone())
            })
    }

    pub(super) fn edit_terrain_vegetation(&mut self, edit: Edit) {
        self.refresh_vegetation_state();
        match edit {
            Edit::Active(on) => self.set_vegetation_tool(on),
            Edit::Enable(on) => {
                let (Some(entity), Some(engine)) =
                    (self.terrain_vegetation.entity, self.engine.as_mut())
                else {
                    return;
                };
                match engine.terrain_vegetation_enable(entity, on) {
                    Ok(()) => {
                        self.commit(
                            if on {
                                "Turn on terrain vegetation"
                            } else {
                                "Turn off terrain vegetation"
                            },
                            None,
                        );
                        self.terrain_vegetation.status = None;
                    }
                    Err(error) => {
                        self.terrain_vegetation.status =
                            Some(format!("Could not change the vegetation: {error:?}"));
                    }
                }
                self.refresh_vegetation_state();
            }
            Edit::Select(index) => {
                if index < self.terrain_vegetation.species.len() {
                    self.terrain_vegetation.selected = index;
                }
            }
            Edit::AddSpecies => {
                let mut species = self.terrain_vegetation.species.clone();
                if species.len() >= doppio_engine::VEG_SPECIES_MAX {
                    self.terrain_vegetation.status =
                        Some("A terrain holds at most 256 species".to_owned());
                    return;
                }
                species.push(Self::new_species(format!("Species {}", species.len())));
                if self.apply_species(&species, "Add species", None) {
                    self.terrain_vegetation.selected = species.len() - 1;
                }
            }
            Edit::AddSpeciesWith {
                name,
                meshes,
                tint,
                density_per_ha,
                radius_m,
            } => {
                let mut species = self.terrain_vegetation.species.clone();
                if species.len() >= doppio_engine::VEG_SPECIES_MAX {
                    self.terrain_vegetation.status =
                        Some("A terrain holds at most 256 species".to_owned());
                    return;
                }
                let mut added = Self::new_species(name);
                added.meshes = meshes;
                added.tint = tint;
                added.density_per_ha = density_per_ha;
                added.radius_m = radius_m;
                species.push(added);
                if self.apply_species(&species, "Add species", None) {
                    self.terrain_vegetation.selected = species.len() - 1;
                }
            }
            Edit::AddSpeciesFromAsset => {
                let Some((name, path)) = self.selected_asset_for_vegetation() else {
                    self.terrain_vegetation.status =
                        Some("Select a mesh in the Assets panel first".to_owned());
                    return;
                };
                let mut species = self.terrain_vegetation.species.clone();
                if species.len() >= doppio_engine::VEG_SPECIES_MAX {
                    self.terrain_vegetation.status =
                        Some("A terrain holds at most 256 species".to_owned());
                    return;
                }
                let mut added = Self::new_species(name);
                added.meshes = vec![path];
                species.push(added);
                if self.apply_species(&species, "Add species", None) {
                    self.terrain_vegetation.selected = species.len() - 1;
                }
            }
            Edit::AddVariantFromAsset => {
                let Some((_, path)) = self.selected_asset_for_vegetation() else {
                    self.terrain_vegetation.status =
                        Some("Select a mesh in the Assets panel first".to_owned());
                    return;
                };
                self.edit_terrain_vegetation(Edit::AddVariant(path));
            }
            Edit::AddVariant(path) => {
                self.edit_selected_species("Add species variant", "variants", |s| {
                    if s.meshes.len() < doppio_engine::VEG_VARIANTS_MAX && !s.meshes.contains(&path)
                    {
                        s.meshes.push(path);
                    }
                });
            }
            Edit::RemoveVariant(index) => {
                self.edit_selected_species("Remove species variant", "variants", |s| {
                    if index < s.meshes.len() {
                        s.meshes.remove(index);
                    }
                });
            }
            Edit::RemoveSpecies => {
                let index = self.terrain_vegetation.selected;
                let mut species = self.terrain_vegetation.species.clone();
                if index >= species.len() {
                    return;
                }
                species.remove(index);
                self.apply_species(&species, "Remove species", None);
            }
            Edit::Name(name) => {
                self.edit_selected_species("Rename species", "name", |s| s.name = name)
            }
            Edit::Material(path) => {
                self.edit_selected_species("Set species material", "material", |s| {
                    s.material = path
                })
            }
            Edit::Number(number, value) => {
                self.edit_selected_species("Edit species", number.key(), |s| number.set(s, value))
            }
            Edit::Procedural(on) => {
                self.edit_selected_species("Edit species", "procedural", |s| s.procedural = on)
            }
            Edit::Visible(on) => {
                self.edit_selected_species("Edit species", "visible", |s| s.visible = on)
            }
            Edit::Recipe(path) => {
                self.edit_selected_species("Set species recipe", "recipe", |s| {
                    s.recipe = path;
                    s.recipe_hash = 0;
                });
                // A recipe that is there is baked at once.
                self.sync_vegetation_recipes(true);
            }
            Edit::RecipeVariants(count) => {
                self.terrain_vegetation.recipe_variants =
                    count.clamp(1, vegetation_recipe::MAX_VARIANTS);
            }
            Edit::BakeRecipe => {
                let index = self.terrain_vegetation.selected;
                let count = self.terrain_vegetation.recipe_variants;
                match self.bake_species_recipe(index, Some(count)) {
                    Ok(message) => {
                        self.log(LogLevel::Action, message.clone());
                        self.terrain_vegetation.status = Some(message);
                    }
                    Err(error) => {
                        self.log(LogLevel::Error, format!("Recipe: {error}"));
                        self.terrain_vegetation.status = Some(format!("Recipe: {error}"));
                    }
                }
            }
            Edit::SyncRecipes => self.sync_vegetation_recipes(true),
            Edit::AddAdapt { mask, tint } => {
                self.edit_selected_species("Add species adaptation", "adapt", |s| {
                    if s.adapt.len() < doppio_engine::VEG_ADAPT_MAX {
                        // Starts as no change at either end; the sliders do the rest.
                        s.adapt.push(VegAdapt {
                            mask,
                            target: if tint {
                                VegAdaptTarget::Tint
                            } else {
                                VegAdaptTarget::Scale
                            },
                            at0: [1.0; 3],
                            at1: [1.0; 3],
                        });
                    }
                });
            }
            Edit::AddAdaptWith {
                mask,
                tint,
                at0,
                at1,
            } => {
                self.edit_selected_species("Add species adaptation", "adapt", |s| {
                    if s.adapt.len() < doppio_engine::VEG_ADAPT_MAX {
                        s.adapt.push(VegAdapt {
                            mask,
                            target: if tint {
                                VegAdaptTarget::Tint
                            } else {
                                VegAdaptTarget::Scale
                            },
                            at0,
                            at1,
                        });
                    }
                });
            }
            Edit::RemoveAdapt(index) => {
                self.edit_selected_species("Remove species adaptation", "adapt", |s| {
                    if index < s.adapt.len() {
                        s.adapt.remove(index);
                    }
                });
            }
            Edit::AdaptValue {
                index,
                at1,
                channel,
                value,
            } => {
                self.edit_selected_species(
                    "Edit species adaptation",
                    &format!("adapt{index}.{}{channel}", u8::from(at1)),
                    |s| {
                        if let Some(term) = s.adapt.get_mut(index) {
                            let slot = if at1 { &mut term.at1 } else { &mut term.at0 };
                            if channel < 3 {
                                let max = if matches!(term.target, VegAdaptTarget::Scale) {
                                    4.0
                                } else {
                                    2.0
                                };
                                slot[channel] = value.clamp(0.05, max).max(
                                    if matches!(term.target, VegAdaptTarget::Scale) {
                                        0.05
                                    } else {
                                        0.0
                                    },
                                );
                            }
                        }
                    },
                );
            }
            Edit::AddRule(mask) => {
                if !RULE_MASKS.contains(&mask.as_str()) {
                    self.terrain_vegetation.status = Some(format!(
                        "No mask called {mask}: use slope, height or curvature"
                    ));
                    return;
                }
                self.edit_selected_species("Add species rule", "rules", |s| {
                    if s.rules.len() < MAX_RULES {
                        let (rise0, rise1, fall1, fall0) = match mask.as_str() {
                            "slope" => (0.0, 0.0, 0.3, 0.4),
                            "height" => (0.0, 0.0, 0.7, 0.8),
                            _ => (0.4, 0.5, 1.0, 1.0),
                        };
                        s.rules.push(VegRule {
                            mask,
                            rise0,
                            rise1,
                            fall1,
                            fall0,
                        });
                    }
                });
            }
            Edit::RemoveRule(index) => {
                self.edit_selected_species("Remove species rule", "rules", |s| {
                    if index < s.rules.len() {
                        s.rules.remove(index);
                    }
                });
            }
            Edit::Rule(index, corner, value) => {
                self.edit_selected_species(
                    "Edit species rule",
                    &format!("rule{index}.{}", corner.key()),
                    |s| {
                        if let Some(rule) = s.rules.get_mut(index) {
                            let mut corners = [rule.rise0, rule.rise1, rule.fall1, rule.fall0];
                            let at = corner.index();
                            corners[at] = value.clamp(0.0, 1.0);
                            // The four numbers never decrease: pull the others along.
                            for i in (at + 1)..4 {
                                corners[i] = corners[i].max(corners[at]);
                            }
                            for i in (0..at).rev() {
                                corners[i] = corners[i].min(corners[at]);
                            }
                            [rule.rise0, rule.rise1, rule.fall1, rule.fall0] = corners;
                        }
                    },
                );
            }
            Edit::Tool(tool) => self.terrain_vegetation.tool = tool,
            Edit::Brush(amount, value) => {
                let range = amount.range();
                let value = value.clamp(*range.start(), *range.end());
                match amount {
                    BrushAmount::Radius => {
                        self.terrain_vegetation.radius = value;
                        if self.terrain_vegetation.active {
                            self.set_terrain_brush_cursor(true, value);
                        }
                    }
                    BrushAmount::Strength => self.terrain_vegetation.strength = value,
                    BrushAmount::Spacing => self.terrain_vegetation.spacing = value,
                }
            }
            Edit::Per100(value) => self.terrain_vegetation.per_100m2 = value.clamp(0.0, 100.0),
            Edit::Amount(value) => self.terrain_vegetation.amount = value,
            Edit::Only(index) => {
                self.terrain_vegetation.only =
                    if index < self.terrain_vegetation.species.len() as i32 {
                        index
                    } else {
                        -1
                    };
            }
            Edit::Respect(on) => self.terrain_vegetation.respect_rules = on,
            Edit::Shared(on) => self.terrain_vegetation.shared_shape = on,
            Edit::Seed(seed) => {
                self.terrain_vegetation.seed = seed;
                self.terrain_vegetation.seed_edited = true;
            }
            Edit::Reseed => {
                let (Some(entity), Some(info)) =
                    (self.terrain_vegetation.entity, self.terrain_vegetation.info)
                else {
                    return;
                };
                let mut seed = self.terrain_vegetation.seed;
                if seed == info.seed {
                    seed += 1;
                }
                let Some(engine) = self.engine.as_mut() else {
                    return;
                };
                self.terrain_vegetation.seed_edited = false;
                match engine.veg_reseed(entity, seed) {
                    Ok(record) if record != 0 => {
                        self.commit_terrain_stroke(record, "Reseed vegetation".to_owned());
                        self.terrain_vegetation.status = None;
                    }
                    Ok(_) => {}
                    Err(error) => {
                        self.terrain_vegetation.status =
                            Some(format!("Could not reseed: {error:?}"));
                    }
                }
                self.refresh_vegetation_state();
            }
            Edit::Bake | Edit::Rederive => {
                let (Some(entity), index) = (
                    self.terrain_vegetation.entity,
                    self.terrain_vegetation.selected,
                ) else {
                    return;
                };
                if index >= self.terrain_vegetation.species.len() {
                    return;
                }
                let bake = matches!(edit, Edit::Bake);
                let Some(engine) = self.engine.as_mut() else {
                    return;
                };
                let outcome = if bake {
                    engine.veg_bake(entity, index as u32)
                } else {
                    engine.veg_rederive(entity, index as u32)
                };
                match outcome {
                    Ok(record) if record != 0 => {
                        self.commit_terrain_stroke(
                            record,
                            if bake {
                                "Bake vegetation"
                            } else {
                                "Re-derive vegetation"
                            }
                            .to_owned(),
                        );
                        self.terrain_vegetation.status = None;
                    }
                    Ok(_) => {}
                    Err(error) => {
                        self.terrain_vegetation.status =
                            Some(format!("Could not change the vegetation: {error:?}"));
                    }
                }
                self.refresh_vegetation_state();
            }
        }
    }

    /// The recipe file of a species: relative to the assets root, or absolute.
    fn species_recipe_path(&self, recipe: &str) -> Option<std::path::PathBuf> {
        if recipe.trim().is_empty() {
            return None;
        }
        let path = std::path::PathBuf::from(recipe.trim());
        if path.is_absolute() {
            return Some(path);
        }
        Some(self.asset_root_dir()?.join(path))
    }

    /// Bakes the variants of species `index` from its recipe (`count` of them, or as many as it has) and points the species
    /// at them, as one undo step. Every plant of the species follows. Returns what happened, for the status line.
    pub(super) fn bake_species_recipe(
        &mut self,
        index: usize,
        count: Option<u32>,
    ) -> Result<String, String> {
        let species = self
            .terrain_vegetation
            .species
            .get(index)
            .cloned()
            .ok_or("select a species first")?;
        let recipe_path = self
            .species_recipe_path(&species.recipe)
            .ok_or("the species has no recipe")?;
        if !recipe_path.is_file() {
            return Err(format!("{} does not exist", recipe_path.display()));
        }
        let root = self
            .asset_root_dir()
            .ok_or("open a project first: variants are baked into its assets folder")?;
        let cache = asset_folders::resolve(&root, asset_folders::AssetFolder::NatureModels)
            .join("Vegetation");
        let count = count.unwrap_or_else(|| (species.meshes.len() as u32).max(1));
        let baked = vegetation_recipe::bake_species(&recipe_path, species.uid, count, &cache)?;
        let project_path = |absolute: &std::path::Path| {
            let relative = asset_folders::relative(&root, absolute.parent().unwrap_or(absolute));
            let file = absolute
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_default();
            let relative = if relative.is_empty() {
                file
            } else {
                format!("{relative}/{file}")
            };
            self.engine
                .as_ref()
                .and_then(|engine| engine.asset_project_path(&relative).ok())
                .filter(|path| !path.is_empty())
                .unwrap_or(relative)
        };
        let meshes: Vec<String> = baked.meshes.iter().map(|m| project_path(m)).collect();
        let materials = self.nature_part_materials(&baked.model, &baked.parts);
        let mut species_list = self.terrain_vegetation.species.clone();
        let target = species_list.get_mut(index).ok_or("the species is gone")?;
        let unchanged = target.meshes == meshes
            && target.slot_materials == materials
            && target.recipe_hash == baked.hash;
        target.meshes = meshes;
        target.slot_materials = materials;
        target.recipe_hash = baked.hash;
        let name = target.name.clone();
        if unchanged {
            return Ok(format!("{name} is up to date with its recipe"));
        }
        if !self.apply_species(&species_list, "Bake species from recipe", None) {
            return Err(self
                .terrain_vegetation
                .status
                .clone()
                .unwrap_or_else(|| "the species could not be changed".to_owned()));
        }
        Ok(format!(
            "Baked {} variant{} of {name} from its recipe",
            baked.meshes.len(),
            if baked.meshes.len() == 1 { "" } else { "s" }
        ))
    }

    /// Bakes again every species whose recipe file changed since it was baked. With `force` it looks now; otherwise it
    /// looks at most every two seconds (it runs on the viewport's tick while the vegetation tab is the one in use).
    pub(super) fn sync_vegetation_recipes(&mut self, force: bool) {
        if !self.terrain_vegetation.info.is_some_and(|i| i.enabled) {
            return;
        }
        if !force
            && self
                .terrain_vegetation
                .last_recipe_check
                .is_some_and(|at| at.elapsed() < Duration::from_secs(2))
        {
            return;
        }
        self.terrain_vegetation.last_recipe_check = Some(Instant::now());
        let stale: Vec<usize> = (0..self.terrain_vegetation.species.len())
            .filter(|index| {
                let species = &self.terrain_vegetation.species[*index];
                self.species_recipe_path(&species.recipe)
                    .filter(|path| path.is_file())
                    .and_then(|path| vegetation_recipe::recipe_hash(&path).ok())
                    .is_some_and(|hash| hash != species.recipe_hash)
            })
            .collect();
        for index in stale {
            let count = self.terrain_vegetation.recipe_variants;
            let had = self.terrain_vegetation.species[index].meshes.len() as u32;
            // A species that already has variants keeps how many; a new recipe bakes the panel's number.
            let wanted = if had > 1 { had } else { count };
            match self.bake_species_recipe(index, Some(wanted)) {
                Ok(message) => {
                    self.log(LogLevel::Action, message.clone());
                    self.terrain_vegetation.status = Some(message);
                }
                Err(error) => {
                    self.log(LogLevel::Error, format!("Recipe: {error}"));
                    self.terrain_vegetation.status = Some(format!("Recipe: {error}"));
                }
            }
        }
    }

    /// The vegetation brush a stroke starts with: the panel's numbers, on the Terrain Brush's shape when asked for, with the
    /// held modifiers applied (Ctrl turns plant into erase and back).
    pub(super) fn vegetation_brush(&self) -> VegBrush {
        let state = &self.terrain_vegetation;
        let mut shape = if state.shared_shape {
            self.terrain_editor.working.clone()
        } else {
            BrushPreset::default()
        };
        shape.radius_m = state.radius;
        shape.strength = state.strength;
        shape.spacing_m = state.spacing;
        let mut tool = state.tool;
        if self.terrain_editor.invert {
            tool = match tool {
                VegTool::Paint => VegTool::Erase,
                VegTool::Erase => VegTool::Paint,
                other => other,
            };
        }
        VegBrush {
            shape,
            tool,
            species: state.selected as u32,
            only_species: state.only,
            per_100m2: state.per_100m2,
            amount: state.amount,
            tint: state
                .species
                .get(state.selected)
                .map_or([1.0; 3], |s| s.tint),
            respect_rules: state.respect_rules,
        }
    }

    /// Opens a vegetation stroke on the terrain under the cursor (the mouse path).
    pub(super) fn begin_vegetation_stroke(&mut self) -> bool {
        if self.terrain_editor.stroke.is_some() || self.views.session {
            return false;
        }
        let Some((entity, hit)) = self.terrain_cursor.hit else {
            return false;
        };
        if self.terrain_vegetation.entity != Some(entity) {
            self.terrain_vegetation.entity = Some(entity);
            self.refresh_vegetation_state();
        }
        if !self.terrain_vegetation.info.is_some_and(|i| i.enabled) {
            self.terrain_vegetation.status =
                Some("Turn on vegetation for this terrain first".to_owned());
            return false;
        }
        let brush = self.vegetation_brush();
        if !self.open_vegetation_stroke(entity, &brush) {
            return false;
        }
        self.add_terrain_stroke_point(hit);
        true
    }

    pub(super) fn open_vegetation_stroke(&mut self, entity: u32, brush: &VegBrush) -> bool {
        let Some(engine) = self.engine.as_mut() else {
            return false;
        };
        match engine.terrain_veg_paint_begin(entity, brush) {
            Ok(()) => {
                self.terrain_editor.stroke = Some(super::terraineditor::OpenStroke {
                    entity,
                    points: 0,
                    paint: false,
                    vegetation: true,
                    mask: None,
                });
                true
            }
            Err(error) => {
                let message = format!("Could not start planting: {error:?}");
                self.log(LogLevel::Error, message.clone());
                self.terrain_vegetation.status = Some(message);
                false
            }
        }
    }

    /// One whole vegetation stroke along world-space points, as an agent or a test makes it: the same path as the mouse.
    pub(super) fn vegetation_stroke_along(
        &mut self,
        terrain: Option<u32>,
        points: &[[f64; 3]],
    ) -> Result<doppio_engine::StrokeSummary, String> {
        self.ensure_terrain_library();
        if points.is_empty() {
            return Err("a stroke needs at least one point".to_owned());
        }
        if self.views.session {
            return Err("stop the game before planting".to_owned());
        }
        self.finish_terrain_stroke();
        self.refresh_vegetation_state();
        let entity = terrain
            .or(self.terrain_vegetation.entity)
            .ok_or("there is no terrain")?;
        if !self.terrain_vegetation.info.is_some_and(|i| i.enabled) {
            return Err("turn on vegetation first".to_owned());
        }
        let brush = self.vegetation_brush();
        if !self.open_vegetation_stroke(entity, &brush) {
            return Err(self
                .terrain_vegetation
                .status
                .clone()
                .unwrap_or_else(|| "could not start planting".to_owned()));
        }
        for point in points {
            self.add_terrain_stroke_point(*point);
        }
        self.finish_terrain_stroke()
            .ok_or_else(|| "the stroke could not be finished".to_owned())
    }

    /// `[` and `]` while planting: the brush grows or shrinks by `factor`.
    pub(super) fn scale_vegetation_brush(&mut self, factor: f32) {
        let radius = (self.terrain_vegetation.radius * factor).clamp(0.1, 500.0);
        self.terrain_vegetation.radius = radius;
        if self.terrain_vegetation.active {
            self.set_terrain_brush_cursor(true, radius);
        }
    }
}

//! Editor state and message handling of the **World Composer** tab (`caffeine-website/.../world-composition.md`).
//!
//! The tab edits a composition (`uikit::world`, the Rust mirror of the engine's text grammar) with its own undo
//! history, checks it with the engine (`world_check`), previews it with a coarse build that runs (terrain bake and
//! engine build) on a worker thread through a session-free `WorldBuilder`, and builds it: every recipe resolved and hashed, the terrain recipe baked, the engine's cached build
//! (`world_build_cached`), then the scene. A small world becomes entities (one per instance, its nature model's parts
//! as children, like Scatter); a large one an instance bake (`World/<name>.cinst`), a models file and one entity with
//! the `World Instances` component. The build is one undo step.

use super::{App, LogLevel, PaneKind};
use crate::uikit::panels::world_composer::{
    self as panel, Edit, LibraryItem, OverrideField, OverrideKind, WaterField,
};
use crate::uikit::recipe::{self, Kind, Recipe};
use crate::uikit::recipe_ref::{self, Baked};
use crate::uikit::world::{self, Composition, Override, OverrideOp, Term};
use crate::uikit::{asset_folders, nature, terrain};
use doppio_engine::{WorldBuild, WorldBuildDesc, WorldMask, WorldRecipe, WorldRecipeKind};
use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc::{Receiver, TryRecvError};
use std::sync::Arc;
use std::time::{Duration, Instant};

/// Up to this many instances a build becomes entities; above it, an instance bake drawn by one entity.
pub(super) const SMALL_WORLD: usize = 3000;
/// The preview bakes the terrain at this resolution (the build at the recipe's own).
const PREVIEW_RESOLUTION: usize = 129;
/// Bake parameters (`adapt param.<name>`) are quantised to this many steps per unit: one model per bucket.
pub(super) const PARAM_STEPS: u32 = 4;
/// Edits settle for this long before the preview runs again.
const PREVIEW_DEBOUNCE: Duration = Duration::from_millis(250);

/// A terrain recipe baked to heights and masks.
#[derive(Debug, Clone)]
pub(super) struct BakedTerrain {
    pub heights: Vec<f32>,
    pub res: usize,
    pub size: [f32; 2],
    pub max_height: f32,
    pub splat: Vec<u8>,
    pub masks: Vec<(String, Vec<u8>)>,
}

/// What the last build placed, for the edit layer (`Remove in region`).
#[derive(Debug, Clone)]
pub(super) struct LastBuild {
    /// (class id, cell, position in metres from the terrain's corner).
    pub instances: Vec<(String, [i32; 2], [f32; 3])>,
    /// Where the `World` entity stood (the terrain's corner), in world space.
    pub corner: [f32; 3],
}

/// What a preview worker sends back.
#[derive(Debug)]
pub(super) struct PreviewOutcome {
    /// The edit generation it previewed; anything but the latest is dropped.
    pub generation: u64,
    /// The terrain it baked (content hash of the recipe, terrain), kept whatever the generation.
    pub terrain: Option<(u64, Arc<BakedTerrain>)>,
    /// Counts per class, the note, and the terrain's size; `Err` is the note of a failed preview; `None` when the
    /// worker stopped early because a newer edit came in.
    pub result: Option<Result<(Vec<(String, u32)>, String, [f32; 2]), String>>,
}

struct PreviewJob {
    rx: Receiver<PreviewOutcome>,
}

pub(super) struct WorldGen {
    pub comp: Composition,
    undo: Vec<(Composition, Option<String>)>,
    cursor: usize,
    pub name: String,
    /// The `.generator.world` file the composition came from or was saved to.
    pub path: Option<PathBuf>,
    pub selected: Option<usize>,
    pub dirty: bool,
    pub status: Option<String>,
    pub problems: Vec<String>,
    pub seed_text: String,
    pub search: String,
    pub region: [f32; 4],
    pub library: Vec<LibraryItem>,
    pub preview: Option<Vec<(String, u32)>>,
    pub preview_note: String,
    preview_at: Option<Instant>,
    preview_job: Option<PreviewJob>,
    /// Bumped by every edit; the worker of an older generation stops before its build and its result is dropped.
    pub preview_generation: u64,
    preview_latest: Arc<AtomicU64>,
    /// Baked terrains by (content hash of the recipe file, resolution).
    terrains: HashMap<(u64, usize), Arc<BakedTerrain>>,
    pub last_build: Option<LastBuild>,
    /// Up to this many instances a build becomes entities (`SMALL_WORLD`; tests lower it).
    pub small_world: usize,
    /// The viewport's Remove instances brush is on.
    pub brush: bool,
    /// Its radius in metres.
    pub brush_radius: f32,
    /// Instances the stroke in progress went over (class id, cell); applied on release.
    pub stroke: Vec<(String, [i32; 2])>,
    /// A Library recipe being dragged onto the Composition.
    pub drag: Option<String>,
    /// The terrain's size in metres, once known (the region sliders' range).
    pub size: [f32; 2],
}

impl WorldGen {
    pub fn new() -> Self {
        let comp = Composition::default();
        WorldGen {
            seed_text: comp.seed.to_string(),
            undo: vec![(comp.clone(), None)],
            cursor: 0,
            comp,
            name: "World".to_owned(),
            path: None,
            selected: None,
            dirty: false,
            status: None,
            problems: Vec::new(),
            search: String::new(),
            region: [0.0; 4],
            library: Vec::new(),
            preview: None,
            preview_note: "No preview yet.".to_owned(),
            preview_at: None,
            preview_job: None,
            preview_generation: 0,
            preview_latest: Arc::new(AtomicU64::new(0)),
            terrains: HashMap::new(),
            last_build: None,
            small_world: SMALL_WORLD,
            brush: false,
            brush_radius: 6.0,
            stroke: Vec::new(),
            drag: None,
            size: [256.0, 256.0],
        }
    }

    /// Replaces the composition (a file was opened, or New): a fresh history.
    pub(super) fn load(&mut self, comp: Composition) {
        self.undo = vec![(comp.clone(), None)];
        self.cursor = 0;
        self.seed_text = comp.seed.to_string();
        self.comp = comp;
        self.selected = (!self.comp.classes.is_empty()).then_some(0);
        self.preview = None;
        self.last_build = None;
        self.dirty = false;
        self.schedule_preview();
    }

    /// Records the composition as one undo step; edits with the same `key` in a row merge (a slider drag).
    fn push(&mut self, key: Option<String>) {
        self.dirty = true;
        self.schedule_preview();
        if key.is_some()
            && self.undo.get(self.cursor).and_then(|(_, k)| k.clone()) == key
            && self.cursor + 1 == self.undo.len()
        {
            self.undo[self.cursor].0 = self.comp.clone();
            return;
        }
        self.undo.truncate(self.cursor + 1);
        self.undo.push((self.comp.clone(), key));
        self.cursor = self.undo.len() - 1;
        if self.undo.len() > 200 {
            self.undo.remove(0);
            self.cursor -= 1;
        }
    }

    fn step(&mut self, forward: bool) -> bool {
        let target = if forward {
            (self.cursor + 1 < self.undo.len()).then(|| self.cursor + 1)
        } else {
            self.cursor.checked_sub(1)
        };
        let Some(target) = target else {
            return false;
        };
        self.cursor = target;
        self.comp = self.undo[target].0.clone();
        self.seed_text = self.comp.seed.to_string();
        if self.selected.is_some_and(|i| i >= self.comp.classes.len()) {
            self.selected = self.comp.classes.len().checked_sub(1);
        }
        self.dirty = true;
        self.schedule_preview();
        true
    }

    fn schedule_preview(&mut self) {
        self.preview_at = Some(Instant::now() + PREVIEW_DEBOUNCE);
        self.bump_preview_generation();
    }

    fn bump_preview_generation(&mut self) {
        self.preview_generation += 1;
        self.preview_latest
            .store(self.preview_generation, Ordering::SeqCst);
    }

    /// Takes a worker's result: its terrain joins the cache, its counts replace the preview only when it previewed
    /// the latest edit. Returns whether it was applied.
    pub(super) fn apply_preview(&mut self, outcome: PreviewOutcome) -> bool {
        if let Some((key, terrain)) = outcome.terrain {
            self.terrains.insert((key, PREVIEW_RESOLUTION), terrain);
        }
        if outcome.generation != self.preview_generation {
            return false;
        }
        match outcome.result {
            Some(Ok((counts, note, size))) => {
                self.preview = Some(counts);
                self.preview_note = note;
                self.size = size;
            }
            Some(Err(note)) => {
                self.preview = None;
                self.preview_note = note;
            }
            None => return false,
        }
        true
    }

    pub fn view_state(&self) -> panel::View<'_> {
        panel::View {
            comp: &self.comp,
            name: &self.name,
            dirty: self.dirty,
            selected: self.selected,
            library: self.library.clone(),
            search: &self.search,
            seed_text: &self.seed_text,
            region: self.region,
            size: self.size,
            preview: self.preview.as_deref(),
            preview_note: &self.preview_note,
            problems: &self.problems,
            status: self.status.as_deref(),
            brush: self.brush,
            brush_radius: self.brush_radius,
            dragging: self.drag.is_some(),
        }
    }

    /// Instances of the last build per class id, as `oak 120, rock 30`.
    fn counts_text(counts: &[(String, u32)]) -> String {
        counts
            .iter()
            .map(|(id, n)| format!("{id} {n}"))
            .collect::<Vec<_>>()
            .join(", ")
    }
}

/// The project's `*.generator.*` recipes under `root` (hidden folders skipped), sorted by path.
pub(super) fn scan_library(root: &Path) -> Vec<LibraryItem> {
    fn walk(root: &Path, dir: &Path, out: &mut Vec<LibraryItem>, depth: usize) {
        if depth > 8 {
            return;
        }
        let Ok(entries) = std::fs::read_dir(dir) else {
            return;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            let hidden = path
                .file_name()
                .and_then(|n| n.to_str())
                .is_some_and(|n| n.starts_with('.'));
            if hidden {
                continue;
            }
            if path.is_dir() {
                walk(root, &path, out, depth + 1);
            } else if let Some(kind) = recipe::kind_of(&path) {
                if kind != Kind::World {
                    out.push(LibraryItem {
                        kind,
                        path: asset_folders::relative(root, &path),
                    });
                }
            }
        }
    }
    let mut out = Vec::new();
    walk(root, root, &mut out, 0);
    out.sort_by(|a, b| a.path.cmp(&b.path));
    out
}

fn engine_kind(kind: Kind) -> Option<WorldRecipeKind> {
    match kind {
        Kind::Terrain => Some(WorldRecipeKind::Terrain),
        Kind::Nature => Some(WorldRecipeKind::Nature),
        Kind::Water => Some(WorldRecipeKind::Water),
        Kind::Material => Some(WorldRecipeKind::Material),
        Kind::Texture => Some(WorldRecipeKind::Texture),
        Kind::World => None,
    }
}

/// What the project knows about every recipe the composition references (kind, version, content hash). A missing
/// file is left out: the engine then reports the reference as missing and builds nothing.
pub(super) fn resolve_recipes(root: &Path, comp: &Composition) -> Vec<WorldRecipe> {
    comp.references()
        .into_iter()
        .filter_map(|reference| {
            let path = root.join(&reference);
            let kind = engine_kind(recipe::kind_of(&path)?)?;
            let bytes = std::fs::read(&path).ok()?;
            let generator_version = recipe::read(&path)
                .map(|r| r.generator_version)
                .unwrap_or(1);
            Some(WorldRecipe {
                reference,
                kind,
                generator_version,
                content_hash: recipe_ref::content_hash(&bytes),
            })
        })
        .collect()
}

/// Bakes a terrain recipe file to heights and masks at `res` (its Output's resolution when `None`, never above it).
pub(super) fn bake_terrain(path: &Path, res: Option<usize>) -> Result<BakedTerrain, String> {
    let recipe = recipe::read(path)?;
    if recipe.kind() != Some(Kind::Terrain) {
        return Err(format!("{} is not a terrain recipe", path.display()));
    }
    let (_, graph) = terrain::from_text(&recipe.graph)
        .ok_or_else(|| format!("{}: the terrain graph is unreadable", path.display()))?;
    let full = graph.output_resolution();
    let res = res.map_or(full, |r| r.min(full));
    let model = graph.build(res)?;
    Ok(BakedTerrain {
        heights: model.height.data,
        res: model.height.res,
        size: model.size,
        max_height: model.max_height,
        splat: model.splat,
        masks: model.masks,
    })
}

/// The terrain's size: the composition's when it sets one, else the recipe's.
fn world_size(comp: &Composition, terrain: &BakedTerrain) -> [f32; 2] {
    if comp.size[0] > 0.0 && comp.size[1] > 0.0 {
        comp.size
    } else {
        terrain.size
    }
}

fn build_desc(
    comp: &Composition,
    terrain: &BakedTerrain,
    recipes: Vec<WorldRecipe>,
    region: Option<[f32; 4]>,
    max_instances: u32,
) -> WorldBuildDesc {
    let res = terrain.res as u32;
    WorldBuildDesc {
        graph: comp.to_text(),
        generator_version: recipe::GENERATOR_VERSION,
        recipes: Some(recipes),
        heights: terrain.heights.clone(),
        resolution: [res, res],
        size: world_size(comp, terrain),
        max_height: terrain.max_height,
        masks: terrain
            .masks
            .iter()
            .map(|(name, data)| WorldMask {
                name: name.clone(),
                resolution: [res, res],
                data: data.clone(),
            })
            .collect(),
        region,
        max_instances,
    }
}

fn per_class_counts(build: &WorldBuild) -> Vec<(String, u32)> {
    build
        .classes
        .iter()
        .map(|class| (class.id.clone(), class.instance_count))
        .collect()
}

/// The variant seed `nature_variants` uses for variant `index` of a pool seeded with `base_seed`.
fn variant_seed(base_seed: u64, index: u32) -> u32 {
    (recipe_ref::derive_seed(base_seed, 0xBA5E, index as i32, 0) % 9_999) as u32 + 1
}

/// Which model an instance draws.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
enum ModelKey {
    Variant(u32),
    Cell([i32; 2]),
}

/// One part of a baked model: its mesh (relative to the asset root), its material(s) as the scene applies them
/// (`;`-joined per sub-mesh, empty for none) and whether it draws as cut-out cards.
#[derive(Debug, Clone, PartialEq)]
pub(super) struct ModelPart {
    pub mesh: String,
    pub slots: String,
    pub cutout: bool,
}

/// The baked models of every class of a build: (class index, key) -> parts.
type Models = BTreeMap<(usize, ModelKey), Vec<ModelPart>>;

/// The Nature Generator's material writer behind a world bake: a baked part gets the same `.mat` asset (bark, leaf
/// cards, rock surface, your own material) as the part of a tree placed from the Nature tab.
pub(super) struct WorldMaterials<'a> {
    pub app: &'a mut App,
    pub root: PathBuf,
}

impl recipe_ref::PartMaterials for WorldMaterials<'_> {
    fn material(&mut self, part: &nature::mesh::Part) -> Result<String, String> {
        match &part.surface {
            Some(spec) => self.app.ensure_surface_material(part, spec),
            None => self.app.ensure_nature_material(
                &part.material,
                super::naturegen::Foliage::for_spec(&part.name, &part.material),
            ),
        }
    }

    fn exists(&self, material: &str) -> bool {
        let path = Path::new(material);
        path.is_file() || self.root.join(path).is_file()
    }
}

/// A material path as the models file stores it: relative to the asset root when it is under it.
fn asset_relative(root: &Path, path: &str) -> String {
    let as_path = Path::new(path);
    if as_path.is_absolute() && as_path.starts_with(root) {
        asset_folders::relative(root, as_path)
    } else {
        path.replace('\\', "/")
    }
}

/// True when a `.mat` cuts its shape out of its picture (it says so in its alpha mode).
fn material_is_cutout(root: &Path, material: &str) -> bool {
    material
        .split(';')
        .filter(|m| !m.is_empty())
        .any(|m| {
            let path = Path::new(m);
            let file = if path.is_absolute() {
                path.to_path_buf()
            } else {
                root.join(path)
            };
            std::fs::read_to_string(file).is_ok_and(|text| text.to_lowercase().contains("cutout"))
        })
}

/// Bakes the nature models a build needs: the variant pool of each class, or one model per parameter bucket
/// (`adapt param.<name>`, quantised) or per instance (`unique`). Every part gets its material from `materials`.
fn bake_models(
    root: &Path,
    build: &WorldBuild,
    cache: &Path,
    materials: &mut dyn recipe_ref::PartMaterials,
) -> Result<(Models, usize), String> {
    let mut models = Models::new();
    let mut baked = 0usize;
    let parts = |baked: &Baked| -> Vec<ModelPart> {
        baked
            .files
            .iter()
            .enumerate()
            .map(|(index, file)| {
                let mesh = asset_folders::relative(root, &baked.directory.join(file));
                let slots = baked.materials.get(index).cloned().unwrap_or_default();
                ModelPart {
                    cutout: world::is_cutout_part(&mesh) || material_is_cutout(root, &slots),
                    mesh,
                    slots,
                }
            })
            .collect()
    };
    for (index, class) in build.classes.iter().enumerate() {
        let recipe_path = root.join(&class.reference);
        let base_seed = u64::from(class.salt);
        if class.unique || !class.params.is_empty() {
            let mut buckets: HashMap<Vec<u32>, Vec<ModelPart>> = HashMap::new();
            for instance in build.instances_of(index) {
                let (seed, values): (u32, Vec<f32>) = if class.unique {
                    (
                        (instance.seed % 9_999) as u32 + 1,
                        instance.params[..class.params.len()].to_vec(),
                    )
                } else {
                    (
                        variant_seed(base_seed, instance.variant),
                        instance.params[..class.params.len()]
                            .iter()
                            .map(|v| recipe_ref::quantize_param(*v, PARAM_STEPS))
                            .collect(),
                    )
                };
                let mut bucket = vec![seed];
                bucket.extend(values.iter().map(|v| v.to_bits()));
                let files = match buckets.get(&bucket) {
                    Some(files) => files.clone(),
                    None => {
                        let params: Vec<(String, f32)> = class
                            .params
                            .iter()
                            .cloned()
                            .zip(values.iter().copied())
                            .collect();
                        let result = recipe_ref::bake_nature_instance_with(
                            &recipe_path,
                            seed,
                            None,
                            &params,
                            cache,
                            Some(&mut *materials),
                        )
                        .map_err(|e| format!("{}: {e}", class.id))?;
                        baked += usize::from(!result.cached);
                        let files = parts(&result);
                        buckets.insert(bucket, files.clone());
                        files
                    }
                };
                models.insert((index, ModelKey::Cell(instance.cell)), files);
            }
        } else {
            let count = build
                .instances_of(index)
                .iter()
                .map(|i| i.variant + 1)
                .max()
                .unwrap_or(0);
            let pool = recipe_ref::nature_variants_with(
                &recipe_path,
                base_seed,
                count,
                None,
                cache,
                Some(&mut *materials),
            )
            .map_err(|e| format!("{}: {e}", class.id))?;
            for (variant, result) in pool.iter().enumerate() {
                baked += usize::from(!result.cached);
                models.insert((index, ModelKey::Variant(variant as u32)), parts(result));
            }
        }
    }
    Ok((models, baked))
}

/// The preview worker: bakes the terrain unless it is cached, stops if a newer edit came in, then builds.
#[allow(clippy::too_many_arguments)]
fn run_preview(
    builder: doppio_engine::WorldBuilder,
    generation: u64,
    latest: &AtomicU64,
    root: &Path,
    comp: &Composition,
    terrain_path: &Path,
    key: u64,
    cached: Option<Arc<BakedTerrain>>,
) -> PreviewOutcome {
    let mut outcome = PreviewOutcome {
        generation,
        terrain: None,
        result: None,
    };
    let terrain = match cached {
        Some(terrain) => terrain,
        None => match bake_terrain(terrain_path, Some(PREVIEW_RESOLUTION)) {
            Ok(baked) => {
                let baked = Arc::new(baked);
                outcome.terrain = Some((key, baked.clone()));
                baked
            }
            Err(error) => {
                outcome.result = Some(Err(format!("Preview: {error}")));
                return outcome;
            }
        },
    };
    if latest.load(Ordering::SeqCst) != generation {
        return outcome;
    }
    let recipes = resolve_recipes(root, comp);
    let desc = build_desc(comp, &terrain, recipes, None, 0);
    let started = Instant::now();
    outcome.result = Some(match builder.build(&desc) {
        Ok(build) if build.ok => {
            let counts = per_class_counts(&build);
            let note = format!(
                "Preview ({}² terrain, {:.0} ms): {} instance{} — {}",
                terrain.res,
                started.elapsed().as_secs_f32() * 1000.0,
                build.instances.len(),
                if build.instances.len() == 1 { "" } else { "s" },
                WorldGen::counts_text(&counts)
            );
            Ok((counts, note, desc.size))
        }
        Ok(build) => Err(format!("Preview: {}", build.errors.join("; "))),
        Err(error) => Err(format!("Preview: {error}")),
    });
    outcome
}

impl App {
    pub(super) fn show_world_pane(&mut self) {
        self.refresh_world_library();
        self.refresh_world_problems();
        self.panes.restore();
        let mut target = None;
        for (pane, group) in self.panes.iter_mut() {
            if let Some(index) = group.pages.iter().position(|page| *page == PaneKind::World) {
                group.active = index;
                target = Some(*pane);
            }
        }
        if target.is_none() {
            for (pane, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Viewport) {
                    group.pages.push(PaneKind::World);
                    group.active = group.pages.len() - 1;
                    target = Some(*pane);
                    break;
                }
            }
        }
        if let Some(pane) = target {
            self.panes.maximize(pane);
        }
    }

    pub(super) fn refresh_world_library(&mut self) {
        self.worldgen.library = self
            .asset_root_dir()
            .map(|root| scan_library(&root))
            .unwrap_or_default();
    }

    /// Asks the engine what is wrong with the composition (or checks it in Rust without an engine library).
    pub(super) fn refresh_world_problems(&mut self) {
        let text = self.worldgen.comp.to_text();
        let checked = self.engine.as_ref().map(|engine| engine.world_check(&text));
        self.worldgen.problems = match checked {
            Some(Ok(())) => Vec::new(),
            Some(Err(error)) if !error.contains("C ABI v28") => error
                .lines()
                .filter(|line| !line.trim().is_empty())
                .map(str::to_owned)
                .collect(),
            _ => self.worldgen.comp.validate(),
        };
    }

    /// Opens a `.generator.world` recipe in the World tab.
    pub(super) fn open_world_recipe(&mut self, path: &Path, recipe: &Recipe) -> bool {
        match Composition::parse(&recipe.graph) {
            Ok(comp) => {
                self.worldgen.name = recipe.name.clone();
                self.worldgen.path = Some(path.to_path_buf());
                self.worldgen.load(comp);
                self.worldgen.status = None;
                self.show_world_pane();
                true
            }
            Err(error) => {
                self.log(LogLevel::Error, format!("World recipe: {error}"));
                false
            }
        }
    }

    fn world_dir(&self) -> Option<PathBuf> {
        self.asset_root_dir().map(|root| root.join("World"))
    }

    pub(super) fn edit_world(&mut self, edit: Edit) {
        let w = &mut self.worldgen;
        match edit {
            Edit::Undo => {
                w.step(false);
            }
            Edit::Redo => {
                w.step(true);
            }
            Edit::New => {
                let terrain = w.comp.terrain.clone();
                w.path = None;
                w.load(world::starter(&terrain));
                w.dirty = true;
            }
            Edit::Save => {
                self.worldgen.status = Some(match self.save_world() {
                    Ok(path) => format!("Saved {path}"),
                    Err(error) => format!("Could not save: {error}"),
                });
                self.refresh_world_library();
            }
            Edit::SaveAsPrefab => self.save_world_prefab(),
            Edit::Build => self.build_world(),
            Edit::Frame => self.frame_world(),
            Edit::Preview => {
                w.preview_at = Some(Instant::now());
                w.bump_preview_generation();
                self.pump_world_preview(false);
            }
            Edit::SetName(name) => w.name = name,
            Edit::SetSearch(search) => w.search = search,
            Edit::Add(path) => {
                // A release on another Library row than the one pressed is a cancelled drag, not a click.
                if w.drag.take().is_none_or(|pressed| pressed == path) {
                    self.add_to_world(&path, None);
                }
            }
            Edit::DragStart(path) => w.drag = Some(path),
            Edit::Drop => {
                if let Some(path) = w.drag.take() {
                    self.add_to_world(&path, None);
                }
            }
            Edit::SetBrush(on) => {
                let radius = w.brush_radius;
                self.set_world_brush(on, radius);
            }
            Edit::SetBrushRadius(radius) => {
                let on = w.brush;
                w.brush_radius = radius.clamp(0.5, 200.0);
                if on {
                    self.set_terrain_brush_cursor(true, radius.clamp(0.5, 200.0));
                }
            }
            Edit::RemoveInCircle { center, radius } => self.remove_world_in_circle(center, radius),
            Edit::AddClass { reference, id } => self.add_to_world(&reference, id),
            Edit::SelectClass(index) => {
                if index < w.comp.classes.len() {
                    w.selected = Some(index);
                }
            }
            Edit::RemoveClass(index) => {
                if index < w.comp.classes.len() {
                    let removed = w.comp.classes.remove(index);
                    // Hand edits of a class that is gone would make the composition invalid.
                    w.comp.overrides.retain(|o| o.class != removed.id);
                    w.selected = w
                        .selected
                        .filter(|_| !w.comp.classes.is_empty())
                        .map(|s| s.min(w.comp.classes.len() - 1));
                    w.push(None);
                }
            }
            Edit::RemoveWater(index) => {
                if index < w.comp.water.len() {
                    w.comp.water.remove(index);
                    w.push(None);
                }
            }
            Edit::RemoveMaterial(index) => {
                if index < w.comp.materials.len() {
                    w.comp.materials.remove(index);
                    w.push(None);
                }
            }
            Edit::SetWater(index, field, value) => {
                if let Some(body) = w.comp.water.get_mut(index) {
                    match field {
                        WaterField::Min => body.min = value.clamp(0.0, 1.0),
                        WaterField::Level => body.level = value.clamp(0.0, 1.0),
                        WaterField::Carve => body.carve = value.max(0.0),
                    }
                    w.push(Some(format!("water:{index}:{field:?}")));
                }
            }
            Edit::SetMaterialSlot(index, slot) => {
                let slot: String = slot.chars().filter(|c| !c.is_whitespace()).collect();
                if let Some(binding) = w.comp.materials.get_mut(index) {
                    if !slot.is_empty() {
                        binding.slot = slot;
                        w.push(Some(format!("slot:{index}")));
                    }
                }
            }
            Edit::SetTerrainSize(axis, value) => {
                if axis < 2 {
                    w.comp.size[axis] = value.max(0.0);
                    w.push(Some(format!("size:{axis}")));
                }
            }
            Edit::SetSeed(text) => {
                w.seed_text = text.clone();
                if let Ok(seed) = text.trim().parse::<u64>() {
                    if seed != w.comp.seed {
                        w.comp.seed = seed;
                        w.push(Some("seed".to_owned()));
                    }
                }
            }
            Edit::Reseed => {
                w.comp.seed = w.comp.reseeded();
                w.seed_text = w.comp.seed.to_string();
                w.push(None);
                w.status = Some(format!("Reseeded: seed {}", w.comp.seed));
            }
            Edit::SetClass(index, field, value) => {
                if let Some(class) = w.comp.classes.get_mut(index) {
                    field.set(class, value);
                    w.push(Some(format!("class:{index}:{}", field.key())));
                }
            }
            Edit::SetClassById {
                class,
                field,
                value,
            } => match w.comp.class_index(&class) {
                Some(index) => {
                    field.set(&mut w.comp.classes[index], value);
                    w.selected = Some(index);
                    w.push(Some(format!("class:{index}:{}", field.key())));
                }
                None => w.status = Some(format!("No class `{class}` in the composition")),
            },
            Edit::SetUnique(index, on) => {
                if let Some(class) = w.comp.classes.get_mut(index) {
                    class.unique = on;
                    w.push(None);
                }
            }
            Edit::AddTerm(index) => {
                if let Some(class) = w.comp.classes.get_mut(index) {
                    let used: Vec<&str> = class.terms.iter().map(|t| t.mask.as_str()).collect();
                    let mask = world::MASKS
                        .iter()
                        .find(|m| !used.contains(m))
                        .unwrap_or(&"slope");
                    class.terms.push(Term {
                        mask: (*mask).to_owned(),
                        values: [0.0, 0.0, 1.0, 1.0],
                    });
                    w.push(None);
                }
            }
            Edit::RemoveTerm(index, term) => {
                if let Some(class) = w.comp.classes.get_mut(index) {
                    if term < class.terms.len() {
                        class.terms.remove(term);
                        w.push(None);
                    }
                }
            }
            Edit::SetTermMask(index, term, mask) => {
                if let Some(t) = w
                    .comp
                    .classes
                    .get_mut(index)
                    .and_then(|c| c.terms.get_mut(term))
                {
                    t.mask = mask;
                    w.push(None);
                }
            }
            Edit::SetTerm(index, term, value_index, value) => {
                if let Some(t) = w
                    .comp
                    .classes
                    .get_mut(index)
                    .and_then(|c| c.terms.get_mut(term))
                {
                    if value_index < 4 {
                        // The four numbers never decrease: moving one pushes its neighbours along.
                        let value = value.clamp(0.0, 1.0);
                        t.values[value_index] = value;
                        for i in value_index + 1..4 {
                            t.values[i] = t.values[i].max(value);
                        }
                        for i in 0..value_index {
                            t.values[i] = t.values[i].min(value);
                        }
                        w.push(Some(format!("term:{index}:{term}:{value_index}")));
                    }
                }
            }
            Edit::SetRule {
                class,
                mask,
                values,
            } => {
                let Some(index) = w.comp.class_index(&class) else {
                    w.status = Some(format!("No class `{class}` in the composition"));
                    return;
                };
                let mut values = values.map(|v| v.clamp(0.0, 1.0));
                for i in 1..4 {
                    values[i] = values[i].max(values[i - 1]);
                }
                let terms = &mut w.comp.classes[index].terms;
                match terms.iter_mut().find(|t| t.mask == mask) {
                    Some(term) => term.values = values,
                    None => terms.push(Term { mask, values }),
                }
                w.selected = Some(index);
                w.push(None);
            }
            Edit::AddAdapt(index) => {
                if let Some(class) = w.comp.classes.get_mut(index) {
                    class.adapt.push(world::Adapt {
                        target: "size".to_owned(),
                        mask: "wetness".to_owned(),
                        amount: None,
                    });
                    w.push(None);
                }
            }
            Edit::RemoveAdapt(index, adapt) => {
                if let Some(class) = w.comp.classes.get_mut(index) {
                    if adapt < class.adapt.len() {
                        class.adapt.remove(adapt);
                        w.push(None);
                    }
                }
            }
            Edit::SetAdaptTarget(index, adapt, target) => {
                if let Some(a) = w
                    .comp
                    .classes
                    .get_mut(index)
                    .and_then(|c| c.adapt.get_mut(adapt))
                {
                    if world::is_adapt_target(&target) {
                        a.target = target;
                        w.push(None);
                    }
                }
            }
            Edit::SetAdaptMask(index, adapt, mask) => {
                if let Some(a) = w
                    .comp
                    .classes
                    .get_mut(index)
                    .and_then(|c| c.adapt.get_mut(adapt))
                {
                    a.mask = mask;
                    w.push(None);
                }
            }
            Edit::SetAdaptAmount(index, adapt, amount) => {
                if let Some(a) = w
                    .comp
                    .classes
                    .get_mut(index)
                    .and_then(|c| c.adapt.get_mut(adapt))
                {
                    a.amount = Some(amount.max(0.0));
                    w.push(Some(format!("adapt:{index}:{adapt}")));
                }
            }
            Edit::SetRegion(index, value) => {
                if index < 4 {
                    w.region[index] = value.max(0.0);
                }
            }
            Edit::RemoveIn(region) => self.remove_world_in_region(region),
            Edit::AddOverride(index, kind) => {
                if let Some(class) = w.comp.classes.get(index) {
                    let op = match kind {
                        OverrideKind::Pin => OverrideOp::Pin { variant: 0 },
                        OverrideKind::Move => OverrideOp::Move {
                            x: w.size[0] * 0.5,
                            z: w.size[1] * 0.5,
                        },
                    };
                    w.comp.overrides.push(Override {
                        op,
                        class: class.id.clone(),
                        cell: [0, 0],
                    });
                    w.push(None);
                }
            }
            Edit::RemoveOverride(index) => {
                if index < w.comp.overrides.len() {
                    w.comp.overrides.remove(index);
                    w.push(None);
                }
            }
            Edit::SetOverride(index, field, value) => {
                let variants = w
                    .comp
                    .overrides
                    .get(index)
                    .and_then(|o| w.comp.class(&o.class))
                    .map_or(1, |c| c.variants.max(1));
                if let Some(o) = w.comp.overrides.get_mut(index) {
                    match (field, &mut o.op) {
                        (OverrideField::CellX, _) => o.cell[0] = value.round() as i32,
                        (OverrideField::CellZ, _) => o.cell[1] = value.round() as i32,
                        (OverrideField::X, OverrideOp::Move { x, .. }) => *x = value.max(0.0),
                        (OverrideField::Z, OverrideOp::Move { z, .. }) => *z = value.max(0.0),
                        (OverrideField::Variant, OverrideOp::Pin { variant }) => {
                            *variant = (value.round().max(0.0) as u32).min(variants - 1)
                        }
                        _ => return,
                    }
                    w.push(Some(format!("override:{index}:{field:?}")));
                }
            }
            Edit::Noop => {}
        }
        self.refresh_world_problems();
    }

    /// A library recipe joins the composition by its kind.
    fn add_to_world(&mut self, reference: &str, id: Option<String>) {
        let kind = recipe::kind_of(Path::new(reference));
        let w = &mut self.worldgen;
        match kind {
            Some(Kind::Terrain) => {
                w.comp.terrain = reference.to_owned();
                w.push(None);
                w.status = Some(format!("The world grows on {reference}"));
            }
            Some(Kind::Nature) => {
                let taken: Vec<String> = w.comp.classes.iter().map(|c| c.id.clone()).collect();
                let id = id
                    .filter(|id| !id.trim().is_empty())
                    .unwrap_or_else(|| world::class_id_for(reference, &taken));
                if taken.contains(&id) {
                    w.status = Some(format!("A class `{id}` is already in the composition"));
                    return;
                }
                let mut class = world::Class::new(&id, reference);
                class.variants = 4;
                class.priority = 10;
                w.comp.classes.push(class);
                w.selected = Some(w.comp.classes.len() - 1);
                w.push(None);
                w.status = Some(format!("Added class {id}"));
            }
            Some(Kind::Water) => {
                w.comp.water.push(world::Water::river(reference));
                w.push(None);
            }
            Some(Kind::Material | Kind::Texture) => {
                let slot = format!("terrain.layer{}", w.comp.materials.len());
                w.comp.materials.push(world::Material {
                    slot,
                    reference: reference.to_owned(),
                });
                w.push(None);
            }
            Some(Kind::World) | None => {
                w.status = Some(format!(
                    "{reference} is not a terrain, nature, water, material or texture recipe"
                ));
            }
        }
    }

    /// Writes `World/<Name>.generator.world` (generator version 2) and returns its project path.
    pub(super) fn save_world(&mut self) -> Result<String, String> {
        self.ensure_workspace();
        let root = self
            .asset_root_dir()
            .ok_or("open a project first: worlds are written to its World folder")?;
        let slug = nature::slug(&self.worldgen.name);
        let slug = if slug.is_empty() {
            "world".to_owned()
        } else {
            slug
        };
        let path = self
            .worldgen
            .path
            .clone()
            .filter(|p| p.starts_with(&root) && p.is_file())
            .unwrap_or_else(|| {
                root.join("World")
                    .join(recipe::file_name(&slug, Kind::World))
            });
        let mut recipe = Recipe::new(
            Kind::World,
            &self.worldgen.name,
            "",
            self.worldgen.comp.to_text(),
        );
        recipe.generator_version = 2;
        recipe::write(&path, &recipe)?;
        self.worldgen.path = Some(path.clone());
        self.worldgen.dirty = false;
        self.refresh_assets();
        Ok(asset_folders::relative(&root, &path))
    }

    /// Bakes (or reuses) the terrain of `comp` at `res`.
    fn world_terrain(
        &mut self,
        root: &Path,
        comp: &Composition,
        res: Option<usize>,
    ) -> Result<Arc<BakedTerrain>, String> {
        if comp.terrain.is_empty() {
            return Err("the composition has no terrain".to_owned());
        }
        let path = root.join(&comp.terrain);
        let bytes = std::fs::read(&path).map_err(|e| format!("{}: {e}", comp.terrain))?;
        let key = (recipe_ref::content_hash(&bytes), res.unwrap_or(0));
        if let Some(baked) = self.worldgen.terrains.get(&key) {
            return Ok(baked.clone());
        }
        let baked = Arc::new(bake_terrain(&path, res)?);
        self.worldgen.terrains.insert(key, baked.clone());
        Ok(baked)
    }

    /// The coarse preview, off the UI thread: a worker bakes the terrain (once per recipe content) and builds the
    /// composition with a session-free `WorldBuilder`, then the counts per class come back through a channel. Runs
    /// from the tick once edits settle, one worker at a time; a worker whose edit is no longer the latest stops before
    /// its build and its result is dropped. `wait` blocks until it is done (tests).
    pub(super) fn pump_world_preview(&mut self, wait: bool) {
        if let Some(job) = self.worldgen.preview_job.take() {
            let outcome = if wait {
                job.rx.recv().ok()
            } else {
                match job.rx.try_recv() {
                    Ok(outcome) => Some(outcome),
                    Err(TryRecvError::Empty) => {
                        self.worldgen.preview_job = Some(job);
                        return;
                    }
                    Err(TryRecvError::Disconnected) => None,
                }
            };
            match outcome {
                Some(outcome) => {
                    self.worldgen.apply_preview(outcome);
                }
                None => self.worldgen.preview_note = "Preview: the worker stopped".to_owned(),
            }
        }
        let due = self
            .worldgen
            .preview_at
            .is_some_and(|at| wait || Instant::now() >= at);
        if !due {
            return;
        }
        let visible = wait
            || self
                .panes
                .iter()
                .any(|(_, group)| group.active_kind() == PaneKind::World);
        if !visible {
            return;
        }
        self.worldgen.preview_at = None;
        let comp = self.worldgen.comp.clone();
        let Some(root) = self.asset_root_dir() else {
            self.worldgen.preview_note =
                "Preview: open a project to resolve the recipes".to_owned();
            return;
        };
        if comp.terrain.is_empty() {
            self.worldgen.preview_note = "Preview: add a terrain recipe first".to_owned();
            return;
        }
        let Some(builder) = self
            .engine
            .as_ref()
            .and_then(doppio_engine::Engine::world_builder)
        else {
            self.worldgen.preview_note =
                "Preview: the engine library has no World Composer (C ABI v28)".to_owned();
            return;
        };
        let path = root.join(&comp.terrain);
        let bytes = match std::fs::read(&path) {
            Ok(bytes) => bytes,
            Err(error) => {
                self.worldgen.preview_note = format!("Preview: {}: {error}", comp.terrain);
                return;
            }
        };
        let key = recipe_ref::content_hash(&bytes);
        let cached = self
            .worldgen
            .terrains
            .get(&(key, PREVIEW_RESOLUTION))
            .cloned();
        if cached.is_none() {
            self.worldgen.preview_note = "Preview: baking the terrain…".to_owned();
        }
        let generation = self.worldgen.preview_generation;
        let latest = self.worldgen.preview_latest.clone();
        let (tx, rx) = std::sync::mpsc::channel();
        std::thread::spawn(move || {
            let _ = tx.send(run_preview(
                builder, generation, &latest, &root, &comp, &path, key, cached,
            ));
        });
        self.worldgen.preview_job = Some(PreviewJob { rx });
        if wait {
            self.pump_world_preview(true);
        }
    }

    /// Points the viewport camera at the last build from above its south-west corner, so the whole world is in view,
    /// and brings the Viewport tab to the front.
    pub(super) fn frame_world(&mut self) {
        let Some(corner) = self.worldgen.last_build.as_ref().map(|b| b.corner) else {
            self.worldgen.status = Some("Build the world first: Frame looks at the last build".to_owned());
            return;
        };
        let size = self.worldgen.size;
        let span = size[0].max(size[1]).max(1.0);
        self.camera = doppio_engine::Camera {
            yaw: 0.75,
            pitch: 0.5,
            distance: span * 0.95,
            focus: [
                corner[0] + size[0] * 0.5,
                corner[1] + span * 0.04,
                corner[2] + size[1] * 0.5,
            ],
        };
        self.apply_camera();
        self.panes.restore();
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group.pages.iter().position(|p| *p == PaneKind::Viewport) {
                group.active = index;
            }
        }
        self.worldgen.status = Some(format!(
            "Framed World {} ({:.0} x {:.0} m) in the Viewport",
            self.worldgen.name, size[0], size[1]
        ));
    }

    pub(super) fn build_world(&mut self) {
        match self.run_world_build() {
            Ok(summary) => {
                self.log(LogLevel::Action, summary.clone());
                self.worldgen.status = Some(summary);
            }
            Err(error) => {
                self.log(LogLevel::Error, format!("World build: {error}"));
                self.worldgen.status = Some(format!("Build: {error}"));
            }
        }
    }

    /// The `World <name>` entity of a world, if the scene has one.
    fn world_holder(&self, name: &str) -> Option<u32> {
        let wanted = format!("World {name}");
        self.entities
            .iter()
            .find(|e| e.name == wanted && e.parent.is_none())
            .map(|e| e.id)
    }

    fn run_world_build(&mut self) -> Result<String, String> {
        let started = Instant::now();
        self.ensure_workspace();
        let root = self
            .asset_root_dir()
            .ok_or("open a project first: the recipes are resolved in its assets folder")?;
        let comp = self.worldgen.comp.clone();
        let name = self.worldgen.name.trim().to_owned();
        let name = if name.is_empty() {
            "World".to_owned()
        } else {
            name
        };
        {
            let engine = self.engine.as_ref().ok_or("the engine is not available")?;
            engine
                .world_check(&comp.to_text())
                .map_err(|e| e.trim().replace('\n', "; "))?;
        }
        let terrain = self.world_terrain(&root, &comp, None)?;
        let recipes = resolve_recipes(&root, &comp);
        let desc = build_desc(&comp, &terrain, recipes, None, 0);
        let size = desc.size;
        let cache = root.join(".cache").join("world");
        let (build, hit) = self
            .engine
            .as_ref()
            .ok_or("the engine is not available")?
            .world_build_cached(&desc, &cache)?;
        if !build.ok {
            return Err(build.errors.join("; "));
        }
        // A cached build has the instances but not the water layer: when the composition has water, the engine
        // builds it once more (uncached) for the carved ground, the water bodies and the `water_distance` mask.
        let water_layer = if hit && !comp.water.is_empty() {
            let full = self
                .engine
                .as_ref()
                .ok_or("the engine is not available")?
                .world_build(&desc)?;
            if !full.ok {
                return Err(full.errors.join("; "));
            }
            Some(full)
        } else {
            None
        };
        let layer = water_layer.as_ref().unwrap_or(&build);
        // Every water body with a surface grid (rivers and lakes) becomes a shaped water surface; without the
        // engine's grid a lake is a flat plane and a river is only carved.
        let shaped = self
            .engine
            .as_ref()
            .is_some_and(doppio_engine::Engine::has_water_surface);
        let waters: Vec<WaterSpec> = layer
            .water
            .iter()
            .filter_map(|body| water_spec(&root, body, terrain.res, size, shaped))
            .collect();
        let rivers = layer.water.iter().filter(|body| !body.lake).count()
            - waters.iter().filter(|w| !w.lake).count();
        let small = build.instances.len() <= self.worldgen.small_world;
        let nature_cache = cache.join("nature");
        let (models, baked) = bake_models(
            &root,
            &build,
            &nature_cache,
            &mut WorldMaterials {
                app: self,
                root: root.clone(),
            },
        )?;
        let slug = {
            let slug = nature::slug(&name);
            if slug.is_empty() {
                "world".to_owned()
            } else {
                slug
            }
        };
        // A large world: the instance bake and its models file, written before the scene changes.
        let mut bake_paths = None;
        if !small {
            let dir = root.join("World");
            std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
            let cinst = dir.join(format!("{slug}.cinst"));
            let written = self
                .engine
                .as_ref()
                .ok_or("the engine is not available")?
                .world_build_baked(&desc, Some(&cinst), 64.0)?;
            if !written.ok || !written.errors.is_empty() {
                return Err(written.errors.join("; "));
            }
            let mut file = world::ModelsFile::default();
            for ((class, key), parts) in &models {
                for part in parts {
                    let slots = part
                        .slots
                        .split(';')
                        .map(|m| asset_relative(&root, m))
                        .collect::<Vec<_>>()
                        .join(";");
                    match key {
                        ModelKey::Variant(v) => {
                            file.variant(*class, *v, &part.mesh, &slots, part.cutout)
                        }
                        ModelKey::Cell(cell) => {
                            file.cell(*class, *cell, &part.mesh, &slots, part.cutout)
                        }
                    }
                }
            }
            let models_path = dir.join(format!("{slug}.models"));
            std::fs::write(&models_path, file.to_text())
                .map_err(|e| format!("{}: {e}", models_path.display()))?;
            bake_paths = Some((
                asset_folders::relative(&root, &cinst),
                asset_folders::relative(&root, &models_path),
            ));
        }

        // The scene: the previous build of this world goes, its place stays.
        let previous = self.world_holder(&name);
        let focus = self.camera.focus;
        let mut problems: Vec<String> = Vec::new();
        let engine = self.engine.as_mut().ok_or("the engine is not available")?;
        let corner = match previous {
            Some(id) => engine.position(id).unwrap_or([
                focus[0] - size[0] * 0.5,
                0.0,
                focus[2] - size[1] * 0.5,
            ]),
            None => [focus[0] - size[0] * 0.5, 0.0, focus[2] - size[1] * 0.5],
        };
        if let Some(old) = previous {
            destroy_tree(engine, old);
        }
        let holder = engine
            .create_entity(&format!("World {name}"))
            .map_err(|e| format!("could not create the holder: {e:?}"))?;
        engine
            .set_position(holder, corner)
            .map_err(|e| format!("position: {e:?}"))?;
        // The ground: the terrain recipe (carved by the rivers when the engine carved it), centred on its entity.
        let heights = if layer.carved && layer.heights.len() == terrain.heights.len() {
            layer.heights.clone()
        } else {
            terrain.heights.clone()
        };
        match engine.create_entity(&format!("{name} terrain")) {
            Ok(ground) => {
                let _ = engine.set_entity_parent(ground, Some(holder));
                let _ = engine.set_position(ground, [size[0] * 0.5, 0.0, size[1] * 0.5]);
                let res = terrain.res as u32;
                let settings = doppio_engine::Terrain {
                    resolution: [res, res],
                    size,
                    max_height: terrain.max_height,
                    cast_shadows: true,
                };
                let splat = (terrain.splat.len() == terrain.heights.len() * 4)
                    .then_some(terrain.splat.as_slice());
                if let Err(error) = engine.set_terrain(ground, &settings, &heights, splat) {
                    problems.push(format!("terrain: {error:?}"));
                } else {
                    for (mask, data) in &terrain.masks {
                        let _ = engine.set_terrain_mask(ground, mask, [res, res], data);
                    }
                    if layer.water_distance.len() == terrain.heights.len() {
                        let _ = engine.set_terrain_mask(
                            ground,
                            "water_distance",
                            [res, res],
                            &layer.water_distance,
                        );
                    }
                }
            }
            Err(error) => problems.push(format!("terrain: {error:?}")),
        }
        // The water: one surface per body over its bounds, shaped by its grid when the engine gives one.
        let (mut lakes_placed, mut rivers_placed) = (0, 0);
        for water in &waters {
            let (kind, number) = if water.lake {
                lakes_placed += 1;
                ("lake", lakes_placed)
            } else {
                rivers_placed += 1;
                ("river", rivers_placed)
            };
            if let Err(error) =
                place_water(engine, holder, &format!("{name} {kind} {number}"), water)
            {
                problems.push(format!("{kind} {number}: {error}"));
            }
        }

        let mut placed: BTreeMap<String, u32> = BTreeMap::new();
        if let Some((cinst, models_file)) = &bake_paths {
            if let Err(error) = set_world_instances(engine, holder, cinst, models_file) {
                problems.push(format!(
                    "the World Instances component ({error}): the instance bake is written, the engine library cannot draw it yet"
                ));
            }
            for class in &build.classes {
                placed.insert(class.id.clone(), class.instance_count);
            }
        } else {
            for (class_index, class) in build.classes.iter().enumerate() {
                for instance in build.instances_of(class_index) {
                    let key = if class.unique || !class.params.is_empty() {
                        ModelKey::Cell(instance.cell)
                    } else {
                        ModelKey::Variant(instance.variant)
                    };
                    let parts = models
                        .get(&(class_index, key))
                        .map(Vec::as_slice)
                        .unwrap_or(&[]);
                    match place_world_instance(engine, holder, &class.id, instance, parts) {
                        Ok(()) => *placed.entry(class.id.clone()).or_default() += 1,
                        Err(error) => {
                            if problems.len() < 3 {
                                problems.push(error);
                            }
                        }
                    }
                }
            }
        }

        self.entities = super::projectloader::read_entities(self.engine.as_ref().expect("engine"));
        if let Some(position) = self.entities.iter().position(|e| e.id == holder) {
            self.select_index(position);
        }
        self.refresh_components();
        self.commit(format!("Build world {name}"), None);
        self.worldgen.size = size;
        if self.worldgen.region == [0.0; 4] {
            self.worldgen.region = [0.0, 0.0, size[0], size[1]];
        }
        self.worldgen.last_build = Some(LastBuild {
            corner,
            instances: build
                .classes
                .iter()
                .enumerate()
                .flat_map(|(index, class)| {
                    build
                        .instances_of(index)
                        .iter()
                        .map(move |i| (class.id.clone(), i.cell, i.position))
                })
                .collect(),
        });

        let counts: Vec<String> = placed.iter().map(|(id, n)| format!("{n} {id}")).collect();
        let total: u32 = placed.values().sum();
        let mut summary = format!(
            "Built World {name}: {total} instance{} ({}) in {:.2} s, {}; {}",
            if total == 1 { "" } else { "s" },
            counts.join(", "),
            started.elapsed().as_secs_f32(),
            if hit { "cache hit" } else { "cache miss" },
            match &bake_paths {
                Some((cinst, _)) => format!("instance bake {cinst}"),
                None => "one entity per instance".to_owned(),
            }
        );
        if baked > 0 {
            summary.push_str(&format!(
                "; {baked} model{} baked",
                if baked == 1 { "" } else { "s" }
            ));
        }
        if lakes_placed > 0 {
            summary.push_str(&format!(
                "; {lakes_placed} lake{} placed",
                if lakes_placed == 1 { "" } else { "s" }
            ));
        }
        if rivers_placed > 0 {
            summary.push_str(&format!(
                "; {rivers_placed} river{} placed",
                if rivers_placed == 1 { "" } else { "s" }
            ));
        }
        if rivers > 0 {
            summary.push_str(&format!(
                "; {rivers} river{} carved into the terrain but not drawn as water (the engine library has no water surface grid)",
                if rivers == 1 { "" } else { "s" }
            ));
        }
        if !problems.is_empty() {
            summary.push_str(&format!("; problems: {}", problems.join(" | ")));
        }
        Ok(summary)
    }

    /// Appends `override remove` for every instance of the last build inside `region`, then rebuilds.
    pub(super) fn remove_world_in_region(&mut self, region: [f32; 4]) {
        self.worldgen.region = region;
        let Some(last) = &self.worldgen.last_build else {
            self.worldgen.status = Some(
                "Build the world first: the region removes instances of the last build".to_owned(),
            );
            return;
        };
        let (x0, x1) = (region[0].min(region[2]), region[0].max(region[2]));
        let (z0, z1) = (region[1].min(region[3]), region[1].max(region[3]));
        let inside: Vec<(String, [i32; 2])> = last
            .instances
            .iter()
            .filter(|(_, _, p)| p[0] >= x0 && p[0] <= x1 && p[2] >= z0 && p[2] <= z1)
            .map(|(class, cell, _)| (class.clone(), *cell))
            .collect();
        self.remove_world_instances(inside, "in the region");
    }

    /// Instances of the last build within `radius` metres of `center` (metres from the terrain's corner).
    fn world_instances_in_circle(&self, center: [f32; 2], radius: f32) -> Vec<(String, [i32; 2])> {
        let Some(last) = &self.worldgen.last_build else {
            return Vec::new();
        };
        last.instances
            .iter()
            .filter(|(_, _, p)| {
                let (dx, dz) = (p[0] - center[0], p[2] - center[1]);
                dx * dx + dz * dz <= radius * radius
            })
            .map(|(class, cell, _)| (class.clone(), *cell))
            .collect()
    }

    /// One stroke of the Remove instances brush, or the agent's circle: every instance of the last build within
    /// `radius` of `center`.
    pub(super) fn remove_world_in_circle(&mut self, center: [f32; 2], radius: f32) {
        if self.worldgen.last_build.is_none() {
            self.worldgen.status = Some(
                "Build the world first: the brush removes instances of the last build".to_owned(),
            );
            return;
        }
        let inside = self.world_instances_in_circle(center, radius);
        self.remove_world_instances(inside, "under the brush");
    }

    /// The brush went over `world` (a point on the terrain, world space): remember what is under it.
    pub(super) fn world_brush_dab(&mut self, world: [f32; 3]) {
        let Some(corner) = self.worldgen.last_build.as_ref().map(|b| b.corner) else {
            return;
        };
        let center = [world[0] - corner[0], world[2] - corner[2]];
        for hit in self.world_instances_in_circle(center, self.worldgen.brush_radius) {
            if !self.worldgen.stroke.contains(&hit) {
                self.worldgen.stroke.push(hit);
            }
        }
    }

    /// The end of a brush stroke: everything it went over is removed as one undo step, then the world rebuilds.
    pub(super) fn finish_world_stroke(&mut self) {
        let stroke = std::mem::take(&mut self.worldgen.stroke);
        if !stroke.is_empty() {
            self.remove_world_instances(stroke, "under the brush");
        }
    }

    /// Turns the viewport's Remove instances brush on or off; its ring is the terrain brush cursor.
    pub(super) fn set_world_brush(&mut self, on: bool, radius: f32) {
        self.worldgen.brush = on;
        self.worldgen.brush_radius = radius.clamp(0.5, 200.0);
        self.worldgen.stroke.clear();
        self.set_terrain_brush_cursor(on, self.worldgen.brush_radius);
        self.worldgen.status = Some(if on {
            "Remove brush on: drag over the world in the Viewport to remove what grows there"
                .to_owned()
        } else {
            "Remove brush off".to_owned()
        });
    }

    /// Writes `override remove` for `cells` (one undo step in the tab), then rebuilds the world.
    fn remove_world_instances(&mut self, cells: Vec<(String, [i32; 2])>, place: &str) {
        let mut added = 0;
        for (class, cell) in cells {
            let exists =
                self.worldgen.comp.overrides.iter().any(|o| {
                    o.class == class && o.cell == cell && matches!(o.op, OverrideOp::Remove)
                });
            if !exists && self.worldgen.comp.class(&class).is_some() {
                self.worldgen.comp.overrides.push(Override {
                    op: OverrideOp::Remove,
                    class,
                    cell,
                });
                added += 1;
            }
        }
        if added == 0 {
            self.worldgen.status = Some(format!("No instance of the last build is {place}"));
            return;
        }
        self.worldgen.push(None);
        self.refresh_world_problems();
        self.build_world();
        let built = self.worldgen.status.clone().unwrap_or_default();
        self.worldgen.status = Some(format!(
            "Removed {added} instance{} {place}. {built}",
            if added == 1 { "" } else { "s" }
        ));
    }

    /// Saves the built world tree as `World/<name>.prefab`, linked to its `.generator.world` (saved first).
    pub(super) fn save_world_prefab(&mut self) {
        let result = (|| -> Result<String, String> {
            let name = self.worldgen.name.clone();
            let holder = self
                .world_holder(&name)
                .ok_or("build the world first: the prefab is the World entity's tree")?;
            let generator = self.save_world()?;
            let dir = self.world_dir().ok_or("open a project first")?;
            let slug = nature::slug(&name);
            let slug = if slug.is_empty() {
                "world".to_owned()
            } else {
                slug
            };
            let path = dir.join(format!("{slug}.prefab"));
            let prefab = self.capture_prefab(holder, &name, &generator)?;
            crate::uikit::prefab_file::write(&path, &prefab)?;
            self.refresh_assets();
            Ok(format!("World/{slug}.prefab"))
        })();
        self.worldgen.status = Some(match result {
            Ok(path) => {
                self.log(LogLevel::Action, format!("Saved world prefab {path}"));
                format!("Saved {path} (it remembers its .generator.world)")
            }
            Err(error) => format!("Could not save the prefab: {error}"),
        });
    }
}

/// A small project for tests and the headless `world` fixture: a 128 m hills terrain at 129², two rock recipes and
/// the composition `World/Valley.generator.world` with two classes. Returns the composition's path.
#[cfg(any(test, feature = "headless"))]
pub(crate) fn write_world_fixture(root: &Path) -> Result<PathBuf, String> {
    let preset = terrain::presets()
        .into_iter()
        .find(|p| p.name == "Rolling Hills")
        .ok_or("no Rolling Hills preset")?;
    let mut graph = preset.graph();
    let output = graph.output_node().ok_or("the preset has no Output")?;
    for (key, value) in [
        ("size_x", 128.0),
        ("size_z", 128.0),
        ("max_height", 12.0),
        ("resolution", 0.0),
    ] {
        graph.set(output, key, value);
    }
    recipe::write(
        &root
            .join("Terrain")
            .join(recipe::file_name("Hills", Kind::Terrain)),
        &Recipe::new(
            Kind::Terrain,
            "Hills",
            "",
            terrain::to_text("Hills", &graph),
        ),
    )?;
    for (stem, preset) in [("Rock", "Low-poly Rock"), ("Stone", "Cut Stone")] {
        let preset = nature::presets()
            .into_iter()
            .find(|p| p.name == preset)
            .ok_or_else(|| format!("no {preset} preset"))?;
        let text = nature::to_text(stem, &preset.graph(), preset.quality);
        recipe::write(
            &root
                .join("Nature")
                .join(recipe::file_name(stem, Kind::Nature)),
            &Recipe::new(Kind::Nature, stem, "", text),
        )?;
    }
    let comp = "seed 7
terrain Terrain/Hills.generator.terrain
class rock ref Nature/Rock.generator.nature variants 2 priority 30 cell 12 radius 2 density 0.6
  slope 0 0 0.6 0.9
class stone ref Nature/Stone.generator.nature variants 2 priority 20 cell 16 radius 2 density 0.5
";
    let path = root
        .join("World")
        .join(recipe::file_name("Valley", Kind::World));
    let mut world = Recipe::new(Kind::World, "Valley", "", comp.to_owned());
    world.generator_version = 2;
    recipe::write(&path, &world)?;
    Ok(path)
}

/// A world that looks like one, for the documentation images and the headless `world_showcase` fixture: a 160 m
/// highland basin (hills round a valley), a river that follows the flow and a lake in the basin, oaks and pines with
/// their leaves, boulders, shrubs and grass. A build is a few thousand instances, so it takes the large path (an
/// instance bake drawn by one `World Instances` entity). Returns the composition's path.
#[cfg(any(test, feature = "headless"))]
pub(crate) fn write_world_showcase(root: &Path) -> Result<PathBuf, String> {
    let preset = terrain::presets()
        .into_iter()
        .find(|p| p.name == "Highland Lake")
        .ok_or("no Highland Lake preset")?;
    let mut graph = preset.graph();
    let output = graph.output_node().ok_or("the preset has no Output")?;
    for (key, value) in [
        ("size_x", 160.0),
        ("size_z", 160.0),
        ("max_height", 26.0),
        ("resolution", 0.0),
    ] {
        graph.set(output, key, value);
    }
    recipe::write(
        &root
            .join("Terrain")
            .join(recipe::file_name("Highlands", Kind::Terrain)),
        &Recipe::new(
            Kind::Terrain,
            "Highlands",
            "",
            terrain::to_text("Highlands", &graph),
        ),
    )?;
    for (stem, preset) in [
        ("Oak", "Oak"),
        ("Pine", "Pine"),
        ("Boulder", "Boulder"),
        ("Shrub", "Shrub"),
        ("Grass", "Grass Clump"),
    ] {
        let preset = nature::presets()
            .into_iter()
            .find(|p| p.name == preset)
            .ok_or_else(|| format!("no {preset} preset"))?;
        let text = nature::to_text(stem, &preset.graph(), preset.quality);
        recipe::write(
            &root
                .join("Nature")
                .join(recipe::file_name(stem, Kind::Nature)),
            &Recipe::new(Kind::Nature, stem, "", text),
        )?;
    }
    for (stem, preset) in [("River", "River"), ("Lake", "Calm Lake")] {
        let preset = crate::uikit::water::presets()
            .into_iter()
            .find(|p| p.name == preset)
            .ok_or_else(|| format!("no {preset} preset"))?;
        let text = crate::uikit::water::to_text(stem, &preset.graph());
        recipe::write(
            &root
                .join("Water")
                .join(recipe::file_name(stem, Kind::Water)),
            &Recipe::new(Kind::Water, stem, "", text),
        )?;
    }
    let comp = "seed 2024
water_distance 24
terrain Terrain/Highlands.generator.terrain
water Water/River.generator.water follow flow min 0.75 carve 1.2
water Water/Lake.generator.water fill basins level 0.3
class boulder ref Nature/Boulder.generator.nature variants 3 priority 40 cell 14 radius 2 density 0.5 scale 0.6 1.5 tilt 0.6 sink 0.2
  slope 0.25 0.4 1 1
class oak ref Nature/Oak.generator.nature variants 3 priority 30 cell 10 radius 3.5 density 0.55 scale 0.7 1.2 tilt 0.05 tint 0.08
  slope 0 0 0.25 0.45
  water_distance 0.05 0.15 0.7 0.9
class pine ref Nature/Pine.generator.nature variants 3 priority 25 cell 9 radius 3 density 0.5 scale 0.7 1.3 tint 0.06
  slope 0 0 0.35 0.55
  height 0.35 0.5 1 1
class shrub ref Nature/Shrub.generator.nature variants 2 priority 20 cell 5 radius 1.2 density 0.35 scale 0.7 1.3 tint 0.1
  slope 0 0 0.4 0.6
  water_distance 0.02 0.08 1 1
class grass ref Nature/Grass.generator.nature variants 2 priority 10 cell 2.2 radius 0.5 density 0.55 scale 0.7 1.4 tilt 0.3 tint 0.15
  slope 0 0 0.35 0.5
  water_distance 0.02 0.05 1 1
";
    let path = root
        .join("World")
        .join(recipe::file_name("Highlands", Kind::World));
    let mut world = Recipe::new(Kind::World, "Highlands", "", comp.to_owned());
    world.generator_version = 2;
    recipe::write(&path, &world)?;
    Ok(path)
}

/// A water body of a build, ready to place: the water of its recipe over the body's bounds.
pub(super) struct WaterSpec {
    pub water: doppio_engine::Water,
    pub material: Option<String>,
    /// Centre of the bounds and the surface's base height, metres from the terrain's corner.
    pub center: [f32; 3],
    pub lake: bool,
    /// The shaped surface: resolution, heights relative to `center`, coverage and flow.
    pub grid: Option<([u32; 2], Vec<f32>, Vec<u8>, Vec<f32>)>,
}

/// The recipe of a water body evaluated (its waves and look), at level 0.
fn water_look(root: &Path, reference: &str) -> Option<(doppio_engine::Water, Option<String>)> {
    let recipe = recipe::read(&root.join(reference)).ok()?;
    let (_, graph) = crate::uikit::water::from_text(&recipe.graph)?;
    let (mut water, _) = graph.build_report().ok()?;
    water.level = 0.0;
    Some((water, graph.look_asset()))
}

/// The water entity of a body. With the engine's surface grid (`shaped`), a river or a lake is a surface over the
/// grid's bounds with the grid's heights (relative to the mean wet height), coverage and flow. Without it a lake is
/// sized to its bounding box (plus a vertex of margin) at its mean surface height, and a river is `None` (it is only
/// carved). `None` too for an empty body or a recipe that does not evaluate.
fn water_spec(
    root: &Path,
    body: &doppio_engine::WorldWaterBody,
    res: usize,
    size: [f32; 2],
    shaped: bool,
) -> Option<WaterSpec> {
    if let (true, Some(grid)) = (shaped, &body.surface_grid) {
        let [x0, z0, x1, z1] = grid.bounds;
        if x1 <= x0 || z1 <= z0 || grid.heights.is_empty() {
            return None;
        }
        let wet: Vec<f32> = grid
            .heights
            .iter()
            .zip(&grid.coverage)
            .filter(|(_, c)| **c > 0)
            .map(|(h, _)| *h)
            .collect();
        if wet.is_empty() {
            return None;
        }
        let base = (wet.iter().map(|h| f64::from(*h)).sum::<f64>() / wet.len() as f64) as f32;
        let (mut water, material) = water_look(root, &body.reference)?;
        water.size = [x1 - x0, z1 - z0];
        return Some(WaterSpec {
            water,
            material,
            center: [(x0 + x1) * 0.5, base, (z0 + z1) * 0.5],
            lake: body.lake,
            grid: Some((
                grid.resolution,
                grid.heights.iter().map(|h| h - base).collect(),
                grid.coverage.clone(),
                grid.flow.clone(),
            )),
        });
    }
    if !body.lake {
        return None;
    }
    let res = res.max(2);
    let (mut x0, mut x1, mut z0, mut z1) = (usize::MAX, 0, usize::MAX, 0);
    let (mut sum, mut count) = (0.0f64, 0usize);
    for (index, covered) in body.coverage.iter().enumerate() {
        if *covered == 0 {
            continue;
        }
        let (x, z) = (index % res, index / res);
        x0 = x0.min(x);
        x1 = x1.max(x);
        z0 = z0.min(z);
        z1 = z1.max(z);
        sum += f64::from(body.surface.get(index).copied().unwrap_or(0.0));
        count += 1;
    }
    if count == 0 {
        return None;
    }
    let step = [size[0] / (res - 1) as f32, size[1] / (res - 1) as f32];
    let (mut water, material) = water_look(root, &body.reference)?;
    water.size = [
        ((x1 - x0) as f32 + 2.0) * step[0],
        ((z1 - z0) as f32 + 2.0) * step[1],
    ];
    Some(WaterSpec {
        water,
        material,
        center: [
            (x0 + x1) as f32 * 0.5 * step[0],
            (sum / count as f64) as f32,
            (z0 + z1) as f32 * 0.5 * step[1],
        ],
        lake: true,
        grid: None,
    })
}

/// A body of water under the world holder, as the Water Generator places water: an engine water surface plus its
/// material, then its shape when it has a grid.
fn place_water(
    engine: &mut doppio_engine::Engine,
    holder: u32,
    name: &str,
    spec: &WaterSpec,
) -> Result<(), String> {
    let id = engine
        .create_entity(name)
        .map_err(|e| format!("create: {e:?}"))?;
    engine
        .set_entity_parent(id, Some(holder))
        .map_err(|e| format!("parent: {e:?}"))?;
    engine
        .set_position(id, spec.center)
        .map_err(|e| format!("position: {e:?}"))?;
    engine
        .set_water(id, &spec.water)
        .map_err(|e| format!("water ({e:?}): the engine library needs C ABI v7"))?;
    if let Some(material) = &spec.material {
        engine
            .apply_material(id, material)
            .map_err(|e| format!("material: {e:?}"))?;
    }
    if let Some((resolution, heights, coverage, flow)) = &spec.grid {
        engine
            .set_water_surface(
                id,
                *resolution,
                heights,
                Some(coverage.as_slice()),
                Some(flow.as_slice()),
            )
            .map_err(|e| format!("surface: {e:?}"))?;
    }
    Ok(())
}

/// Destroys an entity and everything under it, children first.
fn destroy_tree(engine: &mut doppio_engine::Engine, root: u32) {
    let mut parents = Vec::new();
    for index in 0..engine.entity_count() {
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
    for id in order.into_iter().rev() {
        let _ = engine.destroy_entity(id);
    }
}

/// The `World Instances` component of a large world: the bake, its models and how it draws.
fn set_world_instances(
    engine: &mut doppio_engine::Engine,
    holder: u32,
    cinst: &str,
    models: &str,
) -> Result<(), String> {
    let component = super::add_component(engine, holder, "World Instances")?;
    super::set_component_field(
        engine,
        holder,
        component,
        "Bake",
        doppio_engine::FieldValue::String(cinst.to_owned()),
    )?;
    super::set_component_field(
        engine,
        holder,
        component,
        "Models",
        doppio_engine::FieldValue::String(models.to_owned()),
    )?;
    super::set_component_field(
        engine,
        holder,
        component,
        "Draw distance",
        doppio_engine::FieldValue::F32(0.0),
    )?;
    super::set_component_field(
        engine,
        holder,
        component,
        "Cast shadows",
        doppio_engine::FieldValue::Bool(true),
    )
}

/// One instance of a small world: an entity under the holder with its transform and one child per model part, each
/// with its material like a composite Scatter part (the first slot is the part's own material, `Material slots`
/// gives every sub-mesh its own when there are several).
fn place_world_instance(
    engine: &mut doppio_engine::Engine,
    holder: u32,
    class: &str,
    instance: &doppio_engine::WorldInstance,
    parts: &[ModelPart],
) -> Result<(), String> {
    let id = engine
        .create_entity(&format!(
            "{class} {} {}",
            instance.cell[0], instance.cell[1]
        ))
        .map_err(|e| format!("create: {e:?}"))?;
    engine
        .set_entity_parent(id, Some(holder))
        .map_err(|e| format!("parent: {e:?}"))?;
    engine
        .set_position(id, instance.position)
        .map_err(|e| format!("position: {e:?}"))?;
    engine
        .set_rotation(id, world::instance_euler_degrees(instance.up, instance.yaw))
        .map_err(|e| format!("rotation: {e:?}"))?;
    let s = instance.scale;
    engine
        .set_scale(id, [s, s, s])
        .map_err(|e| format!("scale: {e:?}"))?;
    let tint = [instance.tint[0], instance.tint[1], instance.tint[2], 1.0];
    for (number, part) in parts.iter().enumerate() {
        let child = engine
            .create_entity(&format!("{class} part {}", number + 1))
            .map_err(|e| format!("create part: {e:?}"))?;
        engine
            .set_entity_parent(child, Some(id))
            .map_err(|e| format!("parent part: {e:?}"))?;
        let filter = super::add_component(engine, child, "Mesh Filter")?;
        let custom = super::enum_field(engine, "Mesh Filter", "Primitive", "Custom")?;
        super::set_component_field(engine, child, filter, "Primitive", custom)?;
        super::set_component_field(
            engine,
            child,
            filter,
            "Mesh",
            doppio_engine::FieldValue::String(part.mesh.clone()),
        )?;
        let renderer = super::add_component(engine, child, "Mesh Renderer")?;
        if let Some(first) = part.slots.split(';').map(str::trim).find(|s| !s.is_empty()) {
            engine
                .apply_material(child, first)
                .map_err(|e| format!("material {first}: {e:?}"))?;
        }
        if part.slots.contains(';') {
            super::set_component_field(
                engine,
                child,
                filter,
                "Material slots",
                doppio_engine::FieldValue::String(part.slots.clone()),
            )?;
        }
        super::set_component_field(
            engine,
            child,
            renderer,
            "Tint",
            doppio_engine::FieldValue::Color(tint),
        )?;
        if part.cutout {
            super::set_component_field(
                engine,
                child,
                renderer,
                "Cast shadows",
                doppio_engine::FieldValue::Bool(false),
            )?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn variant_seeds_match_the_nature_pool() {
        // `nature_variants` derives the same seeds, so a pool class and a parameter bucket share their models.
        for index in 0..4 {
            let seed = variant_seed(77, index);
            assert!((1..=9_999).contains(&seed));
        }
        assert_ne!(variant_seed(77, 0), variant_seed(77, 1));
    }

    #[test]
    fn the_library_lists_recipes_and_skips_hidden_folders() {
        let dir = std::env::temp_dir().join(format!("doppio-world-lib-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(dir.join("Nature")).unwrap();
        std::fs::create_dir_all(dir.join(".cache")).unwrap();
        std::fs::write(dir.join("Nature/Oak.generator.nature"), "{}").unwrap();
        std::fs::write(dir.join(".cache/Old.generator.nature"), "{}").unwrap();
        std::fs::write(dir.join("Valley.generator.world"), "{}").unwrap();
        let items = scan_library(&dir);
        assert_eq!(items.len(), 1);
        assert_eq!(items[0].path, "Nature/Oak.generator.nature");
        assert_eq!(items[0].kind, Kind::Nature);
        let _ = std::fs::remove_dir_all(&dir);
    }
}

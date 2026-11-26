//! `generator.*` recipe files: the editable source of anything a generator produced.
//!
//! A generator (material, texture, nature, water, terrain) bakes a finished asset such as
//! a `.mat`, a `.png`, an `.obj` or a height map. The graph that made it used to be lost
//! or hidden in a sidecar. A recipe is that graph as a first-class, typed file placed
//! next to its product:
//!
//! ```text
//! Textures/Bark.png                  ← product
//! Textures/Bark.generator.texture    ← recipe (open it to keep editing)
//! ```
//!
//! Other generators reference **recipes**, never the finished model, so a vegetation
//! generator can re-run a tree recipe and adapt the result instead of placing a mesh that
//! was baked once. Assets that were imported have no recipe and cannot be reopened.
//!
//! The file is JSON with a small header; `graph` holds the generator's own text form, so
//! each generator keeps owning its format.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

/// Magic string and current version of the file.
pub const FORMAT: &str = "caffeine-generator";
pub const VERSION: u32 = 1;
/// Version of the algorithm a recipe was authored with (hashing and placement). Recipes written
/// before the field existed read as 1; the core `Caffeine::Core::GENERATOR_VERSION` is 2.
pub const GENERATOR_VERSION: u32 = 2;

fn legacy_generator_version() -> u32 {
    1
}

/// Which generator a recipe belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Kind {
    Material,
    Texture,
    Nature,
    Water,
    Terrain,
    /// A World Composer composition (`Valley.generator.world`): terrain, water and classes of things.
    World,
}

impl Kind {
    pub const ALL: [Kind; 6] = [
        Kind::Material,
        Kind::Texture,
        Kind::Nature,
        Kind::Water,
        Kind::Terrain,
        Kind::World,
    ];

    /// Last extension segment: `material` in `Rock.generator.material`.
    pub fn extension(self) -> &'static str {
        match self {
            Kind::Material => "material",
            Kind::Texture => "texture",
            Kind::Nature => "nature",
            Kind::Water => "water",
            Kind::Terrain => "terrain",
            Kind::World => "world",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Kind::Material => "Material Generator",
            Kind::Texture => "Texture Generator",
            Kind::Nature => "Nature Generator",
            Kind::Water => "Water Generator",
            Kind::Terrain => "Terrain Generator",
            Kind::World => "World Composer",
        }
    }

    fn from_extension(extension: &str) -> Option<Kind> {
        Kind::ALL
            .into_iter()
            .find(|kind| kind.extension() == extension)
    }

    /// Older single-purpose graph files this kind used before recipes existed.
    fn legacy_extensions(self) -> &'static [&'static str] {
        match self {
            Kind::Material => &["mgraph"],
            Kind::Texture => &["texgraph"],
            Kind::Nature => &["nature"],
            Kind::Water => &["water"],
            Kind::Terrain => &["terrain-graph"],
            Kind::World => &[],
        }
    }
}

/// One recipe in memory.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Recipe {
    format: String,
    version: u32,
    kind: String,
    pub name: String,
    /// Product path relative to the recipe's folder (empty when not baked yet).
    pub product: String,
    /// The generator's own text form of the graph.
    pub graph: String,
    /// Algorithm version the recipe was authored with (1 when the file predates the field).
    #[serde(default = "legacy_generator_version")]
    pub generator_version: u32,
    /// Every file this recipe wrote besides `product` (models, materials, textures, the prefab), relative to the
    /// assets root. Empty for generators that write a single file.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub products: Vec<String>,
}

impl Recipe {
    pub fn new(kind: Kind, name: &str, product: &str, graph: String) -> Self {
        Self {
            format: FORMAT.to_owned(),
            version: VERSION,
            kind: kind.extension().to_owned(),
            name: name.to_owned(),
            product: product.to_owned(),
            graph,
            generator_version: GENERATOR_VERSION,
            products: Vec::new(),
        }
    }

    /// The same recipe, listing every file it wrote.
    pub fn with_products(mut self, products: Vec<String>) -> Self {
        self.products = products;
        self
    }

    pub fn kind(&self) -> Option<Kind> {
        Kind::from_extension(&self.kind)
    }

    pub fn to_text(&self) -> String {
        serde_json::to_string_pretty(self).unwrap_or_default() + "\n"
    }

    pub fn from_text(text: &str) -> Result<Self, String> {
        let recipe: Recipe =
            serde_json::from_str(text).map_err(|error| format!("not a generator file: {error}"))?;
        if recipe.format != FORMAT {
            return Err(format!("unknown format `{}`", recipe.format));
        }
        if recipe.version > VERSION {
            return Err(format!(
                "written by a newer Doppio (version {} > {VERSION})",
                recipe.version
            ));
        }
        if recipe.kind().is_none() {
            return Err(format!("unknown generator kind `{}`", recipe.kind));
        }
        Ok(recipe)
    }
}

/// `Rock.generator.material` for stem `Rock`.
pub fn file_name(stem: &str, kind: Kind) -> String {
    format!("{stem}.generator.{}", kind.extension())
}

/// The generator kind of a `*.generator.<kind>` path, if it is one.
pub fn kind_of(path: &Path) -> Option<Kind> {
    let name = path.file_name()?.to_str()?.to_ascii_lowercase();
    let (stem, extension) = name.rsplit_once('.')?;
    stem.ends_with(".generator")
        .then(|| Kind::from_extension(extension))?
}

pub fn is_recipe(path: &Path) -> bool {
    kind_of(path).is_some()
}

/// Stem of a recipe or product: `Rock` for `Rock.generator.material` and `Rock.mat`.
fn stem_of(path: &Path) -> Option<String> {
    let name = path.file_name()?.to_str()?;
    if is_recipe(path) {
        let (head, _) = name.rsplit_once('.')?;
        return Some(head.strip_suffix(".generator").unwrap_or(head).to_owned());
    }
    Some(path.file_stem()?.to_str()?.to_owned())
}

pub fn write(path: &Path, recipe: &Recipe) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)
            .map_err(|error| format!("{}: {error}", parent.display()))?;
    }
    std::fs::write(path, recipe.to_text()).map_err(|error| format!("{}: {error}", path.display()))
}

/// Read a recipe. A legacy graph file (`.mgraph`, `.texgraph`, ...) is accepted and wrapped
/// so old projects keep opening; saving then writes the new file next to it.
pub fn read(path: &Path) -> Result<Recipe, String> {
    let text =
        std::fs::read_to_string(path).map_err(|error| format!("{}: {error}", path.display()))?;
    if is_recipe(path) {
        return Recipe::from_text(&text);
    }
    let extension = path
        .extension()
        .and_then(|extension| extension.to_str())
        .unwrap_or_default();
    let kind = Kind::ALL
        .into_iter()
        .find(|kind| kind.legacy_extensions().contains(&extension))
        .ok_or_else(|| format!("{} is not a generator file", path.display()))?;
    let name = stem_of(path).unwrap_or_default();
    Ok(Recipe::new(kind, &name, "", text))
}

/// The recipe that produced `product`, looked up next to it. Imported assets have none.
pub fn recipe_for_product(product: &Path) -> Option<PathBuf> {
    if is_recipe(product) {
        return None;
    }
    let directory = product.parent()?;
    let stem = stem_of(product)?;
    for kind in Kind::ALL {
        let current = directory.join(file_name(&stem, kind));
        if current.is_file() {
            return Some(current);
        }
    }
    // Multi-part products (`oak_trunk.obj`, `alpine_range_height.png`) belong to the nature
    // or terrain recipe whose stem prefixes theirs.
    if let Ok(entries) = std::fs::read_dir(directory) {
        let mut best: Option<(usize, PathBuf)> = None;
        for entry in entries.flatten() {
            let candidate = entry.path();
            if !matches!(kind_of(&candidate), Some(Kind::Nature | Kind::Terrain)) {
                continue;
            }
            let Some(recipe_stem) = stem_of(&candidate) else {
                continue;
            };
            if stem.starts_with(&format!("{recipe_stem}_"))
                && best
                    .as_ref()
                    .is_none_or(|(len, _)| recipe_stem.len() > *len)
            {
                best = Some((recipe_stem.len(), candidate));
            }
        }
        if let Some((_, path)) = best {
            return Some(path);
        }
    }
    // Legacy sidecars only count for the product type they belonged to.
    let extension = product
        .extension()
        .and_then(|extension| extension.to_str())
        .unwrap_or_default()
        .to_ascii_lowercase();
    let legacy: &[(&str, &str)] = &[
        ("mat", "mgraph"),
        ("png", "texgraph"),
        ("obj", "nature"),
        ("mat", "water"),
        ("png", "terrain-graph"),
    ];
    legacy
        .iter()
        .filter(|(product_extension, _)| *product_extension == extension)
        .map(|(_, legacy)| directory.join(format!("{stem}.{legacy}")))
        .find(|candidate| candidate.is_file())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("doppio-recipe-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).expect("temp dir");
        dir
    }

    #[test]
    fn file_names_and_kinds_round_trip() {
        for kind in Kind::ALL {
            let name = file_name("Rock", kind);
            assert!(name.starts_with("Rock.generator."));
            assert_eq!(kind_of(Path::new(&name)), Some(kind));
        }
        assert_eq!(kind_of(Path::new("Rock.material")), None);
        assert_eq!(kind_of(Path::new("Rock.mat")), None);
        assert_eq!(kind_of(Path::new("generator.material")), None);
        assert_eq!(
            stem_of(Path::new("A/Rock.generator.nature")).as_deref(),
            Some("Rock")
        );
    }

    #[test]
    fn a_recipe_survives_write_and_read() {
        let dir = temp("roundtrip");
        let path = dir.join(file_name("Bark", Kind::Texture));
        let recipe = Recipe::new(
            Kind::Texture,
            "Bark",
            "Bark.png",
            "graph text\nline 2".into(),
        );
        write(&path, &recipe).expect("write");
        assert_eq!(read(&path).expect("read"), recipe);
        assert_eq!(read(&path).unwrap().kind(), Some(Kind::Texture));
    }

    #[test]
    fn bad_and_future_files_are_refused_with_a_reason() {
        assert!(Recipe::from_text("nonsense").is_err());
        let mut recipe = Recipe::new(Kind::Water, "W", "", String::new());
        recipe.version = VERSION + 1;
        let error = Recipe::from_text(&recipe.to_text()).unwrap_err();
        assert!(error.contains("newer Doppio"));
    }

    #[test]
    fn products_find_their_recipe_and_imports_do_not() {
        let dir = temp("find");
        std::fs::write(dir.join("Rock.mat"), "m").unwrap();
        std::fs::write(dir.join("Imported.mat"), "m").unwrap();
        assert!(recipe_for_product(&dir.join("Rock.mat")).is_none());
        std::fs::write(dir.join(file_name("Rock", Kind::Material)), "{}").unwrap();
        assert_eq!(
            recipe_for_product(&dir.join("Rock.mat")),
            Some(dir.join("Rock.generator.material"))
        );
        assert!(recipe_for_product(&dir.join("Imported.mat")).is_none());
        // A recipe is not a product of another recipe.
        assert!(recipe_for_product(&dir.join("Rock.generator.material")).is_none());
    }

    #[test]
    fn multi_part_products_find_the_nature_and_terrain_recipe() {
        let dir = temp("parts");
        std::fs::write(dir.join("oak_trunk.obj"), "o").unwrap();
        std::fs::write(dir.join("oak_leaves.obj"), "o").unwrap();
        std::fs::write(dir.join("oak.generator.nature"), "{}").unwrap();
        std::fs::write(dir.join("alpine_height.png"), "p").unwrap();
        std::fs::write(dir.join("alpine.generator.terrain"), "{}").unwrap();
        for product in ["oak_trunk.obj", "oak_leaves.obj"] {
            assert_eq!(
                recipe_for_product(&dir.join(product)),
                Some(dir.join("oak.generator.nature"))
            );
        }
        assert_eq!(
            recipe_for_product(&dir.join("alpine_height.png")),
            Some(dir.join("alpine.generator.terrain"))
        );
        // A texture recipe does not claim `_`-suffixed files.
        std::fs::write(dir.join("bark.generator.texture"), "{}").unwrap();
        std::fs::write(dir.join("bark_normal.png"), "p").unwrap();
        assert!(recipe_for_product(&dir.join("bark_normal.png")).is_none());
    }

    #[test]
    fn legacy_graph_files_are_still_found_and_wrapped() {
        let dir = temp("legacy");
        std::fs::write(dir.join("Old.png"), "p").unwrap();
        std::fs::write(dir.join("Old.texgraph"), "legacy graph").unwrap();
        let found = recipe_for_product(&dir.join("Old.png")).expect("legacy sidecar");
        let recipe = read(&found).expect("wrapped");
        assert_eq!(recipe.kind(), Some(Kind::Texture));
        assert_eq!(recipe.graph, "legacy graph");
        // A texgraph does not belong to a .mat of the same name.
        std::fs::write(dir.join("Old.mat"), "m").unwrap();
        assert!(recipe_for_product(&dir.join("Old.mat")).is_none());
    }
}

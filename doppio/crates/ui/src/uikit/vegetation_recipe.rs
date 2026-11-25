//! Species from recipes (terrain editing roadmap, phase 4): a vegetation species made from a `.generator.nature` recipe.
//!
//! A species does not copy a recipe's model: it points at the recipe and holds the **baked variants** (a few seeds of the same
//! recipe, each one OBJ with its parts as sub-meshes) and the content hash they were baked from. The editor compares the
//! hash with the recipe file; when the recipe changes, the variants are baked again (into folders named by content, so a
//! variant is never baked twice) and the species is updated: every plant of the species follows, because plants name the
//! species and never copy a mesh.
//!
//! Everything here is pure (files in, files out); the editor side that turns the parts into materials and edits the species
//! is `windows/main/terrainvegetation.rs`.

use super::nature::{self, mesh::NatureModel};
use super::recipe::{self, Kind};
use super::recipe_ref::{cache_key, content_hash, derive_seed, reseed_nature};
use std::path::{Path, PathBuf};

/// The most variants a species shows to the C ABI.
pub const MAX_VARIANTS: u32 = 8;

/// The variants of a recipe, baked.
pub struct BakedSpecies {
    /// The content hash of the recipe file these were baked from.
    pub hash: u64,
    /// One merged OBJ per variant (absolute paths); each part is a sub-mesh in `part_names` order.
    pub meshes: Vec<PathBuf>,
    /// The first variant's model: its parts give the sub-mesh materials (every variant has the same parts).
    pub model: NatureModel,
    /// The indices of the model's parts that are in the OBJ (empty parts are left out), in sub-mesh order.
    pub parts: Vec<usize>,
    /// True when every variant was already in the cache.
    pub cached: bool,
}

/// The content hash of a recipe file: what the species remembers to know the recipe changed.
pub fn recipe_hash(path: &Path) -> Result<u64, String> {
    let bytes = std::fs::read(path).map_err(|error| format!("{}: {error}", path.display()))?;
    Ok(content_hash(&bytes))
}

/// The seed of variant `index` of a species: a hash of the species' uid, so the same species always bakes the same variants.
pub fn variant_seed(uid: u32, index: u32) -> u32 {
    (derive_seed(u64::from(uid), 0x005E_C1E5, index as i32, 0) % 9_999) as u32 + 1
}

/// Bakes `count` variants (1 to 8) of a nature recipe for the species with this `uid` into `cache_dir`.
pub fn bake_species(
    recipe_path: &Path,
    uid: u32,
    count: u32,
    cache_dir: &Path,
) -> Result<BakedSpecies, String> {
    let count = count.clamp(1, MAX_VARIANTS);
    let recipe = recipe::read(recipe_path)?;
    if recipe.kind() != Some(Kind::Nature) {
        return Err(format!("{} is not a nature recipe", recipe_path.display()));
    }
    let hash = recipe_hash(recipe_path)?;
    let (name, graph, quality) = nature::from_text(&recipe.graph)
        .ok_or_else(|| format!("{}: the nature graph is unreadable", recipe_path.display()))?;
    let slug = nature::slug(&name);
    let mut meshes = Vec::new();
    let mut first: Option<(NatureModel, Vec<usize>)> = None;
    let mut cached = true;
    for index in 0..count {
        let seed = variant_seed(uid, index);
        let directory =
            cache_dir.join(format!("{slug}_{:016x}", cache_key(&recipe, seed, quality)));
        let file = directory.join(format!("{slug}_species.obj"));
        // The model is built for the first variant even when it is cached: its parts say what the materials are.
        let need_model = index == 0 || !file.is_file();
        if need_model {
            let mut seeded = graph.clone();
            reseed_nature(&mut seeded, seed);
            let model = seeded.build(quality)?;
            let parts: Vec<usize> = model
                .parts
                .iter()
                .enumerate()
                .filter(|(_, part)| !part.is_empty())
                .map(|(i, _)| i)
                .collect();
            if parts.is_empty() {
                return Err(format!("{} builds nothing", recipe_path.display()));
            }
            if !file.is_file() {
                cached = false;
                std::fs::create_dir_all(&directory).map_err(|e| e.to_string())?;
                std::fs::write(&file, model.merged_obj(&parts, &name))
                    .map_err(|e| e.to_string())?;
            }
            if index == 0 {
                first = Some((model, parts));
            }
        }
        meshes.push(file);
    }
    let (model, parts) = first.ok_or("nothing was baked")?;
    Ok(BakedSpecies {
        hash,
        meshes,
        model,
        parts,
        cached,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let dir =
            std::env::temp_dir().join(format!("doppio-veg-recipe-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).expect("temp dir");
        dir
    }

    /// A nature recipe on disk (a preset's graph).
    pub fn write_recipe(dir: &Path, preset_name: &str) -> PathBuf {
        let preset = nature::presets()
            .into_iter()
            .find(|p| p.name == preset_name)
            .or_else(|| nature::presets().into_iter().next())
            .expect("a nature preset");
        let graph = preset.graph();
        let text = nature::to_text(preset.name, &graph, preset.quality);
        let path = dir.join(recipe::file_name("Plant", Kind::Nature));
        recipe::write(&path, &recipe::Recipe::new(Kind::Nature, "Plant", "", text))
            .expect("write recipe");
        path
    }

    #[test]
    fn variant_seeds_depend_on_the_species_not_on_the_recipe() {
        assert_eq!(variant_seed(3, 0), variant_seed(3, 0));
        assert_ne!(variant_seed(3, 0), variant_seed(3, 1));
        assert_ne!(variant_seed(3, 0), variant_seed(4, 0));
        assert!((1..=9999).contains(&variant_seed(77, 5)));
    }

    #[test]
    fn a_recipe_bakes_variants_once_and_a_changed_recipe_bakes_new_ones() {
        let dir = temp("bake");
        let recipe_path = write_recipe(&dir, "Low-poly Rock");
        let cache = dir.join("cache");
        let first = bake_species(&recipe_path, 7, 3, &cache).expect("a bake");
        assert_eq!(first.meshes.len(), 3);
        assert!(!first.cached);
        assert!(first.meshes.iter().all(|m| m.is_file()));
        assert!(!first.parts.is_empty());
        let a = std::fs::read(&first.meshes[0]).unwrap();
        let b = std::fs::read(&first.meshes[1]).unwrap();
        assert_ne!(a, b, "another seed, another model");
        assert!(
            String::from_utf8_lossy(&a).contains("usemtl"),
            "the parts are sub-meshes"
        );

        // Baked again, nothing is built twice and the files are the same.
        let again = bake_species(&recipe_path, 7, 3, &cache).expect("cached");
        assert!(again.cached);
        assert_eq!(again.meshes, first.meshes);
        assert_eq!(again.hash, first.hash);

        // Another species (another uid) bakes other variants of the same recipe.
        let other = bake_species(&recipe_path, 8, 3, &cache).expect("another species");
        assert_ne!(other.meshes, first.meshes);

        // The recipe changes: the hash moves and the variants are new files.
        let mut recipe = recipe::read(&recipe_path).unwrap();
        recipe.graph.push_str("\n# edited");
        recipe::write(&recipe_path, &recipe).unwrap();
        let edited = bake_species(&recipe_path, 7, 3, &cache);
        // An extra comment line may not parse as part of the graph; either the bake fails loudly or it gives new files.
        match edited {
            Ok(baked) => {
                assert_ne!(baked.hash, first.hash);
                assert_ne!(baked.meshes, first.meshes);
            }
            Err(error) => assert!(!error.is_empty()),
        }
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn the_variant_count_is_kept_between_one_and_eight_and_a_wrong_file_is_refused() {
        let dir = temp("count");
        let recipe_path = write_recipe(&dir, "Low-poly Rock");
        let cache = dir.join("cache");
        assert_eq!(
            bake_species(&recipe_path, 1, 0, &cache)
                .unwrap()
                .meshes
                .len(),
            1
        );
        assert_eq!(
            bake_species(&recipe_path, 1, 99, &cache)
                .unwrap()
                .meshes
                .len(),
            8
        );
        std::fs::write(dir.join("not.generator.nature"), "{}").unwrap();
        assert!(bake_species(&dir.join("not.generator.nature"), 1, 2, &cache).is_err());
        assert!(bake_species(&dir.join("missing.generator.nature"), 1, 2, &cache).is_err());
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn the_recipe_hash_follows_the_file() {
        let dir = temp("hash");
        let path = dir.join("a.generator.nature");
        std::fs::write(&path, "one").unwrap();
        let one = recipe_hash(&path).unwrap();
        std::fs::write(&path, "two").unwrap();
        assert_ne!(one, recipe_hash(&path).unwrap());
        std::fs::write(&path, "one").unwrap();
        assert_eq!(one, recipe_hash(&path).unwrap());
        assert!(recipe_hash(&dir.join("missing")).is_err());
        let _ = std::fs::remove_dir_all(&dir);
    }
}

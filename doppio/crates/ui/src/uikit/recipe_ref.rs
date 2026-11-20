//! Using recipes from other generators.
//!
//! A generator that needs a material, a texture or a plant points at the **recipe**
//! (`Oak.generator.nature`), not at a mesh baked once. This module turns such a reference into
//! files on demand and caches them by content, so a vegetation or scatter generator can ask
//! for "oak, variant 17" and get the same model every time, on every machine.
//!
//! * [`derive_seed`] and [`unit`] are SplitMix64, bit for bit the same as
//!   `Caffeine::Core::deriveSeed` / `toUnit` in the engine (`src/caffeine/core/Hash.hpp`);
//!   the golden tests of both languages pin the same numbers.
//! * [`bake_nature_variant`] reseeds every node of a nature recipe from a variant seed and bakes
//!   the OBJ parts into a cache folder named by the content hash.
//! * [`resolve_product`] gives a material or texture slot the finished file of a recipe,
//!   baking a texture recipe when its picture is missing.

use std::path::{Path, PathBuf};

use super::generators::TextureMap;
use super::nature::{self, Quality};
use super::recipe::{self, Kind, Recipe};
use super::texture_graph::{self, BitDepth};

const GOLDEN: u64 = 0x9E37_79B9_7F4A_7C15;

/// SplitMix64 finaliser (bijective).
pub fn split_mix64(x: u64) -> u64 {
    let mut x = x.wrapping_add(GOLDEN);
    x = (x ^ (x >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    x = (x ^ (x >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    x ^ (x >> 31)
}

/// Order-dependent combination of two values.
pub fn hash_combine(a: u64, b: u64) -> u64 {
    split_mix64(
        a ^ split_mix64(b)
            .wrapping_add(GOLDEN)
            .wrapping_add(a << 6)
            .wrapping_add(a >> 2),
    )
}

/// A child seed for a purpose (`salt`) at integer cell coordinates.
pub fn derive_seed(parent: u64, salt: u32, x: i32, z: i32) -> u64 {
    let h = hash_combine(parent, u64::from(salt));
    let h = hash_combine(h, u64::from(x as u32));
    hash_combine(h, u64::from(z as u32))
}

/// Uniform number in `[0, 1)` from the top 53 bits.
pub fn unit(hash: u64) -> f64 {
    (hash >> 11) as f64 * (1.0 / 9_007_199_254_740_992.0)
}

/// FNV-1a over bytes, then mixed: a stable content hash that does not depend on the platform.
pub fn content_hash(bytes: &[u8]) -> u64 {
    let mut hash = 0xcbf2_9ce4_8422_2325u64;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    split_mix64(hash)
}

/// Cache key of a baked variant: the recipe's graph and algorithm version, the variant seed and
/// the quality. Editing the recipe changes the key, so stale products are never reused.
pub fn cache_key(recipe: &Recipe, seed: u32, quality: Quality) -> u64 {
    let mut key = content_hash(recipe.graph.as_bytes());
    key = hash_combine(key, u64::from(recipe.generator_version));
    key = hash_combine(key, u64::from(seed));
    hash_combine(key, quality.index() as u64)
}

/// One baked variant in the cache.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Baked {
    pub key: u64,
    pub seed: u32,
    pub directory: PathBuf,
    /// OBJ files (one per part), in part order.
    pub files: Vec<String>,
    /// True when the files already existed.
    pub cached: bool,
}

/// Reseeds a nature graph for a variant. Seed `0` keeps the authored seeds; any other value moves
/// every node's `seed` parameter by a hash of the variant seed and the node id.
pub fn reseed_nature(graph: &mut nature::graph::NatureGraph, seed: u32) {
    if seed == 0 {
        return;
    }
    let ids: Vec<_> = graph.nodes.iter().map(|node| node.id).collect();
    for id in ids {
        let Some(base) = graph.get(id, "seed") else {
            continue;
        };
        let offset = (derive_seed(u64::from(seed), 0x5EED, id as i32, 0) % 9_999) as f32;
        graph.set(id, "seed", (base + 1.0 + offset) % 10_000.0);
    }
}

/// Bakes variant `seed` of a nature recipe into `cache_dir`, or returns the cached files.
pub fn bake_nature_variant(
    recipe_path: &Path,
    seed: u32,
    quality: Option<Quality>,
    cache_dir: &Path,
) -> Result<Baked, String> {
    let recipe = recipe::read(recipe_path)?;
    if recipe.kind() != Some(Kind::Nature) {
        return Err(format!("{} is not a nature recipe", recipe_path.display()));
    }
    let (name, mut graph, authored) = nature::from_text(&recipe.graph)
        .ok_or_else(|| format!("{}: the nature graph is unreadable", recipe_path.display()))?;
    let quality = quality.unwrap_or(authored);
    let key = cache_key(&recipe, seed, quality);
    let directory = cache_dir.join(format!("{}_{key:016x}", nature::slug(&name)));
    let marker = directory.join(".baked");
    if marker.is_file() {
        let files = std::fs::read_to_string(&marker)
            .map_err(|error| error.to_string())?
            .lines()
            .map(str::to_owned)
            .collect();
        return Ok(Baked {
            key,
            seed,
            directory,
            files,
            cached: true,
        });
    }
    reseed_nature(&mut graph, seed);
    let model = graph.build(quality)?;
    let files = nature::write_obj_files(&model, &directory, &name)?;
    std::fs::write(&marker, files.join("\n")).map_err(|error| error.to_string())?;
    Ok(Baked {
        key,
        seed,
        directory,
        files,
        cached: false,
    })
}

/// `count` variants of a nature recipe, with seeds derived from `base_seed` (deterministic).
pub fn nature_variants(
    recipe_path: &Path,
    base_seed: u64,
    count: u32,
    quality: Option<Quality>,
    cache_dir: &Path,
) -> Result<Vec<Baked>, String> {
    let mut variants = Vec::new();
    for index in 0..count {
        let seed = (derive_seed(base_seed, 0xBA5E, index as i32, 0) % 9_999) as u32 + 1;
        variants.push(bake_nature_variant(recipe_path, seed, quality, cache_dir)?);
    }
    Ok(variants)
}

/// The finished file a slot should use for a reference to a recipe, relative to `root`.
/// A texture recipe is baked beside itself when its picture is missing; a material recipe must
/// have been baked once in the Material Generator, which is reported otherwise.
pub fn resolve_product(root: &Path, reference: &str, wanted: Kind) -> Result<String, String> {
    let recipe_path = root.join(reference);
    let Some(kind) = recipe::kind_of(&recipe_path) else {
        return Ok(reference.to_owned());
    };
    if kind != wanted {
        return Err(format!(
            "{reference} is a {} recipe, but this slot needs a {} recipe",
            kind.extension(),
            wanted.extension()
        ));
    }
    let recipe = recipe::read(&recipe_path)?;
    let directory = recipe_path.parent().unwrap_or(root);
    let product = if recipe.product.is_empty() {
        match kind {
            Kind::Texture => format!("{}.png", recipe.name),
            Kind::Material => format!("{}.mat", recipe.name),
            _ => String::new(),
        }
    } else {
        recipe.product.clone()
    };
    let product_path = directory.join(&product);
    if !product_path.is_file() {
        match kind {
            Kind::Texture => bake_texture_product(&recipe, &product_path)?,
            Kind::Material => {
                return Err(format!(
                    "{reference}: bake the material once (open the recipe, press Bake) so {product} exists"
                ))
            }
            _ => return Err(format!("{reference} has no product to use here")),
        }
    }
    let relative = product_path
        .strip_prefix(root)
        .map(|path| path.to_string_lossy().replace('\\', "/"))
        .unwrap_or_else(|_| product_path.to_string_lossy().into_owned());
    Ok(relative)
}

fn bake_texture_product(recipe: &Recipe, target: &Path) -> Result<(), String> {
    let graph = texture_graph::from_text(&recipe.graph)
        .ok_or_else(|| format!("the texture graph of {} is unreadable", recipe.name))?;
    let baked = graph.bake_texture_with(&TextureMap::new(), None, None)?;
    texture_graph::export_png(&baked, target, BitDepth::Eight)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let dir =
            std::env::temp_dir().join(format!("doppio-recipe-ref-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).expect("temp dir");
        dir
    }

    fn oak_recipe(dir: &Path) -> PathBuf {
        let preset = nature::presets()
            .into_iter()
            .find(|preset| preset.name == "Low-poly Rock")
            .or_else(|| nature::presets().into_iter().next())
            .expect("a nature preset");
        let graph = preset.graph();
        let text = nature::to_text(preset.name, &graph, preset.quality);
        let path = dir.join(recipe::file_name("Rock", Kind::Nature));
        recipe::write(&path, &Recipe::new(Kind::Nature, "Rock", "", text)).expect("write recipe");
        path
    }

    /// The same numbers as `tests/test_hash.cpp`: the engine and the editor must agree.
    #[test]
    fn hashing_matches_the_engine_golden_values() {
        assert_eq!(split_mix64(0), 0xe220_a839_7b1d_cdaf);
        assert_eq!(split_mix64(1), 0x910a_2dec_8902_5cc1);
        assert_eq!(split_mix64(u64::MAX), 0xe4d9_7177_1b65_2c20);
        assert_eq!(derive_seed(42, 7, 3, -5), 0x9fd0_188a_c08d_7c44);
        assert_eq!(derive_seed(42, 7, -5, 3), 0xbfb8_f192_698b_81d0);
        assert_eq!(derive_seed(0, 0, 0, 0), 0x488f_4fa3_326f_de8b);
        assert!((unit(derive_seed(42, 7, 3, -5)) - 0.624_269_040_942_229).abs() < 1e-15);
    }

    #[test]
    fn new_recipes_carry_the_generator_version_and_old_ones_read_as_one() {
        let recipe = Recipe::new(Kind::Texture, "Bark", "Bark.png", "graph".to_owned());
        assert_eq!(recipe.generator_version, recipe::GENERATOR_VERSION);
        let text = recipe.to_text();
        assert!(text.contains("generator_version"));
        let legacy = text
            .lines()
            .filter(|line| !line.contains("generator_version"))
            .collect::<Vec<_>>()
            .join("\n")
            .replace("\"graph\": \"graph\",", "\"graph\": \"graph\"");
        let parsed = Recipe::from_text(&legacy).expect("legacy file parses");
        assert_eq!(parsed.generator_version, 1);
    }

    #[test]
    fn variants_are_deterministic_cached_and_different() {
        let dir = temp("variants");
        let recipe = oak_recipe(&dir);
        let cache = dir.join("cache");
        let a = bake_nature_variant(&recipe, 5, None, &cache).expect("variant 5");
        assert!(!a.cached && !a.files.is_empty());
        let again = bake_nature_variant(&recipe, 5, None, &cache).expect("cached");
        assert!(again.cached);
        assert_eq!(a.key, again.key);
        assert_eq!(a.files, again.files);
        let other = bake_nature_variant(&recipe, 6, None, &cache).expect("variant 6");
        assert_ne!(a.key, other.key);
        let obj =
            |baked: &Baked| std::fs::read(baked.directory.join(&baked.files[0])).expect("obj");
        assert_ne!(obj(&a), obj(&other), "another seed gives another model");
        let seeds = nature_variants(&recipe, 99, 4, None, &cache).expect("variants");
        let again = nature_variants(&recipe, 99, 4, None, &cache).expect("variants");
        assert_eq!(seeds.len(), 4);
        assert!(again.iter().all(|baked| baked.cached));
        assert_eq!(
            seeds.iter().map(|b| b.key).collect::<Vec<_>>(),
            again.iter().map(|b| b.key).collect::<Vec<_>>()
        );
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn editing_the_recipe_invalidates_the_cache_key() {
        let dir = temp("invalidate");
        let path = oak_recipe(&dir);
        let mut recipe = recipe::read(&path).expect("recipe");
        let before = cache_key(&recipe, 3, Quality::Balanced);
        recipe.graph.push_str("\n# edited");
        assert_ne!(before, cache_key(&recipe, 3, Quality::Balanced));
        assert_ne!(before, cache_key(&recipe, 4, Quality::Balanced));
        assert_ne!(before, cache_key(&recipe, 3, Quality::Detailed));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_texture_slot_bakes_the_recipe_on_demand() {
        let dir = temp("texture-slot");
        let preset = texture_graph::presets().first().expect("a texture preset");
        let text = texture_graph::to_text(&preset.build());
        let path = dir.join(recipe::file_name("Leaf", Kind::Texture));
        recipe::write(&path, &Recipe::new(Kind::Texture, "Leaf", "Leaf.png", text)).unwrap();
        assert!(!dir.join("Leaf.png").exists());
        let resolved = resolve_product(&dir, "Leaf.generator.texture", Kind::Texture).unwrap();
        assert_eq!(resolved, "Leaf.png");
        assert!(dir.join("Leaf.png").is_file());
        // A plain path is returned as it is.
        assert_eq!(
            resolve_product(&dir, "Textures/a.png", Kind::Texture).unwrap(),
            "Textures/a.png"
        );
        // The wrong kind of recipe is refused with a clear message.
        let error = resolve_product(&dir, "Leaf.generator.texture", Kind::Material).unwrap_err();
        assert!(error.contains("texture recipe"), "{error}");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn a_material_slot_needs_a_baked_material() {
        let dir = temp("material-slot");
        let path = dir.join(recipe::file_name("Rock", Kind::Material));
        recipe::write(
            &path,
            &Recipe::new(Kind::Material, "Rock", "Rock.mat", "g".into()),
        )
        .unwrap();
        let error = resolve_product(&dir, "Rock.generator.material", Kind::Material).unwrap_err();
        assert!(error.contains("bake the material once"), "{error}");
        std::fs::write(dir.join("Rock.mat"), "x").unwrap();
        assert_eq!(
            resolve_product(&dir, "Rock.generator.material", Kind::Material).unwrap(),
            "Rock.mat"
        );
        let _ = std::fs::remove_dir_all(&dir);
    }
}

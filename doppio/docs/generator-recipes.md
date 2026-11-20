# Generator recipes (`generator.*`)

<div class="doc-meta">
  <span class="doc-badge doc-badge--reference">Reference</span>
  <span class="doc-badge">Doppio</span>
  <span>Last updated: <time datetime="2026-11-20">2026-11-20</time></span>
  <span>Source: <a href="https://github.com/devscafecommunity/caffeine/blob/main/doppio/crates/ui/src/uikit/recipe.rs"><code>recipe.rs</code></a></span>
</div>

<p class="doc-lede">A recipe is the editable graph behind anything a generator baked. It is a file
next to the product, so the source of a texture, material, tree, lake or terrain is never lost.</p>

## 1. File type

`<name>.generator.<kind>`, one kind per generator:

| File | Generator | Product beside it |
|---|---|---|
| `Bark.generator.texture` | Texture Generator | `Bark.png` |
| `Rock.generator.material` | Material Generator | `Rock.mat` |
| `Oak.generator.nature` | Nature Generator | `oak_trunk.obj`, `oak_leaves.obj` |
| `Lake.generator.water` | Water Generator | `lake.mat` |
| `Alpine.generator.terrain` | Terrain Generator | `alpine_height.png` |

The file is JSON: `format` (`caffeine-generator`), `version`, `kind`, `name`, `product`, `graph` (the generator's own text form)
and `generator_version`, the algorithm version the recipe was authored with (files without it read as 1; new files write 2,
the same number as `Caffeine::Core::GENERATOR_VERSION`). Each generator still owns its graph format; the recipe only wraps it.
A file written by a newer Doppio is refused with a message instead of being misread.
The engine lists these files with asset kind `generator`.

## 2. Behaviour

- Saving or exporting a generator writes its recipe beside the product.
- **Double-click** a recipe, or a baked asset that has one, to reopen the graph in its generator.
- Imported assets have no recipe, so double-click behaves as before (a picture becomes a new material).
  A MaterialCraft archive is the exception because it carries its graph.
- Older sidecars (`.mgraph`, `.texgraph`, `.nature`, `.water`, `.terrain-graph`) are still found and
  opened; the next save writes the new file.
- Multi-part products (`oak_trunk.obj`) find the Nature or Terrain recipe whose name prefixes theirs.

## 3. Folders

Folders the editor creates are capitalised: `Assets/Raw/{Animations,Audio,Generators,Materials,Models,Prefabs,Sky,TextureSets,Textures}`,
`Assets/Processed`, `Scripts`, `Scenes`, `Build`, and `Nature`, `Terrain`, `Water` for generator output.
Existing projects keep their own names; renaming is the user's choice.

## 4. caf-pack

`.generator.*` files are **sources**, not assets to pack. caf-pack now has an explicit rule
(`Registry::isEditorOnly`): recipes and the older graph sidecars (`.mgraph`, `.texgraph`, `.nature`, `.water`,
`.terrain-graph`) are skipped and listed (`Skipped editor-only source: ...`) instead of failing the package with
"No processor registered". `Packer::getSkipped()` returns the list. Covered by `tests/test_cafpack_editor_only.cpp`
(the change lives in the `caf-pack` submodule).

## 5. Referencing recipes

Generators point at recipes, not at finished models: a vegetation generator reruns an `Oak` recipe for
variants and adapts the result to the terrain. The resolver is `uikit/recipe_ref.rs`.

- [x] **Slots.** The Nature *Your Material (.mat)* and *Your Texture (PNG)* nodes and the Terrain layer slots accept a
      `generator.material` / `generator.texture` path. A texture recipe is baked beside itself when its PNG is missing;
      a material recipe must have been baked once (the error says so). A recipe of the wrong kind is refused with a message.
- [x] **Variants, cached.** `bake_nature_variant(recipe, seed, quality, cache_dir)` reseeds every node's `seed` from the
      variant seed and writes the OBJ parts into `<cache>/<name>_<key>/`; the key hashes the recipe's graph, its
      `generator_version`, the seed and the quality, so editing the recipe invalidates the cache. Seed 0 keeps the authored
      seeds. `nature_variants` derives N seeds from a base seed. CLI: `doppio-material variants Oak.generator.nature --out DIR --count 8 --seed 3`.
- [x] **Same numbers in both languages.** `derive_seed`/`unit` are SplitMix64, bit for bit `Caffeine::Core::deriveSeed`/`toUnit`;
      the golden tests in `tests/test_hash.cpp` and `recipe_ref.rs` pin the same values.
- [x] `generator_version` in every recipe.
- [ ] Water: its material comes from the Water Material node, which already writes `Water/<name>.mat`; a recipe slot is not wired.
- [ ] On-demand bake of a **material** recipe (needs the engine session the Material Generator uses).
- [ ] A scatter or vegetation *generator* that consumes `nature_variants` and places the results on a terrain.

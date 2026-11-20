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

The file is JSON: `format` (`caffeine-generator`), `version`, `kind`, `name`, `product` and `graph`,
the generator's own text form. Each generator still owns its graph format; the recipe only wraps it.
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

`.generator.*` files are **sources**, not assets to pack: caf-pack selects processors by extension
(`.png`, `.obj`, `.wav`) and no processor claims them, so they are skipped today. Making that an explicit
rule (ignore and list them as editor-only) is a change in the `caf-pack` repository and is still open.

## 5. Referencing recipes (not done)

Generators should point at recipes, not at finished models: a vegetation generator reruns an `Oak` recipe for
variants and adapts the result to the terrain. Planned steps:

- [ ] Material and texture slots in Nature, Terrain and Water accept a `generator.material` / `generator.texture` path.
- [ ] A resolver bakes the recipe on demand and caches the product by `(recipe hash, seed, quality)`.
- [ ] A scatter or vegetation generator takes `generator.nature` plus a seed and variant count.
- [ ] Recipes carry `generator_version` (see `photoreal-world-roadmap.md`, phase 0).

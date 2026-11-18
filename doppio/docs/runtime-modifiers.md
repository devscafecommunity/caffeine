# Run-time modifiers for generated materials

Textures and materials are generated, so the baked PNG is not the only product: the same material can
react to the world **without re-baking its maps**. A modifier is a few numbers the shader applies on top of
the material, so a script can wet a rock when it rains, cover a roof with snow or age a wall.

| Modifier | Range | Effect |
|---|---|---|
| **Tint** | colour, white = none | multiplies the albedo (season shift, team colour) |
| **Wetness** | 0 dry .. 1 soaked | darker albedo and a glossy film (low roughness) |
| **Snow** | 0 none .. 1 everything facing up | white, rough cover on upward faces; the more snow, the further down the slopes it reaches |
| **Age** | 0 new .. 1 old | washed-out, darker, rougher |

## Where they live

* In the **material** (`.mat` keys `mod_tint`, `mod_wetness`, `mod_snow`, `mod_age`; `MaterialSurface`,
  C ABI **v10**, the Material panel's *Run-time modifiers* section): the defaults a material ships with.
* On the **entity** (Mesh Renderer fields **Tint**, **Wetness**, **Snow**, **Age**, saved with the scene): the
  values a game changes at run time. They are ordinary reflected fields, so scripts read and write them like
  any other (`Mesh Renderer`, `Wetness`, 0..1), and so do the Inspector and the agent bridge.
* The renderer **combines** both: tints multiply, the amounts add and clamp to 0..1. Entities that share one
  material and one mesh still batch only when their modifiers are equal.

Scenes saved before the fields existed load with neutral modifiers.

## What is not here yet

* Modifiers that need new maps (damage decals, moss growing in cracks) still need the generator's graph at
  run time; the graph and its parameter schema are not shipped with the baked output yet.
* Foliage wind and translucency (`wind_*`, `translucency`) are material-only, not per entity.

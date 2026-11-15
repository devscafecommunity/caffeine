--! timeout_ms=90000
-- Material studio: open, preview, assign, presets and texture slots.

local presets = app:query("material_presets")
assert(#presets >= 10, "material presets should be exposed")

app:run_app_action("open_material", { path = "vegetation-kit/materials/ground.mat" })
app:sleep(900)
local ground = app:query("material")
assert(ground.name == "ground", "ground material should load")
assert(ground.preview, "material preview should render")
assert(ground.orm_map ~= "", "ground should expose its ORM map")

app:run_app_action("set_material_preview_shape", { shape = "cube" })
app:run_app_action("set_material_preview_exposure", { value = 1.5 })
app:sleep(800)
local preview = app:query("material")
assert(preview.preview_shape == 1, "preview shape should switch to cube")
assert(math.abs(preview.preview_exposure - 1.5) < 0.01, "preview exposure should update")
assert(preview.preview_pending == false, "async preview should settle")

app:run_app_action("select_entity", { name = "Terrain" })
app:run_app_action("apply_material")
app:sleep(400)
local components = app:query("entity_components")
local assigned = nil
for _, component in ipairs(components.components) do
  if component.name == "Mesh Filter" then
    for _, field in ipairs(component.fields) do
      if field.index == 4 then assigned = field.value end
    end
  end
end
assert(assigned ~= nil and assigned ~= "", "apply should set the Mesh Filter material")

local texture = "vegetation-kit/models/fir_sapling/textures/fir_sapling_branches_diff_1k.jpg"
app:run_app_action("open_material", { path = "vegetation-kit/materials/fir_sapling_branches.mat" })
app:sleep(800)
app:run_app_action("set_material_texture", { slot = "albedo", path = texture })
app:sleep(300)
local fir = app:query("material")
assert(fir.albedo_map:find("fir_sapling_branches_diff_1k"), "texture slot should accept an asset path")
assert(fir.dirty, "texture edits should mark the material dirty")

return { presets = #presets, assigned = assigned, albedo = fir.albedo_map }

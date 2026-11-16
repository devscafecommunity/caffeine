--! timeout_ms=60000
-- Prefab creator: palette + built-in mesh recipe.

local prefabs = app:query("prefabs")
assert(#prefabs.categories >= 4, "prefab categories should be exposed")
assert(#prefabs.items >= 10, "built-in prefabs should be exposed")

local initial = #app:query("entities")
app:run_app_action("create_prefab", { key = "cube" })
app:sleep(500)
local entities = app:query("entities")
local cube = entities[#entities]
local components = app:query("entity_components", { index = #entities - 1 })
local has_filter, has_renderer = false, false
for _, component in ipairs(components.components) do
  if component.name == "Mesh Filter" then has_filter = true end
  if component.name == "Mesh Renderer" then has_renderer = true end
end
assert(has_filter and has_renderer, "cube prefab should add mesh components")

app:run_app_action("create_prefab", { key = "duplicate" })
app:sleep(500)
local after = app:query("entities")
assert(#after == #entities + 1, "duplicate prefab should create one entity")

app:run_app_action("undo")
app:run_app_action("undo")
app:sleep(700)
local restored = app:query("entities")
assert(#restored == initial, "undo should remove the prefab and its duplicate")
local scene = app:query("scene")
for _, line in ipairs(scene.log) do
  assert(not string.find(line, "Viewport render failed"), "prefab undo must not strand a readback")
end

return { cube = cube.name, entities = #restored }

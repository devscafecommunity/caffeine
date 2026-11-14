--! timeout_ms=120000
-- Component schema + generic inspector agent flow.
-- Launch it against a real scene (the default dense_outdoor benchmark).

local catalog = app:query("component_types")
assert(#catalog > 0, "component_types should list the schema")

local function schema_named(name)
  for _, kind in ipairs(catalog) do
    if kind.name == name then return kind end
  end
  return nil
end

local function component_in(data, name)
  for index, component in ipairs(data.components) do
    if component.name == name then return index - 1 end
  end
  return nil
end

-- Prefer Transform; scenes authored with Position 3D use that instead.
local target_name = "Transform"
local transform = schema_named(target_name)
local transform_index = nil
for entity_index = 0, 20 do
  app:run_app_action("select_entity", { index = entity_index })
  transform_index = component_in(app:query("entity_components"), target_name)
  if transform_index ~= nil then break end
end
if transform_index == nil then
  target_name = "Position 3D"
  transform = schema_named(target_name)
  for entity_index = 0, 20 do
    app:run_app_action("select_entity", { index = entity_index })
    transform_index = component_in(app:query("entity_components"), target_name)
    if transform_index ~= nil then break end
  end
end
assert(transform ~= nil, "no transform schema")
assert(transform_index ~= nil, "no entity with " .. target_name)

local position_field = nil
for index, field in ipairs(transform.fields) do
  if field.name == "Position" then position_field = index - 1 end
end
assert(position_field ~= nil, "Position field missing")

app:run_app_action("set_component_field", {
  component = transform_index,
  field = position_field,
  kind = "vec3",
  value = { 7, 8, 9 },
})
local after = app:query("entity_components")
local position = after.components[transform_index + 1].fields[position_field + 1].value
assert(position[1] == 7 and position[2] == 8 and position[3] == 9, "field write failed")

app:run_app_action("undo")
local undone = app:query("entity_components")
local old = undone.components[transform_index + 1].fields[position_field + 1].value
assert(old[1] ~= 7, "undo should restore the old position")
app:run_app_action("redo")

local camera = schema_named("Camera 3D")
assert(camera ~= nil, "Camera 3D schema missing")
local before = #app:query("entity_components").components
app:run_app_action("add_component", { type_id = camera.id })
assert(#app:query("entity_components").components == before + 1, "add_component failed")
app:run_app_action("undo")
assert(#app:query("entity_components").components == before, "component add should be undoable")

return { types = #catalog, target = target_name, fields = #transform.fields }

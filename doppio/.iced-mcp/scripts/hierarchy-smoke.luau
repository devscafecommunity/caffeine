--! timeout_ms=60000
-- Hierarchy parent links: reparent, unparent, undo, drag and inherited transforms.

local listed = app:query("entities")
local schemas = app:query("component_types")

local function position_field(component_name)
  for _, kind in ipairs(schemas) do
    if kind.name == component_name then
      for index, field in ipairs(kind.fields) do
        if field.name == "Position" then return index - 1 end
      end
    end
  end
  return nil
end

-- Pick two entities that own a transform component; scene roots such as
-- RenderEnvironment only carry environment settings.
local function position_component(index)
  app:run_app_action("select_entity", { index = index })
  local data = app:query("entity_components")
  for component_index, component in ipairs(data.components) do
    local field_index = position_field(component.name)
    if field_index ~= nil then
      return {
        index = index,
        id = listed[index + 1].id,
        component = component_index - 1,
        field = field_index,
        value = component.fields[field_index + 1].value,
      }
    end
  end
  return nil
end

local positioned = {}
for index = 0, math.min(#listed - 1, 30) do
  local found = position_component(index)
  if found ~= nil then table.insert(positioned, found) end
  if #positioned >= 2 then break end
end
assert(#positioned >= 2, "need two entities with a position component")
local parent = positioned[1]
local child = positioned[2]

local function entity_by_id(id)
  for _, entity in ipairs(app:query("entities")) do
    if entity.id == id then return entity end
  end
  return nil
end

local function parent_of(id)
  local entity = entity_by_id(id)
  return entity and entity.parent
end

app:run_app_action("set_entity_parent", { entity_id = child.id, parent_id = parent.id })
assert(parent_of(child.id) == parent.id, "reparent failed")

app:run_app_action("set_entity_parent", { entity_id = child.id })
assert(parent_of(child.id) == nil, "unparent failed")

app:run_app_action("undo")
assert(parent_of(child.id) == parent.id, "undo should restore the parent link")

-- Dragging a tree row onto another row reparents it.
local drag_id = listed[2].id
local drop_id = listed[1].id
app:run_app_action("set_entity_parent", { entity_id = drag_id })
app:drag({ id = "hierarchy.list.entity_row.1", dx = 0, dy = -28, steps = 8 })
app:sleep(500)
assert(parent_of(drag_id) == drop_id, "dragging a tree row should reparent")

-- Group transform: moving the parent shifts the child in world space.
app:run_app_action("select_entity", { index = child.index })
local child_before = app:query("selection")
app:run_app_action("select_entity", { index = parent.index })
app:run_app_action("set_component_field", {
  component = parent.component,
  field = parent.field,
  kind = "vec3",
  value = { parent.value[1] + 3, parent.value[2], parent.value[3] },
})
app:run_app_action("select_entity", { index = child.index })
local child_after = app:query("selection")
local inherited = child_after.world_position[1] - child_before.world_position[1]
assert(math.abs(inherited - 3) < 0.001, "child should inherit the group transform")

return { parent = parent.id, child = child.id, inherited = inherited }

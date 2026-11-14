--! timeout_ms=60000
-- Hierarchy parent links: reparent, unparent, undo.

local listed = app:query("entities")
assert(#listed >= 2, "need at least two entities")
local parent_id = listed[1].id
local child_id = listed[2].id

app:run_app_action("set_entity_parent", { entity_id = child_id, parent_id = parent_id })
local function parent_of(id)
  for _, entity in ipairs(app:query("entities")) do
    if entity.id == id then return entity.parent end
  end
  return nil
end
assert(parent_of(child_id) == parent_id, "reparent failed")

app:run_app_action("set_entity_parent", { entity_id = child_id })
assert(parent_of(child_id) == nil, "unparent failed")

app:run_app_action("undo")
assert(parent_of(child_id) == parent_id, "undo should restore the parent link")

return { parent = parent_id, child = child_id }

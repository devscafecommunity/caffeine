--! timeout_ms=90000
-- End-to-end smoke flow for the session-based agent bridge.
-- Run with: iced-mcp run-script .iced-mcp/scripts/editor-smoke.luau
--           --launch "./target/debug/doppio ../assets/benchmarks/dense_outdoor.caf"
-- from doppio/ with ICED_MCP=1.

app:load_fixture("editor")
app:wait_for({ id = "entity-1" })

-- Select the second entity and rename it through the real text input.
app:click({ id = "entity-1" })
app:expect({ id = "entity-name", value = "Entity 2" })
app:type_text({ id = "entity-name", text = "Boss", clear = true, submit = true })

local selection = app:query("selection")
assert(selection ~= nil, "selection query returned nothing")
assert(selection.name == "Boss", "rename failed, got " .. tostring(selection.name))

-- Add an entity and move it through the scene-level action.
app:run_app_action("add_entity", { name = "Rock" })
app:wait_for({ text = "Rock", exact = false })
local scene = app:query("scene")
assert(scene.entity_count == 3, "expected 3 entities, got " .. tostring(scene.entity_count))

app:run_app_action("select_entity", { name = "Boss" })
app:run_app_action("set_camera", { distance = 20 })
local camera = app:query("selection")
assert(camera.name == "Boss", "select_entity by name failed")

local entities = app:query("entities")
assert(#entities == 3, "entities query should list 3 rows")
assert(app:find({ text = "Pause" }) == nil, "editor should start paused")

app:run_app_action("toggle_play")
app:expect({ text = "Pause" })

local scene_after_play = app:query("scene")
local logged_play = false
for _, line in ipairs(scene_after_play.log) do
  if line == "Play" then logged_play = true end
end
assert(logged_play, "toggle_play should log Play in the console")

local messages = app:messages({ limit = 5 })
assert(#messages > 0, "the bridge should log the app messages")

return {
  entities = #entities,
  selected = selection.name,
  camera_distance = 20,
}

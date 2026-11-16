--! timeout_ms=90000
-- Project Manager + Assets browser smoke flow.
-- Run from doppio/ with:
--   CAFFEINE_RECENT_PROJECTS=/tmp/opencode/doppio-smoke-recent.txt \
--     iced-mcp run-script .iced-mcp/scripts/project-smoke.luau \
--     --launch "./target/debug/doppio ../assets/benchmarks/dense_outdoor.caf"
-- Remove /tmp/opencode/doppio-project-smoke before rerunning so the create
-- step starts from a clean location.

app:run_app_action("open_project_manager")
app:sleep(300)
local opened = app:query("project")
assert(opened.open == true, "open_project_manager should show the dialog")
app:expect({ id = "project.manager.tab.create" })

local suffix = tostring(math.random(100000, 999999))
local location = "/tmp/opencode/doppio-project-smoke-" .. suffix
app:run_app_action("create_project", {
  name = "SmokeProject",
  location = location,
  template = "2D",
})
app:sleep(1500)
local created = app:query("project")
assert(created.open == false, "creating a project should close the manager")
assert(created.active ~= nil, "created project should be active")
assert(created.active.name == "SmokeProject", "unexpected project name")
assert(created.active.template == "2D", "template should be stored in the manifest")
assert(created.active.last_scene == "scenes/main.caf", "new projects get scenes/main.caf")

local scene = app:query("scene")
assert(scene.dirty == false, "a new project should start saved")
assert(scene.entity_count == 0, "a new project should start with an empty scene")

-- The asset root follows the project and starts with its standard folders.
local assets = app:query("project")
assert(assets.asset_count >= 7, "new project should expose its starter folders")
app:run_app_action("asset_navigate", { path = "Textures" })
app:sleep(200)
local textures = app:query("project")
assert(textures.asset_directory == "Textures", "asset navigation should enter Textures")
app:run_app_action("set_asset_view", { mode = "list" })
app:sleep(200)
assert(app:query("project").asset_view == "list", "list view should be selectable")
app:run_app_action("asset_root")
app:sleep(200)
assert(app:query("project").asset_directory == "", "root should clear the folder")
app:run_app_action("refresh_assets")
app:sleep(500)
app:screenshot({ name = "project-smoke-assets" })

-- Reopen the manager and confirm the project moved to the top of Recents.
app:run_app_action("open_project_manager")
app:click({ id = "project.manager.tab.recent" })
app:sleep(300)
local recent = app:query("project")
assert(recent.tab == "recent", "recent tab should be active")
assert(#recent.recent >= 1, "recent projects should include the new project")
assert(recent.recent[1].name == "SmokeProject", "new project should be the newest recent")
app:screenshot({ name = "project-smoke-recent" })
app:click({ id = "project.manager.close" })
assert(app:query("project").open == false, "close should dismiss the manager")

return {
  project = created.active.name,
  scene = scene.path,
  assets = assets.asset_count,
  recent = #recent.recent,
}

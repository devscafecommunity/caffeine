--! timeout_ms=60000
-- Viewport interaction flow: drag orbit, right-drag pan, wheel zoom, focus.
app:load_fixture("editor")
app:wait_idle()

local start = app:query("camera")
app:drag({ id = "viewport.canvas.surface", dx = 60, dy = 20, steps = 6 })
local orbited = app:query("camera")
assert(math.abs(orbited.yaw - start.yaw) > 0.05, "left drag should orbit yaw")
assert(math.abs(orbited.pitch - start.pitch) > 0.02, "left drag should orbit pitch")

app:scroll({ id = "viewport.canvas.surface", dy = -2 })
local zoomed = app:query("camera")
assert(zoomed.distance < orbited.distance, "wheel up should zoom in")

app:run_app_action("select_entity", { index = 1 })
local entity = app:query("selection")
app:click({ id = "editor.menu.view" })
app:click({ id = "viewport.camera.focus_selection" })
local focused = app:query("camera")
assert(focused.focus[1] == entity.position[1], "focus should target the selection X")
assert(focused.focus[2] == entity.position[2], "focus should target the selection Y")
assert(focused.focus[3] == entity.position[3], "focus should target the selection Z")

app:run_app_action("select_entity", { index = 1 })
app:run_app_action("set_gizmo_mode", { mode = "rotate" })
assert(app:query("gizmo").mode == "Rotate", "set_gizmo_mode should switch to rotate")
app:run_app_action("set_gizmo_mode", { mode = "move" })
assert(app:query("gizmo").mode == "Move", "set_gizmo_mode should switch back to move")

local viewport = app:query("viewport")
assert(viewport.grid == true, "reference grid should start enabled")
assert(viewport.anti_aliasing == "Fxaa", "FXAA should be the stable default")
assert(viewport.target_width >= 64 and viewport.target_height >= 36, "viewport should report a size")
assert(viewport.render_width >= 64, "viewport should render at a real resolution")
app:run_app_action("toggle_grid")
assert(app:query("viewport").grid == false, "toggle_grid should hide the grid")
app:run_app_action("toggle_grid")
assert(app:query("viewport").grid == true, "toggle_grid should restore the grid")

app:run_app_action("set_viewport", { anti_aliasing = "taa", render_scale = 1.25 })
local quality = app:query("viewport")
assert(quality.anti_aliasing == "Taa", "set_viewport should switch anti-aliasing")
assert(math.abs(quality.render_scale - 1.25) < 0.001, "set_viewport should set render scale")
app:run_app_action("set_viewport", { anti_aliasing = "fxaa", render_scale = 1.5 })

app:run_app_action("set_camera", { focus = { 0, 10000, 0 }, distance = 200 })
local high = app:query("camera")
assert(high.focus[2] == 10000, "camera focus must not be clamped to a height limit")
app:run_app_action("set_camera", { focus = { 0, 0, 0 }, distance = 10 })

local layout = app:query("layout")
assert(#layout.panes == 4, "layout should keep four pane groups")
local pages = 0
for _, pane in ipairs(layout.panes) do pages = pages + #pane.pages end
assert(pages == 8, "layout should keep every panel page")
app:click({ id = "editor.menu.view" })
app:click({ id = "editor.layout.viewport" })
assert(#app:query("layout").panes == 4, "viewport layout keeps every pane group")
app:click({ id = "editor.menu.view" })
app:click({ id = "editor.layout.default" })
assert(#app:query("layout").panes == 4, "default layout keeps every pane group")

-- Drag only the Assets tab into the Viewport pane, then split it back out.
app:drag({ x = 106, y = 77, dx = 164, dy = 0, steps = 10 })
app:sleep(500)
local moved = app:query("layout")
local merged = false
for _, pane in ipairs(moved.panes) do
  local assets, viewport = false, false
  for _, page in ipairs(pane.pages) do
    if page == "Assets" then assets = true end
    if page == "Viewport" then viewport = true end
  end
  if assets and viewport then merged = true end
end
assert(merged, "dragging a tab should move only that page")

app:click({ text = "◫", nth = 0 })
app:sleep(400)
assert(#app:query("layout").panes == 5, "the tab split button should create a pane")

return { yaw = focused.yaw, distance = focused.distance }

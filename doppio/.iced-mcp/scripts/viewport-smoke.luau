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

return { yaw = focused.yaw, distance = focused.distance }

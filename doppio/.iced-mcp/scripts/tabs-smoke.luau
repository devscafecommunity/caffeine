--! timeout_ms=60000
-- Tabs can be dragged independently from their header. Dragging a tab up
-- onto another pane's body groups it there; splitting a grouped tab creates
-- a separate pane.

app:load_fixture("editor")
app:wait_idle()
app:click({ id = "editor.menu.view" })
app:click({ id = "editor.layout.default" })
app:sleep(300)

app:drag({ id = "editor.pane.tab.console", to_x = 500, to_y = 300, steps = 12 })
app:sleep(500)

local layout = app:query("layout")
local merged = false
for _, pane in ipairs(layout.panes) do
  local console, viewport = false, false
  for _, page in ipairs(pane.pages) do
    if page == "Console" then console = true end
    if page == "Viewport" then viewport = true end
  end
  if console and viewport then merged = true end
end
assert(merged, "dragging a tab onto a pane body should group it there")
assert(#layout.panes == 3, "the empty Console pane should close after moving its last tab")

return { panes = #layout.panes }

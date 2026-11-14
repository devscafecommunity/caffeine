--! timeout_ms=120000
-- Visual regression for the Doppio panes.
--
-- Seed baselines once on the target machine/image:
--   iced-mcp run-script .iced-mcp/scripts/visual-regression.luau \
--     --launch "./target/debug/doppio ../assets/benchmarks/dense_outdoor.caf" \
--     --update-baselines
--
-- Then run without --update-baselines in CI. Baselines live in
-- .iced-mcp/baselines/ and are machine/GPU dependent; commit them only from
-- a controlled CI image (see scripts/check-doppio-visual.sh).

app:load_fixture("editor")
app:wait_idle()

local panes = {
  { id = "editor.pane.hierarchy", baseline = "pane-hierarchy" },
  { id = "editor.pane.assets", baseline = "pane-assets" },
  { id = "editor.pane.inspector", baseline = "pane-inspector" },
  { id = "editor.pane.console", baseline = "pane-console" },
}

for _, pane in ipairs(panes) do
  app:expect({ id = pane.id })
  app:screenshot_diff({ id = pane.id, baseline = pane.baseline })
end

return { panes = #panes }

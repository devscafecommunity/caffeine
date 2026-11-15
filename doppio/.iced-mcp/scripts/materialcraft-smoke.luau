--! timeout_ms=120000
-- MaterialCraft portable round-trip through the MCP actions.

app:run_app_action("open_material", { path = "vegetation-kit/materials/fir_sapling_branches.mat" })
app:sleep(600)
app:run_app_action("open_generator")
app:sleep(300)

local archive = "target/materialcraft-smoke.mcmat"
app:run_app_action("export_materialcraft", { path = archive })
app:sleep(1200)

app:run_app_action("import_materialcraft", { path = archive })
app:sleep(1200)
local graph = app:query("generator")
assert(graph.nodes ~= nil and #graph.nodes >= 2, "import should restore a graph")
assert(graph.links ~= nil and #graph.links >= 1, "import should restore graph links")
assert(graph.baking == nil, "import should not leave a bake running")

return { nodes = #graph.nodes, links = #graph.links }

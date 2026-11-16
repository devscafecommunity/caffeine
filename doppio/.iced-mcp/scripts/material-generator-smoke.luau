--! timeout_ms=90000
-- Material generator: node canvas, history, link editing, dots and clipboard.

app:run_app_action("open_material", { path = "vegetation-kit/materials/fir_sapling_branches.mat" })
app:sleep(500)
app:run_app_action("open_generator")
app:sleep(500)
local graph = app:query("generator")
assert(#graph.nodes >= 2, "generator should start with a graph")
assert(#graph.links >= 1, "default noise should feed the output")
assert(graph.can_undo == false, "a fresh graph has no history")

app:run_app_action("add_generator_node", { kind = "checker" })
app:sleep(250)
graph = app:query("generator")
local output, checker = nil, nil
for _, node in ipairs(graph.nodes) do
  if node.kind == "Output" then output = node.id end
  if node.kind == "Checker" then checker = node.id end
end
assert(output ~= nil and checker ~= nil, "checker and output nodes should exist")
app:run_app_action("connect_generator", { from = checker, to = output, input = 0 })
app:sleep(250)
graph = app:query("generator")
assert(graph.can_undo, "connect should create an undo step")
local function connected(from, to, input)
  for _, link in ipairs(graph.links) do
    if link.from == from and link.to == to and link.input == input then return true end
  end
  return false
end
assert(connected(checker, output, 0), "checker should feed the output colour input")

app:run_app_action("generator_undo")
app:sleep(250)
graph = app:query("generator")
assert(not connected(checker, output, 0), "undo should remove the connection")
app:run_app_action("generator_redo")
app:sleep(250)
graph = app:query("generator")
assert(connected(checker, output, 0), "redo should restore the connection")

app:run_app_action("insert_generator_dot", { to = output, input = 0, x = 120, y = 90 })
app:sleep(300)
graph = app:query("generator")
local dot = nil
for _, node in ipairs(graph.nodes) do
  if node.kind == "Dot" then dot = node.id end
end
assert(dot ~= nil, "double-clicking a wire should insert a reroute dot")
assert(#graph.links == 2, "a reroute dot splits the wire into two links")

app:run_app_action("delete_generator_link", { to = output, input = 0 })
app:sleep(250)
graph = app:query("generator")
assert(not connected(dot, output, 0), "delete_generator_link should remove the selected wire")
app:run_app_action("generator_undo")
app:sleep(250)
graph = app:query("generator")
assert(connected(dot, output, 0), "graph undo should restore the deleted wire")

local before_copy = #graph.nodes
app:run_app_action("copy_generator_node")
app:run_app_action("paste_generator_node")
app:sleep(250)
graph = app:query("generator")
assert(#graph.nodes == before_copy + 1, "copy/paste should add a node")

app:run_app_action("set_generator_snap", { enabled = true })
app:run_app_action("set_generator_search", { text = "noise" })
app:run_app_action("fit_generator")
app:sleep(250)
graph = app:query("generator")
assert(graph.snap == true and graph.search == "noise", "canvas options should update")
assert(graph.undo_name ~= nil, "the graph should expose its undo name")

-- Phase 2: texture nodes, packed output and typed ports.
app:run_app_action("add_generator_node", { kind = "image" })
app:run_app_action("add_generator_node", { kind = "voronoi" })
app:sleep(300)
graph = app:query("generator")
local image, voronoi = nil, nil
for _, node in ipairs(graph.nodes) do
  if node.kind == "Image" then image = node end
  if node.kind == "Voronoi" then voronoi = node end
end
assert(image ~= nil and #image.outputs == 1, "image node should expose a colour output")
assert(image.outputs[1].type == "color", "image output should be typed colour")
assert(voronoi ~= nil and voronoi.outputs[1].type == "color", "Voronoi should output colour")
app:run_app_action("set_generator_image", {
  node = image.id,
  path = "vegetation-kit/models/fir_sapling/textures/fir_sapling_branches_diff_1k.jpg",
})
app:run_app_action("set_generator_bits", { bits = 16 })
app:run_app_action("set_generator_packing_channel", { channel = "r", source = "metallic" })
app:sleep(300)
graph = app:query("generator")
assert(graph.packing == "Custom channels", "editing a channel should use custom packing")
app:run_app_action("set_generator_packing_preset", { preset = "hdrp" })
app:sleep(300)
graph = app:query("generator")
assert(graph.bits == 16, "bit depth should switch to 16-bit")
assert(graph.packing == "Unity HDRP mask", "packing preset should switch")
assert(graph.baking == nil, "no bake should be running after these edits")
assert(#graph.packing_channels == 4, "all packed channels should be exposed")

app:screenshot({ name = "material-generator-smoke" })
return { nodes = #graph.nodes, links = #graph.links, undo = graph.undo_name, bits = graph.bits }

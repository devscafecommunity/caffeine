--! timeout_ms=90000
-- Script Editor + Dryad Nodes end-to-end smoke flow.
-- Create the two throwaway inputs before running:
--   printf 'print("hi")\n' > /tmp/opencode/doppio-script-smoke.lua
--   rm -f /tmp/opencode/doppio-dryad-smoke.dyd /tmp/opencode/doppio-dryad-smoke.node.json
--   : > /tmp/opencode/doppio-dryad-smoke.dyd

app:run_app_action("open_script", { path = "/tmp/opencode/doppio-script-smoke.lua" })
app:sleep(500)
local script = app:query("script_editor")
assert(#script.documents == 1, "script editor should have one document")
assert(script.documents[1].title == "doppio-script-smoke.lua", "unexpected script tab")
assert(script.documents[1].dirty == false, "new script should start clean")
app:type_text({ id = "script.editor.code", text = "-- edited\n", clear = false })
app:sleep(250)
assert(app:query("script_editor").documents[1].dirty == true, "typing should mark the script dirty")
app:screenshot({ name = "script-dryad-smoke-script" })

app:run_app_action("open_dryad", { path = "/tmp/opencode/doppio-dryad-smoke.dyd" })
app:sleep(500)
local graph = app:query("dryad")
assert(graph.script_relative:find("doppio%-dryad%-smoke"), "Dryad should open the requested file")
app:run_app_action("dryad_add_motion")
app:sleep(300)
local motion = app:query("dryad")
assert(#motion.nodes >= 3, "motion example should add nodes")
assert(#motion.edges >= 2, "motion example should add wires")
app:run_app_action("dryad_save")
app:sleep(400)
assert(app:query("dryad").dirty == false, "saving should clear the Dryad dirty flag")
app:screenshot({ name = "script-dryad-smoke-nodes" })

app:run_app_action("dryad_attach")
app:run_app_action("dryad_run")
app:sleep(400)
local ran = app:query("dryad")
assert(ran.status ~= nil and ran.status:find("preview"), "Dryad run should report a preview")

return {
  script = script.documents[1].title,
  nodes = #motion.nodes,
  edges = #motion.edges,
  status = ran.status,
}

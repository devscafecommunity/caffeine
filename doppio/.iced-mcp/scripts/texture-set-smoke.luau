--! timeout_ms=90000
-- TextureCraft-style texture set: create, paint, undo and export maps.

app:run_app_action("new_texture_set")
app:sleep(300)
local set = app:query("texture_set")
assert(set ~= nil, "texture set should exist")
assert(set.tile_size > 0, "texture set should have a tile size")
assert(#set.tiles >= 1, "texture set should expose a UDIM tile")
assert(#set.layers >= 1, "texture set should start with a layer")
assert(set.channels[1].name == "baseColor", "base colour should be the first channel")

app:run_app_action("paint_texture_set", { tile = 1001, u = 0.4, v = 0.5 })
app:sleep(200)
app:run_app_action("paint_texture_set", { tile = 1001, u = 0.5, v = 0.5 })
app:sleep(200)
set = app:query("texture_set")
assert(set.can_undo == true, "painting should create tile undo history")
assert(set.dirty == true, "painting should mark the texture set dirty")

app:run_app_action("texture_set_undo")
app:sleep(200)
set = app:query("texture_set")
assert(set.can_redo == true, "undo should expose redo")

app:run_app_action("create_prefab", { key = "cube" })
app:sleep(400)
app:run_app_action("texture_set_bake_mesh_map", { kind = "normal" })
app:sleep(500)
app:run_app_action("texture_set_bake_mesh_map", { kind = "id" })
app:sleep(500)
set = app:query("texture_set")
assert(#set.layers >= 3, "mesh-map bakes should add layers")
assert(set.status:find("Baked") ~= nil, "mesh-map bake should report status")

app:run_app_action("export_texture_set", { path = "target/texture-set-smoke" })
app:sleep(500)
set = app:query("texture_set")
assert(set.status:find("Exported") ~= nil, "export should report written maps")
app:screenshot({ name = "texture-set-smoke" })

return { tile = set.active_tile, channels = #set.channels, layers = #set.layers }

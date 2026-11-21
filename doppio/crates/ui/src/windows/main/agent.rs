//! Agent bridge (`--features agent`).
//!
//! `iced-mcp` exposes the real widget tree to an MCP client, so an agent can
//! click, type, snapshot and record flows. On top of that we register the
//! scene-level operations that the widget tree cannot describe precisely:
//! entity inspection, camera control and the editor commands.

use super::{App, Message};
use crate::uikit::components::gizmo;
use crate::uikit::generators;
use crate::uikit::id;
use crate::uikit::panels;
use crate::uikit::panels::dryad_nodes;
use crate::uikit::panels::material;
use crate::uikit::panels::material_generator;
use crate::uikit::panels::nature_generator;
use crate::uikit::panels::project_manager;
use crate::uikit::panels::terrain_generator;
use crate::uikit::panels::texture_generator;
use crate::uikit::panels::texture_set as texture_set_panel;
use serde_json::{json, Value};

/// Runs Doppio with the MCP bridge attached when `ICED_MCP=1`.
pub fn run() -> iced::Result {
    let application = iced::application(App::new, App::update, App::view)
        .subscription(App::subscription)
        .theme(|_: &App| crate::uikit::theme::app_theme())
        .settings(crate::uikit::theme::settings())
        .window(crate::uikit::theme::window());

    iced_mcp::attach(application)
        .state(describe_state)
        .query("entities", "All entities with id, name, transform and selection.", |app, _| {
            Ok(entities_json(app))
        })
        .query("selection", "The selected entity, or null.", |app, _| {
            Ok(selection_json(app))
        })
        .query("scene", "Scene path, dirty flag and engine connection.", |app, _| {
            Ok(scene_json(app))
        })
        .query("layout", "Pane layout and maximize state.", |app, _| {
            let maximized = app.panes.maximized();
            Ok(json!({
                "panes": app.panes.iter().map(|(pane, group)| json!({
                    "active": format!("{:?}", group.active_kind()),
                    "pages": group.pages.iter().map(|kind| format!("{kind:?}")).collect::<Vec<_>>(),
                    "maximized": maximized == Some(*pane),
                })).collect::<Vec<_>>(),
                "maximized": maximized.map(|pane| format!("{pane:?}")),
                "tab_drag": app.tab_drag.map(|(_, page)| format!("{page:?}")),
                "tab_hover_pane": app.tab_hover_pane.map(|pane| format!("{pane:?}")),
            }))
        })
        .query(
            "camera",
            "Viewport camera yaw, pitch, distance and focus.",
            |app, _| {
                Ok(json!({
                    "yaw": app.camera.yaw,
                    "pitch": app.camera.pitch,
                    "distance": app.camera.distance,
                    "focus": app.camera.focus,
                }))
            },
        )
        .query(
            "viewport",
            "Viewport size, render target, grid and anti-aliasing settings.",
            |app, _| {
                Ok(json!({
                    "target_width": app.viewport_desired.0,
                    "target_height": app.viewport_desired.1,
                    "render_width": app.viewport_render_size.0,
                    "render_height": app.viewport_render_size.1,
                    "uploads": app.viewport_uploads,
                    "hovered": app.viewport_hovered,
                    "grid": app.viewport_settings.show_grid,
                    "anti_aliasing": format!("{:?}", app.viewport_settings.anti_aliasing),
                    "render_scale": app.viewport_settings.render_scale,
                    "pressed": app.fly_keys.iter().map(|key| format!("{key:?}")).collect::<Vec<_>>(),
                }))
            },
        )
        .query(
            "prefabs",
            "Built-in create-entity prefabs grouped by category.",
            |_, _| {
                Ok(json!({
                    "categories": crate::uikit::prefabs::CATEGORIES.iter().map(|category| json!({
                        "key": category.key,
                        "label": category.label,
                    })).collect::<Vec<_>>(),
                    "items": crate::uikit::prefabs::PREFABS.iter().map(|prefab| json!({
                        "key": prefab.key,
                        "label": prefab.label,
                        "description": prefab.description,
                        "category": prefab.category,
                    })).collect::<Vec<_>>(),
                }))
            },
        )
        .query(
            "materials",
            "Project .mat assets with relative path and name.",
            |app, _| {
                Ok(Value::Array(
                    app.assets
                        .iter()
                        .filter(|asset| asset.kind == "material")
                        .map(|asset| json!({
                            "name": asset.name,
                            "path": asset.relative_path,
                            "size": asset.size_bytes,
                        }))
                        .collect(),
                ))
            },
        )
        .query(
            "markers",
            "Scene markers drawn over the viewport: entity id, name, kind (directional, point, spot, camera, collider, empty), world position and visibility.",
            |app, _| {
                use crate::uikit::components::scene_markers::Kind;
                Ok(json!({
                    "visible": app.show_markers,
                    "markers": app.markers.iter().map(|marker| json!({
                        "id": marker.id,
                        "name": marker.name,
                        "position": marker.position,
                        "kind": match &marker.kind {
                            Kind::Directional { .. } => "directional",
                            Kind::Point { .. } => "point",
                            Kind::Spot { .. } => "spot",
                            Kind::Camera { .. } => "camera",
                            Kind::Collider { .. } => "collider",
                            Kind::Empty { .. } => "empty",
                        },
                    })).collect::<Vec<_>>(),
                }))
            },
        )
        .query(
            "nature",
            "Nature Generator state: name, quality, stats, every node (id, op, parameters with ranges, strings), the links, the op catalogue and the preset names.",
            |app, _| {
                use crate::uikit::procedural::ParamKind;
                let nature = &app.nature;
                Ok(json!({
                    "name": nature.name,
                    "quality": nature.quality.label(),
                    "stats": nature.stats,
                    "dirty": nature.dirty,
                    "nodes": nature.graph.nodes.iter().map(|n| {
                        let def = n.def();
                        json!({
                            "id": n.id, "op": n.op,
                            "params": def.params.iter().enumerate().map(|(i, p)| json!({
                                "index": i, "key": p.key, "label": p.label, "value": n.params[i],
                                "min": p.min, "max": p.max, "choice": matches!(p.kind, ParamKind::Enum(_)),
                            })).collect::<Vec<_>>(),
                            "strings": n.strings,
                        })
                    }).collect::<Vec<_>>(),
                    "links": nature.graph.links.iter().map(|l| json!({
                        "from": l.from, "output": l.output, "to": l.to, "input": l.input,
                    })).collect::<Vec<_>>(),
                    "ops": crate::uikit::nature::ops::NODES.iter().map(|d| json!({
                        "id": d.id, "title": d.title, "category": d.category,
                    })).collect::<Vec<_>>(),
                    "presets": crate::uikit::nature::presets()
                        .iter()
                        .map(|preset| json!({ "name": preset.name, "category": preset.category }))
                        .collect::<Vec<_>>(),
                }))
            },
        )
        .query(
            "texture_generator",
            "Texture Generator state: name, size, output mode, nodes, dirty flag and the preset names.",
            |app, _| {
                let graph = &app.texgen.graph;
                Ok(json!({
                    "name": app.texgen.name,
                    "resolution": graph.resolution,
                    "dirty": app.texgen.dirty,
                    "bits": app.texgen.bits,
                    "flipbook": [app.texgen.flip.0, app.texgen.flip.1],
                    "path": app.texgen.path.as_ref().map(|path| path.display().to_string()),
                    "nodes": graph.nodes.iter().map(|node| json!({
                        "id": node.id,
                        "title": node.kind.title(),
                        "subtitle": node.kind.subtitle(),
                    })).collect::<Vec<_>>(),
                    "links": graph.links.len(),
                    "presets": crate::uikit::texture_graph::presets()
                        .iter()
                        .map(|preset| json!({ "name": preset.name, "category": preset.category }))
                        .collect::<Vec<_>>(),
                }))
            },
        )
        .query("version", "Engine and C ABI version information.", |app, _| {
            Ok(json!({
                "capi_version": app.engine.as_ref().map(|engine| engine.capi_version()),
                "engine": app.engine.is_some(),
            }))
        })
        .query(
            "render_stats",
            "What the last rendered frame drew and cost (draw calls, triangles, culling, CPU pass times) and GPU capabilities.",
            |app, _| {
                let stats = app
                    .engine
                    .as_ref()
                    .and_then(|engine| engine.render_stats().ok())
                    .unwrap_or_default();
                Ok(json!({
                    "triangles": stats.triangles,
                    "meshes": stats.meshes,
                    "frustum_culled": stats.frustum_culled,
                    "draw_calls": stats.draw_calls,
                    "instanced_draw_calls": stats.instanced_draw_calls,
                    "gather_ms": stats.gather_ms,
                    "shadows_ms": stats.shadows_ms,
                    "opaque_ms": stats.opaque_ms,
                    "translucent_ms": stats.translucent_ms,
                    "total_ms": stats.total_ms,
                    "indirect_draw": stats.indirect_draw,
                    "graphics_storage_buffers": stats.graphics_storage_buffers,
                    "compute_storage_write": stats.compute_storage_write,
                    "mesh_shaders": stats.mesh_shaders,
                    "gpu_water_draws": stats.gpu_water_draws,
                }))
            },
        )
        .query(
            "material",
            "The open material: surface, maps, path and dirty state.",
            |app, _| Ok(material_json(app)),
        )
        .query(
            "material_presets",
            "Built-in PBR material presets.",
            |app, _| {
                Ok(Value::Array(
                    app.material_presets
                        .iter()
                        .map(|preset| json!({
                            "category": preset.category,
                            "name": preset.name,
                        }))
                        .collect(),
                ))
            },
        )
        .query(
            "generator",
            "Material generator graph, links, resolution and last bake status.",
            |app, _| Ok(generator_json(app)),
        )
        .query(
            "texture_set",
            "Texture set: UDIM tiles, channels, layer stack, brush and undo state.",
            |app, _| Ok(texture_set_json(app)),
        )
        .query(
            "gizmo",
            "Active transform tool and selected entity transform.",
            |app, _| {
                let target = app.entities.get(app.selected).map(|entity| json!({
                    "id": entity.id,
                    "name": entity.name,
                    "position": entity.position,
                    "rotation": entity.rotation,
                    "scale": entity.scale,
                }));
                Ok(json!({
                    "mode": app.gizmo_mode.label(),
                    "snap": app.gizmo_snap,
                    "target": target,
                }))
            },
        )
        .query(
            "component_types",
            "Component schema: id, name, fields with kind/limits/options.",
            |app, _| Ok(component_types_json(app)),
        )
        .query(
            "entity_components",
            "Components and values for an entity. args: { \"index\"?: number } (default selection).",
            entity_components_json,
        )
        .query(
            "assets",
            "Project assets with name, relative path, kind, size and directory flag.",
            |app, _| {
                Ok(Value::Array(
                    app.assets
                        .iter()
                        .map(|asset| {
                            json!({
                                "name": asset.name,
                                "path": asset.relative_path,
                                "kind": asset.kind,
                                "size": asset.size_bytes,
                                "is_directory": asset.is_directory,
                            })
                        })
                        .collect(),
                ))
            },
        )
        .query(
            "project",
            "Active project, manager tab, recent/browse results and asset navigation.",
            |app, _| Ok(project_json(app)),
        )
        .query(
            "script_editor",
            "Open script documents, active tab and dirty state.",
            |app, _| {
                Ok(json!({
                    "active": app.script.current_index(),
                    "status": app.script.status,
                    "documents": app.script.documents.iter().enumerate().map(|(index, document)| json!({
                        "index": index,
                        "title": document.title,
                        "path": document.path.display().to_string(),
                        "relative_path": document.relative_path,
                        "dirty": document.is_dirty(),
                        "bytes": document.text().len(),
                    })).collect::<Vec<_>>(),
                }))
            },
        )
        .query(
            "dryad",
            "Dryad node graph: script, dirty state, selection, nodes and edges.",
            |app, _| Ok(dryad_json(app)),
        )
        .query(
            "history",
            "Undo/redo timeline: steps, current cursor, availability and size.",
            |app, _| Ok(history_json(app)),
        )
        .query(
            "ui_catalog",
            "Documented widget ids as group/subgroup/object, with kind and description.",
            |_, _| Ok(catalog_json()),
        )
        .action("undo", "Reverts the last editor step. No args.", |_| {
            Ok(Message::Undo)
        })
        .action("redo", "Reapplies the last undone editor step. No args.", |_| {
            Ok(Message::Redo)
        })
        .action(
            "set_entity_parent",
            "Reparents an entity. args: { \"entity_id\": number, \"parent_id\"?: number | null }",
            |args| {
                let entity = args
                    .get("entity_id")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"entity_id\"".to_owned())?
                    as u32;
                let parent = args
                    .get("parent_id")
                    .and_then(Value::as_u64)
                    .map(|parent| parent as u32);
                Ok(Message::SetParent { entity, parent })
            },
        )
        .action(
            "add_component",
            "Adds a component by schema id. args: { \"type_id\": number }",
            |args| {
                let type_id = args
                    .get("type_id")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"type_id\"".to_owned())?;
                Ok(Message::Inspector(panels::inspector::Edit::AddComponent(
                    type_id as u32,
                )))
            },
        )
        .action(
            "remove_component",
            "Removes a component by its list index. args: { \"component\": number }",
            |args| {
                let component = args
                    .get("component")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"component\"".to_owned())?;
                Ok(Message::Inspector(
                    panels::inspector::Edit::RemoveComponent(component as usize),
                ))
            },
        )
        .action(
            "set_component_field",
            "Sets a component field. args: { \"component\": number, \"field\": number, \"kind\": string, \"value\": any }",
            |args| {
                let component = args
                    .get("component")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"component\"".to_owned())?
                    as usize;
                let field = args
                    .get("field")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"field\"".to_owned())?
                    as usize;
                let kind = required_str(args, "kind")?;
                let value = args
                    .get("value")
                    .ok_or_else(|| "missing \"value\"".to_owned())?;
                Ok(Message::SetComponentField {
                    component,
                    field,
                    value: parse_field_value(kind, value)?,
                })
            },
        )
        .action(
            "add_entity",
            "Adds an entity. args: { \"name\": string? }",
            |args| {
                let name = optional_str(args, "name")?.unwrap_or("New Entity");
                Ok(Message::AddEntityNamed(name.to_owned()))
            },
        )
        .action("delete_selected", "Deletes the selected entity. No args.", |_| {
            Ok(Message::Inspector(panels::inspector::Edit::Delete))
        })
        .action(
            "select_entity",
            "Selects an entity by index or exact name. args: { \"index\"?: number, \"name\"?: string }",
            |args| {
                if let Some(index) = args.get("index").and_then(Value::as_u64) {
                    return Ok(Message::SelectEntity(index as usize));
                }
                if let Some(name) = optional_str(args, "name")? {
                    return Ok(Message::SelectEntityByName(name.to_owned()));
                }
                Err("expected an \"index\" or a \"name\"".to_owned())
            },
        )
        .action(
            "rename_selected",
            "Renames the selected entity. args: { \"name\": string }",
            |args| {
                let name = required_str(args, "name")?;
                Ok(Message::Inspector(panels::inspector::Edit::Name(
                    name.to_owned(),
                )))
            },
        )
        .action("toggle_play", "Toggles the play/pause state. No args.", |_| {
            Ok(Message::TogglePlay)
        })
        .action("save_scene", "Saves the current scene. No args.", |_| {
            Ok(Message::SaveScene)
        })
        .action("close_scene", "Closes the scene and drops the engine session. No args.", |_| {
            Ok(Message::CloseScene)
        })
        .action(
            "open_project_manager",
            "Opens the Project Manager dialog. No args.",
            |_| Ok(Message::Project(project_manager::Edit::Open)),
        )
        .action(
            "create_project",
            "Creates and opens a project. args: { \"name\": string, \"location\"?: string, \"template\"?: \"Empty\"|\"2D\"|\"3D\" }",
            |args| {
                let name = required_str(args, "name")?.to_owned();
                let location = optional_str(args, "location")?
                    .map(str::to_owned)
                    .unwrap_or_else(|| {
                        crate::uikit::project::default_projects_directory()
                            .display()
                            .to_string()
                    });
                let template = optional_str(args, "template")?
                    .unwrap_or("3D")
                    .to_owned();
                Ok(Message::Project(project_manager::Edit::CreateAt {
                    name,
                    location,
                    template,
                }))
            },
        )
        .action(
            "open_project",
            "Opens an existing project root or project.caffeine file. args: { \"path\": string }",
            |args| {
                Ok(Message::Project(project_manager::Edit::OpenPath(
                    required_str(args, "path")?.to_owned(),
                )))
            },
        )
        .action(
            "open_script",
            "Opens a .lua or .dyd script in the Script Editor. args: { \"path\": string }",
            |args| {
                Ok(Message::OpenScriptPath(
                    required_str(args, "path")?.to_owned(),
                ))
            },
        )
        .action(
            "open_dryad",
            "Opens a .dyd script in the Dryad node editor. args: { \"path\": string }",
            |args| {
                Ok(Message::OpenDryadPath(
                    required_str(args, "path")?.to_owned(),
                ))
            },
        )
        .action(
            "dryad_add_node",
            "Adds a Dryad node by type name. args: { \"kind\": string }",
            |args| {
                let kind = required_str(args, "kind")?;
                let kind = crate::uikit::dryad::NodeKind::from_type_name(kind)
                    .ok_or_else(|| format!("unknown Dryad node \"{kind}\""))?;
                Ok(Message::DryadAddNode(kind))
            },
        )
        .action("dryad_save", "Saves the open Dryad .dyd and .node.json.", |_| {
            Ok(Message::DryadSave)
        })
        .action(
            "dryad_sync",
            "Rebuilds the Dryad canvas from the .dyd markers.",
            |_| Ok(Message::DryadSync),
        )
        .action("dryad_delete", "Deletes the selected Dryad node or link.", |_| {
            Ok(Message::DryadDelete)
        })
        .action("dryad_fit", "Frames the Dryad graph.", |_| Ok(Message::DryadFit))
        .action(
            "dryad_add_motion",
            "Adds the OnUpdate -> Translate motion example.",
            |_| Ok(Message::DryadAddMotion),
        )
        .action(
            "dryad_attach",
            "Attaches the open Dryad script to the selected entity.",
            |_| Ok(Message::DryadAttach),
        )
        .action(
            "dryad_run",
            "Runs one Dryad preview frame on the selected entity.",
            |_| Ok(Message::DryadRun),
        )
        .action(
            "dryad_connect",
            "Connects two Dryad endpoints. args: { \"from\": \"n1.out\", \"to\": \"n2.in\" }",
            |args| {
                Ok(Message::DryadCanvas(dryad_nodes::CanvasEvent::Connect {
                    from: required_str(args, "from")?.to_owned(),
                    to: required_str(args, "to")?.to_owned(),
                }))
            },
        )
        .action(
            "dryad_set_number",
            "Sets a Dryad numeric field. args: { \"node\": string, \"field\": string, \"value\": number }",
            |args| {
                let value = args
                    .get("value")
                    .and_then(Value::as_f64)
                    .ok_or_else(|| "expected a numeric \"value\"".to_owned())?;
                Ok(Message::DryadNumber {
                    node: required_str(args, "node")?.to_owned(),
                    field: required_str(args, "field")?.to_owned(),
                    text: format!("{value}"),
                })
            },
        )
        .action(
            "dryad_set_bool",
            "Sets a Dryad boolean field. args: { \"node\": string, \"field\": string, \"value\": bool }",
            |args| {
                let value = args
                    .get("value")
                    .and_then(Value::as_bool)
                    .ok_or_else(|| "expected a boolean \"value\"".to_owned())?;
                Ok(Message::DryadBool {
                    node: required_str(args, "node")?.to_owned(),
                    field: required_str(args, "field")?.to_owned(),
                    value,
                })
            },
        )
        .action(
            "dryad_set_text",
            "Sets the Log message on a Dryad node. args: { \"node\": string, \"value\": string }",
            |args| {
                Ok(Message::DryadText {
                    node: required_str(args, "node")?.to_owned(),
                    value: required_str(args, "value")?.to_owned(),
                })
            },
        )
        .action(
            "place_prefab",
            "Places a saved .prefab (a tree, say) in the scene at the camera focus and selects it. args: { \"path\": string } relative to the assets root, e.g. Prefabs/oak.prefab",
            |args| Ok(Message::PlacePrefab(required_str(args, "path")?.to_owned())),
        )
        .action(
            "open_generator",
            "Reopens the generator graph behind an asset: a .generator.* recipe, a .prefab, or anything a generator baked (a model, material or texture). args: { \"path\": string } relative to the assets root",
            |args| Ok(Message::OpenGeneratorFor(required_str(args, "path")?.to_owned())),
        )
        .action(
            "asset_navigate",
            "Opens an asset-browser folder (relative to the asset root). args: { \"path\": string }",
            |args| {
                Ok(Message::AssetNavigate(
                    required_str(args, "path")?.to_owned(),
                ))
            },
        )
        .action("asset_up", "Moves the asset browser to its parent folder. No args.", |_| {
            Ok(Message::AssetUp)
        })
        .action("asset_root", "Returns the asset browser to the asset root. No args.", |_| {
            Ok(Message::AssetRoot)
        })
        .action("refresh_assets", "Rescans the project asset root. No args.", |_| {
            Ok(Message::AssetRefresh)
        })
        .action(
            "set_asset_view",
            "Switches the asset browser layout. args: { \"mode\": \"grid\"|\"list\" }",
            |args| {
                let mode = match required_str(args, "mode")?.to_ascii_lowercase().as_str() {
                    "grid" => crate::uikit::panels::assetmanager::ViewMode::Grid,
                    "list" => crate::uikit::panels::assetmanager::ViewMode::List,
                    other => return Err(format!("unknown asset view \"{other}\"")),
                };
                Ok(Message::AssetViewMode(mode))
            },
        )
        .action("reset_camera", "Restores the default camera. No args.", |_| {
            Ok(Message::ResetCamera)
        })
        .action("toggle_grid", "Shows or hides the viewport reference grid. No args.", |_| {
            Ok(Message::ToggleGrid)
        })
        .action(
            "create_prefab",
            "Creates a built-in prefab. args: { \"key\": string }",
            |args| {
                let key = required_str(args, "key")?;
                let index = crate::uikit::prefabs::PREFABS
                    .iter()
                    .position(|prefab| prefab.key == key)
                    .ok_or_else(|| format!("unknown prefab \"{key}\""))?;
                Ok(Message::CreatePrefab(index))
            },
        )
        .action(
            "scatter_on_terrain",
            "Scatters a plan on a terrain: pines, boulders, grass... placed by the terrain's slope, wetness and height, one undo step, deterministic for a seed. args: { \"plan_text\"?: string (a scatter plan; else the project file), \"plan_file\"?: string (project-relative, default \"Scatter/default.scatter\", written from the built-in plan if missing), \"seed\"?: number, \"max_instances\"?: number, \"terrain_id\"?: number (default: the selected entity) }",
            |args| {
                Ok(Message::ScatterOnTerrain(super::scatter::ScatterRequest {
                    plan_text: args
                        .get("plan_text")
                        .and_then(Value::as_str)
                        .map(str::to_owned),
                    plan_file: args
                        .get("plan_file")
                        .and_then(Value::as_str)
                        .map(str::to_owned),
                    seed: args.get("seed").and_then(Value::as_u64),
                    max_instances: args
                        .get("max_instances")
                        .and_then(Value::as_u64)
                        .map(|n| n.min(u64::from(u32::MAX)) as u32),
                    terrain: args
                        .get("terrain_id")
                        .and_then(Value::as_u64)
                        .map(|n| n as u32),
                }))
            },
        )
        .action("open_prefab_palette", "Opens the create-entity palette. No args.", |_| {
            Ok(Message::TogglePrefabPalette)
        })
        .action(
            "open_material",
            "Opens a .mat asset. args: { \"path\": string }",
            |args| Ok(Message::OpenMaterial(required_str(args, "path")?.to_owned())),
        )
        .action("new_material", "Creates an unsaved material. No args.", |_| {
            Ok(Message::Material(material::Edit::New))
        })
        .action("save_material", "Saves the open material. No args.", |_| {
            Ok(Message::Material(material::Edit::Save))
        })
        .action(
            "apply_material",
            "Assigns the open material to the selected entity. No args.",
            |_| Ok(Message::Material(material::Edit::Apply)),
        )
        .action(
            "set_material_texture",
            "Assigns a project texture. args: { \"slot\": \"albedo\"|\"normal\"|\"orm\"|\"emission\"|\"source\", \"path\": string }",
            |args| {
                let slot = match required_str(args, "slot")? {
                    "albedo" | "base" => material::TextureSlot::Albedo,
                    "normal" => material::TextureSlot::Normal,
                    "orm" | "arm" => material::TextureSlot::Orm,
                    "emission" | "emissive" => material::TextureSlot::Emission,
                    "source" | "height" => material::TextureSlot::Source,
                    other => return Err(format!("unknown texture slot \"{other}\"")),
                };
                let path = required_str(args, "path")?.to_owned();
                Ok(Message::SetMaterialTexture { slot, path })
            },
        )
        .action(
            "set_material_preview_shape",
            "Changes the material preview subject. args: { \"shape\": \"sphere\"|\"cube\"|\"plane\" }",
            |args| {
                let shape = match required_str(args, "shape")?.to_lowercase().as_str() {
                    "sphere" => 0,
                    "cube" => 1,
                    "plane" => 2,
                    other => return Err(format!("unknown preview shape \"{other}\"")),
                };
                Ok(Message::Material(material::Edit::SetPreviewShape(shape)))
            },
        )
        .action(
            "set_material_preview_environment",
            "Sets or clears the material preview HDRI. args: { \"path\": string? }",
            |args| {
                if let Some(path) = optional_str(args, "path")? {
                    Ok(Message::MaterialEnvironmentPick(path.to_owned()))
                } else {
                    Ok(Message::Material(material::Edit::ClearPreviewEnvironment))
                }
            },
        )
        .action(
            "set_material_preview_sun",
            "Sets the material preview sun. args: { \"yaw\": degrees, \"elevation\": degrees, \"intensity\": number, \"shadows\": bool } (all optional, applied in order)",
            |args| {
                let number = |key: &str| args.get(key).and_then(Value::as_f64).map(|v| v as f32);
                if let Some(value) = number("yaw") {
                    return Ok(Message::Material(material::Edit::SetSunYaw(value)));
                }
                if let Some(value) = number("elevation") {
                    return Ok(Message::Material(material::Edit::SetSunElevation(value)));
                }
                if let Some(value) = number("intensity") {
                    return Ok(Message::Material(material::Edit::SetSunIntensity(value)));
                }
                if let Some(value) = args.get("shadows").and_then(Value::as_bool) {
                    return Ok(Message::Material(material::Edit::SetPreviewShadows(value)));
                }
                Err("expected one of yaw, elevation, intensity, shadows".to_owned())
            },
        )
        .action(
            "nature_apply_preset",
            "Loads a Nature Generator preset graph (rock, tree or plant). args: { \"name\": \"Oak\" } (see query nature for names)",
            |args| {
                let name = args
                    .get("name")
                    .and_then(Value::as_str)
                    .ok_or_else(|| "expected \"name\"".to_owned())?;
                let index = crate::uikit::nature::presets()
                    .iter()
                    .position(|preset| preset.name.eq_ignore_ascii_case(name))
                    .ok_or_else(|| format!("unknown nature preset `{name}`"))?;
                Ok(Message::NatureGen(nature_generator::Edit::ApplyPreset(index)))
            },
        )
        .action(
            "nature_merge_parts",
            "Nature placement: put the solid parts, and the cut-out parts, in one entity each with a Material slot per part. args: { \"enabled\": bool }",
            |args| {
                let enabled = args.get("enabled").and_then(Value::as_bool).unwrap_or(true);
                Ok(Message::NatureGen(nature_generator::Edit::SetMergeParts(enabled)))
            },
        )
        .action(
            "nature_add_to_scatter",
            "Writes the Nature model as a class of Scatter/default.scatter (solid and foliage meshes with their material slots), so scatter_on_terrain plants it. args: {}",
            |_| Ok(Message::NatureGen(nature_generator::Edit::AddToScatter)),
        )
        .action(
            "terrain_scatter",
            "The Terrain tab's Scatter button: scatters the default plan on the selected terrain, else on the last placed one. args: {}",
            |_| Ok(Message::TerrainGen(terrain_generator::Edit::Scatter)),
        )
        .action(
            "terrain_edit_scatter_plan",
            "Opens Scatter/default.scatter in the script editor. args: {}",
            |_| Ok(Message::TerrainGen(terrain_generator::Edit::EditScatterPlan)),
        )
        .action(
            "nature_micromesh",
            "Nature placement: turn Micromesh on for dense solid parts (rocks, trunks). args: { \"enabled\": bool }",
            |args| {
                let enabled = args.get("enabled").and_then(Value::as_bool).unwrap_or(true);
                Ok(Message::NatureGen(nature_generator::Edit::SetMicromesh(enabled)))
            },
        )
        .action(
            "nature_add_node",
            "Adds a node to the Nature Generator graph. args: { \"op\": \"noise_displace\" } (op ids in query nature)",
            |args| {
                let op = args.get("op").and_then(Value::as_str).ok_or_else(|| "expected \"op\"".to_owned())?;
                let def = crate::uikit::nature::ops::find(op).ok_or_else(|| format!("unknown node `{op}`"))?;
                Ok(Message::NatureGen(nature_generator::Edit::AddNode(def.id)))
            },
        )
        .action(
            "nature_edit_material",
            "Adds an Edit Library Material node after a Library Material / Leaf-Petal Texture node, wired in place, and selects it (set its sliders with nature_set). args: { \"node\": id }",
            |args| {
                let node = args
                    .get("node")
                    .and_then(Value::as_u64)
                    .ok_or("expected numeric \"node\"")?;
                Ok(Message::NatureGen(nature_generator::Edit::EditMaterial(node)))
            },
        )
        .action(
            "nature_connect",
            "Wires two nodes. args: { \"from\": id, \"output\": 0, \"to\": id, \"input\": 0 }",
            |args| {
                let get = |key: &str| args.get(key).and_then(Value::as_u64).ok_or_else(|| format!("expected numeric \"{key}\""));
                Ok(Message::NatureGen(nature_generator::Edit::Connect {
                    from: get("from")?,
                    output: get("output")? as usize,
                    to: get("to")?,
                    input: get("input")? as usize,
                }))
            },
        )
        .action(
            "nature_set",
            "Edits the Nature Generator graph. args: { \"node\": id, \"param\": index, \"value\": number } or { \"node\": id, \"string\": index, \"text\": path } (a Part's own asset) or { \"quality\": 0|1|2 } or { \"name\": string }",
            |args| {
                if let (Some(node), Some(param), Some(value)) = (
                    args.get("node").and_then(Value::as_u64),
                    args.get("param").and_then(Value::as_u64),
                    args.get("value").and_then(Value::as_f64),
                ) {
                    return Ok(Message::NatureGen(nature_generator::Edit::SetParam(node, param as usize, value as f32)));
                }
                if let (Some(node), Some(index), Some(text)) = (
                    args.get("node").and_then(Value::as_u64),
                    args.get("string").and_then(Value::as_u64),
                    args.get("text").and_then(Value::as_str),
                ) {
                    return Ok(Message::NatureGen(nature_generator::Edit::SetString(node, index as usize, text.to_owned())));
                }
                if let Some(quality) = args.get("quality").and_then(Value::as_u64) {
                    return Ok(Message::NatureGen(nature_generator::Edit::SetQuality(quality as usize)));
                }
                if let Some(name) = args.get("name").and_then(Value::as_str) {
                    return Ok(Message::NatureGen(nature_generator::Edit::SetName(name.to_owned())));
                }
                Err("expected node + param + value, node + string + text, quality or name".to_owned())
            },
        )
        .action(
            "nature_export",
            "Writes the Nature Generator model as one .obj per part (plus the .nature recipe) into the project's nature folder.",
            |_| Ok(Message::NatureGen(nature_generator::Edit::Export)),
        )
        .action(
            "nature_place",
            "Places the Nature Generator model in the scene as a group of entities, one per part, each with its own material.",
            |_| Ok(Message::NatureGen(nature_generator::Edit::Place)),
        )
        .action(
            "toggle_scene_markers",
            "Shows or hides the scene markers (light rays, camera frustums, colliders, locators) over the viewport.",
            |_| Ok(Message::ToggleMarkers),
        )
        .action(
            "toggle_gizmo_snap",
            "Turns gizmo snapping on or off (0.25 m, 15 degrees, 0.1 scale).",
            |_| Ok(Message::ToggleGizmoSnap),
        )
        .action(
            "select_marker",
            "Selects the entity behind a scene marker, like clicking its icon. args: { \"id\": entity id }",
            |args| {
                let id = args
                    .get("id")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected numeric \"id\"".to_owned())?;
                Ok(Message::MarkerSelected(id as u32))
            },
        )
        .action(
            "texture_generator_apply_preset",
            "Replaces the Texture Generator graph with a library preset. args: { \"name\": \"Torch Flame\" } (see query texture_generator for names)",
            |args| {
                let name = args
                    .get("name")
                    .and_then(Value::as_str)
                    .ok_or_else(|| "expected \"name\"".to_owned())?;
                let index = crate::uikit::texture_graph::presets()
                    .iter()
                    .position(|preset| preset.name.eq_ignore_ascii_case(name))
                    .ok_or_else(|| format!("unknown texture preset `{name}`"))?;
                Ok(Message::TextureGen(texture_generator::Edit::ApplyPreset(index)))
            },
        )
        .action(
            "texture_generator_export",
            "Bakes the Texture Generator graph to a PNG in the project's textures folder. set the file name, bit depth, size and flipbook grid first with texture_generator_configure. args: { \"flipbook\": true } exports the flipbook sheet instead of the single PNG",
            |args| {
                if args.get("flipbook").and_then(Value::as_bool) == Some(true) {
                    Ok(Message::TextureGen(texture_generator::Edit::ExportFlipbook))
                } else {
                    Ok(Message::TextureGen(texture_generator::Edit::Export))
                }
            },
        )
        .action(
            "texture_generator_configure",
            "Configures the Texture Generator export. args: { \"name\": string } or { \"bits\": 8|16 } or { \"flipbook\": [columns, rows] } or { \"size\": 64..2048 }",
            |args| {
                if let Some(name) = args.get("name").and_then(Value::as_str) {
                    return Ok(Message::TextureGen(texture_generator::Edit::SetName(name.to_owned())));
                }
                if let Some(bits) = args.get("bits").and_then(Value::as_u64) {
                    return Ok(Message::TextureGen(texture_generator::Edit::SetBits(bits as u8)));
                }
                if let Some(size) = args.get("size").and_then(Value::as_u64) {
                    return Ok(Message::TextureGen(texture_generator::Edit::Node(
                        material_generator::Edit::SetResolution(size as u32),
                    )));
                }
                if let Some(grid) = args.get("flipbook").and_then(Value::as_array) {
                    let get = |i: usize| grid.get(i).and_then(Value::as_u64).map(|v| v as u32);
                    if let (Some(columns), Some(rows)) = (get(0), get(1)) {
                        return Ok(Message::TextureGen(texture_generator::Edit::SetFlipbook(
                            columns, rows,
                        )));
                    }
                }
                Err("expected one of name, bits, size, flipbook".to_owned())
            },
        )
        .action(
            "set_material_preview_exposure",
            "Sets material preview exposure. args: { \"value\": number }",
            |args| {
                let value = args
                    .get("value")
                    .and_then(Value::as_f64)
                    .ok_or_else(|| "expected numeric \"value\"".to_owned())? as f32;
                Ok(Message::Material(material::Edit::SetPreviewExposure(value)))
            },
        )
        .action(
            "generate_material_maps",
            "Bakes maps beside the material. args: { \"operation\": \"normal\"|\"ao\"|\"roughness\"|\"metallic\"|\"orm\"|\"all\" }",
            |args| {
                let operation = match required_str(args, "operation")? {
                    "normal" => material::MapOperation::Normal,
                    "ao" | "ambient_occlusion" => material::MapOperation::AmbientOcclusion,
                    "roughness" => material::MapOperation::Roughness,
                    "metallic" => material::MapOperation::Metallic,
                    "orm" => material::MapOperation::Orm,
                    "all" => material::MapOperation::All,
                    other => return Err(format!("unknown map operation \"{other}\"")),
                };
                Ok(Message::Material(material::Edit::Generate(operation)))
            },
        )
        .action(
            "bake_scene_ao",
            "Bakes AO from the selected entity's scene geometry into its ORM map. args: { \"resolution\"?: 512, \"rays\"?: 16, \"max_distance\"?: 2.5 }",
            |args| {
                let resolution = args
                    .get("resolution")
                    .and_then(Value::as_u64)
                    .unwrap_or(512) as u32;
                let rays = args.get("rays").and_then(Value::as_u64).unwrap_or(16) as u32;
                let max_distance = args
                    .get("max_distance")
                    .and_then(Value::as_f64)
                    .unwrap_or(2.5) as f32;
                Ok(Message::Material(material::Edit::BakeSceneAo {
                    resolution,
                    rays,
                    max_distance,
                }))
            },
        )
        .action(
            "import_materialcraft",
            "Imports a portable .mcmat (maps, manifest and SBS source). args: { \"path\": string }",
            |args| {
                let path = required_str(args, "path")?.to_owned();
                Ok(Message::ImportMaterialcraft(path))
            },
        )
        .action(
            "export_materialcraft",
            "Bakes the generator and writes a portable .mcmat. args: { \"path\": string }",
            |args| {
                let path = required_str(args, "path")?.to_owned();
                Ok(Message::ExportMaterialcraft(path))
            },
        )
        .action("new_texture_set", "Creates a texture set. No args.", |_| {
            Ok(Message::TextureSet(
                texture_set_panel::Edit::New,
            ))
        })
        .action(
            "open_texture_set",
            "Opens a .texset project. args: { \"path\": string }",
            |args| {
                let path = required_str(args, "path")?.to_owned();
                Ok(Message::OpenTextureSet(path))
            },
        )
        .action(
            "save_texture_set",
            "Saves the open texture set. No args.",
            |_| Ok(Message::TextureSet(texture_set_panel::Edit::Save)),
        )
        .action(
            "texture_set_bake_ao",
            "Bakes AO from the height channel. No args.",
            |_| Ok(Message::TextureSet(texture_set_panel::Edit::BakeHeightAo)),
        )
        .action(
            "texture_set_bake_curvature",
            "Bakes curvature from the height channel. No args.",
            |_| Ok(Message::TextureSet(texture_set_panel::Edit::BakeCurvature)),
        )
        .action(
            "texture_set_bake_scene_ao",
            "Bakes AO from the selected entity geometry. No args.",
            |_| Ok(Message::TextureSet(texture_set_panel::Edit::BakeSceneAo)),
        )
        .action(
            "texture_set_bake_mesh_map",
            "Bakes a mesh map from the selected entity. args: { \"kind\": \"normal\"|\"position\"|\"id\" }",
            |args| {
                let kind = required_str(args, "kind")?;
                let edit = match kind.to_lowercase().as_str() {
                    "normal" => texture_set_panel::Edit::BakeMeshNormal,
                    "position" => texture_set_panel::Edit::BakeMeshPosition,
                    "id" | "object_id" | "objectid" => {
                        texture_set_panel::Edit::BakeMeshObjectId
                    }
                    other => return Err(format!("unknown mesh map \"{other}\"")),
                };
                Ok(Message::TextureSet(edit))
            },
        )
        .action(
            "texture_set_undo",
            "Undoes the last tile paint stroke.",
            |_| Ok(Message::TextureSet(texture_set_panel::Edit::Undo)),
        )
        .action(
            "texture_set_redo",
            "Redoes the last tile paint stroke.",
            |_| Ok(Message::TextureSet(texture_set_panel::Edit::Redo)),
        )
        .action(
            "paint_texture_set",
            "Paints one brush dab. args: { \"tile\"?: 1001, \"u\": 0..1, \"v\": 0..1 }",
            |args| {
                let tile = args
                    .get("tile")
                    .and_then(Value::as_u64)
                    .unwrap_or(1001) as u32;
                let u = args
                    .get("u")
                    .and_then(Value::as_f64)
                    .ok_or_else(|| "expected numeric \"u\"".to_owned())? as f32;
                let v = args
                    .get("v")
                    .and_then(Value::as_f64)
                    .ok_or_else(|| "expected numeric \"v\"".to_owned())? as f32;
                Ok(Message::TextureSet(texture_set_panel::Edit::PaintDab(
                    tile,
                    [u, v],
                )))
            },
        )
        .action(
            "export_texture_set",
            "Exports the texture set with the active preset. args: { \"path\"?: directory }",
            |args| {
                if let Some(path) = optional_str(args, "path")? {
                    Ok(Message::TextureSet(texture_set_panel::Edit::ExportTo(
                        path.to_owned(),
                    )))
                } else {
                    Ok(Message::TextureSet(texture_set_panel::Edit::Export))
                }
            },
        )
        .action(
            "import_mesh_materials",
            "Imports OBJ/MTL or glTF material definitions as .mat assets. args: { \"path\": string }",
            |args| {
                let path = required_str(args, "path")?.to_owned();
                Ok(Message::ImportMeshMaterials(path))
            },
        )
        .action(
            "open_generator",
            "Opens the node-based material generator. No args.",
            |_| Ok(Message::OpenGenerator),
        )
        .action(
            "add_generator_node",
            "Adds a generator node. args: { \"kind\": \"image\"|\"noise\"|\"voronoi\"|\"checker\"|\"bricks\"|\"gradient\"|\"blend\"|\"levels\"|\"curve\"|\"hsl\"|\"threshold\"|\"blur\"|\"warp\"|\"value\"|\"luminance\"|\"color\"|\"dot\"|\"output\" }",
            |args| {
                let kind = required_str(args, "kind")?.to_lowercase();
                let node = generators::palette()
                    .into_iter()
                    .find(|node| node.title().eq_ignore_ascii_case(&kind))
                    .ok_or_else(|| format!("unknown generator node \"{kind}\""))?;
                Ok(Message::Generator(material_generator::Edit::AddNode(node)))
            },
        )
        .action(
            "connect_generator",
            "Connects generator nodes. args: { \"from\": number, \"to\": number, \"input\": number? }",
            |args| {
                let from = args
                    .get("from")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"from\"".to_owned())?;
                let to = args
                    .get("to")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"to\"".to_owned())?;
                let input = args.get("input").and_then(Value::as_u64).unwrap_or(0) as usize;
                Ok(Message::Generator(material_generator::Edit::Connect {
                    from,
                    output: 0,
                    to,
                    input,
                }))
            },
        )
        .action(
            "bake_generator",
            "Bakes the generator graph into the open material. No args.",
            |_| Ok(Message::Generator(material_generator::Edit::Bake)),
        )
        .action("generator_undo", "Undoes the last generator graph edit.", |_| {
            Ok(Message::Generator(material_generator::Edit::Undo))
        })
        .action("generator_redo", "Redoes the last generator graph edit.", |_| {
            Ok(Message::Generator(material_generator::Edit::Redo))
        })
        .action("fit_generator", "Frames the whole generator graph.", |_| {
            Ok(Message::Generator(material_generator::Edit::Fit))
        })
        .action(
            "delete_generator_link",
            "Deletes the wire feeding an input. args: { \"to\": number, \"input\": number }",
            |args| {
                let to = args
                    .get("to")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"to\"".to_owned())?;
                let input = args
                    .get("input")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"input\"".to_owned())?
                    as usize;
                Ok(Message::Generator(material_generator::Edit::DeleteLink(
                    to, input,
                )))
            },
        )
        .action(
            "insert_generator_dot",
            "Splits a wire with a reroute dot. args: { \"to\": number, \"input\": number, \"x\"?: number, \"y\"?: number }",
            |args| {
                let to = args
                    .get("to")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"to\"".to_owned())?;
                let input = args
                    .get("input")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"input\"".to_owned())?
                    as usize;
                let x = args.get("x").and_then(Value::as_f64).unwrap_or(0.0) as f32;
                let y = args.get("y").and_then(Value::as_f64).unwrap_or(0.0) as f32;
                Ok(Message::Generator(material_generator::Edit::InsertDot(
                    to,
                    input,
                    iced::Point::new(x, y),
                )))
            },
        )
        .action("copy_generator_node", "Copies the selected generator node.", |_| {
            Ok(Message::Generator(material_generator::Edit::Copy))
        })
        .action("paste_generator_node", "Pastes the copied generator node.", |_| {
            Ok(Message::Generator(material_generator::Edit::Paste))
        })
        .action(
            "set_generator_snap",
            "Toggles generator grid snapping. args: { \"enabled\": bool }",
            |args| {
                let enabled = args
                    .get("enabled")
                    .and_then(Value::as_bool)
                    .unwrap_or(true);
                Ok(Message::Generator(material_generator::Edit::SetSnap(enabled)))
            },
        )
        .action(
            "set_generator_search",
            "Filters the generator node palette. args: { \"text\": string }",
            |args| {
                let text = optional_str(args, "text")?.unwrap_or("").to_owned();
                Ok(Message::Generator(material_generator::Edit::SetSearch(text)))
            },
        )
        .action(
            "set_generator_image",
            "Points an Image generator node at an asset-relative texture. args: { \"node\": number, \"path\": string }",
            |args| {
                let node = args
                    .get("node")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"node\"".to_owned())?;
                let path = required_str(args, "path")?.to_owned();
                Ok(Message::Generator(material_generator::Edit::SetImagePath(
                    node, path,
                )))
            },
        )
        .action(
            "set_generator_bits",
            "Sets the generated map bit depth. args: { \"bits\": 8|16 }",
            |args| {
                let bits = args
                    .get("bits")
                    .and_then(Value::as_u64)
                    .ok_or_else(|| "expected a numeric \"bits\"".to_owned())?;
                if bits != 8 && bits != 16 {
                    return Err("bits must be 8 or 16".to_owned());
                }
                Ok(Message::Generator(material_generator::Edit::SetBits(
                    bits as u8,
                )))
            },
        )
        .action(
            "set_generator_packing_preset",
            "Chooses the channel-packing preset. args: { \"preset\": \"orm\"|\"hdrp\"|\"metallic_smoothness\"|\"custom\" }",
            |args| {
                let preset = required_str(args, "preset")?.to_lowercase();
                let index = match preset.as_str() {
                    "orm" => 0,
                    "hdrp" | "unity_hdrp" | "unity_hdrp_mask" => 1,
                    "metallic_smoothness" | "unity_metallic_smoothness" => 2,
                    "custom" => 3,
                    other => return Err(format!("unknown packing preset \"{other}\"")),
                };
                Ok(Message::Generator(
                    material_generator::Edit::SetPackingPreset(index),
                ))
            },
        )
        .action(
            "set_generator_packing_channel",
            "Assigns a packed channel. args: { \"channel\": \"r\"|\"g\"|\"b\"|\"a\", \"source\": \"ao\"|\"roughness\"|\"metallic\"|\"height\"|\"smoothness\"|\"base_luminance\"|\"zero\"|\"one\" }",
            |args| {
                let channel = match required_str(args, "channel")?.to_lowercase().as_str() {
                    "r" | "0" => 0,
                    "g" | "1" => 1,
                    "b" | "2" => 2,
                    "a" | "3" => 3,
                    other => return Err(format!("unknown channel \"{other}\"")),
                };
                let source = match required_str(args, "source")?.to_lowercase().as_str() {
                    "zero" | "0" => generators::PackSource::Zero,
                    "one" | "1" => generators::PackSource::One,
                    "ao" => generators::PackSource::Ao,
                    "roughness" => generators::PackSource::Roughness,
                    "metallic" => generators::PackSource::Metallic,
                    "height" => generators::PackSource::Height,
                    "smoothness" => generators::PackSource::Smoothness,
                    "base_luminance" | "base_luma" => generators::PackSource::BaseLuminance,
                    other => return Err(format!("unknown packing source \"{other}\"")),
                };
                let source_index = generators::PackSource::ALL
                    .iter()
                    .position(|candidate| *candidate == source)
                    .unwrap_or(0);
                Ok(Message::Generator(
                    material_generator::Edit::SetPackingChannel(channel, source_index),
                ))
            },
        )
        .action(
            "cancel_generator_bake",
            "Cancels the running background generator bake. No args.",
            |_| Ok(Message::Generator(material_generator::Edit::CancelBake)),
        )
        .action(
            "set_gizmo_mode",
            "Switches the transform gizmo. args: { \"mode\": \"move\"|\"rotate\"|\"scale\" }",
            |args| {
                let mode = match required_str(args, "mode")? {
                    "move" | "translate" => gizmo::Mode::Translate,
                    "rotate" => gizmo::Mode::Rotate,
                    "scale" => gizmo::Mode::Scale,
                    other => return Err(format!("unknown gizmo mode \"{other}\"")),
                };
                Ok(Message::SetGizmoMode(mode))
            },
        )
        .action(
            "set_viewport",
            "Updates viewport quality. args: { \"grid\"?: bool, \"anti_aliasing\"?: \"off\"|\"fxaa\"|\"taa\", \"render_scale\"?: number }",
            |args| {
                let show_grid = args
                    .get("grid")
                    .map(|grid| {
                        grid.as_bool()
                            .ok_or_else(|| "\"grid\" must be a boolean".to_owned())
                    })
                    .transpose()?;
                let anti_aliasing = args
                    .get("anti_aliasing")
                    .map(|mode| match mode.as_str() {
                        Some("off") => Ok(doppio_engine::AntiAliasing::Off),
                        Some("fxaa") => Ok(doppio_engine::AntiAliasing::Fxaa),
                        Some("taa") => Ok(doppio_engine::AntiAliasing::Taa),
                        _ => Err("anti_aliasing must be off, fxaa or taa".to_owned()),
                    })
                    .transpose()?;
                Ok(Message::SetViewportSettings {
                    show_grid,
                    anti_aliasing,
                    render_scale: optional_f32(args, "render_scale")?,
                })
            },
        )
        .action(
            "set_camera",
            "Sets the viewport camera. args: { \"yaw\"?: number, \"pitch\"?: number, \"distance\"?: number, \"focus\"?: [x,y,z] }",
            |args| {
                let default = doppio_engine::Camera::default();
                Ok(Message::SetCamera(doppio_engine::Camera {
                    yaw: optional_f32(args, "yaw")?.unwrap_or(default.yaw),
                    pitch: optional_f32(args, "pitch")?.unwrap_or(default.pitch),
                    distance: optional_f32(args, "distance")?.unwrap_or(default.distance),
                    focus: camera_focus(args, default.focus)?,
                }))
            },
        )
        .fixture(
            "empty",
            "Empty editor: no scene, no entities. No args.",
            |_| Ok(App::empty()),
        )
        .fixture("editor", "Two entities ready to edit. No args.", |_| {
            let mut app = App::empty();
            app.update(Message::AddEntityNamed("Entity 1".to_owned()));
            app.update(Message::AddEntityNamed("Entity 2".to_owned()));
            Ok(app)
        })
        .run()
}

fn component_types_json(app: &App) -> Value {
    Value::Array(
        app.component_types
            .iter()
            .map(|kind| {
                json!({
                    "id": kind.id,
                    "name": kind.name,
                    "fields": kind.fields.iter().enumerate().map(|(index, field)| json!({
                        "index": index,
                        "name": field.name,
                        "kind": field_kind_name(field.kind),
                        "min": field.min,
                        "max": field.max,
                        "step": field.step,
                        "options": field.options,
                    })).collect::<Vec<_>>(),
                })
            })
            .collect(),
    )
}

fn entity_components_json(app: &App, args: &Value) -> Result<Value, String> {
    let index = args
        .get("index")
        .and_then(Value::as_u64)
        .map(|index| index as usize)
        .unwrap_or(app.selected);
    let Some(entity) = app.entities.get(index) else {
        return Err(format!("no entity at index {index}"));
    };
    let components: Vec<Value> = app
        .components
        .iter()
        .enumerate()
        .map(|(component_index, component)| {
            json!({
                "index": component_index,
                "type_id": component.info.type_id,
                "name": component.info.name,
                "fields": component.values.iter().enumerate().map(|(field_index, value)| json!({
                    "index": field_index,
                    "value": field_value_json(value),
                })).collect::<Vec<_>>(),
            })
        })
        .collect();
    Ok(json!({
        "entity": { "index": index, "id": entity.id, "name": entity.name },
        "components": components,
    }))
}

fn field_kind_name(kind: doppio_engine::FieldKind) -> &'static str {
    use doppio_engine::FieldKind;
    match kind {
        FieldKind::F32 => "f32",
        FieldKind::Bool => "bool",
        FieldKind::I32 => "i32",
        FieldKind::U32 => "u32",
        FieldKind::Vec2 => "vec2",
        FieldKind::Vec3 => "vec3",
        FieldKind::Vec4 => "vec4",
        FieldKind::String => "string",
        FieldKind::Enum => "enum",
        FieldKind::Color => "color",
    }
}

fn field_value_json(value: &doppio_engine::FieldValue) -> Value {
    use doppio_engine::FieldValue;
    match value {
        FieldValue::F32(number) => json!(number),
        FieldValue::Bool(flag) => json!(flag),
        FieldValue::I32(number) => json!(number),
        FieldValue::U32(number) => json!(number),
        FieldValue::Vec2(vector) => json!(vector),
        FieldValue::Vec3(vector) => json!(vector),
        FieldValue::Vec4(vector) => json!(vector),
        FieldValue::String(text) => json!(text),
        FieldValue::Enum(index) => json!(index),
        FieldValue::Color(color) => json!(color),
    }
}

fn parse_field_value(kind: &str, value: &Value) -> Result<doppio_engine::FieldValue, String> {
    use doppio_engine::FieldValue;
    let number = |value: &Value| {
        value
            .as_f64()
            .map(|number| number as f32)
            .ok_or_else(|| format!("expected a number, got {value}"))
    };
    match kind {
        "f32" => Ok(FieldValue::F32(number(value)?)),
        "i32" => Ok(FieldValue::I32(
            value
                .as_i64()
                .ok_or_else(|| format!("expected an integer, got {value}"))? as i32,
        )),
        "u32" => Ok(FieldValue::U32(
            value
                .as_u64()
                .ok_or_else(|| format!("expected an unsigned integer, got {value}"))?
                as u32,
        )),
        "bool" => {
            Ok(FieldValue::Bool(value.as_bool().ok_or_else(|| {
                format!("expected a boolean, got {value}")
            })?))
        }
        "string" => Ok(FieldValue::String(
            value
                .as_str()
                .ok_or_else(|| format!("expected a string, got {value}"))?
                .to_owned(),
        )),
        "enum" => Ok(FieldValue::Enum(
            value
                .as_i64()
                .ok_or_else(|| format!("expected an enum index, got {value}"))? as i32,
        )),
        "color" => {
            let array = value
                .as_array()
                .ok_or_else(|| format!("expected an RGBA array, got {value}"))?;
            if array.len() != 4 {
                return Err("color needs four numbers".to_owned());
            }
            let mut color = [0.0; 4];
            for (index, item) in array.iter().enumerate() {
                color[index] = number(item)?;
            }
            Ok(FieldValue::Color(color))
        }
        "vec2" | "vec3" | "vec4" => {
            let array = value
                .as_array()
                .ok_or_else(|| format!("expected an array, got {value}"))?;
            let expected = if kind == "vec2" {
                2
            } else if kind == "vec3" {
                3
            } else {
                4
            };
            if array.len() != expected {
                return Err(format!("{kind} needs {expected} numbers"));
            }
            let mut vector = [0.0; 4];
            for (index, item) in array.iter().enumerate() {
                vector[index] = number(item)?;
            }
            Ok(match kind {
                "vec2" => FieldValue::Vec2([vector[0], vector[1]]),
                "vec3" => FieldValue::Vec3([vector[0], vector[1], vector[2]]),
                _ => FieldValue::Vec4(vector),
            })
        }
        other => Err(format!("unknown field kind \"{other}\"")),
    }
}

fn dryad_json(app: &App) -> Value {
    let graph = app.dryad.graph.as_ref();
    json!({
        "script_path": app.dryad.script_path.as_ref().map(|path| path.display().to_string()),
        "script_relative": app.dryad.script_relative,
        "dirty": app.dryad.dirty,
        "selected": app.dryad.selected,
        "selected_link": app.dryad.selected_link,
        "status": app.dryad.status,
        "nodes": graph.map(|graph| graph.nodes.iter().map(|node| json!({
            "id": node.id,
            "kind": node.kind.type_name(),
            "x": node.x,
            "y": node.y,
            "number_value": node.number_value,
            "bool_value": node.bool_value,
            "constant_is_bool": node.constant_is_bool,
        })).collect::<Vec<_>>()).unwrap_or_default(),
        "edges": graph.map(|graph| graph.edges.iter().map(|edge| json!({
            "from": edge.from,
            "to": edge.to,
        })).collect::<Vec<_>>()).unwrap_or_default(),
        "catalog": crate::uikit::dryad::catalog().iter().map(|entry| json!({
            "kind": entry.type_name,
            "label": entry.display_name,
            "category": entry.category,
            "description": entry.description,
        })).collect::<Vec<_>>(),
    })
}

fn project_info_json(project: &crate::uikit::project::ProjectInfo) -> Value {
    json!({
        "name": project.name,
        "root": project.root.display().to_string(),
        "manifest": project.manifest_path().display().to_string(),
        "template": project.template,
        "last_scene": project.last_scene,
        "assets_raw": project.paths.assets_raw,
    })
}

fn project_json(app: &App) -> Value {
    let tab = match app.project.tab {
        project_manager::Tab::Create => "create",
        project_manager::Tab::Recent => "recent",
        project_manager::Tab::Browse => "browse",
    };
    let asset_view = match app.asset_view_mode {
        crate::uikit::panels::assetmanager::ViewMode::Grid => "grid",
        crate::uikit::panels::assetmanager::ViewMode::List => "list",
    };
    json!({
        "open": app.project.open,
        "tab": tab,
        "active": app.project.active.as_ref().map(project_info_json),
        "name": app.project.name,
        "location": app.project.location,
        "template": app.project.template.manifest_name(),
        "error": app.project.error,
        "recent": app.project.recent.iter().map(project_info_json).collect::<Vec<_>>(),
        "selected_recent": app.project.selected_recent,
        "browse_path": app.project.browse_path,
        "browse_results": app.project.browse_results.iter().map(project_info_json).collect::<Vec<_>>(),
        "selected_browse": app.project.selected_browse,
        "asset_directory": app.asset_directory,
        "asset_search": app.asset_search,
        "asset_view": asset_view,
        "asset_selected": app.asset_selected,
        "asset_count": app.assets.len(),
    })
}

fn history_json(app: &App) -> Value {
    let cursor = app.history.cursor();
    let steps: Vec<Value> = app
        .history
        .steps()
        .iter()
        .enumerate()
        .map(|(index, step)| {
            json!({
                "step": index + 1,
                "name": step.name,
                "current": index + 1 == cursor,
                "undone": index + 1 > cursor,
            })
        })
        .collect();
    json!({
        "cursor": cursor,
        "steps": steps,
        "can_undo": app.history.can_undo(),
        "can_redo": app.history.can_redo(),
        "undo_name": app.history.undo_name(),
        "redo_name": app.history.redo_name(),
        "bytes": app.history.bytes(),
    })
}

fn catalog_json() -> Value {
    Value::Array(
        id::CATALOG
            .iter()
            .map(|meta| {
                json!({
                    "id": meta.id.key(),
                    "group": meta.id.group(),
                    "subgroup": meta.id.subgroup(),
                    "object": meta.id.object(),
                    "kind": meta.kind,
                    "description": meta.description,
                })
            })
            .collect(),
    )
}

fn describe_state(app: &App) -> String {
    let engine = if app.engine.is_some() {
        "C ABI"
    } else {
        "fixtures"
    };
    let selected = app
        .entities
        .get(app.selected)
        .map(|entity| format!("{} (#{})", entity.name, entity.id))
        .unwrap_or_else(|| "none".to_owned());
    format!(
        "Doppio: engine={engine}, entities={}, selected={selected}, assets={}, playing={}, dirty={}, camera=(yaw {:.2}, pitch {:.2}, distance {:.2})",
        app.entities.len(),
        app.assets.len(),
        app.playing,
        app.engine
            .as_ref()
            .map(|engine| engine.is_dirty())
            .unwrap_or(false),
        app.camera.yaw,
        app.camera.pitch,
        app.camera.distance,
    )
}

fn entities_json(app: &App) -> Value {
    Value::Array(
        app.entities
            .iter()
            .enumerate()
            .map(|(index, entity)| {
                json!({
                    "index": index,
                    "id": entity.id,
                    "name": entity.name,
                    "selected": index == app.selected,
                    "parent": entity.parent,
                    "position": entity.position,
                    "rotation": entity.rotation,
                    "scale": entity.scale,
                })
            })
            .collect(),
    )
}

fn texture_set_json(app: &App) -> Value {
    app.texture_set.as_ref().map_or(Value::Null, |set| {
        json!({
            "name": set.name,
            "path": app.texture_set_path,
            "dirty": app.texture_set_dirty,
            "tile_size": set.tile_size,
            "tiles": set.tiles(),
            "active_tile": set.active_tile,
            "channels": set.channels.iter().map(|channel| json!({
                "name": channel.name,
                "kind": match channel.kind {
                    crate::uikit::texture_set::ChannelKind::Color => "color",
                    crate::uikit::texture_set::ChannelKind::Scalar => "scalar",
                },
                "srgb": channel.srgb,
            })).collect::<Vec<_>>(),
            "active_channel": set.channels.get(set.active_channel).map(|channel| channel.name.clone()),
            "layers": set.layers.iter().enumerate().map(|(index, layer)| json!({
                "index": index,
                "name": layer.name,
                "visible": layer.visible,
                "opacity": layer.opacity,
                "blend": layer.blend.label(),
                "active": index == set.active_layer,
                "paintable": matches!(layer.kind, crate::uikit::texture_set::LayerKind::Paint),
                "has_mask": !layer.masks.is_empty(),
            })).collect::<Vec<_>>(),
            "brush": {
                "radius": set.brush.radius,
                "hardness": set.brush.hardness,
                "opacity": set.brush.opacity,
                "color": set.brush.color,
                "mask": set.paint_mask,
            },
            "paint_mode": app.texture_set_paint_mode,
            "can_undo": app.texture_set_history.can_undo(),
            "can_redo": app.texture_set_history.can_redo(),
            "status": app.texture_set_status,
        })
    })
}

fn generator_json(app: &App) -> Value {
    app.material.as_ref().map_or(Value::Null, |document| {
        let graph = &document.generator;
        json!({
            "material": document.surface.name,
            "resolution": graph.resolution,
            "selected": app.generator_selected,
            "selected_link": app.generator_link.map(|(to, input)| json!({ "to": to, "input": input })),
            "status": app.generator_status,
            "baking": app.generator_bake.as_ref().map(|job| json!({
                "progress": job.progress,
                "stage": job.stage,
            })),
            "preview": app.generator_preview.is_some(),
            "preview_revision": app.generator_preview_revision,
            "snap": app.generator_snap,
            "search": app.generator_search,
            "clipboard": app.generator_clipboard.as_ref().map(|node| node.kind.title()),
            "packing": document.generator_packing.label(),
            "packing_channels": document.generator_packing.channels.iter().map(|source| source.label()).collect::<Vec<_>>(),
            "bits": document.generator_bits,
            "can_undo": document.generator_history.can_undo(),
            "undo_name": document.generator_history.undo_name(),
            "can_redo": document.generator_history.can_redo(),
            "redo_name": document.generator_history.redo_name(),
            "history": document.generator_history.len(),
            "placeholder_nodes": graph
                .nodes
                .iter()
                .filter(|node| matches!(node.kind, generators::NodeKind::Unknown { .. }))
                .count(),
            "nodes": graph.nodes.iter().map(|node| json!({
                "id": node.id,
                "kind": node.kind.title(),
                "subtitle": node.kind.subtitle(),
                "x": node.position.x,
                "y": node.position.y,
                "inputs": node.kind.inputs().iter().map(|port| json!({
                    "label": port.label,
                    "type": port.kind.label(),
                })).collect::<Vec<_>>(),
                "outputs": node.kind.outputs().iter().map(|port| json!({
                    "label": port.label,
                    "type": port.kind.label(),
                })).collect::<Vec<_>>(),
            })).collect::<Vec<_>>(),
            "links": graph.links.iter().map(|link| json!({
                "from": link.from,
                "output": link.output,
                "to": link.to,
                "input": link.input,
            })).collect::<Vec<_>>(),
        })
    })
}

fn material_json(app: &App) -> Value {
    app.material.as_ref().map_or(Value::Null, |document| {
        json!({
            "name": document.surface.name,
            "path": document.asset_path,
            "dirty": document.dirty,
            "status": app.material_baking,
            "preview": app.material_preview.is_some(),
            "preview_pending": app.material_preview_pending,
            "preview_shape": app.material_preview_shape,
            "preview_environment": app.material_preview_environment,
            "preview_exposure": app.material_preview_exposure,
            "source": document.source,
            "albedo": document.surface.albedo,
            "metallic": document.surface.metallic,
            "roughness": document.surface.roughness,
            "reflectance": document.surface.reflectance,
            "emission": document.surface.emission,
            "emission_strength": document.surface.emission_strength,
            "albedo_map": document.surface.albedo_map,
            "normal_map": document.surface.normal_map,
            "orm_map": document.surface.orm_map,
            "emission_map": document.surface.emission_map,
            "normal_strength": document.surface.normal_strength,
            "ao_strength": document.surface.ao_strength,
            "uv_tiling": document.surface.uv_tiling,
            "uv_object_scale": document.surface.uv_object_scale,
            "uv_offset": document.surface.uv_offset,
            "alpha_mode": document.surface.alpha_mode.label(),
            "alpha_cutoff": document.surface.alpha_cutoff,
            "ior": document.surface.ior,
            "transmission": document.surface.transmission,
            "clearcoat": document.surface.clearcoat,
            "clearcoat_roughness": document.surface.clearcoat_roughness,
            "iridescence": document.surface.iridescence,
            "translucency": document.surface.translucency,
            "mod_tint": document.surface.mod_tint,
            "mod_wetness": document.surface.mod_wetness,
            "mod_snow": document.surface.mod_snow,
            "mod_age": document.surface.mod_age,
            "wind_strength": document.surface.wind_strength,
            "wind_speed": document.surface.wind_speed,
            "wind_flutter": document.surface.wind_flutter,
            "reflection": document.surface.reflection,
        })
    })
}

fn selection_json(app: &App) -> Value {
    app.entities
        .get(app.selected)
        .map_or(Value::Null, |entity| {
            let world_position = app
                .engine
                .as_ref()
                .and_then(|engine| engine.world_position(entity.id).ok())
                .unwrap_or(entity.position);
            json!({
                "index": app.selected,
                "id": entity.id,
                "name": entity.name,
                "position": entity.position,
                "world_position": world_position,
                "rotation": entity.rotation,
                "scale": entity.scale,
            })
        })
}

fn scene_json(app: &App) -> Value {
    let path = app.engine.as_ref().and_then(|engine| engine.scene_path());
    let dirty = app
        .engine
        .as_ref()
        .map(|engine| engine.is_dirty())
        .unwrap_or(false);
    json!({
        "engine": app.engine.is_some(),
        "path": path,
        "dirty": dirty,
        "entity_count": app.entities.len(),
        "asset_count": app.assets.len(),
        "log": app.log.iter().map(|entry| &entry.text).collect::<Vec<_>>(),
    })
}

fn optional_str<'a>(args: &'a Value, key: &str) -> Result<Option<&'a str>, String> {
    match args.get(key) {
        None | Some(Value::Null) => Ok(None),
        Some(Value::String(value)) => Ok(Some(value.as_str())),
        Some(_) => Err(format!("\"{key}\" must be a string")),
    }
}

fn required_str<'a>(args: &'a Value, key: &str) -> Result<&'a str, String> {
    optional_str(args, key)?.ok_or_else(|| format!("missing \"{key}\""))
}

fn optional_f32(args: &Value, key: &str) -> Result<Option<f32>, String> {
    match args.get(key) {
        None | Some(Value::Null) => Ok(None),
        Some(value) => value
            .as_f64()
            .map(|number| Some(number as f32))
            .ok_or_else(|| format!("\"{key}\" must be a number")),
    }
}

fn camera_focus(args: &Value, default: [f32; 3]) -> Result<[f32; 3], String> {
    let Some(value) = args.get("focus") else {
        return Ok(default);
    };
    let array = value
        .as_array()
        .ok_or_else(|| "\"focus\" must be an [x, y, z] array".to_owned())?;
    if array.len() != 3 {
        return Err("\"focus\" needs three numbers".to_owned());
    }
    let mut focus = default;
    for (axis, item) in array.iter().enumerate() {
        focus[axis] = item
            .as_f64()
            .map(|number| number as f32)
            .ok_or_else(|| "\"focus\" needs three numbers".to_owned())?;
    }
    Ok(focus)
}

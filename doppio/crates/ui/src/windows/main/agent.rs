//! Agent bridge (`--features agent`).
//!
//! `iced-mcp` exposes the real widget tree to an MCP client, so an agent can
//! click, type, snapshot and record flows. On top of that we register the
//! scene-level operations that the widget tree cannot describe precisely:
//! entity inspection, camera control and the editor commands.

use super::assetpick;
use super::{App, Message};
use crate::uikit::asset_kind::AssetKind;
use crate::uikit::components::gizmo;
use crate::uikit::generators;
use crate::uikit::id;
use crate::uikit::panels;
use crate::uikit::panels::audio as audio_panel;
use crate::uikit::panels::dryad_nodes;
use crate::uikit::panels::environment as environment_panel;
use crate::uikit::panels::file_browser as file_browser_panel;
use crate::uikit::panels::material;
use crate::uikit::panels::material_generator;
use crate::uikit::panels::nature_generator;
use crate::uikit::panels::performance as performance_panel;
use crate::uikit::panels::photon as photon_panel;
use crate::uikit::panels::profiler as profiler_panel;
use crate::uikit::panels::project_manager;
use crate::uikit::panels::terrain_brush as terrain_brush_panel;
use crate::uikit::panels::terrain_generator;
use crate::uikit::panels::terrain_paint as terrain_paint_panel;
use crate::uikit::panels::terrain_vegetation as terrain_vegetation_panel;
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
        .query(
            "photon",
            "Photon global illumination: whether the scene has the component, its settings and the voxel far-field statistics.",
            |app, _| Ok(photon_json(app)),
        )
        .action(
            "show_photon",
            "Brings the Photon tab to the front. args: {}",
            |_| Ok(Message::ShowPhoton),
        )
        .action(
            "photon_add",
            "Adds the Photon GI component on a new entity when the scene has none (on, compute path, High). args: {}",
            |_| Ok(Message::Photon(photon_panel::Edit::Add)),
        )
        .action(
            "photon_set",
            "Sets one Photon setting by its field name: Enabled, Voxel skinned meshes, Voxel cut-out meshes, Voxel terrain, Full rebuilds (bool); Intensity, Bounce gain, Sky intensity, Ambient, Radius (number); Path, Quality, Debug view (option name or index). args: { \"field\": string, \"value\": bool | number | string }",
            |args| {
                let field = required_str(args, "field")?;
                let value = args.get("value").ok_or_else(|| "missing \"value\"".to_owned())?;
                photon_edit(field, value).map(Message::Photon)
            },
        )
        .action(
            "photon_preset",
            "Applies a Photon preset by name (Balanced, Interior (strong bounce), Outdoor, Fast (Eco), Diagnostic (GI only)). args: { \"name\": string }",
            |args| {
                let name = required_str(args, "name")?;
                photon_panel::presets()
                    .iter()
                    .position(|preset| preset.name.eq_ignore_ascii_case(name))
                    .map(|index| Message::Photon(photon_panel::Edit::Preset(index)))
                    .ok_or_else(|| format!("no Photon preset named \"{name}\""))
            },
        )
        .action(
            "photon_rebuild",
            "Throws Photon's voxel volume away; the next frame voxelises the whole scene again. args: {}",
            |_| Ok(Message::Photon(photon_panel::Edit::Rebuild)),
        )
        .query(
            "environment",
            "The scene's environment (clouds, fog, rain): whether the scene has the components, every setting by its key, and the preset names.",
            |app, _| Ok(environment_json(app)),
        )
        .action(
            "show_environment",
            "Brings the Environment tab to the front. args: {}",
            |_| Ok(Message::ShowEnvironment),
        )
        .action(
            "environment_add",
            "Adds the Weather and Volumetric Clouds components on a new entity when the scene has none (starts from the Clear sky preset). args: {}",
            |_| Ok(Message::Environment(environment_panel::Edit::Add)),
        )
        .action(
            "environment_set",
            "Sets one environment setting by its key (see the `environment` query): switches (clouds_enabled, clouds_dim_sun, weather_enabled, link_sky, fog_enabled, rain_enabled) take a bool; numbers (coverage, cloud_density, fog_density, rain_intensity, ...) take a number; clouds_quality takes Off, Low, Medium, High or 0..3; cloud_tint and fog_color take [r, g, b] in 0..1. args: { \"field\": string, \"value\": bool | number | string | [number] }",
            |args| {
                let field = required_str(args, "field")?;
                let value = args.get("value").ok_or_else(|| "missing \"value\"".to_owned())?;
                environment_edit(field, value).map(Message::Environment)
            },
        )
        .action(
            "environment_preset",
            "Applies an environment preset by name (Off, Clear sky, Cloudy, Fog, Light rain, Storm). args: { \"name\": string }",
            |args| {
                let name = required_str(args, "name")?;
                environment_panel::presets()
                    .iter()
                    .position(|preset| preset.name.eq_ignore_ascii_case(name))
                    .map(|index| Message::Environment(environment_panel::Edit::Preset(index)))
                    .ok_or_else(|| format!("no environment preset named \"{name}\""))
            },
        )
        .query(
            "performance",
            "The render options: whether the scene has the Forward Render Features component, every setting by its key (instancing, occlusion, reflections, volumetrics, ibl and their numbers), and the editor viewport's render scale, anti-aliasing, shadows and grid.",
            |app, _| Ok(performance_json(app)),
        )
        .action(
            "show_performance",
            "Brings the Performance tab to the front. args: {}",
            |_| Ok(Message::ShowPerformance),
        )
        .action(
            "performance_add",
            "Adds the Forward Render Features component on a new entity when the scene has none. args: {}",
            |_| Ok(Message::Performance(performance_panel::Edit::Add)),
        )
        .action(
            "performance_set",
            "Sets one render option by its key (see the `performance` query): switches (enabled, instancing, occlusion, reflections, reflections_ssr, reflections_settled, volumetrics, ibl) take a bool; numbers (instancing_batch, occluders, occluder_radius, reflection_strength, reflection_scale, probe_resolution, max_probes, ssr_steps, fog_density) take a number. The viewport's own options: render_scale (0.5, 0.75, 1, 1.25, 1.5 or 2), anti_aliasing (off, fxaa or taa), shadows and grid. args: { \"field\": string, \"value\": bool | number | string }",
            |args| {
                let field = required_str(args, "field")?;
                let value = args.get("value").ok_or_else(|| "missing \"value\"".to_owned())?;
                performance_edit(field, value).map(Message::Performance)
            },
        )
        .query(
            "terrain_vegetation",
            "Terrain Vegetation: whether the brush is on, the terrain, whether it has vegetation, the species (uid, name, variants, density per hectare, room, priority, scale, tilt, sink, tint, drawn-to distance, procedural, visible, rules, manual instances), the selected species, the brush, the seed and the counts of manual instances, overrides and GPU bytes.",
            |app, _| Ok(terrain_vegetation_json(app)),
        )
        .query(
            "terrain_vegetation_instances",
            "The plants within a radius of a world-space point (up to 200): id, species, variant, manual or grown, position, scale. args: { \"x\": number, \"z\": number, \"radius\"?: number (metres, default 20), \"terrain_id\"?: number }",
            |app, args| {
                let x = args.get("x").and_then(Value::as_f64).ok_or("missing \"x\"")?;
                let z = args.get("z").and_then(Value::as_f64).ok_or("missing \"z\"")?;
                let radius = optional_f32(args, "radius")?.unwrap_or(20.0);
                let entity = args
                    .get("terrain_id")
                    .and_then(Value::as_u64)
                    .map(|id| id as u32)
                    .or(app.terrain_vegetation.entity)
                    .ok_or("there is no terrain")?;
                let engine = app.engine.as_ref().ok_or("no engine")?;
                let (list, total) = engine
                    .veg_instances(entity, x, z, radius, 200)
                    .ok_or("that terrain has no vegetation")?;
                Ok(json!({
                    "total": total,
                    "instances": list.iter().map(|i| json!({
                        "id": i.id.to_string(),
                        "species": i.species,
                        "variant": i.variant,
                        "manual": i.manual,
                        "position": i.position,
                        "scale": i.scale,
                    })).collect::<Vec<_>>(),
                }))
            },
        )
        .action(
            "show_terrain_vegetation",
            "Brings the Terrain Vegetation tab to the front. args: {}",
            |_| Ok(Message::ShowTerrainVegetation),
        )
        .action(
            "terrain_vegetation_active",
            "Turns the vegetation brush on or off. args: { \"active\"?: boolean (default true) }",
            |args| {
                Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::Active(
                    args.get("active").and_then(Value::as_bool).unwrap_or(true),
                )))
            },
        )
        .action(
            "terrain_vegetation_enable",
            "Vegetation on or off for the terrain (on: an empty palette to fill). One undo step. args: { \"enabled\"?: boolean (default true) }",
            |args| {
                Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::Enable(
                    args.get("enabled").and_then(Value::as_bool).unwrap_or(true),
                )))
            },
        )
        .action(
            "terrain_vegetation_add_species",
            "Adds a species (and selects it). One undo step. args: { \"name\"?: string, \"meshes\"?: [string] (project paths of the variants), \"tint\"?: [r, g, b], \"density_per_ha\"?: number, \"radius_m\"?: number }",
            |args| {
                let tint = args
                    .get("tint")
                    .and_then(Value::as_array)
                    .map(|a| a.iter().filter_map(Value::as_f64).map(|v| v as f32).collect::<Vec<_>>())
                    .filter(|v| v.len() == 3)
                    .map_or([1.0, 1.0, 1.0], |v| [v[0], v[1], v[2]]);
                let meshes = args
                    .get("meshes")
                    .and_then(Value::as_array)
                    .map(|a| a.iter().filter_map(Value::as_str).map(str::to_owned).collect())
                    .unwrap_or_default();
                Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::AddSpeciesWith {
                    name: optional_str(args, "name")?.unwrap_or("Species").to_owned(),
                    meshes,
                    tint,
                    density_per_ha: optional_f32(args, "density_per_ha")?.unwrap_or(200.0),
                    radius_m: optional_f32(args, "radius_m")?.unwrap_or(1.0),
                }))
            },
        )
        .action(
            "terrain_vegetation_select_species",
            "Selects a species: the one the brush places and the panel edits. args: { \"index\": number }",
            |args| {
                let index = args.get("index").and_then(Value::as_u64).ok_or("missing \"index\"")?;
                Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::Select(index as usize)))
            },
        )
        .action(
            "terrain_vegetation_edit_species",
            "Edits the selected species: `name`, `material` take a string, `procedural` and `visible` a bool, `add_variant` a project path of a mesh, `remove_variant` an index, and these take a number: density (per hectare), radius, priority, min_scale, max_scale, tilt, sink, tint_jitter, tint_r, tint_g, tint_b, distance. One undo step (a run of edits of one field is one). args: { \"field\": string, \"value\": string | number | bool }",
            |args| {
                let field = required_str(args, "field")?;
                let value = args.get("value").ok_or("missing \"value\"")?;
                terrain_vegetation_species_edit(field, value).map(Message::TerrainVegetation)
            },
        )
        .action(
            "terrain_vegetation_remove_species",
            "Removes the selected species and its manual instances. One undo step. args: {}",
            |_| Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::RemoveSpecies)),
        )
        .action(
            "terrain_vegetation_rule",
            "Adds a rule to the selected species: it grows only where the mask is inside a range. args: { \"mask\": \"slope\" | \"height\" | \"curvature\" }, or { \"remove\": number } to drop a rule by index",
            |args| {
                if let Some(index) = args.get("remove").and_then(Value::as_u64) {
                    return Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::RemoveRule(index as usize)));
                }
                Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::AddRule(
                    required_str(args, "mask")?.to_owned(),
                )))
            },
        )
        .action(
            "terrain_vegetation_set_rule",
            "Sets a number of a rule of the selected species: `from`, `full_from`, `full_to`, `to` (0 to 1). args: { \"rule\": number, \"field\": string, \"value\": number }",
            |args| {
                use terrain_paint_panel::RuleCorner;
                let rule = args.get("rule").and_then(Value::as_u64).ok_or("missing \"rule\"")? as usize;
                let value = args.get("value").and_then(Value::as_f64).ok_or("missing \"value\"")? as f32;
                let corner = match required_str(args, "field")? {
                    "from" => RuleCorner::Rise0,
                    "full_from" => RuleCorner::Rise1,
                    "full_to" => RuleCorner::Fall1,
                    "to" => RuleCorner::Fall0,
                    other => return Err(format!("no rule field called \"{other}\"")),
                };
                Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::Rule(rule, corner, value)))
            },
        )
        .action(
            "terrain_vegetation_set",
            "Sets one vegetation brush setting: `tool` (paint, erase, scale, rotate, tint, replace, thin) takes a name; `radius`, `strength`, `spacing`, `per_100m2`, `amount`, `seed` a number; `only_species` an index (-1 for all); `respect_rules` and `shared_shape` a bool. args: { \"field\": string, \"value\": string | number | bool }",
            |args| {
                let field = required_str(args, "field")?;
                let value = args.get("value").ok_or("missing \"value\"")?;
                terrain_vegetation_brush_edit(field, value).map(Message::TerrainVegetation)
            },
        )
        .action(
            "terrain_vegetation_reseed",
            "Grows the procedural vegetation from the seed in the panel (set it with terrain_vegetation_set `seed`). One undo step. args: {}",
            |_| Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::Reseed)),
        )
        .action(
            "terrain_vegetation_bake",
            "Turns the selected species' grown plants into manual ones (it stops growing by itself). One undo step. args: {}",
            |_| Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::Bake)),
        )
        .action(
            "terrain_vegetation_rederive",
            "Removes the baked plants of the selected species and lets it grow by itself again. One undo step. args: {}",
            |_| Ok(Message::TerrainVegetation(terrain_vegetation_panel::Edit::Rederive)),
        )
        .action(
            "terrain_vegetation_stroke",
            "Plants or edits along world-space points (metres) with the selected species and the vegetation brush, exactly as dragging the mouse would: one undo step. args: { \"points\": [[x, z] or [x, y, z], ...], \"terrain_id\"?: number (default: the selected terrain) }",
            |args| {
                let points = args
                    .get("points")
                    .and_then(Value::as_array)
                    .ok_or("missing \"points\": [[x, z], ...]")?
                    .iter()
                    .map(|point| {
                        let numbers: Vec<f64> = point
                            .as_array()
                            .map(|a| a.iter().filter_map(Value::as_f64).collect())
                            .unwrap_or_default();
                        match numbers.as_slice() {
                            [x, z] => Ok([*x, 0.0, *z]),
                            [x, y, z] => Ok([*x, *y, *z]),
                            _ => Err("a point is [x, z] or [x, y, z] in metres".to_owned()),
                        }
                    })
                    .collect::<Result<Vec<_>, String>>()?;
                Ok(Message::TerrainVegetationStroke {
                    terrain: args.get("terrain_id").and_then(Value::as_u64).map(|id| id as u32),
                    points,
                })
            },
        )
        .query(
            "terrain_paint",
            "Terrain Paint: whether the paint brush is on, the terrain being edited, whether it has layered painting, the palette (name, picture, tile, rotation, tint, resolution, locked, texels covered, share of the terrain), the selected layer, the brush (mode, radius, strength, spacing, falloff), the auto-layering rules, the GPU array and the status line.",
            |app, _| Ok(terrain_paint_json(app)),
        )
        .query(
            "terrain_layer_at",
            "The layers under a world-space point of a terrain, up to four with their weights (what an eyedropper reads). args: { \"x\": number, \"z\": number, \"terrain_id\"?: number }",
            |app, args| {
                let x = args.get("x").and_then(Value::as_f64).ok_or("missing \"x\"")?;
                let z = args.get("z").and_then(Value::as_f64).ok_or("missing \"z\"")?;
                let entity = args
                    .get("terrain_id")
                    .and_then(Value::as_u64)
                    .map(|id| id as u32)
                    .or(app.terrain_paint.entity)
                    .ok_or("there is no terrain")?;
                let engine = app.engine.as_ref().ok_or("no engine")?;
                let sample = engine
                    .terrain_layer_at(entity, [x, 0.0, z])
                    .ok_or("that point is off the terrain, or its layers are off")?;
                Ok(json!({
                    "layers": (0..4).filter(|i| sample.weight[*i] > 0.0).map(|i| json!({
                        "layer": sample.layer[i],
                        "name": app.terrain_paint.palette.get(sample.layer[i] as usize).map(|l| l.name.clone()),
                        "weight": sample.weight[i],
                    })).collect::<Vec<_>>(),
                }))
            },
        )
        .action(
            "show_terrain_paint",
            "Brings the Terrain Paint tab to the front. args: {}",
            |_| Ok(Message::ShowTerrainPaint),
        )
        .action(
            "terrain_paint_active",
            "Turns the paint brush on or off (the brush cursor follows the mouse while it is on). args: { \"active\"?: boolean (default true) }",
            |args| {
                Ok(Message::TerrainPaint(terrain_paint_panel::Edit::Active(
                    args.get("active").and_then(Value::as_bool).unwrap_or(true),
                )))
            },
        )
        .action(
            "terrain_paint_layers",
            "Layered painting on or off for the terrain (on: a palette of up to 64 layers, starting from its four). One undo step. args: { \"enabled\"?: boolean (default true) }",
            |args| {
                Ok(Message::TerrainPaint(terrain_paint_panel::Edit::EnableLayers(
                    args.get("enabled").and_then(Value::as_bool).unwrap_or(true),
                )))
            },
        )
        .action(
            "terrain_paint_add_layer",
            "Adds a layer to the palette (and selects it). One undo step. args: { \"name\"?: string, \"albedo\"?: string (project path of the picture), \"tint\"?: [r, g, b] (0 to 1), \"tile_size_m\"?: number }",
            |args| {
                let tint = args
                    .get("tint")
                    .and_then(Value::as_array)
                    .map(|a| a.iter().filter_map(Value::as_f64).map(|v| v as f32).collect::<Vec<_>>())
                    .filter(|v| v.len() == 3)
                    .map_or([0.55, 0.55, 0.55], |v| [v[0], v[1], v[2]]);
                Ok(Message::TerrainPaint(terrain_paint_panel::Edit::AddLayerWith {
                    name: optional_str(args, "name")?.unwrap_or("Layer").to_owned(),
                    albedo: optional_str(args, "albedo")?.unwrap_or_default().to_owned(),
                    tint,
                    tile_size_m: optional_f32(args, "tile_size_m")?.unwrap_or(8.0),
                }))
            },
        )
        .action(
            "terrain_paint_select_layer",
            "Selects a layer of the palette: the one the brush paints. args: { \"index\": number }",
            |args| {
                let index = args.get("index").and_then(Value::as_u64).ok_or("missing \"index\"")?;
                Ok(Message::TerrainPaint(terrain_paint_panel::Edit::SelectLayer(index as usize)))
            },
        )
        .action(
            "terrain_paint_edit_layer",
            "Edits the selected layer: `name`, `albedo` (project path) take a string, `tile`, `rotation`, `tint_r`, `tint_g`, `tint_b` a number, `resolution` 256 to 4096, `locked` a bool. One undo step (a run of edits of one field is one). args: { \"field\": string, \"value\": string | number | bool }",
            |args| {
                let field = required_str(args, "field")?;
                let value = args.get("value").ok_or("missing \"value\"")?;
                terrain_paint_layer_edit(field, value).map(Message::TerrainPaint)
            },
        )
        .action(
            "terrain_paint_remove_layer",
            "Removes the selected layer: its paint goes to the others and the layers after it move down. One undo step. args: {}",
            |_| Ok(Message::TerrainPaint(terrain_paint_panel::Edit::RemoveLayer)),
        )
        .action(
            "terrain_paint_move_layer",
            "Moves the selected layer up (negative) or down (positive) by this many places; the painting follows it. args: { \"delta\": number }",
            |args| {
                let delta = args.get("delta").and_then(Value::as_i64).ok_or("missing \"delta\"")?;
                Ok(Message::TerrainPaint(terrain_paint_panel::Edit::MoveLayer(delta as i32)))
            },
        )
        .action(
            "terrain_paint_set",
            "Sets one paint brush setting: `mode` (paint, erase, replace, smooth), `falloff` (smooth, linear, sharp, constant) take a name; `radius`, `strength`, `spacing` a number; `from_layer` an index (Replace); `shared_shape` a bool (use the Terrain Brush's stamp, jitter, symmetry and protection). args: { \"field\": string, \"value\": string | number | bool }",
            |args| {
                let field = required_str(args, "field")?;
                let value = args.get("value").ok_or("missing \"value\"")?;
                terrain_paint_brush_edit(field, value).map(Message::TerrainPaint)
            },
        )
        .action(
            "terrain_paint_rule",
            "Adds an auto-layering rule: the selected layer goes where the mask is inside a trapezoid (0 to 1). Several rules on one layer must all hold. args: { \"mask\": \"slope\" | \"height\" | \"curvature\" } (then `terrain_paint_set_rule`), or { \"clear\": true } to remove every rule",
            |args| {
                if args.get("clear").and_then(Value::as_bool).unwrap_or(false) {
                    return Ok(Message::TerrainPaintClearRules);
                }
                Ok(Message::TerrainPaint(terrain_paint_panel::Edit::AddRule(
                    required_str(args, "mask")?.to_owned(),
                )))
            },
        )
        .action(
            "terrain_paint_set_rule",
            "Sets a number of an auto-layering rule: `from`, `full_from`, `full_to`, `to` (0 to 1), or `layer` (a palette index). args: { \"rule\": number, \"field\": string, \"value\": number }",
            |args| {
                let rule = args.get("rule").and_then(Value::as_u64).ok_or("missing \"rule\"")? as usize;
                let field = required_str(args, "field")?;
                let value = args.get("value").and_then(Value::as_f64).ok_or("missing \"value\"")?;
                use terrain_paint_panel::{Edit, RuleCorner};
                Ok(Message::TerrainPaint(match field {
                    "from" => Edit::RuleCorner(rule, RuleCorner::Rise0, value as f32),
                    "full_from" => Edit::RuleCorner(rule, RuleCorner::Rise1, value as f32),
                    "full_to" => Edit::RuleCorner(rule, RuleCorner::Fall1, value as f32),
                    "to" => Edit::RuleCorner(rule, RuleCorner::Fall0, value as f32),
                    "layer" => Edit::RuleLayer(rule, value as usize),
                    other => return Err(format!("no rule field called \"{other}\"")),
                }))
            },
        )
        .action(
            "terrain_paint_autolayer",
            "Paints the layers from the rules, with `fallback` (a palette index, default 0) where none applies. One undo step; locked layers keep their weight. args: { \"fallback\"?: number }",
            |args| {
                use terrain_paint_panel::Edit;
                if let Some(fallback) = args.get("fallback").and_then(Value::as_u64) {
                    return Ok(Message::TerrainPaintApplyRules { fallback: Some(fallback as usize) });
                }
                let _ = Edit::ApplyRules;
                Ok(Message::TerrainPaintApplyRules { fallback: None })
            },
        )
        .action(
            "terrain_paint_stroke",
            "Paints one stroke with the selected layer and the paint brush along world-space points (metres), exactly as dragging the mouse would: one undo step. args: { \"points\": [[x, z] or [x, y, z], ...], \"terrain_id\"?: number (default: the selected terrain) }",
            |args| {
                let points = args
                    .get("points")
                    .and_then(Value::as_array)
                    .ok_or("missing \"points\": [[x, z], ...]")?
                    .iter()
                    .map(|point| {
                        let numbers: Vec<f64> = point
                            .as_array()
                            .map(|a| a.iter().filter_map(Value::as_f64).collect())
                            .unwrap_or_default();
                        match numbers.as_slice() {
                            [x, z] => Ok([*x, 0.0, *z]),
                            [x, y, z] => Ok([*x, *y, *z]),
                            _ => Err("a point is [x, z] or [x, y, z] in metres".to_owned()),
                        }
                    })
                    .collect::<Result<Vec<_>, String>>()?;
                Ok(Message::TerrainPaintStroke {
                    terrain: args.get("terrain_id").and_then(Value::as_u64).map(|id| id as u32),
                    points,
                })
            },
        )
        .query(
            "terrain_brush",
            "The Terrain Brush: whether the sculpting tool is on, the library (count, categories, names), the selected brush and the working brush's every setting by key (mode, falloff, symmetry, signed, radius, strength, spacing, rotation, target_height, step, talus, iterations, droplets, offset_x, offset_z, position_jitter, rotation_jitter, size_jitter, opacity_jitter, radial_count, center_x, center_z, pressure), its protection terms, the terrain under the mouse and the status line.",
            |app, _| Ok(terrain_brush_json(app)),
        )
        .action(
            "show_terrain_brush",
            "Brings the Terrain Brush tab to the front and loads the brush library. args: {}",
            |_| Ok(Message::ShowTerrainBrush),
        )
        .action(
            "terrain_brush_active",
            "Turns the sculpting tool on or off (the brush cursor follows the mouse while it is on). args: { \"active\"?: boolean (default true) }",
            |args| {
                Ok(Message::TerrainBrush(terrain_brush_panel::Edit::Active(
                    args.get("active").and_then(Value::as_bool).unwrap_or(true),
                )))
            },
        )
        .action(
            "terrain_brush_select",
            "Makes a brush of the library the working brush. args: { \"name\": string (case-insensitive) } or { \"index\": number }",
            |args| {
                if let Some(index) = args.get("index").and_then(Value::as_u64) {
                    return Ok(Message::TerrainBrush(terrain_brush_panel::Edit::Select(
                        index as usize,
                    )));
                }
                Ok(Message::TerrainBrushSelectNamed(
                    required_str(args, "name")?.to_owned(),
                ))
            },
        )
        .action(
            "terrain_brush_set",
            "Sets one setting of the working brush by its key (see the `terrain_brush` query): the numbers take a number, `mode`, `falloff` and `symmetry` take a name (raise, lower, smooth, flatten, ramp, terrace, pinch, bulge, stamp_absolute, stamp_max, stamp_min, clone, smudge, erode_thermal, erode_hydraulic; smooth, linear, sharp, constant; none, mirror_x, mirror_z, mirror_both, radial), `signed` takes a bool and `name` a string. args: { \"field\": string, \"value\": number | string | bool }",
            |args| {
                let field = required_str(args, "field")?;
                let value = args.get("value").ok_or_else(|| "missing \"value\"".to_owned())?;
                terrain_brush_edit(field, value).map(Message::TerrainBrush)
            },
        )
        .action(
            "terrain_brush_protect",
            "Adds a protection term to the working brush: it acts only where the mask is inside the range (values 0..1; the brush is full between `full_from` and `full_to`, fading to nothing at `from` and `to`). args: { \"mask\": \"slope\" | \"height\" | \"curvature\", \"from\"?: number, \"full_from\"?: number, \"full_to\"?: number, \"to\"?: number } (default: the whole range), or { \"clear\": true } to remove them all",
            terrain_brush_protect,
        )
        .action(
            "terrain_brush_stroke",
            "Sculpts one stroke with the working brush along world-space points (metres), exactly as dragging the mouse would: one undo step. args: { \"points\": [[x, y, z], ...] (y is ignored: the stroke follows the ground), \"terrain_id\"?: number (default: the selected entity) }",
            |args| {
                let points = args
                    .get("points")
                    .and_then(Value::as_array)
                    .ok_or_else(|| "missing \"points\": [[x, y, z], ...]".to_owned())?
                    .iter()
                    .map(|point| {
                        let numbers: Vec<f64> = point
                            .as_array()
                            .map(|a| a.iter().filter_map(Value::as_f64).collect())
                            .unwrap_or_default();
                        match numbers.as_slice() {
                            [x, z] => Ok([*x, 0.0, *z]),
                            [x, y, z] => Ok([*x, *y, *z]),
                            _ => Err("a point is [x, y, z] in metres".to_owned()),
                        }
                    })
                    .collect::<Result<Vec<_>, _>>()?;
                Ok(Message::TerrainBrushStroke {
                    terrain: args
                        .get("terrain_id")
                        .and_then(Value::as_u64)
                        .map(|id| id as u32),
                    points,
                })
            },
        )
        .action(
            "terrain_brush_save",
            "Saves the working brush as Brushes/<name>.brush in the project and adds it to the library under Custom. args: {}",
            |_| Ok(Message::TerrainBrush(terrain_brush_panel::Edit::Save)),
        )
        .action(
            "terrain_brush_search",
            "Filters the brush library by a text (name, category or mode) and by a category. args: { \"text\"?: string, \"category\"?: string (empty for all) }",
            |args| {
                // One message per field is clumsy for an agent: the text is the common one.
                if let Some(category) = args.get("category").and_then(Value::as_str) {
                    return Ok(Message::TerrainBrush(terrain_brush_panel::Edit::Category(
                        (!category.is_empty()).then(|| category.to_owned()),
                    )));
                }
                Ok(Message::TerrainBrush(terrain_brush_panel::Edit::Search(
                    optional_str(args, "text")?.unwrap_or_default().to_owned(),
                )))
            },
        )
        .query(
            "audio",
            "The scene's volumetric audio: whether the scene has the component, every setting by its key, the room round the listener (open, sky, reverb, echoes), the obstacle count and one row per Audio Emitter (distance, level, low-pass, Doppler pitch, blocked, diffracted). Reads what the Audio tab last read.",
            |app, _| Ok(audio_json(app)),
        )
        .action(
            "show_audio",
            "Brings the Audio tab to the front. args: {}",
            |_| Ok(Message::ShowAudio),
        )
        .action(
            "audio_add",
            "Adds the Volumetric Audio component on a new entity when the scene has none. args: {}",
            |_| Ok(Message::Audio(audio_panel::Edit::Add)),
        )
        .action(
            "audio_set",
            "Sets one volumetric audio setting by its key (see the `audio` query): switches (enabled, occlusion, diffraction, reverb, air_absorption, propagation_delay) take a bool; numbers (master_volume, world_volume, ambient_volume, music_volume, doppler_scale, speed_of_sound) take a number. args: { \"field\": string, \"value\": bool | number }",
            |args| {
                let field = required_str(args, "field")?;
                let value = args.get("value").ok_or_else(|| "missing \"value\"".to_owned())?;
                audio_edit(field, value).map(Message::Audio)
            },
        )
        .action(
            "audio_output",
            "Plays the volumetric mixer on this computer's sound device (true) or only monitors it silently (false). args: { \"on\": bool }",
            |args| {
                let on = args.get("on").and_then(Value::as_bool).ok_or_else(|| "missing \"on\"".to_owned())?;
                Ok(Message::Audio(audio_panel::Edit::Output(on)))
            },
        )
        .action(
            "audio_device",
            "Chooses the sound device the volumetric mixer plays on: 0 is the system default, n the nth device of the `audio` query's `devices` list. args: { \"index\": number }",
            |args| {
                let index = args.get("index").and_then(Value::as_u64).ok_or_else(|| "missing \"index\"".to_owned())?;
                Ok(Message::Audio(audio_panel::Edit::Device(index as usize)))
            },
        )
        .action(
            "audio_add_source",
            "Adds an entity with an Audio Emitter at the viewport's focus. args: {}",
            |_| Ok(Message::Audio(audio_panel::Edit::AddSource)),
        )
        .action(
            "audio_add_wall",
            "Adds a 6 x 3 x 0.3 m wall with an Acoustic Obstacle at the viewport's focus. args: {}",
            |_| Ok(Message::Audio(audio_panel::Edit::AddWall)),
        )
        .query(
            "asset_pick",
            "The shared asset picker: whether it is open, what it fills, the kinds it accepts, the current value, the search and kind filter, the kinds the project has and the rows it lists (path, name, kind).",
            |app, _| Ok(asset_pick_json(app)),
        )
        .query(
            "asset_kinds",
            "Every asset kind the editor knows: its key, label and the file extensions the importer accepts for it.",
            |_, _| Ok(asset_kinds_json()),
        )
        .query(
            "windows",
            "The editor's windows: whether detaching is available, and the tabs that are detached into windows of their own.",
            |app, _| Ok(windows_json(app)),
        )
        .action(
            "detach_tab",
            "Turns a tab into a window of its own (it leaves the dock; the last tab of the editor cannot be detached). Tab names are the pane titles or keys, e.g. \"Audio\", \"material_generator\". args: { \"tab\": string }",
            |args| {
                let name = required_str(args, "tab")?;
                super::PaneKind::from_name(name)
                    .map(Message::DetachTab)
                    .ok_or_else(|| format!("no tab named \"{name}\""))
            },
        )
        .action(
            "attach_tab",
            "Docks a detached tab back into the editor and closes its window. args: { \"tab\": string }",
            |args| {
                let name = required_str(args, "tab")?;
                super::PaneKind::from_name(name)
                    .map(Message::AttachTab)
                    .ok_or_else(|| format!("no tab named \"{name}\""))
            },
        )
        .query(
            "file_browser",
            "The file browser: whether it is open, the folder, the filters, the selection and the entries listed (name, path, folder or file, kind, size).",
            |app, _| Ok(file_browser_json(app)),
        )
        .action(
            "file_browser_open",
            "Opens the file browser over the Import assistant (open the assistant first with its Import tab). args: {}",
            |_| Ok(Message::Wizard(panels::asset_wizard::Edit::ImportBrowse)),
        )
        .action(
            "file_browser_go",
            "Types a path into the file browser's path box; follow it with `file_browser_enter` to open the folder (or select the file in its folder). `~`, relative paths and quotes work. args: { \"path\": string }",
            |args| {
                let path = required_str(args, "path")?;
                Ok(Message::FileBrowser(file_browser_panel::Edit::PathInput(path.to_owned())))
            },
        )
        .action(
            "file_browser_enter",
            "Confirms the typed path (the Enter key of the path box). args: {}",
            |_| Ok(Message::FileBrowser(file_browser_panel::Edit::PathGo)),
        )
        .action(
            "file_browser_toggle",
            "Toggles the nth listed entry (see the `file_browser` query) in the selection. args: { \"index\": number }",
            |args| {
                let index = args.get("index").and_then(Value::as_u64).ok_or_else(|| "missing \"index\"".to_owned())?;
                Ok(Message::FileBrowser(file_browser_panel::Edit::Press(index as usize)))
            },
        )
        .action(
            "file_browser_select_all",
            "Selects every importable file listed in the current folder. args: {}",
            |_| Ok(Message::FileBrowser(file_browser_panel::Edit::SelectSupported)),
        )
        .action(
            "file_browser_confirm",
            "Adds the selection to the import queue and closes the browser. args: {}",
            |_| Ok(Message::FileBrowser(file_browser_panel::Edit::Confirm)),
        )
        .action(
            "file_browser_close",
            "Closes the file browser without adding anything. args: {}",
            |_| Ok(Message::FileBrowser(file_browser_panel::Edit::Close)),
        )
        .action(
            "asset_pick_open",
            "Opens the asset picker for a string field of the selected entity's component (see `components`): only fields that take an asset (Mesh, Material, Texture, Clip Path, Script…) open it. args: { \"component\": number, \"field\": number }",
            |args| {
                let component = args.get("component").and_then(Value::as_u64).ok_or_else(|| "missing \"component\"".to_owned())?;
                let field = args.get("field").and_then(Value::as_u64).ok_or_else(|| "missing \"field\"".to_owned())?;
                Ok(Message::Inspector(panels::inspector::Edit::PickField { component: component as usize, field: field as usize }))
            },
        )
        .action(
            "asset_pick_search",
            "Types into the open asset picker's search box. args: { \"text\": string }",
            |args| {
                let text = required_str(args, "text")?;
                Ok(Message::AssetPick(assetpick::PickEdit::Search(text.to_owned())))
            },
        )
        .action(
            "asset_pick_filter",
            "Shows one kind in the open asset picker (a key from `asset_kinds`), or every accepted kind with \"all\". args: { \"kind\": string }",
            |args| {
                let kind = required_str(args, "kind")?;
                let filter = if kind.eq_ignore_ascii_case("all") {
                    None
                } else {
                    match AssetKind::from_key(kind) {
                        AssetKind::Other => return Err(format!("no asset kind \"{kind}\"")),
                        known => Some(known),
                    }
                };
                Ok(Message::AssetPick(assetpick::PickEdit::Filter(filter)))
            },
        )
        .action(
            "asset_pick_choose",
            "Chooses an asset in the open picker by its relative path: the field is set (one undo step) and the picker closes. args: { \"path\": string }",
            |args| {
                let path = required_str(args, "path")?;
                Ok(Message::AssetPick(assetpick::PickEdit::Pick(path.to_owned())))
            },
        )
        .action(
            "asset_pick_clear",
            "Empties the field the open picker was opened for and closes it. args: {}",
            |_| Ok(Message::AssetPick(assetpick::PickEdit::Clear)),
        )
        .action(
            "asset_pick_close",
            "Closes the asset picker without changing anything. args: {}",
            |_| Ok(Message::AssetPick(assetpick::PickEdit::Close)),
        )
        .query(
            "profiler",
            "The engine's timed scopes (render stages and, with GPU timing, the GPU passes): name, depth, calls, last/average/worst/self milliseconds, plus the frame time they imply. Reads the engine fresh.",
            |app, _| Ok(profiler_json(app)),
        )
        .action(
            "show_profiler",
            "Brings the Profiler tab to the front. args: {}",
            |_| Ok(Message::ShowProfiler),
        )
        .action(
            "profiler_gpu_timing",
            "Turns GPU timing on or off: every GPU pass then waits for the GPU at its edges so its time is the real cost; frames are slower while it is on. args: { \"enabled\": bool }",
            |args| {
                let on = args
                    .get("enabled")
                    .and_then(Value::as_bool)
                    .ok_or_else(|| "\"enabled\" takes true or false".to_owned())?;
                Ok(Message::Profiler(profiler_panel::Edit::Gpu(on)))
            },
        )
        .action(
            "profiler_reset",
            "Forgets every profiler sample; the averages start again. args: {}",
            |_| Ok(Message::Profiler(profiler_panel::Edit::Reset)),
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
        .action(
            "toggle_play",
            "Play / Pause: from stopped it starts the game (the scene's simulation as the compiled game runs it, shown in the Gameplay tab); while playing it pauses; while paused it resumes. No args.",
            |_| Ok(Message::TogglePlay),
        )
        .action(
            "stop_play",
            "Stops the game and puts the scene back as it was before Play. Does nothing when there is no play session. No args.",
            |_| Ok(Message::StopPlay),
        )
        .action(
            "show_gameplay",
            "Brings the Gameplay tab (the scene through its active camera) to the front. No args.",
            |_| Ok(Message::ShowGameplay),
        )
        .action(
            "show_camera_preview",
            "Brings the Camera tab (the scene through the selected Camera 3D, or the active one) to the front. No args.",
            |_| Ok(Message::ShowCameraPreview),
        )
        .action(
            "game_key",
            "Presses or releases a key for the running game (ignored unless it plays). `scancode` is the USB HID / SDL scancode: A = 4 … Z = 29, 1 = 30, Enter = 40, Space = 44, Right 79, Left 80, Down 81, Up 82. args: { \"scancode\": number, \"down\": bool }",
            |args| {
                let scancode = args.get("scancode").and_then(Value::as_u64).ok_or_else(|| "missing \"scancode\"".to_owned())?;
                let down = args.get("down").and_then(Value::as_bool).ok_or_else(|| "missing \"down\"".to_owned())?;
                Ok(Message::GameKey(scancode as u32, down))
            },
        )
        .query(
            "play",
            "The play state: stopped, playing or paused; whether the engine runs the game with its scripts; the frames and seconds the game has run; and for each scene view (gameplay, camera) whether the scene has a camera for it and how many frames it has drawn.",
            |app, _| Ok(play_json(app)),
        )
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
            "terrain_brush_cursor",
            "Turns the terrain brush cursor on or off and sizes it: while on, the viewport draws the brush's ring on the terrain under the mouse. args: { \"active\"?: boolean (default true), \"radius_m\"?: number (metres, default 6) }",
            |args| {
                Ok(Message::TerrainBrushCursor {
                    active: args.get("active").and_then(Value::as_bool).unwrap_or(true),
                    radius_m: args
                        .get("radius_m")
                        .and_then(Value::as_f64)
                        .map_or(6.0, |radius| radius as f32),
                })
            },
        )
        .action(
            "convert_terrains",
            "Saves every terrain still in the old .cterrain format as a native .terrain next to it, checked bit for bit, and points the scene at the new files (the old files stay). No args.",
            |_| Ok(Message::ConvertLegacyTerrains),
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

/// The Photon settings and voxel statistics, for the `photon` query.
fn photon_json(app: &App) -> Value {
    let Some(settings) = app.photon.settings.as_ref() else {
        return json!({ "present": false });
    };
    let stats = app.photon_stats();
    json!({
        "present": true,
        "entity": app.photon_entity_name(),
        "settings": {
            "enabled": settings.enabled,
            "path": photon_panel::PATHS.get(settings.path),
            "quality": photon_panel::QUALITIES.get(settings.quality),
            "intensity": settings.intensity,
            "radius": settings.radius,
            "sky_intensity": settings.sky,
            "ambient": settings.ambient,
            "debug_view": photon_panel::DEBUG_VIEWS.get(settings.debug_view),
            "bounce_gain": settings.bounce_gain,
            "voxel_skinned": settings.voxel_skinned,
            "voxel_cutout": settings.voxel_cutout,
            "voxel_terrain": settings.voxel_terrain,
            "full_rebuilds": settings.full_rebuilds,
        },
        "stats": stats.map(|s| json!({
            "objects": s.objects,
            "triangles": s.triangles,
            "over_budget": s.over_budget,
            "far_enabled": s.far_enabled,
            "full_rebuilds": s.full_rebuilds,
            "region_rebuilds": s.region_rebuilds,
            "last_level_mask": s.last_level_mask,
            "last_regions": s.last_regions,
            "last_update_ms": s.last_update_ms,
        })),
    })
}

/// A Photon edit from a field name and a JSON value.
pub(super) fn photon_edit(field: &str, value: &Value) -> Result<photon_panel::Edit, String> {
    use photon_panel::{Amount, Choice, Edit, Flag};
    let same = |name: &str| name.eq_ignore_ascii_case(field);
    for which in [
        Flag::Enabled,
        Flag::VoxelSkinned,
        Flag::VoxelCutout,
        Flag::VoxelTerrain,
        Flag::FullRebuilds,
    ] {
        if same(which.field()) {
            let on = value
                .as_bool()
                .ok_or_else(|| format!("\"{field}\" takes true or false"))?;
            return Ok(Edit::Flag(which, on));
        }
    }
    for which in [
        Amount::Intensity,
        Amount::BounceGain,
        Amount::Sky,
        Amount::Ambient,
        Amount::Radius,
    ] {
        if same(which.field()) {
            let number = value
                .as_f64()
                .ok_or_else(|| format!("\"{field}\" takes a number"))?;
            return Ok(Edit::Amount(which, number as f32));
        }
    }
    for which in [Choice::Path, Choice::Quality, Choice::DebugView] {
        if same(which.field()) {
            let options = which.options();
            let index = match value {
                Value::Number(n) => n.as_u64().map(|i| i as usize),
                Value::String(name) => options
                    .iter()
                    .position(|option| option.eq_ignore_ascii_case(name)),
                _ => None,
            };
            return index
                .filter(|i| *i < options.len())
                .map(|i| Edit::Choice(which, i))
                .ok_or_else(|| format!("\"{field}\" is one of {}", options.join(", ")));
        }
    }
    Err(format!("no Photon setting named \"{field}\""))
}

/// The volumetric audio, for the `audio` query.
fn audio_json(app: &App) -> Value {
    let Some(settings) = app.audio.settings.as_ref() else {
        return json!({ "present": false });
    };
    let mut values = serde_json::Map::new();
    for flag in audio_panel::Flag::ALL {
        let spec = flag.spec();
        values.insert(spec.key.to_owned(), json!(settings.flag(*flag)));
    }
    for amount in audio_panel::Amount::ALL {
        let spec = amount.spec();
        values.insert(spec.key.to_owned(), json!(settings.amount(*amount)));
    }
    let live = &app.audio.live;
    let voices: Vec<Value> = live
        .voices
        .iter()
        .map(|v| {
            json!({
                "name": v.name, "layer": audio_panel::LAYERS[v.layer.clamp(0, 3) as usize], "loaded": v.loaded,
                "playing": v.playing, "distance_m": v.distance_m, "gain_db": v.gain_db, "cutoff_hz": v.cutoff_hz,
                "pitch_ratio": v.pitch_ratio, "blocked": v.blocked, "diffracted": v.diffracted,
            })
        })
        .collect();
    json!({
        "present": true,
        "entity": app.audio_entity_name(),
        "summary": audio_panel::summary(Some(settings), live),
        "settings": Value::Object(values),
        "output_on": app.audio.output_on,
        "devices": app.audio.devices,
        "device": app.audio.device,
        "device_open": live.device_open,
        "room": { "open": live.openness, "sky": live.sky_exposure, "rt60_s": live.rt60_s, "wet": live.wet, "echoes": live.echoes },
        "obstacles": live.obstacles,
        "clips_missing": live.clips_missing,
        "voices": voices,
    })
}

/// The audio edit for a key and a JSON value.
pub(super) fn audio_edit(field: &str, value: &Value) -> Result<audio_panel::Edit, String> {
    use audio_panel::{Amount, Edit, Flag};
    let same = |name: &str| name.eq_ignore_ascii_case(field);
    for which in Flag::ALL {
        if same(which.spec().key) {
            let on = value
                .as_bool()
                .ok_or_else(|| format!("\"{field}\" takes true or false"))?;
            return Ok(Edit::Flag(*which, on));
        }
    }
    for which in Amount::ALL {
        if same(which.spec().key) {
            let number = value
                .as_f64()
                .ok_or_else(|| format!("\"{field}\" takes a number"))?;
            return Ok(Edit::Amount(*which, number as f32));
        }
    }
    Err(format!("no audio setting named \"{field}\""))
}

/// The environment settings, for the `environment` query.
fn environment_json(app: &App) -> Value {
    let presets: Vec<&str> = environment_panel::presets()
        .iter()
        .map(|preset| preset.name)
        .collect();
    let Some(settings) = app.environment.settings.as_ref() else {
        return json!({ "present": false, "presets": presets });
    };
    let mut values = serde_json::Map::new();
    for flag in environment_panel::Flag::ALL {
        let spec = flag.spec();
        values.insert(spec.key.to_owned(), json!(settings.flag(*flag)));
    }
    for amount in environment_panel::Amount::ALL {
        let spec = amount.spec();
        values.insert(spec.key.to_owned(), json!(settings.amount(*amount)));
    }
    values.insert(
        "clouds_quality".to_owned(),
        json!(environment_panel::QUALITIES.get(settings.quality)),
    );
    for tint in environment_panel::Tint::ALL {
        values.insert(tint.key().to_owned(), json!(settings.tint(tint)));
    }
    json!({
        "present": true,
        "entity": app.environment_entity_name(),
        "preset": environment_panel::current_preset(settings),
        "summary": environment_panel::summary(Some(settings)),
        "settings": Value::Object(values),
        "presets": presets,
    })
}

/// The environment edit for a key and a JSON value.
pub(super) fn environment_edit(
    field: &str,
    value: &Value,
) -> Result<environment_panel::Edit, String> {
    use environment_panel::{Amount, Edit, Flag, Tint, QUALITIES};
    let same = |name: &str| name.eq_ignore_ascii_case(field);
    for which in Flag::ALL {
        let spec = which.spec();
        if same(spec.key) {
            let on = value
                .as_bool()
                .ok_or_else(|| format!("\"{field}\" takes true or false"))?;
            return Ok(Edit::Flag(*which, on));
        }
    }
    for which in Amount::ALL {
        let spec = which.spec();
        if same(spec.key) {
            let number = value
                .as_f64()
                .ok_or_else(|| format!("\"{field}\" takes a number"))?;
            return Ok(Edit::Amount(*which, number as f32));
        }
    }
    if same("clouds_quality") {
        let index = match value {
            Value::Number(n) => n.as_u64().map(|i| i as usize),
            Value::String(name) => QUALITIES
                .iter()
                .position(|option| option.eq_ignore_ascii_case(name)),
            _ => None,
        };
        return index
            .filter(|i| *i < QUALITIES.len())
            .map(Edit::Quality)
            .ok_or_else(|| format!("\"{field}\" is one of {}", QUALITIES.join(", ")));
    }
    for which in Tint::ALL {
        if same(which.key()) {
            let channels: Vec<f32> = value
                .as_array()
                .map(|items| {
                    items
                        .iter()
                        .filter_map(|item| item.as_f64().map(|n| n as f32))
                        .collect()
                })
                .unwrap_or_default();
            if channels.len() != 3 {
                return Err(format!("\"{field}\" takes [r, g, b]"));
            }
            return Ok(Edit::Colour(which, [channels[0], channels[1], channels[2]]));
        }
    }
    Err(format!("no environment setting named \"{field}\""))
}

/// The profiler's scopes, for the `profiler` query.
pub(super) fn profiler_json(app: &App) -> Value {
    let Some(engine) = app.engine.as_ref() else {
        return json!({ "available": false });
    };
    let Ok(scopes) = engine.profiler_scopes() else {
        return json!({ "available": false });
    };
    let frame = profiler_panel::frame(&scopes);
    let history = &app.profiler.history;
    let last = |series: &[f32]| series.last().copied();
    json!({
        "available": true,
        "history": {
            "samples": history.fps.len(),
            "fps": last(&history.fps),
            "cpu_cores": history.cores.iter().map(|core| last(core)).collect::<Vec<_>>(),
            "cpu_total": last(&history.cpu_total),
            "process_cpu": last(&history.process_cpu),
            "gpu_busy": last(&history.gpu_busy),
            "vram_used_mib": last(&history.vram_used_mib),
            "vram_total_mib": history.vram_total_mib,
            "ram_used_mib": last(&history.ram_used_mib),
            "ram_process_mib": last(&history.ram_process_mib),
            "ram_total_mib": history.ram_total_mib,
        },
        "gpu_timing": engine.profiler_gpu_timing().unwrap_or(false),
        "frame_ms": frame.map(|(ms, _)| ms),
        "fps": frame.map(|(_, fps)| fps),
        "scopes": scopes
            .iter()
            .map(|scope| json!({
                "name": scope.name,
                "depth": scope.depth,
                "calls": scope.calls,
                "last_ms": scope.last_ms,
                "avg_ms": scope.avg_ms,
                "max_ms": scope.max_ms,
                "self_ms": scope.self_ms,
                "recent_avg_ms": scope.recent_avg_ms,
                "recent_max_ms": scope.recent_max_ms,
            }))
            .collect::<Vec<_>>(),
    })
}

fn asset_pick_json(app: &App) -> Value {
    let Some(pick) = app.asset_pick.open.as_ref() else {
        return json!({ "open": false });
    };
    let rows: Vec<Value> = app
        .asset_pick_rows(pick)
        .into_iter()
        .map(|row| {
            json!({
                "path": row.key,
                "name": row.label,
                "kind": AssetKind::from_key(
                    app.assets
                        .iter()
                        .find(|asset| asset.relative_path == row.key)
                        .map(|asset| asset.kind.as_str())
                        .unwrap_or("file"),
                )
                .key(),
            })
        })
        .collect();
    json!({
        "open": true,
        "title": pick.title,
        "current": pick.current,
        "search": pick.search,
        "filter": pick.filter.map(AssetKind::key),
        "accepts": pick.kinds.iter().map(|kind| kind.key()).collect::<Vec<_>>(),
        "present": app
            .asset_pick_present_kinds(pick)
            .iter()
            .map(|kind| kind.key())
            .collect::<Vec<_>>(),
        "rows": rows,
    })
}

fn asset_kinds_json() -> Value {
    Value::Array(
        AssetKind::IMPORTABLE
            .iter()
            .map(|kind| {
                json!({
                    "key": kind.key(),
                    "label": kind.label(),
                    "extensions": kind.extensions(),
                })
            })
            .collect(),
    )
}

fn file_browser_json(app: &App) -> Value {
    let Some(browser) = app.file_browser.as_ref() else {
        return json!({ "open": false });
    };
    json!({
        "open": true,
        "dir": browser.dir.display().to_string(),
        "path_input": browser.path_input,
        "search": browser.search,
        "show_hidden": browser.show_hidden,
        "only_supported": browser.only_supported,
        "error": browser.error,
        "selected": browser.selected.iter().map(|path| path.display().to_string()).collect::<Vec<_>>(),
        "places": browser.places.iter().map(|place| place.label.clone()).collect::<Vec<_>>(),
        "entries": browser.entries.iter().enumerate().map(|(index, entry)| json!({
            "index": index,
            "name": entry.name,
            "path": entry.path.display().to_string(),
            "is_dir": entry.is_dir,
            "kind": entry.kind.map(|kind| kind.key()),
            "size": entry.size,
        })).collect::<Vec<_>>(),
    })
}

fn windows_json(app: &App) -> Value {
    json!({
        "detach_available": app.detach.supported,
        "detached": app
            .detach
            .windows
            .iter()
            .map(|(_, kind)| json!({ "tab": kind.key(), "title": kind.title() }))
            .collect::<Vec<_>>(),
    })
}

fn play_json(app: &App) -> Value {
    use doppio_engine::SceneView;
    let state = match app.transport_state() {
        crate::uikit::components::transport::State::Playing => "playing",
        crate::uikit::components::transport::State::Paused => "paused",
        crate::uikit::components::transport::State::Stopped => "stopped",
    };
    let game = app
        .engine
        .as_ref()
        .map(|engine| engine.game_state())
        .unwrap_or_default();
    let view = |view: SceneView| {
        let feed = app.feed(view);
        json!({
            "has_camera": !feed.missing_camera,
            "frames_drawn": feed.requests,
            "has_picture": feed.frame.is_some(),
            "camera_entity": app.engine.as_ref().and_then(|engine| engine.scene_camera(view)).map(|(entity, _)| entity),
        })
    };
    json!({
        "state": state,
        "game_running": game.running,
        "scripts_running": game.scripts_running,
        "game_frames": game.frames,
        "game_seconds": game.time_s,
        "gameplay": view(SceneView::Game),
        "camera": view(SceneView::Camera),
    })
}

pub(super) fn performance_edit(
    field: &str,
    value: &Value,
) -> Result<performance_panel::Edit, String> {
    use performance_panel::{Amount, Edit, Flag};
    let same = |name: &str| name.eq_ignore_ascii_case(field);
    for which in Flag::ALL {
        if same(which.spec().key) {
            let on = value
                .as_bool()
                .ok_or_else(|| format!("\"{field}\" takes true or false"))?;
            return Ok(Edit::Flag(*which, on));
        }
    }
    for which in Amount::ALL {
        if same(which.spec().key) {
            let number = value
                .as_f64()
                .ok_or_else(|| format!("\"{field}\" takes a number"))?;
            return Ok(Edit::Amount(*which, number as f32));
        }
    }
    match field.to_ascii_lowercase().as_str() {
        "render_scale" => {
            let number = value
                .as_f64()
                .ok_or_else(|| "\"render_scale\" takes a number".to_owned())?;
            Ok(Edit::RenderScale(performance_panel::scale_index(
                number as f32,
            )))
        }
        "anti_aliasing" => {
            let name = value
                .as_str()
                .ok_or_else(|| "\"anti_aliasing\" takes off, fxaa or taa".to_owned())?;
            match name.to_ascii_lowercase().as_str() {
                "off" => Ok(Edit::AntiAliasing(0)),
                "fxaa" => Ok(Edit::AntiAliasing(1)),
                "taa" => Ok(Edit::AntiAliasing(2)),
                _ => Err("\"anti_aliasing\" takes off, fxaa or taa".to_owned()),
            }
        }
        "shadows" => value
            .as_bool()
            .map(Edit::Shadows)
            .ok_or_else(|| "\"shadows\" takes true or false".to_owned()),
        "grid" => value
            .as_bool()
            .map(Edit::Grid)
            .ok_or_else(|| "\"grid\" takes true or false".to_owned()),
        _ => Err(format!("no performance option named \"{field}\"")),
    }
}

fn normalized(name: &str) -> String {
    name.chars()
        .filter(char::is_ascii_alphanumeric)
        .collect::<String>()
        .to_lowercase()
}

fn terrain_brush_edit(field: &str, value: &Value) -> Result<terrain_brush_panel::Edit, String> {
    use doppio_engine::{BrushMode, Falloff, SymmetryKind};
    use terrain_brush_panel::{Amount, Edit};
    let text = || {
        value
            .as_str()
            .map(normalized)
            .ok_or_else(|| format!("\"{field}\" takes a name"))
    };
    match normalized(field).as_str() {
        "mode" => {
            let wanted = text()?;
            BrushMode::ALL
                .iter()
                .find(|mode| {
                    normalized(mode.label()) == wanted || normalized(&format!("{mode:?}")) == wanted
                })
                .map(|mode| Edit::Mode(*mode))
                .ok_or_else(|| format!("no brush mode called \"{wanted}\""))
        }
        "falloff" => {
            let wanted = text()?;
            Falloff::ALL
                .iter()
                .find(|f| normalized(f.label()) == wanted)
                .map(|f| Edit::Falloff(*f))
                .ok_or_else(|| format!("no falloff called \"{wanted}\""))
        }
        "symmetry" => {
            let wanted = text()?;
            SymmetryKind::ALL
                .iter()
                .find(|k| {
                    normalized(k.label()) == wanted
                        || normalized(&format!("{k:?}")) == wanted
                        || (wanted == "mirrorxz" && **k == SymmetryKind::MirrorBoth)
                })
                .map(|k| Edit::Symmetry(*k))
                .ok_or_else(|| format!("no symmetry called \"{wanted}\""))
        }
        "signed" => value
            .as_bool()
            .map(Edit::Signed)
            .ok_or_else(|| "\"signed\" takes a bool".to_owned()),
        "name" => value
            .as_str()
            .map(|name| Edit::Name(name.to_owned()))
            .ok_or_else(|| "\"name\" takes a string".to_owned()),
        _ => {
            let amount = Amount::from_key(field)
                .ok_or_else(|| format!("no brush setting called \"{field}\""))?;
            value
                .as_f64()
                .map(|v| Edit::Amount(amount, v as f32))
                .ok_or_else(|| format!("\"{field}\" takes a number"))
        }
    }
}

fn terrain_brush_protect(args: &Value) -> Result<Message, String> {
    use terrain_brush_panel::Edit;
    if args.get("clear").and_then(Value::as_bool).unwrap_or(false) {
        return Ok(Message::TerrainBrush(Edit::ClearProtect));
    }
    let mask = required_str(args, "mask")?;
    Ok(Message::TerrainBrush(Edit::AddProtectRange(
        mask.to_owned(),
        [
            optional_f32(args, "from")?.unwrap_or(0.0),
            optional_f32(args, "full_from")?.unwrap_or(0.0),
            optional_f32(args, "full_to")?.unwrap_or(1.0),
            optional_f32(args, "to")?.unwrap_or(1.0),
        ],
    )))
}

fn terrain_paint_layer_edit(
    field: &str,
    value: &Value,
) -> Result<terrain_paint_panel::Edit, String> {
    use terrain_paint_panel::{Edit, LayerNumber};
    match normalized(field).as_str() {
        "name" => value
            .as_str()
            .map(|v| Edit::LayerName(v.to_owned()))
            .ok_or_else(|| "\"name\" takes a string".to_owned()),
        "albedo" | "picture" => value
            .as_str()
            .map(|v| Edit::LayerAlbedo(v.to_owned()))
            .ok_or_else(|| "\"albedo\" takes a project path".to_owned()),
        "locked" => value
            .as_bool()
            .map(Edit::LayerLocked)
            .ok_or_else(|| "\"locked\" takes a bool".to_owned()),
        "resolution" | "sharpness" => value
            .as_u64()
            .map(|v| Edit::LayerResolution(v as u32))
            .ok_or_else(|| "\"resolution\" takes a number of pixels".to_owned()),
        _ => {
            let number = LayerNumber::from_key(&field.to_lowercase())
                .ok_or_else(|| format!("no layer setting called \"{field}\""))?;
            value
                .as_f64()
                .map(|v| Edit::LayerNumber(number, v as f32))
                .ok_or_else(|| format!("\"{field}\" takes a number"))
        }
    }
}

fn terrain_paint_brush_edit(
    field: &str,
    value: &Value,
) -> Result<terrain_paint_panel::Edit, String> {
    use doppio_engine::{Falloff, PaintMode};
    use terrain_paint_panel::{BrushAmount, Edit};
    let text = || {
        value
            .as_str()
            .map(normalized)
            .ok_or_else(|| format!("\"{field}\" takes a name"))
    };
    match normalized(field).as_str() {
        "mode" => {
            let wanted = text()?;
            PaintMode::ALL
                .iter()
                .find(|m| normalized(m.label()) == wanted)
                .map(|m| Edit::Mode(*m))
                .ok_or_else(|| format!("no paint mode called \"{wanted}\""))
        }
        "falloff" => {
            let wanted = text()?;
            Falloff::ALL
                .iter()
                .find(|f| normalized(f.label()) == wanted)
                .map(|f| Edit::Falloff(*f))
                .ok_or_else(|| format!("no falloff called \"{wanted}\""))
        }
        "fromlayer" => value
            .as_u64()
            .map(|v| Edit::FromLayer(v as usize))
            .ok_or_else(|| "\"from_layer\" takes a palette index".to_owned()),
        "sharedshape" => value
            .as_bool()
            .map(Edit::SharedShape)
            .ok_or_else(|| "\"shared_shape\" takes a bool".to_owned()),
        _ => {
            let amount = BrushAmount::from_key(field)
                .ok_or_else(|| format!("no paint brush setting called \"{field}\""))?;
            value
                .as_f64()
                .map(|v| Edit::Brush(amount, v as f32))
                .ok_or_else(|| format!("\"{field}\" takes a number"))
        }
    }
}

fn terrain_vegetation_species_edit(
    field: &str,
    value: &Value,
) -> Result<terrain_vegetation_panel::Edit, String> {
    use terrain_vegetation_panel::{Edit, SpeciesNumber};
    match normalized(field).as_str() {
        "name" => value
            .as_str()
            .map(|v| Edit::Name(v.to_owned()))
            .ok_or_else(|| "\"name\" takes a string".to_owned()),
        "material" => value
            .as_str()
            .map(|v| Edit::Material(v.to_owned()))
            .ok_or_else(|| "\"material\" takes a project path".to_owned()),
        "procedural" => value
            .as_bool()
            .map(Edit::Procedural)
            .ok_or_else(|| "\"procedural\" takes a bool".to_owned()),
        "visible" => value
            .as_bool()
            .map(Edit::Visible)
            .ok_or_else(|| "\"visible\" takes a bool".to_owned()),
        "addvariant" => value
            .as_str()
            .map(|v| Edit::AddVariant(v.to_owned()))
            .ok_or_else(|| "\"add_variant\" takes a project path".to_owned()),
        "removevariant" => value
            .as_u64()
            .map(|v| Edit::RemoveVariant(v as usize))
            .ok_or_else(|| "\"remove_variant\" takes an index".to_owned()),
        _ => {
            let number = SpeciesNumber::from_key(field)
                .ok_or_else(|| format!("no species setting called \"{field}\""))?;
            value
                .as_f64()
                .map(|v| Edit::Number(number, v as f32))
                .ok_or_else(|| format!("\"{field}\" takes a number"))
        }
    }
}

fn terrain_vegetation_brush_edit(
    field: &str,
    value: &Value,
) -> Result<terrain_vegetation_panel::Edit, String> {
    use doppio_engine::VegTool;
    use terrain_paint_panel::BrushAmount;
    use terrain_vegetation_panel::Edit;
    match normalized(field).as_str() {
        "tool" => {
            let wanted = value
                .as_str()
                .map(normalized)
                .ok_or("\"tool\" takes a name")?;
            VegTool::ALL
                .iter()
                .find(|t| {
                    normalized(t.label()) == wanted || (wanted == "thin" && **t == VegTool::Thin)
                })
                .map(|t| Edit::Tool(*t))
                .ok_or_else(|| format!("no vegetation tool called \"{wanted}\""))
        }
        "per100m2" => value
            .as_f64()
            .map(|v| Edit::Per100(v as f32))
            .ok_or_else(|| "\"per_100m2\" takes a number".to_owned()),
        "amount" => value
            .as_f64()
            .map(|v| Edit::Amount(v as f32))
            .ok_or_else(|| "\"amount\" takes a number".to_owned()),
        "seed" => value
            .as_u64()
            .map(Edit::Seed)
            .ok_or_else(|| "\"seed\" takes a number".to_owned()),
        "onlyspecies" => value
            .as_i64()
            .map(|v| Edit::Only(v as i32))
            .ok_or_else(|| "\"only_species\" takes an index".to_owned()),
        "respectrules" => value
            .as_bool()
            .map(Edit::Respect)
            .ok_or_else(|| "\"respect_rules\" takes a bool".to_owned()),
        "sharedshape" => value
            .as_bool()
            .map(Edit::Shared)
            .ok_or_else(|| "\"shared_shape\" takes a bool".to_owned()),
        _ => {
            let amount = BrushAmount::from_key(field)
                .ok_or_else(|| format!("no vegetation brush setting called \"{field}\""))?;
            value
                .as_f64()
                .map(|v| Edit::Brush(amount, v as f32))
                .ok_or_else(|| format!("\"{field}\" takes a number"))
        }
    }
}

fn terrain_vegetation_json(app: &App) -> Value {
    let veg = &app.terrain_vegetation;
    let info = veg.info.unwrap_or_default();
    json!({
        "active": veg.active,
        "terrain": veg.entity,
        "terrain_name": veg.terrain_name,
        "enabled": info.enabled,
        "manual_instances": info.manual_instances,
        "overrides": info.overrides,
        "seed": info.seed.to_string(),
        "gpu_bytes": info.gpu_bytes,
        "selected": veg.selected,
        "species": veg.species.iter().enumerate().map(|(i, s)| json!({
            "index": i,
            "uid": s.uid,
            "name": s.name,
            "meshes": s.meshes,
            "material": s.material,
            "density_per_ha": s.density_per_ha,
            "radius_m": s.radius_m,
            "priority": s.priority,
            "min_scale": s.min_scale,
            "max_scale": s.max_scale,
            "tilt": s.tilt,
            "sink_m": s.sink_m,
            "tint": s.tint,
            "tint_jitter": s.tint_jitter,
            "max_distance_m": s.max_distance_m,
            "procedural": s.procedural,
            "visible": s.visible,
            "manual_instances": s.manual_instances,
            "rules": s.rules.iter().map(|r| json!({
                "mask": r.mask, "from": r.rise0, "full_from": r.rise1, "full_to": r.fall1, "to": r.fall0,
            })).collect::<Vec<_>>(),
        })).collect::<Vec<_>>(),
        "brush": {
            "tool": veg.tool.label(),
            "radius": veg.radius,
            "strength": veg.strength,
            "spacing": veg.spacing,
            "per_100m2": veg.per_100m2,
            "amount": veg.amount,
            "only_species": veg.only,
            "respect_rules": veg.respect_rules,
            "shared_shape": veg.shared_shape,
            "seed": veg.seed.to_string(),
        },
        "status": veg.status,
    })
}

fn terrain_paint_json(app: &App) -> Value {
    let paint = &app.terrain_paint;
    let layers = paint.layers.unwrap_or_default();
    let total = f64::from(layers.map[0]) * f64::from(layers.map[1]);
    json!({
        "active": paint.active,
        "terrain": paint.entity,
        "terrain_name": paint.terrain_name,
        "layers_enabled": layers.enabled,
        "map": layers.map,
        "gpu": {
            "slices": layers.gpu_slices,
            "array_size": layers.gpu_array_size,
            "array_bytes": layers.gpu_array_bytes,
        },
        "selected": paint.selected,
        "palette": paint.palette.iter().enumerate().map(|(i, l)| json!({
            "index": i,
            "name": l.name,
            "albedo": l.albedo,
            "tile_size_m": l.tile_size_m,
            "rotation_degrees": l.rotation_degrees,
            "tint": l.tint,
            "resolution": l.resolution,
            "locked": l.locked,
            "texels": l.texels,
            "share": if total > 0.0 { f64::from(l.texels) / total } else { 0.0 },
        })).collect::<Vec<_>>(),
        "brush": {
            "mode": paint.mode.label(),
            "from_layer": paint.from_layer,
            "radius": paint.radius,
            "strength": paint.strength,
            "spacing": paint.spacing,
            "falloff": paint.falloff.label(),
            "shared_shape": paint.shared_shape,
        },
        "rules": paint.rules.iter().map(|r| json!({
            "layer": r.layer,
            "mask": r.mask,
            "from": r.corners[0], "full_from": r.corners[1], "full_to": r.corners[2], "to": r.corners[3],
        })).collect::<Vec<_>>(),
        "fallback": paint.fallback,
        "status": paint.status,
    })
}

fn terrain_brush_json(app: &App) -> Value {
    use terrain_brush_panel::Amount;
    let editor = &app.terrain_editor;
    let brush = &editor.working;
    let mut settings = serde_json::Map::new();
    for amount in Amount::ALL {
        settings.insert(amount.key().to_owned(), json!(amount.get(brush)));
    }
    json!({
        "active": editor.active,
        "terrain_present": editor.terrain_present,
        "stroke_open": editor.stroke.is_some(),
        "library_count": editor.library.len(),
        "categories": editor.categories(),
        "selected": editor.selected,
        "brushes": editor.library.iter().map(|b| json!({
            "name": b.name,
            "category": b.category,
            "mode": b.mode.label(),
        })).collect::<Vec<_>>(),
        "visible": editor.visible(),
        "search": editor.search,
        "category": editor.category,
        "brush": {
            "name": brush.name,
            "category": brush.category,
            "mode": brush.mode.label(),
            "falloff": brush.falloff.label(),
            "symmetry": brush.symmetry.label(),
            "signed": brush.signed_stamp,
            "stamp": brush.stamp,
            "settings": Value::Object(settings),
            "protect": brush.protect.iter().map(|t| json!({
                "mask": t.mask,
                "from": t.rise0, "full_from": t.rise1, "full_to": t.fall1, "to": t.fall0,
            })).collect::<Vec<_>>(),
        },
        "cursor": app.terrain_cursor.hit.map(|(entity, hit)| json!({"terrain": entity, "point": hit})),
        "status": editor.status,
    })
}

fn performance_json(app: &App) -> Value {
    let viewport = app.viewport_settings;
    let viewport_json = json!({
        "render_scale": viewport.render_scale,
        "anti_aliasing": match viewport.anti_aliasing {
            doppio_engine::AntiAliasing::Off => "off",
            doppio_engine::AntiAliasing::Fxaa => "fxaa",
            doppio_engine::AntiAliasing::Taa => "taa",
        },
        "shadows": viewport.shadows,
        "grid": viewport.show_grid,
    });
    let Some(settings) = app.performance.settings.as_ref() else {
        return json!({ "present": false, "viewport": viewport_json });
    };
    let mut values = serde_json::Map::new();
    for flag in performance_panel::Flag::ALL {
        let spec = flag.spec();
        values.insert(spec.key.to_owned(), json!(settings.flag(*flag)));
    }
    for amount in performance_panel::Amount::ALL {
        let spec = amount.spec();
        values.insert(spec.key.to_owned(), json!(settings.amount(*amount)));
    }
    json!({
        "present": true,
        "entity": app.performance_entity_name(),
        "summary": performance_panel::summary(Some(settings)),
        "settings": Value::Object(values),
        "viewport": viewport_json,
    })
}

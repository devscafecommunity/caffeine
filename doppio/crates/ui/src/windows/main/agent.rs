//! Agent bridge (`--features agent`).
//!
//! `iced-mcp` exposes the real widget tree to an MCP client, so an agent can
//! click, type, snapshot and record flows. On top of that we register the
//! scene-level operations that the widget tree cannot describe precisely:
//! entity inspection, camera control and the editor commands.

use super::{App, Message};
use crate::uikit::id;
use crate::uikit::panels;
use serde_json::{json, Value};

/// Runs Doppio with the MCP bridge attached when `ICED_MCP=1`.
pub fn run() -> iced::Result {
    let application =
        iced::application(App::new, App::update, App::view).subscription(App::subscription);

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
        .action("reset_camera", "Restores the default camera. No args.", |_| {
            Ok(Message::ResetCamera)
        })
        .action(
            "set_camera",
            "Sets the viewport camera. args: { \"yaw\"?: number, \"pitch\"?: number, \"distance\"?: number }",
            |args| {
                let default = doppio_engine::Camera::default();
                Ok(Message::SetCamera(doppio_engine::Camera {
                    yaw: optional_f32(args, "yaw")?.unwrap_or(default.yaw),
                    pitch: optional_f32(args, "pitch")?.unwrap_or(default.pitch),
                    distance: optional_f32(args, "distance")?.unwrap_or(default.distance),
                    focus: default.focus,
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
                    "position": entity.position,
                    "rotation": entity.rotation,
                    "scale": entity.scale,
                })
            })
            .collect(),
    )
}

fn selection_json(app: &App) -> Value {
    app.entities
        .get(app.selected)
        .map_or(Value::Null, |entity| {
            json!({
                "index": app.selected,
                "id": entity.id,
                "name": entity.name,
                "position": entity.position,
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
        "log": app.log,
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

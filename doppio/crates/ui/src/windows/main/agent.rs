//! Agent bridge (`--features agent`).
//!
//! `iced-mcp` exposes the real widget tree to an MCP client, so an agent can
//! click, type, snapshot and record flows. On top of that we register the
//! scene-level operations that the widget tree cannot describe precisely:
//! entity inspection, camera control and the editor commands.

use super::{App, Message};
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
        .action(
            "add_entity",
            "Adds an entity. args: { \"name\": string? }",
            |args| {
                let name = optional_str(args, "name")?.unwrap_or("New Entity");
                Ok(Message::AddEntityNamed(name.to_owned()))
            },
        )
        .action("delete_selected", "Deletes the selected entity. No args.", |_| {
            Ok(Message::DeleteSelected)
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
                Ok(Message::NameChanged(name.to_owned()))
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

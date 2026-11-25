//! The action reference: the single source of truth for what a script step
//! may contain. Parameter validation, `doppio-headless actions` (JSON) and
//! `doppio-headless docs` (Markdown, pasted into the docs) are all generated
//! from this table, so they cannot drift apart.

use serde_json::{json, Value};

/// One parameter of an action.
pub struct Param {
    pub name: &'static str,
    pub ty: &'static str,
    pub required: bool,
    pub doc: &'static str,
}

/// One action a script step can perform.
pub struct ActionDoc {
    pub name: &'static str,
    pub group: &'static str,
    pub summary: &'static str,
    pub params: &'static [Param],
    pub returns: &'static str,
    pub example: &'static str,
}

/// Parameters every step accepts.
pub const COMMON: &[Param] = &[
    Param {
        name: "label",
        ty: "string",
        required: false,
        doc: "Free text echoed back in the result; use it to name the step's intent.",
    },
    Param {
        name: "continue_on_error",
        ty: "bool",
        required: false,
        doc: "Keep running the script after this step fails (default false).",
    },
];

const TARGET: Param = Param {
    name: "target",
    ty: "target",
    required: true,
    doc: "What to act on: `id:<ui id>`, `text:<visible text>` or `point:<x>,<y>` (see Targets).",
};
const OPT_TARGET: Param = Param {
    name: "target",
    ty: "target",
    required: false,
    doc: "Where to act; omit to act at the current pointer position.",
};
const NTH: Param = Param {
    name: "nth",
    ty: "int",
    required: false,
    doc: "0-based index when several widgets match the target (default 0).",
};
const OFFSET: Param = Param {
    name: "offset",
    ty: "[x, y]",
    required: false,
    doc: "Logical pixels added to the target's centre, e.g. to hit a corner.",
};
const BUTTON: Param = Param {
    name: "button",
    ty: "\"left\" | \"right\" | \"middle\"",
    required: false,
    doc: "Mouse button (default left).",
};
const MODIFIERS: Param = Param {
    name: "modifiers",
    ty: "[\"ctrl\" | \"shift\" | \"alt\" | \"cmd\"]",
    required: false,
    doc: "Modifier keys held during the action.",
};
const STEPS: Param = Param {
    name: "steps",
    ty: "int",
    required: false,
    doc: "Intermediate pointer positions emitted while moving, like a real hand (default 6).",
};

pub const ACTIONS: &[ActionDoc] = &[
    ActionDoc {
        name: "move",
        group: "pointer",
        summary: "Moves the pointer to a target in several small steps (hover).",
        params: &[TARGET, NTH, OFFSET, STEPS],
        returns: "{ \"point\": [x, y] }",
        example: r#"{ "action": "move", "target": "id:editor.menu.scene" }"#,
    },
    ActionDoc {
        name: "click",
        group: "pointer",
        summary: "Moves to the target, presses and releases a mouse button.",
        params: &[
            TARGET,
            NTH,
            OFFSET,
            BUTTON,
            MODIFIERS,
            Param {
                name: "count",
                ty: "int",
                required: false,
                doc: "1 = click, 2 = double click, 3 = triple (default 1).",
            },
            STEPS,
        ],
        returns: "{ \"point\": [x, y], \"messages\": [\"…\"] }",
        example: r#"{ "action": "click", "target": "id:assets.toolbar.new" }"#,
    },
    ActionDoc {
        name: "double_click",
        group: "pointer",
        summary: "Shorthand for `click` with `count: 2`.",
        params: &[TARGET, NTH, OFFSET, BUTTON, MODIFIERS, STEPS],
        returns: "as `click`",
        example: r#"{ "action": "double_click", "target": "text:Textures" }"#,
    },
    ActionDoc {
        name: "right_click",
        group: "pointer",
        summary: "Shorthand for `click` with `button: \"right\"` (context menus).",
        params: &[TARGET, NTH, OFFSET, MODIFIERS, STEPS],
        returns: "as `click`",
        example: r#"{ "action": "right_click", "target": "id:hierarchy.list.entity_row.0" }"#,
    },
    ActionDoc {
        name: "drag",
        group: "pointer",
        summary: "Presses on `from`, moves in small steps and releases on `to` (or by an offset).",
        params: &[
            Param {
                name: "from",
                ty: "target",
                required: true,
                doc: "Where the drag starts.",
            },
            Param {
                name: "to",
                ty: "target",
                required: false,
                doc: "Where the drag ends. One of `to` / `by` is required.",
            },
            Param {
                name: "by",
                ty: "[dx, dy]",
                required: false,
                doc: "Relative drag distance in logical pixels.",
            },
            BUTTON,
            MODIFIERS,
            STEPS,
            Param {
                name: "hold",
                ty: "bool",
                required: false,
                doc: "Do not release at the end (finish later with `mouse_up`). Default false.",
            },
        ],
        returns: "{ \"from\": [x, y], \"to\": [x, y], \"messages\": [\"…\"] }",
        example: r#"{ "action": "drag", "from": "id:editor.pane.tab.console", "to": "point:900,300" }"#,
    },
    ActionDoc {
        name: "scroll",
        group: "pointer",
        summary: "Moves to the target and turns the mouse wheel.",
        params: &[
            OPT_TARGET,
            Param {
                name: "dy",
                ty: "number",
                required: false,
                doc: "Wheel lines vertically; positive scrolls up/zooms in (default 0).",
            },
            Param {
                name: "dx",
                ty: "number",
                required: false,
                doc: "Wheel lines horizontally (default 0).",
            },
        ],
        returns: "{ \"point\": [x, y] }",
        example: r##"{ "action": "scroll", "target": "id:editor.viewport.surface", "dy": 3 }"##,
    },
    ActionDoc {
        name: "mouse_down",
        group: "pointer",
        summary: "Low-level: presses a mouse button at the current pointer position.",
        params: &[BUTTON],
        returns: "{}",
        example: r#"{ "action": "mouse_down" }"#,
    },
    ActionDoc {
        name: "mouse_up",
        group: "pointer",
        summary: "Low-level: releases a mouse button at the current pointer position.",
        params: &[BUTTON],
        returns: "{}",
        example: r#"{ "action": "mouse_up" }"#,
    },
    ActionDoc {
        name: "type",
        group: "keyboard",
        summary: "Types text one character at a time. With `target`, clicks it first to focus it.",
        params: &[
            Param {
                name: "text",
                ty: "string",
                required: true,
                doc: "The characters to type.",
            },
            OPT_TARGET,
            Param {
                name: "clear",
                ty: "bool",
                required: false,
                doc: "Select all (ctrl+a) and delete before typing.",
            },
        ],
        returns: "{ \"messages\": [\"…\"] }",
        example: r#"{ "action": "type", "target": "id:assets.wizard.create.name", "text": "Props" }"#,
    },
    ActionDoc {
        name: "key",
        group: "keyboard",
        summary: "Taps a key or chord such as `Enter`, `Escape`, `F5`, `ctrl+z`, `ctrl+shift+n`.",
        params: &[Param {
            name: "key",
            ty: "string",
            required: true,
            doc: "Key name (enter, escape, tab, space, backspace, delete, up/down/left/right, home, end, pageup, pagedown, f1-f12, or one character), optionally prefixed by `ctrl+`, `shift+`, `alt+`, `cmd+`.",
        }],
        returns: "{ \"messages\": [\"…\"] }",
        example: r#"{ "action": "key", "key": "ctrl+z" }"#,
    },
    ActionDoc {
        name: "key_down",
        group: "keyboard",
        summary: "Presses a key and keeps it held (fly camera keys, shift boost). Release with `key_up`.",
        params: &[Param {
            name: "key",
            ty: "string",
            required: true,
            doc: "Same syntax as `key`.",
        }],
        returns: "{}",
        example: r#"{ "action": "key_down", "key": "w" }"#,
    },
    ActionDoc {
        name: "key_up",
        group: "keyboard",
        summary: "Releases a key held by `key_down`.",
        params: &[Param {
            name: "key",
            ty: "string",
            required: true,
            doc: "Same syntax as `key`.",
        }],
        returns: "{}",
        example: r#"{ "action": "key_up", "key": "w" }"#,
    },
    ActionDoc {
        name: "wait",
        group: "time",
        summary: "Lets the app run: delivers redraws and the 16 ms viewport tick.",
        params: &[
            Param {
                name: "frames",
                ty: "int",
                required: false,
                doc: "Number of frames to run (default 1).",
            },
            Param {
                name: "ms",
                ty: "int",
                required: false,
                doc: "Alternative to `frames`: milliseconds of simulated time (16 ms per frame).",
            },
            Param {
                name: "real_time",
                ty: "bool",
                required: false,
                doc: "Also sleep for that long (for background jobs such as caf-pack). Default false.",
            },
        ],
        returns: "{ \"frames\": n }",
        example: r#"{ "action": "wait", "ms": 200 }"#,
    },
    ActionDoc {
        name: "resize",
        group: "time",
        summary: "Resizes the virtual window (logical pixels).",
        params: &[Param {
            name: "size",
            ty: "[w, h]",
            required: true,
            doc: "New window size.",
        }],
        returns: "{ \"size\": [w, h] }",
        example: r#"{ "action": "resize", "size": [1280, 720] }"#,
    },
    ActionDoc {
        name: "file_drop",
        group: "time",
        summary: "Drops a file from the OS onto the window.",
        params: &[Param {
            name: "path",
            ty: "string",
            required: true,
            doc: "Absolute path of the dropped file or folder.",
        }],
        returns: "{ \"messages\": [\"…\"] }",
        example: r#"{ "action": "file_drop", "path": "/tmp/rock.png" }"#,
    },
    ActionDoc {
        name: "find",
        group: "query",
        summary: "Looks a target up without touching it.",
        params: &[TARGET, NTH],
        returns: "{ \"found\": bool, \"id\": …, \"kind\": …, \"bounds\": [x,y,w,h], \"visible_bounds\": …, \"text\": …, \"matches\": n }",
        example: r#"{ "action": "find", "target": "id:editor.pane.viewport" }"#,
    },
    ActionDoc {
        name: "widgets",
        group: "query",
        summary: "Lists the widgets of the current frame (discovery: what can I click?).",
        params: &[
            Param {
                name: "prefix",
                ty: "string",
                required: false,
                doc: "Only ids that start with this (e.g. `assets.`).",
            },
            Param {
                name: "kind",
                ty: "string",
                required: false,
                doc: "Only this kind: container, text, text_input, focusable, scrollable, custom.",
            },
            Param {
                name: "visible_only",
                ty: "bool",
                required: false,
                doc: "Skip widgets that are clipped away (default true).",
            },
            Param {
                name: "with_text",
                ty: "bool",
                required: false,
                doc: "Include anonymous text widgets so you can target `text:` (default false).",
            },
        ],
        returns: "{ \"count\": n, \"widgets\": [{ \"id\", \"kind\", \"bounds\", \"text\" }] }",
        example: r#"{ "action": "widgets", "prefix": "assets." }"#,
    },
    ActionDoc {
        name: "text",
        group: "query",
        summary: "Reads the text of a text widget or the value of a text input.",
        params: &[TARGET, NTH],
        returns: "{ \"text\": \"…\" }",
        example: r#"{ "action": "text", "target": "id:inspector.entity.name" }"#,
    },
    ActionDoc {
        name: "state",
        group: "query",
        summary: "Returns the app state as JSON (panes, selection, menus, wizard, console…).",
        params: &[Param {
            name: "path",
            ty: "string",
            required: false,
            doc: "JSON pointer into the state, e.g. `/panes/0/pages`. Omit for everything.",
        }],
        returns: "the state (or the selected part)",
        example: r#"{ "action": "state", "path": "/menu" }"#,
    },
    ActionDoc {
        name: "log",
        group: "query",
        summary: "Returns the last console entries.",
        params: &[Param {
            name: "last",
            ty: "int",
            required: false,
            doc: "How many entries (default 10).",
        }],
        returns: "{ \"entries\": [{ \"level\", \"text\" }] }",
        example: r#"{ "action": "log", "last": 5 }"#,
    },
    ActionDoc {
        name: "screenshot",
        group: "capture",
        summary: "Renders the window (or one widget) to a PNG, optionally annotated or compared with a baseline.",
        params: &[
            Param {
                name: "name",
                ty: "string",
                required: false,
                doc: "File stem (default `shot`). Files are `<out>/<NNN>-<name>.png`.",
            },
            Param {
                name: "target",
                ty: "target",
                required: false,
                doc: "Crop to this widget; omit for the whole window.",
            },
            Param {
                name: "padding",
                ty: "number",
                required: false,
                doc: "Logical pixels added around a cropped target (default 8).",
            },
            Param {
                name: "annotate",
                ty: "bool | [target]",
                required: false,
                doc: "Draw numbered boxes with a legend. `true` = every visible widget that has a catalogued id; or a list of targets.",
            },
            Param {
                name: "scale",
                ty: "number",
                required: false,
                doc: "Pixel density (default 1; use 2 for retina-like detail).",
            },
            Param {
                name: "baseline",
                ty: "string",
                required: false,
                doc: "Compare with `<baselines>/<baseline>.png`; a mismatch fails the step. Create/refresh with `--update-baselines`.",
            },
            Param {
                name: "tolerance",
                ty: "number",
                required: false,
                doc: "Allowed fraction of differing pixels (default 0.0005).",
            },
        ],
        returns: "{ \"path\", \"width\", \"height\", \"hash\", \"baseline\": { \"status\", \"diff_ratio\" } }",
        example: r#"{ "action": "screenshot", "name": "assets-wizard", "target": "id:assets.wizard", "annotate": true }"#,
    },
    ActionDoc {
        name: "record_start",
        group: "capture",
        summary: "Starts recording the window (or one widget). Frames are taken after every step that changes something and every few frames during `wait`; `record_stop` turns them into a GIF, MP4 or WebM with ffmpeg.",
        params: &[
            Param {
                name: "name",
                ty: "string",
                required: false,
                doc: "File stem of the result (default `recording`): `<out>/<name>.<format>`.",
            },
            Param {
                name: "format",
                ty: "string",
                required: false,
                doc: "`gif` (default), `mp4` or `webm`.",
            },
            Param {
                name: "fps",
                ty: "int",
                required: false,
                doc: "Capture and playback rate, 1-60 (default 15). A `wait` of 2000 ms with `real_time` is about two seconds of video.",
            },
            Param {
                name: "target",
                ty: "target",
                required: false,
                doc: "Record only this widget; omit for the whole window.",
            },
            Param {
                name: "padding",
                ty: "number",
                required: false,
                doc: "Logical pixels around a target (default 8).",
            },
            Param {
                name: "scale",
                ty: "number",
                required: false,
                doc: "Capture pixel density, 0.25-3 (default 1).",
            },
            Param {
                name: "width",
                ty: "int",
                required: false,
                doc: "Output width in pixels (height keeps the aspect); omit to keep the captured size.",
            },
            Param {
                name: "keep_frames",
                ty: "bool",
                required: false,
                doc: "Keep the PNG frames (their folder is returned) instead of deleting them.",
            },
        ],
        returns: "{ \"recording\": true, \"fps\" }",
        example: r#"{ "action": "record_start", "name": "orbit", "format": "mp4", "fps": 30, "target": "id:editor.pane.viewport" }"#,
    },
    ActionDoc {
        name: "record_stop",
        group: "capture",
        summary: "Stops the recording and encodes it with ffmpeg (the FFMPEG environment variable overrides the program path). A recording still open when the script ends is finished automatically.",
        params: &[],
        returns: "{ \"path\", \"format\", \"frames\", \"fps\", \"seconds\", \"bytes\" }",
        example: r#"{ "action": "record_stop" }"#,
    },
    ActionDoc {
        name: "expect",
        group: "assert",
        summary: "Asserts conditions on the current frame/state; every given field must hold.",
        params: &[
            Param {
                name: "exists",
                ty: "target | [target]",
                required: false,
                doc: "These targets are on screen.",
            },
            Param {
                name: "missing",
                ty: "target | [target]",
                required: false,
                doc: "These targets are not on screen.",
            },
            Param {
                name: "visible",
                ty: "target | [target]",
                required: false,
                doc: "These targets exist and are not clipped away.",
            },
            Param {
                name: "shows",
                ty: "string | [string]",
                required: false,
                doc: "These exact texts are on screen.",
            },
            Param {
                name: "text",
                ty: "{ target, equals?, contains? }",
                required: false,
                doc: "Text of a widget equals / contains a string.",
            },
            Param {
                name: "state",
                ty: "{ \"/json/pointer\": value }",
                required: false,
                doc: "Values in the `state` JSON (deep equality). Use `{ \"contains\": … }` for arrays/strings.",
            },
            Param {
                name: "count",
                ty: "{ prefix, equals?, at_least? }",
                required: false,
                doc: "Number of visible widgets whose id starts with `prefix`.",
            },
        ],
        returns: "{ \"checked\": n }",
        example: r#"{ "action": "expect", "exists": "id:assets.wizard", "state": { "/wizard/open": true } }"#,
    },
    ActionDoc {
        name: "reset",
        group: "session",
        summary: "Starts over with a fresh app (new fixture, window size and assets).",
        params: &[
            Param {
                name: "fixture",
                ty: "string",
                required: false,
                doc: "`empty`, `entities`, `material`, `texture`, `scene3d` (an engine-backed scene with lights, a camera and meshes; needs libcaffeine-capi) `nature` (the same, filled with generated rocks, trees and plants) `water` (the Water Generator tab in front) `terrain` (the Terrain Generator tab in front) `world` (the World Composer tab on a scratch project: a hills terrain, two rock recipes and `World/Valley.generator.world`; needs libcaffeine-capi) or `world_showcase` (the same tab on `World/Highlands.generator.world`: hills round a valley, a river, a lake, oaks and pines, boulders, shrubs and grass; Build makes a few thousand instances, Frame points the Viewport at them). Default `entities`.",
            },
            Param {
                name: "size",
                ty: "[w, h]",
                required: false,
                doc: "Window size.",
            },
            Param {
                name: "assets",
                ty: "[{ name, path, kind, directory? }]",
                required: false,
                doc: "Asset list shown in the Assets panel.",
            },
            Param {
                name: "asset_root",
                ty: "string",
                required: false,
                doc: "Folder the asset wizard writes to; `temp` creates a scratch directory.",
            },
            Param {
                name: "preset",
                ty: "string",
                required: false,
                doc: "Library preset applied to the Material Generator (fixture `material`, e.g. `Red Brick`) to the Texture Generator (fixture `texture`, e.g. `Torch Flame`) to the Nature Generator (fixture `nature`, e.g. `Pine`) to the Water Generator (fixture `water`, e.g. `Storm`) or to the Terrain Generator (fixture `terrain`, e.g. `Badlands`).",
            },
            Param {
                name: "components",
                ty: "[{ name, fields? }]",
                required: false,
                doc: "Engine component types the fixture pretends to have (for Add Behaviour).",
            },
        ],
        returns: "{ \"asset_root\": … }",
        example: r#"{ "action": "reset", "fixture": "entities", "asset_root": "temp" }"#,
    },
];

pub fn find(name: &str) -> Option<&'static ActionDoc> {
    ACTIONS.iter().find(|action| action.name == name)
}

/// Every parameter name an action accepts (its own + the common ones).
pub fn allowed_params(action: &ActionDoc) -> Vec<&'static str> {
    action
        .params
        .iter()
        .chain(COMMON.iter())
        .map(|param| param.name)
        .collect()
}

fn param_json(param: &Param) -> Value {
    json!({ "name": param.name, "type": param.ty, "required": param.required, "doc": param.doc })
}

/// Machine-readable reference (`doppio-headless actions`).
pub fn json() -> Value {
    json!({
        "protocol": super::PROTOCOL_VERSION,
        "common_params": COMMON.iter().map(param_json).collect::<Vec<_>>(),
        "actions": ACTIONS.iter().map(|action| json!({
            "name": action.name,
            "group": action.group,
            "summary": action.summary,
            "params": action.params.iter().map(param_json).collect::<Vec<_>>(),
            "returns": action.returns,
            "example": action.example,
        })).collect::<Vec<_>>(),
    })
}

fn schema_for(ty: &str, doc: &str) -> Value {
    let mut value = match ty {
        "string" => json!({ "type": "string" }),
        "bool" => json!({ "type": "boolean" }),
        "int" => json!({ "type": "integer", "minimum": 0 }),
        "number" => json!({ "type": "number" }),
        "target" => json!({ "oneOf": [{ "type": "string" }, { "type": "object" }] }),
        "[x, y]" | "[dx, dy]" | "[w, h]" => {
            json!({ "type": "array", "items": { "type": "number" }, "minItems": 2, "maxItems": 2 })
        }
        _ => json!({}),
    };
    value["description"] = json!(doc);
    value
}

/// JSON Schema (draft-07) of a script, for editors and agent tooling
/// (`doppio-headless schema`).
pub fn schema() -> Value {
    let steps: Vec<Value> = ACTIONS
        .iter()
        .map(|action| {
            let mut properties = serde_json::Map::new();
            properties.insert("action".to_owned(), json!({ "const": action.name }));
            for param in action.params.iter().chain(COMMON.iter()) {
                properties.insert(param.name.to_owned(), schema_for(param.ty, param.doc));
            }
            let mut required = vec![json!("action")];
            required.extend(
                action
                    .params
                    .iter()
                    .filter(|param| param.required)
                    .map(|param| json!(param.name)),
            );
            json!({
                "title": action.name,
                "description": action.summary,
                "type": "object",
                "properties": properties,
                "required": required,
                "additionalProperties": false,
            })
        })
        .collect();
    json!({
        "$schema": "http://json-schema.org/draft-07/schema#",
        "title": "Doppio headless script",
        "type": "object",
        "properties": {
            "name": { "type": "string" },
            "description": { "type": "string" },
            "fixture": { "enum": ["empty", "entities"] },
            "size": { "type": "array", "items": { "type": "number" }, "minItems": 2, "maxItems": 2 },
            "assets": { "type": "array", "items": { "type": "object" } },
            "asset_root": { "type": "string" },
            "steps": { "type": "array", "items": { "oneOf": steps } },
        },
        "required": ["steps"],
    })
}

/// Markdown reference (`doppio-headless docs`).
pub fn markdown() -> String {
    let mut out = String::new();
    let mut group = "";
    for action in ACTIONS {
        if action.group != group {
            group = action.group;
            out.push_str(&format!("\n### {}\n\n", title_case(group)));
        }
        out.push_str(&format!("#### `{}`\n\n{}\n\n", action.name, action.summary));
        if !action.params.is_empty() {
            out.push_str("| Parameter | Type | Required | Description |\n|---|---|---|---|\n");
            for param in action.params {
                out.push_str(&format!(
                    "| `{}` | `{}` | {} | {} |\n",
                    param.name,
                    param.ty.replace('|', "\\|"),
                    if param.required { "yes" } else { "no" },
                    param.doc.replace('|', "\\|")
                ));
            }
            out.push('\n');
        }
        out.push_str(&format!(
            "Returns: `{}`\n\n```json\n{}\n```\n",
            action.returns.replace('|', "\\|"),
            action.example
        ));
    }
    out
}

fn title_case(text: &str) -> String {
    let mut chars = text.chars();
    match chars.next() {
        Some(first) => first.to_uppercase().collect::<String>() + chars.as_str(),
        None => String::new(),
    }
}

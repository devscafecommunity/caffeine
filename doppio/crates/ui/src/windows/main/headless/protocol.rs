//! Wire types of the headless protocol: scripts, steps, targets and results.
//!
//! Everything is plain JSON so any agent or tool can speak it. Steps are kept
//! as JSON objects and validated against [`super::reference`], which lets us
//! report `unknown parameter "tagret", expected: target, …` instead of
//! silently ignoring typos.

use super::reference;
use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};

/// A whole test/flow: initial app plus an ordered list of steps.
#[derive(Debug, Clone, Deserialize)]
pub struct Script {
    #[serde(default)]
    pub name: Option<String>,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(flatten)]
    pub init: Init,
    pub steps: Vec<Value>,
}

/// How the app starts (also the payload of the `reset` action).
#[derive(Debug, Clone, Deserialize, Default)]
pub struct Init {
    /// `entities` (default) or `empty`.
    #[serde(default)]
    pub fixture: Option<String>,
    /// Logical window size.
    #[serde(default)]
    pub size: Option<[f32; 2]>,
    /// Assets shown in the Assets panel.
    #[serde(default)]
    pub assets: Option<Vec<AssetSpec>>,
    /// Folder the asset wizard writes to (`temp` = scratch directory).
    #[serde(default)]
    pub asset_root: Option<String>,
    /// Procedural library preset applied to the Generator (fixture `material`),
    /// e.g. `Red Brick`.
    #[serde(default)]
    pub preset: Option<String>,
    /// Engine component types, as an engine session would report them (the
    /// fixtures have no engine), so pickers such as Add Behaviour have content.
    #[serde(default)]
    pub components: Option<Vec<ComponentSpec>>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ComponentSpec {
    pub name: String,
    /// Field names (all numeric); optional.
    #[serde(default)]
    pub fields: Vec<String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct AssetSpec {
    pub name: String,
    pub path: String,
    #[serde(default)]
    pub kind: Option<String>,
    #[serde(default)]
    pub directory: bool,
}

/// What a step points at.
#[derive(Debug, Clone, PartialEq)]
pub enum Target {
    /// A catalogued widget id such as `editor.menu.scene`.
    Id(String),
    /// Visible text (labels, buttons, text-input values).
    Text(String),
    /// Window coordinates in logical pixels.
    Point(f32, f32),
}

impl Target {
    pub fn describe(&self) -> String {
        match self {
            Target::Id(id) => format!("id:{id}"),
            Target::Text(text) => format!("text:{text}"),
            Target::Point(x, y) => format!("point:{x},{y}"),
        }
    }
}

/// A target plus the optional refinements shared by pointer actions.
#[derive(Debug, Clone, PartialEq)]
pub struct Aim {
    pub target: Target,
    pub nth: usize,
    pub offset: [f32; 2],
}

/// Parses `id:…` / `text:…` / `point:x,y` (and bare strings as a shorthand:
/// something dotted without spaces is an id, anything else is text), or the
/// object forms `{ "id": … }`, `{ "text": … }`, `{ "point": [x, y] }`.
pub fn parse_target(value: &Value) -> Result<Target, String> {
    match value {
        Value::String(text) => parse_target_str(text),
        Value::Object(map) => {
            if let Some(id) = map.get("id").and_then(Value::as_str) {
                Ok(Target::Id(id.to_owned()))
            } else if let Some(text) = map.get("text").and_then(Value::as_str) {
                Ok(Target::Text(text.to_owned()))
            } else if let Some(point) = map.get("point").and_then(Value::as_array) {
                match (
                    point.first().and_then(Value::as_f64),
                    point.get(1).and_then(Value::as_f64),
                ) {
                    (Some(x), Some(y)) => Ok(Target::Point(x as f32, y as f32)),
                    _ => Err("`point` must be [x, y]".to_owned()),
                }
            } else {
                Err("a target object needs `id`, `text` or `point`".to_owned())
            }
        }
        _ => Err("a target is a string (`id:…`, `text:…`, `point:x,y`) or an object".to_owned()),
    }
}

fn parse_target_str(text: &str) -> Result<Target, String> {
    if let Some(id) = text.strip_prefix("id:") {
        return Ok(Target::Id(id.trim().to_owned()));
    }
    if let Some(label) = text.strip_prefix("text:") {
        return Ok(Target::Text(label.to_owned()));
    }
    if let Some(point) = text.strip_prefix("point:") {
        let mut parts = point.split(',');
        let x = parts.next().and_then(|v| v.trim().parse::<f32>().ok());
        let y = parts.next().and_then(|v| v.trim().parse::<f32>().ok());
        return match (x, y) {
            (Some(x), Some(y)) => Ok(Target::Point(x, y)),
            _ => Err(format!("bad point `{text}`; use point:<x>,<y>")),
        };
    }
    let looks_like_id = text.contains('.')
        && !text.contains(char::is_whitespace)
        && text.chars().next().is_some_and(|c| c.is_ascii_lowercase());
    Ok(if looks_like_id {
        Target::Id(text.to_owned())
    } else {
        Target::Text(text.to_owned())
    })
}

/// One validated step.
#[derive(Debug, Clone)]
pub struct Step {
    pub action: String,
    pub label: Option<String>,
    pub continue_on_error: bool,
    pub params: Map<String, Value>,
}

impl Step {
    /// Validates the shape of a step object against the action reference.
    pub fn parse(value: &Value) -> Result<Step, String> {
        let object = value
            .as_object()
            .ok_or("a step must be a JSON object with an `action`")?;
        let action = object
            .get("action")
            .and_then(Value::as_str)
            .ok_or("a step needs an `action` string")?;
        let doc = reference::find(action).ok_or_else(|| {
            let names: Vec<_> = reference::ACTIONS.iter().map(|a| a.name).collect();
            format!("unknown action `{action}`; available: {}", names.join(", "))
        })?;
        let allowed = reference::allowed_params(doc);
        for key in object.keys() {
            if key != "action" && !allowed.contains(&key.as_str()) {
                return Err(format!(
                    "unknown parameter `{key}` for `{action}`; expected: {}",
                    allowed.join(", ")
                ));
            }
        }
        for param in doc.params.iter().filter(|param| param.required) {
            if !object.contains_key(param.name) {
                return Err(format!("`{action}` needs `{}` ({})", param.name, param.doc));
            }
        }
        Ok(Step {
            action: action.to_owned(),
            label: object
                .get("label")
                .and_then(Value::as_str)
                .map(str::to_owned),
            continue_on_error: object
                .get("continue_on_error")
                .and_then(Value::as_bool)
                .unwrap_or(false),
            params: object.clone(),
        })
    }

    pub fn get(&self, key: &str) -> Option<&Value> {
        self.params.get(key)
    }

    pub fn str(&self, key: &str) -> Option<&str> {
        self.get(key).and_then(Value::as_str)
    }

    pub fn bool(&self, key: &str, default: bool) -> bool {
        self.get(key).and_then(Value::as_bool).unwrap_or(default)
    }

    pub fn f32(&self, key: &str) -> Option<f32> {
        self.get(key).and_then(Value::as_f64).map(|v| v as f32)
    }

    pub fn usize(&self, key: &str, default: usize) -> usize {
        self.get(key)
            .and_then(Value::as_u64)
            .map_or(default, |v| v as usize)
    }

    pub fn pair(&self, key: &str) -> Result<Option<[f32; 2]>, String> {
        let Some(value) = self.get(key) else {
            return Ok(None);
        };
        match value.as_array().map(Vec::as_slice) {
            Some([x, y]) => match (x.as_f64(), y.as_f64()) {
                (Some(x), Some(y)) => Ok(Some([x as f32, y as f32])),
                _ => Err(format!("`{key}` must be [number, number]")),
            },
            _ => Err(format!("`{key}` must be [number, number]")),
        }
    }

    pub fn target(&self, key: &str) -> Result<Option<Target>, String> {
        self.get(key)
            .map(|value| parse_target(value).map_err(|error| format!("`{key}`: {error}")))
            .transpose()
    }

    /// The target + `nth` + `offset` triple used by pointer actions.
    pub fn aim(&self, key: &str) -> Result<Option<Aim>, String> {
        let Some(target) = self.target(key)? else {
            return Ok(None);
        };
        Ok(Some(Aim {
            target,
            nth: self.usize("nth", 0),
            offset: self.pair("offset")?.unwrap_or([0.0, 0.0]),
        }))
    }

    pub fn strings(&self, key: &str) -> Vec<String> {
        match self.get(key) {
            Some(Value::String(text)) => vec![text.clone()],
            Some(Value::Array(items)) => items
                .iter()
                .filter_map(Value::as_str)
                .map(str::to_owned)
                .collect(),
            _ => Vec::new(),
        }
    }

    pub fn modifiers(&self) -> Result<Vec<String>, String> {
        let names = self.strings("modifiers");
        for name in &names {
            if !matches!(name.as_str(), "ctrl" | "shift" | "alt" | "cmd") {
                return Err(format!("unknown modifier `{name}` (ctrl, shift, alt, cmd)"));
            }
        }
        Ok(names)
    }
}

/// The outcome of one step.
#[derive(Debug, Clone, Serialize)]
pub struct StepResult {
    pub index: usize,
    pub action: String,
    pub label: Option<String>,
    pub ok: bool,
    pub elapsed_ms: u64,
    pub frame: u64,
    pub data: Value,
    pub error: Option<String>,
    pub hint: Option<String>,
}

impl StepResult {
    pub fn failure(index: usize, action: &str, error: impl Into<String>) -> Self {
        StepResult {
            index,
            action: action.to_owned(),
            label: None,
            ok: false,
            elapsed_ms: 0,
            frame: 0,
            data: json!({}),
            error: Some(error.into()),
            hint: None,
        }
    }
}

/// The result of running a whole script.
#[derive(Debug, Clone, Serialize)]
pub struct RunReport {
    pub protocol: u32,
    pub script: Option<String>,
    pub ok: bool,
    pub passed: usize,
    pub failed: usize,
    pub out_dir: String,
    pub artifacts: Vec<String>,
    pub steps: Vec<StepResult>,
}

impl RunReport {
    /// A short human-readable rendering (`--format text`).
    pub fn text(&self) -> String {
        let mut out = String::new();
        out.push_str(&format!(
            "{} {} — {} passed, {} failed\n",
            if self.ok { "PASS" } else { "FAIL" },
            self.script.as_deref().unwrap_or("(unnamed script)"),
            self.passed,
            self.failed
        ));
        for step in &self.steps {
            let mark = if step.ok { "ok  " } else { "FAIL" };
            out.push_str(&format!(
                "  {mark} #{} {}{}\n",
                step.index,
                step.action,
                step.label
                    .as_deref()
                    .map(|label| format!(" — {label}"))
                    .unwrap_or_default()
            ));
            if let Some(error) = &step.error {
                out.push_str(&format!("       error: {error}\n"));
            }
            if let Some(hint) = &step.hint {
                out.push_str(&format!("       hint:  {hint}\n"));
            }
        }
        for artifact in &self.artifacts {
            out.push_str(&format!("  artifact: {artifact}\n"));
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn targets_parse_in_every_supported_form() {
        assert_eq!(
            parse_target(&json!("id:editor.menu.scene")),
            Ok(Target::Id("editor.menu.scene".into()))
        );
        assert_eq!(
            parse_target(&json!("text:Add Entity")),
            Ok(Target::Text("Add Entity".into()))
        );
        assert_eq!(
            parse_target(&json!("point:12.5, 40")),
            Ok(Target::Point(12.5, 40.0))
        );
        assert_eq!(
            parse_target(&json!("assets.wizard")),
            Ok(Target::Id("assets.wizard".into()))
        );
        assert_eq!(
            parse_target(&json!("Create")),
            Ok(Target::Text("Create".into()))
        );
        assert_eq!(
            parse_target(&json!({ "point": [1, 2] })),
            Ok(Target::Point(1.0, 2.0))
        );
        assert!(parse_target(&json!(5)).is_err());
    }

    #[test]
    fn steps_are_validated_against_the_reference() {
        let ok = Step::parse(&json!({ "action": "click", "target": "id:a.b" }));
        assert!(ok.is_ok());
        let typo =
            Step::parse(&json!({ "action": "click", "tagret": "id:a.b" })).expect_err("typo");
        assert!(typo.contains("unknown parameter `tagret`"), "{typo}");
        assert!(typo.contains("target"));
        let missing = Step::parse(&json!({ "action": "click" })).expect_err("missing");
        assert!(missing.contains("needs `target`"), "{missing}");
        let unknown = Step::parse(&json!({ "action": "tap" })).expect_err("unknown");
        assert!(unknown.contains("available:"), "{unknown}");
    }
}

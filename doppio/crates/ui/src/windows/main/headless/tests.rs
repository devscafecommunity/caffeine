//! Tests of the headless driver and the shipped flows.

use super::protocol::{Init, Script};
use super::{Config, Driver};
use serde_json::{json, Value};

fn config(label: &str) -> Config {
    Config {
        out_dir: std::env::temp_dir().join(format!(
            "doppio-headless-test-{label}-{}",
            std::process::id()
        )),
        baselines: None,
        update_baselines: false,
        size: [1280.0, 720.0],
    }
}

fn driver(label: &str) -> Driver {
    Driver::new(config(label), &Init::default()).expect("driver")
}

fn ok(result: &super::protocol::StepResult) -> &Value {
    assert!(
        result.ok,
        "step failed: {:?} (hint {:?})",
        result.error, result.hint
    );
    &result.data
}

#[test]
fn pointer_clicks_reach_widgets_and_state_reflects_them() {
    let _guard = super::ui_lock();
    let mut driver = driver("click");
    let menu = driver.run_step(
        0,
        &json!({ "action": "click", "target": "id:editor.menu.scene" }),
    );
    ok(&menu);
    let state = driver.run_step(1, &json!({ "action": "state", "path": "/menu" }));
    assert_eq!(ok(&state), &json!("Scene"));
    // Hover updates behind an open menu must not dismiss it.
    let hover = driver.run_step(2, &json!({ "action": "move", "target": "text:Entity 1" }));
    ok(&hover);
    let still = driver.run_step(3, &json!({ "action": "state", "path": "/menu" }));
    assert_eq!(ok(&still), &json!("Scene"));
    let escape = driver.run_step(4, &json!({ "action": "key", "key": "Escape" }));
    ok(&escape);
    let closed = driver.run_step(5, &json!({ "action": "state", "path": "/menu" }));
    assert_eq!(ok(&closed), &Value::Null);
}

#[test]
fn typing_keeps_focus_across_steps_like_a_person() {
    let _guard = super::ui_lock();
    let mut driver = driver("type");
    ok(&driver.run_step(0, &json!({ "action": "click", "target": "text:Entity 1" })));
    let typed = driver.run_step(
        1,
        &json!({ "action": "type", "target": "id:inspector.entity.name", "text": " Renamed", "clear": false }),
    );
    ok(&typed);
    let state = driver.run_step(2, &json!({ "action": "state", "path": "/selected" }));
    assert_eq!(ok(&state), &json!("Entity 1 Renamed"));
}

#[test]
fn unknown_targets_fail_with_actionable_hints() {
    let _guard = super::ui_lock();
    let mut driver = driver("hint");
    let result = driver.run_step(
        0,
        &json!({ "action": "click", "target": "id:editor.menu.sene" }),
    );
    assert!(!result.ok);
    assert!(result
        .error
        .as_deref()
        .unwrap_or("")
        .contains("no widget matches"));
    assert!(result
        .hint
        .as_deref()
        .unwrap_or("")
        .contains("did you mean"));
    let typo = driver.run_step(1, &json!({ "action": "click", "tagret": "id:x.y" }));
    assert!(!typo.ok);
    assert!(typo
        .error
        .as_deref()
        .unwrap_or("")
        .contains("unknown parameter `tagret`"));
}

#[test]
fn screenshots_are_written_annotated_and_compared_with_baselines() {
    let _guard = super::ui_lock();
    let mut cfg = config("shots");
    cfg.baselines = Some(cfg.out_dir.join("baselines"));
    let mut driver = Driver::new(cfg.clone(), &Init::default()).expect("driver");
    let shot = driver.run_step(
        0,
        &json!({ "action": "screenshot", "name": "full", "annotate": true }),
    );
    let data = ok(&shot);
    let path = data["path"].as_str().expect("path");
    assert!(std::path::Path::new(path).is_file());
    assert_eq!(data["width"], json!(1280));
    // First run creates the baseline, the second run matches it.
    let created = driver.run_step(
        1,
        &json!({ "action": "screenshot", "name": "b", "baseline": "home" }),
    );
    assert_eq!(ok(&created)["baseline"]["status"], json!("created"));
    let matched = driver.run_step(
        2,
        &json!({ "action": "screenshot", "name": "b", "baseline": "home" }),
    );
    assert_eq!(ok(&matched)["baseline"]["status"], json!("match"));
    // A different UI state no longer matches.
    ok(&driver.run_step(
        3,
        &json!({ "action": "click", "target": "id:editor.menu.view" }),
    ));
    let differs = driver.run_step(
        4,
        &json!({ "action": "screenshot", "name": "b", "baseline": "home" }),
    );
    assert!(!differs.ok, "a changed UI must fail the baseline");
    let _ = std::fs::remove_dir_all(cfg.out_dir);
}

#[test]
fn docking_by_dragging_a_tab_uses_the_overlay_guides() {
    let _guard = super::ui_lock();
    let mut driver = driver("dock");
    let before = driver.run_step(0, &json!({ "action": "state", "path": "/panes" }));
    let panes_before = ok(&before).as_array().expect("panes").len();
    // Drag the Console tab onto the right edge of the viewport pane.
    let find = driver.run_step(
        1,
        &json!({ "action": "find", "target": "id:editor.pane.viewport" }),
    );
    let bounds = ok(&find)["bounds"].clone();
    let (x, y, w, h) = (
        bounds[0].as_f64().unwrap(),
        bounds[1].as_f64().unwrap(),
        bounds[2].as_f64().unwrap(),
        bounds[3].as_f64().unwrap(),
    );
    let target = format!("point:{},{}", x + w - 12.0, y + h / 2.0);
    let drag = driver.run_step(
        2,
        &json!({ "action": "drag", "from": "id:editor.pane.tab.console", "to": target, "steps": 12 }),
    );
    ok(&drag);
    let after = driver.run_step(3, &json!({ "action": "state", "path": "/panes" }));
    assert_eq!(
        ok(&after).as_array().expect("panes").len(),
        panes_before + 1
    );
}

#[test]
fn every_shipped_flow_passes() {
    let _guard = super::ui_lock();
    let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/ui-flows");
    let mut flows: Vec<_> = std::fs::read_dir(&dir)
        .unwrap_or_else(|e| panic!("{}: {e}", dir.display()))
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| path.extension().is_some_and(|ext| ext == "json"))
        .collect();
    flows.sort();
    assert!(!flows.is_empty(), "no flows in {}", dir.display());
    for path in flows {
        let text = std::fs::read_to_string(&path).expect("flow");
        let script: Script = serde_json::from_str(&text)
            .unwrap_or_else(|e| panic!("{}: invalid script: {e}", path.display()));
        let stem = path
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("flow")
            .to_owned();
        let cfg = Config {
            out_dir: std::env::temp_dir()
                .join(format!("doppio-flows-{}-{stem}", std::process::id())),
            ..config(&stem)
        };
        let mut init = script.init.clone();
        init.size = init.size.or(Some(cfg.size));
        let mut driver = Driver::new(cfg.clone(), &init).expect("driver");
        let report = driver.run_script(&script, true);
        assert!(
            report.ok,
            "flow {} failed:\n{}",
            path.display(),
            report.text()
        );
        let _ = std::fs::remove_dir_all(cfg.out_dir);
    }
}

#[test]
fn the_docs_reference_is_in_sync_with_the_action_table() {
    let path =
        std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../docs/headless-testing.md");
    let docs = std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
    let begin = "<!-- BEGIN GENERATED REFERENCE -->";
    let end = "<!-- END GENERATED REFERENCE -->";
    let start = docs.find(begin).expect("begin marker") + begin.len();
    let stop = docs.find(end).expect("end marker");
    assert_eq!(
        docs[start..stop].trim(),
        super::reference::markdown().trim(),
        "doppio/docs/headless-testing.md is out of date: run `scripts/doppio-headless.sh docs` and paste it between the markers"
    );
    // Every action is also mentioned by name in the hand-written part.
    for action in super::reference::ACTIONS {
        assert!(
            docs.contains(&format!("`{}`", action.name)),
            "docs never mention `{}`",
            action.name
        );
    }
}

#[test]
fn schema_and_reference_cover_the_same_actions() {
    let schema = super::reference::schema();
    let variants = schema["properties"]["steps"]["items"]["oneOf"]
        .as_array()
        .expect("oneOf");
    assert_eq!(variants.len(), super::reference::ACTIONS.len());
    let json = super::reference::json();
    assert_eq!(
        json["actions"].as_array().expect("actions").len(),
        variants.len()
    );
}

#[test]
fn every_documented_example_is_a_valid_step() {
    for action in super::reference::ACTIONS {
        let value: Value = serde_json::from_str(action.example)
            .unwrap_or_else(|e| panic!("example of `{}` is not JSON: {e}", action.name));
        super::protocol::Step::parse(&value)
            .unwrap_or_else(|e| panic!("example of `{}` is invalid: {e}", action.name));
    }
}

#[test]
fn a_held_drag_can_be_finished_with_mouse_up_and_hover_follows_the_pointer() {
    let _guard = super::ui_lock();
    let mut driver = driver("hold");
    ok(&driver.run_step(0, &json!({ "action": "drag", "from": "id:editor.pane.tab.viewport", "by": [0, 150], "hold": true })));
    let during = driver.run_step(
        1,
        &json!({ "action": "state", "path": "/tab_drag/dragging" }),
    );
    assert_eq!(ok(&during), &json!(true));
    ok(&driver.run_step(2, &json!({ "action": "mouse_up" })));
    let after = driver.run_step(3, &json!({ "action": "state", "path": "/tab_drag" }));
    assert_eq!(ok(&after), &Value::Null);
}

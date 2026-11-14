//! Playwright-style UI automation for the Doppio workspace.
//!
//! `Harness` drives the real Iced view tree: it clicks widgets by id, types
//! into inputs, drains the resulting messages through `App::update`, and
//! asserts on what the next frame shows. Tests never open a window or need a
//! GPU, so they run in CI and in `cargo test`.

use super::*;
use iced::widget::Id;
use iced_test::simulator;
use std::sync::{Mutex, MutexGuard};

/// Iced keeps process-wide state (themes, focus, text input), so UI tests run
/// one at a time inside this process.
fn test_guard() -> MutexGuard<'static, ()> {
    static LOCK: Mutex<()> = Mutex::new(());
    LOCK.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

/// Minimal Playwright-like driver over `App`.
pub struct Harness {
    app: App,
    _guard: MutexGuard<'static, ()>,
}

impl Harness {
    /// Editor with no scene and no engine dependency (deterministic tests).
    pub fn empty() -> Self {
        Self {
            app: App::empty(),
            _guard: test_guard(),
        }
    }

    /// Editor with two entities added through the normal message path.
    pub fn with_entities() -> Self {
        let mut harness = Self::empty();
        harness.dispatch(Message::AddEntityNamed("Entity 1".to_owned()));
        harness.dispatch(Message::AddEntityNamed("Entity 2".to_owned()));
        harness
    }

    pub fn app(&self) -> &App {
        &self.app
    }

    pub fn dispatch(&mut self, message: Message) {
        self.app.update(message);
    }

    fn drain(&mut self, messages: Vec<Message>) {
        for message in messages {
            self.app.update(message);
        }
    }

    /// Clicks the widget with this id and applies every message it published.
    pub fn click(&mut self, id: &str) {
        let mut ui = simulator(self.app.view());
        let clicked = ui.click(Id::from(id.to_owned()));
        assert!(
            clicked.is_ok(),
            "widget '{id}' should be clickable: {:?}",
            clicked.err()
        );
        let messages: Vec<_> = ui.into_messages().collect();
        self.drain(messages);
    }

    /// Clicks an input by id, types `text`, and applies the edits.
    pub fn type_into(&mut self, id: &str, text: &str) {
        let mut ui = simulator(self.app.view());
        let focused = ui.click(Id::from(id.to_owned()));
        assert!(
            focused.is_ok(),
            "input '{id}' should be focusable: {:?}",
            focused.err()
        );
        let _ = ui.typewrite(text);
        let messages: Vec<_> = ui.into_messages().collect();
        self.drain(messages);
    }

    /// True when the current frame contains this text.
    pub fn shows(&self, text: &str) -> bool {
        let mut ui = simulator(self.app.view());
        ui.find(text).is_ok()
    }

    /// True when the current frame contains a widget with this id.
    pub fn has(&self, id: &str) -> bool {
        let mut ui = simulator(self.app.view());
        ui.find(Id::from(id.to_owned())).is_ok()
    }

    /// Waits for the text by rendering at most `frames` times, like a
    /// browser assertion that polls instead of sleeping.
    pub fn wait_for(&self, text: &str, frames: usize) -> bool {
        (0..frames).any(|_| self.shows(text))
    }
}

#[test]
fn empty_editor_has_no_entities() {
    let harness = Harness::empty();
    assert!(harness.shows("No entities"));
    assert!(harness.shows("No entity selected"));
}

#[test]
fn add_entity_updates_hierarchy_and_inspector() {
    let mut harness = Harness::with_entities();
    assert_eq!(harness.app().entities.len(), 2);
    assert_eq!(harness.app().selected, 1);

    harness.click("add-entity");
    assert_eq!(harness.app().entities.len(), 3);
    assert_eq!(harness.app().selected, 2);
    assert!(harness.shows("Entity 3"));
    assert!(harness.has("entity-2"));
}

#[test]
fn play_pause_toggles_the_toolbar_label() {
    let mut harness = Harness::with_entities();
    assert!(!harness.app().playing);
    harness.click("toggle-play");
    assert!(harness.app().playing);
    assert!(harness.shows("Pause"));
    harness.click("toggle-play");
    assert!(!harness.app().playing);
    assert!(harness.shows("Play"));
}

#[test]
fn selecting_an_entity_updates_the_inspector() {
    let mut harness = Harness::with_entities();
    harness.click("entity-0");
    assert_eq!(harness.app().selected, 0);
    assert!(harness.shows("Entity: Entity 1"));

    harness.click("entity-1");
    assert_eq!(harness.app().selected, 1);
    assert!(harness.shows("Entity: Entity 2"));
}

#[test]
fn typing_renames_the_selected_entity() {
    let mut harness = Harness::with_entities();
    harness.click("entity-1");
    harness.type_into("entity-name", "Boss");
    let renamed = harness.app().entities[1].name.clone();
    assert!(
        renamed.contains("Boss"),
        "typed text should reach the selected entity, got {renamed:?}"
    );
    assert!(
        harness.shows(&format!("Entity: {renamed}")),
        "the inspector should show the renamed entity"
    );
}

#[test]
fn nudges_update_transform_fields() {
    let mut harness = Harness::with_entities();
    harness.click("entity-0");
    let start = harness.app().entities[0].position[0];
    harness.click("nudge-x");
    assert!(harness.app().entities[0].position[0] > start);

    let rotation = harness.app().entities[0].rotation[1];
    harness.click("nudge-rot-y");
    assert!(harness.app().entities[0].rotation[1] > rotation);

    let scale = harness.app().entities[0].scale[0];
    harness.click("scale-up");
    assert!(harness.app().entities[0].scale[0] > scale);
}

#[test]
fn delete_removes_the_selected_entity() {
    let mut harness = Harness::with_entities();
    harness.click("entity-1");
    harness.click("delete-entity");
    assert_eq!(harness.app().entities.len(), 1);
    assert_eq!(harness.app().selected, 0);
    assert!(harness.shows("Entity: Entity 1"));
}

#[test]
fn orbit_changes_the_camera_and_reset_restores_it() {
    let mut harness = Harness::with_entities();
    let start = harness.app().camera.yaw;
    harness.click("orbit-left");
    assert!(harness.app().camera.yaw < start);

    harness.dispatch(Message::SetCamera(doppio_engine::Camera {
        yaw: 1.0,
        pitch: 0.5,
        distance: 20.0,
        focus: [1.0, 2.0, 3.0],
    }));
    assert_eq!(harness.app().camera.distance, 20.0);

    harness.click("reset-camera");
    assert_eq!(
        harness.app().camera,
        doppio_engine::Camera::default(),
        "reset should restore the default camera"
    );
}

#[test]
fn select_by_name_and_add_named_entity() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::AddEntityNamed("Rock".to_owned()));
    harness.dispatch(Message::AddEntityNamed("Tree".to_owned()));

    harness.dispatch(Message::SelectEntityByName("Rock".to_owned()));
    assert_eq!(harness.app().selected, 0);

    harness.dispatch(Message::SelectEntityByName("Missing".to_owned()));
    assert_eq!(harness.app().selected, 0);
    assert!(
        harness
            .app()
            .log
            .iter()
            .any(|line| line.contains("Missing")),
        "unknown names should be reported in the console"
    );
}

/// One scripted session covering the common editor loop, like a Playwright
/// spec: open, add, select, rename, move and delete.
#[test]
fn editor_workflow_end_to_end() {
    let mut harness = Harness::empty();

    harness.click("add-entity");
    harness.click("add-entity");
    assert_eq!(harness.app().entities.len(), 2);

    harness.click("entity-1");
    assert!(harness.shows("Entity: Entity 2"));

    harness.type_into("entity-name", "Boss");
    assert!(harness.app().entities[1].name.contains("Boss"));

    harness.click("nudge-x");
    assert!(harness.app().entities[1].position[0] > 0.0);

    harness.click("delete-entity");
    assert_eq!(harness.app().entities.len(), 1);
    assert!(harness.wait_for("Entity: Entity 1", 2));
}

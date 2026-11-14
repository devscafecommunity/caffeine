//! Playwright-style UI automation for the Doppio workspace.
//!
//! `Harness` drives the real Iced view tree: it clicks widgets by id, types
//! into inputs, drains the resulting messages through `App::update`, asserts
//! on the next frame, reads widget bounds and captures annotated PNGs.
//! Tests never open a window or need a GPU, so they run in CI.

use super::*;
use crate::uikit::id::{self, UiId};
use font8x8::UnicodeFonts;
use iced::widget::Id;
use iced::{Rectangle, Theme};
use iced_test::simulator;
use image::{Rgba, RgbaImage};
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, MutexGuard};

/// `iced_test` renders screenshots at 2× the logical size.
const SNAPSHOT_SCALE: f32 = 2.0;

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
    pub fn click(&mut self, id: impl Into<Id>) {
        let mut ui = simulator(self.app.view());
        let clicked = ui.click(id.into());
        assert!(
            clicked.is_ok(),
            "target should be clickable: {:?}",
            clicked.err()
        );
        let messages: Vec<_> = ui.into_messages().collect();
        self.drain(messages);
    }

    /// Clicks an input by id, types `text`, and applies the edits.
    pub fn type_into(&mut self, id: impl Into<Id>, text: &str) {
        let mut ui = simulator(self.app.view());
        let focused = ui.click(id.into());
        assert!(
            focused.is_ok(),
            "input should be focusable: {:?}",
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
    pub fn has(&self, id: impl Into<Id>) -> bool {
        let mut ui = simulator(self.app.view());
        ui.find(id.into()).is_ok()
    }

    /// Layout bounds of a widget, in logical pixels.
    pub fn bounds(&self, id: impl Into<Id>) -> Option<Rectangle> {
        let mut ui = simulator(self.app.view());
        ui.find(id.into()).ok().map(|target| target.bounds())
    }

    /// Waits for the text by rendering at most `frames` times, like a
    /// browser assertion that polls instead of sleeping.
    pub fn wait_for(&self, text: &str, frames: usize) -> bool {
        (0..frames).any(|_| self.shows(text))
    }

    /// Captures a PNG of `target` (plus its caption) and returns its bounds.
    ///
    /// The image is cropped from a headless `iced_test` frame, so it works in
    /// CI. `caption` overrides the catalog description.
    pub fn capture(
        &self,
        target: &UiId,
        path: &Path,
        caption: Option<&str>,
    ) -> Result<Rectangle, String> {
        let mut ui = simulator(self.app.view());
        let bounds = ui
            .find(Id::from(target.clone()))
            .map_err(|error| format!("{}: {error}", target.key()))?
            .bounds();

        let full = self.full_frame()?;
        let cropped = crop_scaled(&full, bounds)?;
        let caption = caption
            .or_else(|| id::description(target.key()))
            .unwrap_or(target.key());
        let annotated = with_caption(cropped, caption);

        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)
                .map_err(|error| format!("{}: {error}", parent.display()))?;
        }
        annotated
            .save(path)
            .map_err(|error| format!("{}: {error}", path.display()))?;
        Ok(bounds)
    }

    fn full_frame(&self) -> Result<RgbaImage, String> {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let temp = std::env::temp_dir().join(format!(
            "doppio-ui-frame-{}-{}.png",
            std::process::id(),
            COUNTER.fetch_add(1, Ordering::Relaxed)
        ));
        let _ = std::fs::remove_file(&temp);

        let mut ui = simulator(self.app.view());
        let snapshot = ui
            .snapshot(&Theme::Light)
            .map_err(|error| format!("snapshot: {error}"))?;
        snapshot
            .matches_image(&temp)
            .map_err(|error| format!("snapshot: {error}"))?;

        // iced_test appends the renderer name: "<stem>-<renderer>.png".
        let stem = temp
            .file_stem()
            .map(|stem| stem.to_string_lossy().into_owned())
            .unwrap_or_default();
        let directory = temp.parent().unwrap_or_else(|| Path::new("."));
        let written = std::fs::read_dir(directory)
            .map_err(|error| format!("{}: {error}", directory.display()))?
            .filter_map(Result::ok)
            .map(|entry| entry.path())
            .filter(|path| {
                path.extension().is_some_and(|ext| ext == "png")
                    && path
                        .file_name()
                        .is_some_and(|name| name.to_string_lossy().starts_with(&stem))
            })
            .max_by_key(|path| path.metadata().and_then(|meta| meta.modified()).ok())
            .ok_or_else(|| format!("snapshot for {stem} was not written"))?;

        let image = image::open(&written)
            .map_err(|error| format!("{}: {error}", written.display()))?
            .to_rgba8();
        let _ = std::fs::remove_file(&written);
        Ok(image)
    }
}

fn crop_scaled(image: &RgbaImage, bounds: Rectangle) -> Result<RgbaImage, String> {
    let x = (bounds.x * SNAPSHOT_SCALE).round().max(0.0) as u32;
    let y = (bounds.y * SNAPSHOT_SCALE).round().max(0.0) as u32;
    let width = (bounds.width * SNAPSHOT_SCALE).round().max(1.0) as u32;
    let height = (bounds.height * SNAPSHOT_SCALE).round().max(1.0) as u32;

    let x = x.min(image.width().saturating_sub(1));
    let y = y.min(image.height().saturating_sub(1));
    let width = width.min(image.width().saturating_sub(x)).max(1);
    let height = height.min(image.height().saturating_sub(y)).max(1);

    Ok(image::imageops::crop_imm(image, x, y, width, height).to_image())
}

/// Adds a dark caption strip and a highlight border to a cropped widget.
fn with_caption(mut image: RgbaImage, caption: &str) -> RgbaImage {
    let border = Rgba([255, 96, 128, 255]);
    draw_border(&mut image, border, 3);

    let scale = 2;
    let line_height = 8 * scale + 4;
    let width = image.width();
    let height = image.height();
    let max_chars = ((width.saturating_sub(10)) / (8 * scale)).max(8) as usize;
    let lines = wrap(caption, max_chars);
    let strip_height = lines.len() as u32 * line_height + 10;
    let mut out = RgbaImage::from_pixel(width, height + strip_height, Rgba([20, 22, 28, 255]));
    image::imageops::overlay(&mut out, &image, 0, 0);

    let mut y = height + 5;
    for line in lines {
        draw_text(&mut out, 6, y, &line, scale, Rgba([235, 238, 245, 255]));
        y += line_height;
    }
    out
}

fn draw_border(image: &mut RgbaImage, color: Rgba<u8>, thickness: u32) {
    let (width, height) = (image.width(), image.height());
    for y in 0..height {
        for x in 0..width {
            let edge = x < thickness
                || y < thickness
                || x >= width.saturating_sub(thickness)
                || y >= height.saturating_sub(thickness);
            if edge {
                image.put_pixel(x, y, color);
            }
        }
    }
}

fn draw_text(image: &mut RgbaImage, mut x: u32, y: u32, text: &str, scale: u32, color: Rgba<u8>) {
    for character in text.chars() {
        if let Some(glyph) = font8x8::BASIC_FONTS.get(character) {
            for (row, bits) in glyph.iter().enumerate() {
                for column in 0..8 {
                    if bits & (1 << column) != 0 {
                        for dy in 0..scale {
                            for dx in 0..scale {
                                let px = x + column as u32 * scale + dx;
                                let py = y + row as u32 * scale + dy;
                                if px < image.width() && py < image.height() {
                                    image.put_pixel(px, py, color);
                                }
                            }
                        }
                    }
                }
            }
        }
        x += 8 * scale;
        if x >= image.width() {
            break;
        }
    }
}

fn wrap(text: &str, max_chars: usize) -> Vec<String> {
    let mut lines = Vec::new();
    let mut line = String::new();
    for word in text.split_whitespace() {
        if !line.is_empty() && line.len() + 1 + word.len() > max_chars {
            lines.push(std::mem::take(&mut line));
        }
        if !line.is_empty() {
            line.push(' ');
        }
        line.push_str(word);
    }
    if !line.is_empty() {
        lines.push(line);
    }
    lines
}

#[test]
fn empty_editor_has_no_entities() {
    let harness = Harness::empty();
    assert!(harness.shows("No entities"));
    assert!(harness.shows("No entity selected"));
}

#[test]
fn every_pane_and_toolbar_button_is_addressable() {
    let harness = Harness::with_entities();
    for target in [
        id::ADD_ENTITY,
        id::TOGGLE_PLAY,
        id::PANE_HIERARCHY,
        id::PANE_ASSETS,
        id::PANE_VIEWPORT,
        id::PANE_INSPECTOR,
        id::PANE_CONSOLE,
        id::ENTITY_NAME,
        id::ORBIT_LEFT,
    ] {
        let bounds = harness
            .bounds(Id::from(target.clone()))
            .unwrap_or_else(|| panic!("missing bounds for {}", target.key()));
        assert!(
            bounds.width > 0.0 && bounds.height > 0.0,
            "{} has empty bounds",
            target.key()
        );
    }
}

#[test]
fn add_entity_updates_hierarchy_and_inspector() {
    let mut harness = Harness::with_entities();
    assert_eq!(harness.app().entities.len(), 2);
    assert_eq!(harness.app().selected, 1);

    harness.click(id::ADD_ENTITY);
    assert_eq!(harness.app().entities.len(), 3);
    assert_eq!(harness.app().selected, 2);
    assert!(harness.shows("Entity 3"));
    assert!(harness.has(Id::from(id::entity_row(2))));
}

#[test]
fn play_pause_toggles_the_toolbar_label() {
    let mut harness = Harness::with_entities();
    assert!(!harness.app().playing);
    harness.click(id::TOGGLE_PLAY);
    assert!(harness.app().playing);
    assert!(harness.shows("Pause"));
    harness.click(id::TOGGLE_PLAY);
    assert!(!harness.app().playing);
    assert!(harness.shows("Play"));
}

#[test]
fn selecting_an_entity_updates_the_inspector() {
    let mut harness = Harness::with_entities();
    harness.click(id::entity_row(0));
    assert_eq!(harness.app().selected, 0);
    assert!(harness.shows("Entity: Entity 1"));

    harness.click(id::entity_row(1));
    assert_eq!(harness.app().selected, 1);
    assert!(harness.shows("Entity: Entity 2"));
}

#[test]
fn typing_renames_the_selected_entity() {
    let mut harness = Harness::with_entities();
    harness.click(id::entity_row(1));
    harness.type_into(id::ENTITY_NAME, "Boss");
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
    harness.click(id::entity_row(0));
    let start = harness.app().entities[0].position[0];
    harness.click(id::NUDGE_X);
    assert!(harness.app().entities[0].position[0] > start);

    let rotation = harness.app().entities[0].rotation[1];
    harness.click(id::NUDGE_ROT_Y);
    assert!(harness.app().entities[0].rotation[1] > rotation);

    let scale = harness.app().entities[0].scale[0];
    harness.click(id::SCALE_UP);
    assert!(harness.app().entities[0].scale[0] > scale);
}

#[test]
fn delete_removes_the_selected_entity() {
    let mut harness = Harness::with_entities();
    harness.click(id::entity_row(1));
    harness.click(id::DELETE_ENTITY);
    assert_eq!(harness.app().entities.len(), 1);
    assert_eq!(harness.app().selected, 0);
    assert!(harness.shows("Entity: Entity 1"));
}

#[test]
fn orbit_changes_the_camera_and_reset_restores_it() {
    let mut harness = Harness::with_entities();
    let start = harness.app().camera.yaw;
    harness.click(id::ORBIT_LEFT);
    assert!(harness.app().camera.yaw < start);

    harness.dispatch(Message::SetCamera(doppio_engine::Camera {
        yaw: 1.0,
        pitch: 0.5,
        distance: 20.0,
        focus: [1.0, 2.0, 3.0],
    }));
    assert_eq!(harness.app().camera.distance, 20.0);

    harness.click(id::RESET_CAMERA);
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

    harness.click(id::ADD_ENTITY);
    harness.click(id::ADD_ENTITY);
    assert_eq!(harness.app().entities.len(), 2);

    harness.click(id::entity_row(1));
    assert!(harness.shows("Entity: Entity 2"));

    harness.type_into(id::ENTITY_NAME, "Boss");
    assert!(harness.app().entities[1].name.contains("Boss"));

    harness.click(id::NUDGE_X);
    assert!(harness.app().entities[1].position[0] > 0.0);

    harness.click(id::DELETE_ENTITY);
    assert_eq!(harness.app().entities.len(), 1);
    assert!(harness.wait_for("Entity: Entity 1", 2));
}

/// Generates annotated panel images + a Markdown manifest for docs.
///
/// ```bash
/// cargo test -p doppio-ui capture_ui_gallery -- --ignored --nocapture
/// ```
#[test]
#[ignore = "writes documentation images"]
fn capture_ui_gallery() {
    let harness = Harness::with_entities();
    let out = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../target/doppio-ui-gallery");
    std::fs::create_dir_all(&out).expect("gallery directory");

    let captures = [
        (id::PANE_HIERARCHY, "pane-hierarchy"),
        (id::PANE_ASSETS, "pane-assets"),
        (id::PANE_VIEWPORT, "pane-viewport"),
        (id::PANE_INSPECTOR, "pane-inspector"),
        (id::PANE_CONSOLE, "pane-console"),
        (id::ADD_ENTITY, "button-add-entity"),
        (id::ENTITY_NAME, "input-entity-name"),
    ];

    let mut manifest =
        String::from("# Doppio UI gallery\n\nGenerated by `capture_ui_gallery`.\n\n");
    for (target, name) in captures {
        let file = format!("{name}.png");
        harness
            .capture(&target, &out.join(&file), None)
            .unwrap_or_else(|error| panic!("{error}"));
        manifest.push_str(&format!(
            "## `{}`\n\n![{}]({})\n\n{}\n\n",
            target.key(),
            name,
            file,
            id::description(target.key()).unwrap_or_default()
        ));
    }
    std::fs::write(out.join("README.md"), manifest).expect("gallery manifest");
    println!("wrote {}", out.display());
}

//! Playwright-style UI automation for the Doppio workspace.
//!
//! `Harness` drives the real Iced view tree: it clicks widgets by id, types
//! into inputs, drains the resulting messages through `App::update`, asserts
//! on the next frame, reads widget bounds and captures annotated PNGs.
//! Tests never open a window or need a GPU, so they run in CI.

use super::*;
use crate::uikit::id::{self, UiId};
use iced::widget::Id;
use iced::{mouse, Point, Rectangle, Theme};
use iced_test::Simulator;

/// Same settings as the real application (12 px base text), so layout
/// assertions match what users see.
fn sim<'a>(element: impl Into<iced::Element<'a, Message>>) -> Simulator<'a, Message> {
    Simulator::with_settings(crate::uikit::theme::settings(), element)
}
use image::{Rgba, RgbaImage};
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{MutexGuard, OnceLock};

/// `iced_test` renders screenshots at 2× the logical size.
const SNAPSHOT_SCALE: f32 = 2.0;

/// Documentation captions use the JetBrains Mono already shipped in `assets/`.
const CAPTION_FONT: &[u8] = include_bytes!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../../assets/jetbrains-mono/static/jetbrains-mono-latin-400-normal.ttf"
));
const CAPTION_SIZE: f32 = 15.0;

fn caption_font() -> &'static fontdue::Font {
    static FONT: OnceLock<fontdue::Font> = OnceLock::new();
    FONT.get_or_init(|| {
        fontdue::Font::from_bytes(CAPTION_FONT, fontdue::FontSettings::default())
            .expect("bundled JetBrains Mono should load")
    })
}

/// Iced keeps process-wide state (themes, focus, text input), so UI tests run
/// one at a time inside this process.
fn test_guard() -> MutexGuard<'static, ()> {
    super::headless::ui_lock()
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

    /// Starts a fresh undo timeline after fixture setup, so a test can undo
    /// only the edits it makes itself.
    pub fn rebase_history(&mut self) {
        let initial = self.app.capture_state();
        self.app.history = crate::history::History::new(initial);
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
        let mut ui = sim(self.app.view());
        let clicked = ui.click(id.into());
        assert!(
            clicked.is_ok(),
            "target should be clickable: {:?}",
            clicked.err()
        );
        let messages: Vec<_> = ui.into_messages().collect();
        self.drain(messages);
    }

    /// Presses, moves and releases the left mouse button over a widget.
    pub fn drag(&mut self, id: impl Into<Id>, dx: f32, dy: f32) {
        let start = {
            let mut ui = sim(self.app.view());
            ui.find(id.into())
                .expect("drag target should exist")
                .bounds()
                .center()
        };
        self.drag_at(start, Point::new(start.x + dx, start.y + dy));
    }

    /// Drags one widget onto the centre of another widget.
    pub fn drag_to(&mut self, source: impl Into<Id>, target: impl Into<Id>) {
        let (source, target) = {
            let mut ui = sim(self.app.view());
            let source = ui
                .find(source.into())
                .expect("drag source should exist")
                .bounds()
                .center();
            let target = ui
                .find(target.into())
                .expect("drop target should exist")
                .bounds()
                .center();
            (source, target)
        };
        self.drag_at(source, target);
    }

    /// Drags from an absolute point to another absolute point.
    pub fn drag_at(&mut self, start: Point, end: Point) {
        let mut ui = sim(self.app.view());
        ui.point_at(start);
        ui.simulate([iced::Event::Mouse(mouse::Event::CursorMoved {
            position: start,
        })]);
        ui.simulate([iced::Event::Mouse(mouse::Event::ButtonPressed(
            mouse::Button::Left,
        ))]);
        for step in 1..=4 {
            let t = step as f32 / 4.0;
            let position = Point::new(
                start.x + (end.x - start.x) * t,
                start.y + (end.y - start.y) * t,
            );
            ui.point_at(position);
            ui.simulate([iced::Event::Mouse(mouse::Event::CursorMoved { position })]);
        }
        ui.simulate([iced::Event::Mouse(mouse::Event::ButtonReleased(
            mouse::Button::Left,
        ))]);
        let messages: Vec<_> = ui.into_messages().collect();
        self.drain(messages);
    }

    /// Clicks an input by id, types `text`, and applies the edits.
    pub fn type_into(&mut self, id: impl Into<Id>, text: &str) {
        let mut ui = sim(self.app.view());
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
        let mut ui = sim(self.app.view());
        ui.find(text).is_ok()
    }

    /// True when the current frame contains a widget with this id.
    pub fn has(&self, id: impl Into<Id>) -> bool {
        let mut ui = sim(self.app.view());
        ui.find(id.into()).is_ok()
    }

    /// Layout bounds of a widget, in logical pixels.
    pub fn bounds(&self, id: impl Into<Id>) -> Option<Rectangle> {
        let mut ui = sim(self.app.view());
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
        let mut ui = sim(self.app.view());
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

    /// Captures a group of widgets with numbered overlays and a legend.
    ///
    /// The crop covers the union of their bounds; each target gets a colored
    /// box and a badge, and the footer explains every number. This is the
    /// "annotated panel" format used for docs and agent hand-offs.
    pub fn capture_group(
        &self,
        targets: &[(UiId, &str)],
        path: &Path,
        caption: Option<&str>,
    ) -> Result<Rectangle, String> {
        let mut ui = sim(self.app.view());
        let mut boxes = Vec::with_capacity(targets.len());
        for (index, (target, label)) in targets.iter().enumerate() {
            let bounds = ui
                .find(Id::from(target.clone()))
                .map_err(|error| format!("{}: {error}", target.key()))?
                .bounds();
            boxes.push((bounds, *label, target, index));
        }
        let union = boxes
            .iter()
            .fold(boxes[0].0, |union, (bounds, ..)| Rectangle {
                x: union.x.min(bounds.x),
                y: union.y.min(bounds.y),
                width: (union.x + union.width).max(bounds.x + bounds.width) - union.x.min(bounds.x),
                height: (union.y + union.height).max(bounds.y + bounds.height)
                    - union.y.min(bounds.y),
            });

        let full = self.full_frame()?;
        let mut cropped = crop_scaled(&full, union)?;
        let mut legend = Vec::with_capacity(boxes.len());
        for (bounds, label, target, index) in boxes {
            let local = Rectangle {
                x: (bounds.x - union.x) * SNAPSHOT_SCALE,
                y: (bounds.y - union.y) * SNAPSHOT_SCALE,
                width: bounds.width * SNAPSHOT_SCALE,
                height: bounds.height * SNAPSHOT_SCALE,
            };
            let color = BADGE_COLORS[index % BADGE_COLORS.len()];
            draw_rect_border(&mut cropped, local, color, 2);
            draw_badge(&mut cropped, local, index + 1, color);
            legend.push(format!(
                "{}. {label} — {}",
                index + 1,
                id::description(target.key()).unwrap_or(target.key())
            ));
        }

        let mut lines = Vec::new();
        if let Some(caption) = caption {
            lines.extend(wrap(caption, caption_max_chars(union_width(&cropped))));
        }
        for line in legend {
            lines.extend(wrap(&line, caption_max_chars(union_width(&cropped))));
        }
        let annotated = with_footer(cropped, &lines);

        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)
                .map_err(|error| format!("{}: {error}", parent.display()))?;
        }
        annotated
            .save(path)
            .map_err(|error| format!("{}: {error}", path.display()))?;
        Ok(union)
    }

    fn full_frame(&self) -> Result<RgbaImage, String> {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let temp = std::env::temp_dir().join(format!(
            "doppio-ui-frame-{}-{}.png",
            std::process::id(),
            COUNTER.fetch_add(1, Ordering::Relaxed)
        ));
        let _ = std::fs::remove_file(&temp);

        let mut ui = sim(self.app.view());
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

fn union_width(image: &RgbaImage) -> u32 {
    image.width()
}

fn caption_max_chars(width: u32) -> usize {
    let advance = caption_font().rasterize('M', CAPTION_SIZE).0.advance_width;
    ((width.saturating_sub(12)) as f32 / advance.max(1.0))
        .floor()
        .max(8.0) as usize
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

const BADGE_COLORS: [Rgba<u8>; 6] = [
    Rgba([255, 96, 128, 255]),
    Rgba([64, 196, 255, 255]),
    Rgba([255, 214, 102, 255]),
    Rgba([126, 231, 135, 255]),
    Rgba([255, 159, 92, 255]),
    Rgba([196, 160, 255, 255]),
];

fn with_caption(image: RgbaImage, caption: &str) -> RgbaImage {
    let width = image.width();
    let lines = wrap(caption, caption_max_chars(width));
    with_footer(image, &lines)
}

/// Appends a dark footer with the given lines.
fn with_footer(mut image: RgbaImage, lines: &[String]) -> RgbaImage {
    draw_border(&mut image, BADGE_COLORS[0], 3);

    let line_height = (CAPTION_SIZE * 1.45).round() as u32;
    let padding = 6;
    let strip_height = lines.len() as u32 * line_height + padding * 2;
    let width = image.width();
    let height = image.height();
    let mut out = RgbaImage::from_pixel(width, height + strip_height, Rgba([20, 22, 28, 255]));
    image::imageops::overlay(&mut out, &image, 0, 0);

    let mut baseline = height + padding + CAPTION_SIZE as u32;
    for line in lines {
        draw_text(
            &mut out,
            6,
            baseline,
            line,
            CAPTION_SIZE,
            Rgba([235, 238, 245, 255]),
        );
        baseline += line_height;
    }
    out
}

fn draw_rect_border(image: &mut RgbaImage, rect: Rectangle, color: Rgba<u8>, thickness: i32) {
    let x0 = rect.x.round() as i32;
    let y0 = rect.y.round() as i32;
    let x1 = (rect.x + rect.width).round() as i32;
    let y1 = (rect.y + rect.height).round() as i32;
    for y in y0..y1 {
        for x in x0..x1 {
            let edge = x < x0 + thickness
                || y < y0 + thickness
                || x >= x1 - thickness
                || y >= y1 - thickness;
            if edge {
                blend_pixel(image, x, y, color, 255);
            }
        }
    }
}

fn draw_border(image: &mut RgbaImage, color: Rgba<u8>, thickness: u32) {
    let rect = Rectangle {
        x: 0.0,
        y: 0.0,
        width: image.width() as f32,
        height: image.height() as f32,
    };
    draw_rect_border(image, rect, color, thickness as i32);
}

fn draw_badge(image: &mut RgbaImage, rect: Rectangle, number: usize, color: Rgba<u8>) {
    let size = 22;
    let x = rect.x.round() as i32 + 2;
    let y = rect.y.round() as i32 + 2;
    for py in y..y + size {
        for px in x..x + size {
            blend_pixel(image, px, py, color, 255);
        }
    }
    draw_text(
        image,
        x as u32 + 5,
        y as u32 + size as u32 - 6,
        &number.to_string(),
        15.0,
        Rgba([20, 22, 28, 255]),
    );
}

fn draw_text(image: &mut RgbaImage, x: u32, baseline: u32, text: &str, size: f32, color: Rgba<u8>) {
    let font = caption_font();
    let mut pen = x as f32;
    for character in text.chars() {
        let (metrics, bitmap) = font.rasterize(character, size);
        let origin_x = pen.round() as i32 + metrics.xmin;
        let origin_y = baseline as i32 - metrics.ymin - metrics.height as i32;
        for (index, coverage) in bitmap.iter().enumerate() {
            if *coverage == 0 || metrics.width == 0 {
                continue;
            }
            let px = origin_x + (index % metrics.width) as i32;
            let py = origin_y + (index / metrics.width) as i32;
            blend_pixel(image, px, py, color, *coverage);
        }
        pen += metrics.advance_width;
    }
}

fn blend_pixel(image: &mut RgbaImage, x: i32, y: i32, color: Rgba<u8>, alpha: u8) {
    if x < 0 || y < 0 || x >= image.width() as i32 || y >= image.height() as i32 {
        return;
    }
    let pixel = image.get_pixel(x as u32, y as u32);
    let a = alpha as u32;
    let inverse = 255 - a;
    let blend = |foreground: u8, background: u8| {
        ((foreground as u32 * a + background as u32 * inverse) / 255) as u8
    };
    image.put_pixel(
        x as u32,
        y as u32,
        Rgba([
            blend(color[0], pixel[0]),
            blend(color[1], pixel[1]),
            blend(color[2], pixel[2]),
            pixel[3].max(alpha),
        ]),
    );
}

fn wrap(text: &str, max_chars: usize) -> Vec<String> {
    let mut lines = Vec::new();
    let mut line = String::new();
    for word in text.split_whitespace() {
        if !line.is_empty() && line.chars().count() + 1 + word.chars().count() > max_chars {
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

fn engine_harness() -> Option<Harness> {
    // Take the UI/GPU lock before creating the engine: two idle devices in the
    // same process make later render_begin calls fail under the full suite.
    let guard = test_guard();
    let mut engine = doppio_engine::Engine::load_default()?;
    let first = engine.create_entity("First").ok()?;
    engine.set_position(first, [2.0, 0.0, 0.0]).ok()?;
    engine.create_entity("Second").ok()?;
    let entities = projectloader::read_entities(&engine);
    let loaded = projectloader::LoadedProject {
        engine: Some(engine),
        entities,
        assets: Vec::new(),
        log: Vec::new(),
    };
    Some(Harness {
        app: App::with_project(loaded),
        _guard: guard,
    })
}

#[test]
fn viewport_drag_orbits_and_wheel_zooms() {
    let mut harness = Harness::with_entities();
    harness.rebase_history();
    let yaw = harness.app().camera.yaw;
    let distance = harness.app().camera.distance;

    harness.dispatch(Message::ViewportPressed(mouse::Button::Left));
    harness.dispatch(Message::ViewportMoved(Point::new(10.0, 10.0)));
    harness.dispatch(Message::ViewportMoved(Point::new(40.0, 10.0)));
    assert!(harness.app().camera.yaw > yaw, "left drag should orbit");
    harness.dispatch(Message::ViewportReleased(mouse::Button::Left));

    harness.dispatch(Message::ViewportScrolled(mouse::ScrollDelta::Lines {
        x: 0.0,
        y: 2.0,
    }));
    assert!(
        harness.app().camera.distance < distance,
        "wheel up should zoom in"
    );
}

#[test]
fn prefab_palette_creates_an_entity_from_a_category() {
    let mut harness = Harness::with_entities();
    let before = harness.app().entities.len();
    harness.click(id::HIERARCHY_ADD);
    assert!(harness.shows("Create Entity"));
    harness.click(Id::from(id::prefab_category("mesh")));
    harness.click(Id::from(id::prefab_item("cube")));
    assert_eq!(harness.app().entities.len(), before + 1);
    assert_eq!(
        harness
            .app()
            .entities
            .last()
            .map(|entity| entity.name.as_str()),
        Some("Cube")
    );
    assert_eq!(harness.app().selected, before);
}

#[test]
fn prefab_mesh_creation_adds_renderer_components() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.rebase_history();
    let index = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|prefab| prefab.key == "cube")
        .expect("cube prefab");
    harness.app.create_prefab(index);
    let entity = harness.app().entities.last().expect("created entity");
    let components = harness
        .app()
        .engine
        .as_ref()
        .expect("engine")
        .entity_components(entity.id);
    let names: Vec<&str> = components
        .iter()
        .map(|component| component.name.as_str())
        .collect();
    assert!(names.contains(&"Mesh Filter"));
    assert!(names.contains(&"Mesh Renderer"));
    assert_eq!(harness.app().history.undo_name(), Some("Create Cube"));
}

#[test]
fn duplicate_prefab_copies_components() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.rebase_history();
    let cube = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|prefab| prefab.key == "cube")
        .expect("cube prefab");
    harness.app.create_prefab(cube);
    let source = harness.app().entities.last().expect("cube").id;
    let source_components = harness
        .app()
        .engine
        .as_ref()
        .expect("engine")
        .entity_components(source)
        .len();
    let duplicate = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|prefab| prefab.key == "duplicate")
        .expect("duplicate prefab");
    harness.app.create_prefab(duplicate);
    let copy = harness.app().entities.last().expect("copy");
    let copy_components = harness
        .app()
        .engine
        .as_ref()
        .expect("engine")
        .entity_components(copy.id)
        .len();
    assert_eq!(copy_components, source_components);
    assert!(copy.name.contains("Copy"));
}

#[test]
fn gizmo_shortcuts_switch_modes_only_over_the_viewport() {
    let mut harness = Harness::with_entities();
    harness.app.viewport_hovered = true;
    harness.dispatch(Message::GizmoShortcut(2));
    assert_eq!(harness.app().gizmo_mode, gizmo::Mode::Rotate);
    harness.dispatch(Message::GizmoShortcut(3));
    assert_eq!(harness.app().gizmo_mode, gizmo::Mode::Scale);
    harness.app.viewport_hovered = false;
    harness.dispatch(Message::GizmoShortcut(1));
    assert_eq!(harness.app().gizmo_mode, gizmo::Mode::Scale);
}

#[test]
fn gizmo_move_edit_updates_the_entity_and_history() {
    let mut harness = Harness::with_entities();
    harness.rebase_history();
    harness.dispatch(Message::Gizmo(gizmo::Message::Changed(gizmo::Edit::Move {
        axis: gizmo::Axis::X,
        position: [2.0, 1.0, 0.0],
    })));
    let selected = harness.app().selected;
    assert_eq!(harness.app().entities[selected].position, [2.0, 1.0, 0.0]);
    assert_eq!(harness.app().history.undo_name(), Some("Move X"));
}

#[test]
fn gizmo_snapping_rounds_edits_and_toggles_from_the_menu_and_shortcut() {
    let mut harness = Harness::with_entities();
    harness.rebase_history();
    assert!(!harness.app().gizmo_snap);
    harness.dispatch(Message::Gizmo(gizmo::Message::Changed(gizmo::Edit::Move {
        axis: gizmo::Axis::X,
        position: [2.13, 1.0, 0.0],
    })));
    let selected = harness.app().selected;
    assert_eq!(harness.app().entities[selected].position[0], 2.13);

    harness.app.viewport_hovered = true;
    harness.dispatch(Message::GizmoShortcut(4));
    assert!(
        harness.app().gizmo_snap,
        "X toggles snapping over the viewport"
    );
    harness.dispatch(Message::Gizmo(gizmo::Message::Changed(gizmo::Edit::Move {
        axis: gizmo::Axis::X,
        position: [2.13, 1.0, 0.0],
    })));
    assert_eq!(harness.app().entities[selected].position[0], 2.25);
    harness.dispatch(Message::Gizmo(gizmo::Message::Changed(
        gizmo::Edit::Rotate {
            axis: gizmo::Axis::Y,
            rotation: [0.0, 37.0, 0.0],
        },
    )));
    assert_eq!(harness.app().entities[selected].rotation[1], 30.0);

    harness.app.viewport_hovered = false;
    harness.dispatch(Message::GizmoShortcut(4));
    assert!(
        harness.app().gizmo_snap,
        "the shortcut only works over the viewport"
    );
    harness.dispatch(Message::ToggleGizmoSnap);
    assert!(!harness.app().gizmo_snap);
}

#[test]
fn gizmo_move_uses_world_position_for_a_parented_child() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.rebase_history();
    let parent = harness.app().entities[0].id;
    let child = harness.app().entities[1].id;
    harness
        .app
        .engine
        .as_mut()
        .expect("engine")
        .set_entity_parent(child, Some(parent))
        .expect("parent link");
    harness.app.selected = 1;

    harness.dispatch(Message::Gizmo(gizmo::Message::Changed(gizmo::Edit::Move {
        axis: gizmo::Axis::X,
        position: [5.0, 0.0, 0.0],
    })));

    // First sits at x=2, so world x=5 is local x=3 on the child.
    assert_eq!(harness.app().entities[1].position, [3.0, 0.0, 0.0]);
    let local = harness
        .app()
        .engine
        .as_ref()
        .expect("engine")
        .position(child)
        .expect("child position");
    assert_eq!(local, [3.0, 0.0, 0.0]);
    assert_eq!(harness.app().history.undo_name(), Some("Move X"));
}

#[test]
fn viewport_drag_widget_orbits_the_camera() {
    let mut harness = Harness::with_entities();
    let yaw = harness.app().camera.yaw;
    let pitch = harness.app().camera.pitch;
    harness.drag(id::VIEWPORT_SURFACE, 60.0, 20.0);
    assert!(
        harness.app().camera.yaw > yaw,
        "drag should reach the surface"
    );
    assert!(
        harness.app().camera.pitch > pitch,
        "drag should orbit pitch"
    );
}

#[test]
fn viewport_fly_moves_up_without_a_height_limit() {
    let mut harness = Harness::with_entities();
    // Looking up: W follows the view direction instead of the ground plane.
    harness.app.camera.pitch = -PITCH_LIMIT;
    harness.app.camera.distance = 100.0;
    harness.dispatch(Message::ViewportKey(FlyKey::Forward, true));
    for _ in 0..200 {
        harness.app.fly_camera(0.1);
    }
    harness.dispatch(Message::ViewportKey(FlyKey::Forward, false));
    assert!(
        harness.app().camera.focus[1] > 1000.0,
        "forward flight must leave the ground"
    );

    harness.dispatch(Message::ViewportKey(FlyKey::Up, true));
    for _ in 0..200 {
        harness.app.fly_camera(0.1);
    }
    harness.dispatch(Message::ViewportKey(FlyKey::Up, false));
    assert!(
        harness.app().camera.focus[1] > 5000.0,
        "Q/E flight must not clamp altitude"
    );

    harness.dispatch(Message::ViewportKey(FlyKey::Forward, true));
    harness.dispatch(Message::ViewportHover(false));
    assert!(
        harness.app().fly_keys.is_empty(),
        "leaving the viewport must release held flight keys"
    );
}

#[test]
fn viewport_resize_targets_native_ratio_and_budget() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::ViewportResized(800.0, 450.0));
    assert_eq!(harness.app().viewport_desired, (1600, 900));

    let (width, height) = App::viewport_target_size(4000.0, 3000.0);
    assert!(width as f32 * height as f32 <= VIEWPORT_PIXEL_BUDGET + 10_000.0);
    let ratio = width as f32 / height as f32;
    assert!(
        (ratio - 4.0 / 3.0).abs() < 0.01,
        "pixel budget must preserve the pane aspect ratio"
    );
}

#[test]
fn view_menu_toggles_the_reference_grid() {
    let mut harness = Harness::empty();
    show_page(&mut harness, PaneKind::Console);
    assert!(harness.app().viewport_settings.show_grid);
    harness.dispatch(Message::ToggleGrid);
    assert!(!harness.app().viewport_settings.show_grid);
    harness.dispatch(Message::ToggleGrid);
    assert!(harness.app().viewport_settings.show_grid);
    harness.dispatch(Message::ToggleGrid);
    assert!(harness.shows("Grid off"));

    harness.dispatch(Message::SetViewportSettings {
        show_grid: Some(true),
        anti_aliasing: Some(doppio_engine::AntiAliasing::Taa),
        render_scale: Some(1.25),
    });
    let settings = harness.app().viewport_settings;
    assert!(settings.show_grid);
    assert_eq!(settings.anti_aliasing, doppio_engine::AntiAliasing::Taa);
    assert!((settings.render_scale - 1.25).abs() < 0.001);
}

#[test]
fn viewport_right_drag_pans_and_focus_targets_selection() {
    let mut harness = Harness::with_entities();
    harness.rebase_history();
    let focus = harness.app().camera.focus;

    harness.dispatch(Message::ViewportPressed(mouse::Button::Right));
    harness.dispatch(Message::ViewportMoved(Point::new(10.0, 10.0)));
    harness.dispatch(Message::ViewportMoved(Point::new(40.0, 25.0)));
    harness.dispatch(Message::ViewportReleased(mouse::Button::Left));
    assert_ne!(harness.app().camera.focus, focus, "right drag should pan");

    harness.click(id::entity_row(1));
    harness.dispatch(Message::FocusSelection);
    assert_eq!(
        harness.app().camera.focus,
        harness.app().entities[1].position,
        "focus should target the selected entity"
    );
}

fn tree_entity(id: u32, parent: Option<u32>) -> EntityView {
    EntityView {
        id,
        name: format!("Node {id}"),
        position: [0.0; 3],
        rotation: [0.0; 3],
        scale: [1.0; 3],
        parent,
        icon: crate::uikit::icons::Icon::Generic,
    }
}

#[test]
fn hierarchy_tree_collapses_and_expands_children() {
    let entities = vec![
        tree_entity(0, None),
        tree_entity(1, Some(0)),
        tree_entity(2, Some(1)),
    ];
    let loaded = projectloader::LoadedProject {
        engine: None,
        entities,
        assets: Vec::new(),
        log: Vec::new(),
    };
    let mut harness = Harness {
        app: App::with_project(loaded),
        _guard: test_guard(),
    };
    assert!(harness.has(Id::from(id::entity_row(2))));

    harness.dispatch(Message::HierarchyToggle(0));
    assert!(!harness.has(Id::from(id::entity_row(1))));
    assert!(!harness.has(Id::from(id::entity_row(2))));

    harness.dispatch(Message::HierarchyToggle(0));
    assert!(harness.has(Id::from(id::entity_row(1))));
    assert!(harness.has(Id::from(id::entity_row(2))));
}

#[test]
fn hierarchy_toolbar_adds_and_chevron_toggles() {
    let entities = vec![tree_entity(0, None), tree_entity(1, Some(0))];
    let loaded = projectloader::LoadedProject {
        engine: None,
        entities,
        assets: Vec::new(),
        log: Vec::new(),
    };
    let mut harness = Harness {
        app: App::with_project(loaded),
        _guard: test_guard(),
    };
    let before = harness.app().entities.len();
    harness.click(id::HIERARCHY_ADD);
    harness.click(Id::from(id::prefab_item("empty")));
    assert_eq!(harness.app().entities.len(), before + 1);

    let toggle = id::tree_toggle(id::entity_row(0).key());
    harness.click(toggle.clone());
    assert!(!harness.has(Id::from(id::entity_row(1))));
    harness.click(toggle);
    assert!(harness.has(Id::from(id::entity_row(1))));
}

#[test]
fn hierarchy_root_collapses_and_search_filters() {
    let mut harness = Harness::with_entities();
    harness.click(id::HIERARCHY_ROOT);
    assert!(!harness.has(Id::from(id::entity_row(0))));
    harness.click(id::HIERARCHY_ROOT);
    assert!(harness.has(Id::from(id::entity_row(0))));

    harness.type_into(id::HIERARCHY_SEARCH, "Entity 2");
    assert!(!harness.has(Id::from(id::entity_row(0))));
    assert!(harness.has(Id::from(id::entity_row(1))));
}

#[test]
fn dragging_a_tab_moves_only_that_page() {
    let mut harness = Harness::with_entities();
    let panes: Vec<_> = harness
        .app()
        .panes
        .iter()
        .map(|(pane, group)| (*pane, group.pages.clone()))
        .collect();
    let source = panes
        .iter()
        .find(|(_, pages)| pages.contains(&PaneKind::Assets))
        .map(|(pane, _)| *pane)
        .expect("assets tab");
    let target = panes
        .iter()
        .find(|(_, pages)| pages.contains(&PaneKind::Viewport))
        .map(|(pane, _)| *pane)
        .expect("viewport tab");
    harness.dispatch(Message::TabPressed(source, PaneKind::Assets));
    harness.dispatch(Message::TabEnter(target, PaneKind::Viewport));
    harness.dispatch(Message::PointerReleased);
    let target_pages = harness
        .app()
        .panes
        .get(target)
        .expect("target pane")
        .pages
        .clone();
    let source_pages = harness
        .app()
        .panes
        .get(source)
        .expect("source pane")
        .pages
        .clone();
    assert!(target_pages.contains(&PaneKind::Assets));
    assert!(!source_pages.contains(&PaneKind::Assets));
    assert_eq!(
        harness
            .app()
            .panes
            .get(target)
            .expect("target")
            .active_kind(),
        PaneKind::Assets
    );
}

#[test]
fn splitting_a_tab_creates_a_new_pane() {
    let mut harness = Harness::with_entities();
    let (pane, before) = {
        let (pane, group) = harness
            .app()
            .panes
            .iter()
            .find(|(_, group)| group.pages.len() > 1)
            .expect("grouped pane");
        (*pane, group.pages.len())
    };
    harness.dispatch(Message::SplitPage(pane));
    assert!(harness.app().panes.len() > 4);
    assert!(harness
        .app()
        .panes
        .iter()
        .any(
            |(_, group)| group.pages.len() == 1 && group.pages[0] == PaneKind::Assets
                || group.pages.len() == 1 && group.pages[0] == PaneKind::Hierarchy
        ));
    assert!(before >= 2);
}

#[test]
fn component_cards_collapse_without_touching_the_scene() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let type_id = harness
        .app()
        .components
        .first()
        .map(|component| component.info.type_id)
        .expect("component");
    let before = harness.app().entities.clone();
    harness.dispatch(Message::Inspector(
        panels::inspector::Edit::ToggleComponent(type_id),
    ));
    assert!(harness.app().collapsed_components.contains(&type_id));
    harness.dispatch(Message::Inspector(
        panels::inspector::Edit::ToggleComponent(type_id),
    ));
    assert!(!harness.app().collapsed_components.contains(&type_id));
    assert_eq!(harness.app().entities, before);
}

#[test]
fn transform_card_edits_position_with_one_undo_step() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.rebase_history();
    harness.dispatch(Message::Inspector(panels::inspector::Edit::TransformText {
        kind: 0,
        axis: 0,
        value: "7.5".to_owned(),
    }));
    assert_eq!(harness.app().entities[0].position[0], 7.5);
    assert_eq!(harness.app().history.undo_name(), Some("Set Position X"));
    harness.dispatch(Message::Undo);
    assert_eq!(harness.app().entities[0].position[0], 2.0);
}

#[test]
fn layout_presets_keep_every_page() {
    for preset in [LayoutPreset::Default, LayoutPreset::Viewport] {
        let state = pane_grid::State::with_configuration(App::layout_configuration(preset));
        assert_eq!(state.len(), 4);
        let pages: usize = state.iter().map(|(_, group)| group.pages.len()).sum();
        assert_eq!(pages, 14);
    }
    let mut harness = Harness::with_entities();
    harness.dispatch(Message::Layout(LayoutPreset::Viewport));
    assert_eq!(harness.app().panes.len(), 4);
}

#[test]
fn pane_tabs_switch_between_grouped_panels() {
    let mut harness = Harness::with_entities();
    assert!(harness.has(Id::from(id::PANE_HIERARCHY)));
    harness.click(id::PANE_TAB_ASSETS);
    assert!(harness.has(Id::from(id::PANE_ASSETS)));
    harness.click(id::PANE_TAB_HIERARCHY);
    assert!(harness.has(Id::from(id::PANE_HIERARCHY)));
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
        id::MENU_SCENE,
        id::MENU_EDIT,
        id::MENU_VIEW,
        id::PANE_HIERARCHY,
        id::PANE_VIEWPORT,
        id::PANE_INSPECTOR,
        id::PANE_ASSETS,
        id::PANE_TAB_CONSOLE,
        id::PANE_TAB_ASSETS,
        id::ENTITY_NAME,
        id::VIEWPORT_SURFACE,
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

    harness.dispatch(Message::AddEntity);
    assert_eq!(harness.app().entities.len(), 3);
    assert_eq!(harness.app().selected, 2);
    assert!(harness.shows("Entity 3"));
    assert!(harness.has(Id::from(id::entity_row(2))));
}

#[test]
fn play_pause_toggles_the_status_and_menu() {
    let mut harness = Harness::with_entities();
    assert!(!harness.app().playing);
    harness.dispatch(Message::TogglePlay);
    assert!(harness.app().playing);
    assert!(harness.shows("Playing"));
    harness.click(id::MENU_SCENE);
    assert!(harness.shows("Pause"));
    harness.dispatch(Message::TogglePlay);
    assert!(!harness.app().playing);
    assert!(harness.shows("Stopped"));
}

#[test]
fn hierarchy_context_menu_deletes_the_entity() {
    let mut harness = Harness::with_entities();
    harness.rebase_history();
    harness.dispatch(Message::HierarchyContext(1));
    harness.dispatch(Message::ViewportTick);
    assert!(harness.shows("Delete Entity"));
    harness.click(id::DELETE_ENTITY);
    assert_eq!(harness.app().entities.len(), 1);
    assert_eq!(harness.app().selected, 0);
}

#[test]
fn hierarchy_double_click_focuses_the_entity() {
    let mut harness = Harness::with_entities();
    harness.dispatch(Message::HierarchyDoubleClick(1));
    assert_eq!(
        harness.app().camera.focus,
        harness.app().entities[1].position
    );
}

#[test]
fn selecting_an_entity_updates_the_inspector() {
    let mut harness = Harness::with_entities();
    harness.click(id::entity_row(0));
    assert_eq!(harness.app().selected, 0);
    assert!(harness.shows("Entity 1"));

    harness.click(id::entity_row(1));
    assert_eq!(harness.app().selected, 1);
    assert!(harness.shows("Entity 2"));
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
        harness.shows(&renamed),
        "the inspector should show the renamed entity"
    );
}

#[test]
fn component_fields_edit_transform_and_add_components() {
    let Some(mut harness) = engine_harness() else {
        eprintln!("skipping component test: libcaffeine-capi is not built");
        return;
    };
    harness.rebase_history();
    harness.dispatch(Message::SelectEntity(0));
    assert!(
        harness.shows("Transform"),
        "the generic inspector lists components"
    );

    harness.dispatch(Message::Inspector(panels::inspector::Edit::FieldText {
        component: 0,
        field: 0,
        axis: 0,
        value: "5.5".to_owned(),
    }));
    assert_eq!(harness.app().entities[0].position[0], 5.5);
    assert_eq!(harness.app().history.len(), 1);

    let camera = harness
        .app()
        .component_types
        .iter()
        .find(|kind| kind.name == "Camera 3D")
        .expect("camera schema")
        .id;
    let before = harness.app().components.len();
    harness.dispatch(Message::Inspector(panels::inspector::Edit::AddComponent(
        camera,
    )));
    assert_eq!(harness.app().components.len(), before + 1);
    assert!(harness.shows("Camera 3D"));
}

#[test]
fn delete_removes_the_selected_entity() {
    let mut harness = Harness::with_entities();
    harness.click(id::entity_row(1));
    harness.dispatch(Message::Inspector(panels::inspector::Edit::Delete));
    assert_eq!(harness.app().entities.len(), 1);
    assert_eq!(harness.app().selected, 0);
    assert!(harness.shows("Entity 1"));
}

#[test]
fn orbit_changes_the_camera_and_reset_restores_it() {
    let mut harness = Harness::with_entities();
    let start = harness.app().camera.yaw;
    harness.dispatch(Message::ViewportPressed(mouse::Button::Left));
    harness.dispatch(Message::ViewportMoved(Point::new(0.0, 0.0)));
    harness.dispatch(Message::ViewportMoved(Point::new(30.0, 0.0)));
    harness.dispatch(Message::ViewportReleased(mouse::Button::Left));
    assert!(harness.app().camera.yaw > start);

    harness.dispatch(Message::SetCamera(doppio_engine::Camera {
        yaw: 1.0,
        pitch: 0.5,
        distance: 20.0,
        focus: [1.0, 2.0, 3.0],
    }));
    assert_eq!(harness.app().camera.distance, 20.0);

    harness.click(id::MENU_VIEW);
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
            .any(|entry| entry.text.contains("Missing")),
        "unknown names should be reported in the console"
    );
}

#[test]
fn undo_redo_covers_add_and_delete() {
    let mut harness = Harness::empty();
    harness.rebase_history();

    harness.dispatch(Message::AddEntity);
    harness.dispatch(Message::AddEntity);
    assert_eq!(harness.app().entities.len(), 2);
    assert_eq!(harness.app().history.len(), 2);

    harness.dispatch(Message::Undo);
    assert_eq!(harness.app().entities.len(), 1);
    harness.dispatch(Message::Undo);
    assert_eq!(harness.app().entities.len(), 0);

    harness.dispatch(Message::Redo);
    assert_eq!(harness.app().entities.len(), 1);
    harness.dispatch(Message::Redo);
    assert_eq!(harness.app().entities.len(), 2);
}

#[test]
fn child_world_position_includes_the_parent_transform() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let engine = harness.app.engine.as_mut().expect("engine");
    let parent = engine.create_entity("Group").expect("group");
    let child = engine.create_entity("Child").expect("child");
    engine
        .set_position(parent, [10.0, 0.0, 0.0])
        .expect("parent position");
    engine
        .set_position(child, [2.0, 0.0, 0.0])
        .expect("child position");
    engine
        .set_entity_parent(child, Some(parent))
        .expect("parent link");
    assert_eq!(
        engine.world_position(child).expect("child world"),
        [12.0, 0.0, 0.0]
    );

    engine
        .set_scale(parent, [2.0, 2.0, 2.0])
        .expect("parent scale");
    assert_eq!(
        engine.world_position(child).expect("child world"),
        [14.0, 0.0, 0.0]
    );

    engine
        .set_world_position(child, [20.0, 0.0, 0.0])
        .expect("world position");
    assert_eq!(
        engine.position(child).expect("child local"),
        [5.0, 0.0, 0.0]
    );
    assert_eq!(
        engine.world_position(child).expect("child world"),
        [20.0, 0.0, 0.0]
    );
    engine
        .set_position(child, [2.0, 0.0, 0.0])
        .expect("restore child");

    let entities = projectloader::read_entities(harness.app.engine.as_ref().expect("engine"));
    harness.app.entities = entities;
    harness.app.selected = harness
        .app
        .entities
        .iter()
        .position(|entity| entity.id == child)
        .expect("child view");
    harness.app.focus_selection();
    assert!((harness.app().camera.focus[0] - 14.0).abs() < 0.001);
}

#[test]
fn undo_does_not_drop_a_pending_viewport_readback() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.rebase_history();
    harness.dispatch(Message::Inspector(panels::inspector::Edit::TransformText {
        kind: 0,
        axis: 0,
        value: "3.0".to_owned(),
    }));
    harness.app.viewport_pending = true;
    harness.dispatch(Message::Undo);
    assert!(
        harness.app().viewport_pending,
        "undo must keep polling the in-flight readback"
    );
}

#[test]
fn engine_undo_restores_the_scene_snapshot() {
    let guard = test_guard();
    let Some(mut engine) = doppio_engine::Engine::load_default() else {
        eprintln!("skipping engine undo test: libcaffeine-capi is not built");
        return;
    };
    let first = engine.create_entity("First").expect("first entity");
    engine
        .set_position(first, [2.0, 0.0, 0.0])
        .expect("position");
    engine.create_entity("Second").expect("second entity");
    let entities = projectloader::read_entities(&engine);
    let loaded = projectloader::LoadedProject {
        engine: Some(engine),
        entities,
        assets: Vec::new(),
        log: Vec::new(),
    };

    let mut harness = Harness {
        app: App::with_project(loaded),
        _guard: guard,
    };
    harness.rebase_history();
    harness.dispatch(Message::SelectEntity(0));
    let start = harness.app().entities[0].position[0];

    harness.dispatch(Message::Inspector(panels::inspector::Edit::FieldText {
        component: 0,
        field: 0,
        axis: 0,
        value: "9.0".to_owned(),
    }));
    assert_eq!(harness.app().entities[0].position[0], 9.0);

    harness.dispatch(Message::Undo);
    assert_eq!(harness.app().entities.len(), 2);
    assert_eq!(harness.app().entities[0].name, "First");
    assert_eq!(harness.app().entities[0].position[0], start);
}

#[test]
fn reparenting_updates_the_ui_and_engine() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.rebase_history();
    let child = harness.app().entities[1].id;
    let parent = harness.app().entities[0].id;
    harness.dispatch(Message::SetParent {
        entity: child,
        parent: Some(parent),
    });
    assert_eq!(harness.app().entities[1].parent, Some(parent));
    // Undo restores the link through the scene snapshot.
    harness.dispatch(Message::Undo);
    assert_eq!(harness.app().entities[1].parent, None);
}

#[test]
fn dragging_a_tree_row_reparents_it() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.rebase_history();
    let child = harness.app().entities[0].id;
    let parent = harness.app().entities[1].id;
    harness.drag(id::entity_row(0), 0.0, 28.0);
    assert_eq!(
        harness.app().entities[0].parent,
        Some(parent),
        "dragging row 0 onto row 1 should parent it"
    );
    harness.drag_to(id::entity_row(0), id::HIERARCHY_ROOT);
    assert_eq!(
        harness.app().entities[0].parent,
        None,
        "dropping on the scene root should unparent it"
    );
    let _ = child;
}

#[test]
fn typing_renames_as_one_undo_step() {
    let mut harness = Harness::with_entities();
    harness.rebase_history();
    harness.click(id::entity_row(1));

    harness.type_into(id::ENTITY_NAME, "Boss");
    assert!(harness.app().entities[1].name.contains("Boss"));
    assert_eq!(
        harness.app().history.len(),
        1,
        "a typing session should coalesce into one step"
    );

    harness.dispatch(Message::Undo);
    assert_eq!(harness.app().entities[1].name, "Entity 2");
}

#[test]
fn undo_restores_transform_and_selection() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.rebase_history();
    harness.dispatch(Message::SelectEntity(0));
    let start = harness.app().entities[0].position[0];

    harness.dispatch(Message::Inspector(panels::inspector::Edit::FieldText {
        component: 0,
        field: 0,
        axis: 0,
        value: "7.0".to_owned(),
    }));
    assert_eq!(harness.app().entities[0].position[0], 7.0);

    harness.dispatch(Message::SelectEntity(1));
    assert_eq!(harness.app().selected, 1);

    harness.dispatch(Message::Undo);
    assert_eq!(harness.app().entities[0].position[0], start);
    assert_eq!(
        harness.app().selected,
        0,
        "undo restores the selection captured with the step"
    );
}

#[test]
fn new_edit_after_undo_drops_redo() {
    let mut harness = Harness::empty();
    harness.rebase_history();
    harness.dispatch(Message::AddEntity);
    harness.dispatch(Message::AddEntity);
    harness.dispatch(Message::Undo);
    assert!(harness.app().history.can_redo());

    harness.dispatch(Message::AddEntity);
    assert!(!harness.app().history.can_redo());
    assert_eq!(harness.app().entities.len(), 2);
}

#[test]
fn open_menu_items_have_bounds() {
    let mut harness = Harness::with_entities();
    harness.click(id::MENU_VIEW);
    assert!(harness.shows("Focus Selection"));
    assert!(harness.bounds(Id::from(id::LAYOUT_DEFAULT)).is_some());
}

#[test]
fn menus_stay_open_across_viewport_ticks() {
    let mut harness = Harness::with_entities();
    harness.click(id::MENU_VIEW);
    harness.dispatch(Message::ViewportTick);
    assert!(harness.shows("Focus Selection"));
}

#[test]
fn console_filters_hide_action_entries() {
    let mut harness = Harness::empty();
    show_page(&mut harness, PaneKind::Console);
    harness.dispatch(Message::AddEntity);
    assert!(harness.shows("Added Entity 1"));
    harness.dispatch(Message::ToggleConsoleFilter(
        crate::windows::log::LogLevel::Action,
    ));
    assert!(!harness.shows("Added Entity 1"));
    harness.dispatch(Message::ToggleConsoleFilter(
        crate::windows::log::LogLevel::Action,
    ));
    assert!(harness.shows("Added Entity 1"));
}

#[test]
fn menu_undo_and_redo_click() {
    let mut harness = Harness::empty();
    harness.rebase_history();
    harness.dispatch(Message::AddEntity);
    harness.click(id::MENU_EDIT);
    harness.click(id::UNDO);
    assert!(harness.app().entities.is_empty());
    harness.click(id::MENU_EDIT);
    harness.click(id::REDO);
    assert_eq!(harness.app().entities.len(), 1);
}

/// One scripted session covering the common editor loop, like a Playwright
/// spec: open, add, select, rename, move and delete.
#[test]
fn editor_workflow_end_to_end() {
    let mut harness = Harness::empty();

    harness.dispatch(Message::AddEntity);
    harness.dispatch(Message::AddEntity);
    assert_eq!(harness.app().entities.len(), 2);

    harness.click(id::entity_row(1));
    assert!(harness.shows("Entity 2"));

    harness.type_into(id::ENTITY_NAME, "Boss");
    assert!(harness.app().entities[1].name.contains("Boss"));

    harness.dispatch(Message::Inspector(panels::inspector::Edit::Delete));
    assert_eq!(harness.app().entities.len(), 1);
    assert!(harness.wait_for("Entity 1", 2));
}

/// Generates annotated panel images, Markdown pages and a JSON manifest.
///
/// ```bash
/// cargo test -p doppio-ui capture_ui_gallery -- --ignored --nocapture
/// ```
///
/// Env:
/// - `DOPPIO_DOCS_OUT` — output directory (default `target/doppio-ui-gallery`);
/// - `DOPPIO_DOCS_CAPTIONS` — JSON with `en`/`pt` descriptions
///   (default `caffeine-website/captures/ui-captions.json`). An agent can edit that file to
///   improve or translate the captions without touching code.
#[test]
fn material_open_message_loads_a_saved_material() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = std::env::temp_dir().join(format!(
        "doppio-material-open-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(&root).expect("temp dir");
    let path = root.join("open.mat");
    let surface = doppio_engine::Material {
        name: "Load Me".to_owned(),
        roughness: 0.33,
        ..Default::default()
    };
    harness
        .app
        .engine
        .as_ref()
        .expect("engine")
        .save_material(&path.to_string_lossy(), &surface)
        .expect("save material");
    harness.dispatch(Message::OpenMaterial(path.to_string_lossy().into_owned()));
    let document = harness.app().material.as_ref().expect("material");
    assert_eq!(document.surface.name, "Load Me");
    assert_eq!(document.surface.roughness, 0.33);
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn material_new_and_edits_update_the_document() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::Material(material::Edit::New));
    assert!(harness.app().material.is_some());
    harness.dispatch(Message::Material(material::Edit::Metallic(0.75)));
    harness.dispatch(Message::Material(material::Edit::Roughness(0.2)));
    harness.dispatch(Message::Material(material::Edit::Albedo(0, 0.4)));
    harness.dispatch(Message::Material(material::Edit::NormalDirectX(true)));
    let document = harness.app().material.as_ref().expect("material");
    assert_eq!(document.surface.metallic, 0.75);
    assert_eq!(document.surface.roughness, 0.2);
    assert_eq!(document.surface.albedo[0], 0.4);
    assert!(document.surface.normal_directx);
    assert!(document.dirty);

    harness.dispatch(Message::SetMaterialTexture {
        slot: material::TextureSlot::Albedo,
        path: "textures/wood.png".to_owned(),
    });
    let document = harness.app().material.as_ref().expect("material");
    assert_eq!(document.surface.albedo_map, "textures/wood.png");
    assert!(document.dirty);
}

#[test]
fn material_preview_drag_orbits_and_marks_the_preview_dirty() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::Material(material::Edit::New));
    if let Some(document) = harness.app.material.as_mut() {
        document.preview_dirty = false;
    }
    harness.dispatch(Message::Material(material::Edit::PreviewPressed));
    harness.dispatch(Message::Material(material::Edit::PreviewMoved(Point::new(
        10.0, 10.0,
    ))));
    harness.dispatch(Message::Material(material::Edit::PreviewMoved(Point::new(
        30.0, 20.0,
    ))));
    harness.dispatch(Message::Material(material::Edit::PreviewReleased));
    assert!((harness.app().material_preview_yaw - 10.0).abs() < 0.001);
    assert!((harness.app().material_preview_pitch - 17.0).abs() < 0.001);
    assert!(harness
        .app()
        .material
        .as_ref()
        .is_some_and(|document| document.preview_dirty));
}

#[test]
fn material_preset_keeps_the_name_and_maps() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.dispatch(Message::Material(material::Edit::New));
    if let Some(document) = harness.app.material.as_mut() {
        document.surface.albedo_map = "textures/keep.png".to_owned();
    }
    harness.dispatch(Message::Material(material::Edit::ApplyPreset(0)));
    let document = harness.app().material.as_ref().expect("material");
    assert_eq!(document.surface.albedo_map, "textures/keep.png");
    assert_eq!(document.surface.metallic, 1.0);
    assert_eq!(document.surface.name, "New Material");
}

#[test]
fn material_apply_assigns_the_selected_entity() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let filter_type = harness
        .app()
        .component_types
        .iter()
        .find(|kind| kind.name == "Mesh Filter")
        .map(|kind| kind.id)
        .expect("Mesh Filter schema");
    let entity = harness.app().entities[0].id;
    harness
        .app
        .engine
        .as_mut()
        .expect("engine")
        .add_component(entity, filter_type)
        .expect("add mesh filter");
    harness.app.selected = 0;
    harness.app.material = Some(MaterialDocument {
        surface: doppio_engine::Material::default(),
        asset_path: Some("materials/test.mat".to_owned()),
        file_path: None,
        dirty: false,
        preview_dirty: false,
        source: String::new(),
        generator_history: generators::GraphHistory::new(MaterialGraph::default()),
        generator_packing: generators::ChannelPacking::default(),
        generator_bits: 8,
        generator: MaterialGraph::default(),
        thumbnails: [None, None, None, None],
        source_thumbnail: None,
    });
    harness.dispatch(Message::Material(material::Edit::Apply));
    let assigned = harness
        .app()
        .engine
        .as_ref()
        .expect("engine")
        .entity_material(entity)
        .expect("assigned material");
    assert_eq!(assigned, "materials/test.mat");
    assert_eq!(harness.app().history.undo_name(), Some("Assign Material"));
}

#[test]
fn material_map_baker_writes_and_assigns_maps() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = std::env::temp_dir().join(format!(
        "doppio-material-baker-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(&root).expect("temp dir");
    let height = root.join("height.ppm");
    std::fs::write(
        &height,
        b"P6\n2 2\n255\n\x00\x00\x00\x40\x40\x40\x80\x80\x80\xc0\xc0\xc0",
    )
    .expect("height texture");
    let material_path = root.join("baked.mat");
    let surface = doppio_engine::Material::default();
    harness
        .app
        .engine
        .as_ref()
        .expect("engine")
        .save_material(&material_path.to_string_lossy(), &surface)
        .expect("save material");
    harness.app.material = Some(MaterialDocument {
        surface,
        asset_path: Some("baked.mat".to_owned()),
        file_path: Some(material_path.to_string_lossy().into_owned()),
        dirty: false,
        preview_dirty: false,
        source: height.to_string_lossy().into_owned(),
        generator_history: generators::GraphHistory::new(MaterialGraph::default()),
        generator_packing: generators::ChannelPacking::default(),
        generator_bits: 8,
        generator: MaterialGraph::default(),
        thumbnails: [None, None, None, None],
        source_thumbnail: None,
    });
    harness.dispatch(Message::Material(material::Edit::Generate(
        material::MapOperation::All,
    )));
    let document = harness.app().material.as_ref().expect("material");
    assert!(document.surface.normal_map.ends_with("baked_normal.png"));
    assert!(document.surface.orm_map.ends_with("baked_orm.png"));
    assert!(root.join("baked_normal.png").exists());
    assert!(root.join("baked_ao.png").exists());
    assert!(root.join("baked_orm.png").exists());
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn generator_adds_nodes_edits_params_and_rejects_cycles() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::Material(material::Edit::New));
    harness.dispatch(Message::Generator(material_generator::Edit::AddNode(
        generators::NodeKind::Checker {
            scale: 6.0,
            color_a: [0.8, 0.8, 0.8, 1.0],
            color_b: [0.1, 0.1, 0.1, 1.0],
        },
    )));
    let document = harness.app().material.as_ref().expect("material");
    assert_eq!(document.generator.nodes.len(), 3);
    let checker = document
        .generator
        .nodes
        .iter()
        .find(|node| matches!(node.kind, generators::NodeKind::Checker { .. }))
        .expect("checker")
        .id;
    let output = document.generator.output_node().expect("output");
    harness.dispatch(Message::Generator(material_generator::Edit::SetParam(
        checker,
        material_generator::Param::Scale(12.0),
    )));
    harness.dispatch(Message::Generator(material_generator::Edit::Connect {
        from: checker,
        output: 0,
        to: output,
        input: 0,
    }));
    let document = harness.app().material.as_ref().expect("material");
    assert!(document
        .generator
        .links
        .iter()
        .any(|link| { link.from == checker && link.to == output && link.input == 0 }));
    let mut graph = document.generator.clone();
    assert!(!graph.connect(output, 0, checker, 0));
    harness.dispatch(Message::Generator(material_generator::Edit::Select(Some(
        checker,
    ))));
    assert_eq!(harness.app().generator_selected, Some(checker));
}

#[test]
fn viewport_uv_pick_hits_the_selected_mesh() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let cube = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|prefab| prefab.key == "cube")
        .expect("cube prefab");
    harness.dispatch(Message::CreatePrefab(cube));
    let entity = harness
        .app()
        .entities
        .iter()
        .find(|entity| entity.name == "Cube")
        .expect("cube entity")
        .id;
    let focus = harness
        .app()
        .entities
        .iter()
        .find(|candidate| candidate.id == entity)
        .map(|entity| entity.position)
        .unwrap_or([0.0, 0.0, 0.0]);
    harness.app.camera = doppio_engine::Camera {
        yaw: 0.65,
        pitch: 0.35,
        distance: 8.0,
        focus,
    };
    harness.app.apply_camera();
    let (width, height) = harness.app().viewport_logical;
    let picked = harness
        .app()
        .engine
        .as_ref()
        .expect("engine")
        .pick_uv(entity, width * 0.5, height * 0.5, width, height)
        .expect("pick");
    let (u, v) = picked.expect("cube should be under the camera center");
    assert!((0.0..=1.0).contains(&u));
    assert!((0.0..=1.0).contains(&v));
}

#[test]
fn internal_asset_drag_assigns_materials_and_textures() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let cube = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|prefab| prefab.key == "cube")
        .expect("cube prefab");
    harness.dispatch(Message::CreatePrefab(cube));
    harness.app.assets.push(doppio_engine::AssetInfo {
        name: "ground.mat".to_owned(),
        relative_path: "vegetation-kit/materials/ground.mat".to_owned(),
        kind: "material".to_owned(),
        size_bytes: 0,
        is_directory: false,
    });
    harness.dispatch(Message::AssetPressed(0));
    assert_eq!(harness.app().asset_drag, Some(0));
    let target = harness.app().selected;
    harness.dispatch(Message::HierarchyRowRelease(target));
    assert_eq!(harness.app().asset_drag, None);
    assert_eq!(
        harness.app().selected_material.as_deref(),
        Some("vegetation-kit/materials/ground.mat")
    );

    harness.dispatch(Message::Material(material::Edit::New));
    harness.app.assets.push(doppio_engine::AssetInfo {
        name: "brick.png".to_owned(),
        relative_path: "vegetation-kit/brick.png".to_owned(),
        kind: "image".to_owned(),
        size_bytes: 0,
        is_directory: false,
    });
    harness.dispatch(Message::AssetPressed(1));
    harness.dispatch(Message::Material(material::Edit::DropTextureAsset(
        material::TextureSlot::Albedo,
    )));
    assert_eq!(
        harness
            .app()
            .material
            .as_ref()
            .map(|document| document.surface.albedo_map.as_str()),
        Some("vegetation-kit/brick.png")
    );
}

#[test]
fn material_picker_assigns_a_mat_to_the_selected_mesh() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let cube = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|prefab| prefab.key == "cube")
        .expect("cube prefab");
    harness.dispatch(Message::CreatePrefab(cube));
    harness.dispatch(Message::MaterialAssetPick(
        "vegetation-kit/materials/ground.mat".to_owned(),
    ));
    assert_eq!(
        harness.app().selected_material.as_deref(),
        Some("vegetation-kit/materials/ground.mat")
    );
}

#[test]
fn material_preview_shapes_and_async_readback_work() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let engine = harness.app.engine.as_mut().expect("engine");
    assert!(engine.capi_version() >= 2);
    let material = doppio_engine::Material::default();
    let mut output = vec![0u8; 64 * 64 * 4];
    engine
        .begin_material_preview(
            &material,
            64,
            20.0,
            12.0,
            doppio_engine::PreviewShape::Cube,
            "",
            1.0,
        )
        .expect("begin preview");
    let mut ready = false;
    for _ in 0..200 {
        match engine.poll_material_preview(&mut output) {
            Ok(doppio_engine::RenderState::Ready) => {
                ready = true;
                break;
            }
            Ok(doppio_engine::RenderState::Pending) => {
                std::thread::sleep(std::time::Duration::from_millis(5));
            }
            _ => break,
        }
    }
    assert!(ready, "async material preview should finish");
    assert!(output.chunks(4).any(|pixel| pixel[3] > 0));
    let sphere = engine
        .render_material_preview_ex(
            &material,
            64,
            20.0,
            12.0,
            doppio_engine::PreviewShape::Sphere,
            "",
            1.0,
        )
        .expect("sphere preview");
    assert_ne!(sphere, output, "cube and sphere previews should differ");
}

#[test]
fn material_preview_sun_and_shadows_change_the_render() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let engine = harness.app.engine.as_mut().expect("engine");
    let material = doppio_engine::Material {
        displacement_mode: 1,
        ..doppio_engine::Material::default()
    };
    let render = |engine: &mut doppio_engine::Engine, yaw: f32, shadows: bool| {
        engine.set_material_preview_light(yaw, 25.0, 3.0, shadows);
        engine
            .render_material_preview_ex(
                &material,
                128,
                0.0,
                25.0,
                doppio_engine::PreviewShape::Sphere,
                "",
                1.0,
            )
            .expect("preview")
    };
    let base = render(engine, 41.0, true);
    let moved = render(engine, 180.0, true);
    let unshadowed = render(engine, 180.0, false);
    assert_ne!(base, moved, "moving the sun must change the preview");
    assert_ne!(
        moved, unshadowed,
        "toggling shadows must change the preview"
    );
    // Sun behind the sphere: its shadow falls on the floor in front of it (bottom rows). The
    // shadow map used to be draw-order dependent and the floor erased the sphere's shadow.
    let bottom_mean = |pixels: &[u8]| {
        let rows = pixels[100 * 128 * 4..].chunks(4);
        let count = rows.len() as f32;
        rows.map(|px| f32::from(px[0]) + f32::from(px[1]) + f32::from(px[2]))
            .sum::<f32>()
            / count
    };
    assert!(
        bottom_mean(&moved) < bottom_mean(&unshadowed) * 0.7,
        "the sphere must cast a shadow on the floor ({} vs {})",
        bottom_mean(&moved),
        bottom_mean(&unshadowed)
    );
    if let Ok(dir) = std::env::var("DOPPIO_PREVIEW_DUMP") {
        for (name, pixels) in [
            ("base", &base),
            ("moved", &moved),
            ("noshadow", &unshadowed),
        ] {
            let mut ppm = b"P6\n128 128\n255\n".to_vec();
            for px in pixels.chunks(4) {
                ppm.extend_from_slice(&px[..3]);
            }
            let _ = std::fs::write(format!("{dir}/{name}.ppm"), ppm);
        }
    }
}

#[test]
fn preview_reloads_a_map_rewritten_in_place() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let engine = harness.app.engine.as_mut().expect("engine");
    let dir = std::env::temp_dir().join(format!("doppio-rewrite-{}", std::process::id()));
    std::fs::create_dir_all(&dir).expect("dir");
    let path = dir.join("albedo.png").to_string_lossy().to_string();
    let solid = |rgb: [u8; 3]| -> Vec<u8> {
        (0..16 * 16)
            .flat_map(|_| [rgb[0], rgb[1], rgb[2], 255])
            .collect()
    };
    let material = doppio_engine::Material {
        albedo_map: path.clone(),
        ..doppio_engine::Material::default()
    };
    let render = |engine: &mut doppio_engine::Engine| {
        engine
            .render_material_preview_ex(
                &material,
                64,
                0.0,
                20.0,
                doppio_engine::PreviewShape::Plane,
                "",
                1.0,
            )
            .expect("preview")
    };
    engine
        .save_texture_rgba(&path, 16, 16, &solid([230, 20, 20]))
        .expect("write red");
    let red = render(engine);
    // Same path, new content, and make sure the modification time moves.
    std::thread::sleep(std::time::Duration::from_millis(1100));
    engine
        .save_texture_rgba(&path, 16, 16, &solid([20, 20, 230]))
        .expect("write blue");
    let blue = render(engine);
    let centre = |pixels: &[u8]| {
        let at = (32 * 64 + 32) * 4;
        (pixels[at], pixels[at + 2])
    };
    let (red_r, red_b) = centre(&red);
    let (blue_r, blue_b) = centre(&blue);
    assert!(red_r > red_b, "first render should be reddish");
    assert!(blue_b > blue_r, "the rewritten map must be reloaded");
    let _ = std::fs::remove_dir_all(dir);
}

#[test]
fn mesh_map_bakes_write_uv_channels() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let cube = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|prefab| prefab.key == "cube")
        .expect("cube prefab");
    harness.dispatch(Message::CreatePrefab(cube));
    let entity = harness
        .app()
        .entities
        .iter()
        .find(|entity| entity.name == "Cube")
        .expect("cube")
        .id;
    let root = std::env::temp_dir().join(format!(
        "doppio-meshmap-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(&root).expect("temp dir");
    let engine = harness.app.engine.as_ref().expect("engine");
    for (kind, name) in [
        (doppio_engine::MeshMapKind::Normal, "normal"),
        (doppio_engine::MeshMapKind::Position, "position"),
        (doppio_engine::MeshMapKind::ObjectId, "id"),
    ] {
        let path = root.join(format!("{name}.png"));
        engine
            .bake_mesh_map(entity, kind, 64, &path.to_string_lossy())
            .expect("bake mesh map");
        assert!(path.exists());
        let data = crate::uikit::generators::decode_texture(&path.to_string_lossy())
            .expect("decode mesh map");
        assert_eq!((data.width, data.height), (64, 64));
        assert!(data.rgba.chunks(4).any(|pixel| pixel[3] > 0));
    }
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn texture_set_paints_undoes_saves_and_reopens() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::TextureSet(texture_set_panel::Edit::New));
    assert!(harness.app().texture_set.is_some());
    assert!(harness.app().texture_set_preview.is_some());
    harness.dispatch(Message::TextureSet(texture_set_panel::Edit::PaintStart([
        0.5, 0.5,
    ])));
    harness.dispatch(Message::TextureSet(texture_set_panel::Edit::PaintMove([
        0.55, 0.55,
    ])));
    harness.dispatch(Message::TextureSet(texture_set_panel::Edit::PaintEnd));
    assert!(harness.app().texture_set_dirty);
    assert!(harness.app().texture_set_history.can_undo());
    let painted = harness
        .app()
        .texture_set
        .as_ref()
        .expect("set")
        .composite(1001, "baseColor");
    assert!(painted.chunks(4).any(|pixel| pixel[3] > 0));
    harness.dispatch(Message::TextureSet(texture_set_panel::Edit::Undo));
    let undone = harness
        .app()
        .texture_set
        .as_ref()
        .expect("set")
        .composite(1001, "baseColor");
    assert!(undone.iter().all(|value| *value == 0));

    let root = std::env::temp_dir().join(format!(
        "doppio-texset-ui-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(&root).expect("temp dir");
    let path = root.join("painted.texset");
    harness.app.texture_set_path = Some(path.clone());
    harness.dispatch(Message::TextureSet(texture_set_panel::Edit::Save));
    assert!(path.exists());
    assert!(!harness.app().texture_set_dirty);
    harness.dispatch(Message::TextureSet(texture_set_panel::Edit::New));
    harness.dispatch(Message::OpenTextureSet(path.to_string_lossy().into_owned()));
    assert_eq!(
        harness
            .app()
            .texture_set
            .as_ref()
            .map(|set| set.name.as_str()),
        Some("Texture Set")
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn materialcraft_portable_round_trip_through_the_engine() {
    let Some(harness) = engine_harness() else {
        return;
    };
    let root = std::env::temp_dir().join(format!(
        "doppio-mcmat-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(&root).expect("temp dir");
    let engine = harness.app.engine.as_ref().expect("engine");
    let graph = MaterialGraph {
        resolution: 64,
        ..MaterialGraph::default()
    };
    let archive = root.join("brick.mcmat");
    let exported =
        crate::uikit::materialcraft_io::export_portable(engine, &graph, "Brick", &archive)
            .expect("export");
    assert!(archive.exists());
    assert_eq!(exported.maps, 6);

    let imported = crate::uikit::materialcraft_io::import_portable(
        engine,
        &archive,
        Some(&root.join("imported")),
    )
    .expect("import");
    assert!(imported.material_path.exists());
    assert!(
        crate::uikit::recipe::recipe_for_product(&imported.material_path).is_some(),
        "a MaterialCraft archive carries its graph, so the import keeps a recipe"
    );
    assert!(imported
        .source_path
        .as_ref()
        .is_some_and(|path| path.exists()));
    assert!(imported.warnings.is_empty());
    assert_eq!(imported.graph.nodes.len(), graph.nodes.len());
    assert_eq!(imported.graph.resolution, 64);
    assert!(imported.surface.orm_map.contains("brick_orm.png"));
    assert!(imported.surface.albedo_map.contains("brick_basecolor.png"));
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn generator_live_preview_updates_after_each_edit() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.dispatch(Message::Material(material::Edit::New));
    harness.dispatch(Message::OpenGenerator);
    let mut first = 0;
    for _ in 0..300 {
        harness.dispatch(Message::ViewportTick);
        first = harness.app().generator_preview_revision;
        if first > 0 {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(5));
    }
    assert!(first > 0, "live preview should produce an image");
    assert!(
        !harness.app().generator_node_previews.is_empty(),
        "the live preview should also fill per-node thumbnails"
    );
    let node = harness
        .app()
        .material
        .as_ref()
        .and_then(|document| document.generator.nodes.first())
        .map(|node| node.id)
        .expect("noise node");
    harness.dispatch(Message::Generator(material_generator::Edit::SetParam(
        node,
        material_generator::Param::Scale(14.0),
    )));
    let mut second = first;
    for _ in 0..300 {
        harness.dispatch(Message::ViewportTick);
        second = harness.app().generator_preview_revision;
        if second > first {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(5));
    }
    assert!(
        second > first,
        "editing the graph should refresh the live preview"
    );
}

#[test]
fn generator_bake_writes_base_normal_and_orm() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = std::env::temp_dir().join(format!(
        "doppio-generator-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(&root).expect("temp dir");
    let material_path = root.join("generated.mat");
    let surface = doppio_engine::Material::default();
    harness
        .app
        .engine
        .as_ref()
        .expect("engine")
        .save_material(&material_path.to_string_lossy(), &surface)
        .expect("save material");
    harness.app.material = Some(MaterialDocument {
        surface,
        asset_path: Some("generated.mat".to_owned()),
        file_path: Some(material_path.to_string_lossy().into_owned()),
        dirty: false,
        preview_dirty: false,
        source: String::new(),
        generator_history: generators::GraphHistory::new(MaterialGraph::default()),
        generator_packing: generators::ChannelPacking::default(),
        generator_bits: 8,
        generator: MaterialGraph::default(),
        thumbnails: [None, None, None, None],
        source_thumbnail: None,
    });
    if let Some(document) = harness.app.material.as_mut() {
        document.generator.resolution = 128;
    }
    harness.dispatch(Message::Generator(material_generator::Edit::Bake));
    assert!(harness.app().generator_bake.is_some());
    wait_for_bake(&mut harness);
    let document = harness.app().material.as_ref().expect("material");
    assert!(document
        .surface
        .albedo_map
        .ends_with("generated_gen_base_8.png"));
    assert!(document
        .surface
        .normal_map
        .ends_with("generated_gen_normal.png"));
    assert!(document
        .surface
        .orm_map
        .ends_with("generated_gen_packed_8.png"));
    assert!(root.join("generated_gen_base_8.png").exists());
    assert!(root.join("generated_gen_normal.png").exists());
    assert!(root.join("generated_gen_packed_8.png").exists());
    assert!(harness.app().generator_preview.is_some());
    assert_eq!(
        harness.app().generator_status.as_deref(),
        Some("Baked 128×128 · ORM (glTF/Unreal/Godot) · 8-bit")
    );

    if let Some(document) = harness.app.material.as_mut() {
        document.generator_bits = 16;
    }
    harness.dispatch(Message::Generator(material_generator::Edit::Bake));
    wait_for_bake(&mut harness);
    let document = harness.app().material.as_ref().expect("material");
    assert!(document
        .surface
        .albedo_map
        .ends_with("generated_gen_base_16.png"));
    assert!(root.join("generated_gen_base_16.png").exists());
    assert!(root.join("generated_gen_packed_16.png").exists());

    harness.dispatch(Message::Material(material::Edit::Apply));
    let saved = harness
        .app()
        .engine
        .as_ref()
        .expect("engine")
        .load_material(&material_path.to_string_lossy())
        .expect("saved material");
    assert!(saved.albedo_map.ends_with("generated_gen_base_16.png"));
    assert!(saved.normal_map.ends_with("generated_gen_normal.png"));
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn generator_bake_decodes_image_nodes_off_the_ui_thread() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = std::env::temp_dir().join(format!(
        "doppio-generator-image-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(&root).expect("temp dir");
    let texture_path = root.join("swatch.png");
    let texture = image::RgbaImage::from_raw(
        2,
        2,
        vec![
            255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
        ],
    )
    .expect("texture");
    texture.save(&texture_path).expect("save texture");
    let material_path = root.join("image.mat");
    let surface = doppio_engine::Material::default();
    harness
        .app
        .engine
        .as_ref()
        .expect("engine")
        .save_material(&material_path.to_string_lossy(), &surface)
        .expect("save material");
    let mut graph = MaterialGraph {
        resolution: 64,
        ..MaterialGraph::default()
    };
    graph.nodes[0].kind = generators::NodeKind::Image {
        path: texture_path.to_string_lossy().into_owned(),
        projection: generators::Projection::Uv,
        tiling: [1.0, 1.0],
        offset: [0.0, 0.0],
        filter: generators::ImageFilter::Nearest,
        wrap: generators::WrapMode::Repeat,
    };
    harness.app.material = Some(MaterialDocument {
        surface,
        asset_path: Some("image.mat".to_owned()),
        file_path: Some(material_path.to_string_lossy().into_owned()),
        dirty: false,
        preview_dirty: false,
        source: String::new(),
        generator_history: generators::GraphHistory::new(graph.clone()),
        generator_packing: generators::ChannelPacking::default(),
        generator_bits: 8,
        generator: graph,
        thumbnails: [None, None, None, None],
        source_thumbnail: None,
    });
    harness.dispatch(Message::Generator(material_generator::Edit::Bake));
    wait_for_bake(&mut harness);
    let baked = image::open(root.join("image_gen_base_8.png"))
        .expect("baked image")
        .to_rgba8();
    assert_eq!(baked.get_pixel(0, 0).0, [255, 0, 0, 255]);
    assert_eq!(baked.get_pixel(63, 0).0, [0, 255, 0, 255]);
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn generator_bake_cancels_from_the_worker() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = std::env::temp_dir().join(format!(
        "doppio-generator-cancel-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(&root).expect("temp dir");
    let material_path = root.join("cancel.mat");
    let surface = doppio_engine::Material::default();
    harness
        .app
        .engine
        .as_ref()
        .expect("engine")
        .save_material(&material_path.to_string_lossy(), &surface)
        .expect("save material");
    let mut graph = MaterialGraph {
        resolution: 512,
        ..MaterialGraph::default()
    };
    let output = graph.output_node().expect("output");
    let noise = graph.nodes[0].id;
    let blur = graph.add_node_auto(generators::NodeKind::Blur {
        radius: 32.0,
        wrap: generators::WrapMode::Repeat,
    });
    assert!(graph.connect(noise, 0, blur, 0));
    assert!(graph.connect(blur, 0, output, 0));
    harness.app.material = Some(MaterialDocument {
        surface,
        asset_path: Some("cancel.mat".to_owned()),
        file_path: Some(material_path.to_string_lossy().into_owned()),
        dirty: false,
        preview_dirty: false,
        source: String::new(),
        generator_history: generators::GraphHistory::new(graph.clone()),
        generator_packing: generators::ChannelPacking::default(),
        generator_bits: 8,
        generator: graph,
        thumbnails: [None, None, None, None],
        source_thumbnail: None,
    });
    harness.dispatch(Message::Generator(material_generator::Edit::Bake));
    assert!(harness.app().generator_bake.is_some());
    harness.dispatch(Message::Generator(material_generator::Edit::CancelBake));
    assert_eq!(
        harness.app().generator_status.as_deref(),
        Some("Cancelling bake…")
    );
    for _ in 0..600 {
        if harness.app().generator_bake.is_none() {
            break;
        }
        harness.dispatch(Message::ViewportTick);
        std::thread::sleep(std::time::Duration::from_millis(5));
    }
    assert!(harness.app().generator_bake.is_none());
    assert_eq!(
        harness.app().generator_status.as_deref(),
        Some("Bake cancelled")
    );
    assert!(!root.join("cancel_gen_base_8.png").exists());
    std::fs::remove_dir_all(root).ok();
}

fn wait_for_bake(harness: &mut Harness) {
    for _ in 0..600 {
        if harness.app().generator_bake.is_none() {
            return;
        }
        harness.dispatch(Message::ViewportTick);
        std::thread::sleep(std::time::Duration::from_millis(5));
    }
    panic!("generator bake did not finish");
}

#[test]
fn generator_palette_groups_collapse_with_the_tree_view() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::Material(material::Edit::New));
    harness.dispatch(Message::OpenGenerator);
    assert!(harness.has(Id::from(id::generator_group("Filters"))));
    assert!(harness.has(Id::from(id::generator_add("blur"))));
    harness.click(Id::from(id::generator_group("Filters")));
    assert!(!harness.has(Id::from(id::generator_add("blur"))));
    harness.click(Id::from(id::generator_group("Filters")));
    assert!(harness.has(Id::from(id::generator_add("blur"))));
}

#[test]
fn generator_image_picker_packing_and_bits_edit_the_document() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::Material(material::Edit::New));
    harness.dispatch(Message::OpenGenerator);
    harness.dispatch(Message::Generator(material_generator::Edit::AddNode(
        generators::NodeKind::Image {
            path: String::new(),
            projection: generators::Projection::Uv,
            tiling: [1.0, 1.0],
            offset: [0.0, 0.0],
            filter: generators::ImageFilter::Linear,
            wrap: generators::WrapMode::Repeat,
        },
    )));
    let node = harness.app().generator_selected.expect("image node");
    harness.dispatch(Message::Generator(
        material_generator::Edit::OpenTexturePicker(node),
    ));
    assert_eq!(harness.app().generator_texture_target, Some(node));
    harness.dispatch(Message::GeneratorTexturePick(
        "textures/brick set/albedo.png".to_owned(),
    ));
    assert_eq!(harness.app().generator_texture_target, None);
    let document = harness.app().material.as_ref().expect("material");
    assert!(matches!(
        document.generator.node(node).map(|node| &node.kind),
        Some(generators::NodeKind::Image { path, .. }) if path == "textures/brick set/albedo.png"
    ));

    harness.dispatch(Message::Generator(
        material_generator::Edit::SetPackingPreset(1),
    ));
    harness.dispatch(Message::Generator(
        material_generator::Edit::SetPackingChannel(0, 4),
    ));
    harness.dispatch(Message::Generator(material_generator::Edit::SetBits(16)));
    let document = harness.app().material.as_ref().expect("material");
    assert_eq!(
        document.generator_packing.preset,
        generators::PackingPreset::Custom
    );
    assert_eq!(
        document.generator_packing.channels[0],
        generators::PackSource::from_index(4)
    );
    assert_eq!(document.generator_bits, 16);
    assert!(document
        .generator_history
        .undo_name()
        .is_some_and(|name| name.contains("image") || name == "Set image texture"));
}

#[test]
fn packing_user_presets_save_and_delete_through_messages() {
    let mut harness = Harness::empty();
    let root = std::env::temp_dir().join(format!(
        "doppio-packing-message-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ));
    harness.app.packing_store_path = Some(root.join("presets.txt"));
    harness.dispatch(Message::Material(material::Edit::New));
    harness.dispatch(Message::OpenGenerator);
    harness.dispatch(Message::Generator(
        material_generator::Edit::SetPackingPresetName("Studio".to_owned()),
    ));
    harness.dispatch(Message::Generator(
        material_generator::Edit::SetPackingChannel(0, 4),
    ));
    harness.dispatch(Message::Generator(
        material_generator::Edit::SavePackingPreset,
    ));
    let path = harness.app.packing_store_path.clone().expect("path");
    let presets = generators::load_user_packing_presets(&path);
    assert_eq!(presets.len(), 1);
    assert_eq!(presets[0].name, "Studio");
    assert_eq!(
        presets[0].channels[0],
        generators::PackSource::from_index(4)
    );

    harness.dispatch(Message::Generator(
        material_generator::Edit::DeletePackingPreset,
    ));
    assert!(generators::load_user_packing_presets(&path).is_empty());
    harness.app.packing_user_presets.clear();
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn generator_scene_shortcut_routes_to_graph_history() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::Material(material::Edit::New));
    harness.dispatch(Message::OpenGenerator);
    harness.dispatch(Message::Generator(material_generator::Edit::AddNode(
        generators::NodeKind::Checker {
            scale: 4.0,
            color_a: [1.0; 4],
            color_b: [0.0; 4],
        },
    )));
    assert_eq!(
        harness
            .app()
            .material
            .as_ref()
            .expect("material")
            .generator
            .nodes
            .len(),
        3
    );
    harness.dispatch(Message::Undo);
    assert_eq!(
        harness
            .app()
            .material
            .as_ref()
            .expect("material")
            .generator
            .nodes
            .len(),
        2
    );
    harness.dispatch(Message::Redo);
    assert_eq!(
        harness
            .app()
            .material
            .as_ref()
            .expect("material")
            .generator
            .nodes
            .len(),
        3
    );
}

#[test]
fn detaching_and_reconnecting_a_link_is_one_history_step() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::Material(material::Edit::New));
    harness.dispatch(Message::OpenGenerator);
    harness.dispatch(Message::Generator(material_generator::Edit::AddNode(
        generators::NodeKind::Checker {
            scale: 4.0,
            color_a: [1.0; 4],
            color_b: [0.0; 4],
        },
    )));
    let (checker, output) = {
        let document = harness.app().material.as_ref().expect("material");
        (
            document
                .generator
                .nodes
                .iter()
                .find(|node| matches!(node.kind, generators::NodeKind::Checker { .. }))
                .expect("checker")
                .id,
            document.generator.output_node().expect("output"),
        )
    };
    harness.dispatch(Message::Generator(material_generator::Edit::Connect {
        from: checker,
        output: 0,
        to: output,
        input: 0,
    }));
    let steps = harness
        .app()
        .material
        .as_ref()
        .expect("material")
        .generator_history
        .len();
    harness.dispatch(Message::Generator(material_generator::Edit::DetachLink(
        output, 0,
    )));
    harness.dispatch(Message::Generator(material_generator::Edit::Connect {
        from: checker,
        output: 0,
        to: output,
        input: 1,
    }));
    let document = harness.app().material.as_ref().expect("material");
    assert_eq!(document.generator_history.len(), steps + 1);
    assert!(document
        .generator
        .links
        .iter()
        .any(|link| link.to == output && link.input == 1));

    harness.dispatch(Message::Generator(material_generator::Edit::Undo));
    let document = harness.app().material.as_ref().expect("material");
    assert!(document
        .generator
        .links
        .iter()
        .any(|link| link.to == output && link.input == 0));
    harness.dispatch(Message::Generator(material_generator::Edit::Redo));
    let document = harness.app().material.as_ref().expect("material");
    assert!(document
        .generator
        .links
        .iter()
        .any(|link| link.to == output && link.input == 1));
}

#[test]
fn generator_undo_redo_links_dots_copy_paste_and_snap() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::Material(material::Edit::New));
    harness.dispatch(Message::Generator(material_generator::Edit::AddNode(
        generators::NodeKind::Checker {
            scale: 6.0,
            color_a: [0.8, 0.8, 0.8, 1.0],
            color_b: [0.1, 0.1, 0.1, 1.0],
        },
    )));
    let initial = harness
        .app()
        .material
        .as_ref()
        .expect("material")
        .generator
        .nodes
        .len();
    assert_eq!(initial, 3);

    harness.dispatch(Message::Generator(material_generator::Edit::Undo));
    assert_eq!(
        harness
            .app()
            .material
            .as_ref()
            .expect("material")
            .generator
            .nodes
            .len(),
        2
    );
    harness.dispatch(Message::Generator(material_generator::Edit::Redo));
    assert_eq!(
        harness
            .app()
            .material
            .as_ref()
            .expect("material")
            .generator
            .nodes
            .len(),
        3
    );

    let (checker, output) = {
        let document = harness.app().material.as_ref().expect("material");
        (
            document
                .generator
                .nodes
                .iter()
                .find(|node| matches!(node.kind, generators::NodeKind::Checker { .. }))
                .expect("checker")
                .id,
            document.generator.output_node().expect("output"),
        )
    };
    harness.dispatch(Message::Generator(material_generator::Edit::Connect {
        from: checker,
        output: 0,
        to: output,
        input: 0,
    }));
    assert_eq!(
        harness
            .app()
            .material
            .as_ref()
            .expect("material")
            .generator
            .links
            .len(),
        1
    );

    harness.dispatch(Message::Generator(material_generator::Edit::DeleteLink(
        output, 0,
    )));
    assert!(harness
        .app()
        .material
        .as_ref()
        .expect("material")
        .generator
        .links
        .is_empty());
    harness.dispatch(Message::Generator(material_generator::Edit::Undo));
    assert_eq!(
        harness
            .app()
            .material
            .as_ref()
            .expect("material")
            .generator
            .links
            .len(),
        1
    );

    harness.dispatch(Message::Generator(material_generator::Edit::InsertDot(
        output,
        0,
        iced::Point::new(120.0, 80.0),
    )));
    let document = harness.app().material.as_ref().expect("material");
    assert_eq!(document.generator.links.len(), 2);
    let dot = document
        .generator
        .nodes
        .iter()
        .find(|node| matches!(node.kind, generators::NodeKind::Dot))
        .expect("dot")
        .id;
    assert!(document
        .generator
        .links
        .iter()
        .any(|link| link.from == checker && link.to == dot && link.input == 0));
    assert!(document
        .generator
        .links
        .iter()
        .any(|link| link.from == dot && link.to == output && link.input == 0));

    harness.dispatch(Message::Generator(material_generator::Edit::Select(Some(
        checker,
    ))));
    let before_paste = harness
        .app()
        .material
        .as_ref()
        .expect("material")
        .generator
        .nodes
        .len();
    harness.dispatch(Message::Generator(material_generator::Edit::Copy));
    harness.dispatch(Message::Generator(material_generator::Edit::Paste));
    let pasted = harness
        .app()
        .material
        .as_ref()
        .expect("material")
        .generator
        .nodes
        .len();
    assert_eq!(pasted, before_paste + 1);

    let before_move = harness
        .app()
        .material
        .as_ref()
        .expect("material")
        .generator
        .node(checker)
        .expect("checker")
        .position;
    harness.dispatch(Message::Generator(material_generator::Edit::MoveNode(
        checker,
        iced::Vector::new(5.0, 3.0),
    )));
    let position = harness
        .app()
        .material
        .as_ref()
        .expect("material")
        .generator
        .node(checker)
        .expect("checker")
        .position;
    // Snapping is applied by the node canvas against the drag origin, so the
    // model applies the delta it is given.
    assert_eq!(position.x, before_move.x + 5.0);
    assert_eq!(position.y, before_move.y + 3.0);
}

fn test_temp_root(label: &str) -> std::path::PathBuf {
    std::env::temp_dir().join(format!(
        "doppio-{label}-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ))
}

#[test]
fn project_manager_creates_a_project_and_reopens_it() {
    let root = test_temp_root("project-create");
    std::fs::create_dir_all(&root).expect("temp dir");
    let mut harness = Harness::empty();
    harness.app.project_recent_file = root.join("recent.txt");
    harness.app.project.location = root.join("projects").display().to_string();

    harness.dispatch(Message::Project(project_manager::Edit::Open));
    assert!(harness.app.project.open);
    assert!(harness.has(iced::widget::Id::from(id::PROJECT_MANAGER)));
    assert!(harness.has(iced::widget::Id::from(id::PROJECT_TAB_CREATE)));

    harness.type_into(id::PROJECT_NAME, "Demo");
    assert_eq!(harness.app.project.name, "Demo");
    harness.click(id::PROJECT_CREATE);

    assert!(
        !harness.app.project.open,
        "creating a project should close the manager"
    );
    let active = harness
        .app
        .project
        .active
        .as_ref()
        .expect("created project should become active")
        .clone();
    assert_eq!(active.name, "Demo");
    assert_eq!(active.template, "3D");
    assert!(active.root.join(project::PROJECT_FILE).exists());
    assert!(active.root.join("Assets/Raw/Textures").is_dir());
    assert!(active.root.join("Scenes").is_dir());
    let recents = project::recent_projects_at(&harness.app.project_recent_file);
    assert_eq!(recents.len(), 1);
    assert_eq!(recents[0].name, "Demo");

    harness.dispatch(Message::Project(project_manager::Edit::Open));
    harness.dispatch(Message::Project(project_manager::Edit::Tab(
        project_manager::Tab::Recent,
    )));
    assert_eq!(harness.app.project.recent.len(), 1);
    harness.click(id::project_recent_row(0));
    assert_eq!(harness.app.project.selected_recent, Some(0));
    harness.click(id::PROJECT_RECENT_OPEN);
    assert_eq!(
        harness
            .app
            .project
            .active
            .as_ref()
            .map(|project| project.name.as_str()),
        Some("Demo")
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn project_manager_browse_finds_nested_projects() {
    let root = test_temp_root("project-browse");
    let nested = root.join("games").join("nested");
    std::fs::create_dir_all(&nested).expect("nested dir");
    project::create_project(&nested, "Nested", "2D").expect("create project");
    let mut harness = Harness::empty();
    harness.app.project_recent_file = root.join("recent.txt");

    harness.dispatch(Message::Project(project_manager::Edit::Open));
    harness.dispatch(Message::Project(project_manager::Edit::Tab(
        project_manager::Tab::Browse,
    )));
    harness.dispatch(Message::Project(project_manager::Edit::BrowsePath(
        root.join("games").display().to_string(),
    )));
    harness.click(id::PROJECT_BROWSE_SCAN);
    assert_eq!(harness.app.project.browse_results.len(), 1);
    assert_eq!(harness.app.project.browse_results[0].name, "Nested");

    harness.click(id::project_browse_row(0));
    assert_eq!(harness.app.project.selected_browse, Some(0));
    harness.click(id::PROJECT_BROWSE_OPEN);
    assert_eq!(
        harness
            .app
            .project
            .active
            .as_ref()
            .map(|project| project.name.as_str()),
        Some("Nested")
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn project_launch_argument_accepts_root_and_manifest() {
    let root = test_temp_root("project-launch-arg");
    std::fs::create_dir_all(&root).expect("temp dir");
    let project = project::create_project(&root, "LaunchMe", "3D").expect("create project");
    assert_eq!(
        projectloader::project_launch_path(&project.root.display().to_string()),
        Some(project.root.clone())
    );
    let manifest = project.root.join(project::PROJECT_FILE);
    assert_eq!(
        projectloader::project_launch_path(&manifest.display().to_string()),
        Some(manifest)
    );
    assert_eq!(
        projectloader::project_launch_path("/tmp/not-a-project.caf"),
        None
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn saving_a_material_inside_a_project_makes_it_an_asset() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("project-material");
    std::fs::create_dir_all(&root).expect("temp dir");
    let project = project::create_project(&root, "MaterialProject", "3D").expect("create project");
    harness.app.project_recent_file = root.join("recent.txt");
    harness.app.open_project_path(&project.root);

    harness.dispatch(Message::Material(material::Edit::New));
    harness.dispatch(Message::Material(material::Edit::Name("brick".to_owned())));
    harness.dispatch(Message::Material(material::Edit::Save));
    let document = harness.app.material.as_ref().expect("material");
    let asset_path = document.asset_path.clone().expect("material should save");
    assert!(asset_path.ends_with("brick.mat"));
    assert!(
        project.root.join("Assets/Raw").join(&asset_path).is_file(),
        "saved material should live under the project asset root: {asset_path}"
    );
    assert!(
        harness
            .app
            .assets
            .iter()
            .any(|asset| asset.relative_path == asset_path),
        "saved material should appear in the Assets pane"
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn opening_a_project_creates_its_scene_and_asset_root() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("project-engine");
    std::fs::create_dir_all(&root).expect("temp dir");
    let project = project::create_project(&root, "EngineDemo", "3D").expect("create project");
    std::fs::write(project.asset_root().join("Notes.txt"), b"hello").expect("asset file");
    harness.app.project_recent_file = root.join("recent.txt");

    harness.dispatch(Message::Project(project_manager::Edit::OpenPath(
        project.root.display().to_string(),
    )));

    assert_eq!(
        harness
            .app
            .project
            .active
            .as_ref()
            .map(|project| project.name.as_str()),
        Some("EngineDemo")
    );
    assert!(
        harness.app.entities.is_empty(),
        "new project should start empty"
    );
    let scene = project.root.join("Scenes").join("main.caf");
    assert!(scene.is_file(), "new project should get a default scene");
    let engine = harness.app.engine.as_ref().expect("engine");
    assert!(engine
        .scene_path()
        .is_some_and(|path| path.ends_with("main.caf")));
    assert!(engine
        .assets()
        .iter()
        .any(|asset| asset.name == "Notes.txt"));
    let reloaded = project::load_project(&project.root).expect("reload project");
    assert_eq!(reloaded.last_scene, "Scenes/main.caf");

    // Saving a renamed scene updates last_scene in the manifest.
    let other = project.root.join("Scenes").join("other.caf");
    harness
        .app
        .engine
        .as_mut()
        .expect("engine")
        .save_scene(other.to_string_lossy().as_ref())
        .expect("save other scene");
    harness.dispatch(Message::SaveScene);
    let reloaded = project::load_project(&project.root).expect("reload project");
    assert_eq!(reloaded.last_scene, "Scenes/other.caf");
    assert_eq!(
        harness
            .app
            .project
            .active
            .as_ref()
            .map(|project| project.last_scene.as_str()),
        Some("Scenes/other.caf")
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn script_editor_opens_saves_and_attaches_to_selected_entity() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("script-editor");
    std::fs::create_dir_all(&root).expect("temp dir");
    let path = root.join("player.dyd");
    std::fs::write(&path, "function onUpdate(entity, dt) {}\n").expect("script");

    harness
        .app
        .script
        .open(&path, Some("scripts/player.dyd".to_owned()))
        .expect("open script");
    assert_eq!(
        harness
            .app
            .script
            .current()
            .map(|document| document.title.as_str()),
        Some("player.dyd")
    );
    assert!(harness.has(iced::widget::Id::from(id::PANE_TAB_SCRIPT)));

    harness.app.script.update(iced_code_editor::Message::Paste(
        "function onUpdate(entity, dt) {}\n// edited\n".to_owned(),
    ));
    assert!(harness
        .app
        .script
        .current()
        .is_some_and(|document| document.is_dirty()));
    harness.dispatch(Message::ScriptSave);
    let saved = std::fs::read_to_string(&path).expect("read script");
    assert!(
        saved.contains("// edited"),
        "saved script should include the edit"
    );
    assert!(harness
        .app
        .script
        .current()
        .is_some_and(|document| !document.is_dirty()));

    harness.dispatch(Message::ScriptAttach);
    let entity = harness
        .app
        .entities
        .get(harness.app.selected)
        .map(|entity| entity.id)
        .expect("selected entity");
    let engine = harness.app.engine.as_ref().expect("engine");
    let components = engine.entity_component_data(entity);
    let script = components
        .iter()
        .find(|component| component.info.name == "Script")
        .expect("script component");
    assert!(matches!(
        script.values.first(),
        Some(doppio_engine::FieldValue::String(path)) if path == "scripts/player.dyd"
    ));
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn dryad_editor_opens_saves_runs_and_attaches_a_graph() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("dryad-editor");
    std::fs::create_dir_all(&root).expect("temp dir");
    let path = root.join("demo.dyd");
    let mut graph = dryad::Graph::new("demo.dyd");
    graph.add_node(dryad::NodeKind::OnUpdate, 40.0, 40.0);
    graph
        .write_sibling_files(&path)
        .expect("write graph source");

    harness
        .app
        .dryad
        .open(&path, Some("scripts/demo.dyd".to_owned()))
        .expect("open graph");
    harness.dispatch(Message::OpenDryadNodes);
    assert!(harness.has(iced::widget::Id::from(id::PANE_DRYAD_NODES)));
    assert!(harness.has(iced::widget::Id::from(id::DRYAD_CANVAS)));

    harness.dispatch(Message::DryadAddMotion);
    assert!(harness.app().dryad.dirty);
    harness.dispatch(Message::DryadSave);
    assert!(!harness.app().dryad.dirty);
    let source = std::fs::read_to_string(&path).expect("read emitted script");
    assert!(
        source.contains("// @node"),
        "emitter should write node markers"
    );
    assert!(path.with_extension("node.json").is_file());

    harness.dispatch(Message::DryadAttach);
    let entity = harness
        .app
        .entities
        .get(harness.app.selected)
        .map(|entity| entity.id)
        .expect("selected entity");
    let engine = harness.app.engine.as_ref().expect("engine");
    let components = engine.entity_component_data(entity);
    let script = components
        .iter()
        .find(|component| component.info.name == "Script")
        .expect("script component");
    assert!(matches!(
        script.values.first(),
        Some(doppio_engine::FieldValue::String(path)) if path == "scripts/demo.dyd"
    ));

    let before = engine.position(entity).expect("position before preview")[1];
    harness.dispatch(Message::DryadRun);
    let after = harness
        .app
        .engine
        .as_ref()
        .expect("engine")
        .position(entity)
        .expect("position after preview")[1];
    assert!(after > before, "Dryad preview should translate the entity");
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn asset_double_click_opens_a_script_in_the_editor() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("script-asset");
    std::fs::create_dir_all(&root).expect("temp dir");
    let path = root.join("enemy.lua");
    std::fs::write(&path, "print('enemy')\n").expect("script");
    harness.app.assets.push(doppio_engine::AssetInfo {
        name: "enemy.lua".to_owned(),
        relative_path: "Scripts/enemy.lua".to_owned(),
        kind: "script".to_owned(),
        size_bytes: 15,
        is_directory: false,
    });
    harness.app.asset_paths.push(path.display().to_string());

    harness.dispatch(Message::AssetDoubleClick(0));
    assert_eq!(
        harness
            .app
            .script
            .current()
            .map(|document| document.title.as_str()),
        Some("enemy.lua")
    );
    assert!(harness.has(iced::widget::Id::from(id::PANE_SCRIPT)));
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn reseed_gives_a_texture_graph_new_seeds_and_is_one_undo_step() {
    use crate::uikit::panels::{material_generator as n, texture_generator as tg};
    let mut harness = Harness::empty();
    let preset = crate::uikit::texture_graph::presets()
        .iter()
        .position(|preset| preset.name == "Wave Function Collapse Bricks")
        .or(Some(0))
        .unwrap_or(0);
    harness.dispatch(Message::TextureGen(tg::Edit::ApplyPreset(preset)));
    let before = crate::uikit::texture_graph::to_text(&harness.app().texgen.graph);
    harness.dispatch(Message::TextureGen(tg::Edit::Node(n::Edit::Reseed)));
    let after = crate::uikit::texture_graph::to_text(&harness.app().texgen.graph);
    assert_ne!(before, after, "seeds moved");
    assert!(harness.app().texgen.dirty);
    harness.dispatch(Message::TextureGen(tg::Edit::Node(n::Edit::Undo)));
    assert_eq!(
        crate::uikit::texture_graph::to_text(&harness.app().texgen.graph),
        before,
        "one undo restores the old seeds"
    );
    // The same graph reseeds to the same result.
    harness.dispatch(Message::TextureGen(tg::Edit::Node(n::Edit::Reseed)));
    assert_eq!(
        crate::uikit::texture_graph::to_text(&harness.app().texgen.graph),
        after
    );
}

#[test]
fn double_clicking_a_baked_asset_or_its_recipe_reopens_the_generator_graph() {
    use crate::uikit::panels::texture_generator as tg;
    let root = test_temp_root("recipe-open");
    let mut harness = Harness::empty();
    harness.app.asset_root_override = Some(root.clone());
    harness.dispatch(Message::TextureGen(tg::Edit::SetName("Bark".to_owned())));
    harness.dispatch(Message::TextureGen(tg::Edit::Export));
    harness.app.pump_texgen_export(true);
    let png = root.join("Textures/bark.png");
    let recipe = root.join("Textures/bark.generator.texture");
    assert!(png.is_file() && recipe.is_file());

    // An imported picture has no recipe: double-click must not open a generator.
    let imported = root.join("Textures/photo.png");
    std::fs::copy(&png, &imported).expect("copy");

    let entry = |name: &str, kind: &str, path: &std::path::Path| {
        (
            doppio_engine::AssetInfo {
                name: name.to_owned(),
                relative_path: format!("Textures/{name}"),
                kind: kind.to_owned(),
                size_bytes: 1,
                is_directory: false,
            },
            path.display().to_string(),
        )
    };
    for (info, path) in [
        entry("bark.png", "image", &png),
        entry("bark.generator.texture", "generator", &recipe),
        entry("photo.png", "image", &imported),
    ] {
        harness.app.assets.push(info);
        harness.app.asset_paths.push(path);
    }
    assert!(harness.app.asset_recipe(0).is_some());
    assert!(harness.app.asset_recipe(1).is_some());
    assert!(
        harness.app.asset_recipe(2).is_none(),
        "imports have no recipe"
    );

    for index in [0, 1] {
        harness.dispatch(Message::TextureGen(tg::Edit::SetName("Other".to_owned())));
        harness.dispatch(Message::AssetDoubleClick(index));
        assert_eq!(harness.app().texgen.name, "Bark", "asset {index}");
        assert_eq!(harness.app().texgen.path.as_deref(), Some(recipe.as_path()));
        assert!(!harness.app().texgen.dirty);
    }
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn opening_a_dyd_asset_uses_the_dryad_editor() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("dryad-asset");
    std::fs::create_dir_all(&root).expect("temp dir");
    let path = root.join("patrol.dyd");
    std::fs::write(&path, "").expect("empty dyd");
    harness.app.assets.push(doppio_engine::AssetInfo {
        name: "patrol.dyd".to_owned(),
        relative_path: "Scripts/patrol.dyd".to_owned(),
        kind: "script".to_owned(),
        size_bytes: 0,
        is_directory: false,
    });
    harness.app.asset_paths.push(path.display().to_string());

    // A single click only selects; opening needs a double click.
    harness.dispatch(Message::AssetPressed(0));
    harness.dispatch(Message::AssetReleased(0));
    assert!(harness.app.dryad.script_path.is_none());
    harness.dispatch(Message::AssetDoubleClick(0));
    assert!(harness
        .app
        .dryad
        .script_path
        .as_ref()
        .is_some_and(|path| path.ends_with("patrol.dyd")));
    assert!(harness.has(iced::widget::Id::from(id::PANE_DRYAD_NODES)));
    assert_eq!(
        harness
            .app
            .script
            .current()
            .map(|document| document.title.as_str()),
        Some("patrol.dyd"),
        "the source tab should also be ready"
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn asset_browser_navigates_folders_and_switches_views() {
    let mut harness = Harness::empty();
    let assets_pane = harness
        .app
        .panes
        .iter()
        .find(|(_, group)| group.pages.contains(&PaneKind::Assets))
        .map(|(pane, _)| *pane)
        .expect("assets pane");
    harness.dispatch(Message::TabPressed(assets_pane, PaneKind::Assets));
    harness.app.assets = vec![
        doppio_engine::AssetInfo {
            name: "Textures".to_owned(),
            relative_path: "Textures".to_owned(),
            kind: "folder".to_owned(),
            size_bytes: 0,
            is_directory: true,
        },
        doppio_engine::AssetInfo {
            name: "brick.png".to_owned(),
            relative_path: "Textures/brick.png".to_owned(),
            kind: "image".to_owned(),
            size_bytes: 2048,
            is_directory: false,
        },
        doppio_engine::AssetInfo {
            name: "Materials".to_owned(),
            relative_path: "Materials".to_owned(),
            kind: "folder".to_owned(),
            size_bytes: 0,
            is_directory: true,
        },
        doppio_engine::AssetInfo {
            name: "rock.mat".to_owned(),
            relative_path: "Materials/rock.mat".to_owned(),
            kind: "material".to_owned(),
            size_bytes: 512,
            is_directory: false,
        },
        doppio_engine::AssetInfo {
            name: "root.png".to_owned(),
            relative_path: "root.png".to_owned(),
            kind: "image".to_owned(),
            size_bytes: 1024,
            is_directory: false,
        },
    ];

    assert!(harness.has(iced::widget::Id::from(id::asset_card(0))));
    // A single click only selects; a double click enters the folder.
    harness.click(id::asset_card(0));
    assert_eq!(harness.app.asset_directory, "");
    assert_eq!(harness.app.asset_selected, Some(0));
    harness.dispatch(Message::AssetDoubleClick(0));
    assert_eq!(harness.app.asset_directory, "Textures");
    assert!(harness.has(iced::widget::Id::from(id::asset_card(1))));
    assert!(!harness.has(iced::widget::Id::from(id::asset_card(3))));

    harness.dispatch(Message::AssetSearch("rock".to_owned()));
    assert_eq!(
        panels::assetmanager::visible_count(&harness.app.assets, "Textures", "rock"),
        1
    );
    assert!(harness.has(iced::widget::Id::from(id::asset_card(3))));

    harness.dispatch(Message::AssetRoot);
    assert_eq!(harness.app.asset_directory, "");
    assert_eq!(harness.app.asset_search, "");
    harness.dispatch(Message::AssetViewMode(panels::assetmanager::ViewMode::List));
    assert_eq!(
        harness.app.asset_view_mode,
        panels::assetmanager::ViewMode::List
    );
    assert!(harness.has(iced::widget::Id::from(id::asset_row(4))));

    harness.dispatch(Message::AssetNavigate("Textures/Sub".to_owned()));
    harness.dispatch(Message::AssetUp);
    assert_eq!(harness.app.asset_directory, "Textures");
}

#[test]
#[ignore = "writes documentation images"]
fn capture_ui_gallery() {
    use serde_json::{json, Value};

    let harness = engine_harness().unwrap_or_else(Harness::with_entities);
    let out = std::env::var_os("DOPPIO_DOCS_OUT")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| {
            Path::new(env!("CARGO_MANIFEST_DIR")).join("../../target/doppio-ui-gallery")
        });
    std::fs::create_dir_all(&out).expect("gallery directory");
    for entry in std::fs::read_dir(&out).expect("gallery directory") {
        let path = entry.expect("gallery entry").path();
        let stale = path.extension().is_some_and(|ext| ext == "png")
            || path
                .file_name()
                .is_some_and(|name| name.to_string_lossy().starts_with("README"))
            || path.file_name().is_some_and(|name| name == "manifest.json");
        if stale {
            let _ = std::fs::remove_file(path);
        }
    }

    let captions = load_captions();
    let mut entries: Vec<Value> = Vec::new();

    let mut capture_entry = |target: &UiId, file: String, kind: &str, bounds: Rectangle| {
        let en = caption(&captions, "en", target.key());
        let pt = caption(&captions, "pt", target.key());
        harness
            .capture(target, &out.join(&file), Some(&en))
            .unwrap_or_else(|error| panic!("{error}"));
        entries.push(json!({
            "id": target.key(),
            "group": target.group(),
            "subgroup": target.subgroup(),
            "object": target.object(),
            "kind": kind,
            "image": format!("../images/ui-reference/{file}"),
            "description": { "en": en, "pt": pt },
            "bounds": {
                "x": bounds.x,
                "y": bounds.y,
                "width": bounds.width,
                "height": bounds.height,
            },
        }));
    };

    for meta in id::CATALOG {
        if meta.kind == "list_row" {
            continue;
        }
        let Some(bounds) = harness.bounds(Id::from(meta.id.clone())) else {
            continue;
        };
        capture_entry(
            &meta.id,
            format!("{}.png", slug(meta.id.key())),
            meta.kind,
            bounds,
        );
    }

    for index in 0..2 {
        let target = id::entity_row(index);
        let Some(bounds) = harness.bounds(Id::from(target.clone())) else {
            continue;
        };
        capture_entry(
            &target,
            format!("entity-row-{index}.png"),
            "list_row",
            bounds,
        );
    }

    let mut inspector_controls = vec![(id::ENTITY_NAME, "Entity name")];
    if harness.app().engine.is_some() {
        inspector_controls.insert(1, (id::transform_axis(0, 0), "Position X"));
        inspector_controls.insert(2, (id::INSPECTOR_ADD_BEHAVIOUR, "Add Behaviour"));
    }
    let annotated = out.join("annotated-inspector.png");
    let annotated_caption = caption(&captions, "en", "annotated.inspector");
    harness
        .capture_group(&inspector_controls, &annotated, Some(&annotated_caption))
        .unwrap_or_else(|error| panic!("{error}"));
    let annotated_pt = caption(&captions, "pt", "annotated.inspector");
    entries.push(json!({
        "id": "annotated.inspector",
        "group": "annotated",
        "subgroup": "inspector",
        "object": "controls",
        "kind": "annotated_group",
        "image": "../images/ui-reference/annotated-inspector.png",
        "description": { "en": annotated_caption, "pt": annotated_pt },
    }));

    std::fs::write(
        out.join("manifest.json"),
        serde_json::to_string_pretty(&entries).expect("manifest"),
    )
    .expect("write manifest");

    for lang in ["en", "pt"] {
        std::fs::write(
            out.join(format!("README.{lang}.md")),
            render_markdown(lang, &entries),
        )
        .expect("write markdown");
    }
    std::fs::write(out.join("README.md"), render_markdown("en", &entries)).expect("write markdown");
    println!("wrote {} ({} entries)", out.display(), entries.len());
}

fn load_captions() -> serde_json::Value {
    let path = std::env::var_os("DOPPIO_DOCS_CAPTIONS")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| {
            Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../../../caffeine-website/captures/ui-captions.json")
        });
    std::fs::read_to_string(&path)
        .ok()
        .and_then(|text| serde_json::from_str(&text).ok())
        .unwrap_or_else(|| serde_json::json!({}))
}

fn caption(captions: &serde_json::Value, lang: &str, key: &str) -> String {
    captions[lang][key]
        .as_str()
        .map(str::to_owned)
        .or_else(|| id::description(key).map(str::to_owned))
        .unwrap_or_else(|| key.to_owned())
}

fn slug(key: &str) -> String {
    key.replace('.', "-")
}

fn render_markdown(lang: &str, entries: &[serde_json::Value]) -> String {
    let title = if lang == "pt" {
        "# Referência da UI do Doppio"
    } else {
        "# Doppio UI reference"
    };
    let note = if lang == "pt" {
        "Gerado por `cargo test -p doppio-ui capture_ui_gallery -- --ignored`. \
As descrições vivem em `caffeine-website/captures/ui-captions.json`."
    } else {
        "Generated by `cargo test -p doppio-ui capture_ui_gallery -- --ignored`. \
Descriptions live in `caffeine-website/captures/ui-captions.json`."
    };
    let mut markdown = format!(
        "{title}\n\n<div class=\"doc-meta\">\n  \
         <span class=\"doc-badge doc-badge--generated\">Generated</span>\n  \
         <span class=\"doc-badge\">Doppio</span>\n</div>\n\n\
         <p class=\"doc-lede\">{note}</p>\n\n"
    );
    for entry in entries {
        let id = entry["id"].as_str().unwrap_or_default();
        let image = entry["image"].as_str().unwrap_or_default();
        let description = entry["description"][lang]
            .as_str()
            .or_else(|| entry["description"]["en"].as_str())
            .unwrap_or_default();
        markdown.push_str(&format!(
            "## `{id}`\n\n<figure class=\"doc-figure\">\n  \
             <img src=\"{image}\" alt=\"{id}\" loading=\"lazy\">\n  \
             <figcaption>{description}</figcaption>\n</figure>\n\n"
        ));
    }
    markdown
}

#[test]
fn pane_maximize_button_toggles_and_restores() {
    let mut harness = Harness::with_entities();
    let pane = harness
        .app()
        .panes
        .iter()
        .map(|(pane, _)| *pane)
        .next()
        .expect("a pane");
    let button = id::pane_maximize(&format!("{pane:?}"));
    assert!(harness.app().panes.maximized().is_none());
    harness.click(button.clone());
    assert_eq!(harness.app().panes.maximized(), Some(pane));
    harness.click(button);
    assert!(harness.app().panes.maximized().is_none());
}

fn show_page(harness: &mut Harness, kind: PaneKind) {
    let panes: Vec<_> = harness.app.panes.iter().map(|(pane, _)| *pane).collect();
    for pane in panes {
        if let Some(group) = harness.app.panes.get_mut(pane) {
            if let Some(index) = group.pages.iter().position(|page| *page == kind) {
                group.active = index;
            }
        }
    }
}

#[test]
fn dragging_a_generator_node_with_the_mouse_moves_it_on_the_snap_grid() {
    let mut harness = Harness::with_entities();
    harness.dispatch(Message::Material(material::Edit::New));
    show_page(&mut harness, PaneKind::Generator);
    harness.app.panes.restore();
    let canvas = harness
        .bounds(Id::from(id::NODE_CANVAS))
        .expect("generator canvas is visible");
    let node = harness
        .app()
        .material
        .as_ref()
        .expect("material")
        .generator
        .nodes
        .first()
        .expect("a starter node")
        .clone();
    let pan = harness.app().generator_pan;
    let zoom = harness.app().generator_zoom;
    // Grab the node's header, inside the card and away from its ports.
    let start = Point::new(
        canvas.x + pan.x + (node.position.x + 60.0) * zoom,
        canvas.y + pan.y + (node.position.y + 8.0) * zoom,
    );
    harness.drag_at(start, Point::new(start.x + 45.0, start.y + 30.0));
    let moved = harness
        .app()
        .material
        .as_ref()
        .expect("material")
        .generator
        .node(node.id)
        .expect("node")
        .position;
    assert!(moved.x > node.position.x, "node should follow the cursor");
    assert!(moved.y > node.position.y, "node should follow the cursor");
    assert_eq!((moved.x % 16.0).abs(), 0.0);
    assert_eq!((moved.y % 16.0).abs(), 0.0);
}

#[test]
fn dragging_a_pane_divider_resizes_the_neighbouring_panes() {
    let mut harness = Harness::with_entities();
    let hierarchy = harness
        .bounds(Id::from(id::PANE_HIERARCHY))
        .expect("hierarchy pane");
    // The divider sits in the gap just right of the hierarchy pane.
    let start = Point::new(hierarchy.x + hierarchy.width + 2.0, hierarchy.y + 120.0);
    harness.drag_at(start, Point::new(start.x + 80.0, start.y));
    let resized = harness
        .bounds(Id::from(id::PANE_HIERARCHY))
        .expect("hierarchy pane");
    assert!(
        resized.width > hierarchy.width + 40.0,
        "pane should grow: {} -> {}",
        hierarchy.width,
        resized.width
    );
}

#[test]
fn inspector_entity_section_collapses_and_expands() {
    let mut harness = Harness::with_entities();
    harness.dispatch(Message::SelectEntity(0));
    assert!(harness.has(Id::from(id::ENTITY_NAME)));
    let toggle = id::component_toggle(usize::MAX - 1);
    harness.click(toggle.clone());
    assert!(!harness.has(Id::from(id::ENTITY_NAME)));
    harness.click(toggle);
    assert!(harness.has(Id::from(id::ENTITY_NAME)));
}

fn pane_with(harness: &Harness, kind: PaneKind) -> pane_grid::Pane {
    harness
        .app()
        .panes
        .iter()
        .find(|(_, group)| group.pages.contains(&kind))
        .map(|(pane, _)| *pane)
        .expect("pane hosting the page")
}

#[test]
fn assets_dock_next_to_the_console_by_default() {
    let harness = Harness::with_entities();
    let bottom = pane_with(&harness, PaneKind::Assets);
    let group = harness.app().panes.get(bottom).expect("group");
    assert_eq!(group.pages, vec![PaneKind::Assets, PaneKind::Console]);
    assert_eq!(group.active_kind(), PaneKind::Assets);
    assert!(
        harness.has(Id::from(id::ASSET_TREE)),
        "folder tree is shown"
    );
    assert!(harness.has(Id::from(id::ASSET_NEW)));
    assert!(harness.has(Id::from(id::ASSET_IMPORT)));
}

#[test]
fn docking_guides_split_or_merge_the_dragged_tab() {
    use crate::uikit::components::dock_overlay::Zone;
    let mut harness = Harness::with_entities();
    let source = pane_with(&harness, PaneKind::Console);
    let target = pane_with(&harness, PaneKind::Viewport);
    let before = harness.app().panes.len();

    // Edge guide: the console leaves the bottom dock and splits the viewport.
    harness.dispatch(Message::TabPressed(source, PaneKind::Console));
    harness.dispatch(Message::DockDrop(target, Zone::Right));
    assert_eq!(harness.app().panes.len(), before + 1);
    let console = pane_with(&harness, PaneKind::Console);
    assert_ne!(console, target);
    assert_eq!(
        harness
            .app()
            .panes
            .get(console)
            .map(|group| group.pages.clone()),
        Some(vec![PaneKind::Console])
    );
    let assets = pane_with(&harness, PaneKind::Assets);
    assert_eq!(
        harness
            .app()
            .panes
            .get(assets)
            .map(|group| group.pages.clone()),
        Some(vec![PaneKind::Assets])
    );
    assert!(harness.app().tab_drag.is_none());

    // Centre guide: join another pane's tab group.
    let hierarchy = pane_with(&harness, PaneKind::Hierarchy);
    harness.dispatch(Message::TabPressed(console, PaneKind::Console));
    harness.dispatch(Message::DockDrop(hierarchy, Zone::Center));
    let group = harness
        .app()
        .panes
        .get(pane_with(&harness, PaneKind::Hierarchy))
        .expect("hierarchy group");
    assert!(group.pages.contains(&PaneKind::Console));
}

#[test]
fn docking_guides_appear_only_after_the_tab_is_dragged_away() {
    let mut harness = Harness::with_entities();
    let source = pane_with(&harness, PaneKind::Viewport);
    // A plain press must not cover the panes with the overlay.
    harness.dispatch(Message::TabPressed(source, PaneKind::Viewport));
    assert!(!harness.app().tab_dragging);
    harness.dispatch(Message::TabExit(source, PaneKind::Viewport));
    assert!(harness.app().tab_dragging);
    harness.dispatch(Message::PointerReleased);
    assert!(!harness.app().tab_dragging);
    assert!(harness.app().tab_drag.is_none());
}

#[test]
fn open_menus_survive_hover_updates_from_widgets_behind_them() {
    let mut harness = Harness::with_entities();
    harness.dispatch(Message::ToggleMenu(MenuId::Scene));
    harness.dispatch(Message::HierarchyRowEnter(0));
    harness.dispatch(Message::HierarchyRowExit(0));
    harness.dispatch(Message::HierarchyHoverClear);
    harness.dispatch(Message::TabEnter(
        pane_with(&harness, PaneKind::Viewport),
        PaneKind::Viewport,
    ));
    assert_eq!(harness.app().open_menu, Some(MenuId::Scene));
    // A real action (or Escape / a click outside) still closes it.
    harness.dispatch(Message::CloseMenu);
    assert_eq!(harness.app().open_menu, None);
}

#[test]
fn asset_wizard_creates_imports_and_lists_folders() {
    use panels::asset_wizard::{Edit, Tab};
    let root = test_temp_root("wizard-root");
    let source = test_temp_root("wizard-src");
    std::fs::create_dir_all(&source).expect("src");
    std::fs::write(source.join("rock.png"), b"png").expect("png");
    let mut harness = Harness::with_entities();
    harness.app.asset_root_override = Some(root.clone());

    harness.dispatch(Message::Wizard(Edit::Open(Tab::Create)));
    assert!(harness.app().wizard.open);
    assert!(harness.has(Id::from(id::WIZARD)));
    harness.dispatch(Message::Wizard(Edit::Template(0)));
    harness.dispatch(Message::Wizard(Edit::Name("Props".to_owned())));
    harness.dispatch(Message::Wizard(Edit::Create));
    assert!(root.join("Props").is_dir());

    harness.dispatch(Message::Wizard(Edit::Template(3)));
    harness.dispatch(Message::Wizard(Edit::Name("spin".to_owned())));
    harness.dispatch(Message::Wizard(Edit::Create));
    assert!(root.join("spin.lua").is_file());

    harness.dispatch(Message::Wizard(Edit::Tab(Tab::Import)));
    harness.dispatch(Message::Wizard(Edit::ImportInput(
        source.join("rock.png").to_string_lossy().into_owned(),
    )));
    harness.dispatch(Message::Wizard(Edit::ImportAdd));
    assert_eq!(harness.app().wizard.sources.len(), 1);
    harness.dispatch(Message::Wizard(Edit::ImportRun));
    assert!(root.join("rock.png").is_file());
    assert!(harness.app().wizard.sources.is_empty());

    harness.dispatch(Message::Wizard(Edit::Close));
    assert!(!harness.app().wizard.open);

    // The tree is derived from the listed folders and their parents.
    let rows = panels::assetmanager::folder_rows(
        &[
            doppio_engine::AssetInfo {
                name: "rock.png".to_owned(),
                relative_path: "Textures/Rock/rock.png".to_owned(),
                kind: "image".to_owned(),
                size_bytes: 1,
                is_directory: false,
            },
            doppio_engine::AssetInfo {
                name: "Audio".to_owned(),
                relative_path: "Audio".to_owned(),
                kind: "folder".to_owned(),
                size_bytes: 0,
                is_directory: true,
            },
        ],
        &std::collections::HashSet::new(),
    );
    let paths: Vec<_> = rows.iter().map(|row| row.path.as_str()).collect();
    assert_eq!(paths, ["", "Audio", "Textures", "Textures/Rock"]);
    let collapsed: std::collections::HashSet<String> = ["Textures".to_owned()].into();
    let hidden = panels::assetmanager::folder_rows(
        &[doppio_engine::AssetInfo {
            name: "rock.png".to_owned(),
            relative_path: "Textures/Rock/rock.png".to_owned(),
            kind: "image".to_owned(),
            size_bytes: 1,
            is_directory: false,
        }],
        &collapsed,
    );
    assert_eq!(
        hidden
            .iter()
            .map(|row| row.path.as_str())
            .collect::<Vec<_>>(),
        ["", "Textures"]
    );
    let _ = std::fs::remove_dir_all(root);
    let _ = std::fs::remove_dir_all(source);
}

#[test]
fn add_behaviour_lists_components_scripts_meshes_and_materials_together() {
    use doppio_engine::{ComponentType, FieldInfo, FieldKind};
    let mut harness = Harness::with_entities();
    let kind = |id: u32, name: &str| ComponentType {
        id,
        name: name.to_owned(),
        fields: vec![FieldInfo {
            name: "Value".to_owned(),
            kind: FieldKind::F32,
            min: 0.0,
            max: 1.0,
            step: 0.1,
            options: Vec::new(),
        }],
    };
    harness.app.component_types = vec![
        kind(1, "Transform"),
        kind(7, "Mesh Renderer"),
        kind(8, "Rigid Body 3D"),
    ];
    let asset = |name: &str, path: &str, kind: &str| doppio_engine::AssetInfo {
        name: name.to_owned(),
        relative_path: path.to_owned(),
        kind: kind.to_owned(),
        size_bytes: 1,
        is_directory: false,
    };
    harness.app.assets = vec![
        asset("spin.lua", "Scripts/spin.lua", "script"),
        asset("hero.glb", "Models/hero.glb", "mesh"),
        asset("brick.mat", "Materials/brick.mat", "material"),
        asset("rock.png", "Textures/rock.png", "image"),
    ];
    harness.click(id::INSPECTOR_ADD_BEHAVIOUR);
    assert!(harness.has(Id::from(id::BEHAVIOUR_PICKER)));
    for shown in [
        "Mesh Renderer",
        "Rigid Body 3D",
        "spin.lua",
        "hero.glb",
        "brick.mat",
    ] {
        assert!(harness.shows(shown), "{shown} should be listed");
    }
    // Five attachable entries: the Transform is not a behaviour to add.
    assert!(harness.has(Id::from(id::behaviour_item(4))));
    assert!(!harness.has(Id::from(id::behaviour_item(5))));
    assert!(
        !harness.shows("rock.png"),
        "plain textures are not behaviours"
    );

    harness.type_into(id::BEHAVIOUR_PICKER_SEARCH, "physics");
    assert!(harness.shows("Rigid Body 3D"));
    assert!(
        !harness.shows("Mesh Renderer"),
        "search also matches the group"
    );
}

#[test]
fn generator_bake_can_run_again_and_stores_height_as_real_depth() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("rebake-depth");
    std::fs::create_dir_all(&root).expect("temp dir");
    let material_path = root.join("relief.mat");
    let surface = doppio_engine::Material::default();
    harness
        .app
        .engine
        .as_ref()
        .expect("engine")
        .save_material(&material_path.to_string_lossy(), &surface)
        .expect("save material");
    harness.app.material = Some(MaterialDocument {
        surface,
        asset_path: Some("relief.mat".to_owned()),
        file_path: Some(material_path.to_string_lossy().into_owned()),
        dirty: false,
        preview_dirty: false,
        source: String::new(),
        generator_history: generators::GraphHistory::new(MaterialGraph::default()),
        generator_packing: generators::ChannelPacking::default(),
        generator_bits: 8,
        generator: MaterialGraph::default(),
        thumbnails: [None, None, None, None],
        source_thumbnail: None,
    });
    if let Some(document) = harness.app.material.as_mut() {
        document.generator.resolution = 64;
        // Drive the Height output from the noise so the relief is authored, not assumed.
        let noise = document
            .generator
            .nodes
            .iter()
            .find(|node| matches!(node.kind, generators::NodeKind::Noise { .. }))
            .map(|node| node.id)
            .expect("noise node");
        let output = document
            .generator
            .nodes
            .iter()
            .find(|node| matches!(node.kind, generators::NodeKind::Output { .. }))
            .map(|node| node.id)
            .expect("output node");
        document
            .generator
            .links
            .push(crate::uikit::components::node_canvas::Link {
                from: noise,
                output: 0,
                to: output,
                input: 3,
            });
    }

    let normal_alpha = |harness: &Harness| -> Vec<u8> {
        let path = root.join("relief_gen_normal.png");
        let (_, _, pixels) = harness
            .app
            .engine
            .as_ref()
            .expect("engine")
            .load_texture_rgba(&path.to_string_lossy())
            .expect("normal map");
        pixels.chunks(4).map(|pixel| pixel[3]).collect()
    };

    harness.dispatch(Message::Generator(material_generator::Edit::Bake));
    wait_for_bake(&mut harness);
    let first_base = std::fs::read(root.join("relief_gen_base_8.png")).expect("first base");
    let alpha = normal_alpha(&harness);
    assert!(
        alpha.iter().max() > alpha.iter().min(),
        "the height must live in the normal map's alpha, not a flat 255"
    );
    let document = harness.app().material.as_ref().expect("material");
    assert!(
        document.surface.height_scale > 0.0,
        "a baked relief turns parallax depth on"
    );

    // Bake a second time with a different graph: it must run again and rewrite the maps.
    if let Some(document) = harness.app.material.as_mut() {
        for node in &mut document.generator.nodes {
            if let generators::NodeKind::Noise { seed, scale, .. } = &mut node.kind {
                *seed += 37;
                *scale = 14.0;
            }
        }
    }
    harness.dispatch(Message::Generator(material_generator::Edit::Bake));
    assert!(
        harness.app().generator_bake.is_some(),
        "a second bake must start"
    );
    wait_for_bake(&mut harness);
    let second_base = std::fs::read(root.join("relief_gen_base_8.png")).expect("second base");
    assert_ne!(first_base, second_base, "the second bake rewrites the maps");
    assert!(harness
        .app()
        .generator_status
        .as_deref()
        .is_some_and(|status| status.starts_with("Baked 64×64")));
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn generator_library_preset_replaces_graph_undoably_and_channels_switch() {
    let _guard = test_guard();
    let mut app = App::empty();
    app.update(Message::Material(material::Edit::New));
    app.update(Message::OpenGenerator);
    let before = app
        .material
        .as_ref()
        .map(|document| document.generator.nodes.len())
        .expect("material");
    if let Some(document) = app.material.as_mut() {
        document.generator.resolution = 256;
    }
    app.update(Message::Generator(material_generator::Edit::ApplyPreset(0)));
    let document = app.material.as_ref().expect("material");
    assert_eq!(document.generator.resolution, 256, "resolution is kept");
    assert!(document.generator.nodes.len() > before);
    assert!(document
        .generator
        .nodes
        .iter()
        .any(|node| matches!(node.kind, generators::NodeKind::Proc { .. })));
    assert!(document.generator_history.can_undo());

    app.update(Message::Generator(
        material_generator::Edit::SetPreviewChannel(2),
    ));
    assert_eq!(app.generator_preview_channel, 2);
    app.update(Message::Generator(
        material_generator::Edit::SetPreviewChannel(99),
    ));
    assert_eq!(
        app.generator_preview_channel,
        generators::PreviewChannel::ALL.len() - 1
    );

    app.update(Message::Generator(material_generator::Edit::Undo));
    let document = app.material.as_ref().expect("material");
    assert_eq!(document.generator.nodes.len(), before);
}

#[test]
fn procedural_graphs_preview_without_an_engine() {
    let mut harness = Harness::empty();
    harness.dispatch(Message::Material(material::Edit::New));
    harness.dispatch(Message::Generator(material_generator::Edit::ApplyPreset(0)));
    for _ in 0..4000 {
        harness.dispatch(Message::ViewportTick);
        std::thread::sleep(std::time::Duration::from_millis(5));
        if harness.app().generator_preview.is_some() {
            break;
        }
    }
    assert!(
        harness.app().generator_preview.is_some(),
        "a purely procedural graph must preview without an engine (status: {:?})",
        harness.app().generator_status
    );
}

// ----------------------------------------------------------- Texture Generator

#[test]
fn texture_generator_is_a_separate_pane_with_its_own_widgets() {
    use crate::uikit::panels::texture_generator as tg;
    let mut harness = Harness::empty();
    harness.dispatch(Message::ShowTextureGenerator);
    let active = harness
        .app()
        .panes
        .iter()
        .any(|(_, group)| group.active_kind() == PaneKind::TextureGenerator);
    assert!(active, "the Texture Generator tab must come to the front");
    for widget in [
        id::TEXTUREGEN_CANVAS,
        id::TEXTUREGEN_LIBRARY,
        id::TEXTUREGEN_EXPORT,
        id::TEXTUREGEN_PREVIEW,
        id::TEXTUREGEN_FLIP_COLS,
    ] {
        assert!(harness.has(Id::from(widget.clone())), "missing {widget:?}");
    }
    // It never borrows the Material Generator's ids for its own toolbar.
    assert!(!harness.has(Id::from(id::GENERATOR_BAKE)));
    assert_eq!(PaneKind::Generator.title(), "Material Generator");
    assert_eq!(PaneKind::TextureGenerator.title(), "Texture Generator");
    let _ = tg::PREVIEW_MODES;
}

#[test]
fn texture_generator_edits_the_graph_with_undo_and_a_live_preview() {
    use crate::uikit::generators::{NodeKind, TextureOutputMode};
    use crate::uikit::panels::{material_generator as n, texture_generator as tg};
    let mut harness = Harness::empty();
    assert!(
        harness.app().texgen.preview.is_some(),
        "the new graph previews at once"
    );
    // A library preset replaces the graph and keeps the chosen size.
    harness.dispatch(Message::TextureGen(tg::Edit::Node(n::Edit::SetResolution(
        256,
    ))));
    let explosion = crate::uikit::texture_graph::presets()
        .iter()
        .position(|preset| preset.name == "Explosion Burst")
        .expect("preset");
    harness.dispatch(Message::TextureGen(tg::Edit::ApplyPreset(explosion)));
    assert_eq!(harness.app().texgen.graph.resolution, 256);
    assert!(harness.app().texgen.graph.nodes.len() >= 6);
    assert!(harness.app().texgen.preview.is_some());

    // The Texture Output cannot be deleted.
    let output = harness
        .app()
        .texgen
        .graph
        .texture_output_node()
        .expect("out");
    harness.dispatch(Message::TextureGen(tg::Edit::Node(n::Edit::DeleteNode(
        output,
    ))));
    assert!(harness.app().texgen.graph.node(output).is_some());

    // Add a node, undo it, redo it.
    let before = harness.app().texgen.graph.nodes.len();
    let kind = crate::uikit::texture_graph::palette()
        .into_iter()
        .find(|kind| kind.title() == "Flame")
        .expect("flame");
    harness.dispatch(Message::TextureGen(tg::Edit::Node(n::Edit::AddNode(kind))));
    assert_eq!(harness.app().texgen.graph.nodes.len(), before + 1);
    harness.dispatch(Message::TextureGen(tg::Edit::Node(n::Edit::Undo)));
    assert_eq!(harness.app().texgen.graph.nodes.len(), before);
    harness.dispatch(Message::TextureGen(tg::Edit::Node(n::Edit::Redo)));
    assert_eq!(harness.app().texgen.graph.nodes.len(), before + 1);

    // Switching the output to packed channels keeps RGBA ports and drops nothing valid; going
    // back to colour+alpha removes links to the ports that vanished.
    harness.dispatch(Message::TextureGen(tg::Edit::Node(n::Edit::SetParam(
        output,
        n::Param::TextureOutputMode(1),
    ))));
    assert!(matches!(
        harness
            .app()
            .texgen
            .graph
            .node(output)
            .map(|node| &node.kind),
        Some(NodeKind::TextureOutput {
            mode: TextureOutputMode::Channels
        })
    ));
    harness.dispatch(Message::TextureGen(tg::Edit::Node(n::Edit::SetParam(
        output,
        n::Param::TextureOutputMode(0),
    ))));
    let ports = harness
        .app()
        .texgen
        .graph
        .node(output)
        .map(|node| node.kind.inputs().len());
    assert_eq!(ports, Some(2));
    assert!(harness
        .app()
        .texgen
        .graph
        .links
        .iter()
        .all(|link| link.to != output || link.input < 2));
}

#[test]
fn texture_generator_saves_and_exports_into_the_project_textures_folder() {
    use crate::uikit::panels::texture_generator as tg;
    let root = test_temp_root("texgen-root");
    let mut harness = Harness::empty();
    harness.app.asset_root_override = Some(root.clone());
    harness.dispatch(Message::TextureGen(tg::Edit::SetName(
        "Maple Leaf".to_owned(),
    )));
    harness.dispatch(Message::TextureGen(tg::Edit::SetBits(16)));
    harness.dispatch(Message::TextureGen(tg::Edit::Export));
    harness.app.pump_texgen_export(true);
    let png = root.join("Textures/maple_leaf.png");
    let graph = root.join("Textures/maple_leaf.generator.texture");
    assert!(
        png.is_file(),
        "png missing: {:?}",
        harness.app().texgen.status
    );
    assert!(graph.is_file(), "the graph travels with its image");
    let image = image::open(&png).expect("png");
    assert!(image.color().has_alpha());
    assert_eq!(image.color().bits_per_pixel(), 64);

    // A flipbook is one wide sheet.
    harness.dispatch(Message::TextureGen(tg::Edit::SetFlipbook(2, 2)));
    harness.dispatch(Message::TextureGen(tg::Edit::ExportFlipbook));
    harness.app.pump_texgen_export(true);
    let sheet = root.join("Textures/maple_leaf_flipbook_2x2.png");
    let sheet = image::open(sheet).expect("sheet");
    assert_eq!(sheet.width(), 2 * 512);
    assert_eq!(sheet.height(), 2 * 512);

    // The saved graph reopens as a texture graph, never as a material.
    let recipe = crate::uikit::recipe::read(&graph).expect("recipe");
    let loaded = crate::uikit::texture_graph::from_text(&recipe.graph).expect("load");
    assert!(crate::uikit::texture_graph::is_texture_graph(&loaded));
    std::fs::remove_dir_all(root).ok();
}

fn render_entity_view(engine: &mut doppio_engine::Engine) -> Vec<u8> {
    render_entity_view_sized(engine, 160, 90)
}

fn render_entity_view_sized(
    engine: &mut doppio_engine::Engine,
    width: u32,
    height: u32,
) -> Vec<u8> {
    engine.render_begin(width, height).expect("begin");
    let mut pixels = vec![0u8; (width * height * 4) as usize];
    for _ in 0..400 {
        match engine.render_poll(&mut pixels).expect("poll") {
            doppio_engine::RenderState::Ready => return pixels,
            _ => std::thread::sleep(std::time::Duration::from_millis(5)),
        }
    }
    panic!("render did not finish");
}

/// Pixels that differ from a render of the empty scene: roughly "how much the object covers".
fn coverage(engine: &mut doppio_engine::Engine, empty: &[u8]) -> usize {
    let frame = render_entity_view(engine);
    frame
        .chunks(4)
        .zip(empty.chunks(4))
        .filter(|(a, b)| a.iter().zip(b.iter()).any(|(x, y)| x.abs_diff(*y) > 6))
        .count()
}

#[test]
fn scale_edits_change_what_the_engine_renders() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let empty = {
        let engine = harness.app.engine.as_mut().expect("engine");
        render_entity_view(engine)
    };
    for label in ["Cube", "Plane", "Quad"] {
        let index = prefabs::PREFABS
            .iter()
            .position(|p| p.label == label)
            .unwrap_or_else(|| panic!("{label} prefab"));
        harness.dispatch(Message::CreatePrefab(index));
        let id = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(id, [0.0, 0.0, 0.0]).unwrap();
        engine.set_scale(id, [1.0, 1.0, 1.0]).unwrap();
        let small = coverage(engine, &empty);
        engine.set_scale(id, [2.0, 2.0, 2.0]).unwrap();
        let large = coverage(engine, &empty);
        assert!(
            large as f32 > small as f32 * 1.6,
            "{label}: doubling the scale must roughly quadruple the covered pixels ({small} -> {large})"
        );
        engine.set_scale(id, [1.0, 1.0, 1.0]).unwrap();
        harness.dispatch(Message::Inspector(panels::inspector::Edit::Delete));
    }
}

#[test]
fn scale_applies_to_scene_loaded_entities_too() {
    let _guard = test_guard();
    let Some(mut engine) = doppio_engine::Engine::load_default() else {
        return;
    };
    let scene = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../assets/benchmarks/dense_outdoor.caf");
    if engine.load_scene(&scene.to_string_lossy()).is_err() {
        return;
    }
    let count = engine.entity_count();
    let mut both = 0;
    for index in 0..count {
        if let Ok(info) = engine.entity_at(index) {
            let names: Vec<String> = engine
                .entity_components(info.id)
                .iter()
                .map(|c| c.name.clone())
                .collect();
            let has = |n: &str| names.iter().any(|x| x == n);
            if has("Transform") && has("Position 3D") {
                both += 1;
            }
        }
    }
    eprintln!("entities with BOTH Transform and Position 3D: {both} of {count}");
    let mut checked = 0;
    for index in 0..count.min(40) {
        let Ok(info) = engine.entity_at(index) else {
            continue;
        };
        let before = engine.scale(info.id).unwrap_or([1.0; 3]);
        let want = [before[0] * 2.0, before[1] * 2.0, before[2] * 2.0];
        if engine.set_scale(info.id, want).is_err() {
            continue;
        }
        let got = engine.scale(info.id).unwrap_or([0.0; 3]);
        assert_eq!(
            got, want,
            "entity {} ({}) lost its scale",
            info.id, info.name
        );
        checked += 1;
    }
    assert!(checked > 0);
}

/// Luminance steps *inside the object* (pixels that differ from the empty scene): a proxy for
/// how finely its texture tiles. The grid and axis lines of the empty viewport are ignored.
fn texture_edges(pixels: &[u8], empty: &[u8], width: usize) -> usize {
    let luma =
        |p: &[u8], i: usize| p[i * 4] as i32 * 3 + p[i * 4 + 1] as i32 * 6 + p[i * 4 + 2] as i32;
    let inside = |i: usize| (0..3).any(|c| pixels[i * 4 + c].abs_diff(empty[i * 4 + c]) > 10);
    let height = pixels.len() / 4 / width;
    let mut edges = 0;
    for y in 0..height - 1 {
        for x in 0..width - 1 {
            let i = y * width + x;
            if !inside(i) || !inside(i + 1) || !inside(i + width) {
                continue;
            }
            if (luma(pixels, i) - luma(pixels, i + 1)).abs() > 60
                || (luma(pixels, i) - luma(pixels, i + width)).abs() > 60
            {
                edges += 1;
            }
        }
    }
    edges
}

#[test]
fn scaled_objects_keep_their_texture_density_when_uvs_follow_the_scale() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("uv-object-scale");
    std::fs::create_dir_all(&root).expect("dir");
    let checker = root.join("checker.png");
    let mut image = RgbaImage::new(64, 64);
    for (x, y, pixel) in image.enumerate_pixels_mut() {
        *pixel = if (x / 16 + y / 16) % 2 == 0 {
            Rgba([245, 245, 245, 255])
        } else {
            Rgba([15, 15, 15, 255])
        };
    }
    image.save(&checker).expect("checker");

    let empty = {
        let engine = harness.app.engine.as_mut().expect("engine");
        render_entity_view_sized(engine, 480, 270)
    };
    let plane = prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Plane")
        .expect("plane prefab");
    harness.dispatch(Message::CreatePrefab(plane));
    let id = harness.app().entities[harness.app().selected].id;
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_position(id, [0.0, 0.0, 0.0]).unwrap();

    let mut edges = |follow: bool, size: f32| -> usize {
        let material = doppio_engine::Material {
            albedo: [1.0, 1.0, 1.0, 1.0],
            roughness: 0.8,
            albedo_map: checker.display().to_string(),
            // Self-lit, so the pattern is readable whatever the light does (and in the docs).
            emission: [1.0, 1.0, 1.0],
            emission_strength: 1.0,
            emission_map: checker.display().to_string(),
            uv_object_scale: follow,
            ..doppio_engine::Material::default()
        };
        let path = root.join(format!("m_{follow}.mat"));
        engine
            .save_material(&path.to_string_lossy(), &material)
            .expect("save material");
        engine
            .apply_material(id, &path.to_string_lossy())
            .expect("apply material");
        engine.set_scale(id, [size, 1.0, size]).unwrap();
        let frame = render_entity_view_sized(engine, 480, 270);
        if let Ok(dir) = std::env::var("DOPPIO_UV_DOC_DIR") {
            // Doc images: the same plane, UVs authored 1:1 versus following the scale.
            std::fs::create_dir_all(&dir).ok();
            if let Some(image) = RgbaImage::from_raw(480, 270, frame.clone()) {
                image
                    .save(format!(
                        "{dir}/uv-scale-{}-{size}.png",
                        if follow { "follows" } else { "stretched" }
                    ))
                    .ok();
            }
        }
        texture_edges(&frame, &empty, 480)
    };
    let stretched = edges(false, 4.0);
    let following = edges(true, 4.0);
    assert!(
        following as f32 > stretched as f32 * 1.5,
        "a 4x plane must tile the texture finer when UVs follow the scale ({stretched} -> {following} edges)"
    );
    // At scale 1 nothing changes: the flag only matters once the object is scaled.
    let unit_off = edges(false, 1.0);
    let unit_on = edges(true, 1.0);
    let ratio = unit_on as f32 / unit_off.max(1) as f32;
    assert!(
        (0.8..1.25).contains(&ratio),
        "scale 1 must look the same ({unit_off} vs {unit_on})"
    );
    std::fs::remove_dir_all(root).ok();
}

// ------------------------------------------------------------- scene markers

fn create_prefab_labelled(harness: &mut Harness, label: &str) -> u32 {
    let index = prefabs::PREFABS
        .iter()
        .position(|p| p.label == label)
        .unwrap_or_else(|| panic!("{label} prefab"));
    harness.dispatch(Message::CreatePrefab(index));
    harness.app().entities[harness.app().selected].id
}

#[test]
fn scene_markers_describe_lights_cameras_colliders_and_empties() {
    use crate::uikit::components::scene_markers::{ColliderShape, Kind};
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let sun = create_prefab_labelled(&mut harness, "Directional Light");
    let point = create_prefab_labelled(&mut harness, "Point Light");
    let spot = create_prefab_labelled(&mut harness, "Spot Light");
    let camera = create_prefab_labelled(&mut harness, "Camera");
    let empty = create_prefab_labelled(&mut harness, "Empty Entity");
    let body = create_prefab_labelled(&mut harness, "Physics Cube");
    let kind_of = |harness: &Harness, id: u32| {
        harness
            .app()
            .markers
            .iter()
            .find(|marker| marker.id == id)
            .map(|marker| marker.kind.clone())
    };
    // An unrotated sun uses the engine's default direction, exactly like the renderer.
    let Some(Kind::Directional { direction }) = kind_of(&harness, sun) else {
        panic!("directional light marker missing");
    };
    assert!(
        direction[1] < -0.5,
        "the default sun shines down: {direction:?}"
    );
    assert!(matches!(kind_of(&harness, point), Some(Kind::Point { radius }) if radius > 0.0));
    assert!(matches!(kind_of(&harness, spot), Some(Kind::Spot { angle, .. }) if angle > 0.0));
    assert!(matches!(kind_of(&harness, camera), Some(Kind::Camera { fov, .. }) if fov > 10.0));
    assert!(matches!(kind_of(&harness, empty), Some(Kind::Empty { .. })));
    let colliders = harness
        .app()
        .markers
        .iter()
        .filter(|m| {
            m.id == body
                && matches!(
                    m.kind,
                    Kind::Collider {
                        shape: ColliderShape::Box,
                        ..
                    }
                )
        })
        .count();
    assert_eq!(colliders, 1, "a physics cube shows its box collider");
    // A plain mesh has no marker of its own.
    let cube = create_prefab_labelled(&mut harness, "Cube");
    assert!(kind_of(&harness, cube).is_none());

    // Rotating the sun re-aims its marker.
    harness.dispatch(Message::MarkerSelected(sun));
    harness.dispatch(Message::Gizmo(gizmo::Message::Changed(
        gizmo::Edit::Rotate {
            axis: gizmo::Axis::Y,
            rotation: [0.0, 90.0, 0.0],
        },
    )));
    let Some(Kind::Directional { direction }) = kind_of(&harness, sun) else {
        panic!("directional light marker missing");
    };
    assert!(
        direction[1].abs() < 0.2,
        "a rotated sun no longer uses the default: {direction:?}"
    );
    assert!(
        (direction[0] + 1.0).abs() < 0.05,
        "turned 90 degrees about Y it shines along -X: {direction:?}"
    );
    assert_eq!(
        harness.app().selected,
        sun_index(&harness, sun),
        "clicking a marker selects its entity"
    );

    // The toggle hides the overlay without losing the data.
    harness.dispatch(Message::ToggleMarkers);
    assert!(!harness.app().show_markers && !harness.app().markers.is_empty());
    harness.dispatch(Message::ToggleMarkers);
    assert!(harness.app().show_markers);
}

fn sun_index(harness: &Harness, id: u32) -> usize {
    harness
        .app()
        .entities
        .iter()
        .position(|entity| entity.id == id)
        .expect("entity")
}

#[test]
fn gizmo_plane_and_uniform_edits_reach_the_engine() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let id = create_prefab_labelled(&mut harness, "Cube");
    harness.dispatch(Message::Gizmo(gizmo::Message::Changed(
        gizmo::Edit::MovePlane {
            plane: gizmo::Plane::XZ,
            position: [3.0, 0.0, -2.0],
        },
    )));
    let engine = harness.app.engine.as_ref().expect("engine");
    let moved = engine.world_position(id).expect("position");
    assert!(
        (moved[0] - 3.0).abs() < 1e-3 && (moved[2] + 2.0).abs() < 1e-3,
        "{moved:?}"
    );

    harness.dispatch(Message::Gizmo(gizmo::Message::Changed(
        gizmo::Edit::ScaleUniform {
            scale: [2.0, 2.0, 2.0],
        },
    )));
    let engine = harness.app.engine.as_ref().expect("engine");
    assert_eq!(engine.scale(id).unwrap(), [2.0, 2.0, 2.0]);
    assert_eq!(
        harness.app().entities[harness.app().selected].scale,
        [2.0, 2.0, 2.0]
    );
    // Undo restores the previous scale: gizmo edits stay undoable.
    harness.dispatch(Message::Undo);
    let engine = harness.app.engine.as_ref().expect("engine");
    assert_ne!(engine.scale(id).unwrap(), [2.0, 2.0, 2.0]);
}

#[test]
fn rebuilding_markers_for_a_large_scene_stays_cheap() {
    let _guard = test_guard();
    let Some(mut engine) = doppio_engine::Engine::load_default() else {
        return;
    };
    let scene = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../assets/benchmarks/dense_outdoor.caf");
    if engine.load_scene(&scene.to_string_lossy()).is_err() {
        return;
    }
    let entities: Vec<_> = projectloader::read_entities(&engine)
        .into_iter()
        .map(|e| (e.id, e.name, e.position, e.rotation, e.scale))
        .collect();
    let count = entities.len();
    let started = std::time::Instant::now();
    let markers = scene_markers::collect(&engine, &entities, |id| engine.world_position(id).ok());
    let elapsed = started.elapsed();
    eprintln!(
        "{count} entities -> {} markers in {elapsed:?}",
        markers.len()
    );
    assert!(
        elapsed < std::time::Duration::from_millis(400),
        "collecting markers for {count} entities took {elapsed:?}"
    );
}

#[test]
fn a_scaled_parent_scales_where_its_children_are() {
    let _guard = test_guard();
    let Some(mut engine) = doppio_engine::Engine::load_default() else {
        return;
    };
    let parent = engine.create_entity("Group").unwrap();
    let child = engine.create_entity("Child").unwrap();
    engine.set_position(child, [1.0, 0.0, 0.0]).unwrap();
    engine.set_entity_parent(child, Some(parent)).unwrap();
    engine.set_scale(parent, [3.0, 3.0, 3.0]).unwrap();
    let world = engine.world_position(child).unwrap();
    assert!(
        (world[0] - 3.0).abs() < 1e-3,
        "child at x=1 under a 3x parent must sit at x=3, got {world:?}"
    );
}

#[test]
fn scale_survives_a_save_and_reload_and_still_renders() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let empty = {
        let engine = harness.app.engine.as_mut().expect("engine");
        render_entity_view(engine)
    };
    let id = create_prefab_labelled(&mut harness, "Cube");
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_position(id, [0.0, 0.0, 0.0]).unwrap();
    let path = std::env::temp_dir().join(format!("doppio-scale-{}.caf", std::process::id()));
    engine.save_scene(&path.to_string_lossy()).expect("save");
    engine.load_scene(&path.to_string_lossy()).expect("load");
    let mut found = None;
    for index in 0..engine.entity_count() {
        let info = engine.entity_at(index).unwrap();
        let names: Vec<String> = engine
            .entity_components(info.id)
            .iter()
            .map(|c| c.name.clone())
            .collect();
        eprintln!("{} {:?}", info.name, names);
        if names.iter().any(|n| n == "Mesh Filter") {
            found = Some(info.id);
        }
    }
    let id = found.expect("cube after reload");
    engine.set_scale(id, [1.0; 3]).unwrap();
    let small = coverage(engine, &empty);
    engine.set_scale(id, [3.0; 3]).unwrap();
    let large = coverage(engine, &empty);
    assert!(
        large as f32 > small as f32 * 1.6,
        "after reload: {small} -> {large}"
    );
}

#[test]
fn a_cube_with_a_stone_like_material_still_scales() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("cube-material-scale");
    std::fs::create_dir_all(&root).expect("dir");
    let tex = root.join("t.png");
    RgbaImage::from_pixel(64, 64, Rgba([120, 118, 110, 255]))
        .save(&tex)
        .unwrap();
    let empty = {
        let engine = harness.app.engine.as_mut().expect("engine");
        render_entity_view(engine)
    };
    let id = create_prefab_labelled(&mut harness, "Cube");
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_position(id, [0.0, 0.0, 0.0]).unwrap();
    for (label, material) in [
        ("plain", doppio_engine::Material::default()),
        (
            "textured",
            doppio_engine::Material {
                albedo_map: tex.display().to_string(),
                normal_map: tex.display().to_string(),
                height_scale: 0.04,
                ..doppio_engine::Material::default()
            },
        ),
        (
            "displaced",
            doppio_engine::Material {
                albedo_map: tex.display().to_string(),
                normal_map: tex.display().to_string(),
                displacement_mode: 1,
                ..doppio_engine::Material::default()
            },
        ),
    ] {
        let path = root.join(format!("{label}.mat"));
        engine
            .save_material(&path.to_string_lossy(), &material)
            .unwrap();
        engine.apply_material(id, &path.to_string_lossy()).unwrap();
        engine.set_scale(id, [1.0; 3]).unwrap();
        let small = coverage(engine, &empty);
        engine.set_scale(id, [3.0; 3]).unwrap();
        let large = coverage(engine, &empty);
        assert!(
            large as f32 > small as f32 * 1.6,
            "{label}: {small} -> {large}"
        );
    }
}

#[test]
fn entities_with_both_transform_and_split_components_render_their_scale_and_rotation() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let empty = {
        let engine = harness.app.engine.as_mut().expect("engine");
        render_entity_view(engine)
    };
    let id = create_prefab_labelled(&mut harness, "Cube");
    let engine = harness.app.engine.as_mut().expect("engine");
    // Scenes saved by older editors carry the legacy Transform AND Position/Rotation/Scale 3D.
    for name in ["Position 3D", "Rotation 3D", "Scale 3D"] {
        add_component(engine, id, name).expect(name);
    }
    engine.set_position(id, [0.0, 0.0, 0.0]).unwrap();
    engine.set_scale(id, [1.0; 3]).unwrap();
    let small = coverage(engine, &empty);
    engine.set_scale(id, [3.0; 3]).unwrap();
    let large = coverage(engine, &empty);
    assert!(
        large as f32 > small as f32 * 1.6,
        "the scale of an entity that has both component sets must reach the renderer ({small} -> {large})"
    );
    assert_eq!(engine.scale(id).unwrap(), [3.0; 3]);

    // Rotation too: a 45 degree turn about Y changes the silhouette of a cube.
    engine.set_scale(id, [2.0, 1.0, 1.0]).unwrap();
    let before = render_entity_view(engine);
    engine.set_rotation(id, [0.0, 90.0, 0.0]).unwrap();
    let after = render_entity_view(engine);
    assert_ne!(
        before, after,
        "rotating an entity with both component sets must change the render"
    );
    let turned = engine.rotation(id).unwrap();
    assert!((turned[1] - 90.0).abs() < 0.5, "{turned:?}");
}

// ------------------------------------------------------------- Nature Generator

#[test]
fn nature_generator_is_a_node_graph_pane_with_live_parameters() {
    use crate::uikit::panels::nature_generator as ng;
    let mut harness = Harness::empty();
    harness.dispatch(Message::ShowNatureGenerator);
    assert!(
        harness
            .app()
            .panes
            .iter()
            .any(|(_, g)| g.active_kind() == PaneKind::NatureGenerator),
        "the tab comes to the front"
    );
    for widget in [
        id::NATUREGEN_CANVAS,
        id::NATUREGEN_PREVIEW,
        id::NATUREGEN_PRESET,
        id::NATUREGEN_PLACE,
        id::NATUREGEN_EXPORT,
    ] {
        assert!(harness.has(Id::from(widget.clone())), "missing {widget:?}");
    }
    assert!(
        harness.app().nature.preview.is_some(),
        "the preview renders at once"
    );
    let before = harness.app().nature.stats.clone();
    let grow = harness
        .app()
        .nature
        .graph
        .nodes
        .iter()
        .find(|n| n.op == "grow")
        .unwrap()
        .id;
    let levels = crate::uikit::nature::ops::find("grow")
        .unwrap()
        .params
        .iter()
        .position(|p| p.key == "levels")
        .unwrap();
    // The Oak already has four levels: take one away.
    harness.dispatch(Message::NatureGen(ng::Edit::SetParam(grow, levels, 2.0)));
    assert_ne!(
        harness.app().nature.stats,
        before,
        "parameters regenerate the model"
    );

    // Nodes can be added, wired, undone and redone; port colours are enforced.
    let nodes = harness.app().nature.graph.nodes.len();
    harness.dispatch(Message::NatureGen(ng::Edit::AddNode("noise_displace")));
    assert_eq!(harness.app().nature.graph.nodes.len(), nodes + 1);
    let added = harness
        .app()
        .nature
        .selected
        .expect("the new node is selected");
    harness.dispatch(Message::NatureGen(ng::Edit::Connect {
        from: grow,
        output: 0,
        to: added,
        input: 0,
    }));
    assert!(
        harness
            .app()
            .nature
            .status
            .as_deref()
            .unwrap_or("")
            .contains("cannot connect"),
        "a skeleton cannot feed a mesh input"
    );
    harness.dispatch(Message::NatureGen(ng::Edit::Undo));
    assert_eq!(
        harness.app().nature.graph.nodes.len(),
        nodes,
        "undo removes the node"
    );
    harness.dispatch(Message::NatureGen(ng::Edit::Redo));
    assert_eq!(harness.app().nature.graph.nodes.len(), nodes + 1);
    // The Output node stays.
    let output = harness.app().nature.graph.output_node().unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::DeleteNode(output)));
    assert!(harness.app().nature.graph.node(output).is_some());

    // A preset replaces the graph; a rock has one part.
    let rock = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Boulder")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(rock)));
    assert!(
        harness.app().nature.stats.contains("1 part"),
        "{}",
        harness.app().nature.stats
    );
    harness.dispatch(Message::NatureGen(ng::Edit::SetQuality(0)));
    let low = harness.app().nature.stats.clone();
    harness.dispatch(Message::NatureGen(ng::Edit::SetQuality(2)));
    assert_ne!(
        harness.app().nature.stats,
        low,
        "quality changes the polygon count"
    );
}

#[test]
fn nature_models_export_and_place_in_the_scene_with_their_own_materials() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("nature-place");
    harness.app.asset_root_override = Some(root.clone());
    let empty = {
        let engine = harness.app.engine.as_mut().expect("engine");
        render_entity_view(engine)
    };
    // A tree: trunk with a baked bark material, leaves with a cut-out card material.
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Oak")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(oak)));
    harness.dispatch(Message::NatureGen(ng::Edit::Export));
    assert!(
        root.join("Models/Nature/oak_trunk.obj").is_file(),
        "{:?}",
        harness.app().nature.status
    );
    assert!(root.join("Models/Nature/oak_leaves.obj").is_file());
    let recipe = crate::uikit::recipe::read(&root.join("Models/Nature/oak.generator.nature"))
        .expect("recipe file")
        .graph;
    assert!(
        crate::uikit::nature::from_text(&recipe).is_some(),
        "the .nature file holds the graph"
    );

    let before = harness.app().entities.len();
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    assert_eq!(
        harness.app().entities.len(),
        before + 3,
        "group + trunk + leaves: {:?}",
        harness.app().nature.status
    );
    assert!(
        harness
            .app()
            .nature
            .status
            .as_deref()
            .unwrap_or("")
            .starts_with("Placed Oak ("),
        "{:?}",
        harness.app().nature.status
    );
    let group = harness.app().entities[harness.app().selected].id;
    let engine = harness.app.engine.as_mut().expect("engine");
    let children: Vec<u32> = (0..engine.entity_count())
        .filter_map(|i| engine.entity_at(i).ok())
        .filter(|info| engine.entity_parent(info.id).ok().flatten() == Some(group))
        .map(|info| info.id)
        .collect();
    assert_eq!(children.len(), 2);
    let materials: Vec<String> = children
        .iter()
        .map(|c| engine.entity_material(*c).unwrap_or_default())
        .collect();
    assert!(
        materials
            .iter()
            .all(|m| m.contains("Materials/Nature") || m.contains("Textures/Nature")),
        "{materials:?}"
    );
    assert_ne!(
        materials[0], materials[1],
        "trunk and leaves get different materials"
    );
    assert!(
        crate::uikit::asset_folders::resolve(
            &root,
            crate::uikit::asset_folders::AssetFolder::NatureMaterials
        )
        .read_dir()
        .unwrap()
        .count()
            >= 2,
        "the .mat files were written"
    );

    // Every kind places cleanly (several parts under one group: trees, a four-part flower...).
    for name in [
        "Pine",
        "Boulder",
        "Pink Flower",
        "Grass Clump",
        "Low-poly Tree",
        "Fern",
    ] {
        let index = harness
            .app()
            .nature
            .presets
            .iter()
            .position(|p| p.name == name)
            .unwrap();
        harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(index)));
        harness.dispatch(Message::NatureGen(ng::Edit::Place));
        let status = harness.app().nature.status.clone().unwrap_or_default();
        assert!(
            status.starts_with(&format!("Placed {name} (")) && !status.contains("problems"),
            "{name}: {status}"
        );
    }
    let engine = harness.app.engine.as_mut().expect("engine");
    // The real engine draws it.
    let mut covered = 0;
    for _ in 0..3 {
        let frame = render_entity_view(engine);
        covered = frame
            .chunks(4)
            .zip(empty.chunks(4))
            .filter(|(a, b)| a.iter().zip(b.iter()).any(|(x, y)| x.abs_diff(*y) > 6))
            .count();
    }
    assert!(
        covered > 150,
        "the placed tree must show up in the viewport ({covered} px)"
    );
    std::fs::remove_dir_all(root).ok();
}

/// Swaps the material node feeding `part` for one that points at your own project asset.
fn use_project_asset(harness: &mut Harness, part: u64, op: &'static str, path: &str) {
    let graph = &mut harness.app.nature.graph;
    let old = graph
        .links
        .iter()
        .find(|l| l.to == part && l.input == 1)
        .map(|l| l.from)
        .expect("a material node");
    graph.remove(old);
    let node = graph.add(op, iced::Point::new(0.0, 0.0));
    graph.set_string(node, 0, path.to_owned());
    assert!(graph.connect(node, 0, part, 1));
    harness.app.nature.refresh();
}

#[test]
fn nature_presets_write_their_materials_as_project_assets() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("nature-materialized");
    std::fs::create_dir_all(&root).expect("dir");
    if std::env::var("DOPPIO_OAK_SCRATCH").is_ok() {
        harness.app.scratch_override = Some(root.clone());
    } else {
        harness.app.asset_root_override = Some(root.clone());
    }
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Oak")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(oak)));
    let graph = &harness.app().nature.graph;
    // Nothing is left as a hidden built-in: every material node points at a file in the project.
    assert!(!graph
        .nodes
        .iter()
        .any(|n| matches!(n.op, "mat_library" | "mat_cards" | "mat_solid")));
    let materials: Vec<_> = graph
        .nodes
        .iter()
        .filter(|n| n.op == "mat_project")
        .collect();
    assert!(materials.len() >= 2, "trunk and leaves both have one");
    for node in &materials {
        assert!(root.join(&node.strings[0]).is_file(), "{}", node.strings[0]);
    }
    // The leaf picture stays an atlas and the picture is a real PNG beside its .mat.
    assert!(materials.iter().any(|n| n.params == vec![4.0, 2.0]));
    let textures = crate::uikit::asset_folders::resolve(
        &root,
        crate::uikit::asset_folders::AssetFolder::NatureTextures,
    );
    assert!(std::fs::read_dir(textures).unwrap().any(|e| e
        .unwrap()
        .path()
        .extension()
        .is_some_and(|x| x == "png")));
    assert!(harness
        .app
        .nature
        .graph
        .build(harness.app.nature.quality)
        .is_ok());
}

#[test]
fn nature_parts_can_use_your_own_materials_and_textures() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("nature-own-assets");
    std::fs::create_dir_all(&root).expect("dir");
    harness.app.asset_root_override = Some(root.clone());
    // Your own material (a .mat saved by the engine) and your own cut-out image.
    let mat = root.join("my wet stone.mat");
    {
        let engine = harness.app.engine.as_mut().expect("engine");
        let mut material = engine.new_material().unwrap();
        material.albedo = [0.1, 0.4, 0.8, 1.0];
        engine
            .save_material(&mat.to_string_lossy(), &material)
            .unwrap();
    }
    let png = root.join("my_grass.png");
    RgbaImage::from_fn(32, 32, |x, _| {
        Rgba([40, 160, 40, if (8..24).contains(&x) { 255 } else { 0 }])
    })
    .save(&png)
    .unwrap();

    // Rock with your material, grass with your PNG.
    let rock = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Boulder")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(rock)));
    let part = harness
        .app()
        .nature
        .graph
        .nodes
        .iter()
        .find(|n| n.op == "part")
        .unwrap()
        .id;
    use_project_asset(&mut harness, part, "mat_project", &mat.to_string_lossy());
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let status = harness.app().nature.status.clone().unwrap_or_default();
    assert!(status.starts_with("Placed Boulder"), "{status}");
    let group = harness.app().entities[harness.app().selected].id;
    let engine = harness.app.engine.as_mut().expect("engine");
    let child = (0..engine.entity_count())
        .filter_map(|i| engine.entity_at(i).ok())
        .find(|info| engine.entity_parent(info.id).ok().flatten() == Some(group))
        .expect("a child part")
        .id;
    assert_eq!(
        engine.entity_material(child).unwrap(),
        mat.to_string_lossy(),
        "your .mat is applied as it is"
    );
    assert!(
        !root
            .join("Materials/Nature")
            .join("project_my_wet_stone.mat")
            .exists(),
        "your material is never copied or rebaked"
    );

    let grass = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Grass Clump")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(grass)));
    let part = harness
        .app()
        .nature
        .graph
        .nodes
        .iter()
        .find(|n| n.op == "part")
        .unwrap()
        .id;
    use_project_asset(&mut harness, part, "mat_texture", &png.to_string_lossy());
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let status = harness.app().nature.status.clone().unwrap_or_default();
    assert!(
        status.starts_with("Placed Grass Clump") && !status.contains("problems"),
        "{status}"
    );
    // The file name carries the wind profile of the part (blades sway and let light through).
    let wrapped = std::fs::read_dir(crate::uikit::asset_folders::resolve(
        &root,
        crate::uikit::asset_folders::AssetFolder::NatureMaterials,
    ))
    .unwrap()
    .filter_map(|entry| entry.ok().map(|entry| entry.path()))
    .find(|path| {
        path.file_name()
            .and_then(|name| name.to_str())
            .is_some_and(|name| name.starts_with("project_my_grass") && name.ends_with(".mat"))
    })
    .expect("a cut-out material is made around your image");
    let text = std::fs::read_to_string(wrapped).unwrap();
    assert!(
        text.contains("my_grass.png") && text.contains("alpha_mode"),
        "{text}"
    );
    assert!(
        text.contains("wind_strength 0.25") && text.contains("translucency 0.45"),
        "blades sway and are translucent: {text}"
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn photoreal_rock_places_with_a_generated_texture_set() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("nature-surface");
    harness.app.asset_root_override = Some(root.clone());
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    {
        let id = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(id, [0.0, 8.0, 0.0]).unwrap();
        engine.set_rotation(id, [-42.0, 140.0, 0.0]).unwrap();
    }
    let rock = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Photoreal Rock")
        .unwrap();
    let other = std::env::var("DOPPIO_NATURE_PRESET").ok();
    let rock = other
        .as_deref()
        .and_then(|n| {
            harness
                .app()
                .nature
                .presets
                .iter()
                .position(|p| p.name == n)
        })
        .unwrap_or(rock);
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(rock)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let status = harness.app().nature.status.clone().unwrap_or_default();
    assert!(
        status.starts_with("Placed ") && !status.contains("problems"),
        "{status}"
    );
    let materials = crate::uikit::asset_folders::resolve(
        &root,
        crate::uikit::asset_folders::AssetFolder::NatureMaterials,
    );
    let textures = crate::uikit::asset_folders::resolve(
        &root,
        crate::uikit::asset_folders::AssetFolder::NatureTextures,
    );
    let names: Vec<String> = [&materials, &textures]
        .iter()
        .flat_map(|dir| std::fs::read_dir(dir).unwrap())
        .filter_map(|e| e.ok()?.file_name().into_string().ok())
        .collect();
    for suffix in ["_albedo.png", "_normal.png", "_orm.png", ".mat"] {
        if other.is_some() {
            break;
        }
        assert!(
            names
                .iter()
                .any(|n| n.starts_with("rock_") && n.ends_with(suffix)),
            "{suffix}: {names:?}"
        );
    }
    let engine = harness.app.engine.as_mut().expect("engine");
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.7,
            pitch: 0.32,
            distance: std::env::var("DOPPIO_NATURE_DIST")
                .ok()
                .and_then(|v| v.parse().ok())
                .unwrap_or(3.1),
            focus: [
                0.0,
                std::env::var("DOPPIO_NATURE_FOCUS_Y")
                    .ok()
                    .and_then(|v| v.parse().ok())
                    .unwrap_or(0.7),
                0.0,
            ],
        })
        .unwrap();
    let frame = render_entity_view_sized(engine, 960, 540);
    if let Ok(dir) = std::env::var("DOPPIO_NATURE_SURFACE_DUMP") {
        image::save_buffer(
            format!(
                "{dir}/render_{}.png",
                other.as_deref().unwrap_or("surface").replace(' ', "_")
            ),
            &frame,
            960,
            540,
            image::ColorType::Rgba8,
        )
        .unwrap();
    }
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn water_generator_is_a_node_graph_tab_with_a_live_preview() {
    use crate::uikit::panels::water_generator as wg;
    let mut harness = Harness::empty();
    harness.dispatch(Message::ShowWaterGenerator);
    assert!(
        harness
            .app()
            .panes
            .iter()
            .any(|(_, g)| g.active_kind() == PaneKind::WaterGenerator),
        "the tab comes to the front"
    );
    for widget in [
        id::WATERGEN_CANVAS,
        id::WATERGEN_PREVIEW,
        id::WATERGEN_PRESET,
        id::WATERGEN_PLACE,
        id::WATERGEN_EXPORT,
        id::WATERGEN_PLAY,
    ] {
        assert!(harness.has(Id::from(widget.clone())), "missing {widget:?}");
    }
    assert!(harness.app().watergen.preview.is_some());
    let before = harness.app().watergen.stats.clone();

    // A parameter regenerates the water; the preset list replaces the graph.
    let wind = harness
        .app()
        .watergen
        .graph
        .nodes
        .iter()
        .find(|n| n.op == "wind")
        .unwrap()
        .id;
    let index = crate::uikit::water::find("wind")
        .unwrap()
        .params
        .iter()
        .position(|p| p.key == "count")
        .unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::SetParam(wind, index, 3.0)));
    assert_ne!(
        harness.app().watergen.stats,
        before,
        "{}",
        harness.app().watergen.stats
    );

    // Nodes can be added and wired; port colours are enforced; undo and redo work.
    let nodes = harness.app().watergen.graph.nodes.len();
    harness.dispatch(Message::WaterGen(wg::Edit::AddNode("ripples")));
    assert_eq!(harness.app().watergen.graph.nodes.len(), nodes + 1);
    let look = harness
        .app()
        .watergen
        .graph
        .nodes
        .iter()
        .find(|n| n.op == "look")
        .unwrap()
        .id;
    let ripples = harness.app().watergen.selected.unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::Connect {
        from: look,
        output: 0,
        to: ripples,
        input: 0,
    }));
    assert!(harness
        .app()
        .watergen
        .status
        .as_deref()
        .unwrap_or("")
        .contains("cannot connect"));
    harness.dispatch(Message::WaterGen(wg::Edit::Undo));
    assert_eq!(harness.app().watergen.graph.nodes.len(), nodes);
    harness.dispatch(Message::WaterGen(wg::Edit::Redo));
    assert_eq!(harness.app().watergen.graph.nodes.len(), nodes + 1);
    let output = harness.app().watergen.graph.output_node().unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::DeleteNode(output)));
    assert!(harness.app().watergen.graph.node(output).is_some());

    let storm = harness
        .app()
        .watergen
        .presets
        .iter()
        .position(|p| p.name == "Storm")
        .unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::ApplyPreset(storm)));
    assert_eq!(harness.app().watergen.name, "Storm");
    let storm_waves = harness.app().watergen.water.as_ref().unwrap().waves.len();
    assert!(storm_waves >= 12, "{storm_waves}");

    // The preview animates.
    let first = harness.app().watergen.preview.clone();
    harness.app.watergen.time = 3.0;
    harness.app.watergen.redraw();
    assert_ne!(
        format!("{first:?}"),
        format!("{:?}", harness.app().watergen.preview)
    );
}

#[test]
fn water_presets_write_their_material_and_keep_your_edits() {
    use crate::uikit::panels::water_generator as wg;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("water-materialized");
    harness.app.asset_root_override = Some(root.clone());
    let lake = harness
        .app()
        .watergen
        .presets
        .iter()
        .position(|p| p.name == "Calm Lake")
        .unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::ApplyPreset(lake)));
    // The material exists as a file as soon as the preset is applied, before anything is placed.
    let file = root.join("Water/calm_lake.mat");
    assert!(file.is_file());
    let graph = &harness.app().watergen.graph;
    assert!(graph
        .look_asset()
        .is_some_and(|p| p.ends_with("Water/calm_lake.mat")));
    // Your edits to the file survive applying the preset again.
    std::fs::write(&file, "edited by the user").unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::ApplyPreset(lake)));
    assert_eq!(
        std::fs::read_to_string(&file).unwrap(),
        "edited by the user"
    );
    // The reference round-trips through the saved graph text.
    let text = crate::uikit::water::to_text("Calm Lake", &harness.app().watergen.graph);
    let (_, back) = crate::uikit::water::from_text(&text).unwrap();
    assert_eq!(back.look_asset(), harness.app().watergen.graph.look_asset());
}

#[test]
fn placing_water_creates_an_animated_engine_surface() {
    use crate::uikit::panels::water_generator as wg;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let engine = harness.app.engine.as_ref().expect("engine");
    if engine.capi_version() < 7 {
        return;
    }
    let root = test_temp_root("water-place");
    harness.app.asset_root_override = Some(root.clone());
    let sea = harness
        .app()
        .watergen
        .presets
        .iter()
        .position(|p| p.name == "Open Sea")
        .unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::ApplyPreset(sea)));
    let before = harness.app().entities.len();
    harness.dispatch(Message::WaterGen(wg::Edit::Place));
    let status = harness.app().watergen.status.clone().unwrap_or_default();
    assert!(status.starts_with("Placed Open Sea"), "{status}");
    assert_eq!(harness.app().entities.len(), before + 1);
    let id = harness.app().entities[harness.app().selected].id;
    // The colour the preset's Water Material node had when it was turned into the asset.
    let look = {
        let graph = harness.app().watergen.presets[sea].graph();
        let node = graph.nodes.iter().find(|n| n.op == "look").unwrap();
        [
            node.params[0],
            node.params[1],
            node.params[2],
            node.params[3],
        ]
    };
    // After placing, the node is the project's material, not a hidden built-in.
    let graph = &harness.app().watergen.graph;
    assert!(graph.nodes.iter().any(|n| n.op == "look_asset"));
    assert!(!graph.nodes.iter().any(|n| n.op == "look"));
    let engine = harness.app.engine.as_mut().expect("engine");
    let water = engine.water(id).expect("a water component");
    assert_eq!(water.size, [300.0, 300.0]);
    assert!(water.waves.len() >= 8);
    // The water's material is a real project asset assigned to the entity, written from the
    // Water Material node: it is not hidden in the surface.
    let material = engine.entity_material(id).expect("a material");
    assert!(material.ends_with("Water/open_sea.mat"), "{material}");
    let file = root.join("Water/open_sea.mat");
    let text = std::fs::read_to_string(&file).expect("the .mat exists");
    assert!(
        text.contains("clearcoat") || text.contains("transmission"),
        "{text}"
    );
    let saved = engine
        .load_material(&file.to_string_lossy())
        .expect("loads");
    assert!((saved.albedo[2] - look[2]).abs() < 0.01 && (saved.albedo[3] - look[3]).abs() < 0.01);
    assert_eq!(saved.alpha_mode, doppio_engine::MaterialAlphaMode::Blend);
    // Ticking builds the surface; the waves move the height over time.
    for _ in 0..3 {
        engine.tick(0.05);
    }
    let h0 = engine.water_height(id, 5.0, 5.0).expect("height");
    for _ in 0..40 {
        engine.tick(0.05);
    }
    let h1 = engine.water_height(id, 5.0, 5.0).expect("height");
    assert!(h0.abs() < 3.0 && h1.abs() < 3.0);
    assert!((h0 - h1).abs() > 1e-4, "the surface animates: {h0} vs {h1}");

    // Saving writes the graph, which reloads to the same water.
    harness.dispatch(Message::WaterGen(wg::Edit::Export));
    let file = root.join("Water/open_sea.generator.water");
    let text = crate::uikit::recipe::read(&file).expect("saved").graph;
    let (name, graph) = crate::uikit::water::from_text(&text).expect("parses");
    assert_eq!(name, "Open Sea");
    assert_eq!(
        graph.build().unwrap().waves,
        harness.app().watergen.water.as_ref().unwrap().waves
    );
    std::fs::remove_dir_all(root).ok();
}

/// Renders every water preset in the engine to `DOPPIO_WATER_DUMP` (documentation images).
#[test]
fn water_presets_render_in_the_engine() {
    use crate::uikit::panels::water_generator as wg;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 7 {
        return;
    }
    let Ok(dir) = std::env::var("DOPPIO_WATER_DUMP") else {
        return;
    };
    let root = test_temp_root("water-render");
    harness.app.asset_root_override = Some(root.clone());
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    {
        let id = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(id, [0.0, 8.0, 0.0]).unwrap();
        engine.set_rotation(id, [-35.0, 140.0, 0.0]).unwrap();
    }
    let count = harness.app().watergen.presets.len();
    for index in 0..count {
        let name = harness.app().watergen.presets[index].name;
        harness.dispatch(Message::WaterGen(wg::Edit::ApplyPreset(index)));
        harness.dispatch(Message::WaterGen(wg::Edit::Place));
        let id = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        // Above the editor grid plane, which would otherwise cut the troughs.
        engine.set_position(id, [0.0, 1.0, 0.0]).unwrap();
        for _ in 0..30 {
            engine.tick(0.05);
        }
        let sea = matches!(name, "Open Sea" | "Storm");
        engine
            .set_camera(doppio_engine::Camera {
                yaw: 0.5,
                pitch: if sea { 0.3 } else { 0.4 },
                distance: if sea { 46.0 } else { 12.0 },
                focus: [0.0, 1.0, 0.0],
            })
            .unwrap();
        let frame = render_entity_view_sized(engine, 960, 540);
        image::save_buffer(
            format!("{dir}/{}.png", name.to_lowercase().replace(' ', "_")),
            &frame,
            960,
            540,
            image::ColorType::Rgba8,
        )
        .unwrap();
        engine.destroy_entity(id).ok();
        harness.app.has_water = false;
    }
}

/// The stormy sea used to show large smooth black patches with hard edges: its screen-space
/// reflection hit stale pixels (water writes no depth). The darkest 5 % of the lower half of the
/// frame must stay close to the median brightness.
#[test]
fn storm_water_has_no_dark_reflection_patches() {
    use crate::uikit::panels::water_generator as wg;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 7 {
        return;
    }
    let root = test_temp_root("water-dark-patches");
    harness.app.asset_root_override = Some(root.clone());
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    {
        let id = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(id, [0.0, 8.0, 0.0]).unwrap();
        engine.set_rotation(id, [-35.0, 140.0, 0.0]).unwrap();
    }
    // The open sea first, then the storm: the reflection trace reads the previous frame, which
    // here shows the other water.
    let mut frame = Vec::new();
    for name in ["Open Sea", "Storm"] {
        let index = harness
            .app()
            .watergen
            .presets
            .iter()
            .position(|p| p.name == name)
            .unwrap();
        harness.dispatch(Message::WaterGen(wg::Edit::ApplyPreset(index)));
        harness.dispatch(Message::WaterGen(wg::Edit::Place));
        let id = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(id, [0.0, 1.0, 0.0]).unwrap();
        for _ in 0..30 {
            engine.tick(0.05);
        }
        engine
            .set_camera(doppio_engine::Camera {
                yaw: 0.5,
                pitch: 0.3,
                distance: 46.0,
                focus: [0.0, 1.0, 0.0],
            })
            .unwrap();
        frame = render_entity_view_sized(engine, 960, 540);
        engine.destroy_entity(id).ok();
        harness.app.has_water = false;
    }
    let mut lumas: Vec<f32> = frame
        .as_chunks::<4>()
        .0
        .iter()
        .skip(960 * 300)
        .map(|p| 0.2126 * p[0] as f32 + 0.7152 * p[1] as f32 + 0.0722 * p[2] as f32)
        .collect();
    lumas.sort_by(|a, b| a.total_cmp(b));
    let median = lumas[lumas.len() / 2];
    let p5 = lumas[lumas.len() / 20];
    assert!(median > 30.0, "the sea should be lit, median {median}");
    assert!(
        p5 / median > 0.72,
        "dark patches on the water: p5 {p5} vs median {median}"
    );
}

/// A dense rock for the Micromesh tests: a latitude / longitude sphere pushed in and out by layered
/// noise, written as an OBJ with normals and UVs (the first and last column are duplicated, a real UV
/// seam). Returns the triangle count.
fn write_rock_obj(path: &std::path::Path, segments: usize, rings: usize) -> usize {
    write_rock_obj_with(path, segments, rings, false, false)
}

/// The same rock, optionally with two materials (`usemtl`: the northern and the southern half) and a vertex
/// colour (`v x y z r g b a`: a tint that fades with latitude).
fn write_rock_obj_with(
    path: &std::path::Path,
    segments: usize,
    rings: usize,
    two_materials: bool,
    colours: bool,
) -> usize {
    fn hash(x: i32, y: i32, z: i32) -> f32 {
        let mut h = (x as u32)
            .wrapping_mul(374_761_393)
            .wrapping_add((y as u32).wrapping_mul(668_265_263))
            .wrapping_add((z as u32).wrapping_mul(2_147_483_647));
        h = (h ^ (h >> 13)).wrapping_mul(1_274_126_177);
        ((h ^ (h >> 16)) & 0xFFFF) as f32 / 65_535.0
    }
    fn noise(p: [f32; 3]) -> f32 {
        let [x, y, z] = p;
        let (xi, yi, zi) = (x.floor() as i32, y.floor() as i32, z.floor() as i32);
        let (fx, fy, fz) = (x - xi as f32, y - yi as f32, z - zi as f32);
        let s = |t: f32| t * t * (3.0 - 2.0 * t);
        let (sx, sy, sz) = (s(fx), s(fy), s(fz));
        let mix = |a: f32, b: f32, t: f32| a + (b - a) * t;
        let mut c = [[0.0f32; 2]; 2];
        for (dy, row) in c.iter_mut().enumerate() {
            for (dz, cell) in row.iter_mut().enumerate() {
                let (yy, zz) = (yi + dy as i32, zi + dz as i32);
                *cell = mix(hash(xi, yy, zz), hash(xi + 1, yy, zz), sx);
            }
        }
        mix(mix(c[0][0], c[1][0], sy), mix(c[0][1], c[1][1], sy), sz)
    }
    let tau = std::f32::consts::TAU;
    let point = |i: usize, j: usize| -> [f32; 3] {
        let phi = (i % segments) as f32 / segments as f32 * tau;
        let theta = j as f32 / rings as f32 * std::f32::consts::PI;
        let d = [
            theta.sin() * phi.cos(),
            theta.cos(),
            theta.sin() * phi.sin(),
        ];
        let n = noise([d[0] * 1.6, d[1] * 1.6, d[2] * 1.6]) * 0.55
            + noise([d[0] * 3.7, d[1] * 3.7, d[2] * 3.7]) * 0.28
            + noise([d[0] * 9.0, d[1] * 9.0, d[2] * 9.0]) * 0.12;
        let r = 0.75 + n * 0.5;
        [d[0] * r, d[1] * r, d[2] * r]
    };
    let columns = segments + 1;
    let index = |i: usize, j: usize| j * columns + i;
    let mut positions = Vec::with_capacity(columns * (rings + 1));
    for j in 0..=rings {
        for i in 0..=segments {
            positions.push(point(i, j));
        }
    }
    // Smooth normals: accumulate the face normals of every triangle on the wrapped grid.
    let mut normals = vec![[0.0f32; 3]; positions.len()];
    let mut triangles: Vec<[usize; 3]> = Vec::new();
    for j in 0..rings {
        for i in 0..segments {
            triangles.push([index(i, j), index(i + 1, j), index(i, j + 1)]);
            triangles.push([index(i + 1, j), index(i + 1, j + 1), index(i, j + 1)]);
        }
    }
    let wrap = |v: usize| -> usize {
        let (i, j) = (v % columns, v / columns);
        index(i % segments, j)
    };
    let mut accumulated = vec![[0.0f32; 3]; positions.len()];
    for t in &triangles {
        let (a, b, c) = (positions[t[0]], positions[t[1]], positions[t[2]]);
        let e1 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
        let e2 = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
        let n = [
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0],
        ];
        for &v in t {
            let w = wrap(v);
            for k in 0..3 {
                accumulated[w][k] += n[k];
            }
        }
    }
    for (v, normal) in normals.iter_mut().enumerate() {
        let a = accumulated[wrap(v)];
        let l = (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]).sqrt().max(1e-9);
        *normal = [a[0] / l, a[1] / l, a[2] / l];
        // Pole rows: every vertex is the same point.
        let j = v / columns;
        if j == 0 {
            *normal = [0.0, 1.0, 0.0];
        } else if j == rings {
            *normal = [0.0, -1.0, 0.0];
        }
    }
    let mut obj = String::from("# rock\n");
    for (v, p) in positions.iter().enumerate() {
        if colours {
            let latitude = (v / columns) as f32 / rings as f32;
            obj.push_str(&format!(
                "v {} {} {} {} {} {} 1\n",
                p[0],
                p[1],
                p[2],
                1.0 - 0.6 * latitude,
                0.5 + 0.4 * latitude,
                0.9 - 0.5 * latitude
            ));
        } else {
            obj.push_str(&format!("v {} {} {}\n", p[0], p[1], p[2]));
        }
    }
    for j in 0..=rings {
        for i in 0..=segments {
            obj.push_str(&format!(
                "vt {} {}\n",
                i as f32 / segments as f32,
                1.0 - j as f32 / rings as f32
            ));
        }
    }
    for n in &normals {
        obj.push_str(&format!("vn {} {} {}\n", n[0], n[1], n[2]));
    }
    let mut count = 0;
    let mut material = 0;
    if two_materials {
        obj.push_str("usemtl north\n");
    }
    for t in &triangles {
        // The two triangles touching a pole row have a zero-area edge: skip the degenerate ones.
        let (a, b, c) = (positions[t[0]], positions[t[1]], positions[t[2]]);
        let e1 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
        let e2 = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
        let n = [
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0],
        ];
        if n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-14 {
            continue;
        }
        if two_materials && material == 0 && t[0] / columns >= rings / 2 {
            obj.push_str("usemtl south\n");
            material = 1;
        }
        obj.push_str(&format!(
            "f {0}/{0}/{0} {1}/{1}/{1} {2}/{2}/{2}\n",
            t[0] + 1,
            t[1] + 1,
            t[2] + 1
        ));
        count += 1;
    }
    std::fs::write(path, obj).unwrap();
    count
}

/// Places a rock entity (Mesh Filter with the OBJ, Mesh Renderer with the material and, optionally, the
/// Micromesh switch on).
fn place_rock(
    engine: &mut doppio_engine::Engine,
    mesh: &str,
    material: &str,
    position: [f32; 3],
    scale: f32,
    micromesh: bool,
) -> (u32, u32) {
    let id = engine.create_entity("Rock").expect("entity");
    engine.set_position(id, position).unwrap();
    engine.set_scale(id, [scale, scale, scale]).unwrap();
    let filter = add_component(engine, id, "Mesh Filter").expect("Mesh Filter");
    let primitive = super::enum_field(engine, "Mesh Filter", "Primitive", "Custom").expect("enum");
    set_component_field(engine, id, filter, "Primitive", primitive).expect("primitive");
    set_component_field(
        engine,
        id,
        filter,
        "Mesh",
        doppio_engine::FieldValue::String(mesh.to_owned()),
    )
    .expect("mesh");
    let renderer = add_component(engine, id, "Mesh Renderer").expect("Mesh Renderer");
    set_component_field(
        engine,
        id,
        renderer,
        "Material",
        doppio_engine::FieldValue::String(material.to_owned()),
    )
    .expect("material");
    if micromesh {
        set_component_field(
            engine,
            id,
            renderer,
            "Micromesh",
            doppio_engine::FieldValue::Bool(true),
        )
        .expect("micromesh");
    }
    (id, renderer)
}

/// The scene renderer draws a mesh through the Micromesh path (cluster cut selected on the GPU, drawn
/// into the same G-buffer through the lit shader) and the picture matches the plain mesh's.
#[test]
fn micromesh_draw_matches_the_plain_mesh_and_submits_fewer_triangles() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    {
        let engine = harness.app.engine.as_ref().expect("engine");
        if engine.capi_version() < 14 {
            return;
        }
    }
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    let light_id = harness.app().entities[harness.app().selected].id;
    let root = test_temp_root("micromesh-scene");
    std::fs::create_dir_all(&root).unwrap();
    let mesh = root.join("rock.obj");
    let source = write_rock_obj(&mesh, 192, 120);
    assert!(source > 40_000, "a dense rock, got {source} triangles");
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_rotation(light_id, [-50.0, 35.0, 0.0]).unwrap();
    let material = doppio_engine::Material {
        albedo: [0.5, 0.46, 0.4, 1.0],
        roughness: 0.85,
        ..doppio_engine::Material::default()
    };
    let mat = root.join("rock.mat");
    engine
        .save_material(&mat.to_string_lossy(), &material)
        .unwrap();
    let (rock, renderer) = place_rock(
        engine,
        &mesh.to_string_lossy(),
        &mat.to_string_lossy(),
        [0.0, 0.0, 0.0],
        1.0,
        false,
    );
    // A floor under it, so the sun's shadow of the rock is part of the picture (and of the comparison).
    let plane = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Plane")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(plane));
    let floor = harness.app().entities[harness.app().selected].id;
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_position(floor, [0.0, -0.9, 0.0]).unwrap();
    engine.set_scale(floor, [8.0, 1.0, 8.0]).unwrap();
    engine
        .apply_material(floor, &mat.to_string_lossy())
        .unwrap();
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.7,
            pitch: 0.6,
            distance: 6.5,
            focus: [0.0, -0.4, 0.0],
        })
        .unwrap();
    // The viewport renders without shadow maps unless asked: ask.
    let mut settings = engine.viewport_settings().unwrap();
    settings.shadows = true;
    engine.set_viewport_settings(settings).unwrap();
    let _ = render_entity_view_sized(engine, 480, 360); // settle
    let plain = render_entity_view_sized(engine, 480, 360);
    let plain_stats = engine.render_stats().unwrap();
    if !(plain_stats.indirect_draw
        && plain_stats.graphics_storage_buffers
        && plain_stats.compute_storage_write)
    {
        return; // no GPU path for it
    }
    assert_eq!(plain_stats.micromesh_draws, 0);
    assert!(plain_stats.triangles as usize >= source);

    set_component_field(
        engine,
        rock,
        renderer,
        "Micromesh",
        doppio_engine::FieldValue::Bool(true),
    )
    .unwrap();
    let _ = render_entity_view_sized(engine, 480, 360); // builds the cluster hierarchy
    let micro = render_entity_view_sized(engine, 480, 360);
    let micro_stats = engine.render_stats().unwrap();
    assert_eq!(
        micro_stats.micromesh_draws, 1,
        "the rock draws as a Micromesh"
    );
    let frame = engine.micromesh_frame_stats().unwrap();
    assert_eq!(frame.draws, 1);
    assert!(frame.triangles > 0);
    // The camera and at least one shadow cascade were cut, into one arena, and everything fit.
    assert!(frame.views >= 2, "views {}", frame.views);
    assert!(frame.arena_used > 1 && frame.arena_used <= frame.arena_capacity);
    assert_eq!(frame.overflowed, 0);
    assert!(
        frame.triangles * 2 < frame.source_triangles,
        "the cut keeps {} of {} triangles",
        frame.triangles,
        frame.source_triangles
    );
    // The viewport HUD says so: triangles, draws and the Micromesh draws.
    let label = render_stats_label(engine);
    assert!(
        label.contains("tris") && label.contains("1 micromesh"),
        "HUD: {label:?}"
    );
    // The same picture: mean absolute difference per channel stays small.
    let mut sum = 0u64;
    let mut covered = 0u64;
    for (a, b) in plain
        .as_chunks::<4>()
        .0
        .iter()
        .zip(micro.as_chunks::<4>().0.iter())
    {
        let d = (0..3)
            .map(|k| (a[k] as i32 - b[k] as i32).unsigned_abs() as u64)
            .sum::<u64>();
        sum += d;
        covered += 1;
    }
    let mean = sum as f64 / (3.0 * covered as f64);
    assert!(
        mean < 2.5,
        "micromesh vs plain: mean difference {mean} / 255"
    );
    // Switching it off draws the plain mesh again.
    set_component_field(
        engine,
        rock,
        renderer,
        "Micromesh",
        doppio_engine::FieldValue::Bool(false),
    )
    .unwrap();
    let _ = render_entity_view_sized(engine, 480, 360);
    assert_eq!(engine.render_stats().unwrap().micromesh_draws, 0);
    assert!(!render_stats_label(engine).contains("micromesh"));
}

#[test]
fn hud_counts_are_compact() {
    assert_eq!(compact_count(0), "0");
    assert_eq!(compact_count(999), "999");
    assert_eq!(compact_count(1_234), "1.2k");
    assert_eq!(compact_count(610_337), "610.3k");
    assert_eq!(compact_count(5_600_000), "5.6M");
}

/// What the scene path adds beyond a plain rock: a non-uniform scale (normals use the inverse transpose, the
/// cull uses the largest scale), a mesh with two materials (one hierarchy per sub-mesh) and a vertex colour.
/// Each draws like its plain mesh does.
#[test]
fn micromesh_handles_non_uniform_scale_submeshes_and_vertex_colours() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 14 {
        return;
    }
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    let light_id = harness.app().entities[harness.app().selected].id;
    let root = test_temp_root("micromesh-variants");
    std::fs::create_dir_all(&root).unwrap();
    let plain_obj = root.join("rock.obj");
    let two_obj = root.join("rock_two.obj");
    let coloured_obj = root.join("rock_coloured.obj");
    write_rock_obj(&plain_obj, 160, 100);
    write_rock_obj_with(&two_obj, 160, 100, true, false);
    write_rock_obj_with(&coloured_obj, 160, 100, false, true);
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_rotation(light_id, [-50.0, 35.0, 0.0]).unwrap();
    let material = doppio_engine::Material {
        albedo: [0.55, 0.5, 0.45, 1.0],
        roughness: 0.8,
        ..doppio_engine::Material::default()
    };
    let mat = root.join("rock.mat");
    engine
        .save_material(&mat.to_string_lossy(), &material)
        .unwrap();
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.6,
            pitch: 0.3,
            distance: 4.2,
            focus: [0.0, 0.0, 0.0],
        })
        .unwrap();
    let _ = render_entity_view_sized(engine, 400, 300); // settle the device
    let cases: [(&str, &std::path::Path, [f32; 3]); 3] = [
        ("non-uniform scale", &plain_obj, [1.0, 1.5, 0.7]),
        ("two materials", &two_obj, [1.0, 1.0, 1.0]),
        ("vertex colours", &coloured_obj, [1.0, 1.0, 1.0]),
    ];
    for (label, obj, scale) in cases {
        let (id, renderer) = place_rock(
            engine,
            &obj.to_string_lossy(),
            &mat.to_string_lossy(),
            [0.0, 0.0, 0.0],
            1.0,
            false,
        );
        engine.set_scale(id, scale).unwrap();
        let _ = render_entity_view_sized(engine, 400, 300);
        let plain = render_entity_view_sized(engine, 400, 300);
        set_component_field(
            engine,
            id,
            renderer,
            "Micromesh",
            doppio_engine::FieldValue::Bool(true),
        )
        .unwrap();
        let _ = render_entity_view_sized(engine, 400, 300); // builds the hierarchy
        let micro = render_entity_view_sized(engine, 400, 300);
        let stats = engine.render_stats().unwrap();
        let frame = engine.micromesh_frame_stats().unwrap();
        assert_eq!(stats.micromesh_draws, 1, "{label}: drawn as a Micromesh");
        assert!(
            frame.triangles > 0 && frame.triangles < frame.source_triangles,
            "{label}: {frame:?}"
        );
        assert_eq!(frame.overflowed, 0, "{label}");
        let mut sum = 0u64;
        for (a, b) in plain
            .as_chunks::<4>()
            .0
            .iter()
            .zip(micro.as_chunks::<4>().0.iter())
        {
            sum += (0..3)
                .map(|k| (a[k] as i32 - b[k] as i32).unsigned_abs() as u64)
                .sum::<u64>();
        }
        let mean = sum as f64 / (3.0 * (plain.len() / 4) as f64);
        assert!(
            mean < 3.0,
            "{label}: micromesh vs plain, mean difference {mean} / 255"
        );
        engine.destroy_entity(id).ok();
    }
}

/// Placing a Nature model with Merge parts on: the parts that share a kind of surface become one entity with a mesh
/// that has a sub-mesh per part and the Mesh Filter's Material slots in the same order; off, every part is its own
/// entity. Micromesh is switched on for a dense solid part and not for cut-out cards.
#[test]
fn nature_places_parts_as_one_entity_with_material_slots_and_micromesh() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("nature-slots");
    harness.app.asset_root_override = Some(root.clone());
    let preset = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Pink Flower")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(preset)));
    let count_children = |harness: &mut Harness| {
        let group = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        let kids: Vec<u32> = (0..engine.entity_count())
            .filter_map(|i| engine.entity_at(i).ok())
            .filter(|info| engine.entity_parent(info.id).ok().flatten() == Some(group))
            .map(|info| info.id)
            .collect();
        kids
    };
    let slots_of = |engine: &doppio_engine::Engine, id: u32| -> String {
        let data = engine.entity_component_data(id);
        let filter = data
            .iter()
            .find(|c| c.info.name == "Mesh Filter")
            .expect("filter");
        // Primitive, Mesh, Texture, Normal map, Material, Material slots, Shininess.
        match filter.values.get(5) {
            Some(doppio_engine::FieldValue::String(text)) => text.clone(),
            other => panic!("Material slots is the filter's sixth field, got {other:?}"),
        }
    };

    harness.dispatch(Message::NatureGen(ng::Edit::SetMergeParts(false)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let separate = count_children(&mut harness);
    assert!(
        separate.len() >= 3,
        "a flower has several parts: {}",
        separate.len()
    );

    harness.dispatch(Message::NatureGen(ng::Edit::SetMergeParts(true)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let merged = count_children(&mut harness);
    assert!(
        merged.len() < separate.len(),
        "merged {} vs separate {}: {:?}",
        merged.len(),
        separate.len(),
        harness.app().nature.status
    );
    let engine = harness.app.engine.as_ref().expect("engine");
    let with_slots: Vec<String> = merged
        .iter()
        .map(|&id| slots_of(engine, id))
        .filter(|text| text.contains(';'))
        .collect();
    assert!(
        !with_slots.is_empty(),
        "a merged entity lists a material per part: {:?}",
        merged
            .iter()
            .map(|&id| slots_of(engine, id))
            .collect::<Vec<_>>()
    );
    for text in &with_slots {
        assert!(
            text.split(';').all(|slot| slot.ends_with(".mat")),
            "every slot is a material: {text}"
        );
    }
    // The merged mesh is on disk with one group per slot.
    let models = crate::uikit::asset_folders::resolve(
        &root,
        crate::uikit::asset_folders::AssetFolder::NatureModels,
    );
    let merged_files: Vec<String> = std::fs::read_dir(&models)
        .unwrap()
        .filter_map(|e| e.ok())
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| n.ends_with("_solid.obj") || n.ends_with("_foliage.obj"))
        .collect();
    assert!(!merged_files.is_empty(), "{merged_files:?}");
    let groups: usize = merged_files
        .iter()
        .map(|n| {
            std::fs::read_to_string(models.join(n))
                .unwrap()
                .lines()
                .filter(|l| l.starts_with("usemtl "))
                .count()
        })
        .sum();
    assert_eq!(
        groups,
        with_slots
            .iter()
            .map(|t| t.split(';').count())
            .sum::<usize>()
    );
}

/// Micromesh on placing: a dense solid part (a detailed boulder) gets the Mesh Renderer's Micromesh switch, a small one
/// and the switch off do not.
#[test]
fn nature_switches_micromesh_on_for_dense_solid_parts() {
    use crate::uikit::nature::Quality;
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    harness.app.asset_root_override = Some(test_temp_root("nature-micromesh"));
    let boulder = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Boulder")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(boulder)));
    harness.dispatch(Message::NatureGen(ng::Edit::SetQuality(
        Quality::ALL
            .iter()
            .position(|q| *q == Quality::Detailed)
            .unwrap(),
    )));
    let triangles = harness
        .app()
        .nature
        .graph
        .build(Quality::Detailed)
        .unwrap()
        .triangles();
    assert!(
        triangles >= 3000,
        "the detailed boulder is dense: {triangles}"
    );
    let micromesh_of_first_part = |harness: &mut Harness| -> bool {
        let group = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_ref().expect("engine");
        let part = (0..engine.entity_count())
            .filter_map(|i| engine.entity_at(i).ok())
            .find(|info| engine.entity_parent(info.id).ok().flatten() == Some(group))
            .expect("a part")
            .id;
        let data = engine.entity_component_data(part);
        let renderer = data
            .iter()
            .find(|c| c.info.name == "Mesh Renderer")
            .expect("renderer");
        // Mesh, Material, Cast shadows, Receive shadows, Tint, Wetness, Snow, Age, Micromesh.
        matches!(
            renderer.values.get(8),
            Some(doppio_engine::FieldValue::Bool(true))
        )
    };
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    assert!(
        micromesh_of_first_part(&mut harness),
        "dense rock: Micromesh on"
    );
    harness.dispatch(Message::NatureGen(ng::Edit::SetMicromesh(false)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    assert!(
        !micromesh_of_first_part(&mut harness),
        "switch off: no Micromesh"
    );
}

/// Documentation media of the material slots (env-gated: `DOPPIO_SLOTS_DOCS=<dir>`; see
/// `scripts/render-material-slots-docs.sh`): one two-group rock drawn with its slots in both orders, and a Nature model
/// placed with Merge parts off and on (entity counts and a lit picture of the merged one).
#[test]
fn material_slots_pictures_for_docs() {
    use crate::uikit::panels::nature_generator as ng;
    let Ok(dir) = std::env::var("DOPPIO_SLOTS_DOCS") else {
        return;
    };
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    let light_id = harness.app().entities[harness.app().selected].id;
    harness.dispatch(Message::CreatePrefab(light));
    let fill_id = harness.app().entities[harness.app().selected].id;
    let root = test_temp_root("material-slots-docs");
    std::fs::create_dir_all(&root).unwrap();
    harness.app.asset_root_override = Some(root.clone());
    let obj = root.join("rock_two.obj");
    write_rock_obj_with(&obj, 160, 100, true, false);
    {
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_rotation(light_id, [-48.0, 35.0, 0.0]).unwrap();
        engine.set_rotation(fill_id, [-20.0, 215.0, 0.0]).unwrap();
        if let Some(light_type) = component_type_id(engine, "Light") {
            let _ = set_component_field(
                engine,
                fill_id,
                light_type,
                "Intensity",
                doppio_engine::FieldValue::F32(0.4),
            );
        }
        let mut settings = engine.viewport_settings().unwrap();
        settings.show_grid = false;
        engine.set_viewport_settings(settings).unwrap();
    }
    let engine = harness.app.engine.as_mut().expect("engine");
    let save =
        |engine: &mut doppio_engine::Engine, name: &str, albedo: [f32; 4], roughness: f32| {
            let path = root.join(name);
            let material = doppio_engine::Material {
                albedo,
                roughness,
                ..doppio_engine::Material::default()
            };
            engine
                .save_material(&path.to_string_lossy(), &material)
                .unwrap();
            path.to_string_lossy().into_owned()
        };
    let moss = save(engine, "moss.mat", [0.18, 0.36, 0.12, 1.0], 0.95);
    let stone = save(engine, "stone.mat", [0.46, 0.44, 0.41, 1.0], 0.7);
    let (rock, _) = place_rock(engine, &obj.to_string_lossy(), &stone, [0.0; 3], 1.0, false);
    let filter = component_type_id(engine, "Mesh Filter").expect("Mesh Filter");
    for (name, slots) in [
        ("rock_moss_north", format!("{moss};{stone}")),
        ("rock_moss_south", format!("{stone};{moss}")),
    ] {
        set_component_field(
            engine,
            rock,
            filter,
            "Material slots",
            doppio_engine::FieldValue::String(slots),
        )
        .unwrap();
        engine
            .set_camera(doppio_engine::Camera {
                yaw: 0.6,
                pitch: 0.55,
                distance: 4.0,
                focus: [0.0, 0.0, 0.0],
            })
            .unwrap();
        let _ = render_entity_view_sized(engine, 480, 360);
        let frame = render_entity_view_sized(engine, 480, 360);
        image::save_buffer(
            format!("{dir}/{name}.png"),
            &frame,
            480,
            360,
            image::ColorType::Rgba8,
        )
        .unwrap();
    }
    engine.destroy_entity(rock).ok();

    // A Nature model, placed twice: every part its own entity, then merged.
    let preset = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Pink Flower")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(preset)));
    let children = |harness: &mut Harness| {
        let group = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_ref().expect("engine");
        (0..engine.entity_count())
            .filter_map(|i| engine.entity_at(i).ok())
            .filter(|info| engine.entity_parent(info.id).ok().flatten() == Some(group))
            .map(|info| info.id)
            .collect::<Vec<u32>>()
    };
    harness.dispatch(Message::NatureGen(ng::Edit::SetMergeParts(false)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let separate = children(&mut harness).len();
    let first_group = harness.app().entities[harness.app().selected].id;
    harness
        .app
        .engine
        .as_mut()
        .unwrap()
        .destroy_entity(first_group)
        .ok();
    harness.dispatch(Message::NatureGen(ng::Edit::SetMergeParts(true)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let merged_ids = children(&mut harness);
    let engine = harness.app.engine.as_mut().expect("engine");
    let mut rows = String::new();
    for &id in &merged_ids {
        let data = engine.entity_component_data(id);
        if let Some(filter) = data.iter().find(|c| c.info.name == "Mesh Filter") {
            if let Some(doppio_engine::FieldValue::String(slots)) = filter.values.get(5) {
                rows.push_str(&format!(
                    "{}\t{}\n",
                    engine.entity_name(id).unwrap_or_default(),
                    slots
                ));
            }
        }
    }
    std::fs::write(
        format!("{dir}/nature.txt"),
        format!("separate\t{separate}\nmerged\t{}\n{rows}", merged_ids.len()),
    )
    .unwrap();
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.7,
            pitch: 0.3,
            distance: 1.7,
            focus: [0.0, 0.42, 0.0],
        })
        .unwrap();
    let _ = render_entity_view_sized(engine, 480, 360);
    let frame = render_entity_view_sized(engine, 480, 360);
    image::save_buffer(
        format!("{dir}/nature_merged.png"),
        &frame,
        480,
        360,
        image::ColorType::Rgba8,
    )
    .unwrap();
}

/// Generators work without a project: the first action that needs files gives the engine a scratch project, says so
/// in a notice, and the model is placed. Every status line a generator sets becomes a notice (and a Console entry).
#[test]
fn generators_work_without_a_project_and_say_what_happened() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    assert!(
        harness.app().asset_root_dir().is_none(),
        "the harness starts without a project"
    );
    let scratch = harness.app().scratch_root();
    let _ = std::fs::remove_dir_all(&scratch);
    let before = harness.app().entities.len();
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    assert!(
        harness.app().entities.len() > before,
        "Place worked: {:?}",
        harness.app().nature.status
    );
    assert!(
        scratch.join("Models/Nature").is_dir() || scratch.join("MODELS/Nature").is_dir(),
        "the model files are in the scratch project"
    );
    assert_eq!(
        harness.app().asset_root_dir().as_deref(),
        Some(scratch.as_path()),
        "the scratch project is now the asset root"
    );
    let notice = harness.app().notice.clone().expect("a notice");
    assert!(
        notice.text.starts_with("Placed Oak"),
        "the last word is the placement: {notice:?}"
    );
    assert_eq!(notice.tone, crate::uikit::theme::ChipTone::Success);
    let console: Vec<String> = harness
        .app()
        .log
        .iter()
        .map(|entry| entry.text.clone())
        .collect();
    assert!(
        console.iter().any(|t| t.contains("scratch folder")),
        "the Console says where the files went: {console:?}"
    );
    // A failing action warns: Scatter on something that is not a terrain.
    harness.dispatch(Message::ScatterOnTerrain(scatter::ScatterRequest::default()));
    let notice = harness.app().notice.clone().expect("a notice");
    assert_eq!(
        notice.tone,
        crate::uikit::theme::ChipTone::Danger,
        "{notice:?}"
    );
    let _ = std::fs::remove_dir_all(&scratch);
}

/// The generators talk to each other: *Add to scatter plan* in the Nature tab writes the model as a composite class
/// (a solid and a foliage mesh with their material slots), and *Scatter* in the Terrain tab plants it, every instance
/// an entity with one child per part.
#[test]
fn nature_models_scatter_from_the_terrain_tab_as_composite_instances() {
    use crate::uikit::panels::nature_generator as ng;
    use crate::uikit::panels::terrain_generator as tg;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 16 {
        return;
    }
    let root = test_temp_root("nature-to-scatter");
    if std::env::var("DOPPIO_OAK_SCRATCH").is_ok() {
        harness.app.scratch_override = Some(root.clone());
    } else {
        harness.app.asset_root_override = Some(root.clone());
    }
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Oak")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(oak)));
    harness.dispatch(Message::NatureGen(ng::Edit::AddToScatter));
    let status = harness.app().nature.status.clone().unwrap_or_default();
    assert!(status.starts_with("Added class 4 (oak)"), "{status}");
    let plan_path = root.join("Scatter/default.scatter");
    let text = std::fs::read_to_string(&plan_path).unwrap();
    let plan = crate::uikit::scatter::ScatterPlan::from_text(&text).expect("the plan parses");
    let class = plan.class(4).expect("the new class");
    assert_eq!(class.parts.len(), 2, "solid and foliage: {:?}", class.parts);
    assert!(class.parts[0].mesh.ends_with("oak_solid.obj"));
    assert!(class.parts[1].mesh.ends_with("oak_foliage.obj") && class.parts[1].cutout);
    assert!(
        class.parts.iter().all(|p| !p.slots.is_empty()),
        "every part names its materials"
    );
    // Plant only the oak (the built-in classes would make the test slow), seeded.
    let oak_block = &text[text.find("class 4").unwrap()..];
    std::fs::write(&plan_path, format!("seed 3\n{oak_block}")).unwrap();

    let alpine = harness
        .app()
        .terraingen
        .presets
        .iter()
        .position(|p| p.name == "Alpine Range")
        .unwrap();
    harness.dispatch(Message::TerrainGen(tg::Edit::ApplyPreset(alpine)));
    harness.dispatch(Message::TerrainGen(tg::Edit::Place));
    let terrain = harness.app().terraingen.placed.expect("the placed terrain");
    // Deselect: the tab falls back to the terrain it placed.
    harness.dispatch(Message::SelectEntity(0));
    harness.dispatch(Message::TerrainGen(tg::Edit::Scatter));
    let status = harness.app().scatter_status.clone().unwrap_or_default();
    assert!(status.starts_with("Scattered "), "{status}");
    let names: Vec<&str> = harness
        .app()
        .entities
        .iter()
        .map(|e| e.name.as_str())
        .collect();
    let trees = names
        .iter()
        .filter(|n| n.starts_with("oak ") && !n.contains("part"))
        .count();
    let first_parts = names.iter().filter(|n| n.ends_with(" part 1")).count();
    let second_parts = names.iter().filter(|n| n.ends_with(" part 2")).count();
    assert!(trees > 5, "an oak forest grew: {trees} ({status})");
    assert_eq!((first_parts, second_parts), (trees, trees));
    assert!(harness
        .app()
        .engine
        .as_ref()
        .is_some_and(|e| e.terrain(terrain).is_ok()));
    let notice = harness.app().notice.clone().expect("a notice");
    assert!(notice.text.starts_with("Scattered "), "{notice:?}");
}

/// The mean colour of the middle of a frame (a rock seen close fills it).
fn centre_colour(pixels: &[u8], width: usize, height: usize) -> [f32; 3] {
    let (x0, x1, y0, y1) = (width * 2 / 5, width * 3 / 5, height * 2 / 5, height * 3 / 5);
    let mut sum = [0.0f32; 3];
    let mut count = 0.0;
    for y in y0..y1 {
        for x in x0..x1 {
            let at = (y * width + x) * 4;
            for k in 0..3 {
                sum[k] += pixels[at + k] as f32;
            }
            count += 1.0;
        }
    }
    sum.map(|v| v / count)
}

/// Material slots: an OBJ with two `usemtl` groups is two sub-meshes, `Material slots` gives each its own `.mat`
/// (red for the northern half, blue for the southern), and it holds with Micromesh on too. Without slots the mesh
/// keeps one material, and a material set only on the Mesh Renderer is drawn (the renderer's fallback).
#[test]
fn material_slots_give_each_submesh_its_own_material() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    let light_id = harness.app().entities[harness.app().selected].id;
    // A second light from below, so the southern half is lit as well.
    harness.dispatch(Message::CreatePrefab(light));
    let under_id = harness.app().entities[harness.app().selected].id;
    let root = test_temp_root("material-slots");
    std::fs::create_dir_all(&root).unwrap();
    let obj = root.join("rock_two.obj");
    write_rock_obj_with(&obj, 96, 64, true, false);
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_rotation(light_id, [-90.0, 0.0, 0.0]).unwrap();
    engine.set_rotation(under_id, [90.0, 0.0, 0.0]).unwrap();
    let save = |engine: &mut doppio_engine::Engine, name: &str, albedo: [f32; 4]| {
        let path = root.join(name);
        let material = doppio_engine::Material {
            albedo,
            roughness: 0.8,
            ..doppio_engine::Material::default()
        };
        engine
            .save_material(&path.to_string_lossy(), &material)
            .unwrap();
        path.to_string_lossy().into_owned()
    };
    let red = save(engine, "red.mat", [0.9, 0.05, 0.05, 1.0]);
    let blue = save(engine, "blue.mat", [0.05, 0.05, 0.9, 1.0]);
    let grey = save(engine, "grey.mat", [0.5, 0.5, 0.5, 1.0]);
    let (id, renderer) = place_rock(engine, &obj.to_string_lossy(), &grey, [0.0; 3], 1.0, false);
    let filter = component_type_id(engine, "Mesh Filter").expect("Mesh Filter");
    let look = |engine: &mut doppio_engine::Engine, pitch: f32| {
        engine
            .set_camera(doppio_engine::Camera {
                yaw: 0.3,
                pitch,
                distance: 3.2,
                focus: [0.0, 0.0, 0.0],
            })
            .unwrap();
        let _ = render_entity_view_sized(engine, 400, 300);
        render_entity_view_sized(engine, 400, 300)
    };
    let colour = |engine: &mut doppio_engine::Engine, pitch: f32| {
        let frame = look(engine, pitch);
        centre_colour(&frame, 400, 300)
    };
    // 1. The Mesh Renderer's material alone is drawn (grey: neither red nor blue dominates).
    let (above, below) = (colour(engine, 1.2), colour(engine, -1.2));
    assert!(
        (above[0] - above[2]).abs() < 12.0 && (below[0] - below[2]).abs() < 12.0,
        "one grey material: {above:?} {below:?}"
    );
    // 2. Slots: red above, blue below, with or without Micromesh.
    set_component_field(
        engine,
        id,
        filter,
        "Material slots",
        doppio_engine::FieldValue::String(format!("{red};{blue}")),
    )
    .unwrap();
    for micromesh in [false, true] {
        set_component_field(
            engine,
            id,
            renderer,
            "Micromesh",
            doppio_engine::FieldValue::Bool(micromesh),
        )
        .unwrap();
        let (above, below) = (colour(engine, 1.2), colour(engine, -1.2));
        assert!(
            above[0] > above[2] + 40.0,
            "micromesh {micromesh}: northern half should be red, got {above:?}"
        );
        assert!(
            below[2] > below[0] + 40.0,
            "micromesh {micromesh}: southern half should be blue, got {below:?}"
        );
        if micromesh {
            assert!(
                engine.render_stats().unwrap().micromesh_draws >= 2,
                "each slot draws as its own Micromesh"
            );
        }
    }
    // 3. An empty slot falls back to the mesh's own material: slot 0 red, slot 1 empty -> the grey one.
    set_component_field(
        engine,
        id,
        filter,
        "Material slots",
        doppio_engine::FieldValue::String(format!("{red};")),
    )
    .unwrap();
    let (above, below) = (colour(engine, 1.2), colour(engine, -1.2));
    assert!(above[0] > above[2] + 40.0, "slot 0 stays red: {above:?}");
    assert!(
        (below[0] - below[2]).abs() < 12.0,
        "the empty slot uses the fallback: {below:?}"
    );
}

/// Micromesh occlusion: last frame's depth, reduced to a farthest-depth pyramid, hides the clusters behind a wall.
/// Six rocks stand on either side of a big wall seen from one side; with the switch on the camera's cut drops the
/// rocks the wall hides and the picture stays the same.
#[test]
fn micromesh_occlusion_hides_what_last_frames_depth_shows_behind_a_wall() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 14 {
        return;
    }
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    let light_id = harness.app().entities[harness.app().selected].id;
    let cube = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Cube")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(cube));
    let wall = harness.app().entities[harness.app().selected].id;
    let root = test_temp_root("micromesh-occlusion");
    std::fs::create_dir_all(&root).unwrap();
    let mesh = root.join("rock.obj");
    write_rock_obj(&mesh, 160, 100);
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_rotation(light_id, [-50.0, 35.0, 0.0]).unwrap();
    let material = doppio_engine::Material {
        albedo: [0.5, 0.46, 0.4, 1.0],
        roughness: 0.85,
        ..doppio_engine::Material::default()
    };
    let mat = root.join("rock.mat");
    engine
        .save_material(&mat.to_string_lossy(), &material)
        .unwrap();
    engine.apply_material(wall, &mat.to_string_lossy()).unwrap();
    engine.set_position(wall, [0.0, 0.0, 0.0]).unwrap();
    engine.set_scale(wall, [16.0, 16.0, 0.5]).unwrap();
    let mut rocks = Vec::new();
    for z in [-3.5f32, 3.5] {
        for x in [-2.5f32, 0.0, 2.5] {
            rocks.push(place_rock(
                engine,
                &mesh.to_string_lossy(),
                &mat.to_string_lossy(),
                [x, 0.0, z],
                1.0,
                true,
            ));
        }
    }
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.0,
            pitch: 0.02,
            distance: 12.0,
            focus: [0.0, 0.0, 0.0],
        })
        .unwrap();
    let measure = |engine: &mut doppio_engine::Engine| {
        let mut pixels = Vec::new();
        for _ in 0..4 {
            pixels = render_entity_view_sized(engine, 480, 360);
        }
        (pixels, engine.micromesh_frame_stats().unwrap())
    };
    let (open, open_stats) = measure(engine);
    assert!(!open_stats.occlusion);
    assert_eq!(open_stats.draws, 6);
    for &(id, renderer) in &rocks {
        set_component_field(
            engine,
            id,
            renderer,
            "Micromesh occlusion",
            doppio_engine::FieldValue::Bool(true),
        )
        .unwrap();
    }
    let (culled, culled_stats) = measure(engine);
    assert!(
        culled_stats.occlusion,
        "the cut was tested against last frame's depth"
    );
    assert_eq!(culled_stats.overflowed, 0);
    // About half of the rocks are behind the wall: their clusters go.
    assert!(
        culled_stats.triangles * 4 < open_stats.triangles * 3,
        "culled {} vs open {}",
        culled_stats.triangles,
        open_stats.triangles
    );
    assert!(
        culled_stats.triangles * 4 > open_stats.triangles,
        "the visible rocks stay: culled {} vs open {}",
        culled_stats.triangles,
        open_stats.triangles
    );
    // And the picture is the same.
    let mut sum = 0u64;
    for (a, b) in open
        .as_chunks::<4>()
        .0
        .iter()
        .zip(culled.as_chunks::<4>().0.iter())
    {
        sum += (0..3)
            .map(|k| (a[k] as i32 - b[k] as i32).unsigned_abs() as u64)
            .sum::<u64>();
    }
    let mean = sum as f64 / (3.0 * (open.len() / 4) as f64);
    assert!(mean < 1.5, "occlusion changed the picture: {mean} / 255");
}

// ------------------------------------------------------------------------------------------ benchmarks

/// One mesh under construction: positions with a vertex colour, UVs and indexed triangles, smooth normals added
/// by `write` (grids that close on themselves share their seam's normals).
#[derive(Default)]
struct ObjBuilder {
    positions: Vec<[f32; 3]>,
    colours: Vec<[f32; 4]>,
    uvs: Vec<[f32; 2]>,
    weld: Vec<usize>,
    triangles: Vec<[usize; 3]>,
}

impl ObjBuilder {
    /// A `cols x rows` grid of quads (`cols + 1` by `rows + 1` vertices; the last column repeats the first
    /// when `closed`). `shape(u, v)` gives the position and the colour at (u, v) in 0..1.
    fn grid(
        &mut self,
        cols: usize,
        rows: usize,
        closed: bool,
        shape: &dyn Fn(f32, f32) -> ([f32; 3], [f32; 4]),
    ) {
        let first = self.positions.len();
        let columns = cols + 1;
        for j in 0..=rows {
            for i in 0..=cols {
                let (u, v) = (i as f32 / cols as f32, j as f32 / rows as f32);
                let (position, colour) = shape(u, v);
                self.positions.push(position);
                self.colours.push(colour);
                self.uvs.push([u, 1.0 - v]);
                let canonical = if closed && i == cols { 0 } else { i };
                self.weld.push(first + j * columns + canonical);
            }
        }
        for j in 0..rows {
            for i in 0..cols {
                let at = |i: usize, j: usize| first + j * columns + i;
                self.triangles.push([at(i, j), at(i + 1, j), at(i, j + 1)]);
                self.triangles
                    .push([at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)]);
            }
        }
    }

    /// Writes the OBJ (degenerate triangles skipped) and returns its triangle count.
    fn write(&self, path: &std::path::Path) -> usize {
        let cross = |t: &[usize; 3]| {
            let (a, b, c) = (
                self.positions[t[0]],
                self.positions[t[1]],
                self.positions[t[2]],
            );
            let (e1, e2) = (
                [b[0] - a[0], b[1] - a[1], b[2] - a[2]],
                [c[0] - a[0], c[1] - a[1], c[2] - a[2]],
            );
            [
                e1[1] * e2[2] - e1[2] * e2[1],
                e1[2] * e2[0] - e1[0] * e2[2],
                e1[0] * e2[1] - e1[1] * e2[0],
            ]
        };
        let mut accumulated = vec![[0.0f32; 3]; self.positions.len()];
        for t in &self.triangles {
            let n = cross(t);
            for &v in t {
                for (k, value) in n.iter().enumerate() {
                    accumulated[self.weld[v]][k] += value;
                }
            }
        }
        let mut obj = String::from("# benchmark mesh\n");
        for (p, c) in self.positions.iter().zip(&self.colours) {
            obj.push_str(&format!(
                "v {} {} {} {} {} {} {}\n",
                p[0], p[1], p[2], c[0], c[1], c[2], c[3]
            ));
        }
        for uv in &self.uvs {
            obj.push_str(&format!("vt {} {}\n", uv[0], uv[1]));
        }
        for v in 0..self.positions.len() {
            let a = accumulated[self.weld[v]];
            let l = (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]).sqrt().max(1e-9);
            obj.push_str(&format!("vn {} {} {}\n", a[0] / l, a[1] / l, a[2] / l));
        }
        let mut count = 0;
        for t in &self.triangles {
            let n = cross(t);
            if n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-14 {
                continue;
            }
            obj.push_str(&format!(
                "f {0}/{0}/{0} {1}/{1}/{1} {2}/{2}/{2}\n",
                t[0] + 1,
                t[1] + 1,
                t[2] + 1
            ));
            count += 1;
        }
        std::fs::write(path, obj).unwrap();
        count
    }
}

/// Deterministic value noise in 3D (a hash, no randomness): the benchmark content must be the same every run.
fn bench_noise(x: f32, y: f32, z: f32) -> f32 {
    fn hash(x: i32, y: i32, z: i32) -> f32 {
        let mut h = (x as u32)
            .wrapping_mul(374_761_393)
            .wrapping_add((y as u32).wrapping_mul(668_265_263))
            .wrapping_add((z as u32).wrapping_mul(2_147_483_647));
        h = (h ^ (h >> 13)).wrapping_mul(1_274_126_177);
        ((h ^ (h >> 16)) & 0xFFFF) as f32 / 65_535.0
    }
    let (xi, yi, zi) = (x.floor() as i32, y.floor() as i32, z.floor() as i32);
    let (fx, fy, fz) = (x - xi as f32, y - yi as f32, z - zi as f32);
    let smooth = |t: f32| t * t * (3.0 - 2.0 * t);
    let (sx, sy, sz) = (smooth(fx), smooth(fy), smooth(fz));
    let mix = |a: f32, b: f32, t: f32| a + (b - a) * t;
    let mut c = [[0.0f32; 2]; 2];
    for (dy, row) in c.iter_mut().enumerate() {
        for (dz, cell) in row.iter_mut().enumerate() {
            let (yy, zz) = (yi + dy as i32, zi + dz as i32);
            *cell = mix(hash(xi, yy, zz), hash(xi + 1, yy, zz), sx);
        }
    }
    mix(mix(c[0][0], c[1][0], sy), mix(c[0][1], c[1][1], sy), sz)
}

/// A dense tree: a barky trunk and three lumpy crowns (about 48 000 triangles), brown and green vertex colours.
fn write_tree_obj(path: &std::path::Path) -> usize {
    let tau = std::f32::consts::TAU;
    let mut tree = ObjBuilder::default();
    tree.grid(48, 120, true, &|u, v| {
        let (a, y) = (u * tau, v * 3.4);
        let bark = bench_noise(a.cos() * 3.0, y * 4.0, a.sin() * 3.0);
        let r = (0.26 - 0.07 * v) * (1.0 + 0.35 * (bark - 0.5));
        (
            [r * a.cos(), y, r * a.sin()],
            [0.42 + 0.1 * bark, 0.29, 0.18, 1.0],
        )
    });
    for (centre, radius) in [
        ([0.0f32, 3.6, 0.0], 1.5f32),
        ([0.5, 4.7, -0.3], 1.15),
        ([-0.3, 5.5, 0.3], 0.85),
    ] {
        tree.grid(96, 64, true, &|u, v| {
            let (a, t) = (u * tau, v * std::f32::consts::PI);
            let d = [t.sin() * a.cos(), t.cos(), t.sin() * a.sin()];
            let n = bench_noise(d[0] * 2.5 + centre[0], d[1] * 2.5 + centre[1], d[2] * 2.5) * 0.6
                + bench_noise(d[0] * 7.0, d[1] * 7.0 + centre[1], d[2] * 7.0) * 0.4;
            let r = radius * (0.8 + 0.4 * n);
            (
                [
                    centre[0] + d[0] * r,
                    centre[1] + d[1] * r * 0.85,
                    centre[2] + d[2] * r,
                ],
                [0.18 + 0.2 * n, 0.42 + 0.3 * n, 0.12 + 0.1 * n, 1.0],
            )
        });
    }
    tree.write(path)
}

/// One frame's numbers: encoding time, wall time of render + readback, and the renderer's counters.
#[derive(Clone, Default)]
struct BenchFrame {
    wall_ms: f64,
    cpu_ms: f32,
    triangles: u64,
    draw_calls: u32,
    instanced: u32,
    meshes: u32,
    culled: u32,
    micro_draws: u32,
    micro_triangles: u64,
    micro_clusters: u32,
}

/// Renders one frame and returns the pixels and its wall-clock time (render + readback, polled finely).
fn render_timed(engine: &mut doppio_engine::Engine, w: u32, h: u32) -> (Vec<u8>, f64) {
    let started = std::time::Instant::now();
    engine.render_begin(w, h).expect("begin");
    let mut pixels = vec![0u8; (w * h * 4) as usize];
    for _ in 0..40_000 {
        if let doppio_engine::RenderState::Ready = engine.render_poll(&mut pixels).expect("poll") {
            return (pixels, started.elapsed().as_secs_f64() * 1000.0);
        }
        std::thread::sleep(std::time::Duration::from_micros(200));
    }
    panic!("render did not finish");
}

/// A fixed flight: `camera_at(a)` for `frames` values of `a` in 0..1 after `warmup` frames at the start.
fn bench_flight(
    engine: &mut doppio_engine::Engine,
    camera_at: &dyn Fn(f32) -> doppio_engine::Camera,
    frames: usize,
    warmup: usize,
    micromesh: bool,
) -> Vec<BenchFrame> {
    let (w, h) = (640u32, 360u32);
    engine.set_camera(camera_at(0.0)).unwrap();
    for _ in 0..warmup {
        let _ = render_timed(engine, w, h);
    }
    let mut rows = Vec::new();
    for i in 0..frames {
        engine
            .set_camera(camera_at(i as f32 / (frames - 1) as f32))
            .unwrap();
        let (_, wall_ms) = render_timed(engine, w, h);
        let stats = engine.render_stats().unwrap();
        let mut row = BenchFrame {
            wall_ms,
            cpu_ms: stats.total_ms,
            triangles: stats.triangles,
            draw_calls: stats.draw_calls,
            instanced: stats.instanced_draw_calls,
            meshes: stats.meshes,
            culled: stats.frustum_culled,
            ..BenchFrame::default()
        };
        if micromesh {
            let m = engine.micromesh_frame_stats().unwrap();
            row.micro_draws = m.draws;
            row.micro_triangles = m.triangles;
            row.micro_clusters = m.clusters;
        }
        rows.push(row);
    }
    rows
}

/// The benchmark content is the same every run: byte-identical OBJ files, fixed triangle counts.
#[test]
fn benchmark_content_is_deterministic() {
    let root = test_temp_root("bench-content");
    std::fs::create_dir_all(&root).unwrap();
    let (a, b) = (root.join("tree_a.obj"), root.join("tree_b.obj"));
    let count_a = write_tree_obj(&a);
    let count_b = write_tree_obj(&b);
    assert_eq!(count_a, count_b);
    assert_eq!(std::fs::read(&a).unwrap(), std::fs::read(&b).unwrap());
    assert_eq!(count_a, 47_808, "the forest benchmark's tree");
    let (c, d) = (root.join("rock_a.obj"), root.join("rock_b.obj"));
    let rock_a = write_rock_obj(&c, 256, 160);
    assert_eq!(rock_a, write_rock_obj(&d, 256, 160));
    assert_eq!(std::fs::read(&c).unwrap(), std::fs::read(&d).unwrap());
    assert_eq!(rock_a, 81_408, "the rock field benchmark's rock");
}

/// The reproducible benchmark set (env-gated: `DOPPIO_BENCH_DIR=<dir>`, run by
/// `scripts/benchmark-micromesh.sh`): a dense rock field, a forest of dense trees and a terrain, each flown by a
/// fixed camera at 640x360 with the sun's shadows on, with every renderer path that applies. Content is
/// generated in code by hashes: no assets, the same triangles every run. Writes `benchmarks.json`.
#[test]
fn micromesh_benchmark_set() {
    let Ok(dir) = std::env::var("DOPPIO_BENCH_DIR") else {
        return;
    };
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 14 {
        return;
    }
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    let light_id = harness.app().entities[harness.app().selected].id;
    let plane = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Plane")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(plane));
    let ground = harness.app().entities[harness.app().selected].id;
    let root = test_temp_root("micromesh-bench");
    std::fs::create_dir_all(&root).unwrap();
    let rock_obj = root.join("rock.obj");
    let tree_obj = root.join("tree.obj");
    let rock_triangles = write_rock_obj(&rock_obj, 256, 160);
    let tree_triangles = write_tree_obj(&tree_obj);
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_rotation(light_id, [-42.0, 30.0, 0.0]).unwrap();
    let mut settings = engine.viewport_settings().unwrap();
    settings.shadows = true;
    settings.show_grid = false;
    engine.set_viewport_settings(settings).unwrap();
    let save = |engine: &mut doppio_engine::Engine, name: &str, albedo: [f32; 4]| {
        let path = root.join(name);
        let material = doppio_engine::Material {
            albedo,
            roughness: 0.85,
            ..doppio_engine::Material::default()
        };
        engine
            .save_material(&path.to_string_lossy(), &material)
            .unwrap();
        path.to_string_lossy().into_owned()
    };
    let rock_mat = save(engine, "rock.mat", [0.26, 0.24, 0.21, 1.0]);
    let tree_mat = save(engine, "tree.mat", [1.0, 1.0, 1.0, 1.0]); // the vertex colours do the colouring
    let dirt = save(engine, "dirt.mat", [0.42, 0.37, 0.28, 1.0]);
    engine.apply_material(ground, &dirt).unwrap();
    engine.set_position(ground, [0.0, 0.0, 0.0]).unwrap();
    engine.set_scale(ground, [400.0, 1.0, 400.0]).unwrap();
    let hash = |x: usize, z: usize, k: usize| {
        let mut v = (x * 73_856_093) ^ (z * 19_349_663) ^ (k * 83_492_791);
        v = (v ^ (v >> 13)).wrapping_mul(1_274_126_177);
        ((v ^ (v >> 16)) & 0xFFFF) as f32 / 65_535.0
    };

    struct Config {
        name: &'static str,
        frames: Vec<BenchFrame>,
    }
    struct Scene {
        name: &'static str,
        description: String,
        instances: usize,
        source_triangles: usize,
        configs: Vec<Config>,
    }
    let mut scenes: Vec<Scene> = Vec::new();
    let frames = 40usize;
    let toggle = |engine: &mut doppio_engine::Engine, entities: &[(u32, u32)], on: bool| {
        for &(id, renderer) in entities {
            set_component_field(
                engine,
                id,
                renderer,
                "Micromesh",
                doppio_engine::FieldValue::Bool(on),
            )
            .unwrap();
        }
    };

    // ---- 1. the dense rock field: 81 rocks of 81 408 triangles
    let mut rocks = Vec::new();
    for ix in 0..9usize {
        for iz in 0..9usize {
            let scale = 0.85 + 0.9 * hash(ix, iz, 1);
            let x = (ix as f32 - 4.0) * 4.4 + (hash(ix, iz, 2) - 0.5) * 2.4;
            let z = (iz as f32 - 4.0) * 4.4 + (hash(ix, iz, 3) - 0.5) * 2.4;
            let (id, renderer) = place_rock(
                engine,
                &rock_obj.to_string_lossy(),
                &rock_mat,
                [x, 0.5 * scale, z],
                scale,
                false,
            );
            engine
                .set_rotation(id, [0.0, hash(ix, iz, 4) * 360.0, 0.0])
                .unwrap();
            rocks.push((id, renderer));
        }
    }
    let rock_camera = |a: f32| doppio_engine::Camera {
        yaw: 0.25 + 0.5 * (a * std::f32::consts::TAU).sin(),
        pitch: 0.2 + 0.05 * (a * std::f32::consts::TAU * 2.0).sin(),
        distance: 7.5,
        focus: [
            3.0 * (a * std::f32::consts::TAU * 1.5).sin(),
            1.0,
            15.0 - 30.0 * a,
        ],
    };
    toggle(engine, &rocks, false);
    let plain = bench_flight(engine, &rock_camera, frames, 3, false);
    toggle(engine, &rocks, true);
    let micro = bench_flight(engine, &rock_camera, frames, 3, true);
    scenes.push(Scene {
        name: "Rock field",
        description: format!("81 rocks x {rock_triangles} triangles, a low flight over the field"),
        instances: rocks.len(),
        source_triangles: rocks.len() * rock_triangles,
        configs: vec![
            Config {
                name: "plain meshes (instanced)",
                frames: plain,
            },
            Config {
                name: "Micromesh",
                frames: micro,
            },
        ],
    });
    for &(id, _) in &rocks {
        engine.destroy_entity(id).ok();
    }

    // ---- 2. the forest: 300 trees of ~48 000 triangles with vertex colours
    let mut trees = Vec::new();
    for ix in 0..20usize {
        for iz in 0..15usize {
            let scale = 0.8 + 0.5 * hash(ix, iz, 5);
            let x = (ix as f32 - 9.5) * 5.0 + (hash(ix, iz, 6) - 0.5) * 3.5;
            let z = (iz as f32 - 7.0) * 5.0 + (hash(ix, iz, 7) - 0.5) * 3.5;
            let (id, renderer) = place_rock(
                engine,
                &tree_obj.to_string_lossy(),
                &tree_mat,
                [x, 0.0, z],
                scale,
                false,
            );
            engine
                .set_rotation(id, [0.0, hash(ix, iz, 8) * 360.0, 0.0])
                .unwrap();
            trees.push((id, renderer));
        }
    }
    let forest_camera = |a: f32| doppio_engine::Camera {
        yaw: 0.35 * (a * std::f32::consts::TAU).sin(),
        pitch: 0.14,
        distance: 9.0,
        focus: [
            3.0 * (a * std::f32::consts::TAU).sin(),
            3.0,
            30.0 - 60.0 * a,
        ],
    };
    toggle(engine, &trees, false);
    let plain = bench_flight(engine, &forest_camera, frames, 3, false);
    toggle(engine, &trees, true);
    let micro = bench_flight(engine, &forest_camera, frames, 3, true);
    scenes.push(Scene {
        name: "Forest",
        description: format!("300 trees x {tree_triangles} triangles (trunk and three crowns, vertex colours), a flight between them"),
        instances: trees.len(),
        source_triangles: trees.len() * tree_triangles,
        configs: vec![
            Config { name: "plain meshes (instanced)", frames: plain },
            Config { name: "Micromesh", frames: micro },
        ],
    });
    for &(id, _) in &trees {
        engine.destroy_entity(id).ok();
    }

    // ---- 3. the terrain: two sizes of the engine's terrain path (Micromesh does not draw terrain yet)
    engine.set_position(ground, [0.0, -500.0, 0.0]).unwrap(); // out of the way
    let mut terrain_configs = Vec::new();
    let mut terrain_triangles = 0usize;
    for (name, resolution) in [("513 x 513", 513u32), ("1025 x 1025", 1025u32)] {
        let entity = engine.create_entity("Terrain").expect("terrain entity");
        let cells = (resolution * resolution) as usize;
        let mut heights = Vec::with_capacity(cells);
        for z in 0..resolution {
            for x in 0..resolution {
                let (fx, fz) = (
                    x as f32 / resolution as f32 * 6.0,
                    z as f32 / resolution as f32 * 6.0,
                );
                let h = bench_noise(fx, 0.0, fz) * 0.55
                    + bench_noise(fx * 2.3, 1.0, fz * 2.3) * 0.3
                    + bench_noise(fx * 5.1, 2.0, fz * 5.1) * 0.15;
                heights.push(h);
            }
        }
        let terrain = doppio_engine::Terrain {
            resolution: [resolution, resolution],
            size: [800.0, 800.0],
            max_height: 90.0,
            cast_shadows: true,
        };
        engine
            .set_terrain(entity, &terrain, &heights, None)
            .unwrap();
        engine.set_position(entity, [0.0, 0.0, 0.0]).unwrap();
        terrain_triangles = 2 * (resolution as usize - 1) * (resolution as usize - 1);
        let terrain_camera = |a: f32| doppio_engine::Camera {
            yaw: 0.8 + 1.2 * a,
            pitch: 0.32,
            distance: 160.0 - 60.0 * (a * std::f32::consts::PI).sin(),
            focus: [-100.0 + 200.0 * a, 40.0, 0.0],
        };
        let rows = bench_flight(engine, &terrain_camera, frames, 3, false);
        terrain_configs.push(Config {
            name: if resolution == 513 {
                "513 x 513"
            } else {
                "1025 x 1025"
            },
            frames: rows,
        });
        let _ = name;
        engine.destroy_entity(entity).ok();
    }
    scenes.push(Scene {
        name: "Terrain",
        description: "an 800 m terrain at two grid sizes (the engine's chunked terrain path), a flight over it".to_owned(),
        instances: 1,
        source_triangles: terrain_triangles,
        configs: terrain_configs,
    });

    // ---- the report
    let mut json =
        String::from("{\n  \"resolution\": [640, 360],\n  \"frames\": 40,\n  \"scenes\": [\n");
    for (si, scene) in scenes.iter().enumerate() {
        json.push_str(&format!(
            "{}    {{\"name\": \"{}\", \"description\": \"{}\", \"instances\": {}, \"sourceTriangles\": {}, \"configs\": [\n",
            if si == 0 { "" } else { ",\n" },
            scene.name,
            scene.description,
            scene.instances,
            scene.source_triangles
        ));
        for (ci, config) in scene.configs.iter().enumerate() {
            json.push_str(&format!(
                "{}      {{\"name\": \"{}\", \"frames\": [\n",
                if ci == 0 { "" } else { ",\n" },
                config.name
            ));
            for (fi, f) in config.frames.iter().enumerate() {
                json.push_str(&format!(
                    "{}        {{\"wallMs\": {}, \"cpuMs\": {}, \"triangles\": {}, \"drawCalls\": {}, \"instancedDrawCalls\": {}, \"meshes\": {}, \"culled\": {}, \"microDraws\": {}, \"microTriangles\": {}, \"microClusters\": {}}}",
                    if fi == 0 { "" } else { ",\n" },
                    f.wall_ms, f.cpu_ms, f.triangles, f.draw_calls, f.instanced, f.meshes, f.culled,
                    f.micro_draws, f.micro_triangles, f.micro_clusters
                ));
            }
            json.push_str("\n      ]}");
        }
        json.push_str("\n    ]}");
    }
    json.push_str("\n  ]\n}\n");
    std::fs::write(format!("{dir}/benchmarks.json"), json).unwrap();
}

/// Documentation media of Micromesh occlusion (env-gated: `DOPPIO_MICROMESH_OCCLUSION=<dir>`; see
/// `scripts/render-micromesh-scene-docs.sh`): the camera swings around a wall with rocks behind it. Every
/// frame is drawn twice from the same camera, with and without occlusion culling, to count the triangles and to
/// measure how much the picture differs (the single-phase test uses last frame's depth).
#[test]
fn micromesh_occlusion_frames_for_docs() {
    let Ok(dir) = std::env::var("DOPPIO_MICROMESH_OCCLUSION") else {
        return;
    };
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 14 {
        return;
    }
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    let light_id = harness.app().entities[harness.app().selected].id;
    let cube = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Cube")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(cube));
    let wall = harness.app().entities[harness.app().selected].id;
    let plane = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Plane")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(plane));
    let ground = harness.app().entities[harness.app().selected].id;
    let root = test_temp_root("micromesh-occlusion-docs");
    std::fs::create_dir_all(&root).unwrap();
    let mesh = root.join("rock.obj");
    let source = write_rock_obj(&mesh, 192, 120);
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_rotation(light_id, [-42.0, 30.0, 0.0]).unwrap();
    let save = |engine: &mut doppio_engine::Engine, name: &str, albedo: [f32; 4]| {
        let path = root.join(name);
        let material = doppio_engine::Material {
            albedo,
            roughness: 0.85,
            ..doppio_engine::Material::default()
        };
        engine
            .save_material(&path.to_string_lossy(), &material)
            .unwrap();
        path.to_string_lossy().into_owned()
    };
    let rock_mat = save(engine, "rock.mat", [0.26, 0.24, 0.21, 1.0]);
    let wall_mat = save(engine, "wall.mat", [0.45, 0.2, 0.15, 1.0]);
    let dirt = save(engine, "dirt.mat", [0.42, 0.37, 0.28, 1.0]);
    engine.apply_material(wall, &wall_mat).unwrap();
    engine.set_position(wall, [0.0, 1.6, 2.5]).unwrap();
    engine.set_scale(wall, [10.0, 3.2, 0.5]).unwrap();
    engine.apply_material(ground, &dirt).unwrap();
    engine.set_scale(ground, [60.0, 1.0, 60.0]).unwrap();
    engine.set_position(ground, [0.0, 0.0, 0.0]).unwrap();
    let mut rocks = Vec::new();
    for (row, z) in [0.0f32, -2.4, -4.8, 5.0].into_iter().enumerate() {
        for i in 0..6 {
            let x = (i as f32 - 2.5) * 2.4 + if row % 2 == 0 { 0.0 } else { 1.0 };
            rocks.push(place_rock(
                engine,
                &mesh.to_string_lossy(),
                &rock_mat,
                [x, 0.55, z],
                1.0,
                true,
            ));
        }
    }
    let mut settings = engine.viewport_settings().unwrap();
    settings.shadows = true;
    engine.set_viewport_settings(settings).unwrap();
    let set_all = |engine: &mut doppio_engine::Engine, on: bool| {
        for &(id, renderer) in &rocks {
            set_component_field(
                engine,
                id,
                renderer,
                "Micromesh occlusion",
                doppio_engine::FieldValue::Bool(on),
            )
            .unwrap();
        }
    };
    let camera_at = |a: f32| doppio_engine::Camera {
        yaw: std::f32::consts::PI - 0.9 + 1.8 * a,
        pitch: 0.22,
        distance: 9.5,
        focus: [0.0, 1.0, 0.0],
    };
    let (w, h) = (640u32, 360u32);
    set_all(engine, false);
    engine.set_camera(camera_at(0.0)).unwrap();
    for _ in 0..3 {
        let _ = render_entity_view_sized(engine, w, h);
    }
    let frames = 60usize;
    let mut rows = Vec::new();
    for i in 0..frames {
        engine
            .set_camera(camera_at(i as f32 / (frames - 1) as f32))
            .unwrap();
        set_all(engine, true);
        let on = render_entity_view_sized(engine, w, h);
        let on_stats = engine.micromesh_frame_stats().unwrap();
        set_all(engine, false);
        let off = render_entity_view_sized(engine, w, h);
        let off_stats = engine.micromesh_frame_stats().unwrap();
        image::save_buffer(
            format!("{dir}/occ_{i:03}.png"),
            &on,
            w,
            h,
            image::ColorType::Rgba8,
        )
        .unwrap();
        if i == frames / 2 || i == 0 {
            image::save_buffer(
                format!("{dir}/occ_off_{i:03}.png"),
                &off,
                w,
                h,
                image::ColorType::Rgba8,
            )
            .unwrap();
        }
        let mut sum = 0u64;
        let mut worst = 0u64;
        for (a, b) in on
            .as_chunks::<4>()
            .0
            .iter()
            .zip(off.as_chunks::<4>().0.iter())
        {
            let d = (0..3)
                .map(|k| (a[k] as i32 - b[k] as i32).unsigned_abs() as u64)
                .sum::<u64>();
            sum += d;
            worst = worst.max(d);
        }
        let mean = sum as f64 / (3.0 * (on.len() / 4) as f64);
        rows.push((
            on_stats.triangles,
            off_stats.triangles,
            on_stats.clusters,
            off_stats.clusters,
            mean,
            worst,
            on_stats.occlusion,
        ));
    }
    let mut json = format!(
        "{{\n  \"rocks\": {},\n  \"rockTriangles\": {},\n  \"frames\": [\n",
        rocks.len(),
        source
    );
    for (i, r) in rows.iter().enumerate() {
        json.push_str(&format!(
            "{}    {{\"occTriangles\": {}, \"openTriangles\": {}, \"occClusters\": {}, \"openClusters\": {}, \"meanDifference\": {}, \"worstPixel\": {}, \"active\": {}}}",
            if i == 0 { "" } else { ",\n" },
            r.0, r.1, r.2, r.3, r.4, r.5, r.6
        ));
    }
    json.push_str("\n  ]\n}\n");
    std::fs::write(format!("{dir}/occlusion.json"), json).unwrap();
}

/// Documentation media of the Micromesh scene path (env-gated: `DOPPIO_MICROMESH_ANIM=<dir>`; see
/// `scripts/render-micromesh-scene-docs.sh`): a flight over a field of dense rocks drawn through the real
/// scene renderer (lit shader, sun shadows, G-buffer), with the cut on and off, and the triangle counts.
#[test]
fn micromesh_scene_frames_for_docs() {
    let Ok(dir) = std::env::var("DOPPIO_MICROMESH_ANIM") else {
        return;
    };
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 14 {
        return;
    }
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    let light_id = harness.app().entities[harness.app().selected].id;
    let plane = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Plane")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(plane));
    let ground = harness.app().entities[harness.app().selected].id;
    // A weaker light from the other side, so the shaded faces are not black.
    harness.dispatch(Message::CreatePrefab(light));
    let fill_id = harness.app().entities[harness.app().selected].id;
    let root = test_temp_root("micromesh-scene-docs");
    std::fs::create_dir_all(&root).unwrap();
    let mesh = root.join("rock.obj");
    let source = write_rock_obj(&mesh, 256, 160);
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_rotation(light_id, [-38.0, 40.0, 0.0]).unwrap();
    engine.set_rotation(fill_id, [-25.0, 215.0, 0.0]).unwrap();
    if let Some(light_type) = component_type_id(engine, "Light") {
        let _ = set_component_field(
            engine,
            fill_id,
            light_type,
            "Intensity",
            doppio_engine::FieldValue::F32(0.35),
        );
    }
    engine.set_position(ground, [0.0, 0.0, 0.0]).unwrap();
    engine.set_scale(ground, [90.0, 1.0, 90.0]).unwrap();
    let save =
        |engine: &mut doppio_engine::Engine, name: &str, albedo: [f32; 4], roughness: f32| {
            let path = root.join(name);
            let material = doppio_engine::Material {
                albedo,
                roughness,
                ..doppio_engine::Material::default()
            };
            engine
                .save_material(&path.to_string_lossy(), &material)
                .unwrap();
            path.to_string_lossy().into_owned()
        };
    let dirt = save(engine, "dirt.mat", [0.42, 0.37, 0.28, 1.0], 0.95);
    engine.apply_material(ground, &dirt).unwrap();
    let materials = [
        save(engine, "rock_a.mat", [0.26, 0.24, 0.21, 1.0], 0.85),
        save(engine, "rock_b.mat", [0.32, 0.28, 0.22, 1.0], 0.8),
        save(engine, "rock_c.mat", [0.20, 0.20, 0.21, 1.0], 0.9),
    ];
    let mut rocks: Vec<(u32, u32)> = Vec::new();
    let n = 9usize;
    for ix in 0..n {
        for iz in 0..n {
            let h = |k: usize| {
                let mut v = (ix * 73856093) ^ (iz * 19349663) ^ (k * 83492791);
                v = (v ^ (v >> 13)).wrapping_mul(1274126177);
                ((v ^ (v >> 16)) & 0xFFFF) as f32 / 65535.0
            };
            let scale = 0.85 + 0.9 * h(1);
            let x = (ix as f32 - (n as f32 - 1.0) * 0.5) * 4.4 + (h(2) - 0.5) * 2.4;
            let z = (iz as f32 - (n as f32 - 1.0) * 0.5) * 4.4 + (h(3) - 0.5) * 2.4;
            let (id, renderer) = place_rock(
                engine,
                &mesh.to_string_lossy(),
                &materials[(ix + iz) % 3],
                [x, 0.5 * scale, z],
                scale,
                true,
            );
            engine.set_rotation(id, [0.0, h(4) * 360.0, 0.0]).unwrap();
            rocks.push((id, renderer));
        }
    }
    let set_all = |engine: &mut doppio_engine::Engine, on: bool| {
        for &(id, renderer) in &rocks {
            set_component_field(
                engine,
                id,
                renderer,
                "Micromesh",
                doppio_engine::FieldValue::Bool(on),
            )
            .unwrap();
        }
    };
    let camera_at = |a: f32| doppio_engine::Camera {
        yaw: 0.25 + 0.5 * (a * std::f32::consts::TAU).sin(),
        pitch: 0.2 + 0.05 * (a * std::f32::consts::TAU * 2.0).sin(),
        distance: 7.5,
        focus: [
            3.0 * (a * std::f32::consts::TAU * 1.5).sin(),
            1.0,
            15.0 - 30.0 * a,
        ],
    };
    let frames = 72usize;
    let (w, h) = (640u32, 360u32);
    // The viewport renders without shadow maps unless asked: ask (the cascades get their own coarser cut).
    let mut settings = engine.viewport_settings().unwrap();
    settings.shadows = true;
    engine.set_viewport_settings(settings).unwrap();
    // Build the hierarchy and settle shadows before the first recorded frame.
    set_all(engine, true);
    engine.set_camera(camera_at(0.0)).unwrap();
    let _ = render_entity_view_sized(engine, w, h);
    let _ = render_entity_view_sized(engine, w, h);

    let mut micro_rows = Vec::new();
    for i in 0..frames {
        engine
            .set_camera(camera_at(i as f32 / (frames - 1) as f32))
            .unwrap();
        let pixels = render_entity_view_sized(engine, w, h);
        let stats = engine.render_stats().unwrap();
        let frame = engine.micromesh_frame_stats().unwrap();
        image::save_buffer(
            format!("{dir}/scene_{i:03}.png"),
            &pixels,
            w,
            h,
            image::ColorType::Rgba8,
        )
        .unwrap();
        micro_rows.push((
            frame.draws,
            frame.clusters,
            frame.triangles,
            frame.source_triangles,
            stats.total_ms,
            stats.triangles,
            frame.views,
            frame.arena_used,
            frame.arena_capacity,
            frame.overflowed,
        ));
    }
    // The same flight with the plain mesh (instanced draws): triangles submitted and CPU encoding time.
    set_all(engine, false);
    let _ = render_entity_view_sized(engine, w, h);
    let mut plain_rows = Vec::new();
    for i in 0..frames {
        engine
            .set_camera(camera_at(i as f32 / (frames - 1) as f32))
            .unwrap();
        let _ = render_entity_view_sized(engine, w, h);
        let stats = engine.render_stats().unwrap();
        plain_rows.push((stats.triangles, stats.total_ms, stats.draw_calls));
    }
    // One aerial view, both ways, to compare the pictures.
    let aerial = doppio_engine::Camera {
        yaw: 0.6,
        pitch: 0.75,
        distance: 42.0,
        focus: [0.0, 0.0, 0.0],
    };
    engine.set_camera(aerial).unwrap();
    let _ = render_entity_view_sized(engine, w, h);
    let off = render_entity_view_sized(engine, w, h);
    image::save_buffer(
        format!("{dir}/still_off.png"),
        &off,
        w,
        h,
        image::ColorType::Rgba8,
    )
    .unwrap();
    set_all(engine, true);
    let _ = render_entity_view_sized(engine, w, h);
    let on = render_entity_view_sized(engine, w, h);
    image::save_buffer(
        format!("{dir}/still_on.png"),
        &on,
        w,
        h,
        image::ColorType::Rgba8,
    )
    .unwrap();
    let aerial_stats = engine.micromesh_frame_stats().unwrap();
    let mut diff = 0u64;
    for (a, b) in off
        .as_chunks::<4>()
        .0
        .iter()
        .zip(on.as_chunks::<4>().0.iter())
    {
        diff += (0..3)
            .map(|k| (a[k] as i32 - b[k] as i32).unsigned_abs() as u64)
            .sum::<u64>();
    }
    let mean = diff as f64 / (3.0 * (off.len() / 4) as f64);
    let mut json = format!(
        "{{\n  \"rocks\": {},\n  \"rockTriangles\": {},\n  \"aerial\": {{\"triangles\": {}, \"source\": {}, \"meanDifference\": {}}},\n  \"frames\": [\n",
        rocks.len(),
        source,
        aerial_stats.triangles,
        aerial_stats.source_triangles,
        mean
    );
    for (i, (m, p)) in micro_rows.iter().zip(&plain_rows).enumerate() {
        json.push_str(&format!(
            "{}    {{\"draws\": {}, \"clusters\": {}, \"triangles\": {}, \"source\": {}, \"microMs\": {}, \"plainTriangles\": {}, \"plainMs\": {}, \"plainDraws\": {}, \"views\": {}, \"arenaUsed\": {}, \"arenaCapacity\": {}, \"overflowed\": {}}}",
            if i == 0 { "" } else { ",\n" },
            m.0, m.1, m.2, m.3, m.4, p.0, p.1, p.2, m.6, m.7, m.8, m.9
        ));
    }
    json.push_str("\n  ]\n}\n");
    std::fs::write(format!("{dir}/scene.json"), json).unwrap();
}

#[test]
fn terrain_generator_is_a_node_graph_tab_with_a_3d_preview() {
    use crate::uikit::panels::terrain_generator as tg;
    let mut harness = Harness::empty();
    harness.dispatch(Message::ShowTerrainGenerator);
    assert!(
        harness
            .app()
            .panes
            .iter()
            .any(|(_, g)| g.active_kind() == PaneKind::TerrainGenerator),
        "the tab comes to the front"
    );
    for widget in [
        id::TERRAINGEN_CANVAS,
        id::TERRAINGEN_PREVIEW,
        id::TERRAINGEN_PRESET,
        id::TERRAINGEN_PLACE,
        id::TERRAINGEN_EXPORT,
    ] {
        assert!(harness.has(Id::from(widget.clone())), "missing {widget:?}");
    }
    assert!(harness.app().terraingen.preview.is_some());
    let before = harness.app().terraingen.stats.clone();
    let ridge = harness
        .app()
        .terraingen
        .graph
        .nodes
        .iter()
        .find(|n| n.op == "output")
        .unwrap()
        .id;
    let max = crate::uikit::terrain::find("output")
        .unwrap()
        .params
        .iter()
        .position(|p| p.key == "max_height")
        .unwrap();
    harness.dispatch(Message::TerrainGen(tg::Edit::SetParam(ridge, max, 900.0)));
    assert_ne!(harness.app().terraingen.stats, before);

    // Nodes can be added; undo/redo; the Output stays; presets replace the graph.
    let nodes = harness.app().terraingen.graph.nodes.len();
    harness.dispatch(Message::TerrainGen(tg::Edit::AddNode("blur")));
    assert_eq!(harness.app().terraingen.graph.nodes.len(), nodes + 1);
    harness.dispatch(Message::TerrainGen(tg::Edit::Undo));
    assert_eq!(harness.app().terraingen.graph.nodes.len(), nodes);
    harness.dispatch(Message::TerrainGen(tg::Edit::Redo));
    assert_eq!(harness.app().terraingen.graph.nodes.len(), nodes + 1);
    harness.dispatch(Message::TerrainGen(tg::Edit::DeleteNode(ridge)));
    assert!(harness.app().terraingen.graph.node(ridge).is_some());
    let island = harness
        .app()
        .terraingen
        .presets
        .iter()
        .position(|p| p.name == "Volcanic Island")
        .unwrap();
    harness.dispatch(Message::TerrainGen(tg::Edit::ApplyPreset(island)));
    assert_eq!(harness.app().terraingen.name, "Volcanic Island");

    // Orbiting redraws the preview without re-evaluating the graph.
    let first = format!("{:?}", harness.app().terraingen.preview);
    harness.dispatch(Message::TerrainGen(tg::Edit::PreviewPress));
    harness.dispatch(Message::TerrainGen(tg::Edit::PreviewMove(
        iced::Point::new(10.0, 10.0),
    )));
    harness.dispatch(Message::TerrainGen(tg::Edit::PreviewMove(
        iced::Point::new(90.0, 40.0),
    )));
    harness.dispatch(Message::TerrainGen(tg::Edit::PreviewRelease));
    assert_ne!(first, format!("{:?}", harness.app().terraingen.preview));
}

/// Documentation media of the terrain masks (env-gated: `DOPPIO_TERRAIN_MASKS=<dir>`; see
/// `scripts/render-terrain-mask-docs.sh`): the Alpine Range placed in the engine, every mask read back through the
/// C ABI as a 256 x 256 picture, and a lit view of the terrain.
#[test]
fn terrain_mask_pictures_for_docs() {
    use crate::uikit::panels::terrain_generator as tg;
    let Ok(dir) = std::env::var("DOPPIO_TERRAIN_MASKS") else {
        return;
    };
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 15 {
        return;
    }
    harness.app.asset_root_override = Some(test_temp_root("terrain-mask-docs"));
    let alpine = harness
        .app()
        .terraingen
        .presets
        .iter()
        .position(|p| p.name == "Alpine Range")
        .unwrap();
    harness.dispatch(Message::TerrainGen(tg::Edit::ApplyPreset(alpine)));
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    {
        let lid = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(lid, [0.0, 300.0, 0.0]).unwrap();
        engine.set_rotation(lid, [-38.0, 140.0, 0.0]).unwrap();
    }
    harness.dispatch(Message::TerrainGen(tg::Edit::Place));
    let id = harness.app().entities[harness.app().selected].id;
    let engine = harness.app.engine.as_mut().expect("engine");
    let terrain = engine.terrain(id).unwrap();
    for info in engine.terrain_masks(id).unwrap() {
        let grid = engine
            .terrain_mask_rect(
                id,
                &info.name,
                [0.0, 0.0, terrain.size[0], terrain.size[1]],
                [256, 256],
            )
            .unwrap();
        let mut rgba = Vec::with_capacity(grid.len() * 4);
        for v in &grid {
            rgba.extend_from_slice(&[*v, *v, *v, 255]);
        }
        image::save_buffer(
            format!("{dir}/mask_{}.png", info.name),
            &rgba,
            256,
            256,
            image::ColorType::Rgba8,
        )
        .unwrap();
    }
    let mut settings = engine.viewport_settings().unwrap();
    settings.show_grid = false;
    settings.shadows = true;
    engine.set_viewport_settings(settings).unwrap();
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.9,
            pitch: 0.75,
            distance: terrain.size[0] * 1.5,
            focus: [0.0, terrain.max_height * 0.25, 0.0],
        })
        .unwrap();
    let _ = render_entity_view_sized(engine, 512, 512);
    let frame = render_entity_view_sized(engine, 512, 512);
    image::save_buffer(
        format!("{dir}/terrain.png"),
        &frame,
        512,
        512,
        image::ColorType::Rgba8,
    )
    .unwrap();
}

/// Documentation media of the Scatter action (env-gated: `DOPPIO_SCATTER_DOCS=<dir>`; see
/// `scripts/render-scatter-docs.sh`): the Alpine Range scattered with the default plan, a lit view of it, and where
/// each class landed over the masks it reads.
#[test]
fn scatter_pictures_for_docs() {
    use crate::uikit::panels::terrain_generator as tg;
    use scatter::ScatterRequest;
    let Ok(dir) = std::env::var("DOPPIO_SCATTER_DOCS") else {
        return;
    };
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 16 {
        return;
    }
    harness.app.asset_root_override = Some(test_temp_root("scatter-docs"));
    let alpine = harness
        .app()
        .terraingen
        .presets
        .iter()
        .position(|p| p.name == "Alpine Range")
        .unwrap();
    harness.dispatch(Message::TerrainGen(tg::Edit::ApplyPreset(alpine)));
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    {
        let lid = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(lid, [0.0, 300.0, 0.0]).unwrap();
        engine.set_rotation(lid, [-42.0, 35.0, 0.0]).unwrap();
    }
    // A weaker light from the other side, so the shaded faces are not black.
    harness.dispatch(Message::CreatePrefab(light));
    {
        let fid = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(fid, [0.0, 300.0, 0.0]).unwrap();
        engine.set_rotation(fid, [-30.0, 215.0, 0.0]).unwrap();
        if let Some(light_type) = component_type_id(engine, "Light") {
            let _ = set_component_field(
                engine,
                fid,
                light_type,
                "Intensity",
                doppio_engine::FieldValue::F32(0.45),
            );
        }
    }
    harness.dispatch(Message::TerrainGen(tg::Edit::Place));
    let terrain = harness.app().entities[harness.app().selected].id;
    harness.dispatch(Message::ScatterOnTerrain(ScatterRequest {
        terrain: Some(terrain),
        max_instances: Some(20_000),
        ..ScatterRequest::default()
    }));
    let status = harness.app().scatter_status.clone().unwrap_or_default();
    let holder = harness.app().entities[harness.app().selected].id;
    let children: Vec<(u32, String)> = harness
        .app()
        .entities
        .iter()
        .filter(|e| e.parent == Some(holder))
        .map(|e| (e.id, e.name.clone()))
        .collect();
    let engine = harness.app.engine.as_mut().expect("engine");
    let info = engine.terrain(terrain).unwrap();
    let centre = engine.world_position(terrain).unwrap();
    for mask in ["slope", "wetness", "height"] {
        let grid = engine
            .terrain_mask_rect(
                terrain,
                mask,
                [0.0, 0.0, info.size[0], info.size[1]],
                [256, 256],
            )
            .unwrap();
        let mut rgba = Vec::new();
        for v in &grid {
            rgba.extend_from_slice(&[*v, *v, *v, 255]);
        }
        image::save_buffer(
            format!("{dir}/mask_{mask}.png"),
            &rgba,
            256,
            256,
            image::ColorType::Rgba8,
        )
        .unwrap();
    }
    let mut points = String::from("{\n  \"status\": \"");
    points.push_str(&status.replace('"', "'"));
    points.push_str("\",\n  \"instances\": [\n");
    let mut first = true;
    for (id, name) in &children {
        let p = engine.world_position(*id).unwrap();
        let (x, z) = (
            p[0] - centre[0] + info.size[0] * 0.5,
            p[2] - centre[2] + info.size[1] * 0.5,
        );
        let class = name.split(' ').next().unwrap_or("");
        points.push_str(&format!(
            "{}    {{\"class\": \"{class}\", \"u\": {}, \"v\": {}}}",
            if first { "" } else { ",\n" },
            x / info.size[0],
            z / info.size[1]
        ));
        first = false;
    }
    points.push_str("\n  ]\n}\n");
    std::fs::write(format!("{dir}/scatter.json"), points).unwrap();
    let mut settings = engine.viewport_settings().unwrap();
    settings.show_grid = false;
    settings.shadows = true;
    engine.set_viewport_settings(settings).unwrap();
    for (name, yaw, pitch, distance, focus_y) in [
        ("aerial", 2.4f32, 0.6f32, info.size[0] * 0.8, 0.2f32),
        ("close", 2.4, 0.38, info.size[0] * 0.3, 0.3),
    ] {
        engine
            .set_camera(doppio_engine::Camera {
                yaw,
                pitch,
                distance,
                focus: [
                    -info.size[0] * 0.12,
                    info.max_height * focus_y,
                    info.size[1] * 0.1,
                ],
            })
            .unwrap();
        let _ = render_entity_view_sized(engine, 640, 400);
        let frame = render_entity_view_sized(engine, 640, 400);
        image::save_buffer(
            format!("{dir}/view_{name}.png"),
            &frame,
            640,
            400,
            image::ColorType::Rgba8,
        )
        .unwrap();
    }
}

/// The Scatter action on a placed terrain: entities per instance under one holder, put where the plan's masks allow,
/// the same for the same seed, one undo step, errors that say what is wrong.
#[test]
fn scatter_on_terrain_places_the_plan_by_the_terrains_masks() {
    use crate::uikit::panels::terrain_generator as tg;
    use scatter::ScatterRequest;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 16 {
        return;
    }
    let root = test_temp_root("scatter-action");
    harness.app.asset_root_override = Some(root.clone());
    let alpine = harness
        .app()
        .terraingen
        .presets
        .iter()
        .position(|p| p.name == "Alpine Range")
        .unwrap();
    harness.dispatch(Message::TerrainGen(tg::Edit::ApplyPreset(alpine)));
    harness.dispatch(Message::TerrainGen(tg::Edit::Place));
    let terrain = harness.app().entities[harness.app().selected].id;
    let before = harness.app().entities.len();

    // Nothing selected that is a terrain: it says so and creates nothing.
    harness.dispatch(Message::ScatterOnTerrain(ScatterRequest {
        terrain: Some(harness.app().entities[0].id),
        ..ScatterRequest::default()
    }));
    let status = harness.app().scatter_status.clone().unwrap_or_default();
    assert!(status.contains("not a terrain"), "{status}");
    assert_eq!(harness.app().entities.len(), before);
    // A plan that does not parse names its line.
    harness.dispatch(Message::ScatterOnTerrain(ScatterRequest {
        plan_text: Some("seed 1\nclass 1 cell 0\n".to_owned()),
        terrain: Some(terrain),
        ..ScatterRequest::default()
    }));
    let status = harness.app().scatter_status.clone().unwrap_or_default();
    assert!(status.contains("line 2"), "{status}");
    // A biome term the engine rejects (a decreasing trapezoid cannot reach it: the parser stops it first).
    assert_eq!(harness.app().entities.len(), before);

    // The default plan (written to the project the first time).
    harness.dispatch(Message::ScatterOnTerrain(ScatterRequest {
        terrain: Some(terrain),
        ..ScatterRequest::default()
    }));
    let status = harness.app().scatter_status.clone().unwrap_or_default();
    assert!(status.starts_with("Scattered "), "{status}");
    assert!(root.join("Scatter/default.scatter").is_file());
    let holder = harness.app().entities[harness.app().selected].id;
    assert!(harness
        .app()
        .entities
        .iter()
        .any(|e| e.id == holder && e.name == "Scatter default"));
    let children: Vec<(u32, String)> = harness
        .app()
        .entities
        .iter()
        .filter(|e| e.parent == Some(holder))
        .map(|e| (e.id, e.name.clone()))
        .collect();
    assert!(
        children.len() > 150,
        "{} instances: {status}",
        children.len()
    );
    assert_eq!(harness.app().entities.len(), before + 1 + children.len());
    let count = |prefix: &str| {
        children
            .iter()
            .filter(|(_, n)| n.starts_with(prefix))
            .count()
    };
    assert!(count("pine ") > 20 && count("tuft ") > 20, "{status}");

    // Each instance stands where its class's masks allow: read the masks back at its position.
    let engine = harness.app.engine.as_ref().expect("engine");
    let info = engine.terrain(terrain).unwrap();
    let centre = engine.world_position(terrain).unwrap();
    let mut positions = Vec::new();
    for (id, name) in &children {
        let p = engine.world_position(*id).unwrap();
        let (x, z) = (
            p[0] - centre[0] + info.size[0] * 0.5,
            p[2] - centre[2] + info.size[1] * 0.5,
        );
        assert!(
            (0.0..=info.size[0]).contains(&x) && (0.0..=info.size[1]).contains(&z),
            "{name} at {x}, {z}"
        );
        let mask = |m: &str| engine.terrain_mask(terrain, m, x, z).unwrap();
        let ground = engine.terrain_height(terrain, x, z).unwrap() + centre[1];
        assert!(
            (p[1] - ground).abs() < 0.8 + 0.3 * 1.6,
            "{name}: y {} vs ground {ground}",
            p[1]
        );
        if name.starts_with("pine ") {
            assert!(
                mask("slope") < 0.45,
                "a pine on a slope of {}",
                mask("slope")
            );
            assert!(
                mask("wetness") < 0.7,
                "a pine in wetness {}",
                mask("wetness")
            );
        } else if name.starts_with("boulder ") {
            assert!(
                mask("slope") > 0.25,
                "a boulder on a slope of {}",
                mask("slope")
            );
        }
        positions.push([p[0], p[1], p[2]]);
    }
    // A boulder is drawn as a Micromesh when its class says so; the tint was set from the class colour.
    let boulder = children
        .iter()
        .find(|(_, n)| n.starts_with("boulder "))
        .map(|(id, _)| *id);
    if let Some(id) = boulder {
        let types = engine.entity_components(id);
        assert!(types.iter().any(|c| c.name == "Mesh Renderer"));
    }

    // One undo step takes the holder and every instance away.
    harness.dispatch(Message::Undo);
    assert_eq!(harness.app().entities.len(), before);

    // The same seed again gives the same forest; another seed does not.
    let run = |harness: &mut Harness, seed: u64| -> Vec<[f32; 3]> {
        harness.dispatch(Message::ScatterOnTerrain(ScatterRequest {
            terrain: Some(terrain),
            seed: Some(seed),
            ..ScatterRequest::default()
        }));
        let holder = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_ref().expect("engine");
        let mut out: Vec<[f32; 3]> = harness
            .app()
            .entities
            .iter()
            .filter(|e| e.parent == Some(holder))
            .map(|e| engine.world_position(e.id).unwrap())
            .collect();
        out.sort_by(|a, b| a[0].total_cmp(&b[0]).then(a[2].total_cmp(&b[2])));
        harness.dispatch(Message::Undo);
        out
    };
    let first = run(&mut harness, 7);
    let second = run(&mut harness, 7);
    let other = run(&mut harness, 8);
    assert!(first.len() > 150);
    assert_eq!(first, second, "the same seed gives the same instances");
    assert_ne!(first, other, "another seed gives another forest");

    // A limit places only the first ones and says so.
    harness.dispatch(Message::ScatterOnTerrain(ScatterRequest {
        terrain: Some(terrain),
        max_instances: Some(40),
        ..ScatterRequest::default()
    }));
    let status = harness.app().scatter_status.clone().unwrap_or_default();
    assert!(status.contains("only the first 40"), "{status}");
    let holder = harness.app().entities[harness.app().selected].id;
    assert_eq!(
        harness
            .app()
            .entities
            .iter()
            .filter(|e| e.parent == Some(holder))
            .count(),
        40
    );
    assert_eq!(positions.len(), children.len());
}

/// A placed terrain carries its masks into the engine: the three built-ins, the generator's flow and wetness, and
/// the erosion's wear and deposit; the engine's slope mask and normal agree with the height it was given.
#[test]
fn placing_a_terrain_exports_its_masks_to_the_engine() {
    use crate::uikit::panels::terrain_generator as tg;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 15 {
        return;
    }
    harness.app.asset_root_override = Some(test_temp_root("terrain-masks"));
    let alpine = harness
        .app()
        .terraingen
        .presets
        .iter()
        .position(|p| p.name == "Alpine Range")
        .unwrap();
    harness.dispatch(Message::TerrainGen(tg::Edit::ApplyPreset(alpine)));
    harness.dispatch(Message::TerrainGen(tg::Edit::Place));
    let status = harness.app().terraingen.status.clone().unwrap_or_default();
    assert!(status.starts_with("Placed Alpine Range"), "{status}");
    assert!(
        status.contains("flow") && status.contains("wear"),
        "{status}"
    );
    let id = harness.app().entities[harness.app().selected].id;
    let engine = harness.app.engine.as_ref().expect("engine");
    let masks = engine.terrain_masks(id).unwrap();
    let names: Vec<&str> = masks.iter().map(|m| m.name.as_str()).collect();
    assert_eq!(
        names,
        [
            "height",
            "slope",
            "curvature",
            "wear",
            "deposit",
            "flow",
            "wetness"
        ]
    );
    assert!(masks[0].builtin && !masks[3].builtin);
    let terrain = engine.terrain(id).unwrap();
    assert_eq!(masks[5].resolution, terrain.resolution);

    // Scan the terrain on a grid: every mask stays in 0..1, flow and wetness vary, and the engine's slope
    // mask agrees with the heights the engine reports (steeper where the height changes faster).
    let (sx, sz) = (terrain.size[0], terrain.size[1]);
    let mut flows = Vec::new();
    let mut steep = Vec::new();
    for j in 1..24 {
        for i in 1..24 {
            let (x, z) = (sx * i as f32 / 24.0, sz * j as f32 / 24.0);
            for name in [
                "height",
                "slope",
                "curvature",
                "wear",
                "deposit",
                "flow",
                "wetness",
            ] {
                let v = engine.terrain_mask(id, name, x, z).unwrap();
                assert!((0.0..=1.0).contains(&v), "{name} at ({x}, {z}) = {v}");
            }
            flows.push(engine.terrain_mask(id, "flow", x, z).unwrap());
            let d = 4.0;
            let rise = (engine.terrain_height(id, x + d, z).unwrap()
                - engine.terrain_height(id, x - d, z).unwrap())
            .abs()
                / (2.0 * d);
            steep.push((rise, engine.terrain_mask(id, "slope", x, z).unwrap()));
            let n = engine.terrain_normal(id, x, z).unwrap();
            assert!((n.iter().map(|c| c * c).sum::<f32>().sqrt() - 1.0).abs() < 1e-3);
            assert!(n[1] > 0.0);
        }
    }
    let (lo, hi) = flows
        .iter()
        .fold((1.0f32, 0.0f32), |(l, h), v| (l.min(*v), h.max(*v)));
    assert!(hi - lo > 0.3, "flow varies across the terrain: {lo}..{hi}");
    // The slope mask is monotonic in the measured rise: the steepest quarter beats the flattest quarter.
    steep.sort_by(|a, b| a.0.total_cmp(&b.0));
    let quarter = steep.len() / 4;
    let flat: f32 = steep[..quarter].iter().map(|s| s.1).sum::<f32>() / quarter as f32;
    let steepest: f32 = steep[steep.len() - quarter..]
        .iter()
        .map(|s| s.1)
        .sum::<f32>()
        / quarter as f32;
    assert!(
        steepest > flat + 0.1,
        "slope mask: flat {flat}, steep {steepest}"
    );
    // A rectangle over the whole terrain is the same data resampled.
    let grid = engine
        .terrain_mask_rect(id, "wetness", [0.0, 0.0, sx, sz], [32, 32])
        .unwrap();
    assert_eq!(grid.len(), 32 * 32);
    assert!(grid.iter().any(|&b| b > 200) && grid.iter().any(|&b| b < 50));
}

#[test]
fn placing_a_terrain_creates_an_engine_terrain_with_splat_layers() {
    use crate::uikit::panels::terrain_generator as tg;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 8 {
        return;
    }
    let root = test_temp_root("terrain-place");
    harness.app.asset_root_override = Some(root.clone());
    let alpine = harness
        .app()
        .terraingen
        .presets
        .iter()
        .position(|p| p.name == "Alpine Range")
        .unwrap();
    harness.dispatch(Message::TerrainGen(tg::Edit::ApplyPreset(alpine)));
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    {
        let lid = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(lid, [0.0, 300.0, 0.0]).unwrap();
        engine.set_rotation(lid, [-38.0, 140.0, 0.0]).unwrap();
    }
    let before = harness.app().entities.len();
    harness.dispatch(Message::TerrainGen(tg::Edit::Place));
    let status = harness.app().terraingen.status.clone().unwrap_or_default();
    assert!(status.starts_with("Placed Alpine Range"), "{status}");
    assert_eq!(harness.app().entities.len(), before + 1);
    // The four layer pictures are real files in the project, written when the preset was applied.
    let layers = harness.app().terraingen.graph.layer_paths();
    assert_eq!(layers.len(), 4);
    for path in &layers {
        assert!(!path.is_empty() && path.ends_with(".mat"), "{path}");
        assert!(root.join("Terrain/Layers").is_dir());
    }
    for name in ["ground", "rock", "dirt", "mud"] {
        assert!(root.join(format!("Terrain/Layers/{name}.png")).is_file());
        assert!(root.join(format!("Terrain/Layers/{name}.mat")).is_file());
    }
    // The engine paints each layer with its material's albedo map.
    let id = harness.app().entities[harness.app().selected].id;
    let engine = harness.app.engine.as_ref().expect("engine");
    let components = engine.entity_components(id);
    let terrain = components.iter().position(|c| c.name == "Terrain").unwrap() as u32;
    let painted = engine
        .component_values(id, terrain)
        .iter()
        .filter(
            |v| matches!(v, doppio_engine::FieldValue::String(t) if t.contains("Terrain/Layers/")),
        )
        .count();
    assert_eq!(painted, 4);
    let id = harness.app().entities[harness.app().selected].id;
    let model = harness.app().terraingen.graph.build(257).unwrap();
    let engine = harness.app.engine.as_mut().expect("engine");
    let terrain = engine.terrain(id).expect("a terrain component");
    assert_eq!(terrain.resolution, [257, 257]);
    assert_eq!(terrain.size, model.size);
    // The engine's height at the centre is the generated height there.
    let centre = engine
        .terrain_height(id, model.size[0] * 0.5, model.size[1] * 0.5)
        .expect("height");
    let expected = model.height.sample(0.5, 0.5) * model.max_height;
    assert!((centre - expected).abs() < 2.0, "{centre} vs {expected}");
    // It renders: the terrain changes the picture of an empty scene.
    let frame = render_entity_view_sized(engine, 160, 90);
    assert!(frame.chunks(4).any(|p| p[0..3] != frame[0..3]));
    if let Ok(dir) = std::env::var("DOPPIO_TERRAIN_DUMP") {
        engine
            .set_camera(doppio_engine::Camera {
                yaw: 0.9,
                pitch: 0.9,
                distance: 1300.0,
                focus: [0.0, 80.0, 0.0],
            })
            .unwrap();
        let big = render_entity_view_sized(engine, 960, 540);
        image::save_buffer(
            format!("{dir}/engine_alpine.png"),
            &big,
            960,
            540,
            image::ColorType::Rgba8,
        )
        .unwrap();
    }

    harness.dispatch(Message::TerrainGen(tg::Edit::Export));
    assert!(root
        .join("Terrain/alpine_range.generator.terrain")
        .is_file());
    assert!(root.join("Terrain/alpine_range_height.png").is_file());
    let text = crate::uikit::recipe::read(&root.join("Terrain/alpine_range.generator.terrain"))
        .unwrap()
        .graph;
    let (name, graph) = crate::uikit::terrain::from_text(&text).expect("parses");
    assert_eq!(name, "Alpine Range");
    assert_eq!(
        graph.build(65).unwrap(),
        harness.app().terraingen.graph.build(65).unwrap()
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn foliage_profiles_keep_leaves_attached_to_their_branches() {
    use super::naturegen::Foliage;
    let trunk = Foliage::for_part("trunk");
    let leaves = Foliage::for_part("leaves");
    let blades = Foliage::for_part("blades");
    // Trunk and crown sway by the same amount (leaves flutter on top of it), rocks stay still.
    assert!(trunk.sway > 0.0 && trunk.flutter == 0.0 && trunk.translucency == 0.0);
    assert_eq!(trunk.sway, leaves.sway);
    assert!(leaves.flutter > 0.0 && leaves.translucency > 0.0);
    // Low plants move more per metre of height than trees.
    assert!(blades.sway > leaves.sway);
    for still in ["rock", "moss", "ground", "body"] {
        assert_eq!(Foliage::for_part(still).sway, 0.0, "{still}");
        assert_eq!(Foliage::for_part(still).translucency, 0.0, "{still}");
    }
}

#[test]
fn scattered_parts_are_placed_as_instances_sharing_one_mesh() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("nature-instances");
    harness.app.asset_root_override = Some(root.clone());
    let preset = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Mossy Rock")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(preset)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let status = harness.app().nature.status.clone().unwrap_or_default();
    assert!(
        status.starts_with("Placed ") && !status.contains("problems"),
        "{status}"
    );
    // One template mesh on disk; many entities use it.
    let templates: Vec<_> = std::fs::read_dir(root.join("Models/Nature"))
        .unwrap()
        .filter_map(|e| e.ok())
        .filter(|e| e.file_name().to_string_lossy().ends_with("_instance.obj"))
        .collect();
    assert_eq!(templates.len(), 1, "{templates:?}");
    let copies = harness
        .app()
        .entities
        .iter()
        .filter(|e| e.name.starts_with("Mossy Rock moss "))
        .count();
    assert!(copies > 3, "{copies} instanced moss entities");
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn texture_node_picker_lists_project_images_and_sets_the_node_image() {
    use crate::uikit::panels::nature_generator as ng;
    let mut harness = Harness::empty();
    harness.dispatch(Message::ShowNatureGenerator);
    harness.app.assets.push(doppio_engine::AssetInfo {
        name: "bark.png".into(),
        relative_path: "textures/bark.png".into(),
        kind: "image".into(),
        size_bytes: 1,
        is_directory: false,
    });
    harness.dispatch(Message::NatureGen(ng::Edit::AddNode("texture")));
    let node = harness
        .app()
        .nature
        .graph
        .nodes
        .iter()
        .find(|n| n.op == "texture")
        .expect("the Texture node is in the palette and is added")
        .id;
    harness.dispatch(Message::NatureGen(ng::Edit::Select(Some(node))));
    // The panel shows a picker of the project's images and a path field for the node's image.
    for widget in ["slot_0_image", "path_0_image"] {
        assert!(
            harness.has(Id::from(id::naturegen_param(widget))),
            "missing {widget}"
        );
    }
    // Clicking the slot opens the picker with previews; picking an image sets the node.
    harness.dispatch(Message::NatureGen(ng::Edit::OpenPicker(
        node,
        0,
        ng::PickKind::Image,
    )));
    assert!(harness.app().nature.picker.is_some());
    assert!(
        harness.has(Id::from(id::NATUREGEN_PICKER)),
        "the picker overlay is drawn"
    );
    harness.dispatch(Message::NatureGen(ng::Edit::PickerPick(
        "textures/bark.png".to_owned(),
    )));
    assert!(harness.app().nature.picker.is_none());
    assert_eq!(
        harness.app().nature.graph.node(node).unwrap().strings[0],
        "textures/bark.png"
    );
    // Clearing empties the slot.
    harness.dispatch(Message::NatureGen(ng::Edit::SetString(
        node,
        0,
        String::new(),
    )));
    // Dragging an image from the Assets pane and releasing it on the slot assigns it; a material
    // is refused by an image slot.
    harness.app.assets.push(doppio_engine::AssetInfo {
        name: "wet.mat".into(),
        relative_path: "materials/wet.mat".into(),
        kind: "material".into(),
        size_bytes: 1,
        is_directory: false,
    });
    harness.app.asset_drag = Some(1);
    harness.dispatch(Message::NatureGen(ng::Edit::DropAsset(
        node,
        0,
        ng::PickKind::Image,
    )));
    assert_eq!(
        harness.app().nature.graph.node(node).unwrap().strings[0],
        ""
    );
    harness.app.asset_drag = Some(0);
    harness.dispatch(Message::NatureGen(ng::Edit::DropAsset(
        node,
        0,
        ng::PickKind::Image,
    )));
    assert_eq!(
        harness.app().nature.graph.node(node).unwrap().strings[0],
        "textures/bark.png"
    );
    // The Material node has a base-material slot that takes .mat files only.
    harness.dispatch(Message::NatureGen(ng::Edit::AddNode("material")));
    let material = harness
        .app()
        .nature
        .graph
        .nodes
        .iter()
        .find(|n| n.op == "material")
        .unwrap()
        .id;
    harness.dispatch(Message::NatureGen(ng::Edit::Select(Some(material))));
    assert!(harness.has(Id::from(id::naturegen_param("slot_0_material"))));
    harness.app.asset_drag = Some(1);
    harness.dispatch(Message::NatureGen(ng::Edit::DropAsset(
        material,
        0,
        ng::PickKind::Material,
    )));
    assert_eq!(
        harness.app().nature.graph.node(material).unwrap().strings[0],
        "materials/wet.mat"
    );
}

#[test]
fn texture_nodes_are_summed_into_baked_maps_when_placed() {
    use crate::uikit::nature::NatureGraph;
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("nature-texture");
    std::fs::create_dir_all(root.join("textures")).unwrap();
    image::save_buffer(
        root.join("textures/bark.png"),
        &[120u8, 90, 60, 255].repeat(16 * 16),
        16,
        16,
        image::ColorType::Rgba8,
    )
    .unwrap();
    harness.app.asset_root_override = Some(root.clone());
    let mut g = NatureGraph::new();
    let ico = g.add("icosphere", iced::Point::new(0.0, 0.0));
    let tex = g.add("texture", iced::Point::new(1.0, 0.0));
    g.set_string(tex, 0, "textures/bark.png".to_owned());
    let part = g.add("part", iced::Point::new(2.0, 0.0));
    let out = g.add("output", iced::Point::new(3.0, 0.0));
    assert!(g.connect(ico, 0, tex, 0) && g.connect(tex, 0, part, 0) && g.connect(part, 0, out, 0));
    harness.app.nature.graph = g;
    harness.app.nature.refresh();
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let status = harness.app().nature.status.clone().unwrap_or_default();
    assert!(
        status.starts_with("Placed ") && !status.contains("problems"),
        "{status}"
    );
    let materials = crate::uikit::asset_folders::resolve(
        &root,
        crate::uikit::asset_folders::AssetFolder::NatureMaterials,
    );
    let names: Vec<String> = std::fs::read_dir(&materials)
        .unwrap()
        .filter_map(|e| e.ok())
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .collect();
    let textures = crate::uikit::asset_folders::resolve(
        &root,
        crate::uikit::asset_folders::AssetFolder::NatureTextures,
    );
    assert!(
        textures
            .read_dir()
            .unwrap()
            .filter_map(|e| e.ok())
            .any(|e| {
                let n = e.file_name().to_string_lossy().into_owned();
                n.starts_with("custom_") && n.ends_with("_albedo.png")
            }),
        "the baked maps go to Textures/Nature"
    );
    let mat = names
        .iter()
        .find(|n| n.starts_with("custom_") && n.ends_with(".mat"))
        .unwrap();
    let text = std::fs::read_to_string(materials.join(mat)).unwrap();
    assert!(
        text.contains("albedo_map") && text.contains("_albedo.png"),
        "{text}"
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn material_node_starts_from_a_project_material_and_keeps_its_maps() {
    use crate::uikit::nature::NatureGraph;
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("nature-base-material");
    std::fs::create_dir_all(root.join("materials")).unwrap();
    harness.app.asset_root_override = Some(root.clone());
    {
        let engine = harness.app.engine.as_mut().expect("engine");
        let mut base = engine.new_material().unwrap();
        base.roughness = 0.37;
        base.albedo_map = "textures/base.png".to_owned();
        engine
            .save_material(&root.join("materials/wet.mat").to_string_lossy(), &base)
            .unwrap();
    }
    let mut g = NatureGraph::new();
    let ico = g.add("icosphere", iced::Point::new(0.0, 0.0));
    let material = g.add_with("material", iced::Point::new(1.0, 0.0), &[("sway", 0.02)]);
    g.set_string(
        material,
        0,
        root.join("materials/wet.mat").display().to_string(),
    );
    let part = g.add("part", iced::Point::new(2.0, 0.0));
    let out = g.add("output", iced::Point::new(3.0, 0.0));
    assert!(
        g.connect(ico, 0, material, 0)
            && g.connect(material, 0, part, 0)
            && g.connect(part, 0, out, 0)
    );
    harness.app.nature.graph = g;
    harness.app.nature.refresh();
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let status = harness.app().nature.status.clone().unwrap_or_default();
    assert!(
        status.starts_with("Placed ") && !status.contains("problems"),
        "{status}"
    );
    let mat = std::fs::read_dir(crate::uikit::asset_folders::resolve(
        &root,
        crate::uikit::asset_folders::AssetFolder::NatureMaterials,
    ))
    .unwrap()
    .filter_map(|e| e.ok())
    .map(|e| e.path())
    .find(|p| p.extension().is_some_and(|x| x == "mat"))
    .expect("a material was written");
    let text = std::fs::read_to_string(mat).unwrap();
    assert!(
        text.contains("roughness 0.37"),
        "base settings kept: {text}"
    );
    assert!(text.contains("textures/base.png"), "base maps kept: {text}");
    assert!(
        text.contains("wind_strength 0.02"),
        "node wind added: {text}"
    );
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn assets_pane_creates_renames_moves_and_trashes_files() {
    use crate::uikit::panels::assetmanager::FileAction;
    let root = test_temp_root("assets-files");
    std::fs::create_dir_all(root.join("Textures")).unwrap();
    std::fs::write(root.join("bark.png"), b"png").unwrap();
    let mut harness = Harness::empty();
    harness.app.asset_root_override = Some(root.clone());
    let asset = |name: &str, directory: bool| doppio_engine::AssetInfo {
        name: name.into(),
        relative_path: name.into(),
        kind: if directory { "folder" } else { "image" }.into(),
        size_bytes: 1,
        is_directory: directory,
    };
    harness.app.assets = vec![asset("bark.png", false), asset("Textures", true)];
    assert!(harness.has(Id::from(id::ASSET_NEW_FOLDER)));
    // Rename and delete only appear with something selected.
    assert!(!harness.has(Id::from(id::ASSET_RENAME)));
    harness.dispatch(Message::AssetPressed(0));
    harness.app.asset_drag = None;
    assert!(harness.has(Id::from(id::ASSET_RENAME)) && harness.has(Id::from(id::ASSET_DELETE)));

    // New folder: type a name, OK.
    harness.dispatch(Message::AssetFile(FileAction::NewFolder));
    assert!(harness.has(Id::from(id::ASSET_EDIT_NAME)));
    harness.dispatch(Message::AssetFile(FileAction::Text("Moss".into())));
    harness.dispatch(Message::AssetFile(FileAction::Commit));
    assert!(root.join("Moss").is_dir());
    assert!(harness.app().asset_edit.is_none());

    // A bad name keeps the editor open and changes nothing.
    harness.dispatch(Message::AssetFile(FileAction::NewFolder));
    harness.dispatch(Message::AssetFile(FileAction::Text("a/b".into())));
    harness.dispatch(Message::AssetFile(FileAction::Commit));
    assert!(harness.app().asset_edit.is_some());
    harness.dispatch(Message::AssetFile(FileAction::Cancel));

    // Rename the selected file.
    harness.app.assets = vec![asset("bark.png", false), asset("Textures", true)];
    harness.dispatch(Message::AssetPressed(0));
    harness.app.asset_drag = None;
    harness.dispatch(Message::AssetFile(FileAction::Rename));
    harness.dispatch(Message::AssetFile(FileAction::Text("oak.png".into())));
    harness.dispatch(Message::AssetFile(FileAction::Commit));
    assert!(root.join("oak.png").is_file() && !root.join("bark.png").exists());

    // Dragging a file onto a folder moves it.
    harness.app.assets = vec![asset("oak.png", false), asset("Textures", true)];
    harness.app.asset_drag = Some(0);
    harness.dispatch(Message::AssetReleased(1));
    assert!(root.join("Textures/oak.png").is_file() && !root.join("oak.png").exists());

    // Delete asks first, then moves to the hidden trash.
    harness.app.assets = vec![asset("Textures", true)];
    harness.dispatch(Message::AssetPressed(0));
    harness.app.asset_drag = None;
    harness.dispatch(Message::AssetFile(FileAction::Delete));
    assert!(harness.has(Id::from(id::ASSET_DELETE_CONFIRM)));
    assert!(
        root.join("Textures").exists(),
        "nothing is deleted before confirming"
    );
    harness.dispatch(Message::AssetFile(FileAction::ConfirmDelete));
    assert!(!root.join("Textures").exists());
    assert!(root.join(".trash/Textures/oak.png").is_file());
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn mesh_renderer_modifiers_change_how_the_same_material_renders() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    {
        let id = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_rotation(id, [-60.0, 30.0, 0.0]).unwrap();
    }
    let plane = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Plane")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(plane));
    let id = harness.app().entities[harness.app().selected].id;
    let root = test_temp_root("modifiers");
    std::fs::create_dir_all(&root).unwrap();
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_position(id, [0.0, 0.0, 0.0]).unwrap();
    engine.set_scale(id, [6.0, 1.0, 6.0]).unwrap();
    // A mid-grey material: the modifiers change how this very material renders.
    let material = doppio_engine::Material {
        albedo: [0.4, 0.4, 0.4, 1.0],
        roughness: 0.8,
        ..doppio_engine::Material::default()
    };
    let mat = root.join("grey.mat");
    engine
        .save_material(&mat.to_string_lossy(), &material)
        .unwrap();
    engine.apply_material(id, &mat.to_string_lossy()).unwrap();
    let renderer = add_component(engine, id, "Mesh Renderer").expect("Mesh Renderer");
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.6,
            pitch: 1.2,
            distance: 5.0,
            focus: [0.0, 0.0, 0.0],
        })
        .unwrap();
    let mean = |pixels: &[u8]| -> [f32; 3] {
        let n = (pixels.len() / 4) as f32;
        let mut sum = [0.0f32; 3];
        for p in pixels.chunks(4) {
            for (total, value) in sum.iter_mut().zip(p) {
                *total += *value as f32;
            }
        }
        [sum[0] / n, sum[1] / n, sum[2] / n]
    };
    let render = |engine: &mut doppio_engine::Engine| mean(&render_entity_view(engine));
    let set = |engine: &mut doppio_engine::Engine, field: &str, value| {
        set_component_field(engine, id, renderer, field, value).expect(field);
    };
    let _ = render(engine); // let the scene settle
    let base = render(engine);

    // Snow covers the faces turned up: the cube gets brighter.
    set(engine, "Snow", doppio_engine::FieldValue::F32(1.0));
    let snowy = render(engine);
    assert!(
        snowy.iter().sum::<f32>() > base.iter().sum::<f32>() + 3.0,
        "snow {snowy:?} vs {base:?}"
    );
    set(engine, "Snow", doppio_engine::FieldValue::F32(0.0));

    // A red tint removes green and blue.
    set(
        engine,
        "Tint",
        doppio_engine::FieldValue::Color([1.0, 0.0, 0.0, 1.0]),
    );
    let red = render(engine);
    assert!(red[1] < base[1] - 2.0, "tint {red:?} vs {base:?}");
    set(
        engine,
        "Tint",
        doppio_engine::FieldValue::Color([1.0, 1.0, 1.0, 1.0]),
    );

    // Wet darkens the albedo.
    set(engine, "Wetness", doppio_engine::FieldValue::F32(1.0));
    let wet = render(engine);
    assert!(
        wet.iter().sum::<f32>() < base.iter().sum::<f32>() - 1.0,
        "wet {wet:?} vs {base:?}"
    );
    // Back to neutral: the original look returns, so nothing was baked into the material.
    set(engine, "Wetness", doppio_engine::FieldValue::F32(0.0));
    let dry = render(engine);
    assert!((dry.iter().sum::<f32>() - base.iter().sum::<f32>()).abs() < 1.0);
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn material_modifiers_round_trip_through_the_engine_and_a_material_modifier_renders() {
    let Some(harness) = engine_harness() else {
        return;
    };
    let engine = harness.app.engine.as_ref().expect("engine");
    assert!(engine.capi_version() >= 10, "modifiers need C ABI v10");
    let root = test_temp_root("modifier-material");
    std::fs::create_dir_all(&root).unwrap();
    let mut material = engine.new_material().unwrap();
    assert_eq!(material.mod_tint, [1.0; 3]);
    material.mod_tint = [0.9, 0.8, 0.7];
    material.mod_wetness = 0.4;
    material.mod_snow = 0.5;
    material.mod_age = 0.6;
    let path = root.join("m.mat");
    engine
        .save_material(&path.to_string_lossy(), &material)
        .unwrap();
    let loaded = engine.load_material(&path.to_string_lossy()).unwrap();
    assert_eq!(loaded.mod_tint, [0.9, 0.8, 0.7]);
    assert!((loaded.mod_wetness - 0.4).abs() < 1e-5);
    assert!((loaded.mod_snow - 0.5).abs() < 1e-5);
    assert!((loaded.mod_age - 0.6).abs() < 1e-5);
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn render_stats_count_draws_and_report_gpu_capabilities() {
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let engine = harness.app.engine.as_ref().expect("engine");
    assert!(engine.capi_version() >= 11, "render stats need C ABI v11");
    let plane = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Plane")
        .unwrap();
    let cube = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Cube")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(plane));
    let id = harness.app().entities[harness.app().selected].id;
    harness.dispatch(Message::CreatePrefab(cube));
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_position(id, [0.0, 0.0, 0.0]).unwrap();
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.6,
            pitch: 1.0,
            distance: 6.0,
            focus: [0.0, 0.0, 0.0],
        })
        .unwrap();
    let _ = render_entity_view(engine);
    let stats = engine.render_stats().expect("stats");
    assert!(stats.draw_calls >= 1, "{stats:?}");
    assert!(stats.triangles >= 2, "{stats:?}");
    assert!(stats.meshes >= 1);
    assert!(stats.total_ms > 0.0 && stats.total_ms >= stats.opaque_ms);
    // The device reports what GPU-driven rendering can use; SDL3 GPU has no mesh shaders.
    assert!(stats.indirect_draw && stats.graphics_storage_buffers && stats.compute_storage_write);
    assert!(!stats.mesh_shaders);
}

#[test]
fn micromesh_bake_through_the_engine_is_deterministic_and_bounded() {
    let Some(harness) = engine_harness() else {
        return;
    };
    let engine = harness.app.engine.as_ref().expect("engine");
    assert!(engine.capi_version() >= 12, "micromesh needs C ABI v12");
    // A rock mesh from the Nature generator: icosphere of 20480 triangles.
    let (directions, faces) = crate::uikit::nature::rock::icosphere(5);
    let positions: Vec<[f32; 3]> = directions.iter().map(|d| [d[0], d[1], d[2]]).collect();
    let indices: Vec<u32> = faces.iter().flat_map(|f| [f[0], f[1], f[2]]).collect();
    let bake = engine.micromesh_build(&positions, &indices).expect("bake");
    assert_eq!(bake.source_triangles as usize, faces.len());
    assert!(bake.clusters > faces.len() as u32 / 128, "{bake:?}");
    assert!(bake.levels >= 5);
    assert_eq!(bake.root_clusters, 1, "a closed mesh ends in one root");
    assert!(bake.max_cluster_triangles <= 128);
    assert!(bake.max_error > 0.0);
    assert!(!bake.bytes.is_empty());
    // Offline work must be reproducible: same mesh, same bytes.
    let again = engine.micromesh_build(&positions, &indices).expect("bake");
    assert_eq!(bake.bytes, again.bytes);
    // Garbage is refused.
    assert!(engine.micromesh_build(&positions, &[0, 1]).is_err());
}

/// Frames of an animated Storm for the documentation (`DOPPIO_WATER_ANIM=<dir>`).
#[test]
fn water_animation_frames_for_docs() {
    use crate::uikit::panels::water_generator as wg;
    let Ok(dir) = std::env::var("DOPPIO_WATER_ANIM") else {
        return;
    };
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("water-anim");
    harness.app.asset_root_override = Some(root.clone());
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    {
        let id = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(id, [0.0, 8.0, 0.0]).unwrap();
        engine.set_rotation(id, [-35.0, 140.0, 0.0]).unwrap();
    }
    let storm = harness
        .app()
        .watergen
        .presets
        .iter()
        .position(|p| p.name == "Storm")
        .unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::ApplyPreset(storm)));
    harness.dispatch(Message::WaterGen(wg::Edit::Place));
    let id = harness.app().entities[harness.app().selected].id;
    let engine = harness.app.engine.as_mut().expect("engine");
    engine.set_position(id, [0.0, 1.0, 0.0]).unwrap();
    for _ in 0..30 {
        engine.tick(0.05);
    }
    for i in 0..48 {
        engine
            .set_camera(doppio_engine::Camera {
                yaw: 0.5 + i as f32 * 0.004,
                pitch: 0.3,
                distance: 46.0,
                focus: [0.0, 1.0, 0.0],
            })
            .unwrap();
        engine.tick(0.09);
        let frame = render_entity_view_sized(engine, 640, 360);
        image::save_buffer(
            format!("{dir}/storm_{i:03}.png"),
            &frame,
            640,
            360,
            image::ColorType::Rgba8,
        )
        .unwrap();
    }
}

/// Frames of an oak in the wind for the documentation (`DOPPIO_NATURE_ANIM=<dir>`). The wind is time
/// based and advances one step per rendered frame, so most frames are rendered small and thrown
/// away: the GIF is a time-lapse.
#[test]
fn nature_wind_frames_for_docs() {
    use crate::uikit::panels::nature_generator as ng;
    let Ok(dir) = std::env::var("DOPPIO_NATURE_ANIM") else {
        return;
    };
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("nature-wind");
    harness.app.asset_root_override = Some(root.clone());
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    {
        let id = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_position(id, [0.0, 8.0, 0.0]).unwrap();
        engine.set_rotation(id, [-38.0, 130.0, 0.0]).unwrap();
    }
    let plane = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Plane")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(plane));
    let ground = harness.app().entities[harness.app().selected].id;
    {
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_scale(ground, [30.0, 1.0, 30.0]).unwrap();
        let mut material = engine.new_material().unwrap();
        material.albedo = [0.22, 0.34, 0.15, 1.0];
        material.roughness = 0.95;
        material.uv_object_scale = false;
        let path = root.join("ground.mat");
        engine
            .save_material(&path.to_string_lossy(), &material)
            .unwrap();
        engine
            .apply_material(ground, &path.to_string_lossy())
            .unwrap();
    }
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Oak")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(oak)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let engine = harness.app.engine.as_mut().expect("engine");
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.8,
            pitch: 0.25,
            distance: 9.0,
            focus: [0.0, 3.4, 0.0],
        })
        .unwrap();
    let _ = render_entity_view_sized(engine, 640, 360);
    for i in 0..60 {
        for _ in 0..8 {
            let _ = render_entity_view_sized(engine, 16, 9);
        }
        let frame = render_entity_view_sized(engine, 640, 360);
        image::save_buffer(
            format!("{dir}/oak_{i:03}.png"),
            &frame,
            640,
            360,
            image::ColorType::Rgba8,
        )
        .unwrap();
    }
}

/// Regression: a water surface whose entity has an authored material (the Water Generator writes
/// one) must still be waved. A flat-looking sea once shipped because the authored-material branch
/// skipped the GPU wave setup.
#[test]
fn placed_water_with_its_material_is_still_waved() {
    use crate::uikit::panels::water_generator as wg;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("water-waved");
    harness.app.asset_root_override = Some(root.clone());
    let light = crate::uikit::prefabs::PREFABS
        .iter()
        .position(|p| p.label == "Directional Light")
        .unwrap();
    harness.dispatch(Message::CreatePrefab(light));
    {
        let id = harness.app().entities[harness.app().selected].id;
        let engine = harness.app.engine.as_mut().expect("engine");
        engine.set_rotation(id, [-35.0, 140.0, 0.0]).unwrap();
    }
    let storm = harness
        .app()
        .watergen
        .presets
        .iter()
        .position(|p| p.name == "Storm")
        .unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::ApplyPreset(storm)));
    harness.dispatch(Message::WaterGen(wg::Edit::Place));
    let id = harness.app().entities[harness.app().selected].id;
    let engine = harness.app.engine.as_mut().expect("engine");
    assert!(
        !engine.entity_material(id).unwrap_or_default().is_empty(),
        "the generator gives the water its own material"
    );
    engine.set_position(id, [0.0, 1.0, 0.0]).unwrap();
    for _ in 0..30 {
        engine.tick(0.05);
    }
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.5,
            pitch: 0.3,
            distance: 46.0,
            focus: [0.0, 1.0, 0.0],
        })
        .unwrap();
    let frame = render_entity_view_sized(engine, 480, 270);
    // The waves must run in the vertex shader (the flat grid is only a carrier).
    let stats = engine.render_stats().expect("stats");
    assert!(
        stats.gpu_water_draws >= 1,
        "the water is not waved: {stats:?}"
    );
    // Mean luminance change between neighbouring pixels over the sea (the lower 60 %): flat water is
    // a smooth gradient, waves, ripples and foam are not.
    let luma = |x: usize, y: usize| {
        let i = (y * 480 + x) * 4;
        0.3 * frame[i] as f32 + 0.59 * frame[i + 1] as f32 + 0.11 * frame[i + 2] as f32
    };
    let (mut sum, mut n) = (0.0f32, 0.0f32);
    for y in 108..269 {
        for x in 0..479 {
            sum += (luma(x + 1, y) - luma(x, y)).abs() + (luma(x, y + 1) - luma(x, y)).abs();
            n += 2.0;
        }
    }
    let roughness = sum / n;
    assert!(roughness > 1.5, "the sea looks flat: {roughness:.2}");
    std::fs::remove_dir_all(root).ok();
}

/// Regression: vegetation must keep its wind and translucency now that preset materials are project
/// assets (nodes pointing at `.mat` files). Leaves and trunks get derived copies; rocks stay still.
#[test]
fn placed_vegetation_keeps_wind_and_translucency_while_rocks_stay_still() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("nature-foliage");
    harness.app.asset_root_override = Some(root.clone());
    let read_material = |harness: &Harness, entity: u32| -> String {
        let engine = harness.app.engine.as_ref().expect("engine");
        let path = engine.entity_material(entity).unwrap_or_default();
        let resolved = engine
            .resolve_path(&path)
            .ok()
            .filter(|p| std::path::Path::new(p).exists())
            .or_else(|| engine.asset_path(&path).ok())
            .unwrap_or(path);
        std::fs::read_to_string(&resolved).unwrap_or_else(|e| format!("unreadable {resolved}: {e}"))
    };
    let place = |harness: &mut Harness, name: &str| -> Vec<(String, String)> {
        let index = harness
            .app()
            .nature
            .presets
            .iter()
            .position(|p| p.name == name)
            .unwrap();
        harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(index)));
        harness.dispatch(Message::NatureGen(ng::Edit::Place));
        let group = harness.app().entities[harness.app().selected].id;
        let children: Vec<(u32, String)> = harness
            .app()
            .entities
            .iter()
            .filter(|e| e.parent == Some(group))
            .map(|e| (e.id, e.name.clone()))
            .collect();
        children
            .into_iter()
            .map(|(id, name)| (name, read_material(harness, id)))
            .collect()
    };
    let oak = place(&mut harness, "Oak");
    let leaves = oak
        .iter()
        .find(|(n, _)| n.ends_with("leaves"))
        .expect("leaves");
    let trunk = oak
        .iter()
        .find(|(n, _)| n.ends_with("trunk"))
        .expect("trunk");
    assert!(leaves.1.contains("wind_strength 0.012"), "{}", leaves.1);
    assert!(leaves.1.contains("wind_flutter 0.6"), "{}", leaves.1);
    assert!(leaves.1.contains("translucency 0.55"), "{}", leaves.1);
    assert!(trunk.1.contains("wind_strength 0.012"), "{}", trunk.1);
    assert!(trunk.1.contains("wind_flutter 0"), "{}", trunk.1);
    let rock = place(&mut harness, "Boulder");
    for (name, text) in &rock {
        assert!(
            text.contains("wind_strength 0\n") && text.contains("translucency 0\n"),
            "{name} must stay still: {text}"
        );
    }
    std::fs::remove_dir_all(root).ok();
}

/// The frame the engine draws, as pixels, after a short settle.
fn frame_of(engine: &mut doppio_engine::Engine, focus: [f32; 3]) -> Vec<u8> {
    engine
        .set_camera(doppio_engine::Camera {
            yaw: 0.9,
            pitch: 0.38,
            distance: 21.0,
            focus,
        })
        .unwrap();
    let _ = render_entity_view_sized(engine, 320, 240);
    render_entity_view_sized(engine, 320, 240)
}

fn differing_pixels(a: &[u8], b: &[u8]) -> usize {
    a.chunks(4)
        .zip(b.chunks(4))
        .filter(|(x, y)| x.iter().zip(y.iter()).any(|(p, q)| p.abs_diff(*q) > 24))
        .count()
}

#[test]
fn a_tree_placed_without_a_project_is_visible_in_the_viewport() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    // No project and no asset root: the generator falls back to its scratch folder, and the engine must find the
    // meshes it wrote there by the relative paths it stored (it resolves them against the project root).
    let scratch = test_temp_root("tree-no-project");
    std::fs::create_dir_all(&scratch).unwrap();
    harness.app.scratch_override = Some(scratch.clone());
    let before = frame_of(harness.app.engine.as_mut().unwrap(), [0.0, 3.0, 0.0]);
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Oak")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(oak)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    assert!(
        harness
            .app()
            .nature
            .status
            .as_deref()
            .is_some_and(|text| text.starts_with("Placed Oak")),
        "{:?}",
        harness.app().nature.status
    );
    let after = frame_of(harness.app.engine.as_mut().unwrap(), [0.0, 3.0, 0.0]);
    let changed = differing_pixels(&before, &after);
    assert!(
        changed > 400,
        "the placed tree draws nothing: only {changed} pixels changed"
    );
    // The meshes are on disk where the entities say they are.
    assert!(scratch.join("Models/Nature/oak_trunk.obj").is_file());
}

#[test]
fn placing_a_tree_saves_its_recipes_materials_textures_and_a_prefab() {
    use crate::uikit::panels::nature_generator as ng;
    use crate::uikit::{prefab_file, recipe};
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("tree-bundle");
    std::fs::create_dir_all(&root).unwrap();
    harness.app.asset_root_override = Some(root.clone());
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Oak")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(oak)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let status = harness.app().nature.status.clone().unwrap_or_default();
    assert!(
        status.starts_with("Placed Oak") && status.contains("oak.prefab"),
        "{status}"
    );

    // The recipe lists everything the tree is made of.
    let recipe_path = root.join("Models/Nature/oak.generator.nature");
    let tree = recipe::read(&recipe_path).expect("the nature recipe is written when placing");
    assert_eq!(tree.kind(), Some(recipe::Kind::Nature));
    let has = |suffix: &str| tree.products.iter().any(|path| path.ends_with(suffix));
    assert!(
        has("oak_trunk.obj") && has("oak_leaves.obj"),
        "{:?}",
        tree.products
    );
    assert!(has(".mat"), "{:?}", tree.products);
    assert!(has(".png"), "textures are listed: {:?}", tree.products);
    assert!(has("oak.prefab"), "{:?}", tree.products);
    for product in &tree.products {
        assert!(
            root.join(product).is_file(),
            "{product} is listed but missing"
        );
    }

    // The prefab points back at the recipe and rebuilds the same tree.
    let prefab = prefab_file::read(&root.join("Prefabs/oak.prefab")).expect("prefab");
    assert_eq!(prefab.generator, "Models/Nature/oak.generator.nature");
    let names: Vec<&str> = prefab.entities.iter().map(|e| e.name.as_str()).collect();
    assert_eq!(names, ["Oak", "Oak trunk", "Oak leaves"]);
    let before = harness.app().entities.len();
    harness.dispatch(Message::PlacePrefab("Prefabs/oak.prefab".to_owned()));
    assert_eq!(
        harness.app().entities.len(),
        before + 3,
        "a copy of the tree"
    );
    let engine = harness.app.engine.as_ref().expect("engine");
    let mut meshes = Vec::new();
    for index in 0..engine.entity_count() {
        let info = engine.entity_at(index).unwrap();
        for data in engine.entity_component_data(info.id) {
            if data.info.name == "Mesh Filter" {
                if let Some(doppio_engine::FieldValue::String(path)) = data.values.get(1) {
                    meshes.push(path.clone());
                }
            }
        }
    }
    assert_eq!(
        meshes
            .iter()
            .filter(|path| path.ends_with("oak_trunk.obj"))
            .count(),
        2,
        "the placed copy keeps its meshes: {meshes:?}"
    );

    // From the prefab, or from a model it uses, the Nature tab gets the graph back.
    for path in ["Prefabs/oak.prefab", "Models/Nature/oak_trunk.obj"] {
        harness.app.nature.name = "other".to_owned();
        harness.dispatch(Message::OpenGeneratorFor(path.to_owned()));
        assert_eq!(harness.app().nature.name, "Oak", "opening {path}");
    }
}

#[test]
fn exporting_a_tree_leaves_the_scene_alone_and_keeps_the_library_nodes_in_the_recipe() {
    use crate::uikit::panels::nature_generator as ng;
    use crate::uikit::{nature, recipe};
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("tree-export");
    std::fs::create_dir_all(&root).unwrap();
    harness.app.asset_root_override = Some(root.clone());
    let library_nodes = |graph: &nature::graph::NatureGraph| {
        graph
            .nodes
            .iter()
            .filter(|node| matches!(node.op, "mat_library" | "mat_cards"))
            .count()
    };
    // Presets are materialised as soon as they are applied, so add a library material node by hand: it is what
    // the user is still authoring when they press Export.
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Oak")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(oak)));
    harness
        .app
        .nature
        .graph
        .add("mat_library", iced::Point::new(0.0, 0.0));
    let authored = library_nodes(&harness.app().nature.graph);
    assert_eq!(authored, 1);
    let entities = harness.app().entities.len();
    let slug = nature::slug(&harness.app().nature.name);
    harness.dispatch(Message::NatureGen(ng::Edit::Export));
    assert_eq!(
        harness.app().entities.len(),
        entities,
        "export adds nothing to the scene"
    );
    assert!(root.join(format!("Prefabs/{slug}.prefab")).is_file());
    let saved =
        recipe::read(&root.join(format!("Models/Nature/{slug}.generator.nature"))).expect("recipe");
    let (_, graph, _) = nature::from_text(&saved.graph).expect("graph");
    assert_eq!(
        library_nodes(&graph),
        authored,
        "reopening the recipe gives the library node back, not Your Material"
    );
    assert_eq!(
        library_nodes(&harness.app().nature.graph),
        0,
        "the open graph now points at the files it wrote"
    );
}

#[test]
fn placing_water_or_terrain_saves_the_recipe_so_the_graph_is_never_lost() {
    use crate::uikit::panels::{terrain_generator as tg, water_generator as wg};
    use crate::uikit::recipe;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let engine = harness.app.engine.as_ref().expect("engine");
    if engine.capi_version() < 8 {
        return;
    }
    let root = test_temp_root("place-recipes");
    std::fs::create_dir_all(&root).unwrap();
    harness.app.asset_root_override = Some(root.clone());
    let sea = harness
        .app()
        .watergen
        .presets
        .iter()
        .position(|p| p.name == "Open Sea")
        .unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::ApplyPreset(sea)));
    harness.dispatch(Message::WaterGen(wg::Edit::Place));
    let water = recipe::read(&root.join("Water/open_sea.generator.water")).expect("water recipe");
    assert_eq!(water.kind(), Some(recipe::Kind::Water));

    let alpine = harness
        .app()
        .terraingen
        .presets
        .iter()
        .position(|p| p.name == "Alpine Range")
        .unwrap();
    harness.dispatch(Message::TerrainGen(tg::Edit::ApplyPreset(alpine)));
    harness.dispatch(Message::TerrainGen(tg::Edit::Place));
    let terrain =
        recipe::read(&root.join("Terrain/alpine_range.generator.terrain")).expect("terrain recipe");
    assert_eq!(terrain.kind(), Some(recipe::Kind::Terrain));

    // And the graph comes back from the recipe.
    harness.app.terraingen.name = "other".to_owned();
    harness.dispatch(Message::OpenGeneratorFor(
        "Terrain/alpine_range.generator.terrain".to_owned(),
    ));
    assert_eq!(harness.app().terraingen.name, "Alpine Range");
}

#[test]
fn a_tree_in_a_project_with_capitalised_folders_is_visible_and_its_recipes_are_listed() {
    use crate::uikit::panels::{
        nature_generator as ng, terrain_generator as tg, water_generator as wg,
    };
    let Some(mut harness) = engine_harness() else {
        return;
    };
    if harness.app.engine.as_ref().expect("engine").capi_version() < 8 {
        return;
    }
    // The layout Doppio creates: `project.caffeine`, `Scenes/`, `Assets/Raw/`. The engine must take the folder with
    // the manifest as the project root (not `Scenes`) and find meshes stored as project paths.
    let project = test_temp_root("capitalised-project");
    std::fs::create_dir_all(project.join("Scenes")).unwrap();
    std::fs::create_dir_all(project.join("Assets/Raw")).unwrap();
    std::fs::write(project.join("project.caffeine"), "{}\n").unwrap();
    let scene = project.join("Scenes/main.caf");
    {
        let engine = harness.app.engine.as_mut().unwrap();
        engine.save_scene(&scene.to_string_lossy()).unwrap();
        engine.load_scene(&scene.to_string_lossy()).unwrap();
        engine
            .set_asset_root(&project.join("Assets/Raw").to_string_lossy())
            .unwrap();
    }
    let before = frame_of(harness.app.engine.as_mut().unwrap(), [0.0, 3.0, 0.0]);
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Oak")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(oak)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let after = frame_of(harness.app.engine.as_mut().unwrap(), [0.0, 3.0, 0.0]);
    let changed = differing_pixels(&before, &after);
    assert!(
        changed > 400,
        "the placed tree draws nothing: only {changed} pixels changed ({:?})",
        harness.app().nature.status
    );
    let sea = harness
        .app()
        .watergen
        .presets
        .iter()
        .position(|p| p.name == "Open Sea")
        .unwrap();
    harness.dispatch(Message::WaterGen(wg::Edit::ApplyPreset(sea)));
    harness.dispatch(Message::WaterGen(wg::Edit::Place));
    let alpine = harness
        .app()
        .terraingen
        .presets
        .iter()
        .position(|p| p.name == "Alpine Range")
        .unwrap();
    harness.dispatch(Message::TerrainGen(tg::Edit::ApplyPreset(alpine)));
    harness.dispatch(Message::TerrainGen(tg::Edit::Place));
    let listed: Vec<String> = harness
        .app()
        .assets
        .iter()
        .map(|asset| asset.relative_path.clone())
        .collect();
    for expected in [
        "Water/open_sea.generator.water",
        "Terrain/alpine_range.generator.terrain",
        "Models/Nature/oak.generator.nature",
        "Prefabs/oak.prefab",
    ] {
        assert!(
            listed.iter().any(|path| path == expected),
            "{expected} is not in the Assets pane: {listed:?}"
        );
        assert!(project.join("Assets/Raw").join(expected).is_file());
    }
    std::fs::remove_dir_all(project).ok();
}

#[test]
fn editing_a_library_leaf_texture_writes_a_graded_copy_and_leaves_the_library_asset_alone() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let project = test_temp_root("edit-library-material");
    std::fs::create_dir_all(project.join("Scenes")).unwrap();
    std::fs::create_dir_all(project.join("Assets/Raw")).unwrap();
    std::fs::write(project.join("project.caffeine"), "{}\n").unwrap();
    let scene = project.join("Scenes/main.caf");
    {
        let engine = harness.app.engine.as_mut().unwrap();
        engine.save_scene(&scene.to_string_lossy()).unwrap();
        engine.load_scene(&scene.to_string_lossy()).unwrap();
        engine
            .set_asset_root(&project.join("Assets/Raw").to_string_lossy())
            .unwrap();
    }
    // The preset's own graph, with its library nodes still in it (applying a preset turns them into files).
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .find(|p| p.name == "Oak")
        .unwrap()
        .graph();
    harness.app.nature.load(oak);
    let cards = harness
        .app()
        .nature
        .graph
        .nodes
        .iter()
        .find(|node| node.op == "mat_cards")
        .map(|node| node.id)
        .expect("the Oak has a leaf texture node");
    let nodes_before = harness.app().nature.graph.nodes.len();
    harness.dispatch(Message::NatureGen(ng::Edit::EditMaterial(cards)));
    let edit = harness.app().nature.selected.expect("the edit node");
    assert_eq!(
        harness.app().nature.graph.node(edit).unwrap().op,
        "mat_edit"
    );
    assert_eq!(harness.app().nature.graph.nodes.len(), nodes_before + 1);
    // Asking again selects the same node instead of stacking another.
    harness.dispatch(Message::NatureGen(ng::Edit::Select(Some(cards))));
    harness.dispatch(Message::NatureGen(ng::Edit::EditMaterial(cards)));
    assert_eq!(harness.app().nature.selected, Some(edit));
    assert_eq!(harness.app().nature.graph.nodes.len(), nodes_before + 1);
    // Hue 90 and brighter.
    harness.dispatch(Message::NatureGen(ng::Edit::SetParam(edit, 0, 90.0)));
    harness.dispatch(Message::NatureGen(ng::Edit::SetParam(edit, 2, 1.6)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let status = harness.app().nature.status.clone().unwrap_or_default();
    assert!(status.starts_with("Placed Oak"), "{status}");

    let list = |sub: &str| -> Vec<String> {
        std::fs::read_dir(project.join("Assets/Raw").join(sub))
            .map(|dir| {
                dir.flatten()
                    .map(|entry| entry.file_name().to_string_lossy().to_string())
                    .collect()
            })
            .unwrap_or_default()
    };
    let materials = list("Materials/Nature");
    let textures = list("Textures/Nature");
    let edited_mat = materials
        .iter()
        .find(|name| name.contains("_e") && name.ends_with(".mat"))
        .unwrap_or_else(|| panic!("no edited material in {materials:?}"));
    assert!(
        textures
            .iter()
            .any(|name| name.contains("_e") && name.ends_with("_albedo.png")),
        "no graded colour map in {textures:?}"
    );
    // The library material it started from is still there, untouched and not graded.
    assert!(
        materials
            .iter()
            .any(|name| name.starts_with("atlas") && !name.contains("_e")),
        "{materials:?}"
    );
    let text =
        std::fs::read_to_string(project.join("Assets/Raw/Materials/Nature").join(edited_mat))
            .unwrap();
    assert!(text.contains("_albedo.png"), "{text}");
    std::fs::remove_dir_all(project).ok();
}

#[test]
fn a_placed_leaf_material_is_lifted_and_carries_a_height_map() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let root = test_temp_root("leaf-material");
    std::fs::create_dir_all(&root).unwrap();
    harness.app.asset_root_override = Some(root.clone());
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Oak")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(oak)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let textures = root.join("Textures/Nature");
    let leaf = image::open(textures.join("atlas___broadleaf_mix_leaf.png"))
        .or_else(|_| {
            // Whatever the atlas is called, the first `_leaf.png` is the leaf picture.
            let first = std::fs::read_dir(&textures)
                .unwrap()
                .flatten()
                .find(|e| e.file_name().to_string_lossy().ends_with("_leaf.png"))
                .expect("a leaf picture");
            image::open(first.path())
        })
        .expect("opens")
        .to_rgba8();
    assert!(
        leaf.width() >= 2048 && leaf.height() >= 1024,
        "a 2048 x 1024 atlas"
    );
    // Opaque pixels are no longer near black: the picture is lifted.
    let (mut sum, mut count) = (0u64, 0u64);
    for pixel in leaf.pixels().filter(|p| p[3] > 200) {
        sum += pixel[1] as u64;
        count += 1;
    }
    assert!(
        count > 0 && sum / count > 70,
        "mean green {}",
        sum / count.max(1)
    );
    // The normal map's alpha is a height map, not a constant.
    let normal = std::fs::read_dir(&textures)
        .unwrap()
        .flatten()
        .find(|e| e.file_name().to_string_lossy().ends_with("_leaf_n.png"))
        .map(|e| image::open(e.path()).unwrap().to_rgba8())
        .expect("a leaf normal map");
    let (low, high) = normal
        .pixels()
        .fold((255u8, 0u8), |(lo, hi), p| (lo.min(p[3]), hi.max(p[3])));
    assert!(high > low + 80, "height range {low}..{high}");
    std::fs::remove_dir_all(root).ok();
}

#[test]
fn a_preset_the_project_baked_can_still_be_edited_through_its_library_materials() {
    use crate::uikit::panels::nature_generator as ng;
    let Some(mut harness) = engine_harness() else {
        return;
    };
    let project = test_temp_root("edit-after-preset");
    std::fs::create_dir_all(project.join("Scenes")).unwrap();
    std::fs::create_dir_all(project.join("Assets/Raw")).unwrap();
    std::fs::write(project.join("project.caffeine"), "{}\n").unwrap();
    let scene = project.join("Scenes/main.caf");
    {
        let engine = harness.app.engine.as_mut().unwrap();
        engine.save_scene(&scene.to_string_lossy()).unwrap();
        engine.load_scene(&scene.to_string_lossy()).unwrap();
        engine
            .set_asset_root(&project.join("Assets/Raw").to_string_lossy())
            .unwrap();
    }
    // Applying the preset in a project turns its library nodes into Your Material files...
    let oak = harness
        .app()
        .nature
        .presets
        .iter()
        .position(|p| p.name == "Oak")
        .unwrap();
    harness.dispatch(Message::NatureGen(ng::Edit::ApplyPreset(oak)));
    let nodes = &harness.app().nature.graph.nodes;
    assert!(!nodes.iter().any(|n| n.op == "mat_cards"), "baked at once");
    // ...and the leaf one is still a library asset, so it can be edited.
    let leaf = nodes
        .iter()
        .filter(|n| n.op == "mat_project")
        .find(|n| {
            n.strings
                .first()
                .is_some_and(|path| path.contains("atlas") || path.contains("leaf"))
        })
        .map(|n| n.id)
        .expect("the leaf material node");
    harness.dispatch(Message::NatureGen(ng::Edit::EditMaterial(leaf)));
    let edit = harness.app().nature.selected.expect("selected");
    assert_eq!(
        harness.app().nature.graph.node(edit).unwrap().op,
        "mat_edit",
        "{:?}",
        harness.app().nature.status
    );
    // Your own material, not from the library, is refused.
    let own = harness
        .app
        .nature
        .graph
        .add("mat_project", iced::Point::new(0.0, 0.0));
    harness
        .app
        .nature
        .graph
        .set_string(own, 0, "Materials/Mine.mat".to_owned());
    assert!(!harness.app.nature.graph.connect(own, 0, edit, 0));
    harness.dispatch(Message::NatureGen(ng::Edit::SetParam(edit, 2, 1.8)));
    harness.dispatch(Message::NatureGen(ng::Edit::Place));
    let status = harness.app().nature.status.clone().unwrap_or_default();
    assert!(status.starts_with("Placed Oak"), "{status}");
    let edited = std::fs::read_dir(project.join("Assets/Raw/Materials/Nature"))
        .unwrap()
        .flatten()
        .any(|e| e.file_name().to_string_lossy().contains("_e"));
    assert!(edited, "an edited copy was written");
    std::fs::remove_dir_all(project).ok();
}

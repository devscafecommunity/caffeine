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
use iced_test::simulator;
use image::{Rgba, RgbaImage};
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, MutexGuard, OnceLock};

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

    /// Presses, moves and releases the left mouse button over a widget.
    pub fn drag(&mut self, id: impl Into<Id>, dx: f32, dy: f32) {
        let start = {
            let mut ui = simulator(self.app.view());
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
            let mut ui = simulator(self.app.view());
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
        let mut ui = simulator(self.app.view());
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
        let mut ui = simulator(self.app.view());
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
        _guard: test_guard(),
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
        assert_eq!(pages, 8);
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
        id::PANE_CONSOLE,
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
        _guard: test_guard(),
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
///   (default `doppio/docs/ui-captions.json`). An agent can edit that file to
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
    assert!(imported.material_path.with_extension("mgraph").exists());
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
    assert_eq!(position.x % 16.0, 0.0);
    assert_eq!(position.y % 16.0, 0.0);
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
    assert!(active.root.join("assets/raw/Textures").is_dir());
    assert!(active.root.join("scenes").is_dir());
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
    let scene = project.root.join("scenes").join("main.caf");
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
    assert_eq!(reloaded.last_scene, "scenes/main.caf");

    // Saving a renamed scene updates last_scene in the manifest.
    let other = project.root.join("scenes").join("other.caf");
    harness
        .app
        .engine
        .as_mut()
        .expect("engine")
        .save_scene(other.to_string_lossy().as_ref())
        .expect("save other scene");
    harness.dispatch(Message::SaveScene);
    let reloaded = project::load_project(&project.root).expect("reload project");
    assert_eq!(reloaded.last_scene, "scenes/other.caf");
    assert_eq!(
        harness
            .app
            .project
            .active
            .as_ref()
            .map(|project| project.last_scene.as_str()),
        Some("scenes/other.caf")
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
    harness.click(id::asset_card(0));
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
        inspector_controls.insert(2, (id::ADD_COMPONENT, "Add Component"));
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
            Path::new(env!("CARGO_MANIFEST_DIR")).join("../../docs/ui-captions.json")
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
As descrições vivem em `doppio/docs/ui-captions.json`."
    } else {
        "Generated by `cargo test -p doppio-ui capture_ui_gallery -- --ignored`. \
Descriptions live in `doppio/docs/ui-captions.json`."
    };
    let mut markdown = format!("{title}\n\n{note}\n\n");
    for entry in entries {
        let id = entry["id"].as_str().unwrap_or_default();
        let image = entry["image"].as_str().unwrap_or_default();
        let description = entry["description"][lang]
            .as_str()
            .or_else(|| entry["description"]["en"].as_str())
            .unwrap_or_default();
        markdown.push_str(&format!(
            "## `{id}`\n\n![{id}]({image})\n\n{description}\n\n"
        ));
    }
    markdown
}

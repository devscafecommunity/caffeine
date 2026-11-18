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
        project.root.join("assets/raw").join(&asset_path).is_file(),
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
    let png = root.join("textures/maple_leaf.png");
    let graph = root.join("textures/maple_leaf.texgraph");
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
    let sheet = root.join("textures/maple_leaf_flipbook_2x2.png");
    let sheet = image::open(sheet).expect("sheet");
    assert_eq!(sheet.width(), 2 * 512);
    assert_eq!(sheet.height(), 2 * 512);

    // The saved graph reopens as a texture graph, never as a material.
    let loaded = crate::uikit::texture_graph::load(&graph).expect("load");
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
    let Some(mut engine) = doppio_engine::Engine::load_default() else {
        return;
    };
    let scene = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../../assets/benchmarks/dense_outdoor.caf");
    if engine.load_scene(&scene.to_string_lossy()).is_err() {
        return;
    }
    let _guard = test_guard();
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
    let Some(mut engine) = doppio_engine::Engine::load_default() else {
        return;
    };
    let _guard = test_guard();
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
    harness.dispatch(Message::NatureGen(ng::Edit::SetParam(grow, levels, 4.0)));
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
        root.join("nature/oak_trunk.obj").is_file(),
        "{:?}",
        harness.app().nature.status
    );
    assert!(root.join("nature/oak_leaves.obj").is_file());
    let recipe = std::fs::read_to_string(root.join("nature/oak.nature")).expect("recipe file");
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
            .all(|m| m.contains("nature/materials") || m.contains("materials/")),
        "{materials:?}"
    );
    assert_ne!(
        materials[0], materials[1],
        "trunk and leaves get different materials"
    );
    assert!(
        root.join("nature/materials").read_dir().unwrap().count() >= 3,
        "maps and .mat files were written"
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
    harness.app.asset_root_override = Some(root.clone());
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
    assert!(std::fs::read_dir(root.join("nature/materials"))
        .unwrap()
        .any(|e| e.unwrap().path().extension().is_some_and(|x| x == "png")));
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
            .join("nature/materials")
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
    let wrapped = root.join("nature/materials/project_my_grass.mat");
    assert!(
        wrapped.is_file(),
        "a cut-out material is made around your image"
    );
    let text = std::fs::read_to_string(wrapped).unwrap();
    assert!(
        text.contains("my_grass.png") && text.contains("alpha_mode"),
        "{text}"
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
    let materials = root.join("nature/materials");
    let names: Vec<String> = std::fs::read_dir(&materials)
        .unwrap()
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
    let file = root.join("water/calm_lake.mat");
    assert!(file.is_file());
    let graph = &harness.app().watergen.graph;
    assert!(graph
        .look_asset()
        .is_some_and(|p| p.ends_with("water/calm_lake.mat")));
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
    assert!(material.ends_with("water/open_sea.mat"), "{material}");
    let file = root.join("water/open_sea.mat");
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
    let file = root.join("water/open_sea.water");
    let text = std::fs::read_to_string(&file).expect("saved");
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
    assert!(root.join("terrain/alpine_range.terrain-graph").is_file());
    assert!(root.join("terrain/alpine_range_height.png").is_file());
    let text = std::fs::read_to_string(root.join("terrain/alpine_range.terrain-graph")).unwrap();
    let (name, graph) = crate::uikit::terrain::from_text(&text).expect("parses");
    assert_eq!(name, "Alpine Range");
    assert_eq!(
        graph.build(65).unwrap(),
        harness.app().terraingen.graph.build(65).unwrap()
    );
    std::fs::remove_dir_all(root).ok();
}

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
fn layout_presets_keep_every_page() {
    for preset in [LayoutPreset::Default, LayoutPreset::Viewport] {
        let state = pane_grid::State::with_configuration(App::layout_configuration(preset));
        assert_eq!(state.len(), 4);
        let pages: usize = state.iter().map(|(_, group)| group.pages.len()).sum();
        assert_eq!(pages, 5);
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
    assert!(harness.shows("Entity: Entity 1"));
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
    assert!(harness.shows("Entity: Entity 2"));

    harness.type_into(id::ENTITY_NAME, "Boss");
    assert!(harness.app().entities[1].name.contains("Boss"));

    harness.dispatch(Message::Inspector(panels::inspector::Edit::Delete));
    assert_eq!(harness.app().entities.len(), 1);
    assert!(harness.wait_for("Entity: Entity 1", 2));
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
        inspector_controls.insert(1, (id::component_field(0, 0, 0), "Position X"));
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

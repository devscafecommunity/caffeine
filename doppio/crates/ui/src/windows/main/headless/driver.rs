//! The headless driver: a persistent, human-like session over the real `App`.
//!
//! Unlike a one-shot simulator, the driver keeps the widget state tree
//! (focus, scroll offsets, canvas drags) and one GPU/CPU renderer alive across
//! steps, and it routes every event exactly like the running application:
//!
//! 1. the event goes through the widget tree (`UserInterface::update`);
//! 2. messages published by widgets are applied with `App::update`;
//! 3. events no widget captured also go through the same mapping the live
//!    subscriptions use (keyboard shortcuts, pointer release, file drop);
//! 4. the view is rebuilt from the new state before the next event.
//!
//! Pointer actions glide from the previous position in small steps and let the
//! UI settle between press and release, so hover, enter/exit and drag logic
//! sees what a person's hand would produce.

use super::super::{global_event_message, keyboard_event_message, App, Message};
use super::protocol::{Aim, Init, RunReport, Script, Step, StepResult, Target};
use super::{annotate, reference, state, PROTOCOL_VERSION};
use crate::uikit::id;
use iced_test::core::clipboard;
use iced_test::core::event;
use iced_test::core::keyboard::{self, key::Named, Key, Modifiers};
use iced_test::core::renderer::Headless;
use iced_test::core::theme::Base;
use iced_test::core::time::Instant;
use iced_test::core::widget::{self, Operation};
use iced_test::core::{mouse, window, Event, Font, Point, Rectangle, Size, SmolStr};
use iced_test::runtime::{user_interface, UserInterface};
use iced_test::selector::{Candidate, Target as Hit};
use image::RgbaImage;
use serde_json::{json, Value};
use std::path::{Path, PathBuf};
use std::time::Duration;

/// Gap kept between two separate clicks on the same spot so they never count
/// as an accidental double click (iced's window is 300 ms).
const CLICK_GAP: Duration = Duration::from_millis(330);
/// Duration of one simulated frame (the app's tick interval).
const FRAME: Duration = Duration::from_millis(16);

/// Settings of a driver session.
#[derive(Debug, Clone)]
pub struct Config {
    /// Where screenshots and other artifacts are written.
    pub out_dir: PathBuf,
    /// Baseline images for `screenshot.baseline`.
    pub baselines: Option<PathBuf>,
    /// Write/refresh baselines instead of failing on a mismatch.
    pub update_baselines: bool,
    /// Default window size (logical pixels).
    pub size: [f32; 2],
}

impl Default for Config {
    fn default() -> Self {
        Config {
            out_dir: PathBuf::from("doppio-headless-out"),
            baselines: None,
            update_baselines: false,
            size: [1600.0, 900.0],
        }
    }
}

/// A failed step: message plus an optional hint on how to recover.
pub struct Failure {
    pub error: String,
    pub hint: Option<String>,
}

impl From<String> for Failure {
    fn from(error: String) -> Self {
        Failure { error, hint: None }
    }
}

impl From<&str> for Failure {
    fn from(error: &str) -> Self {
        Failure {
            error: error.to_owned(),
            hint: None,
        }
    }
}

type Outcome = Result<Value, Failure>;

/// A widget located in the current frame.
struct Located {
    point: Point,
    bounds: Rectangle,
    visible: Option<Rectangle>,
    kind: &'static str,
    id: Option<String>,
    text: Option<String>,
}

#[derive(Default)]
struct Clipboard {
    content: Option<String>,
}

impl clipboard::Clipboard for Clipboard {
    fn read(&self, _kind: clipboard::Kind) -> Option<String> {
        self.content.clone()
    }

    fn write(&mut self, _kind: clipboard::Kind, contents: String) {
        self.content = Some(contents);
    }
}

pub struct Driver {
    app: App,
    renderer: iced_test::renderer::Renderer,
    cache: Option<user_interface::Cache>,
    clipboard: Clipboard,
    size: Size,
    cursor: mouse::Cursor,
    modifiers: Modifiers,
    last_click: Option<(std::time::Instant, Point)>,
    frame: u64,
    config: Config,
    shots: u32,
    artifacts: Vec<String>,
    temp_roots: Vec<PathBuf>,
    asset_root: Option<PathBuf>,
    recording: Option<super::record::Recording>,
}

fn select_all(candidate: Candidate<'_>) -> Option<Hit> {
    Some(Hit::from(candidate))
}

/// The readable key of a widget id (`Id(Custom("a.b"))` → `a.b`).
fn id_key(id: &widget::Id) -> Option<String> {
    let debug = format!("{id:?}");
    let start = debug.find("Custom(\"")? + "Custom(\"".len();
    let end = debug[start..].find("\")")?;
    Some(debug[start..start + end].to_owned())
}

fn hit_parts(
    hit: &Hit,
) -> (
    &'static str,
    Option<String>,
    Option<String>,
    Rectangle,
    Option<Rectangle>,
) {
    match hit {
        Hit::Container {
            id,
            bounds,
            visible_bounds,
        } => (
            "container",
            id.as_ref().and_then(id_key),
            None,
            *bounds,
            *visible_bounds,
        ),
        Hit::Focusable {
            id,
            bounds,
            visible_bounds,
        } => (
            "focusable",
            id.as_ref().and_then(id_key),
            None,
            *bounds,
            *visible_bounds,
        ),
        Hit::Scrollable {
            id,
            bounds,
            visible_bounds,
            ..
        } => (
            "scrollable",
            id.as_ref().and_then(id_key),
            None,
            *bounds,
            *visible_bounds,
        ),
        Hit::TextInput {
            id,
            bounds,
            visible_bounds,
            content,
        } => (
            "text_input",
            id.as_ref().and_then(id_key),
            Some(content.clone()),
            *bounds,
            *visible_bounds,
        ),
        Hit::Text {
            id,
            bounds,
            visible_bounds,
            content,
        } => (
            "text",
            id.as_ref().and_then(id_key),
            Some(content.clone()),
            *bounds,
            *visible_bounds,
        ),
        Hit::Custom {
            id,
            bounds,
            visible_bounds,
        } => (
            "custom",
            id.as_ref().and_then(id_key),
            None,
            *bounds,
            *visible_bounds,
        ),
    }
}

fn rect_json(rect: Rectangle) -> Value {
    json!([
        round2(rect.x),
        round2(rect.y),
        round2(rect.width),
        round2(rect.height)
    ])
}

fn round2(value: f32) -> f64 {
    (value as f64 * 100.0).round() / 100.0
}

fn truncate(text: String, limit: usize) -> String {
    if text.chars().count() <= limit {
        text
    } else {
        let mut cut: String = text.chars().take(limit).collect();
        cut.push('…');
        cut
    }
}

impl Driver {
    /// Starts a session with the given initial app.
    pub fn new(config: Config, init: &Init) -> Result<Driver, String> {
        let settings = crate::uikit::theme::settings();
        let font = if settings.default_font == Font::DEFAULT {
            Font::with_name("Fira Sans")
        } else {
            settings.default_font
        };
        let backend = std::env::var("ICED_TEST_BACKEND").ok();
        let renderer = iced_test::futures::futures::executor::block_on(
            <iced_test::renderer::Renderer as Headless>::new(
                font,
                settings.default_text_size,
                backend.as_deref(),
            ),
        )
        .ok_or("could not create a headless renderer (set ICED_TEST_BACKEND=tiny-skia to force the CPU one)")?;
        let size = init.size.unwrap_or(config.size);
        let mut driver = Driver {
            app: App::empty(),
            renderer,
            cache: Some(user_interface::Cache::default()),
            clipboard: Clipboard::default(),
            size: Size::new(size[0], size[1]),
            cursor: mouse::Cursor::Unavailable,
            modifiers: Modifiers::default(),
            last_click: None,
            frame: 0,
            config,
            shots: 0,
            artifacts: Vec::new(),
            temp_roots: Vec::new(),
            recording: None,
            asset_root: None,
        };
        driver.reset(init)?;
        Ok(driver)
    }

    /// Replaces the app with a fresh fixture.
    pub fn reset(&mut self, init: &Init) -> Result<Value, String> {
        let fixture = init.fixture.as_deref().unwrap_or("entities");
        let mut app = App::empty();
        match fixture {
            "empty" => {}
            "entities" => {
                app.update(Message::AddEntityNamed("Entity 1".to_owned()));
                app.update(Message::AddEntityNamed("Entity 2".to_owned()));
            }
            "material" => {
                app.new_material();
                app.show_generator_pane();
                if let Some(name) = &init.preset {
                    let index = crate::uikit::material_library::presets()
                        .iter()
                        .position(|preset| preset.name.eq_ignore_ascii_case(name))
                        .ok_or_else(|| format!("unknown preset `{name}`"))?;
                    app.update(Message::Generator(
                        crate::uikit::panels::material_generator::Edit::ApplyPreset(index),
                    ));
                }
            }
            "scene3d" => {
                // A small engine-backed scene so the gizmos and scene markers have something to
                // draw on: needs libcaffeine-capi (build it first).
                let engine = doppio_engine::Engine::load_default()
                    .ok_or_else(|| "fixture `scene3d` needs libcaffeine-capi".to_owned())?;
                app.engine = Some(engine);
                app.build_demo_scene();
            }
            "nature" => {
                // An engine-backed scene filled with generated rocks, trees and plants (written to a
                // scratch asset folder), with the Nature Generator tab in front.
                let engine = doppio_engine::Engine::load_default()
                    .ok_or_else(|| "fixture `nature` needs libcaffeine-capi".to_owned())?;
                app.engine = Some(engine);
                let dir =
                    std::env::temp_dir().join(format!("doppio-nature-{}", std::process::id()));
                std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
                app.asset_root_override = Some(dir);
                app.build_nature_demo();
                if let Some(name) = &init.preset {
                    let index = crate::uikit::nature::presets()
                        .iter()
                        .position(|preset| preset.name.eq_ignore_ascii_case(name))
                        .ok_or_else(|| format!("unknown nature preset `{name}`"))?;
                    app.update(Message::NatureGen(
                        crate::uikit::panels::nature_generator::Edit::ApplyPreset(index),
                    ));
                }
                app.show_nature_pane();
            }
            "terrain" => {
                // The Terrain Generator tab in front (no engine needed for the graph and preview).
                if let Some(name) = &init.preset {
                    let index = crate::uikit::terrain::presets()
                        .iter()
                        .position(|preset| preset.name.eq_ignore_ascii_case(name))
                        .ok_or_else(|| format!("unknown terrain preset `{name}`"))?;
                    app.update(Message::TerrainGen(
                        crate::uikit::panels::terrain_generator::Edit::ApplyPreset(index),
                    ));
                }
                app.show_terrain_pane();
            }
            "water" => {
                // The Water Generator tab in front (no engine needed for the graph and preview).
                if let Some(name) = &init.preset {
                    let index = crate::uikit::water::presets()
                        .iter()
                        .position(|preset| preset.name.eq_ignore_ascii_case(name))
                        .ok_or_else(|| format!("unknown water preset `{name}`"))?;
                    app.update(Message::WaterGen(
                        crate::uikit::panels::water_generator::Edit::ApplyPreset(index),
                    ));
                }
                app.show_water_pane();
            }
            "world" => {
                // The World Composer tab with a small project: a hills terrain, two rock recipes and the
                // composition World/Valley.generator.world (written to a scratch asset folder). Needs libcaffeine-capi.
                let engine = doppio_engine::Engine::load_default()
                    .ok_or_else(|| "fixture `world` needs libcaffeine-capi".to_owned())?;
                app.engine = Some(engine);
                let dir = std::env::temp_dir().join(format!(
                    "doppio-world-{}-{}",
                    std::process::id(),
                    self.temp_roots.len()
                ));
                let _ = std::fs::remove_dir_all(&dir);
                std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
                let path = super::super::worldgen::write_world_fixture(&dir)?;
                app.asset_root_override = Some(dir.clone());
                self.temp_roots.push(dir);
                app.open_recipe_file(&path);
                app.show_world_pane();
            }
            "texture" => {
                app.show_texgen_pane();
                if let Some(name) = &init.preset {
                    let index = crate::uikit::texture_graph::presets()
                        .iter()
                        .position(|preset| preset.name.eq_ignore_ascii_case(name))
                        .ok_or_else(|| format!("unknown texture preset `{name}`"))?;
                    app.update(Message::TextureGen(
                        crate::uikit::panels::texture_generator::Edit::ApplyPreset(index),
                    ));
                }
            }
            other => {
                return Err(format!(
                "unknown fixture `{other}` (empty, entities, material, texture, scene3d, nature, water, terrain, world)"
            ))
            }
        }
        app.project.open = false;
        if let Some(assets) = &init.assets {
            app.assets = assets
                .iter()
                .map(|spec| doppio_engine::AssetInfo {
                    name: spec.name.clone(),
                    relative_path: spec.path.clone(),
                    kind: spec.kind.clone().unwrap_or_else(|| {
                        if spec.directory {
                            "folder".to_owned()
                        } else {
                            kind_from_extension(&spec.path)
                        }
                    }),
                    size_bytes: 0,
                    is_directory: spec.directory,
                })
                .collect();
        }
        self.asset_root = match init.asset_root.as_deref() {
            None => None,
            Some("temp") => {
                let dir = std::env::temp_dir().join(format!(
                    "doppio-headless-{}-{}",
                    std::process::id(),
                    std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .map(|d| d.as_nanos())
                        .unwrap_or_default()
                ));
                std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
                self.temp_roots.push(dir.clone());
                Some(dir)
            }
            Some(path) => {
                let dir = PathBuf::from(path);
                std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
                Some(dir)
            }
        };
        if let Some(components) = &init.components {
            app.component_types = components
                .iter()
                .enumerate()
                .map(|(index, spec)| doppio_engine::ComponentType {
                    id: index as u32 + 1,
                    name: spec.name.clone(),
                    fields: spec
                        .fields
                        .iter()
                        .map(|name| doppio_engine::FieldInfo {
                            name: name.clone(),
                            kind: doppio_engine::FieldKind::F32,
                            min: 0.0,
                            max: 1.0,
                            step: 0.1,
                            options: Vec::new(),
                        })
                        .collect(),
                })
                .collect();
        }
        // The `world` fixture is a project of its own: its folder is the asset root unless the script names one.
        if self.asset_root.is_none() && fixture == "world" {
            self.asset_root = app.asset_root_override.clone();
        }
        app.asset_root_override = self.asset_root.clone();
        if let Some(size) = init.size {
            self.size = Size::new(size[0], size[1]);
        }
        self.app = app;
        self.cache = Some(user_interface::Cache::default());
        self.cursor = mouse::Cursor::Unavailable;
        self.modifiers = Modifiers::default();
        self.last_click = None;
        self.settle();
        Ok(json!({ "asset_root": self.asset_root.as_ref().map(|p| p.display().to_string()) }))
    }

    pub fn out_dir(&self) -> &Path {
        &self.config.out_dir
    }

    pub fn artifacts(&self) -> &[String] {
        &self.artifacts
    }

    // ---------------------------------------------------------------- events

    /// Delivers `events` through the widget tree, then through the app's own
    /// global mappings, applying every resulting message. Returns a short
    /// description of each message (for debugging a step).
    fn deliver(&mut self, events: Vec<Event>) -> Vec<String> {
        let mut messages: Vec<Message> = Vec::new();
        let statuses = {
            let cache = self.cache.take().unwrap_or_default();
            let mut ui =
                UserInterface::build(self.app.view(), self.size, cache, &mut self.renderer);
            let (_state, statuses) = ui.update(
                &events,
                self.cursor,
                &mut self.renderer,
                &mut self.clipboard,
                &mut messages,
            );
            self.cache = Some(ui.into_cache());
            statuses
        };
        // Events that no widget captured reach the application's subscriptions.
        for (event, status) in events.iter().zip(statuses.iter()) {
            if *status != event::Status::Ignored {
                continue;
            }
            if let Some(message) = global_event_message(event) {
                messages.push(message);
            }
            if let Event::Keyboard(keyboard_event) = event {
                if let Some(message) = keyboard_event_message(keyboard_event.clone()) {
                    messages.push(message);
                }
            }
        }
        let mut log = Vec::new();
        for message in messages {
            log.push(truncate(format!("{message:?}"), 140));
            self.app.update(message);
        }
        log
    }

    /// One idle frame: a redraw plus the app's 16 ms tick.
    fn settle(&mut self) {
        let _ = self.deliver(vec![Event::Window(window::Event::RedrawRequested(
            Instant::now(),
        ))]);
        self.app.update(Message::ViewportTick);
        self.frame += 1;
    }

    fn set_modifiers(&mut self, names: &[String]) {
        let mut modifiers = Modifiers::empty();
        for name in names {
            modifiers |= match name.as_str() {
                "ctrl" => Modifiers::CTRL,
                "shift" => Modifiers::SHIFT,
                "alt" => Modifiers::ALT,
                _ => Modifiers::LOGO,
            };
        }
        if modifiers != self.modifiers {
            self.modifiers = modifiers;
            let _ = self.deliver(vec![Event::Keyboard(keyboard::Event::ModifiersChanged(
                modifiers,
            ))]);
        }
    }

    /// Moves the pointer to `to` in `steps` small increments.
    fn glide_to(&mut self, to: Point, steps: usize) {
        let start = match self.cursor.position() {
            Some(position) => position,
            None => {
                // First appearance: enter the window slightly away from the target.
                let start = Point::new((to.x - 30.0).max(0.0), (to.y - 20.0).max(0.0));
                self.cursor = mouse::Cursor::Available(start);
                let _ = self.deliver(vec![
                    Event::Mouse(mouse::Event::CursorEntered),
                    Event::Mouse(mouse::Event::CursorMoved { position: start }),
                ]);
                start
            }
        };
        let steps = steps.max(1);
        for step in 1..=steps {
            let t = step as f32 / steps as f32;
            let position = Point::new(
                start.x + (to.x - start.x) * t,
                start.y + (to.y - start.y) * t,
            );
            self.cursor = mouse::Cursor::Available(position);
            let _ = self.deliver(vec![Event::Mouse(mouse::Event::CursorMoved { position })]);
        }
    }

    // --------------------------------------------------------------- queries

    fn hits(&mut self) -> Vec<Hit> {
        let cache = self.cache.take().unwrap_or_default();
        let mut ui = UserInterface::build(self.app.view(), self.size, cache, &mut self.renderer);
        let mut operation =
            iced_test::selector::Selector::find_all(select_all as fn(Candidate<'_>) -> Option<Hit>);
        ui.operate(
            &self.renderer,
            &mut widget::operation::black_box(&mut operation),
        );
        let hits = match operation.finish() {
            widget::operation::Outcome::Some(hits) => hits,
            _ => Vec::new(),
        };
        self.cache = Some(ui.into_cache());
        hits
    }

    fn locate(&mut self, aim: &Aim) -> Result<Located, Failure> {
        if let Target::Point(x, y) = aim.target {
            let point = Point::new(x + aim.offset[0], y + aim.offset[1]);
            return Ok(Located {
                point,
                bounds: Rectangle::new(point, Size::ZERO),
                visible: Some(Rectangle::new(point, Size::ZERO)),
                kind: "point",
                id: None,
                text: None,
            });
        }
        let hits = self.hits();
        let matches: Vec<&Hit> = match &aim.target {
            Target::Id(wanted) => hits
                .iter()
                .filter(|hit| hit_parts(hit).1.as_deref() == Some(wanted.as_str()))
                .collect(),
            Target::Text(wanted) => {
                let exact: Vec<&Hit> = hits
                    .iter()
                    .filter(|hit| hit_parts(hit).2.as_deref() == Some(wanted.as_str()))
                    .collect();
                if exact.is_empty() {
                    hits.iter()
                        .filter(|hit| {
                            hit_parts(hit)
                                .2
                                .is_some_and(|text| text.contains(wanted.as_str()))
                        })
                        .collect()
                } else {
                    exact
                }
            }
            Target::Point(..) => Vec::new(),
        };
        // Prefer widgets a person could actually reach.
        let reachable: Vec<&&Hit> = matches
            .iter()
            .filter(|hit| hit_parts(hit).4.is_some())
            .collect();
        let chosen = reachable
            .get(aim.nth)
            .map(|hit| **hit)
            .or_else(|| matches.get(aim.nth).copied());
        let Some(hit) = chosen else {
            return Err(Failure {
                error: format!("no widget matches {}", aim.target.describe()),
                hint: Some(self.suggest(&aim.target, &hits)),
            });
        };
        let (kind, id, text, bounds, visible) = hit_parts(hit);
        let basis = visible.unwrap_or(bounds);
        let center = basis.center();
        Ok(Located {
            point: Point::new(center.x + aim.offset[0], center.y + aim.offset[1]),
            bounds,
            visible,
            kind,
            id,
            text,
        })
    }

    fn suggest(&self, target: &Target, hits: &[Hit]) -> String {
        let mut candidates: Vec<String> = Vec::new();
        match target {
            Target::Id(wanted) => {
                let tail = wanted.rsplit('.').next().unwrap_or(wanted);
                for hit in hits {
                    if let Some(key) = hit_parts(hit).1 {
                        if key.contains(tail)
                            || key.starts_with(wanted.split('.').next().unwrap_or(""))
                        {
                            candidates.push(format!("id:{key}"));
                        }
                    }
                }
            }
            Target::Text(wanted) => {
                let needle = wanted.to_lowercase();
                for hit in hits {
                    if let Some(text) = hit_parts(hit).2 {
                        if text.to_lowercase().contains(&needle) {
                            candidates.push(format!("text:{text}"));
                        }
                    }
                }
            }
            Target::Point(..) => {}
        }
        candidates.dedup();
        candidates.truncate(8);
        if candidates.is_empty() {
            "nothing similar on screen; run the `widgets` action to list what is visible (is the right pane/tab/dialog open?)".to_owned()
        } else {
            format!(
                "did you mean: {}? (see also the `widgets` action)",
                candidates.join(", ")
            )
        }
    }

    fn reachable(located: &Located) -> Result<Point, Failure> {
        if located.visible.is_none() {
            return Err(Failure {
                error: "the widget exists but is clipped out of view".to_owned(),
                hint: Some(
                    "scroll its container, maximise the pane or enlarge the window (`resize`)"
                        .to_owned(),
                ),
            });
        }
        Ok(located.point)
    }

    // --------------------------------------------------------------- actions

    fn button(name: Option<&str>) -> Result<mouse::Button, Failure> {
        match name.unwrap_or("left") {
            "left" => Ok(mouse::Button::Left),
            "right" => Ok(mouse::Button::Right),
            "middle" => Ok(mouse::Button::Middle),
            other => Err(format!("unknown button `{other}` (left, right, middle)").into()),
        }
    }

    fn do_click(&mut self, step: &Step, count: usize, button: mouse::Button) -> Outcome {
        let aim = step
            .aim("target")?
            .ok_or_else(|| Failure::from("`click` needs a target"))?;
        let located = self.locate(&aim)?;
        let point = Self::reachable(&located)?;
        let modifiers = step.modifiers()?;
        self.set_modifiers(&modifiers);
        self.glide_to(point, step.usize("steps", 6));
        // A person's hand rests a frame on the target before pressing.
        self.settle();
        if count == 1 {
            self.respect_click_gap(point);
        }
        let mut messages = Vec::new();
        for _ in 0..count {
            messages.extend(self.deliver(vec![Event::Mouse(mouse::Event::ButtonPressed(button))]));
            messages.extend(self.deliver(vec![Event::Mouse(mouse::Event::ButtonReleased(button))]));
        }
        self.last_click = Some((std::time::Instant::now(), point));
        self.set_modifiers(&[]);
        self.settle();
        Ok(json!({ "point": [round2(point.x), round2(point.y)], "messages": messages }))
    }

    fn respect_click_gap(&mut self, point: Point) {
        if let Some((when, previous)) = self.last_click {
            let close = (previous.x - point.x).abs() < 6.0 && (previous.y - point.y).abs() < 6.0;
            if close {
                let elapsed = when.elapsed();
                if elapsed < CLICK_GAP {
                    std::thread::sleep(CLICK_GAP - elapsed);
                }
            }
        }
    }

    fn do_drag(&mut self, step: &Step) -> Outcome {
        let from_aim = step
            .aim("from")?
            .ok_or_else(|| Failure::from("`drag` needs `from`"))?;
        let from = self.locate(&from_aim)?;
        let from_point = Self::reachable(&from)?;
        let to_point = if let Some(to_aim) = step.aim("to")? {
            let to = self.locate(&to_aim)?;
            Self::reachable(&to)?
        } else if let Some([dx, dy]) = step.pair("by")? {
            Point::new(from_point.x + dx, from_point.y + dy)
        } else {
            return Err("`drag` needs `to` or `by`".into());
        };
        let button = Self::button(step.str("button"))?;
        let modifiers = step.modifiers()?;
        let steps = step.usize("steps", 8).max(2);
        self.set_modifiers(&modifiers);
        self.glide_to(from_point, 6);
        self.settle();
        let mut messages = self.deliver(vec![Event::Mouse(mouse::Event::ButtonPressed(button))]);
        self.settle();
        for index in 1..=steps {
            let t = index as f32 / steps as f32;
            let position = Point::new(
                from_point.x + (to_point.x - from_point.x) * t,
                from_point.y + (to_point.y - from_point.y) * t,
            );
            self.cursor = mouse::Cursor::Available(position);
            messages
                .extend(self.deliver(vec![Event::Mouse(mouse::Event::CursorMoved { position })]));
            // The UI repaints between pointer samples, like a real frame loop.
            self.settle();
        }
        if !step.bool("hold", false) {
            messages.extend(self.deliver(vec![Event::Mouse(mouse::Event::ButtonReleased(button))]));
            self.settle();
        }
        self.set_modifiers(&[]);
        Ok(json!({
            "from": [round2(from_point.x), round2(from_point.y)],
            "to": [round2(to_point.x), round2(to_point.y)],
            "messages": messages,
        }))
    }

    fn do_move(&mut self, step: &Step) -> Outcome {
        let aim = step
            .aim("target")?
            .ok_or_else(|| Failure::from("`move` needs a target"))?;
        let located = self.locate(&aim)?;
        let point = Self::reachable(&located)?;
        self.glide_to(point, step.usize("steps", 6));
        self.settle();
        Ok(json!({ "point": [round2(point.x), round2(point.y)] }))
    }

    fn do_scroll(&mut self, step: &Step) -> Outcome {
        if let Some(aim) = step.aim("target")? {
            let located = self.locate(&aim)?;
            let point = Self::reachable(&located)?;
            self.glide_to(point, 6);
            self.settle();
        } else if self.cursor.position().is_none() {
            return Err("`scroll` without a target needs the pointer to be somewhere first".into());
        }
        let delta = mouse::ScrollDelta::Lines {
            x: step.f32("dx").unwrap_or(0.0),
            y: step.f32("dy").unwrap_or(0.0),
        };
        let messages = self.deliver(vec![Event::Mouse(mouse::Event::WheelScrolled { delta })]);
        self.settle();
        let point = self.cursor.position().unwrap_or(Point::ORIGIN);
        Ok(json!({ "point": [round2(point.x), round2(point.y)], "messages": messages }))
    }

    fn do_mouse_button(&mut self, step: &Step, down: bool) -> Outcome {
        let button = Self::button(step.str("button"))?;
        let event = if down {
            mouse::Event::ButtonPressed(button)
        } else {
            mouse::Event::ButtonReleased(button)
        };
        let messages = self.deliver(vec![Event::Mouse(event)]);
        self.settle();
        Ok(json!({ "messages": messages }))
    }

    /// Parses `ctrl+shift+z` style chords.
    fn parse_chord(chord: &str) -> Result<(Key, Option<SmolStr>, Vec<String>), Failure> {
        let mut parts: Vec<&str> = chord.split('+').collect();
        // A literal "+" key is written "ctrl++" → trailing empty part.
        let key_name = if chord.ends_with('+') && parts.len() > 1 {
            parts.pop();
            parts.pop();
            "+"
        } else {
            parts.pop().unwrap_or("")
        };
        let mut modifiers = Vec::new();
        for part in parts {
            match part.to_ascii_lowercase().as_str() {
                "ctrl" | "control" => modifiers.push("ctrl".to_owned()),
                "shift" => modifiers.push("shift".to_owned()),
                "alt" | "option" => modifiers.push("alt".to_owned()),
                "cmd" | "meta" | "super" | "logo" => modifiers.push("cmd".to_owned()),
                other => return Err(format!("unknown modifier `{other}` in `{chord}`").into()),
            }
        }
        let lower = key_name.to_ascii_lowercase();
        let named = |name: Named| Ok((Key::Named(name), None, modifiers.clone()));
        match lower.as_str() {
            "enter" | "return" => named(Named::Enter),
            "escape" | "esc" => named(Named::Escape),
            "tab" => named(Named::Tab),
            "backspace" => named(Named::Backspace),
            "delete" | "del" => named(Named::Delete),
            "up" | "arrowup" => named(Named::ArrowUp),
            "down" | "arrowdown" => named(Named::ArrowDown),
            "left" | "arrowleft" => named(Named::ArrowLeft),
            "right" | "arrowright" => named(Named::ArrowRight),
            "home" => named(Named::Home),
            "end" => named(Named::End),
            "pageup" => named(Named::PageUp),
            "pagedown" => named(Named::PageDown),
            "shift" => named(Named::Shift),
            "f1" => named(Named::F1),
            "f2" => named(Named::F2),
            "f3" => named(Named::F3),
            "f4" => named(Named::F4),
            "f5" => named(Named::F5),
            "f6" => named(Named::F6),
            "f7" => named(Named::F7),
            "f8" => named(Named::F8),
            "f9" => named(Named::F9),
            "f10" => named(Named::F10),
            "f11" => named(Named::F11),
            "f12" => named(Named::F12),
            "space" => Ok((
                Key::Named(Named::Space),
                Some(SmolStr::new_inline(" ")),
                modifiers,
            )),
            _ if key_name.chars().count() == 1 => {
                let text = if modifiers
                    .iter()
                    .any(|m| m == "ctrl" || m == "alt" || m == "cmd")
                {
                    None
                } else {
                    Some(SmolStr::new(key_name))
                };
                Ok((Key::Character(SmolStr::new(&lower)), text, modifiers))
            }
            _ => Err(format!("unknown key `{key_name}`").into()),
        }
    }

    fn key_events(key: &Key, text: &Option<SmolStr>, modifiers: Modifiers, press: bool) -> Event {
        let modified = match (key, modifiers.shift()) {
            (Key::Character(c), true) => Key::Character(SmolStr::new(c.to_uppercase())),
            _ => key.clone(),
        };
        if press {
            Event::Keyboard(keyboard::Event::KeyPressed {
                key: key.clone(),
                modified_key: modified,
                physical_key: keyboard::key::Physical::Unidentified(
                    keyboard::key::NativeCode::Unidentified,
                ),
                location: keyboard::Location::Standard,
                modifiers,
                text: text.clone(),
                repeat: false,
            })
        } else {
            Event::Keyboard(keyboard::Event::KeyReleased {
                key: key.clone(),
                modified_key: modified,
                physical_key: keyboard::key::Physical::Unidentified(
                    keyboard::key::NativeCode::Unidentified,
                ),
                location: keyboard::Location::Standard,
                modifiers,
            })
        }
    }

    fn do_key(&mut self, chord: &str, press: bool, release: bool) -> Outcome {
        let (key, text, modifier_names) = Self::parse_chord(chord)?;
        self.set_modifiers(&modifier_names);
        let modifiers = self.modifiers;
        let mut messages = Vec::new();
        if press {
            messages.extend(self.deliver(vec![Self::key_events(&key, &text, modifiers, true)]));
        }
        if release {
            messages.extend(self.deliver(vec![Self::key_events(&key, &text, modifiers, false)]));
            self.set_modifiers(&[]);
        }
        self.settle();
        Ok(json!({ "messages": messages }))
    }

    fn do_type(&mut self, step: &Step) -> Outcome {
        let text = step.str("text").unwrap_or_default().to_owned();
        if let Some(aim) = step.aim("target")? {
            let located = self.locate(&aim)?;
            let point = Self::reachable(&located)?;
            self.glide_to(point, 6);
            self.settle();
            self.respect_click_gap(point);
            self.deliver(vec![Event::Mouse(mouse::Event::ButtonPressed(
                mouse::Button::Left,
            ))]);
            self.deliver(vec![Event::Mouse(mouse::Event::ButtonReleased(
                mouse::Button::Left,
            ))]);
            self.last_click = Some((std::time::Instant::now(), point));
            self.settle();
        }
        if step.bool("clear", false) {
            self.do_key("ctrl+a", true, true)?;
            self.do_key("backspace", true, true)?;
        }
        let mut messages = Vec::new();
        for character in text.chars() {
            let key = Key::Character(SmolStr::new(character.to_string()));
            let typed = Some(SmolStr::new(character.to_string()));
            let mods = if character.is_uppercase() {
                Modifiers::SHIFT
            } else {
                Modifiers::empty()
            };
            messages.extend(self.deliver(vec![Self::key_events(&key, &typed, mods, true)]));
            messages.extend(self.deliver(vec![Self::key_events(&key, &typed, mods, false)]));
        }
        self.settle();
        Ok(json!({ "typed": text, "messages": messages }))
    }

    fn do_wait(&mut self, step: &Step) -> Outcome {
        let frames = match step.get("ms").and_then(Value::as_u64) {
            Some(ms) => ((ms as f32 / 16.0).ceil() as usize).max(1),
            None => step.usize("frames", 1).max(1),
        };
        let real = step.bool("real_time", false);
        for _ in 0..frames {
            self.settle();
            self.record_tick();
            if real {
                std::thread::sleep(FRAME);
            }
        }
        Ok(json!({ "frames": frames }))
    }

    // ---------------------------------------------------------------- queries

    fn describe_hit(hit: &Hit) -> Value {
        let (kind, id, text, bounds, visible) = hit_parts(hit);
        json!({
            "id": id,
            "kind": kind,
            "bounds": rect_json(bounds),
            "visible_bounds": visible.map(rect_json),
            "text": text,
        })
    }

    fn do_find(&mut self, step: &Step) -> Outcome {
        let aim = step
            .aim("target")?
            .ok_or_else(|| Failure::from("`find` needs a target"))?;
        match self.locate(&aim) {
            Ok(located) => Ok(json!({
                "found": true,
                "id": located.id,
                "kind": located.kind,
                "bounds": rect_json(located.bounds),
                "visible_bounds": located.visible.map(rect_json),
                "center": [round2(located.point.x), round2(located.point.y)],
                "text": located.text,
            })),
            Err(failure) => {
                Ok(json!({ "found": false, "reason": failure.error, "hint": failure.hint }))
            }
        }
    }

    fn do_widgets(&mut self, step: &Step) -> Outcome {
        let prefix = step.str("prefix").map(str::to_owned);
        let kind = step.str("kind").map(str::to_owned);
        let visible_only = step.bool("visible_only", true);
        let with_text = step.bool("with_text", false);
        let hits = self.hits();
        let widgets: Vec<Value> = hits
            .iter()
            .filter(|hit| {
                let (k, id, _, _, visible) = hit_parts(hit);
                if visible_only && visible.is_none() {
                    return false;
                }
                if let Some(want) = &kind {
                    if k != want {
                        return false;
                    }
                }
                match (&prefix, &id) {
                    (Some(prefix), Some(id)) => id.starts_with(prefix.as_str()),
                    (Some(_), None) => false,
                    (None, Some(_)) => true,
                    (None, None) => with_text && k.starts_with("text"),
                }
            })
            .map(Self::describe_hit)
            .collect();
        Ok(json!({ "count": widgets.len(), "widgets": widgets }))
    }

    fn do_text(&mut self, step: &Step) -> Outcome {
        let aim = step
            .aim("target")?
            .ok_or_else(|| Failure::from("`text` needs a target"))?;
        let located = self.locate(&aim)?;
        // A container (button, card) has no text of its own: gather the text
        // widgets inside it.
        let text = match located.text {
            Some(text) => text,
            None => {
                let area = located.bounds;
                let inner: Vec<String> = self
                    .hits()
                    .iter()
                    .filter_map(|hit| {
                        let (_, _, text, bounds, _) = hit_parts(hit);
                        let text = text?;
                        let center = bounds.center();
                        area.contains(center).then_some(text)
                    })
                    .collect();
                inner.join(" ")
            }
        };
        Ok(json!({ "text": text }))
    }

    fn do_log(&mut self, step: &Step) -> Outcome {
        let last = step.usize("last", 10);
        let entries: Vec<Value> = self
            .app
            .log
            .iter()
            .rev()
            .take(last)
            .rev()
            .map(|entry| json!({ "level": format!("{:?}", entry.level), "text": entry.text }))
            .collect();
        Ok(json!({ "entries": entries }))
    }

    fn state(&self) -> Value {
        let mut value = state::describe(&self.app);
        if let Some(object) = value.as_object_mut() {
            object.insert("frame".to_owned(), json!(self.frame));
            object.insert(
                "window".to_owned(),
                json!({ "size": [self.size.width, self.size.height] }),
            );
            object.insert(
                "pointer".to_owned(),
                json!(self.cursor.position().map(|p| [round2(p.x), round2(p.y)])),
            );
        }
        value
    }

    fn do_state(&mut self, step: &Step) -> Outcome {
        let state = self.state();
        match step.str("path") {
            None | Some("") => Ok(state),
            Some(pointer) => state.pointer(pointer).cloned().ok_or_else(|| Failure {
                error: format!("no value at `{pointer}` in the state"),
                hint: Some("run `state` without a path to see the available keys".to_owned()),
            }),
        }
    }

    // --------------------------------------------------------------- capture

    // ---------------------------------------------------------------- recording

    fn do_record_start(&mut self, step: &Step) -> Outcome {
        if self.recording.is_some() {
            return Err("a recording is already running: use `record_stop` first".into());
        }
        let format = step.str("format").unwrap_or("gif").to_lowercase();
        if !super::record::FORMATS.contains(&format.as_str()) {
            return Err(format!("`format` must be one of gif, mp4, webm (got `{format}`)").into());
        }
        let fps = step.usize("fps", 15).clamp(1, 60) as u32;
        let dir = std::env::temp_dir().join(format!(
            "doppio-record-{}-{}",
            std::process::id(),
            self.shots
        ));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
        self.recording = Some(super::record::Recording {
            dir,
            name: sanitize_file_stem(step.str("name").unwrap_or("recording")),
            format,
            fps,
            scale: step.f32("scale").unwrap_or(1.0).clamp(0.25, 3.0),
            width: step.usize("width", 0) as u32,
            frames: 0,
            every: super::record::frames_per_capture(fps),
            tick: 0,
            crop: step.aim("target")?,
            padding: step.f32("padding").unwrap_or(8.0),
            keep_frames: step.bool("keep_frames", false),
        });
        // The first frame is the starting state.
        self.record_frame();
        Ok(json!({ "recording": true, "fps": fps }))
    }

    fn do_record_stop(&mut self, _step: &Step) -> Outcome {
        self.finish_recording()
    }

    /// Captures a frame on every Nth settled frame (called from `wait`).
    fn record_tick(&mut self) {
        let due = match self.recording.as_mut() {
            Some(rec) => {
                rec.tick += 1;
                rec.tick % rec.every == 0
            }
            None => false,
        };
        if due {
            self.record_frame();
        }
    }

    /// Writes one frame of the current window (or the recording's target) to the scratch folder.
    fn record_frame(&mut self) {
        let Some((scale, crop, padding, path)) = self
            .recording
            .as_ref()
            .map(|r| (r.scale, r.crop.clone(), r.padding, r.frame_path(r.frames)))
        else {
            return;
        };
        let Ok(full) = self.render(scale) else {
            return;
        };
        let image = match crop {
            Some(aim) => match self.locate(&aim) {
                Ok(located) => {
                    let area = located.visible.unwrap_or(located.bounds);
                    let x = ((area.x - padding).max(0.0) * scale).round() as u32;
                    let y = ((area.y - padding).max(0.0) * scale).round() as u32;
                    let w = (((area.width + 2.0 * padding) * scale).round() as u32)
                        .min(full.width().saturating_sub(x));
                    let h = (((area.height + 2.0 * padding) * scale).round() as u32)
                        .min(full.height().saturating_sub(y));
                    image::imageops::crop_imm(&full, x, y, w.max(2), h.max(2)).to_image()
                }
                Err(_) => full,
            },
            None => full,
        };
        // Every frame of a video must have the size of the first one.
        let size = self.recording.as_ref().and_then(|r| {
            (r.frames > 0)
                .then(|| image::image_dimensions(r.frame_path(0)).ok())
                .flatten()
        });
        let image = match size {
            Some((w, h)) if (w, h) != (image.width(), image.height()) => {
                image::imageops::resize(&image, w, h, image::imageops::FilterType::Triangle)
            }
            _ => image,
        };
        if image.save(&path).is_ok() {
            if let Some(rec) = self.recording.as_mut() {
                rec.frames += 1;
            }
        }
    }

    /// Encodes the captured frames with ffmpeg and returns the artifact description.
    fn finish_recording(&mut self) -> Outcome {
        let Some(rec) = self.recording.take() else {
            return Err("no recording is running: use `record_start` first".into());
        };
        if rec.frames == 0 {
            return Err("the recording captured no frames".into());
        }
        let output = super::record::encode(&rec, &self.config.out_dir)?;
        let frames = rec.frames;
        if !rec.keep_frames {
            let _ = std::fs::remove_dir_all(&rec.dir);
        }
        let path_text = output.display().to_string();
        self.artifacts.push(path_text.clone());
        let bytes = std::fs::metadata(&output).map(|m| m.len()).unwrap_or(0);
        Ok(json!({
            "path": path_text,
            "format": rec.format,
            "frames": frames,
            "fps": rec.fps,
            "seconds": frames as f32 / rec.fps as f32,
            "bytes": bytes,
            "frames_dir": rec.keep_frames.then(|| rec.dir.display().to_string()),
        }))
    }

    fn do_screenshot(&mut self, step: &Step) -> Outcome {
        let name = sanitize_file_stem(step.str("name").unwrap_or("shot"));
        let scale = step.f32("scale").unwrap_or(1.0).clamp(0.5, 4.0);
        let padding = step.f32("padding").unwrap_or(8.0);
        let crop = match step.aim("target")? {
            Some(aim) => {
                let located = self.locate(&aim)?;
                let area = located.visible.unwrap_or(located.bounds);
                Some(Rectangle {
                    x: (area.x - padding).max(0.0),
                    y: (area.y - padding).max(0.0),
                    width: (area.width + 2.0 * padding).min(self.size.width),
                    height: (area.height + 2.0 * padding).min(self.size.height),
                })
            }
            None => None,
        };
        let full = self.render(scale)?;

        // Numbered boxes (annotate) are computed in logical coordinates.
        let marks_logical = self.annotation_marks(step)?;
        let (mut image, origin) = match crop {
            Some(area) => {
                let x = (area.x * scale).round() as u32;
                let y = (area.y * scale).round() as u32;
                let w = ((area.width * scale).round() as u32).min(full.width().saturating_sub(x));
                let h = ((area.height * scale).round() as u32).min(full.height().saturating_sub(y));
                (
                    image::imageops::crop_imm(&full, x, y, w.max(1), h.max(1)).to_image(),
                    (area.x, area.y),
                )
            }
            None => (full, (0.0, 0.0)),
        };
        if !marks_logical.is_empty() {
            let marks: Vec<annotate::Mark> = marks_logical
                .into_iter()
                .filter_map(|(rect, label)| {
                    let x = (rect.x - origin.0) * scale;
                    let y = (rect.y - origin.1) * scale;
                    let inside = x + rect.width * scale > 0.0
                        && y + rect.height * scale > 0.0
                        && x < image.width() as f32
                        && y < image.height() as f32;
                    inside.then_some(annotate::Mark {
                        rect: [x, y, rect.width * scale, rect.height * scale],
                        label,
                    })
                })
                .collect();
            image = annotate::annotate(image, &marks, step.str("name"));
        }

        self.shots += 1;
        std::fs::create_dir_all(&self.config.out_dir)
            .map_err(|e| format!("{}: {e}", self.config.out_dir.display()))?;
        let path = self
            .config
            .out_dir
            .join(format!("{:03}-{name}.png", self.shots));
        image
            .save(&path)
            .map_err(|e| format!("{}: {e}", path.display()))?;
        let path_text = path.display().to_string();
        self.artifacts.push(path_text.clone());
        let mut data = json!({
            "path": path_text,
            "width": image.width(),
            "height": image.height(),
            "hash": fnv_hex(image.as_raw()),
        });
        if let Some(baseline) = step.str("baseline") {
            let tolerance = step.f32("tolerance").unwrap_or(0.0005) as f64;
            let outcome = self.compare_baseline(&image, baseline, tolerance)?;
            let failed = matches!(outcome["status"].as_str(), Some("mismatch" | "missing"));
            data["baseline"] = outcome.clone();
            if failed {
                return Err(Failure {
                    error: format!(
                        "screenshot differs from baseline `{baseline}` ({})",
                        outcome["status"].as_str().unwrap_or("")
                    ),
                    hint: Some(
                        "inspect the saved screenshot and *.diff.png; if the change is intended re-run with --update-baselines"
                            .to_owned(),
                    ),
                });
            }
        }
        Ok(data)
    }

    /// Renders the current frame at `scale` into an RGBA image.
    fn render(&mut self, scale: f32) -> Result<RgbaImage, Failure> {
        let theme = crate::uikit::theme::app_theme();
        let style = theme.base();
        let physical = Size::new(
            (self.size.width * scale).round() as u32,
            (self.size.height * scale).round() as u32,
        );
        let cache = self.cache.take().unwrap_or_default();
        let mut ui = UserInterface::build(self.app.view(), self.size, cache, &mut self.renderer);
        // Without a cursor, so hover highlights do not tint the capture.
        let _ = ui.update(
            &[Event::Window(
                window::Event::RedrawRequested(Instant::now()),
            )],
            self.cursor,
            &mut self.renderer,
            &mut self.clipboard,
            &mut Vec::new(),
        );
        ui.draw(
            &mut self.renderer,
            &theme,
            &iced_test::core::renderer::Style {
                text_color: style.text_color,
            },
            self.cursor,
        );
        self.cache = Some(ui.into_cache());
        let rgba = self
            .renderer
            .screenshot(physical, scale, style.background_color);
        RgbaImage::from_raw(physical.width, physical.height, rgba)
            .ok_or_else(|| Failure::from("the renderer returned an unexpected buffer size"))
    }

    fn annotation_marks(&mut self, step: &Step) -> Result<Vec<(Rectangle, String)>, Failure> {
        let Some(value) = step.get("annotate") else {
            return Ok(Vec::new());
        };
        match value {
            Value::Bool(false) => Ok(Vec::new()),
            Value::Bool(true) => {
                // Interesting widgets only: catalogued ids, one box per id, and
                // no pane-sized containers that would just frame the picture.
                let window_area = self.size.width * self.size.height;
                let mut seen = std::collections::HashSet::new();
                let mut marks = Vec::new();
                for hit in self.hits() {
                    let (_, id, _, _, visible) = hit_parts(&hit);
                    let (Some(key), Some(area)) = (id, visible) else {
                        continue;
                    };
                    let Some(meta) = id::CATALOG.iter().find(|meta| meta.id.key() == key) else {
                        continue;
                    };
                    if meta.kind == "pane"
                        || area.width < 6.0
                        || area.height < 6.0
                        || area.width * area.height > window_area * 0.35
                        || !seen.insert(key.clone())
                    {
                        continue;
                    }
                    marks.push((area, format!("{key} — {}", meta.description)));
                }
                marks.truncate(40);
                Ok(marks)
            }
            Value::Array(items) => {
                let mut marks = Vec::new();
                for item in items {
                    let target = super::protocol::parse_target(item)?;
                    let aim = Aim {
                        target,
                        nth: 0,
                        offset: [0.0, 0.0],
                    };
                    let located = self.locate(&aim)?;
                    let area = located.visible.unwrap_or(located.bounds);
                    let label = match (&located.id, &located.text) {
                        (Some(id), _) => match id::description(id) {
                            Some(description) => format!("{id} — {description}"),
                            None => id.clone(),
                        },
                        (None, Some(text)) => format!("text: {text}"),
                        _ => aim.target.describe(),
                    };
                    marks.push((area, label));
                }
                Ok(marks)
            }
            _ => Err("`annotate` is true, false or a list of targets".into()),
        }
    }

    fn compare_baseline(
        &mut self,
        image: &RgbaImage,
        baseline: &str,
        tolerance: f64,
    ) -> Result<Value, Failure> {
        let dir = self
            .config
            .baselines
            .clone()
            .ok_or_else(|| Failure::from("`baseline` needs --baselines <dir>"))?;
        std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
        let path = dir.join(format!("{}.png", sanitize_file_stem(baseline)));
        if !path.exists() || self.config.update_baselines {
            let existed = path.exists();
            image
                .save(&path)
                .map_err(|e| format!("{}: {e}", path.display()))?;
            if self.config.update_baselines || existed {
                return Ok(
                    json!({ "status": if existed { "updated" } else { "created" }, "path": path.display().to_string() }),
                );
            }
            return Ok(json!({ "status": "created", "path": path.display().to_string() }));
        }
        let expected = image::open(&path)
            .map_err(|e| format!("{}: {e}", path.display()))?
            .to_rgba8();
        if expected.dimensions() != image.dimensions() {
            return Ok(json!({
                "status": "mismatch",
                "reason": "size differs",
                "expected": [expected.width(), expected.height()],
                "actual": [image.width(), image.height()],
            }));
        }
        let mut diff = image.clone();
        let mut differing = 0u64;
        for (x, y, pixel) in image.enumerate_pixels() {
            let other = expected.get_pixel(x, y);
            let changed = pixel
                .0
                .iter()
                .zip(other.0.iter())
                .any(|(a, b)| a.abs_diff(*b) > 8);
            if changed {
                differing += 1;
                diff.put_pixel(x, y, image::Rgba([255, 0, 0, 255]));
            }
        }
        let total = (image.width() as u64 * image.height() as u64).max(1);
        let ratio = differing as f64 / total as f64;
        if ratio > tolerance {
            let diff_path = self.config.out_dir.join(format!(
                "{:03}-{}.diff.png",
                self.shots,
                sanitize_file_stem(baseline)
            ));
            let _ = diff.save(&diff_path);
            self.artifacts.push(diff_path.display().to_string());
            Ok(
                json!({ "status": "mismatch", "diff_pixels": differing, "diff_ratio": ratio, "diff_image": diff_path.display().to_string() }),
            )
        } else {
            Ok(json!({ "status": "match", "diff_pixels": differing, "diff_ratio": ratio }))
        }
    }

    // ---------------------------------------------------------------- expect

    fn do_expect(&mut self, step: &Step) -> Outcome {
        let mut checked = 0usize;
        let hits = self.hits();
        let visible_ids: Vec<String> = hits
            .iter()
            .filter(|hit| hit_parts(hit).4.is_some())
            .filter_map(|hit| hit_parts(hit).1)
            .collect();
        let all_ids: Vec<String> = hits.iter().filter_map(|hit| hit_parts(hit).1).collect();
        let texts: Vec<String> = hits.iter().filter_map(|hit| hit_parts(hit).2).collect();

        let targets = |key: &str| -> Result<Vec<Target>, Failure> {
            match step.get(key) {
                None => Ok(Vec::new()),
                Some(Value::Array(items)) => items
                    .iter()
                    .map(|item| super::protocol::parse_target(item).map_err(Failure::from))
                    .collect(),
                Some(single) => Ok(vec![super::protocol::parse_target(single)?]),
            }
        };
        let present = |target: &Target, only_visible: bool| -> bool {
            match target {
                Target::Id(wanted) => {
                    let list = if only_visible { &visible_ids } else { &all_ids };
                    list.iter().any(|id| id == wanted)
                }
                Target::Text(wanted) => texts
                    .iter()
                    .any(|text| text == wanted || text.contains(wanted.as_str())),
                Target::Point(..) => true,
            }
        };
        for target in targets("exists")? {
            checked += 1;
            if !present(&target, false) {
                return Err(Failure {
                    error: format!(
                        "expected {} to exist, but it is not on screen",
                        target.describe()
                    ),
                    hint: Some(self.suggest(&target, &hits)),
                });
            }
        }
        for target in targets("visible")? {
            checked += 1;
            if !present(&target, true) {
                return Err(Failure {
                    error: format!("expected {} to be visible", target.describe()),
                    hint: Some(self.suggest(&target, &hits)),
                });
            }
        }
        for target in targets("missing")? {
            checked += 1;
            if present(&target, false) {
                return Err(format!(
                    "expected {} to be absent, but it is on screen",
                    target.describe()
                )
                .into());
            }
        }
        for wanted in step.strings("shows") {
            checked += 1;
            if !texts.contains(&wanted) {
                let near: Vec<&String> = texts
                    .iter()
                    .filter(|text| text.to_lowercase().contains(&wanted.to_lowercase()))
                    .take(6)
                    .collect();
                return Err(Failure {
                    error: format!("expected the text {wanted:?} on screen"),
                    hint: Some(if near.is_empty() {
                        "no similar text is visible".to_owned()
                    } else {
                        format!("similar texts: {near:?}")
                    }),
                });
            }
        }
        if let Some(spec) = step.get("text") {
            checked += 1;
            let target = spec
                .get("target")
                .ok_or_else(|| Failure::from("`text` needs a `target`"))?;
            let aim = Aim {
                target: super::protocol::parse_target(target)?,
                nth: 0,
                offset: [0.0, 0.0],
            };
            let mut inner = Step::parse(&json!({ "action": "text" }));
            let _ = &mut inner;
            let located = self.locate(&aim)?;
            let actual = located.text.unwrap_or_default();
            if let Some(equals) = spec.get("equals").and_then(Value::as_str) {
                if actual != equals {
                    return Err(format!(
                        "text of {} is {actual:?}, expected {equals:?}",
                        aim.target.describe()
                    )
                    .into());
                }
            }
            if let Some(contains) = spec.get("contains").and_then(Value::as_str) {
                if !actual.contains(contains) {
                    return Err(format!(
                        "text of {} is {actual:?}, expected it to contain {contains:?}",
                        aim.target.describe()
                    )
                    .into());
                }
            }
        }
        if let Some(Value::Object(wanted)) = step.get("state") {
            let state = self.state();
            for (pointer, expected) in wanted {
                checked += 1;
                let Some(actual) = state.pointer(pointer) else {
                    return Err(Failure {
                        error: format!("no value at `{pointer}` in the state"),
                        hint: Some("use the `state` action to see the available keys".to_owned()),
                    });
                };
                if !value_matches(actual, expected) {
                    return Err(format!("state {pointer} is {actual}, expected {expected}").into());
                }
            }
        }
        if let Some(spec) = step.get("count") {
            checked += 1;
            let prefix = spec.get("prefix").and_then(Value::as_str).unwrap_or("");
            let count = visible_ids
                .iter()
                .filter(|id| id.starts_with(prefix))
                .count();
            if let Some(equals) = spec.get("equals").and_then(Value::as_u64) {
                if count as u64 != equals {
                    return Err(format!(
                        "{count} visible widgets start with `{prefix}`, expected {equals}"
                    )
                    .into());
                }
            }
            if let Some(at_least) = spec.get("at_least").and_then(Value::as_u64) {
                if (count as u64) < at_least {
                    return Err(format!("{count} visible widgets start with `{prefix}`, expected at least {at_least}").into());
                }
            }
        }
        if checked == 0 {
            return Err(Failure {
                error: "`expect` needs at least one condition".to_owned(),
                hint: Some("use exists, missing, visible, shows, text, state or count".to_owned()),
            });
        }
        Ok(json!({ "checked": checked }))
    }

    // ---------------------------------------------------------------- runner

    /// Runs one step (any JSON object with an `action`).
    pub fn run_step(&mut self, index: usize, value: &Value) -> StepResult {
        let started = std::time::Instant::now();
        let step = match Step::parse(value) {
            Ok(step) => step,
            Err(error) => {
                let mut result = StepResult::failure(index, "?", error);
                if let Some(action) = value.get("action").and_then(Value::as_str) {
                    result.action = action.to_owned();
                }
                result.hint =
                    Some("run `doppio-headless actions` for the full reference".to_owned());
                return result;
            }
        };
        let outcome = self.execute(&step);
        // While recording, a step that changed something gets a frame (so a click is visible).
        if outcome.is_ok()
            && self.recording.is_some()
            && !matches!(
                step.action.as_str(),
                "wait"
                    | "record_start"
                    | "record_stop"
                    | "screenshot"
                    | "expect"
                    | "find"
                    | "widgets"
                    | "text"
                    | "state"
                    | "log"
            )
        {
            self.record_frame();
        }
        let (ok, data, error, hint) = match outcome {
            Ok(data) => (true, data, None, None),
            Err(failure) => (false, json!({}), Some(failure.error), failure.hint),
        };
        StepResult {
            index,
            action: step.action.clone(),
            label: step.label.clone(),
            ok,
            elapsed_ms: started.elapsed().as_millis() as u64,
            frame: self.frame,
            data,
            error,
            hint,
        }
    }

    fn execute(&mut self, step: &Step) -> Outcome {
        match step.action.as_str() {
            "move" => self.do_move(step),
            "click" => {
                let count = step.usize("count", 1).clamp(1, 3);
                let button = Self::button(step.str("button"))?;
                self.do_click(step, count, button)
            }
            "double_click" => self.do_click(step, 2, Self::button(step.str("button"))?),
            "right_click" => self.do_click(step, 1, mouse::Button::Right),
            "drag" => self.do_drag(step),
            "scroll" => self.do_scroll(step),
            "mouse_down" => self.do_mouse_button(step, true),
            "mouse_up" => self.do_mouse_button(step, false),
            "type" => self.do_type(step),
            "key" => self.do_key(step.str("key").unwrap_or(""), true, true),
            "key_down" => self.do_key(step.str("key").unwrap_or(""), true, false),
            "key_up" => self.do_key(step.str("key").unwrap_or(""), false, true),
            "wait" => self.do_wait(step),
            "resize" => {
                let [w, h] = step
                    .pair("size")?
                    .ok_or_else(|| Failure::from("`resize` needs `size`"))?;
                self.size = Size::new(w, h);
                let _ = self.deliver(vec![Event::Window(window::Event::Resized(self.size))]);
                self.settle();
                Ok(json!({ "size": [w, h] }))
            }
            "file_drop" => {
                let path = step.str("path").unwrap_or_default();
                let messages = self.deliver(vec![Event::Window(window::Event::FileDropped(
                    PathBuf::from(path),
                ))]);
                self.settle();
                Ok(json!({ "messages": messages }))
            }
            "find" => self.do_find(step),
            "widgets" => self.do_widgets(step),
            "text" => self.do_text(step),
            "state" => self.do_state(step),
            "log" => self.do_log(step),
            "screenshot" => self.do_screenshot(step),
            "record_start" => self.do_record_start(step),
            "record_stop" => self.do_record_stop(step),
            "expect" => self.do_expect(step),
            "reset" => {
                let mut init = Init::default();
                if let Some(fixture) = step.str("fixture") {
                    init.fixture = Some(fixture.to_owned());
                }
                init.size = step.pair("size")?;
                init.asset_root = step.str("asset_root").map(str::to_owned);
                init.preset = step.str("preset").map(str::to_owned);
                if let Some(components) = step.get("components") {
                    init.components = Some(
                        serde_json::from_value(components.clone())
                            .map_err(|e| format!("`components`: {e}"))?,
                    );
                }
                if let Some(assets) = step.get("assets") {
                    init.assets = Some(
                        serde_json::from_value(assets.clone())
                            .map_err(|e| format!("`assets`: {e}"))?,
                    );
                }
                Ok(self.reset(&init)?)
            }
            other => Err(format!("action `{other}` is not implemented").into()),
        }
    }

    /// Runs a whole script and reports every step.
    pub fn run_script(&mut self, script: &Script, fail_fast: bool) -> RunReport {
        let mut steps = Vec::new();
        for (index, value) in script.steps.iter().enumerate() {
            let result = self.run_step(index, value);
            let failed = !result.ok;
            let keep_going = value
                .get("continue_on_error")
                .and_then(Value::as_bool)
                .unwrap_or(false);
            steps.push(result);
            if failed && !keep_going && fail_fast {
                break;
            }
            if failed && !keep_going {
                break;
            }
        }
        if self.recording.is_some() {
            // A recording left open is finished so its video is not lost.
            let _ = self.finish_recording();
        }
        let failed = steps.iter().filter(|step| !step.ok).count();
        RunReport {
            protocol: PROTOCOL_VERSION,
            script: script.name.clone(),
            ok: failed == 0,
            passed: steps.len() - failed,
            failed,
            out_dir: self.config.out_dir.display().to_string(),
            artifacts: self.artifacts.clone(),
            steps,
        }
    }

    /// The machine-readable action reference (for tooling built on the driver).
    pub fn reference() -> Value {
        reference::json()
    }
}

impl Drop for Driver {
    fn drop(&mut self) {
        for dir in &self.temp_roots {
            let _ = std::fs::remove_dir_all(dir);
        }
    }
}

fn kind_from_extension(path: &str) -> String {
    crate::uikit::asset_kind::classify(Path::new(path))
        .map(|kind| kind.key())
        .unwrap_or("file")
        .to_owned()
}

fn sanitize_file_stem(name: &str) -> String {
    let cleaned: String = name
        .chars()
        .map(|c| {
            if c.is_alphanumeric() || matches!(c, '-' | '_') {
                c
            } else {
                '_'
            }
        })
        .collect();
    if cleaned.is_empty() {
        "shot".to_owned()
    } else {
        cleaned
    }
}

fn fnv_hex(bytes: &[u8]) -> String {
    let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
    for byte in bytes {
        hash ^= *byte as u64;
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    format!("{hash:016x}")
}

/// Deep equality, plus `{ "contains": x }` for arrays and strings.
fn value_matches(actual: &Value, expected: &Value) -> bool {
    if let Value::Object(map) = expected {
        if map.len() == 1 {
            if let Some(needle) = map.get("contains") {
                return match actual {
                    Value::Array(items) => items.iter().any(|item| item == needle),
                    Value::String(text) => needle.as_str().is_some_and(|n| text.contains(n)),
                    _ => false,
                };
            }
        }
    }
    actual == expected
}

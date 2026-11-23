//! **Play and the scene views**: the Gameplay and Camera tabs, and the transport that drives them.
//!
//! Play snapshots the scene, starts the engine's game simulation (the systems `caffeine-runtime` runs, over the
//! editor's own world) and shows it through the scene's active camera in the Gameplay tab. Pause freezes it, Stop
//! ends it and restores the snapshot, so nothing the game did to the scene is kept. The Camera tab shows the scene
//! through the selected Camera 3D without playing. Both pictures come from the engine's scene views, each with its
//! own render target, and draw on demand like the editor viewport: only while playing, or after a change.

use super::{history, App, LogLevel, Message, PaneKind};
use crate::uikit::components::{scene_view, transport, viewport_shader};
use crate::uikit::id;
use doppio_engine::{RenderState, SceneView};
use iced::{Element, Size};
use std::time::Instant;

/// Frames a view keeps drawing after the last change so temporal effects settle.
const SETTLE_FRAMES: u32 = 12;
/// Render the pictures above their logical size, like the editor viewport.
const PIXEL_RATIO: f32 = 1.5;
const PIXEL_BUDGET: f32 = 2_500_000.0;

/// One scene view's picture and its render bookkeeping.
pub(super) struct Feed {
    pub frame: Option<viewport_shader::Frame>,
    pixels: Vec<u8>,
    /// The pane's logical size, as the surface reports it.
    pub logical: (f32, f32),
    render_size: (u32, u32),
    pending: bool,
    pub requests: u64,
    uploads: u64,
    /// The scene change this picture shows (see `App::scene_epoch`).
    epoch: u64,
    settle: u32,
    last_request: Option<Instant>,
    /// The scene has no Camera 3D to look through.
    pub missing_camera: bool,
}

impl Feed {
    fn new() -> Self {
        Self {
            frame: None,
            pixels: Vec::new(),
            logical: (0.0, 0.0),
            render_size: (0, 0),
            pending: false,
            requests: 0,
            uploads: 0,
            epoch: u64::MAX,
            settle: 0,
            last_request: None,
            missing_camera: false,
        }
    }

    /// The render target for the pane: logical size scaled up, within a pixel budget, never tiny.
    fn target(&self) -> (u32, u32) {
        // Until the pane has reported its size (or where nothing reports it), draw at a sensible default.
        let (logical_width, logical_height) = if self.logical.0 > 1.0 && self.logical.1 > 1.0 {
            self.logical
        } else {
            (640.0, 360.0)
        };
        let width = (logical_width * PIXEL_RATIO).max(64.0);
        let height = (logical_height * PIXEL_RATIO).max(36.0);
        let scale = if width * height > PIXEL_BUDGET {
            (PIXEL_BUDGET / (width * height)).sqrt()
        } else {
            1.0
        };
        (
            ((width * scale) as u32).max(2) & !1,
            ((height * scale) as u32).max(2) & !1,
        )
    }
}

pub(super) struct SceneViews {
    pub game: Feed,
    pub camera: Feed,
    /// The scene as it was when Play was pressed; Stop puts it back.
    before_play: Option<history::State>,
    /// A play session exists: playing or paused (Stop ends it).
    pub session: bool,
    /// The engine runs the game simulation (a library without it only ticks the scene).
    pub game_active: bool,
    last_game_tick: Instant,
}

impl Default for SceneViews {
    fn default() -> Self {
        Self {
            game: Feed::new(),
            camera: Feed::new(),
            before_play: None,
            session: false,
            game_active: false,
            last_game_tick: Instant::now(),
        }
    }
}

impl App {
    pub(super) fn transport_state(&self) -> transport::State {
        if self.playing {
            transport::State::Playing
        } else if self.views.session {
            transport::State::Paused
        } else {
            transport::State::Stopped
        }
    }

    /// Play / Pause: from Stopped it starts a session, while playing it pauses, while paused it resumes.
    pub(super) fn toggle_play(&mut self) {
        if self.engine.is_none() {
            return;
        }
        if !self.views.session {
            self.begin_play();
        } else {
            self.playing = !self.playing;
            self.views.last_game_tick = Instant::now();
            self.sync_simulation();
            self.log(
                LogLevel::Action,
                if self.playing { "Resume" } else { "Pause" },
            );
        }
    }

    fn begin_play(&mut self) {
        self.views.before_play = Some(self.capture_state());
        let started = self
            .engine
            .as_mut()
            .map(|engine| engine.game_start())
            .unwrap_or(false);
        self.views.game_active = started;
        self.views.session = true;
        self.playing = true;
        self.views.last_game_tick = Instant::now();
        self.sync_simulation();
        self.show_gameplay_pane();
        let scripts = self
            .engine
            .as_ref()
            .map(|engine| engine.game_state().scripts_running)
            .unwrap_or(false);
        self.log(
            LogLevel::Action,
            if started && scripts {
                "Play: the game runs with its scripts"
            } else if started {
                "Play: the game runs (scripts are not available in this build)"
            } else {
                "Play: this engine library cannot run the game, only animate the scene"
            },
        );
    }

    /// Stop: the game ends and the scene returns to what it was before Play.
    pub(super) fn stop_play(&mut self) {
        if !self.views.session {
            return;
        }
        if let Some(engine) = self.engine.as_mut() {
            engine.game_stop();
            engine.game_release_keys();
        }
        self.playing = false;
        self.views.session = false;
        self.views.game_active = false;
        self.sync_simulation();
        if let Some(state) = self.views.before_play.take() {
            self.apply_history_state(state);
        }
        self.refresh_components();
        self.log(
            LogLevel::Action,
            "Stop: the scene is back as it was before Play",
        );
    }

    /// Advances the running game by the real time since its last frame. Independent of which tab is showing:
    /// the game keeps running behind an editor tab.
    pub(super) fn advance_game(&mut self) {
        if !(self.views.session && self.playing) {
            return;
        }
        let dt = self
            .views
            .last_game_tick
            .elapsed()
            .as_secs_f32()
            .clamp(0.0, 0.1);
        self.views.last_game_tick = Instant::now();
        if let Some(engine) = self.engine.as_mut() {
            if self.views.game_active {
                engine.game_tick(dt);
            } else {
                engine.tick(dt);
            }
        }
        // The game moves things: both pictures show a new scene.
        self.scene_epoch += 1;
    }

    /// A key reached the game (only while it plays); `scancode` is the USB HID / SDL scancode.
    pub(super) fn game_key(&mut self, scancode: u32, down: bool) {
        if self.views.session && self.playing {
            if let Some(engine) = self.engine.as_mut() {
                engine.game_key(scancode, down);
            }
        }
    }

    pub(super) fn game_pointer(&mut self, x: f32, y: f32, button: u32, down: bool) {
        if self.views.session && self.playing {
            if let Some(engine) = self.engine.as_mut() {
                engine.game_mouse(x, y, button, down);
            }
        }
    }

    /// True while the game owns the keyboard: the editor's own shortcuts step aside so WASD plays the game
    /// instead of flying the editor camera.
    pub(super) fn game_has_keyboard(&self) -> bool {
        self.views.session && self.playing && self.feed_visible(PaneKind::Gameplay)
    }

    pub(super) fn show_gameplay_pane(&mut self) {
        self.show_scene_pane(PaneKind::Gameplay);
    }

    pub(super) fn show_camera_preview_pane(&mut self) {
        self.show_scene_pane(PaneKind::CameraPreview);
    }

    /// Brings a tab to the front, adding it next to the Viewport when the layout lost it.
    fn show_scene_pane(&mut self, kind: PaneKind) {
        if let Some(window) = self.detach.window_of(kind) {
            self.detach.pending_focus.push(window);
            return;
        }
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group.pages.iter().position(|page| *page == kind) {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Viewport) {
                    group.pages.insert(1, kind);
                    group.active = 1;
                    break;
                }
            }
        }
        self.scene_epoch += 1;
    }

    /// Whether a tab is on screen: the active tab of a pane, or a detached window.
    pub(super) fn feed_visible(&self, kind: PaneKind) -> bool {
        self.detach.window_of(kind).is_some()
            || self
                .panes
                .iter()
                .any(|(_, group)| group.active_kind() == kind)
    }

    /// A scene view's frame is being read back. The device keeps one offscreen frame in flight, so the editor
    /// viewport and the other views wait their turn.
    pub(super) fn scene_frame_in_flight(&self) -> bool {
        self.views.game.pending || self.views.camera.pending
    }

    pub(super) fn feed(&self, view: SceneView) -> &Feed {
        match view {
            SceneView::Game => &self.views.game,
            SceneView::Camera => &self.views.camera,
        }
    }

    fn feed_mut(&mut self, view: SceneView) -> &mut Feed {
        match view {
            SceneView::Game => &mut self.views.game,
            SceneView::Camera => &mut self.views.camera,
        }
    }

    /// Resizes a view's picture to its pane.
    pub(super) fn feed_resized(&mut self, view: SceneView, size: Size) {
        self.feed_mut(view).logical = (size.width, size.height);
    }

    /// The camera preview looks through the selected entity when it is a camera, else the active one.
    fn sync_preview_camera(&mut self) {
        let selected = self.entities.get(self.selected).map(|entity| entity.id);
        let is_camera = selected.is_some_and(|id| {
            self.engine.as_ref().is_some_and(|engine| {
                engine
                    .entity_components(id)
                    .iter()
                    .any(|component| component.name == "Camera 3D")
            })
        });
        if let Some(engine) = self.engine.as_mut() {
            engine.set_preview_camera(if is_camera { selected } else { None });
        }
    }

    /// Reads back and requests the pictures of the Gameplay and Camera tabs that are on screen.
    pub(super) fn pump_feeds(&mut self) {
        for (view, kind) in [
            (SceneView::Game, PaneKind::Gameplay),
            (SceneView::Camera, PaneKind::CameraPreview),
        ] {
            // A frame already asked for is always collected, even when its tab has since been hidden: it
            // would otherwise stay "in flight" for ever and keep every other picture from drawing.
            if self.feed_visible(kind) || self.feed(view).pending {
                self.pump_feed(view);
            }
        }
    }

    fn pump_feed(&mut self, view: SceneView) {
        if self.engine.is_none() {
            return;
        }
        if self.feed(view).pending {
            self.poll_feed(view);
            return;
        }
        if view == SceneView::Camera {
            self.sync_preview_camera();
        }
        // Collected above even when hidden; a frame is only requested for a tab that is on screen.
        let kind = match view {
            SceneView::Game => PaneKind::Gameplay,
            SceneView::Camera => PaneKind::CameraPreview,
        };
        if !self.feed_visible(kind) {
            return;
        }
        let has_camera = self
            .engine
            .as_ref()
            .and_then(|engine| engine.scene_camera(view))
            .is_some();
        let animating = self.playing && self.views.session;
        let epoch = self.scene_epoch;
        let feed = self.feed_mut(view);
        feed.missing_camera = !has_camera;
        if !has_camera {
            feed.frame = None;
            return;
        }
        // One frame in flight at a time across the editor viewport and both scene views.
        if self.viewport_pending || self.scene_frame_in_flight() {
            return;
        }
        let feed = self.feed_mut(view);
        let target = feed.target();
        let resized = target != feed.render_size;
        let changed = feed.epoch != epoch;
        if !(animating || changed || feed.settle > 0 || resized) {
            return;
        }
        // Throttle like the editor viewport.
        if feed
            .last_request
            .is_some_and(|last| last.elapsed() < std::time::Duration::from_millis(33))
        {
            return;
        }
        if resized {
            feed.render_size = target;
            feed.pixels = vec![0; (target.0 * target.1 * 4) as usize];
        }
        let (width, height) = feed.render_size;
        let started = self
            .engine
            .as_mut()
            .map(|engine| engine.view_render_begin(view, width, height));
        let feed = self.feed_mut(view);
        feed.last_request = Some(Instant::now());
        if matches!(started, Some(Ok(()))) {
            feed.pending = true;
            feed.requests += 1;
            if changed {
                feed.epoch = epoch;
                feed.settle = SETTLE_FRAMES;
            } else {
                feed.settle = feed.settle.saturating_sub(1);
            }
        }
    }

    fn poll_feed(&mut self, view: SceneView) {
        let Some(engine) = self.engine.as_mut() else {
            return;
        };
        let feed = match view {
            SceneView::Game => &mut self.views.game,
            SceneView::Camera => &mut self.views.camera,
        };
        match engine.view_render_poll(view, &mut feed.pixels) {
            Ok(RenderState::Ready) => {
                feed.pending = false;
                let (width, height) = feed.render_size;
                let pixels = std::mem::take(&mut feed.pixels);
                feed.uploads += 1;
                feed.frame = Some(viewport_shader::Frame {
                    pixels: std::sync::Arc::new(pixels),
                    width,
                    height,
                    id: feed.uploads,
                });
                feed.pixels = vec![0; (width * height * 4) as usize];
            }
            Ok(RenderState::Pending) => {}
            Ok(RenderState::Idle) | Ok(RenderState::Failed) | Err(_) => feed.pending = false,
        }
    }

    /// The Gameplay tab.
    pub(super) fn gameplay_view(&self) -> Element<'_, Message> {
        let feed = self.feed(SceneView::Game);
        let state = match self.transport_state() {
            transport::State::Playing => "playing",
            transport::State::Paused => "paused",
            transport::State::Stopped => "stopped: press ▶ to play",
        };
        let scripts = self
            .engine
            .as_ref()
            .map(|engine| engine.game_state().scripts_running)
            .unwrap_or(false);
        let status = format!(
            "Game camera · {state}{}",
            if self.views.session && !scripts {
                " · no scripts"
            } else {
                ""
            }
        );
        let placeholder = feed.missing_camera.then(|| {
            (
                "No Camera 3D in the scene".to_owned(),
                "Add a Camera 3D to an entity: the game is seen through it.".to_owned(),
            )
        });
        let interactive = self.views.session && self.playing;
        scene_view::view(scene_view::SceneView {
            id: id::GAMEPLAY_VIEW,
            frame: feed.frame.as_ref(),
            slot: 1,
            status,
            placeholder,
            on_move: interactive.then(|| {
                Box::new(|point: iced::Point| Message::GamePointer(point.x, point.y, 0, false))
                    as Box<dyn Fn(iced::Point) -> Message>
            }),
            on_button: interactive.then(|| {
                Box::new(|button: u32, down: bool| Message::GamePointer(0.0, 0.0, button, down))
                    as Box<dyn Fn(u32, bool) -> Message>
            }),
            on_resize: Box::new(|size| Message::SceneViewResized(SceneView::Game, size)),
        })
    }

    /// The Camera tab.
    pub(super) fn camera_preview_view(&self) -> Element<'_, Message> {
        let feed = self.feed(SceneView::Camera);
        let name = self
            .engine
            .as_ref()
            .and_then(|engine| engine.scene_camera(SceneView::Camera))
            .and_then(|(entity, fov)| {
                self.entities
                    .iter()
                    .find(|candidate| candidate.id == entity)
                    .map(|candidate| format!("{} · {fov:.0}° fov", candidate.name))
            })
            .unwrap_or_else(|| "no camera".to_owned());
        let placeholder = feed.missing_camera.then(|| {
            (
                "No Camera 3D in the scene".to_owned(),
                "Select an entity with a Camera 3D to see through it.".to_owned(),
            )
        });
        scene_view::view(scene_view::SceneView {
            id: id::CAMERA_PREVIEW_VIEW,
            frame: feed.frame.as_ref(),
            slot: 2,
            status: format!("Camera · {name}"),
            placeholder,
            on_move: None,
            on_button: None,
            on_resize: Box::new(|size| Message::SceneViewResized(SceneView::Camera, size)),
        })
    }
}

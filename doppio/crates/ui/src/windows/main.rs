use crate::uikit::components::button;
use crate::uikit::panels;
use crate::uikit::EntityView;
use crate::windows::projectloader;

use iced::widget::{column, pane_grid, row, space, text};
use iced::Element;

#[cfg(feature = "agent")]
pub fn run() -> iced::Result {
    iced_mcp::run(
        iced::application(App::new, App::update, App::view).subscription(App::subscription),
    )
}

#[cfg(not(feature = "agent"))]
pub fn run() -> iced::Result {
    iced::application(App::new, App::update, App::view)
        .subscription(App::subscription)
        .run()
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PaneKind {
    Hierarchy,
    Assets,
    Viewport,
    Inspector,
    Console,
}

#[derive(Debug, Clone)]
enum Message {
    PaneDragged(pane_grid::DragEvent),
    PaneResized(pane_grid::ResizeEvent),
    SelectEntity(usize),
    NameChanged(String),
    TogglePlay,
    AddEntity,
    DeleteSelected,
    NudgeX,
    NudgeRotationY,
    ScaleUp,
    SaveScene,
    RenderViewport,
    OrbitLeft,
    OrbitRight,
    ZoomIn,
    ZoomOut,
    ResetCamera,
    Log,
}

struct App {
    panes: pane_grid::State<PaneKind>,
    engine: Option<doppio_engine::Engine>,
    entities: Vec<EntityView>,
    assets: Vec<doppio_engine::AssetInfo>,
    selected: usize,
    name_input: String,
    log: Vec<String>,
    playing: bool,
    next_entity_id: u32,
    viewport_image: Option<iced::widget::image::Handle>,
    camera: doppio_engine::Camera,
}

impl App {
    fn new() -> Self {
        use pane_grid::{Axis, Configuration};

        let configuration = Configuration::Split {
            axis: Axis::Horizontal,
            ratio: 0.72,
            a: Box::new(Configuration::Split {
                axis: Axis::Vertical,
                ratio: 0.22,
                a: Box::new(Configuration::Split {
                    axis: Axis::Horizontal,
                    ratio: 0.58,
                    a: Box::new(Configuration::Pane(PaneKind::Hierarchy)),
                    b: Box::new(Configuration::Pane(PaneKind::Assets)),
                }),
                b: Box::new(Configuration::Split {
                    axis: Axis::Vertical,
                    ratio: 0.72,
                    a: Box::new(Configuration::Pane(PaneKind::Viewport)),
                    b: Box::new(Configuration::Pane(PaneKind::Inspector)),
                }),
            }),
            b: Box::new(Configuration::Pane(PaneKind::Console)),
        };
        let panes = pane_grid::State::with_configuration(configuration);

        let loaded = projectloader::load_from_args();
        let next_entity_id = loaded
            .entities
            .iter()
            .map(|entity| entity.id.saturating_add(1))
            .max()
            .unwrap_or(0);
        let name_input = loaded
            .entities
            .first()
            .map(|entity| entity.name.clone())
            .unwrap_or_default();

        let camera = loaded
            .engine
            .as_ref()
            .and_then(|engine| engine.camera().ok())
            .unwrap_or_default();

        Self {
            panes,
            engine: loaded.engine,
            entities: loaded.entities,
            assets: loaded.assets,
            selected: 0,
            name_input,
            log: loaded.log,
            playing: false,
            next_entity_id,
            viewport_image: None,
            camera,
        }
    }

    fn apply_camera(&mut self) {
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.set_camera(self.camera);
        }
    }

    fn subscription(&self) -> iced::Subscription<Message> {
        iced::time::every(std::time::Duration::from_millis(500)).map(|_| Message::RenderViewport)
    }

    fn render_viewport(&mut self) {
        const WIDTH: u32 = 640;
        const HEIGHT: u32 = 360;
        let Some(engine) = self.engine.as_mut() else {
            return;
        };
        match engine.render(WIDTH, HEIGHT) {
            Ok(pixels) => {
                if self.viewport_image.is_none() {
                    self.log.push("Viewport ready".to_owned());
                }
                self.viewport_image = Some(iced::widget::image::Handle::from_rgba(
                    WIDTH, HEIGHT, pixels,
                ));
            }
            Err(_) => {
                if self.log.last().map(String::as_str) != Some("Viewport render failed") {
                    self.log.push("Viewport render failed".to_owned());
                }
            }
        }
    }

    fn update(&mut self, message: Message) {
        match message {
            Message::PaneDragged(event) => {
                if let pane_grid::DragEvent::Dropped { pane, target } = event {
                    self.panes.drop(pane, target);
                }
            }
            Message::PaneResized(event) => {
                self.panes.resize(event.split, event.ratio);
            }
            Message::SelectEntity(index) => {
                if let Some(entity) = self.entities.get(index) {
                    self.selected = index;
                    self.name_input = entity.name.clone();
                    if let Some(engine) = self.engine.as_mut() {
                        let _ = engine.select_entity(entity.id);
                        if engine.selected_entity() != entity.id {
                            self.log.push("Engine selection mismatch".to_owned());
                        }
                    }
                    self.log.push(format!("Selected {}", entity.name));
                }
            }
            Message::NameChanged(value) => {
                self.name_input = value;
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    entity.name = self.name_input.clone();
                    if let Some(engine) = self.engine.as_mut() {
                        let _ = engine.set_entity_name(entity.id, &self.name_input);
                        if let Ok(confirmed) = engine.entity_name(entity.id) {
                            entity.name = confirmed;
                        }
                    }
                }
            }
            Message::TogglePlay => {
                self.playing = !self.playing;
                self.log
                    .push(if self.playing { "Play" } else { "Stop" }.to_owned());
                if let Some(engine) = self.engine.as_mut() {
                    engine.tick(0.016);
                    let stats = engine.frame_stats();
                    self.log.push(format!(
                        "Engine frame {} ({} entities, {:.2} ms)",
                        stats.frame_index,
                        stats.entity_count,
                        stats.delta_time * 1000.0
                    ));
                }
            }
            Message::AddEntity => {
                let index = self.entities.len();
                let name = format!("Entity {}", index + 1);
                let id = if let Some(engine) = self.engine.as_mut() {
                    engine
                        .create_entity(&name)
                        .unwrap_or(doppio_engine::ENTITY_NONE)
                } else {
                    let id = self.next_entity_id;
                    self.next_entity_id += 1;
                    id
                };
                if id != doppio_engine::ENTITY_NONE {
                    self.entities.push(EntityView {
                        id,
                        name: name.clone(),
                        position: [0.0; 3],
                        rotation: [0.0; 3],
                        scale: [1.0; 3],
                    });
                    self.selected = index;
                    self.name_input = name.clone();
                    self.log.push(format!("Added {name}"));
                }
            }
            Message::DeleteSelected => {
                if self.entities.is_empty() {
                    return;
                }
                let entity = self.entities.remove(self.selected);
                if let Some(engine) = self.engine.as_mut() {
                    let _ = engine.destroy_entity(entity.id);
                }
                if self.selected >= self.entities.len() {
                    self.selected = self.entities.len().saturating_sub(1);
                }
                self.name_input = self
                    .entities
                    .get(self.selected)
                    .map(|entity| entity.name.clone())
                    .unwrap_or_default();
                self.log.push(format!("Deleted {}", entity.name));
            }
            Message::NudgeX => {
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    let mut position = entity.position;
                    position[0] += 1.0;
                    if let Some(engine) = self.engine.as_mut() {
                        let _ = engine.set_position(entity.id, position);
                        if let Ok(readback) = engine.position(entity.id) {
                            position = readback;
                        }
                    }
                    entity.position = position;
                    self.log
                        .push(format!("Moved {} to x={:.2}", entity.name, position[0]));
                }
            }
            Message::NudgeRotationY => {
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    let mut rotation = entity.rotation;
                    rotation[1] += 15.0;
                    if let Some(engine) = self.engine.as_mut() {
                        let _ = engine.set_rotation(entity.id, rotation);
                        if let Ok(readback) = engine.rotation(entity.id) {
                            rotation = readback;
                        }
                    }
                    entity.rotation = rotation;
                    self.log
                        .push(format!("Rotated {} to y={:.2}", entity.name, rotation[1]));
                }
            }
            Message::ScaleUp => {
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    let mut scale = entity.scale;
                    scale = [scale[0] * 1.1, scale[1] * 1.1, scale[2] * 1.1];
                    if let Some(engine) = self.engine.as_mut() {
                        let _ = engine.set_scale(entity.id, scale);
                        if let Ok(readback) = engine.scale(entity.id) {
                            scale = readback;
                        }
                    }
                    entity.scale = scale;
                    self.log
                        .push(format!("Scaled {} to x={:.2}", entity.name, scale[0]));
                }
            }
            Message::SaveScene => {
                if let Some(engine) = self.engine.as_mut() {
                    match engine.scene_path() {
                        Some(path) => match engine.save_scene(&path) {
                            Ok(()) => self.log.push(format!("Saved {path}")),
                            Err(_) => self.log.push(format!("Could not save {path}")),
                        },
                        None => self.log.push("No scene path to save".to_owned()),
                    }
                } else {
                    self.log.push("No engine session to save".to_owned());
                }
            }
            Message::RenderViewport => {
                self.render_viewport();
            }
            Message::OrbitLeft => {
                self.camera.yaw -= 0.2;
                self.apply_camera();
            }
            Message::OrbitRight => {
                self.camera.yaw += 0.2;
                self.apply_camera();
            }
            Message::ZoomIn => {
                self.camera.distance = (self.camera.distance * 0.9).max(1.0);
                self.apply_camera();
            }
            Message::ZoomOut => {
                self.camera.distance = (self.camera.distance * 1.1).min(500.0);
                self.apply_camera();
            }
            Message::ResetCamera => {
                self.camera = doppio_engine::Camera::default();
                self.apply_camera();
            }
            Message::Log => {
                self.log.push("Hello from the Rust UI".to_owned());
            }
        }
    }

    fn view(&self) -> Element<'_, Message> {
        let toolbar = row![
            text("Doppio").size(20),
            space::horizontal(),
            button::toolbar(
                "toggle-play",
                if self.playing { "Pause" } else { "Play" },
                Message::TogglePlay,
            ),
            button::primary("add-entity", "Add Entity", Message::AddEntity),
            button::toolbar("save-scene", "Save", Message::SaveScene),
            button::toolbar("render-viewport", "Render", Message::RenderViewport),
            button::toolbar("log", "Log", Message::Log),
            text(if self.engine.is_some() {
                "C++ engine"
            } else {
                "fixtures"
            })
            .size(12),
            text(
                if self
                    .engine
                    .as_ref()
                    .map(|engine| engine.is_dirty())
                    .unwrap_or(false)
                {
                    "*"
                } else {
                    ""
                }
            )
            .size(14),
        ]
        .spacing(8)
        .padding(8)
        .align_y(iced::Alignment::Center);

        let grid = pane_grid(&self.panes, |_pane, kind, _is_maximized| {
            pane_grid::Content::new(self.pane_view(*kind))
        })
        .on_drag(Message::PaneDragged)
        .on_resize(8, Message::PaneResized)
        .spacing(4);

        column![toolbar, grid].into()
    }

    fn pane_view(&self, kind: PaneKind) -> Element<'_, Message> {
        match kind {
            PaneKind::Hierarchy => {
                panels::hierarchy::view(&self.entities, self.selected, Message::SelectEntity)
            }
            PaneKind::Assets => panels::assetmanager::view(&self.assets),
            PaneKind::Viewport => panels::sceneviewport::view(
                self.viewport_image.clone(),
                Message::OrbitLeft,
                Message::OrbitRight,
                Message::ZoomIn,
                Message::ZoomOut,
                Message::ResetCamera,
            ),
            PaneKind::Inspector => panels::inspector::view(
                self.entities.get(self.selected),
                &self.name_input,
                Message::NameChanged,
                Message::NudgeX,
                Message::NudgeRotationY,
                Message::ScaleUp,
                Message::DeleteSelected,
            ),
            PaneKind::Console => panels::console::view(&self.log),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use iced::widget::Id;
    use iced_test::simulator;

    fn test_guard() -> std::sync::MutexGuard<'static, ()> {
        static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
        LOCK.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    fn click_and_update(app: &mut App, id: &'static str) {
        let mut ui = simulator(app.view());
        let clicked = ui.click(Id::new(id));
        assert!(clicked.is_ok(), "widget {id} should be clickable");
        for message in ui.into_messages() {
            app.update(message);
        }
    }

    #[test]
    fn add_entity_updates_hierarchy_and_inspector() {
        let _guard = test_guard();
        let mut app = App::new();
        let before = app.entities.len();
        click_and_update(&mut app, "add-entity");
        assert_eq!(app.entities.len(), before + 1);
        assert_eq!(app.selected, before);

        let mut ui = simulator(app.view());
        assert!(ui.find("Entity 3").is_ok(), "new entity should be listed");
    }

    #[test]
    fn toggle_play_updates_label() {
        let _guard = test_guard();
        let mut app = App::new();
        click_and_update(&mut app, "toggle-play");
        assert!(app.playing);
        let mut ui = simulator(app.view());
        assert!(ui.find("Pause").is_ok(), "button should read Pause");
    }

    #[test]
    fn nudge_x_updates_position() {
        let _guard = test_guard();
        let mut app = App::new();
        let before = app.entities[0].position[0];
        click_and_update(&mut app, "nudge-x");
        assert!(app.entities[0].position[0] > before);
    }

    #[test]
    fn nudge_rotation_updates_rotation() {
        let _guard = test_guard();
        let mut app = App::new();
        let before = app.entities[0].rotation[1];
        click_and_update(&mut app, "nudge-rot-y");
        assert!(app.entities[0].rotation[1] > before);
    }

    #[test]
    fn scale_up_updates_scale() {
        let _guard = test_guard();
        let mut app = App::new();
        let before = app.entities[0].scale[0];
        click_and_update(&mut app, "scale-up");
        assert!(app.entities[0].scale[0] > before);
    }

    #[test]
    fn orbit_changes_camera_yaw() {
        let _guard = test_guard();
        let mut app = App::new();
        let before = app.camera.yaw;
        click_and_update(&mut app, "orbit-left");
        assert!(app.camera.yaw < before);
    }

    #[test]
    fn rename_updates_selected_entity() {
        let _guard = test_guard();
        let mut app = App::new();
        click_and_update(&mut app, "entity-1");
        assert_eq!(app.selected, 1);

        let mut ui = simulator(app.view());
        let focused = ui.click(Id::new("entity-name"));
        assert!(focused.is_ok(), "name field should be focusable");
        let _ = ui.typewrite("Boss");
        for message in ui.into_messages() {
            app.update(message);
        }
        assert!(app.entities[1].name.contains("Boss"));
    }
}

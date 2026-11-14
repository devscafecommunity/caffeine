use crate::history::{self, History};
use crate::uikit::components::button;
use crate::uikit::id;
use crate::uikit::panels;
use crate::uikit::EntityView;
use crate::windows::projectloader;

use iced::widget::{column, pane_grid, row, space, text};
use iced::Element;
use std::collections::HashMap;

const VIEWPORT_WIDTH: u32 = 960;
const VIEWPORT_HEIGHT: u32 = 540;

#[cfg(feature = "agent")]
pub fn run() -> iced::Result {
    agent::run()
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
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    SelectEntityByName(String),
    TogglePlay,
    AddEntity,
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    AddEntityNamed(String),
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    SetCamera(doppio_engine::Camera),
    Inspector(panels::inspector::Edit),
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    SetComponentField {
        component: usize,
        field: usize,
        value: doppio_engine::FieldValue,
    },
    SaveScene,
    RenderViewport,
    ViewportTick,
    OrbitLeft,
    OrbitRight,
    ZoomIn,
    ZoomOut,
    ResetCamera,
    Undo,
    Redo,
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    CloseScene,
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
    viewport_pixels: Vec<u8>,
    viewport_pending: bool,
    camera: doppio_engine::Camera,
    history: History,
    component_types: Vec<doppio_engine::ComponentType>,
    components: Vec<doppio_engine::ComponentData>,
    field_inputs: HashMap<(usize, usize, usize), String>,
}

impl App {
    fn new() -> Self {
        let loaded = projectloader::load_from_args();
        Self::with_project(loaded)
    }

    /// Editor with no scene and no engine session, used by agent fixtures.
    #[cfg_attr(not(any(test, feature = "agent")), allow(dead_code))]
    fn empty() -> Self {
        Self::with_project(projectloader::LoadedProject {
            engine: None,
            entities: Vec::new(),
            assets: Vec::new(),
            log: vec!["Empty fixture".to_owned()],
        })
    }

    fn with_project(loaded: projectloader::LoadedProject) -> Self {
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

        let mut app = Self {
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
            viewport_pixels: vec![0; (VIEWPORT_WIDTH * VIEWPORT_HEIGHT * 4) as usize],
            viewport_pending: false,
            camera,
            history: History::new(history::State::new(Vec::new(), None, false)),
            component_types: Vec::new(),
            components: Vec::new(),
            field_inputs: HashMap::new(),
        };
        if let Some(engine) = app.engine.as_ref() {
            app.component_types = engine.component_types();
        }
        app.refresh_components();
        let initial = app.capture_state();
        app.history = History::new(initial);
        app
    }

    fn refresh_components(&mut self) {
        self.field_inputs.clear();
        self.components = match (self.engine.as_ref(), self.entities.get(self.selected)) {
            (Some(engine), Some(entity)) => engine.entity_component_data(entity.id),
            _ => Vec::new(),
        };
    }

    fn handle_inspector(&mut self, edit: panels::inspector::Edit) {
        match edit {
            panels::inspector::Edit::Name(value) => self.rename_selected(value),
            panels::inspector::Edit::FieldText {
                component,
                field,
                axis,
                value,
            } => self.edit_component_text(component, field, axis, value),
            panels::inspector::Edit::FieldBool {
                component,
                field,
                value,
            } => self.edit_component_bool(component, field, value),
            panels::inspector::Edit::FieldEnum {
                component,
                field,
                index,
            } => self.edit_component_enum(component, field, index),
            panels::inspector::Edit::AddComponent(type_id) => self.add_component(type_id),
            panels::inspector::Edit::RemoveComponent(component) => self.remove_component(component),
            panels::inspector::Edit::Delete => self.delete_selected(),
        }
    }

    fn rename_selected(&mut self, value: String) {
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
        let name = format!("Rename {}", self.name_input);
        let key = Some(format!("rename:{}", self.selected));
        self.commit(name, key);
    }

    fn write_component_field(
        &mut self,
        component: usize,
        field: usize,
        value: doppio_engine::FieldValue,
    ) {
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.set_component_field(entity, component as u32, field as u32, &value);
        }
        let mut type_id = 0;
        let mut field_name = String::new();
        if let Some(data) = self.components.get_mut(component) {
            type_id = data.info.type_id;
            if let Some(slot) = data.values.get_mut(field) {
                *slot = value;
            }
            if let Some(meta) = self
                .component_types
                .iter()
                .find(|kind| kind.id == type_id)
                .and_then(|kind| kind.fields.get(field))
            {
                field_name = meta.name.clone();
            }
        }
        self.sync_selected_transform();
        let name = format!("Set {field_name}");
        let key = Some(format!("field:{}:{}:{}", self.selected, type_id, field));
        self.commit(name, key);
    }

    fn sync_selected_transform(&mut self) {
        let Some(id) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        let Some(engine) = self.engine.as_ref() else {
            return;
        };
        let position = engine.position(id).ok();
        let rotation = engine.rotation(id).ok();
        let scale = engine.scale(id).ok();
        if let Some(entity) = self.entities.get_mut(self.selected) {
            if let Some(position) = position {
                entity.position = position;
            }
            if let Some(rotation) = rotation {
                entity.rotation = rotation;
            }
            if let Some(scale) = scale {
                entity.scale = scale;
            }
        }
    }

    fn edit_component_text(&mut self, component: usize, field: usize, axis: usize, text: String) {
        self.field_inputs
            .insert((component, field, axis), text.clone());
        let Some(current) = self
            .components
            .get(component)
            .and_then(|data| data.values.get(field))
            .cloned()
        else {
            return;
        };
        let Some(value) =
            crate::uikit::components::field_editor::parse_value(&current, axis, &text)
        else {
            return;
        };
        self.write_component_field(component, field, value);
    }

    fn edit_component_bool(&mut self, component: usize, field: usize, flag: bool) {
        self.write_component_field(component, field, doppio_engine::FieldValue::Bool(flag));
    }

    fn edit_component_enum(&mut self, component: usize, field: usize, index: i32) {
        self.write_component_field(component, field, doppio_engine::FieldValue::Enum(index));
    }

    fn add_component(&mut self, type_id: u32) {
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        let name = self
            .component_types
            .iter()
            .find(|kind| kind.id == type_id)
            .map(|kind| kind.name.clone())
            .unwrap_or_else(|| format!("Component {type_id}"));
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.add_component(entity, type_id);
        }
        self.refresh_components();
        self.commit(format!("Add {name}"), None);
    }

    fn remove_component(&mut self, component: usize) {
        let Some(type_id) = self.components.get(component).map(|data| data.info.type_id) else {
            return;
        };
        let name = self
            .components
            .get(component)
            .map(|data| data.info.name.clone())
            .unwrap_or_default();
        let Some(entity) = self.entities.get(self.selected).map(|entity| entity.id) else {
            return;
        };
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.remove_component(entity, type_id);
        }
        self.refresh_components();
        self.commit(format!("Remove {name}"), None);
    }

    fn delete_selected(&mut self) {
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
        self.refresh_components();
        self.log.push(format!("Deleted {}", entity.name));
        self.commit(format!("Delete {}", entity.name), None);
    }

    fn capture_state(&self) -> history::State {
        let mut state = history::State::new(self.entities.clone(), None, false);
        state.selected = self.selected;
        if let Some(engine) = self.engine.as_ref() {
            if let Ok(snapshot) = engine.snapshot() {
                state.engine = Some(snapshot.data);
                state.dirty = snapshot.dirty;
            }
        }
        state
    }

    /// Records the current editor state as one undo step.
    fn commit(&mut self, name: impl Into<String>, key: Option<String>) {
        let after = self.capture_state();
        self.history.push(after, name, key);
    }

    fn amend_selection(&mut self) {
        let selected = self.selected;
        self.history
            .amend_current(|state| state.selected = selected);
    }

    fn apply_history_state(&mut self, state: history::State) {
        if let (Some(engine), Some(data)) = (self.engine.as_mut(), state.engine.as_ref()) {
            let snapshot = doppio_engine::Snapshot {
                data: data.clone(),
                dirty: state.dirty,
            };
            match engine.restore(&snapshot) {
                Ok(()) => {
                    self.entities = projectloader::read_entities(engine);
                }
                Err(error) => {
                    self.log.push(format!("Restore failed: {error:?}"));
                    self.entities = state.entities.clone();
                }
            }
            self.viewport_pending = false;
        } else {
            self.entities = state.entities.clone();
        }

        self.selected = state.selected.min(self.entities.len().saturating_sub(1));
        self.name_input = self
            .entities
            .get(self.selected)
            .map(|entity| entity.name.clone())
            .unwrap_or_default();
        if let (Some(engine), Some(entity)) =
            (self.engine.as_mut(), self.entities.get(self.selected))
        {
            let _ = engine.select_entity(entity.id);
        }
        self.refresh_components();
    }

    fn undo(&mut self) {
        if let Some((name, state)) = self.history.undo() {
            self.apply_history_state(state);
            self.log.push(format!("Undo {name}"));
        }
    }

    fn redo(&mut self) {
        if let Some((name, state)) = self.history.redo() {
            self.apply_history_state(state);
            self.log.push(format!("Redo {name}"));
        }
    }

    fn select_index(&mut self, index: usize) {
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
        self.refresh_components();
        self.amend_selection();
    }

    fn add_entity_named(&mut self, name: String) {
        let index = self.entities.len();
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

    fn apply_camera(&mut self) {
        if let Some(engine) = self.engine.as_mut() {
            let _ = engine.set_camera(self.camera);
        }
    }

    fn subscription(&self) -> iced::Subscription<Message> {
        iced::Subscription::batch([
            iced::time::every(std::time::Duration::from_millis(16)).map(|_| Message::ViewportTick),
            iced::keyboard::listen().filter_map(|event| {
                let iced::keyboard::Event::KeyPressed { key, modifiers, .. } = event else {
                    return None;
                };
                if !modifiers.control() {
                    return None;
                }
                match key.as_ref() {
                    iced::keyboard::Key::Character(character)
                        if character.eq_ignore_ascii_case("z") =>
                    {
                        Some(if modifiers.shift() {
                            Message::Redo
                        } else {
                            Message::Undo
                        })
                    }
                    iced::keyboard::Key::Character(character)
                        if character.eq_ignore_ascii_case("y") =>
                    {
                        Some(Message::Redo)
                    }
                    _ => None,
                }
            }),
        ])
    }

    fn request_viewport(&mut self) {
        if self.viewport_pending {
            return;
        }
        let started = self
            .engine
            .as_mut()
            .map(|engine| engine.render_begin(VIEWPORT_WIDTH, VIEWPORT_HEIGHT));
        match started {
            Some(Ok(())) => self.viewport_pending = true,
            Some(Err(_)) => self.log_render_failure(),
            None => {}
        }
    }

    /// Non-blocking readback: submit one frame, then map it a few ticks later.
    fn pump_viewport(&mut self) {
        if !self.viewport_pending {
            self.request_viewport();
            return;
        }

        let polled = match self.engine.as_mut() {
            Some(engine) => Some(engine.render_poll(&mut self.viewport_pixels)),
            None => None,
        };
        match polled {
            Some(Ok(doppio_engine::RenderState::Ready)) => {
                self.viewport_pending = false;
                let pixels = std::mem::take(&mut self.viewport_pixels);
                self.viewport_pixels = vec![0; pixels.len()];
                if self.viewport_image.is_none() {
                    self.log.push("Viewport ready".to_owned());
                }
                self.viewport_image = Some(iced::widget::image::Handle::from_rgba(
                    VIEWPORT_WIDTH,
                    VIEWPORT_HEIGHT,
                    pixels,
                ));
            }
            Some(Ok(doppio_engine::RenderState::Pending)) => {}
            Some(Ok(doppio_engine::RenderState::Idle)) => self.viewport_pending = false,
            Some(Ok(doppio_engine::RenderState::Failed)) | Some(Err(_)) => {
                self.viewport_pending = false;
                self.log_render_failure();
            }
            None => {}
        }
    }

    fn log_render_failure(&mut self) {
        if self.log.last().map(String::as_str) != Some("Viewport render failed") {
            self.log.push("Viewport render failed".to_owned());
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
                self.select_index(index);
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
                let name = format!("Entity {}", self.entities.len() + 1);
                self.add_entity_named(name.clone());
                self.commit(format!("Add {name}"), None);
            }
            Message::AddEntityNamed(name) => {
                self.add_entity_named(name.clone());
                self.commit(format!("Add {name}"), None);
            }
            Message::SelectEntityByName(name) => {
                if let Some(index) = self.entities.iter().position(|entity| entity.name == name) {
                    self.select_index(index);
                } else {
                    self.log.push(format!("No entity named {name}"));
                }
            }
            Message::SetCamera(camera) => {
                self.camera = camera;
                self.apply_camera();
            }
            Message::Inspector(edit) => {
                self.handle_inspector(edit);
            }
            Message::SetComponentField {
                component,
                field,
                value,
            } => {
                self.write_component_field(component, field, value);
            }
            Message::SaveScene => {
                if let Some(engine) = self.engine.as_mut() {
                    match engine.scene_path() {
                        Some(path) => match engine.save_scene(&path) {
                            Ok(()) => {
                                self.log.push(format!("Saved {path}"));
                                self.history.amend_current(|state| state.dirty = false);
                            }
                            Err(_) => self.log.push(format!("Could not save {path}")),
                        },
                        None => self.log.push("No scene path to save".to_owned()),
                    }
                } else {
                    self.log.push("No engine session to save".to_owned());
                }
            }
            Message::RenderViewport => {
                self.request_viewport();
            }
            Message::ViewportTick => {
                self.pump_viewport();
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
            Message::Undo => {
                self.undo();
            }
            Message::Redo => {
                self.redo();
            }
            Message::CloseScene => {
                self.engine = None;
                self.entities.clear();
                self.assets.clear();
                self.selected = 0;
                self.name_input.clear();
                self.viewport_image = None;
                self.log.push("Closed scene".to_owned());
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
            button::toolbar(id::UNDO, "Undo", Message::Undo),
            button::toolbar(id::REDO, "Redo", Message::Redo),
            button::toolbar(
                id::TOGGLE_PLAY,
                if self.playing { "Pause" } else { "Play" },
                Message::TogglePlay,
            ),
            button::primary(id::ADD_ENTITY, "Add Entity", Message::AddEntity),
            button::toolbar(id::SAVE_SCENE, "Save", Message::SaveScene),
            button::toolbar(id::RENDER_VIEWPORT, "Render", Message::RenderViewport),
            button::toolbar(id::LOG_MESSAGE, "Log", Message::Log),
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
        let (pane_id, content): (id::UiId, Element<'_, Message>) = match kind {
            PaneKind::Hierarchy => (
                id::PANE_HIERARCHY,
                panels::hierarchy::view(&self.entities, self.selected, Message::SelectEntity),
            ),
            PaneKind::Assets => (id::PANE_ASSETS, panels::assetmanager::view(&self.assets)),
            PaneKind::Viewport => (
                id::PANE_VIEWPORT,
                panels::sceneviewport::view(
                    self.viewport_image.clone(),
                    Message::OrbitLeft,
                    Message::OrbitRight,
                    Message::ZoomIn,
                    Message::ZoomOut,
                    Message::ResetCamera,
                ),
            ),
            PaneKind::Inspector => (
                id::PANE_INSPECTOR,
                panels::inspector::view(
                    panels::inspector::Inspector {
                        entity: self.entities.get(self.selected),
                        name_input: &self.name_input,
                        components: &self.components,
                        component_types: &self.component_types,
                        field_inputs: &self.field_inputs,
                    },
                    Message::Inspector,
                ),
            ),
            PaneKind::Console => (id::PANE_CONSOLE, panels::console::view(&self.log)),
        };
        iced::widget::container(content).id(pane_id).into()
    }
}

#[cfg(test)]
mod tests;

#[cfg(feature = "agent")]
mod agent;

use crate::history::{self, History};
use crate::uikit::components::{menu, panel, status, toolbar};
use crate::uikit::id;
use crate::uikit::panels;
use crate::uikit::theme;
use crate::uikit::EntityView;
use crate::windows::projectloader;

use iced::widget::{column, container, pane_grid, row, text};
use iced::{mouse, Element, Length, Point};
use std::collections::HashMap;
use std::time::Instant;

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

#[derive(Debug, Clone, Copy)]
struct ViewportDrag {
    button: mouse::Button,
    last: Option<Point>,
    distance: f32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum MenuId {
    Scene,
    Edit,
    View,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum LayoutPreset {
    Default,
    Viewport,
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
    HierarchyContext(usize),
    HierarchyToggle(usize),
    HierarchyDoubleClick(usize),
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
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    SetParent {
        entity: u32,
        parent: Option<u32>,
    },
    SaveScene,
    RenderViewport,
    ViewportTick,
    ViewportPressed(mouse::Button),
    ViewportReleased(mouse::Button),
    ViewportMoved(Point),
    ViewportScrolled(mouse::ScrollDelta),
    ViewportDoubleClick,
    FocusSelection,
    ResetCamera,
    Undo,
    Redo,
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    CloseScene,
    ToggleMenu(MenuId),
    Layout(LayoutPreset),
    MaximizeViewport,
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
    viewport_started: Option<Instant>,
    viewport_latency_ms: f32,
    viewport_fps: f32,
    viewport_fps_window: Instant,
    viewport_frames: u32,
    viewport_drag: Option<ViewportDrag>,
    open_menu: Option<MenuId>,
    hierarchy_menu: Option<usize>,
    collapsed_hierarchy: std::collections::HashSet<u32>,
    camera: doppio_engine::Camera,
    history: History,
    component_types: Vec<doppio_engine::ComponentType>,
    components: Vec<doppio_engine::ComponentData>,
    field_inputs: HashMap<(usize, usize, usize), String>,
}

impl App {
    fn layout_configuration(preset: LayoutPreset) -> pane_grid::Configuration<PaneKind> {
        use pane_grid::{Axis, Configuration};
        match preset {
            LayoutPreset::Default => Configuration::Split {
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
            },
            LayoutPreset::Viewport => Configuration::Split {
                axis: Axis::Horizontal,
                ratio: 0.85,
                a: Box::new(Configuration::Split {
                    axis: Axis::Vertical,
                    ratio: 0.16,
                    a: Box::new(Configuration::Split {
                        axis: Axis::Horizontal,
                        ratio: 0.5,
                        a: Box::new(Configuration::Pane(PaneKind::Hierarchy)),
                        b: Box::new(Configuration::Pane(PaneKind::Assets)),
                    }),
                    b: Box::new(Configuration::Split {
                        axis: Axis::Horizontal,
                        ratio: 0.82,
                        a: Box::new(Configuration::Pane(PaneKind::Viewport)),
                        b: Box::new(Configuration::Pane(PaneKind::Inspector)),
                    }),
                }),
                b: Box::new(Configuration::Pane(PaneKind::Console)),
            },
        }
    }

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
        let panes =
            pane_grid::State::with_configuration(Self::layout_configuration(LayoutPreset::Default));

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
            viewport_started: None,
            viewport_latency_ms: 0.0,
            viewport_fps: 0.0,
            viewport_fps_window: Instant::now(),
            viewport_frames: 0,
            viewport_drag: None,
            open_menu: None,
            hierarchy_menu: None,
            collapsed_hierarchy: std::collections::HashSet::new(),
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

        self.collapsed_hierarchy.clear();
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
                parent: None,
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
                    iced::keyboard::Key::Character(character)
                        if character.eq_ignore_ascii_case("n") =>
                    {
                        Some(Message::AddEntity)
                    }
                    iced::keyboard::Key::Character(character)
                        if character.eq_ignore_ascii_case("s") =>
                    {
                        Some(Message::SaveScene)
                    }
                    iced::keyboard::Key::Character(character)
                        if character.eq_ignore_ascii_case("f") =>
                    {
                        Some(Message::FocusSelection)
                    }
                    iced::keyboard::Key::Character(character)
                        if character.eq_ignore_ascii_case("r") =>
                    {
                        Some(Message::RenderViewport)
                    }
                    iced::keyboard::Key::Character("0") => Some(Message::ResetCamera),
                    iced::keyboard::Key::Character("1") => {
                        Some(Message::Layout(LayoutPreset::Default))
                    }
                    iced::keyboard::Key::Character("2") => {
                        Some(Message::Layout(LayoutPreset::Viewport))
                    }
                    iced::keyboard::Key::Character("m") => Some(Message::MaximizeViewport),
                    iced::keyboard::Key::Named(iced::keyboard::key::Named::Enter) => {
                        Some(Message::TogglePlay)
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
            Some(Ok(())) => {
                self.viewport_pending = true;
                self.viewport_started = Some(Instant::now());
            }
            Some(Err(_)) => self.log_render_failure(),
            None => {}
        }
    }

    fn orbit_camera(&mut self, dx: f32, dy: f32) {
        self.camera.yaw += dx * 0.008;
        self.camera.pitch = (self.camera.pitch + dy * 0.008).clamp(-1.45, 1.45);
        self.apply_camera();
    }

    fn pan_camera(&mut self, dx: f32, dy: f32) {
        let (sin_yaw, cos_yaw) = self.camera.yaw.sin_cos();
        let (sin_pitch, cos_pitch) = self.camera.pitch.sin_cos();
        let right = [-cos_yaw, 0.0, -sin_yaw];
        let forward = [-sin_yaw * cos_pitch, -sin_pitch, cos_yaw * cos_pitch];
        let up = [
            right[1] * forward[2] - right[2] * forward[1],
            right[2] * forward[0] - right[0] * forward[2],
            right[0] * forward[1] - right[1] * forward[0],
        ];
        let scale = self.camera.distance * 0.0018;
        for axis in 0..3 {
            self.camera.focus[axis] += right[axis] * (-dx * scale) + up[axis] * (dy * scale);
        }
        self.apply_camera();
    }

    fn zoom_camera(&mut self, lines: f32) {
        self.camera.distance = (self.camera.distance * 0.9f32.powf(lines)).clamp(0.5, 5000.0);
        self.apply_camera();
    }

    fn focus_selection(&mut self) {
        let target = self
            .entities
            .get(self.selected)
            .map(|entity| (entity.position, entity.name.clone()));
        if let Some((position, name)) = target {
            self.camera.focus = position;
            self.camera.distance = (self.camera.distance * 0.5).clamp(1.0, 40.0);
            self.apply_camera();
            self.log.push(format!("Focused {name}"));
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
                if let Some(started) = self.viewport_started.take() {
                    self.viewport_latency_ms = started.elapsed().as_secs_f32() * 1000.0;
                }
                self.viewport_frames += 1;
                let elapsed = self.viewport_fps_window.elapsed().as_secs_f32();
                if elapsed >= 1.0 {
                    self.viewport_fps = self.viewport_frames as f32 / elapsed;
                    self.viewport_frames = 0;
                    self.viewport_fps_window = Instant::now();
                }
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
        if !matches!(
            &message,
            Message::ToggleMenu(_) | Message::HierarchyContext(_) | Message::ViewportTick
        ) {
            self.open_menu = None;
            self.hierarchy_menu = None;
        }
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
            Message::HierarchyContext(index) => {
                if index < self.entities.len() {
                    self.select_index(index);
                    self.hierarchy_menu = Some(index);
                }
            }
            Message::HierarchyDoubleClick(index) => {
                self.select_index(index);
                self.focus_selection();
            }
            Message::HierarchyToggle(index) => {
                if let Some(entity) = self.entities.get(index) {
                    let id = entity.id;
                    if !self.collapsed_hierarchy.remove(&id) {
                        self.collapsed_hierarchy.insert(id);
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
            Message::SetParent { entity, parent } => {
                let result = self
                    .engine
                    .as_mut()
                    .map(|engine| engine.set_entity_parent(entity, parent));
                match result {
                    Some(Ok(())) => {
                        if let Some(view) = self.entities.iter_mut().find(|view| view.id == entity)
                        {
                            view.parent = parent;
                        }
                        eprintln!(
                            "parent set entity={} parent={:?} view={:?}",
                            entity,
                            parent,
                            self.entities
                                .iter()
                                .find(|view| view.id == entity)
                                .map(|view| view.parent)
                        );
                        self.log.push("Changed parent".to_owned());
                        self.commit("Change Parent", None);
                    }
                    Some(Err(error)) => {
                        self.log.push(format!("Parent change failed: {error:?}"));
                    }
                    None => {}
                }
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
            Message::ViewportPressed(button) => {
                self.viewport_drag = Some(ViewportDrag {
                    button,
                    last: None,
                    distance: 0.0,
                });
            }
            Message::ViewportReleased(button) => {
                if let Some(drag) = self.viewport_drag.take() {
                    if button == mouse::Button::Right && drag.distance < 4.0 {
                        self.focus_selection();
                    }
                }
            }
            Message::ViewportMoved(point) => {
                if let Some((button, Some(last))) = self
                    .viewport_drag
                    .as_ref()
                    .map(|drag| (drag.button, drag.last))
                {
                    let dx = point.x - last.x;
                    let dy = point.y - last.y;
                    match button {
                        mouse::Button::Left => self.orbit_camera(dx, dy),
                        mouse::Button::Right | mouse::Button::Middle => self.pan_camera(dx, dy),
                        _ => {}
                    }
                    if let Some(drag) = self.viewport_drag.as_mut() {
                        drag.distance += dx.abs() + dy.abs();
                    }
                }
                if let Some(drag) = self.viewport_drag.as_mut() {
                    drag.last = Some(point);
                }
            }
            Message::ViewportScrolled(delta) => {
                let lines = match delta {
                    mouse::ScrollDelta::Lines { y, .. } => y,
                    mouse::ScrollDelta::Pixels { y, .. } => y / 40.0,
                };
                self.zoom_camera(lines);
            }
            Message::ViewportDoubleClick | Message::FocusSelection => {
                self.focus_selection();
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
            Message::ToggleMenu(menu) => {
                self.open_menu = if self.open_menu == Some(menu) {
                    None
                } else {
                    Some(menu)
                };
            }
            Message::Layout(preset) => {
                self.panes =
                    pane_grid::State::with_configuration(Self::layout_configuration(preset));
            }
            Message::MaximizeViewport => {
                let viewport = self
                    .panes
                    .iter()
                    .find(|(_, kind)| **kind == PaneKind::Viewport)
                    .map(|(pane, _)| *pane);
                if let Some(pane) = viewport {
                    if self.panes.maximized().is_some() {
                        self.panes.restore();
                    }
                    self.panes.maximize(pane);
                }
            }
        }
    }

    fn view(&self) -> Element<'_, Message> {
        let brand = column![
            text("Doppio").size(18).color(theme::TEXT),
            text("Caffeine engine").size(10).color(theme::TEXT_MUTED),
        ]
        .spacing(0);

        let scene_menu = menu::view(
            id::MENU_SCENE,
            "Scene",
            self.open_menu == Some(MenuId::Scene),
            vec![
                menu::Item {
                    id: id::ADD_ENTITY,
                    label: "Add Entity",
                    shortcut: Some("Ctrl+N"),
                    message: Message::AddEntity,
                },
                menu::Item {
                    id: id::SAVE_SCENE,
                    label: "Save Scene",
                    shortcut: Some("Ctrl+S"),
                    message: Message::SaveScene,
                },
                menu::Item {
                    id: id::TOGGLE_PLAY,
                    label: if self.playing { "Pause" } else { "Play" },
                    shortcut: Some("Ctrl+Enter"),
                    message: Message::TogglePlay,
                },
            ],
            Message::ToggleMenu(MenuId::Scene),
        );
        let edit_menu = menu::view(
            id::MENU_EDIT,
            "Edit",
            self.open_menu == Some(MenuId::Edit),
            vec![
                menu::Item {
                    id: id::UNDO,
                    label: "Undo",
                    shortcut: Some("Ctrl+Z"),
                    message: Message::Undo,
                },
                menu::Item {
                    id: id::REDO,
                    label: "Redo",
                    shortcut: Some("Ctrl+Shift+Z"),
                    message: Message::Redo,
                },
                menu::Item {
                    id: id::DELETE_ENTITY,
                    label: "Delete Selected",
                    shortcut: None,
                    message: Message::Inspector(panels::inspector::Edit::Delete),
                },
            ],
            Message::ToggleMenu(MenuId::Edit),
        );
        let view_menu = menu::view(
            id::MENU_VIEW,
            "View",
            self.open_menu == Some(MenuId::View),
            vec![
                menu::Item {
                    id: id::FOCUS_SELECTION,
                    label: "Focus Selection",
                    shortcut: Some("Ctrl+F"),
                    message: Message::FocusSelection,
                },
                menu::Item {
                    id: id::RESET_CAMERA,
                    label: "Reset Camera",
                    shortcut: Some("Ctrl+0"),
                    message: Message::ResetCamera,
                },
                menu::Item {
                    id: id::RENDER_VIEWPORT,
                    label: "Render Now",
                    shortcut: Some("Ctrl+R"),
                    message: Message::RenderViewport,
                },
                menu::Item {
                    id: id::LAYOUT_DEFAULT,
                    label: "Default Layout",
                    shortcut: Some("Ctrl+1"),
                    message: Message::Layout(LayoutPreset::Default),
                },
                menu::Item {
                    id: id::LAYOUT_VIEWPORT,
                    label: "Viewport Layout",
                    shortcut: Some("Ctrl+2"),
                    message: Message::Layout(LayoutPreset::Viewport),
                },
                menu::Item {
                    id: id::LAYOUT_MAXIMIZE_VIEWPORT,
                    label: "Maximize Viewport",
                    shortcut: Some("Ctrl+M"),
                    message: Message::MaximizeViewport,
                },
            ],
            Message::ToggleMenu(MenuId::View),
        );
        let menu_group = toolbar::group(vec![scene_menu, edit_menu, view_menu]);

        let engine_status = if self.engine.is_some() {
            status::chip("C++ engine", theme::ChipTone::Accent)
        } else {
            status::chip("fixtures", theme::ChipTone::Neutral)
        };
        let dirty = self
            .engine
            .as_ref()
            .map(|engine| engine.is_dirty())
            .unwrap_or(false);
        let save_status = if dirty {
            status::chip("Unsaved", theme::ChipTone::Warning)
        } else {
            status::chip("Saved", theme::ChipTone::Success)
        };
        let entity_status = status::chip(
            format!("{} entities", self.entities.len()),
            theme::ChipTone::Neutral,
        );
        let play_status = if self.playing {
            status::chip("Playing", theme::ChipTone::Success)
        } else {
            status::chip("Stopped", theme::ChipTone::Neutral)
        };
        let status_row = row![engine_status, save_status, play_status, entity_status].spacing(6);
        let bar = toolbar::view(brand.into(), vec![menu_group], status_row.into());

        let grid = pane_grid(&self.panes, |pane, kind, is_maximized| {
            self.pane_content(pane, *kind, is_maximized)
        })
        .on_drag(Message::PaneDragged)
        .on_resize(8, Message::PaneResized)
        .spacing(6);

        container(column![bar, grid].spacing(8).padding(8))
            .width(Length::Fill)
            .height(Length::Fill)
            .style(theme::background)
            .into()
    }

    fn pane_content<'a>(
        &'a self,
        _pane: pane_grid::Pane,
        kind: PaneKind,
        _maximized: bool,
    ) -> pane_grid::Content<'a, Message> {
        let (pane_id, title, subtitle, body) = self.pane_parts(kind);
        let title_bar = pane_grid::TitleBar::new(panel::title(title, subtitle))
            .padding([0, 2])
            .style(theme::panel_title);
        pane_grid::Content::new(container(panel::body(body)).id(pane_id)).title_bar(title_bar)
    }

    fn pane_parts(
        &self,
        kind: PaneKind,
    ) -> (id::UiId, &'static str, Option<String>, Element<'_, Message>) {
        let (pane_id, title, subtitle, body): (
            id::UiId,
            &str,
            Option<String>,
            Element<'_, Message>,
        ) = match kind {
            PaneKind::Hierarchy => (
                id::PANE_HIERARCHY,
                "Hierarchy",
                Some(format!("{} entities", self.entities.len())),
                panels::hierarchy::view(
                    panels::hierarchy::View {
                        entities: &self.entities,
                        selected: self.selected,
                        collapsed: &self.collapsed_hierarchy,
                        menu_for: self.hierarchy_menu,
                    },
                    panels::hierarchy::Callbacks {
                        on_toggle: Box::new(Message::HierarchyToggle),
                        on_select: Box::new(Message::SelectEntity),
                        on_context: Box::new(Message::HierarchyContext),
                        on_double_click: Box::new(Message::HierarchyDoubleClick),
                        on_action: Box::new(|action| match action {
                            panels::hierarchy::Action::AddEntity => Message::AddEntity,
                            panels::hierarchy::Action::Focus => Message::FocusSelection,
                            panels::hierarchy::Action::Delete => {
                                Message::Inspector(panels::inspector::Edit::Delete)
                            }
                        }),
                    },
                ),
            ),
            PaneKind::Assets => {
                let files = self
                    .assets
                    .iter()
                    .filter(|asset| !asset.is_directory)
                    .count();
                let subtitle = if files > 60 {
                    format!("{files} files · showing 60")
                } else {
                    format!("{files} files")
                };
                (
                    id::PANE_ASSETS,
                    "Assets",
                    Some(subtitle),
                    panels::assetmanager::view(&self.assets),
                )
            }
            PaneKind::Viewport => {
                let scene = self
                    .engine
                    .as_ref()
                    .and_then(|engine| engine.scene_path())
                    .and_then(|path| path.rsplit(['/', '\\']).next().map(str::to_owned))
                    .unwrap_or_else(|| "No scene".to_owned());
                let status_line = format!(
                    "{}×{} · {:.0} ms · {:.0} FPS · yaw {:.2} · pitch {:.2} · dist {:.1}",
                    VIEWPORT_WIDTH,
                    VIEWPORT_HEIGHT,
                    self.viewport_latency_ms,
                    self.viewport_fps,
                    self.camera.yaw,
                    self.camera.pitch,
                    self.camera.distance
                );
                let hint = self
                    .entities
                    .get(self.selected)
                    .map(|entity| format!("Selected: {} · Double-click to focus", entity.name))
                    .unwrap_or_else(|| "Double-click to focus the selection".to_owned());
                let state = panels::sceneviewport::ViewportState {
                    image: self.viewport_image.clone(),
                    status: status_line,
                    hint,
                    empty: self.engine.is_some() && self.entities.is_empty(),
                    dragging: self.viewport_drag.is_some(),
                    camera: &self.camera,
                };
                let callbacks = panels::sceneviewport::ViewportCallbacks {
                    surface_left_press: Message::ViewportPressed(mouse::Button::Left),
                    surface_right_press: Message::ViewportPressed(mouse::Button::Right),
                    surface_left_release: Message::ViewportReleased(mouse::Button::Left),
                    surface_right_release: Message::ViewportReleased(mouse::Button::Right),
                    surface_move: Box::new(Message::ViewportMoved),
                    surface_scroll: Box::new(Message::ViewportScrolled),
                    surface_double_click: Message::ViewportDoubleClick,
                };
                (
                    id::PANE_VIEWPORT,
                    "Viewport",
                    Some(scene),
                    panels::sceneviewport::view(state, callbacks),
                )
            }
            PaneKind::Inspector => {
                let selected = self
                    .entities
                    .get(self.selected)
                    .map(|entity| format!("ID {}", entity.id))
                    .unwrap_or_else(|| "No selection".to_owned());
                (
                    id::PANE_INSPECTOR,
                    "Inspector",
                    Some(selected),
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
                )
            }
            PaneKind::Console => (
                id::PANE_CONSOLE,
                "Console",
                Some(format!("{} lines", self.log.len())),
                panels::console::view(&self.log),
            ),
        };

        (pane_id, title, subtitle, body)
    }
}

#[cfg(test)]
mod tests;

#[cfg(feature = "agent")]
mod agent;

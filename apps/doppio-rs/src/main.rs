use iced::widget::{
    button, column, container, pane_grid, row, scrollable, space, text, text_input,
};
use iced::{Element, Length};

mod engine;

#[cfg(feature = "agent")]
fn main() -> iced::Result {
    iced_mcp::run(iced::application(App::new, App::update, App::view))
}

#[cfg(not(feature = "agent"))]
fn main() -> iced::Result {
    iced::application(App::new, App::update, App::view).run()
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PaneKind {
    Hierarchy,
    Inspector,
    Console,
    Viewport,
}

#[derive(Debug, Clone)]
struct Entity {
    id: u32,
    name: String,
    position: [f32; 3],
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
    Log,
}

struct App {
    panes: pane_grid::State<PaneKind>,
    engine: Option<engine::Engine>,
    entities: Vec<Entity>,
    selected: usize,
    name_input: String,
    log: Vec<String>,
    playing: bool,
    next_entity_id: u32,
}

impl App {
    fn new() -> Self {
        use pane_grid::{Axis, Configuration};

        // Left column: hierarchy over console. Right column: viewport beside
        // inspector. This mirrors the Doppio dock layout closely enough to
        // validate PaneGrid + ids + tests.
        let configuration = Configuration::Split {
            axis: Axis::Vertical,
            ratio: 0.22,
            a: Box::new(Configuration::Split {
                axis: Axis::Horizontal,
                ratio: 0.70,
                a: Box::new(Configuration::Pane(PaneKind::Hierarchy)),
                b: Box::new(Configuration::Pane(PaneKind::Console)),
            }),
            b: Box::new(Configuration::Split {
                axis: Axis::Vertical,
                ratio: 0.72,
                a: Box::new(Configuration::Pane(PaneKind::Viewport)),
                b: Box::new(Configuration::Pane(PaneKind::Inspector)),
            }),
        };
        let panes = pane_grid::State::with_configuration(configuration);

        let mut engine = engine::Engine::load_default();
        let mut log = vec!["Iced PoC ready".to_owned()];
        let mut entities = Vec::new();
        let mut next_entity_id = 0u32;
        let mut scene_loaded = false;

        if let Some(engine) = engine.as_mut() {
            log.push("Connected to libcaffeine-capi".to_owned());
            if let Some(path) = std::env::args().nth(1) {
                match engine.load_scene(&path) {
                    Ok(()) => {
                        scene_loaded = true;
                        log.push(format!("Loaded {path}"));
                    }
                    Err(_) => log.push(format!("Could not load {path}")),
                }
            }

            if scene_loaded {
                let count = engine.entity_count();
                for index in 0..count {
                    if let Ok(info) = engine.entity_at(index) {
                        let position = engine.position(info.id).unwrap_or([0.0; 3]);
                        entities.push(Entity {
                            id: info.id,
                            name: info.name,
                            position,
                        });
                        next_entity_id = next_entity_id.max(info.id.saturating_add(1));
                    }
                }
            } else {
                for name in ["Entity 1", "Entity 2"] {
                    if let Ok(id) = engine.create_entity(name) {
                        entities.push(Entity {
                            id,
                            name: name.to_owned(),
                            position: [0.0; 3],
                        });
                        next_entity_id = next_entity_id.max(id.saturating_add(1));
                    }
                }
            }
        } else {
            log.push("C ABI library not found; using fake data".to_owned());
            for name in ["Entity 1", "Entity 2"] {
                entities.push(Entity {
                    id: next_entity_id,
                    name: name.to_owned(),
                    position: [0.0; 3],
                });
                next_entity_id += 1;
            }
        }

        let name_input = entities
            .first()
            .map(|entity| entity.name.clone())
            .unwrap_or_default();

        Self {
            panes,
            engine,
            entities,
            selected: 0,
            name_input,
            log,
            playing: false,
            next_entity_id,
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
                    engine.create_entity(&name).unwrap_or(engine::ENTITY_NONE)
                } else {
                    let id = self.next_entity_id;
                    self.next_entity_id += 1;
                    id
                };
                if id != engine::ENTITY_NONE {
                    self.entities.push(Entity {
                        id,
                        name: name.clone(),
                        position: [0.0; 3],
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
            Message::Log => {
                self.log.push("Hello from the Rust UI".to_owned());
            }
        }
    }

    fn view(&self) -> Element<'_, Message> {
        let toolbar = row![
            text("Doppio RS").size(20),
            space::horizontal(),
            container(
                button(if self.playing { "Pause" } else { "Play" }).on_press(Message::TogglePlay)
            )
            .id("toggle-play"),
            container(button("Add Entity").on_press(Message::AddEntity)).id("add-entity"),
            container(button("Log").on_press(Message::Log)).id("log"),
            text(if self.engine.is_some() {
                "C++ engine"
            } else {
                "fake data"
            })
            .size(12),
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
                let mut list = column![text("Hierarchy").size(16)].spacing(4);
                for (index, entity) in self.entities.iter().enumerate() {
                    let label = format!("{}  #{}", entity.name, entity.id);
                    let mut item = button(text(label)).width(Length::Fill);
                    if index == self.selected {
                        item = item.style(iced::widget::button::primary);
                    }
                    list = list.push(
                        container(item.on_press(Message::SelectEntity(index)))
                            .id(format!("entity-{index}")),
                    );
                }
                container(scrollable(list)).padding(8).into()
            }
            PaneKind::Inspector => {
                let mut content = column![text("Inspector").size(16)].spacing(8);
                if let Some(entity) = self.entities.get(self.selected) {
                    content = content.push(text(format!("Entity: {}", entity.name)));
                    content = content.push(
                        text_input("Name", &self.name_input)
                            .on_input(Message::NameChanged)
                            .id("entity-name"),
                    );
                    content = content.push(text(format!("Entity ID: {}", entity.id)));
                    content = content.push(text(format!(
                        "Position: {:.2}, {:.2}, {:.2}",
                        entity.position[0], entity.position[1], entity.position[2]
                    )));
                    content = content
                        .push(container(button("+X").on_press(Message::NudgeX)).id("nudge-x"));
                    content = content.push(
                        container(button("Delete").on_press(Message::DeleteSelected))
                            .id("delete-entity"),
                    );
                } else {
                    content = content.push(text("No entity selected"));
                }
                container(content).padding(8).into()
            }
            PaneKind::Console => {
                let mut content = column![text("Console").size(16)].spacing(2);
                for line in &self.log {
                    content = content.push(text(line));
                }
                container(scrollable(content)).padding(8).into()
            }
            PaneKind::Viewport => container(
                column![
                    text("Viewport").size(16),
                    text("The caffeine render texture will be embedded here."),
                ]
                .spacing(8),
            )
            .padding(8)
            .width(Length::Fill)
            .height(Length::Fill)
            .id("viewport-pane")
            .into(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use iced_test::simulator;

    #[test]
    fn add_entity_updates_hierarchy_and_inspector() {
        let _guard = engine::test_lock();
        let mut app = App::new();
        let before = app.entities.len();

        let mut ui = simulator(app.view());
        let _ = ui.click("Add Entity");
        for message in ui.into_messages() {
            app.update(message);
        }

        assert_eq!(app.entities.len(), before + 1);
        assert_eq!(app.selected, before);

        let mut ui = simulator(app.view());
        assert!(ui.find("Entity 3").is_ok(), "new entity should be listed");
    }

    #[test]
    fn toggle_play_updates_label() {
        let _guard = engine::test_lock();
        let mut app = App::new();

        let mut ui = simulator(app.view());
        let _ = ui.click("Play");
        for message in ui.into_messages() {
            app.update(message);
        }
        assert!(app.playing);

        let mut ui = simulator(app.view());
        assert!(ui.find("Pause").is_ok(), "button should read Pause");
    }

    #[test]
    fn nudge_x_updates_position() {
        let _guard = engine::test_lock();
        let mut app = App::new();
        let before = app.entities[0].position[0];

        let mut ui = simulator(app.view());
        let _ = ui.click(iced::widget::Id::new("nudge-x"));
        for message in ui.into_messages() {
            app.update(message);
        }

        assert!(app.entities[0].position[0] > before);
    }

    #[test]
    fn rename_updates_selected_entity() {
        let _guard = engine::test_lock();
        let mut app = App::new();

        let mut ui = simulator(app.view());
        let clicked = ui.click(iced::widget::Id::new("entity-1"));
        assert!(clicked.is_ok(), "entity button should be clickable");
        for message in ui.into_messages() {
            app.update(message);
        }
        assert_eq!(app.selected, 1);

        let mut ui = simulator(app.view());
        let focused = ui.click(iced::widget::Id::new("entity-name"));
        assert!(focused.is_ok(), "name field should be focusable");
        let _ = ui.typewrite("Boss");
        for message in ui.into_messages() {
            app.update(message);
        }

        assert!(app.entities[1].name.contains("Boss"));
    }
}

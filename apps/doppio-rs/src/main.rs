use iced::widget::{
    button, column, container, pane_grid, row, scrollable, space, text, text_input,
};
use iced::{Element, Length};

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
    name: String,
    x: f32,
}

#[derive(Debug, Clone)]
enum Message {
    PaneDragged(pane_grid::DragEvent),
    PaneResized(pane_grid::ResizeEvent),
    SelectEntity(usize),
    NameChanged(String),
    TogglePlay,
    AddEntity,
    Log,
}

struct App {
    panes: pane_grid::State<PaneKind>,
    entities: Vec<Entity>,
    selected: usize,
    name_input: String,
    log: Vec<String>,
    playing: bool,
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

        Self {
            panes,
            entities: vec![
                Entity {
                    name: "Entity 1".to_owned(),
                    x: 0.0,
                },
                Entity {
                    name: "Entity 2".to_owned(),
                    x: 5.0,
                },
            ],
            selected: 0,
            name_input: "Entity 1".to_owned(),
            log: vec!["Iced PoC ready".to_owned()],
            playing: false,
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
                    self.log.push(format!("Selected {}", entity.name));
                }
            }
            Message::NameChanged(value) => {
                self.name_input = value;
                if let Some(entity) = self.entities.get_mut(self.selected) {
                    entity.name = self.name_input.clone();
                }
            }
            Message::TogglePlay => {
                self.playing = !self.playing;
                self.log
                    .push(if self.playing { "Play" } else { "Stop" }.to_owned());
            }
            Message::AddEntity => {
                let index = self.entities.len();
                self.entities.push(Entity {
                    name: format!("Entity {}", index + 1),
                    x: index as f32,
                });
                self.selected = index;
                self.name_input = self.entities[index].name.clone();
                self.log
                    .push(format!("Added {}", self.entities[index].name));
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
                    let label = format!("{}  ({:.1})", entity.name, entity.x);
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
                    content = content.push(text(format!("Position X: {:.2}", entity.x)));
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
    fn rename_updates_selected_entity() {
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

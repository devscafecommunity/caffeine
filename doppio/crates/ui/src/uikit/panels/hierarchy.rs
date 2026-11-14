use crate::uikit::components::{list_row, menu};
use crate::uikit::id;
use crate::uikit::EntityView;
use iced::widget::{column, container, scrollable, text};
use iced::Element;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Action {
    AddEntity,
    Focus,
    Delete,
}

pub fn view<'a, Message: Clone + 'a>(
    entities: &'a [EntityView],
    selected: usize,
    menu_for: Option<usize>,
    on_select: impl Fn(usize) -> Message + 'a,
    on_context: impl Fn(usize) -> Message + 'a,
    on_double_click: impl Fn(usize) -> Message + 'a,
    on_action: impl Fn(Action) -> Message + 'a,
) -> Element<'a, Message> {
    let mut list = column![].spacing(3);
    if entities.is_empty() {
        list = list.push(text("No entities").color(crate::uikit::theme::TEXT_MUTED));
    }
    for (index, entity) in entities.iter().enumerate() {
        list = list.push(list_row::view(
            id::entity_row(index),
            format!("{}  #{}", entity.name, entity.id),
            index == selected,
            on_select(index),
            on_context(index),
            on_double_click(index),
        ));
        if menu_for == Some(index) {
            list = list.push(menu::context(vec![
                menu::Item {
                    id: id::ADD_ENTITY,
                    label: "Add Entity",
                    shortcut: Some("Ctrl+N"),
                    message: on_action(Action::AddEntity),
                },
                menu::Item {
                    id: id::FOCUS_SELECTION,
                    label: "Focus in Viewport",
                    shortcut: Some("Ctrl+F"),
                    message: on_action(Action::Focus),
                },
                menu::Item {
                    id: id::DELETE_ENTITY,
                    label: "Delete Entity",
                    shortcut: None,
                    message: on_action(Action::Delete),
                },
            ]));
        }
    }
    container(scrollable(list)).into()
}

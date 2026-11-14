use crate::uikit::theme;
use iced::widget::{button, container, text, Id};
use iced::{Element, Length};

/// Toolbar/secondary action with a stable id.
pub fn toolbar<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    message: Message,
) -> Element<'a, Message> {
    container(
        button(text(label.into()))
            .style(theme::button_secondary)
            .on_press(message),
    )
    .id(id.into())
    .into()
}

/// Primary action with a stable id.
pub fn primary<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    message: Message,
) -> Element<'a, Message> {
    container(
        button(text(label.into()))
            .style(theme::button_primary)
            .on_press(message),
    )
    .id(id.into())
    .into()
}

/// Destructive action with a stable id.
pub fn danger<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    message: Message,
) -> Element<'a, Message> {
    container(
        button(text(label.into()))
            .style(theme::button_danger)
            .on_press(message),
    )
    .id(id.into())
    .into()
}

/// Full-width selectable row used by the hierarchy/list panels.
pub fn selectable<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    selected: bool,
    message: Message,
) -> Element<'a, Message> {
    let mut item = button(text(label.into())).width(Length::Fill);
    item = item.style(if selected {
        theme::button_primary
    } else {
        theme::button_secondary
    });
    container(item.on_press(message)).id(id.into()).into()
}

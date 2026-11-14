use iced::widget::{button, container, text};
use iced::{Element, Length};

/// Toolbar/secondary action with a stable id.
pub fn toolbar<'a, Message: Clone + 'a>(
    id: impl Into<String>,
    label: impl Into<String>,
    message: Message,
) -> Element<'a, Message> {
    container(button(text(label.into())).on_press(message))
        .id(id.into())
        .into()
}

/// Primary action with a stable id.
pub fn primary<'a, Message: Clone + 'a>(
    id: impl Into<String>,
    label: impl Into<String>,
    message: Message,
) -> Element<'a, Message> {
    container(
        button(text(label.into()))
            .style(iced::widget::button::primary)
            .on_press(message),
    )
    .id(id.into())
    .into()
}

/// Destructive action with a stable id.
pub fn danger<'a, Message: Clone + 'a>(
    id: impl Into<String>,
    label: impl Into<String>,
    message: Message,
) -> Element<'a, Message> {
    container(
        button(text(label.into()))
            .style(iced::widget::button::danger)
            .on_press(message),
    )
    .id(id.into())
    .into()
}

/// Full-width selectable row used by the hierarchy/list panels.
pub fn selectable<'a, Message: Clone + 'a>(
    id: impl Into<String>,
    label: impl Into<String>,
    selected: bool,
    message: Message,
) -> Element<'a, Message> {
    let mut item = button(text(label.into())).width(Length::Fill);
    if selected {
        item = item.style(iced::widget::button::primary);
    }
    container(item.on_press(message)).id(id.into()).into()
}

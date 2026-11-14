use crate::uikit::theme;
use iced::widget::{button, container, text, Id};
use iced::Element;

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

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
        button(text(label.into()).size(12))
            .style(theme::button_secondary)
            .padding([4, 10])
            .on_press(message),
    )
    .id(id.into())
    .into()
}

/// Compact square-ish action for panel toolbars and tree headers.
pub fn compact<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    message: Message,
) -> Element<'a, Message> {
    container(
        button(text(label.into()).size(12))
            .style(theme::button_secondary)
            .padding([1, 6])
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
        button(text(label.into()).size(12))
            .style(theme::button_primary)
            .padding([4, 10])
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
        button(text(label.into()).size(12))
            .style(theme::button_danger)
            .padding([4, 10])
            .on_press(message),
    )
    .id(id.into())
    .into()
}

/// Full-width secondary action for stacked property panels.
pub fn block<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    message: Message,
) -> Element<'a, Message> {
    container(
        button(text(label.into()).size(12).width(iced::Length::Fill))
            .style(theme::button_secondary)
            .padding([4, 8])
            .width(iced::Length::Fill)
            .on_press(message),
    )
    .id(id.into())
    .width(iced::Length::Fill)
    .into()
}

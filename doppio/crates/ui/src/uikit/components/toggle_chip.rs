use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::{button, container, text};
use iced::Element;

/// A clickable filter chip (active state is visible).
pub fn view<'a, Message: Clone + 'a>(
    id: UiId,
    label: impl Into<String>,
    active: bool,
    color: iced::Color,
    message: Message,
) -> Element<'a, Message> {
    container(
        button(text(label.into()).size(10))
            .style(theme::filter_chip(active, color))
            .padding([3, 7])
            .on_press(message),
    )
    .id(id)
    .into()
}

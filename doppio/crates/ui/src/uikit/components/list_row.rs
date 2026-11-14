use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::{container, mouse_area, text};
use iced::{Element, Length};

/// A selectable list row with left click, right click and double click.
pub fn view<'a, Message: Clone + 'a>(
    id: UiId,
    label: impl Into<String>,
    selected: bool,
    on_select: Message,
    on_context: Message,
    on_double_click: Message,
) -> Element<'a, Message> {
    let row = container(text(label.into()).size(12))
        .id(id)
        .padding([5, 8])
        .width(Length::Fill)
        .style(theme::list_row(selected));
    mouse_area(row)
        .on_press(on_select)
        .on_right_press(on_context)
        .on_double_click(on_double_click)
        .interaction(iced::mouse::Interaction::Pointer)
        .into()
}

use iced::widget::{container, pick_list, row, text};
use iced::{Element, Length};
use std::fmt::Display;

/// Labelled dropdown used by property panels.
pub fn property<'a, Message, T>(
    id: impl Into<iced::widget::Id>,
    label: impl Into<String>,
    options: Vec<T>,
    selected: Option<T>,
    on_select: impl Fn(T) -> Message + 'a,
) -> Element<'a, Message>
where
    Message: Clone + 'a,
    T: Clone + PartialEq + Display + 'a,
{
    container(
        row![
            text(label.into())
                .size(10)
                .color(crate::uikit::theme::TEXT_MUTED),
            pick_list(options, selected, on_select)
                .width(Length::Fill)
                .text_size(11)
                .placeholder("Choose…"),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    )
    .id(id.into())
    .into()
}

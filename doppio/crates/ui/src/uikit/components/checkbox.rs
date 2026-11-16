use iced::widget::{checkbox, container, row, space, text};
use iced::{Element, Length};

/// Labelled checkbox row for property panels.
pub fn property<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    label: impl Into<String>,
    value: bool,
    on_toggle: impl Fn(bool) -> Message + 'a,
) -> Element<'a, Message> {
    container(
        row![
            checkbox(value).size(14).text_size(10).on_toggle(on_toggle),
            space::horizontal(),
            text(label.into())
                .size(10)
                .color(crate::uikit::theme::TEXT_MUTED),
        ]
        .spacing(5)
        .align_y(iced::Alignment::Center),
    )
    .id(id.into())
    .width(Length::Fill)
    .into()
}

use iced::widget::{row, text, text_input, Row};
use iced::{Element, Length};

/// A labelled single-line text field.
pub fn view<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    label: impl Into<String>,
    value: &str,
    on_input: impl Fn(String) -> Message + 'a,
) -> Element<'a, Message> {
    let label = label.into();
    row![
        text(label).width(Length::Fixed(96.0)),
        text_input("", value)
            .id(id)
            .width(Length::Fill)
            .on_input(on_input)
    ]
    .spacing(6)
    .align_y(iced::Alignment::Center)
    .into()
}

/// A stacked label above the field, for narrow panels.
pub fn stacked<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    label: impl Into<String>,
    value: &str,
    on_input: impl Fn(String) -> Message + 'a,
) -> Element<'a, Message> {
    let label = label.into();
    iced::widget::column![
        text(label).size(11),
        text_input("", value).id(id).on_input(on_input)
    ]
    .spacing(3)
    .into()
}

/// A full-width search field without a label.
pub fn search<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    placeholder: impl Into<String>,
    value: &str,
    on_input: impl Fn(String) -> Message + 'a,
) -> Element<'a, Message> {
    let placeholder = placeholder.into();
    text_input(placeholder.as_str(), value)
        .id(id)
        .width(Length::Fill)
        .on_input(on_input)
        .into()
}

/// A compact field with a short axis label, for vector components.
pub fn compact<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    label: &'static str,
    value: &str,
    on_input: impl Fn(String) -> Message + 'a,
) -> Element<'a, Message> {
    Row::new()
        .push(text(label).size(12))
        .push(
            text_input("", value)
                .id(id)
                .width(Length::Fixed(56.0))
                .on_input(on_input),
        )
        .spacing(4)
        .align_y(iced::Alignment::Center)
        .into()
}

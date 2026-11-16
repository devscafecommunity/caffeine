use crate::uikit::theme;
use iced::widget::{container, row, text, text_input, Row};
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
            .style(theme::input)
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
        text_input("", value)
            .id(id)
            .on_input(on_input)
            .style(theme::input)
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
        .size(12)
        .padding([4, 7])
        .on_input(on_input)
        .style(theme::input)
        .into()
}

/// Small field used by vector rows: coloured axis label inline with the value.
pub fn axis<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    label: &'static str,
    color: iced::Color,
    value: &str,
    on_input: impl Fn(String) -> Message + 'a,
) -> Element<'a, Message> {
    row![
        text(label).size(9).color(color).width(Length::Fixed(9.0)),
        text_input("", value)
            .id(id)
            .size(11)
            .padding([2, 4])
            .width(Length::Fill)
            .on_input(on_input)
            .style(theme::input),
    ]
    .spacing(3)
    .width(Length::Fill)
    .align_y(iced::Alignment::Center)
    .into()
}

/// A compact labelled text field: label left, value right.
pub fn property<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    label: impl Into<String>,
    value: &str,
    on_input: impl Fn(String) -> Message + 'a,
) -> Element<'a, Message> {
    row![
        container(text(label.into()).size(10).color(theme::TEXT_MUTED)).width(Length::Fixed(74.0)),
        text_input("", value)
            .id(id)
            .size(11)
            .padding([2, 5])
            .width(Length::Fill)
            .on_input(on_input)
            .style(theme::input),
    ]
    .spacing(5)
    .align_y(iced::Alignment::Center)
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
        .push(text(label).size(11))
        .push(
            text_input("", value)
                .id(id)
                .size(11)
                .padding([2, 4])
                .width(Length::Fixed(56.0))
                .on_input(on_input)
                .style(theme::input),
        )
        .spacing(4)
        .align_y(iced::Alignment::Center)
        .into()
}

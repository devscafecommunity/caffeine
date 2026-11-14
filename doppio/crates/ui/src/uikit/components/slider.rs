use iced::widget::{container, row, slider, text, Id};
use iced::Element;

use std::ops::RangeInclusive;

/// Labelled slider with its current value. Kept generic over the message.
pub fn labeled<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    range: RangeInclusive<f32>,
    value: f32,
    on_change: impl Fn(f32) -> Message + 'a,
) -> Element<'a, Message> {
    container(
        row![
            text(label.into()).width(80.0),
            slider(range, value, on_change),
            text(format!("{value:.2}")).size(10).width(42.0),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    )
    .id(id.into())
    .into()
}

/// Compact slider for colour channels in narrow inspectors.
pub fn compact<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    range: RangeInclusive<f32>,
    value: f32,
    on_change: impl Fn(f32) -> Message + 'a,
) -> Element<'a, Message> {
    container(
        row![
            text(label.into()).size(10).width(12.0),
            slider(range, value, on_change),
            text(format!("{value:.2}")).size(9).width(30.0),
        ]
        .spacing(4)
        .align_y(iced::Alignment::Center),
    )
    .id(id.into())
    .into()
}

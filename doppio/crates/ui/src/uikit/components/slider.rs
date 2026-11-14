use iced::widget::{row, slider, text};
use iced::Element;

use std::ops::RangeInclusive;

/// Labelled slider. Kept generic over the message so panels can compose it.
pub fn labeled<'a, Message: Clone + 'a>(
    label: impl Into<String>,
    range: RangeInclusive<f32>,
    value: f32,
    on_change: impl Fn(f32) -> Message + 'a,
) -> Element<'a, Message> {
    row![text(label.into()), slider(range, value, on_change)]
        .spacing(8)
        .into()
}

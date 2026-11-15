use iced::widget::{container, row, slider, text, Id};
use iced::Element;

use std::ops::RangeInclusive;

fn default_step(range: &RangeInclusive<f32>) -> f32 {
    ((*range.end() - *range.start()) / 1000.0).clamp(0.0001, 1.0)
}

fn format_value(value: f32) -> String {
    let text = format!("{value:.3}");
    let text = text.trim_end_matches('0').trim_end_matches('.');
    if text.is_empty() {
        "0".to_owned()
    } else {
        text.to_owned()
    }
}

/// Labelled slider with its current value. Kept generic over the message.
pub fn labeled<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    range: RangeInclusive<f32>,
    value: f32,
    on_change: impl Fn(f32) -> Message + 'a,
) -> Element<'a, Message> {
    let step = default_step(&range);
    container(
        row![
            text(label.into()).width(80.0),
            slider(range, value, on_change)
                .step(step)
                .shift_step(step * 0.1),
            text(format_value(value)).size(10).width(42.0),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    )
    .id(id.into())
    .into()
}

/// Dense inspector slider: label on the left, slider in the middle, value on
/// the right, so a property row stays one line tall.
pub fn property<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    range: RangeInclusive<f32>,
    value: f32,
    on_change: impl Fn(f32) -> Message + 'a,
) -> Element<'a, Message> {
    let step = default_step(&range);
    property_step(id, label, range, value, step, on_change)
}

/// Slider with an explicit step, used when the component schema publishes one.
pub fn property_step<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    label: impl Into<String>,
    range: RangeInclusive<f32>,
    value: f32,
    step: f32,
    on_change: impl Fn(f32) -> Message + 'a,
) -> Element<'a, Message> {
    let step = step.max(0.0001);
    container(
        row![
            text(label.into())
                .size(9)
                .color(crate::uikit::theme::TEXT_MUTED)
                .width(iced::Length::Fixed(74.0)),
            slider(range, value, on_change)
                .step(step)
                .shift_step(step * 0.1)
                .width(iced::Length::Fill)
                .height(14),
            text(format_value(value))
                .size(9)
                .width(iced::Length::Fixed(42.0)),
        ]
        .spacing(5)
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
    let step = default_step(&range);
    container(
        row![
            text(label.into()).size(9).width(10.0),
            slider(range, value, on_change)
                .step(step)
                .shift_step(step * 0.1),
            text(format_value(value)).size(9).width(34.0),
        ]
        .spacing(4)
        .align_y(iced::Alignment::Center),
    )
    .id(id.into())
    .into()
}

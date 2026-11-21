use crate::uikit::theme::{self, ChipTone};
use iced::widget::{container, text, text as text_widget};
use iced::Element;

pub fn chip<'a, Message: 'a>(label: impl Into<String>, tone: ChipTone) -> Element<'a, Message> {
    let label = label.into();
    container(text(label).size(10))
        .padding([2, 7])
        .style(theme::chip(tone))
        .into()
}

/// How serious a status line is, from what it says: failures are danger, "placed with problems" and a missing
/// project are warnings, anything else is a success.
pub fn tone_for_text(text: &str) -> ChipTone {
    let lower = text.to_lowercase();
    if [
        "could not",
        "error",
        "failed",
        "not available",
        "not a terrain",
        "no terrain",
        "scatter:",
    ]
    .iter()
    .any(|needle| lower.contains(needle))
    {
        ChipTone::Danger
    } else if [
        "problem",
        "open a project",
        "only the first",
        "did not fit",
        "graph:",
    ]
    .iter()
    .any(|needle| lower.contains(needle))
    {
        ChipTone::Warning
    } else {
        ChipTone::Success
    }
}

/// A generator's last message under its panel: wraps, coloured by how it went.
pub fn note<'a, Message: 'a>(text: &'a str) -> Element<'a, Message> {
    container(text_widget(text).size(10))
        .width(iced::Length::Fill)
        .padding([4, 7])
        .style(theme::chip(tone_for_text(text)))
        .into()
}

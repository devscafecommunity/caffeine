use crate::uikit::theme;
use iced::widget::{column, container, progress_bar, row, space, text, Id};
use iced::{Element, Length};

/// Determinate progress bar with a stable id.
pub fn bar<'a, Message: 'a>(id: impl Into<Id>, fraction: f32) -> Element<'a, Message> {
    container(
        progress_bar(0.0..=1.0, fraction.clamp(0.0, 1.0))
            .style(theme::progress)
            .length(Length::Fill)
            .girth(6.0),
    )
    .id(id.into())
    .width(Length::Fill)
    .into()
}

/// Progress bar plus a caption row used by long-running cooks.
pub fn labelled<'a, Message: 'a>(
    id: impl Into<Id>,
    fraction: f32,
    label: impl Into<String>,
) -> Element<'a, Message> {
    column![
        row![
            text(label.into()).size(9).color(theme::TEXT_MUTED),
            space().width(Length::Fill),
            text(format!("{:.0}%", fraction.clamp(0.0, 1.0) * 100.0))
                .size(9)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(4),
        bar(id, fraction),
    ]
    .spacing(3)
    .into()
}

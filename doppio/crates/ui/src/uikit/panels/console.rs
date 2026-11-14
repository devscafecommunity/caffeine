use crate::uikit::components::toggle_chip;
use crate::uikit::id;
use crate::uikit::theme;
use crate::windows::log::{LogEntry, LogLevel};
use iced::widget::{column, container, row, scrollable, space, text};
use iced::{Element, Length};
use std::collections::HashSet;

fn level_color(level: LogLevel) -> iced::Color {
    match level {
        LogLevel::Error => theme::DANGER,
        LogLevel::Log => theme::TEXT_MUTED,
        LogLevel::Action => theme::ACCENT,
        LogLevel::Engine => theme::SUCCESS,
        LogLevel::Ui => theme::TEXT,
    }
}

fn filter_id(level: LogLevel) -> id::UiId {
    match level {
        LogLevel::Error => id::CONSOLE_FILTER_ERROR,
        LogLevel::Log => id::CONSOLE_FILTER_LOG,
        LogLevel::Action => id::CONSOLE_FILTER_ACTION,
        LogLevel::Engine => id::CONSOLE_FILTER_ENGINE,
        LogLevel::Ui => id::CONSOLE_FILTER_UI,
    }
}

pub fn view<'a, Message: Clone + 'a>(
    lines: &'a [LogEntry],
    filters: &'a HashSet<LogLevel>,
    on_toggle: impl Fn(LogLevel) -> Message + 'a,
) -> Element<'a, Message> {
    let mut chips = row![].spacing(4);
    for level in LogLevel::ALL {
        chips = chips.push(toggle_chip::view(
            filter_id(level),
            level.label(),
            filters.contains(&level),
            level_color(level),
            on_toggle(level),
        ));
    }

    let mut content = column![].spacing(1);
    let mut shown = 0usize;
    for entry in lines.iter().filter(|entry| filters.contains(&entry.level)) {
        shown += 1;
        content = content.push(
            row![
                text(entry.level.prefix())
                    .size(9)
                    .width(Length::Fixed(26.0))
                    .color(level_color(entry.level)),
                text(entry.text.as_str())
                    .size(11)
                    .color(level_color(entry.level)),
            ]
            .spacing(6),
        );
    }
    if shown == 0 {
        content = content.push(
            text("No entries for these filters")
                .size(11)
                .color(theme::TEXT_MUTED),
        );
    }

    container(column![
        chips,
        space().height(6),
        scrollable(content).anchor_bottom()
    ])
    .into()
}

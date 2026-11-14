use crate::uikit::theme;
use iced::widget::{column, container, row, text};
use iced::{Element, Length};

pub struct Panel<'a, Message> {
    pub title: &'a str,
    pub subtitle: Option<String>,
    pub body: Element<'a, Message>,
}

/// Panel title row used inside a `PaneGrid` title bar.
pub fn title<'a, Message: 'a>(label: &'a str, subtitle: Option<String>) -> Element<'a, Message> {
    let mut header = row![text(label).size(13).color(theme::TEXT)]
        .spacing(6)
        .align_y(iced::Alignment::Center);
    if let Some(subtitle) = subtitle {
        header = header.push(text(subtitle).size(10).color(theme::TEXT_MUTED));
    }
    container(header)
        .padding([2, 6])
        .style(theme::panel_title)
        .into()
}

/// Panel body (the framed content below the title bar).
pub fn body<'a, Message: 'a>(content: Element<'a, Message>) -> Element<'a, Message> {
    container(content)
        .padding(8)
        .width(Length::Fill)
        .height(Length::Fill)
        .style(theme::panel)
        .into()
}

/// Standalone panel (title + body) for non-pane surfaces.
pub fn view<'a, Message: 'a>(panel: Panel<'a, Message>) -> Element<'a, Message> {
    column![title(panel.title, panel.subtitle), body(panel.body)]
        .spacing(2)
        .into()
}

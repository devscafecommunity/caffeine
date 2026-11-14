use crate::uikit::theme;
use iced::widget::{column, container, row, space, text};
use iced::Element;

pub struct Panel<'a, Message> {
    pub title: &'a str,
    pub subtitle: Option<String>,
    pub body: Element<'a, Message>,
}

/// Standard panel chrome: title row, muted subtitle and a framed body.
pub fn view<'a, Message: 'a>(panel: Panel<'a, Message>) -> Element<'a, Message> {
    let mut header =
        row![text(panel.title).size(14).color(theme::TEXT)].align_y(iced::Alignment::Center);
    if let Some(subtitle) = panel.subtitle {
        header = header
            .push(space::horizontal())
            .push(text(subtitle).size(11).color(theme::TEXT_MUTED));
    }
    container(column![header, panel.body].spacing(8))
        .padding(10)
        .width(iced::Length::Fill)
        .height(iced::Length::Fill)
        .style(theme::panel)
        .into()
}

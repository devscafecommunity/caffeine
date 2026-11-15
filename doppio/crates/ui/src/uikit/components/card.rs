use crate::uikit::theme;
use iced::widget::{button, column, container, row, space, text};
use iced::{Element, Length};

/// Collapsible card used by property panels (component cards keep their own
/// data-driven header).
pub fn view<'a, Message: Clone + 'a>(
    title: &'a str,
    icon: &'a str,
    collapsed: bool,
    toggle_id: iced::widget::Id,
    body: Element<'a, Message>,
    on_toggle: Message,
) -> Element<'a, Message> {
    let chevron = container(
        button(text(if collapsed { "▸" } else { "▾" }).size(9))
            .style(theme::button_text)
            .padding([0, 2])
            .on_press(on_toggle),
    )
    .id(toggle_id);
    let header = container(
        row![
            chevron,
            text(icon).size(11).color(theme::ACCENT),
            text(title).size(11).color(theme::TEXT),
            space::horizontal(),
        ]
        .spacing(5)
        .align_y(iced::Alignment::Center),
    )
    .padding([1, 4])
    .style(theme::component_header);
    let body: Element<'a, Message> = if collapsed {
        space().height(0).into()
    } else {
        body
    };
    container(column![header, body].spacing(0))
        .width(Length::Fill)
        .style(theme::component_card)
        .into()
}

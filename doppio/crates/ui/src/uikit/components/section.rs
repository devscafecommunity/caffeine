//! Collapsible titled section used by property panels. The whole header row
//! toggles, like component cards in Unity/Godot inspectors.

use crate::uikit::theme;
use iced::widget::{column, container, mouse_area, row, text, Id};
use iced::{Element, Length};

pub fn view<'a, Message: Clone + 'a>(
    toggle_id: impl Into<Id>,
    title: &'a str,
    collapsed: bool,
    on_toggle: Message,
    body: Element<'a, Message>,
) -> Element<'a, Message> {
    let header = mouse_area(
        container(
            row![
                text(if collapsed { "▸" } else { "▾" })
                    .size(9)
                    .color(theme::TEXT_MUTED),
                text(title).size(11).color(theme::TEXT),
            ]
            .spacing(6)
            .align_y(iced::Alignment::Center),
        )
        .id(toggle_id.into())
        .width(Length::Fill)
        .padding([3, 6])
        .style(theme::component_header),
    )
    .on_press(on_toggle)
    .interaction(iced::mouse::Interaction::Pointer);

    let mut card = column![header].spacing(0);
    if !collapsed {
        card = card.push(container(body).padding([5, 6]));
    }
    container(card)
        .width(Length::Fill)
        .style(theme::component_card)
        .into()
}

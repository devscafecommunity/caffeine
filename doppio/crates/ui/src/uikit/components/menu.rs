use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::{button, column, container, row, space, text};
use iced::{Element, Length};

pub struct Item<'a, Message> {
    pub id: UiId,
    pub label: &'a str,
    pub shortcut: Option<&'a str>,
    pub message: Message,
}

/// A menu bar trigger (the dropdown is rendered as an overlay).
pub fn trigger<'a, Message: Clone + 'a>(
    id: UiId,
    label: &'a str,
    open: bool,
    on_toggle: Message,
) -> Element<'a, Message> {
    container(
        button(text(label).size(13))
            .style(theme::menu_trigger(open))
            .padding([4, 9])
            .on_press(on_toggle),
    )
    .id(id)
    .into()
}

/// A floating-looking list of menu items.
pub fn context<'a, Message: Clone + 'a>(items: Vec<Item<'a, Message>>) -> Element<'a, Message> {
    let mut list = column![].spacing(1);
    for item in items {
        let label = row![
            text(item.label).size(12).width(Length::Fill),
            text(item.shortcut.unwrap_or(""))
                .size(10)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(16);
        list = list.push(
            container(
                button(label)
                    .style(theme::menu_item)
                    .width(Length::Fill)
                    .padding([5, 8])
                    .on_press(item.message),
            )
            .id(item.id),
        );
    }
    container(list)
        .width(220)
        .padding(4)
        .style(theme::menu)
        .into()
}

/// Positions a dropdown below the menu bar at `x` logical pixels.
pub fn overlay<'a, Message: Clone + 'a>(
    x: f32,
    items: Vec<Item<'a, Message>>,
) -> Element<'a, Message> {
    container(
        column![
            space().height(Length::Fixed(52.0)),
            row![space().width(Length::Fixed(x)), context(items)],
        ]
        .width(Length::Fill),
    )
    .width(Length::Fill)
    .height(Length::Fill)
    .align_x(iced::alignment::Horizontal::Left)
    .align_y(iced::alignment::Vertical::Top)
    .into()
}

use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::{button, column, container, row, text};
use iced::{Element, Length};

pub struct Item<'a, Message> {
    pub id: UiId,
    pub label: &'a str,
    pub shortcut: Option<&'a str>,
    pub message: Message,
}

/// A menu bar entry with an optional dropdown.
pub fn view<'a, Message: Clone + 'a>(
    id: UiId,
    label: &'a str,
    open: bool,
    items: Vec<Item<'a, Message>>,
    on_toggle: Message,
) -> Element<'a, Message> {
    let trigger = container(
        button(text(label).size(13))
            .style(theme::button_text)
            .padding([4, 9])
            .on_press(on_toggle),
    )
    .id(id);

    let mut menu = column![trigger];
    if open {
        menu = menu.push(context(items));
    }
    menu.into()
}

/// A floating-looking list of menu items (used by context menus).
pub fn context<'a, Message: Clone + 'a>(items: Vec<Item<'a, Message>>) -> Element<'a, Message> {
    let mut list = column![].spacing(1);
    for item in items {
        let label = row![
            text(item.label).size(12).width(Length::Fill),
            text(item.shortcut.unwrap_or(""))
                .size(10)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(12);
        list = list.push(
            container(
                button(label)
                    .style(theme::button_text)
                    .width(Length::Fill)
                    .padding([4, 8])
                    .on_press(item.message),
            )
            .id(item.id),
        );
    }
    container(list)
        .width(190)
        .padding(4)
        .style(theme::panel)
        .into()
}

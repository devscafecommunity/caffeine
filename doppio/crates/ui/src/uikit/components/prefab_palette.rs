use crate::uikit::id;
use crate::uikit::prefabs::{Category, Prefab};
use crate::uikit::theme;
use iced::widget::{button, column, container, row, text};
use iced::{Element, Length};

pub struct Callbacks<'a, Message> {
    pub on_category: Box<dyn Fn(&'static str) -> Message + 'a>,
    pub on_item: Box<dyn Fn(&'static str) -> Message + 'a>,
    pub on_close: Message,
}

/// Floating create-entity palette: categories on the left, prefab recipes on
/// the right.
pub fn view<'a, Message: Clone + 'a>(
    categories: &'a [Category],
    active: &'a str,
    prefabs: &'a [Prefab],
    callbacks: Callbacks<'a, Message>,
) -> Element<'a, Message> {
    let close = container(
        button(text("×").size(12))
            .style(theme::button_text)
            .padding([0, 4])
            .on_press(callbacks.on_close),
    )
    .id(id::PREFAB_CLOSE);
    let header = container(
        row![
            text("Create Entity").size(13).color(theme::TEXT),
            iced::widget::space::horizontal(),
            close,
        ]
        .align_y(iced::Alignment::Center),
    )
    .padding([5, 8])
    .style(theme::panel_title);

    let mut sidebar = column![].spacing(2);
    for category in categories {
        let on_category = &callbacks.on_category;
        sidebar = sidebar.push(
            container(
                button(text(category.label).size(11))
                    .style(theme::tab(category.key == active))
                    .padding([4, 7])
                    .width(Length::Fill)
                    .on_press(on_category(category.key)),
            )
            .id(id::prefab_category(category.key)),
        );
    }

    let mut items = column![].spacing(2);
    for prefab in prefabs.iter().filter(|prefab| prefab.category == active) {
        let on_item = &callbacks.on_item;
        let caption = column![
            text(prefab.label).size(11).color(theme::TEXT),
            text(prefab.description).size(9).color(theme::TEXT_MUTED),
        ]
        .spacing(1);
        items = items.push(
            container(
                button(
                    row![
                        text(prefab.icon.glyph())
                            .size(14)
                            .color(prefab.icon.color()),
                        caption,
                    ]
                    .spacing(7)
                    .align_y(iced::Alignment::Center),
                )
                .style(theme::menu_item)
                .width(Length::Fill)
                .padding([4, 6])
                .on_press(on_item(prefab.key)),
            )
            .id(id::prefab_item(prefab.key)),
        );
    }

    let body = row![
        container(crate::uikit::components::scroll::vertical(sidebar)).width(Length::Fixed(124.0)),
        container(crate::uikit::components::scroll::vertical(items))
            .width(Length::Fill)
            .padding([0, 4]),
    ]
    .spacing(6)
    .height(Length::Fixed(360.0));

    container(column![header, body].spacing(0))
        .id(id::PREFAB_PALETTE)
        .width(Length::Fixed(430.0))
        .style(theme::menu)
        .into()
}

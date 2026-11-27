//! "Go to panel": a searchable list of every panel of the editor, grouped, with the open ones marked. It is the way
//! to a tab that a crowded tab strip hides.

use crate::uikit::components::{scroll, text_field};
use crate::uikit::id::{self, UiId};
use crate::uikit::theme;
use iced::widget::{button, column, container, row, space, text};
use iced::{Element, Length};

/// One panel of the list.
pub struct Row<Message> {
    pub id: UiId,
    pub label: String,
    /// What the panel is for.
    pub detail: &'static str,
    /// Whether it is the tab showing in its group now.
    pub showing: bool,
    pub on_pick: Message,
}

pub struct Section<Message> {
    pub title: &'static str,
    pub rows: Vec<Row<Message>>,
}

pub struct Callbacks<'a, Message> {
    pub on_search: Box<dyn Fn(String) -> Message + 'a>,
    pub on_close: Message,
}

pub fn view<'a, Message: Clone + 'a>(
    sections: Vec<Section<Message>>,
    search: &'a str,
    callbacks: Callbacks<'a, Message>,
) -> Element<'a, Message> {
    let header = container(
        row![
            text("Go to panel").size(13).color(theme::TEXT),
            text("Ctrl+P").size(10).color(theme::TEXT_MUTED),
            space::horizontal(),
            container(
                button(text("×").size(12))
                    .style(theme::button_text)
                    .padding([0, 4])
                    .on_press(callbacks.on_close),
            )
            .id(id::PANEL_PALETTE_CLOSE),
        ]
        .spacing(8)
        .align_y(iced::Alignment::Center),
    )
    .padding([5, 8])
    .style(theme::panel_title);

    let search_field = text_field::search(
        id::PANEL_PALETTE_SEARCH,
        "Type a panel name…",
        search,
        move |value| (callbacks.on_search)(value),
    );

    let mut list = column![].spacing(1);
    let mut any = false;
    for section in sections.into_iter().filter(|s| !s.rows.is_empty()) {
        any = true;
        list = list.push(
            container(
                text(section.title.to_uppercase())
                    .size(9)
                    .color(theme::TEXT_MUTED),
            )
            .padding(iced::Padding {
                top: 6.0,
                right: 6.0,
                bottom: 2.0,
                left: 6.0,
            }),
        );
        for entry in section.rows {
            let tag: Element<'a, Message> = if entry.showing {
                text("showing").size(9).color(theme::ACCENT).into()
            } else {
                space().into()
            };
            let body = row![
                column![
                    text(entry.label).size(11).color(theme::TEXT),
                    text(entry.detail).size(9).color(theme::TEXT_MUTED),
                ]
                .spacing(0)
                .width(Length::Fill),
                tag,
            ]
            .align_y(iced::Alignment::Center);
            list = list.push(
                container(
                    button(body)
                        .style(theme::menu_item)
                        .width(Length::Fill)
                        .padding([3, 6])
                        .on_press(entry.on_pick),
                )
                .id(entry.id),
            );
        }
    }
    if !any {
        list = list.push(
            container(
                text("No panel matches this search.")
                    .size(11)
                    .color(theme::TEXT_MUTED),
            )
            .padding([8, 6]),
        );
    }

    container(
        column![
            header,
            container(search_field).padding([4, 8]),
            container(scroll::vertical(list))
                .height(Length::Fixed(380.0))
                .padding([0, 6]),
        ]
        .spacing(0),
    )
    .id(id::PANEL_PALETTE)
    .width(Length::Fixed(420.0))
    .style(theme::menu)
    .into()
}

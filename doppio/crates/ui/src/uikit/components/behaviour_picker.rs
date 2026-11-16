//! Searchable, grouped picker for "Add Behaviour": components, scripts,
//! meshes and materials in one list (see `uikit::behaviour`).

use crate::uikit::components::{scroll, text_field};
use crate::uikit::id::{self, UiId};
use crate::uikit::theme;
use iced::widget::{button, column, container, row, space, text};
use iced::{Element, Length};

/// One selectable (or already-added) row.
pub struct Row<Message> {
    pub id: UiId,
    pub label: String,
    pub detail: String,
    /// Right-aligned tag, e.g. "Added".
    pub tag: Option<&'static str>,
    /// `None` makes the row inert (already on the entity).
    pub on_pick: Option<Message>,
}

pub struct Section<Message> {
    pub title: &'static str,
    pub rows: Vec<Row<Message>>,
}

pub struct Callbacks<'a, Message> {
    pub on_search: Box<dyn Fn(String) -> Message + 'a>,
    pub on_close: Message,
    pub on_new_script: Message,
}

pub fn view<'a, Message: Clone + 'a>(
    sections: Vec<Section<Message>>,
    search: &'a str,
    empty: &'a str,
    callbacks: Callbacks<'a, Message>,
) -> Element<'a, Message> {
    let header = container(
        row![
            text("Add Behaviour").size(13).color(theme::TEXT),
            space::horizontal(),
            container(
                button(text("×").size(12))
                    .style(theme::button_text)
                    .padding([0, 4])
                    .on_press(callbacks.on_close),
            )
            .id(id::BEHAVIOUR_PICKER_CLOSE),
        ]
        .align_y(iced::Alignment::Center),
    )
    .padding([5, 8])
    .style(theme::panel_title);

    let search_field = text_field::search(
        id::BEHAVIOUR_PICKER_SEARCH,
        "Search components, scripts, meshes, materials…",
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
            .padding([6.0, 6.0, 2.0, 6.0].into_padding()),
        );
        for entry in section.rows {
            let tag: Element<'a, Message> = match entry.tag {
                Some(tag) => text(tag).size(9).color(theme::TEXT_MUTED).into(),
                None => space().into(),
            };
            let body = row![
                column![
                    text(entry.label)
                        .size(11)
                        .color(if entry.on_pick.is_some() {
                            theme::TEXT
                        } else {
                            theme::TEXT_MUTED
                        }),
                    text(entry.detail).size(9).color(theme::TEXT_MUTED),
                ]
                .spacing(0)
                .width(Length::Fill),
                tag,
            ]
            .align_y(iced::Alignment::Center);
            let mut item = button(body)
                .style(theme::menu_item)
                .width(Length::Fill)
                .padding([3, 6]);
            if let Some(message) = entry.on_pick {
                item = item.on_press(message);
            }
            list = list.push(container(item).id(entry.id));
        }
    }
    if !any {
        list = list.push(container(text(empty).size(11).color(theme::TEXT_MUTED)).padding([8, 6]));
    }

    container(
        column![
            header,
            container(search_field).padding([4, 8]),
            container(scroll::vertical(list))
                .height(Length::Fixed(340.0))
                .padding([0, 6]),
            container(crate::uikit::components::button::block(
                id::BEHAVIOUR_NEW,
                "+ New Lua script…",
                callbacks.on_new_script,
            ))
            .padding([6, 8]),
        ]
        .spacing(0),
    )
    .id(id::BEHAVIOUR_PICKER)
    .width(Length::Fixed(420.0))
    .style(theme::menu)
    .into()
}

/// Tiny helper so the heading padding reads clearly.
trait IntoPadding {
    fn into_padding(self) -> iced::Padding;
}

impl IntoPadding for [f32; 4] {
    fn into_padding(self) -> iced::Padding {
        iced::Padding {
            top: self[0],
            right: self[1],
            bottom: self[2],
            left: self[3],
        }
    }
}

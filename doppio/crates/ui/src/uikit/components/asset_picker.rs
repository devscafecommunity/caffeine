use crate::uikit::components::text_field;
use crate::uikit::id;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{button, column, container, image, row, text};
use iced::{ContentFit, Element, Length};

pub struct Item {
    pub key: String,
    pub label: String,
    pub detail: String,
    pub thumbnail: Option<Handle>,
}

pub struct Ids {
    pub root: id::UiId,
    pub close: id::UiId,
    pub search: id::UiId,
    pub item: fn(usize) -> id::UiId,
}

pub struct Callbacks<'a, Message> {
    pub ids: Ids,
    pub on_search: Box<dyn Fn(String) -> Message + 'a>,
    pub on_pick: Box<dyn Fn(String) -> Message + 'a>,
    pub on_close: Message,
}

/// Texts and extras that differ between pickers (textures, scripts…).
pub struct Labels<'a, Message> {
    pub placeholder: &'a str,
    /// Short tag shown where a thumbnail would be (`TEX`, `LUA`…).
    pub badge: &'a str,
    pub empty: &'a str,
    /// Optional row under the list, e.g. "New script…".
    pub footer: Option<Element<'a, Message>>,
}

/// Floating project-asset picker used by texture slots.
pub fn view<'a, Message: Clone + 'a>(
    title: &'a str,
    items: Vec<Item>,
    search: &'a str,
    callbacks: Callbacks<'a, Message>,
) -> Element<'a, Message> {
    view_labeled(
        title,
        items,
        search,
        callbacks,
        Labels {
            placeholder: "Search textures…",
            badge: "TEX",
            empty: "No matching assets.",
            footer: None,
        },
    )
}

/// Same picker with custom wording and an optional footer.
pub fn view_labeled<'a, Message: Clone + 'a>(
    title: &'a str,
    items: Vec<Item>,
    search: &'a str,
    callbacks: Callbacks<'a, Message>,
    labels: Labels<'a, Message>,
) -> Element<'a, Message> {
    let close = container(
        button(text("×").size(12))
            .style(theme::button_text)
            .padding([0, 4])
            .on_press(callbacks.on_close),
    )
    .id(callbacks.ids.close.clone());
    let header = container(
        row![
            text(title).size(13).color(theme::TEXT),
            iced::widget::space::horizontal(),
            close,
        ]
        .align_y(iced::Alignment::Center),
    )
    .padding([5, 8])
    .style(theme::panel_title);

    let search = text_field::search(
        callbacks.ids.search.clone(),
        labels.placeholder,
        search,
        move |value| (callbacks.on_search)(value),
    );

    let mut list = column![].spacing(2);
    if items.is_empty() {
        list = list
            .push(container(text(labels.empty).size(11).color(theme::TEXT_MUTED)).padding([8, 6]));
    }
    for (index, item) in items.into_iter().enumerate() {
        let thumbnail: Element<'a, Message> = if let Some(handle) = &item.thumbnail {
            image(handle.clone())
                .width(Length::Fixed(30.0))
                .height(Length::Fixed(30.0))
                .content_fit(ContentFit::Cover)
                .into()
        } else {
            container(text(labels.badge).size(8).color(theme::TEXT_MUTED))
                .width(Length::Fixed(30.0))
                .height(Length::Fixed(30.0))
                .center_x(Length::Fixed(30.0))
                .center_y(Length::Fixed(30.0))
                .into()
        };
        let on_pick = &callbacks.on_pick;
        list = list.push(
            container(
                button(
                    row![
                        thumbnail,
                        column![
                            text(item.label.clone()).size(11).color(theme::TEXT),
                            text(item.detail.clone()).size(9).color(theme::TEXT_MUTED),
                        ]
                        .spacing(0)
                        .width(Length::Fill),
                    ]
                    .spacing(7)
                    .align_y(iced::Alignment::Center),
                )
                .style(theme::menu_item)
                .width(Length::Fill)
                .padding([3, 6])
                .on_press(on_pick(item.key.clone())),
            )
            .id((callbacks.ids.item)(index)),
        );
    }

    let mut body = column![
        header,
        container(search).padding([4, 8]),
        container(crate::uikit::components::scroll::vertical(list))
            .height(Length::Fixed(300.0))
            .padding([0, 6]),
    ]
    .spacing(0);
    if let Some(footer) = labels.footer {
        body = body.push(container(footer).padding([6, 8]));
    }

    container(body)
        .id(callbacks.ids.root.clone())
        .width(Length::Fixed(380.0))
        .style(theme::menu)
        .into()
}

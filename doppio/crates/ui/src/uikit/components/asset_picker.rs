use crate::uikit::asset_kind;
use crate::uikit::components::{asset_preview, text_field, toggle_chip};
use crate::uikit::id;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{button, column, container, row, text};
use iced::{Element, Length};

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

/// One kind filter above the list (`All`, `Textures`, `Meshes`…).
pub struct KindChip<Message> {
    pub id: id::UiId,
    pub label: String,
    pub active: bool,
    pub color: iced::Color,
    pub message: Message,
}

/// What a picker for a *field* adds to the plain list: the value it holds now, a way to empty it
/// and the kind filters. Pickers that only choose (no current value) use [`Extras::none`].
pub struct Extras<'a, Message> {
    /// The path the field holds now: that row is highlighted.
    pub current: &'a str,
    /// A "None" row that empties the field, with its id.
    pub clear: Option<(id::UiId, Message)>,
    pub kinds: Vec<KindChip<Message>>,
}

impl<'a, Message> Extras<'a, Message> {
    pub fn none() -> Self {
        Self {
            current: "",
            clear: None,
            kinds: Vec::new(),
        }
    }
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
    view_full(title, items, search, callbacks, labels, Extras::none())
}

/// The full picker: list with kind icons and extension chips, kind filters, a "None" row and the
/// current value highlighted.
pub fn view_full<'a, Message: Clone + 'a>(
    title: &'a str,
    items: Vec<Item>,
    search: &'a str,
    callbacks: Callbacks<'a, Message>,
    labels: Labels<'a, Message>,
    extras: Extras<'a, Message>,
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
    if let Some((clear_id, message)) = extras.clear {
        list = list.push(
            container(
                button(
                    row![
                        container(text("∅").size(14).color(theme::TEXT_MUTED))
                            .width(Length::Fixed(34.0))
                            .height(Length::Fixed(34.0))
                            .center_x(Length::Fixed(34.0))
                            .center_y(Length::Fixed(34.0))
                            .style(theme::asset_tile(theme::TEXT_MUTED)),
                        text("None").size(11).color(theme::TEXT),
                    ]
                    .spacing(7)
                    .align_y(iced::Alignment::Center),
                )
                .style(theme::menu_item)
                .width(Length::Fill)
                .padding([3, 6])
                .on_press(message),
            )
            .id(clear_id),
        );
    }
    if items.is_empty() {
        list = list
            .push(container(text(labels.empty).size(11).color(theme::TEXT_MUTED)).padding([8, 6]));
    }
    let shown = items.len();
    for (index, item) in items.into_iter().enumerate() {
        let kind = asset_kind::classify(std::path::Path::new(&item.key));
        let icon = kind.map(|kind| kind.icon());
        let entry = asset_preview::Entry {
            name: item.label.clone(),
            kind: labels.badge.to_owned(),
            detail: None,
            thumbnail: item.thumbnail.as_ref(),
            tint: None,
            icon,
            selected: false,
            accent: icon.map(|icon| icon.color()),
            tag: asset_kind::extension_tag(&item.label),
        };
        let current = !extras.current.is_empty() && extras.current == item.key;
        let on_pick = &callbacks.on_pick;
        let row_body = button(
            row![
                asset_preview::tile(&entry, 34.0),
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
        .on_press(on_pick(item.key.clone()));
        let mut wrapped = container(row_body).id((callbacks.ids.item)(index));
        if current {
            wrapped = wrapped.style(theme::list_row(true));
        }
        list = list.push(wrapped);
    }
    let _ = shown;

    let kind_row: Option<Element<'a, Message>> = if extras.kinds.is_empty() {
        None
    } else {
        let mut chips = row![].spacing(3).align_y(iced::Alignment::Center);
        for chip in extras.kinds {
            chips = chips.push(toggle_chip::view(
                chip.id,
                chip.label,
                chip.active,
                chip.color,
                chip.message,
            ));
        }
        Some(container(chips).padding([0, 8]).into())
    };

    let mut body = column![header, container(search).padding([4, 8])].spacing(0);
    if let Some(kind_row) = kind_row {
        body = body.push(kind_row);
    }
    body = body.push(
        container(crate::uikit::components::scroll::vertical(list))
            .height(Length::Fixed(340.0))
            .padding([4, 6]),
    );
    if let Some(footer) = labels.footer {
        body = body.push(container(footer).padding([6, 8]));
    }

    container(body)
        .id(callbacks.ids.root.clone())
        .width(Length::Fixed(420.0))
        .style(theme::menu)
        .into()
}

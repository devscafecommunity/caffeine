use crate::uikit::icons::Icon;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{column, container, image, mouse_area, stack, text, Id};
use iced::{ContentFit, Element, Length};

/// One asset tile or row. Panels build an `Entry` from their own asset data;
/// the preview component only handles the thumbnail/icon and labels.
pub struct Entry<'a> {
    pub name: String,
    pub kind: String,
    pub detail: Option<String>,
    pub thumbnail: Option<&'a Handle>,
    pub tint: Option<iced::Color>,
    pub icon: Option<Icon>,
    pub selected: bool,
    /// The kind's colour: the bar across the card and the icon well.
    pub accent: Option<iced::Color>,
    /// Extension chip on the tile corner (`PNG`, `GLB`); empty for none.
    pub tag: String,
}

impl<'a> Entry<'a> {
    pub fn new(name: impl Into<String>, kind: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            kind: kind.into(),
            detail: None,
            thumbnail: None,
            tint: None,
            icon: None,
            selected: false,
            accent: None,
            tag: String::new(),
        }
    }
}

fn short_name(name: &str, limit: usize) -> String {
    if name.chars().count() > limit {
        let mut short: String = name.chars().take(limit).collect();
        short.push('…');
        short
    } else {
        name.to_owned()
    }
}

/// The picture of an asset: its thumbnail in a frame, a material's colour, or its kind's icon on a
/// tinted well, with the extension on a chip in the corner.
pub fn tile<'a, Message: 'a>(entry: &Entry<'_>, size: f32) -> Element<'a, Message> {
    let accent = entry.accent.unwrap_or(theme::TEXT_MUTED);
    let picture: Element<'a, Message> = if let Some(handle) = entry.thumbnail {
        container(
            image(handle.clone())
                .width(Length::Fixed(size))
                .height(Length::Fixed(size))
                .content_fit(ContentFit::Cover),
        )
        .style(theme::asset_thumbnail_frame)
        .into()
    } else if let Some(tint) = entry.tint {
        container(iced::widget::space())
            .width(Length::Fixed(size))
            .height(Length::Fixed(size))
            .style(theme::swatch(tint))
            .into()
    } else if let Some(icon) = entry.icon {
        container(text(icon.glyph()).size(size * 0.52).color(icon.color()))
            .width(Length::Fixed(size))
            .height(Length::Fixed(size))
            .center_x(Length::Fixed(size))
            .center_y(Length::Fixed(size))
            .style(theme::asset_tile(icon.color()))
            .into()
    } else {
        container(
            text(entry.kind.to_uppercase())
                .size((size * 0.24).max(6.0))
                .color(theme::TEXT_MUTED),
        )
        .width(Length::Fixed(size))
        .height(Length::Fixed(size))
        .center_x(Length::Fixed(size))
        .center_y(Length::Fixed(size))
        .style(theme::asset_tile(accent))
        .into()
    };
    if entry.tag.is_empty() || size < 40.0 {
        return picture;
    }
    let chip = container(
        container(text(entry.tag.clone()).size(7))
            .padding([0, 3])
            .style(theme::asset_chip(accent)),
    )
    .width(Length::Fixed(size))
    .height(Length::Fixed(size))
    .padding(2)
    .align_x(iced::alignment::Horizontal::Right)
    .align_y(iced::alignment::Vertical::Bottom);
    stack([picture, chip.into()])
        .width(Length::Fixed(size))
        .height(Length::Fixed(size))
        .into()
}

/// Width of one grid tile; the asset browser derives its column count from it.
pub const CARD_WIDTH: f32 = 92.0;
const CARD_THUMB: f32 = 72.0;

/// Grid tile used by the asset browser: large thumbnail with the name below,
/// the way every DCC/engine content browser lays assets out. A bar in the kind's colour crowns
/// the card, so a folder full of mixed assets can be read at a glance.
pub fn card<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    entry: Entry<'a>,
    on_press: Message,
    on_release: Message,
    on_double_click: Option<Message>,
) -> Element<'a, Message> {
    let selected = entry.selected;
    let accent = entry.accent.unwrap_or(theme::BORDER_STRONG);
    let mut label = column![text(short_name(&entry.name, 13))
        .size(10)
        .color(theme::TEXT)
        .wrapping(iced::widget::text::Wrapping::None)]
    .spacing(0)
    .align_x(iced::Alignment::Center);
    if let Some(detail) = &entry.detail {
        label = label.push(
            text(short_name(detail, 16))
                .size(8)
                .color(theme::TEXT_MUTED)
                .wrapping(iced::widget::text::Wrapping::None),
        );
    }
    let bar = container(iced::widget::space())
        .width(Length::Fixed(CARD_THUMB))
        .height(Length::Fixed(2.0))
        .style(theme::asset_accent_bar(accent));
    let body = container(
        column![bar, tile(&entry, CARD_THUMB), label]
            .spacing(4)
            .align_x(iced::Alignment::Center),
    )
    .id(id.into())
    .width(Length::Fixed(CARD_WIDTH))
    .padding([5, 4])
    .center_x(Length::Fixed(CARD_WIDTH))
    .style(move |iced_theme: &iced::Theme| {
        if selected {
            theme::asset_card_selected(iced_theme)
        } else {
            theme::asset_card(iced_theme)
        }
    });
    let area = mouse_area(body)
        .on_press(on_press)
        .on_release(on_release)
        .interaction(iced::mouse::Interaction::Pointer);
    let area = if let Some(message) = on_double_click {
        area.on_double_click(message)
    } else {
        area
    };
    area.into()
}

/// Full-width asset row used by the list view.
pub fn row<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    entry: Entry<'a>,
    on_press: Message,
    on_release: Message,
    on_double_click: Option<Message>,
) -> Element<'a, Message> {
    let mut label = column![text(short_name(&entry.name, 30))
        .size(11)
        .color(theme::TEXT)
        .wrapping(iced::widget::text::Wrapping::None)]
    .spacing(0);
    if let Some(detail_text) = &entry.detail {
        label = label.push(
            text(short_name(detail_text, 42))
                .size(8)
                .color(theme::TEXT_MUTED)
                .wrapping(iced::widget::text::Wrapping::None),
        );
    }
    let accent = entry.accent.unwrap_or(theme::TEXT_MUTED);
    let kind = container(text(entry.kind.to_uppercase()).size(7).color(accent))
        .width(Length::Fixed(52.0))
        .align_x(iced::alignment::Horizontal::Right);
    let content = iced::widget::Row::new()
        .push(tile(&entry, 22.0))
        .push(container(label).width(Length::Fill))
        .push(kind)
        .width(Length::Fill)
        .spacing(6)
        .align_y(iced::Alignment::Center);
    let body = container(content)
        .id(id.into())
        .width(Length::Fill)
        .padding([3, 5])
        .style(theme::list_row(entry.selected));
    let area = mouse_area(body)
        .on_press(on_press)
        .on_release(on_release)
        .interaction(iced::mouse::Interaction::Pointer);
    let area = if let Some(message) = on_double_click {
        area.on_double_click(message)
    } else {
        area
    };
    area.into()
}

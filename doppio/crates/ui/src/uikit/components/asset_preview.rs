use crate::uikit::icons::Icon;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{column, container, image, mouse_area, text, Id};
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

fn tile<'a, Message: 'a>(entry: &Entry<'a>, size: f32) -> Element<'a, Message> {
    if let Some(handle) = entry.thumbnail {
        image(handle.clone())
            .width(Length::Fixed(size))
            .height(Length::Fixed(size))
            .content_fit(ContentFit::Cover)
            .into()
    } else if let Some(tint) = entry.tint {
        container(iced::widget::space())
            .width(Length::Fixed(size))
            .height(Length::Fixed(size))
            .style(theme::swatch(tint))
            .into()
    } else if let Some(icon) = entry.icon {
        container(text(icon.glyph()).size(size * 0.62).color(icon.color()))
            .width(Length::Fixed(size))
            .height(Length::Fixed(size))
            .center_x(Length::Fixed(size))
            .center_y(Length::Fixed(size))
            .style(theme::component_card)
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
        .into()
    }
}

/// Width of one grid tile; the asset browser derives its column count from it.
pub const CARD_WIDTH: f32 = 84.0;
const CARD_THUMB: f32 = 64.0;

/// Grid tile used by the asset browser: large thumbnail with the name below,
/// the way every DCC/engine content browser lays assets out.
pub fn card<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    entry: Entry<'a>,
    on_press: Message,
    on_release: Message,
    on_double_click: Option<Message>,
) -> Element<'a, Message> {
    let selected = entry.selected;
    let body = container(
        column![
            tile(&entry, CARD_THUMB),
            text(short_name(&entry.name, 12))
                .size(10)
                .color(theme::TEXT)
                .wrapping(iced::widget::text::Wrapping::None),
        ]
        .spacing(3)
        .align_x(iced::Alignment::Center),
    )
    .id(id.into())
    .width(Length::Fixed(CARD_WIDTH))
    .padding([5, 4])
    .center_x(Length::Fixed(CARD_WIDTH))
    .style(move |iced_theme: &iced::Theme| {
        if selected {
            theme::list_row(true)(iced_theme)
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
    let content = iced::widget::Row::new()
        .push(tile(&entry, 18.0))
        .push(label)
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

use crate::uikit::icons::Icon;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{button, column, container, image, mouse_area, text, Id};
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

/// Compact grid tile used by the asset browser.
pub fn card<'a, Message: Clone + 'a>(
    id: impl Into<Id>,
    entry: Entry<'a>,
    on_press: Message,
    on_release: Message,
    on_double_click: Option<Message>,
) -> Element<'a, Message> {
    let detail = entry.detail.clone().unwrap_or_else(|| entry.kind.clone());
    let selected = entry.selected;
    let body = container(
        button(
            iced::widget::row![
                tile(&entry, 28.0),
                column![
                    text(short_name(&entry.name, 13))
                        .size(10)
                        .color(theme::TEXT),
                    text(short_name(&detail, 14))
                        .size(8)
                        .color(theme::TEXT_MUTED),
                ]
                .spacing(0),
            ]
            .spacing(5)
            .align_y(iced::Alignment::Center),
        )
        .style(theme::menu_item)
        .padding(3)
        .width(Length::Fill),
    )
    .id(id.into())
    .width(Length::Fixed(92.0))
    .height(Length::Fixed(46.0))
    .padding(1)
    .style(move |iced_theme: &iced::Theme| {
        if selected {
            theme::list_row(true)(iced_theme)
        } else {
            theme::surface(iced_theme)
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

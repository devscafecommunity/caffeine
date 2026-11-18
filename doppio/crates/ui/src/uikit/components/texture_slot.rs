use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{button, column, container, image, mouse_area, row, text};
use iced::{ContentFit, Element, Length};

pub struct Slot<'a> {
    pub id: UiId,
    pub label: &'a str,
    pub path: &'a str,
    pub thumbnail: Option<Handle>,
    /// Short tag where a thumbnail would be (`TEX`, `MAT`).
    pub badge: &'a str,
}

/// A map slot: thumbnail, label, path and an optional clear button.
pub fn view<'a, Message: Clone + 'a>(
    slot: Slot<'a>,
    on_pick: Message,
    on_clear: Option<Message>,
    on_drop: Option<Message>,
) -> Element<'a, Message> {
    let file_name = std::path::Path::new(slot.path)
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("Empty");
    let thumbnail: Element<'a, Message> = if let Some(handle) = slot.thumbnail {
        image(handle)
            .width(Length::Fixed(34.0))
            .height(Length::Fixed(34.0))
            .content_fit(ContentFit::Cover)
            .into()
    } else {
        container(text(slot.badge.to_owned()).size(8).color(theme::TEXT_MUTED))
            .width(Length::Fixed(34.0))
            .height(Length::Fixed(34.0))
            .center_x(Length::Fixed(34.0))
            .center_y(Length::Fixed(34.0))
            .into()
    };

    let content = button(
        row![
            thumbnail,
            column![
                text(slot.label).size(10).color(theme::TEXT),
                text(file_name).size(9).color(theme::TEXT_MUTED),
            ]
            .spacing(0)
            .width(Length::Fill),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    )
    .style(theme::menu_item)
    .width(Length::Fill)
    .padding([2, 3])
    .on_press(on_pick);

    let mut content_row = row![container(content).width(Length::Fill)]
        .spacing(3)
        .align_y(iced::Alignment::Center);
    if let Some(on_clear) = on_clear {
        content_row = content_row.push(
            button(text("×").size(10))
                .style(theme::button_danger)
                .padding([0, 4])
                .on_press(on_clear),
        );
    }

    let body = container(content_row)
        .id(slot.id)
        .width(Length::Fill)
        .padding(1)
        .style(theme::texture_slot(!slot.path.is_empty()));
    if let Some(on_drop) = on_drop {
        mouse_area(body).on_release(on_drop).into()
    } else {
        body.into()
    }
}

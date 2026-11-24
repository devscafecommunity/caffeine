//! A brush in the Terrain Brush grid: the stamp's picture over the brush's name. Clicking selects it.

use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{column, container, image, mouse_area, text};
use iced::{ContentFit, Element, Length};

/// The picture side of a card, in logical pixels.
pub const THUMBNAIL: f32 = 52.0;

fn short(name: &str, limit: usize) -> String {
    if name.chars().count() > limit {
        let mut cut: String = name.chars().take(limit).collect();
        cut.push('…');
        cut
    } else {
        name.to_owned()
    }
}

/// One card: `thumbnail` is the stamp (or the falloff curve for a tool), `detail` a few words under the name
/// (the mode, for a tool).
pub fn view<'a, Message: Clone + 'a>(
    id: UiId,
    name: &str,
    detail: &str,
    thumbnail: Option<&Handle>,
    selected: bool,
    on_press: Message,
) -> Element<'a, Message> {
    let picture: Element<'a, Message> = match thumbnail {
        Some(handle) => container(
            image(handle.clone())
                .width(Length::Fixed(THUMBNAIL))
                .height(Length::Fixed(THUMBNAIL))
                .content_fit(ContentFit::Cover),
        )
        .style(theme::asset_thumbnail_frame)
        .into(),
        None => container(text("·").size(14).color(theme::TEXT_MUTED))
            .width(Length::Fixed(THUMBNAIL))
            .height(Length::Fixed(THUMBNAIL))
            .center_x(Length::Fixed(THUMBNAIL))
            .center_y(Length::Fixed(THUMBNAIL))
            .style(theme::asset_thumbnail_frame)
            .into(),
    };
    let card = container(
        column![
            picture,
            text(short(name, 11)).size(10).color(theme::TEXT),
            text(short(detail, 12)).size(8).color(theme::TEXT_MUTED),
        ]
        .spacing(2)
        .align_x(iced::Alignment::Center),
    )
    .padding(4)
    .width(Length::Fixed(THUMBNAIL + 14.0))
    .style(if selected {
        theme::asset_card_selected
    } else {
        theme::asset_card
    });
    container(
        mouse_area(card)
            .on_press(on_press)
            .interaction(iced::mouse::Interaction::Pointer),
    )
    .id(id)
    .into()
}

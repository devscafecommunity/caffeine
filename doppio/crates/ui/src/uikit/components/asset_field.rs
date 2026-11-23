//! A field that holds the path of a project asset: a thumbnail (or the kind's icon), the path as
//! editable text, a **…** button that opens the asset picker and a **×** that empties it.
//!
//! Typing stays possible on purpose: engine built-ins and paths outside the project are not in the
//! picker's list. Every place that asks for an asset composes this instead of a bare text field.

use crate::uikit::asset_kind::{self, AssetKind};
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{button, container, image, row, text, text_input};
use iced::{ContentFit, Element, Length};

pub struct AssetField<'a> {
    pub label: &'a str,
    pub value: &'a str,
    /// The kinds the field accepts; the first one names the icon shown when there is no thumbnail.
    pub kinds: &'a [AssetKind],
    pub thumbnail: Option<&'a Handle>,
    pub text_id: iced::widget::Id,
    pub pick_id: iced::widget::Id,
    pub clear_id: iced::widget::Id,
}

const CHIP: f32 = 20.0;

/// The icon well or thumbnail on the left of the field.
fn chip<'a, Message: 'a>(field: &AssetField<'_>) -> Element<'a, Message> {
    if let Some(handle) = field.thumbnail {
        return container(
            image(handle.clone())
                .width(Length::Fixed(CHIP))
                .height(Length::Fixed(CHIP))
                .content_fit(ContentFit::Cover),
        )
        .style(theme::asset_thumbnail_frame)
        .into();
    }
    // What the value is wins over what the field hoped for.
    let kind = asset_kind::classify(std::path::Path::new(field.value))
        .or_else(|| field.kinds.first().copied())
        .unwrap_or(AssetKind::Other);
    let icon = kind.icon();
    container(text(icon.glyph()).size(CHIP * 0.55).color(icon.color()))
        .width(Length::Fixed(CHIP))
        .height(Length::Fixed(CHIP))
        .center_x(Length::Fixed(CHIP))
        .center_y(Length::Fixed(CHIP))
        .style(theme::asset_tile(icon.color()))
        .into()
}

/// One asset reference row.
pub fn view<'a, Message: Clone + 'a>(
    field: AssetField<'_>,
    on_text: impl Fn(String) -> Message + 'a,
    on_pick: Message,
    on_clear: Message,
) -> Element<'a, Message> {
    let filled = !field.value.is_empty();
    let mut line = row![
        container(
            text(field.label.to_owned())
                .size(10)
                .color(theme::TEXT_MUTED)
        )
        .width(Length::Fixed(74.0)),
        chip(&field),
        text_input("none", field.value)
            .id(field.text_id.clone())
            .size(11)
            .padding([2, 5])
            .width(Length::Fill)
            .on_input(on_text)
            .style(theme::input),
        container(
            button(text("…").size(11))
                .style(theme::button_secondary)
                .padding([1, 6])
                .on_press(on_pick),
        )
        .id(field.pick_id.clone()),
    ]
    .spacing(4)
    .align_y(iced::Alignment::Center);
    if filled {
        line = line.push(
            container(
                button(text("×").size(10))
                    .style(theme::button_danger)
                    .padding([0, 4])
                    .on_press(on_clear),
            )
            .id(field.clear_id.clone()),
        );
    }
    line.into()
}

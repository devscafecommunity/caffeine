use crate::uikit::components::status;
use crate::uikit::id::UiId;
use crate::uikit::theme::{self, ChipTone};
use iced::mouse;
use iced::widget::{container, image, mouse_area, stack, text, Column};
use iced::{ContentFit, Element, Length, Point};

pub struct Surface<'a, Message> {
    pub id: UiId,
    pub image: Option<image::Handle>,
    pub status: String,
    pub hint: String,
    pub empty: bool,
    pub dragging: bool,
    pub on_left_press: Message,
    pub on_right_press: Message,
    pub on_left_release: Message,
    pub on_right_release: Message,
    pub on_move: Box<dyn Fn(Point) -> Message + 'a>,
    pub on_scroll: Box<dyn Fn(mouse::ScrollDelta) -> Message + 'a>,
    pub on_double_click: Message,
}

/// The engine surface: image or empty state, HUD chips and mouse handling.
pub fn view<'a, Message: Clone + 'a>(surface: Surface<'a, Message>) -> Element<'a, Message> {
    let Surface {
        id,
        image,
        status: status_label,
        hint,
        empty,
        dragging,
        on_left_press,
        on_right_press,
        on_left_release,
        on_right_release,
        on_move,
        on_scroll,
        on_double_click,
    } = surface;
    let base: Element<'a, Message> = match image {
        Some(handle) => container(
            iced::widget::image(handle)
                .width(Length::Fill)
                .height(Length::Fill)
                .content_fit(ContentFit::Contain),
        )
        .id(id)
        .width(Length::Fill)
        .height(Length::Fill)
        .style(theme::surface)
        .into(),
        None => container(
            text("Waiting for the engine viewport…")
                .size(13)
                .color(theme::TEXT_MUTED),
        )
        .id(id)
        .width(Length::Fill)
        .height(Length::Fill)
        .center(Length::Fill)
        .style(theme::surface)
        .into(),
    };

    let interactive: Element<'a, Message> = mouse_area(base)
        .on_press(on_left_press)
        .on_right_press(on_right_press)
        .on_release(on_left_release)
        .on_right_release(on_right_release)
        .on_move(on_move)
        .on_scroll(on_scroll)
        .on_double_click(on_double_click)
        .interaction(if dragging {
            mouse::Interaction::Grabbing
        } else {
            mouse::Interaction::Grab
        })
        .into();

    let top: Element<'a, Message> = container(
        iced::widget::row![
            status::chip(status_label, ChipTone::Accent),
            status::chip(
                "Drag orbit · Right drag pan · Wheel zoom",
                ChipTone::Neutral
            ),
        ]
        .spacing(6),
    )
    .padding(8)
    .width(Length::Fill)
    .height(Length::Fill)
    .align_x(iced::alignment::Horizontal::Left)
    .align_y(iced::alignment::Vertical::Top)
    .into();

    let bottom: Element<'a, Message> = container(text(hint).size(11).color(theme::TEXT_MUTED))
        .padding(8)
        .width(Length::Fill)
        .height(Length::Fill)
        .align_x(iced::alignment::Horizontal::Left)
        .align_y(iced::alignment::Vertical::Bottom)
        .into();

    let mut layers = stack([interactive, top, bottom]);
    if empty {
        let empty: Element<'a, Message> = container(
            Column::new()
                .push(text("Empty scene").size(18).color(theme::TEXT))
                .push(
                    text("Add an entity or open a .caf scene to see it here.")
                        .size(12)
                        .color(theme::TEXT_MUTED),
                )
                .spacing(4),
        )
        .width(Length::Fill)
        .height(Length::Fill)
        .center(Length::Fill)
        .into();
        layers = layers.push(empty);
    }
    layers.into()
}

use crate::uikit::components::{status, viewport_shader};
use crate::uikit::id::UiId;
use crate::uikit::theme::{self, ChipTone};
use iced::mouse;
use iced::widget::{container, mouse_area, sensor, stack, text, Column};
use iced::{Element, Length, Point, Size};
use std::rc::Rc;

pub struct Surface<'a, Message> {
    pub id: UiId,
    pub frame: Option<&'a viewport_shader::Frame>,
    pub status: String,
    pub hint: String,
    pub empty: bool,
    pub dragging: bool,
    pub on_left_press: Message,
    pub on_right_press: Message,
    pub on_left_release: Message,
    pub on_right_release: Message,
    pub on_exit: Message,
    pub on_move: Box<dyn Fn(Point) -> Message + 'a>,
    pub on_scroll: Box<dyn Fn(mouse::ScrollDelta) -> Message + 'a>,
    pub on_double_click: Message,
    pub on_resize: Box<dyn Fn(Size) -> Message + 'a>,
    pub gizmo: Element<'a, Message>,
}

/// The engine surface: image or empty state, HUD chips and mouse handling.
pub fn view<'a, Message: Clone + 'a>(surface: Surface<'a, Message>) -> Element<'a, Message> {
    let Surface {
        id,
        frame,
        status: status_label,
        hint,
        empty,
        dragging,
        on_left_press,
        on_right_press,
        on_left_release,
        on_right_release,
        on_exit,
        on_move,
        on_scroll,
        on_double_click,
        on_resize,
        gizmo,
    } = surface;
    let base: Element<'a, Message> = match frame {
        Some(frame) => container(viewport_shader::view(frame))
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
        .on_exit(on_exit)
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
                "WASD/QE fly · Shift boost · Drag orbit · Wheel zoom",
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

    let mut layers = stack([interactive, gizmo, top, bottom]);
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

    // Report the real pane size so the engine can render at native resolution
    // instead of stretching a fixed image.
    let on_resize = Rc::new(on_resize);
    let on_show = on_resize.clone();
    let on_resize = on_resize.clone();
    sensor(layers)
        .on_show(move |size| on_show(size))
        .on_resize(move |size| on_resize(size))
        .into()
}

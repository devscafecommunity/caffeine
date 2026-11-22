//! A scene view: the scene seen through one of its cameras (the Gameplay and Camera tabs).
//!
//! The picture fills the pane; a chip names what it shows. The Gameplay tab also forwards the pointer to the
//! running game, which is why the pointer messages are optional.

use crate::uikit::components::{status, viewport_shader};
use crate::uikit::id::UiId;
use crate::uikit::theme::{self, ChipTone};
use iced::widget::{container, mouse_area, sensor, stack, text, Column};
use iced::{Element, Length, Point, Size};
use std::rc::Rc;

pub struct SceneView<'a, Message> {
    pub id: UiId,
    pub frame: Option<&'a viewport_shader::Frame>,
    /// The texture slot of this picture (see `viewport_shader::view_slot`): never the editor viewport's 0.
    pub slot: u64,
    /// What the chip at the top left says (`Game camera · playing`).
    pub status: String,
    /// Shown instead of the picture when there is none: why (`No Camera 3D in the scene`).
    pub placeholder: Option<(String, String)>,
    /// The game reads the pointer: move and button messages (button 1 left, 2 middle, 3 right).
    pub on_move: Option<Box<dyn Fn(Point) -> Message + 'a>>,
    pub on_button: Option<Box<dyn Fn(u32, bool) -> Message + 'a>>,
    pub on_resize: Box<dyn Fn(Size) -> Message + 'a>,
}

pub fn view<'a, Message: Clone + 'a>(scene: SceneView<'a, Message>) -> Element<'a, Message> {
    let SceneView {
        id,
        frame,
        slot,
        status: status_label,
        placeholder,
        on_move,
        on_button,
        on_resize,
    } = scene;
    let base: Element<'a, Message> = match (frame, placeholder) {
        (_, Some((title, detail))) => container(
            Column::new()
                .push(text(title).size(16).color(theme::TEXT))
                .push(text(detail).size(12).color(theme::TEXT_MUTED))
                .spacing(4)
                .align_x(iced::Alignment::Center),
        )
        .id(id)
        .width(Length::Fill)
        .height(Length::Fill)
        .center(Length::Fill)
        .style(theme::surface)
        .into(),
        (Some(frame), None) => container(viewport_shader::view_slot(frame, slot))
            .id(id)
            .width(Length::Fill)
            .height(Length::Fill)
            .style(theme::surface)
            .into(),
        (None, None) => container(
            text("Waiting for the engine…")
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

    let mut area = mouse_area(base);
    if let Some(on_move) = on_move {
        area = area.on_move(on_move);
    }
    if let Some(on_button) = on_button {
        let on_button = Rc::new(on_button);
        let (press, release) = (on_button.clone(), on_button.clone());
        let (press_right, release_right) = (on_button.clone(), on_button.clone());
        area = area
            .on_press(press(1, true))
            .on_release(release(1, false))
            .on_right_press(press_right(3, true))
            .on_right_release(release_right(3, false));
    }

    let chip: Element<'a, Message> = container(status::chip(status_label, ChipTone::Accent))
        .padding(8)
        .width(Length::Fill)
        .height(Length::Fill)
        .align_x(iced::alignment::Horizontal::Left)
        .align_y(iced::alignment::Vertical::Top)
        .into();
    let on_resize = Rc::new(on_resize);
    let on_show = on_resize.clone();
    sensor(stack([area.into(), chip]))
        .on_show(move |size| on_show(size))
        .on_resize(move |size| on_resize(size))
        .into()
}

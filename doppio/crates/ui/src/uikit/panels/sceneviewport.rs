use crate::uikit::components::rendering_canvas;
use iced::widget::{column, container, text};
use iced::Element;

pub fn view<'a, Message: 'a>(image: Option<iced::widget::image::Handle>) -> Element<'a, Message> {
    let viewport = match image {
        Some(handle) => rendering_canvas::preview::<Message>("scene-viewport", handle),
        None => rendering_canvas::placeholder::<Message>(
            "scene-viewport",
            "Waiting for the engine viewport...",
        ),
    };
    container(column![text("Viewport").size(16), viewport].spacing(8))
        .padding(8)
        .into()
}

use crate::uikit::components::{button, rendering_canvas};
use iced::widget::{column, container, row, text};
use iced::Element;

pub fn view<'a, Message: Clone + 'a>(
    image: Option<iced::widget::image::Handle>,
    on_orbit_left: Message,
    on_orbit_right: Message,
    on_zoom_in: Message,
    on_zoom_out: Message,
    on_reset: Message,
) -> Element<'a, Message> {
    let viewport = match image {
        Some(handle) => rendering_canvas::preview::<Message>("scene-viewport", handle),
        None => rendering_canvas::placeholder::<Message>(
            "scene-viewport",
            "Waiting for the engine viewport...",
        ),
    };

    let camera = row![
        button::toolbar("orbit-left", "Orbit -", on_orbit_left),
        button::toolbar("orbit-right", "Orbit +", on_orbit_right),
        button::toolbar("zoom-in", "Zoom +", on_zoom_in),
        button::toolbar("zoom-out", "Zoom -", on_zoom_out),
        button::toolbar("reset-camera", "Reset", on_reset),
    ]
    .spacing(4);

    container(column![text("Viewport").size(16), camera, viewport].spacing(8))
        .padding(8)
        .into()
}

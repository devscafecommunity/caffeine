use crate::uikit::components::viewport_surface;
use crate::uikit::id;
use iced::mouse;
use iced::widget::container;
use iced::{Element, Point};

pub struct ViewportState<'a> {
    pub image: Option<iced::widget::image::Handle>,
    pub status: String,
    pub hint: String,
    pub empty: bool,
    pub dragging: bool,
    pub camera: &'a doppio_engine::Camera,
}

pub struct ViewportCallbacks<'a, Message> {
    pub surface_left_press: Message,
    pub surface_right_press: Message,
    pub surface_left_release: Message,
    pub surface_right_release: Message,
    pub surface_move: Box<dyn Fn(Point) -> Message + 'a>,
    pub surface_scroll: Box<dyn Fn(mouse::ScrollDelta) -> Message + 'a>,
    pub surface_double_click: Message,
}

/// The viewport panel: camera toolbar + interactive engine surface.
pub fn view<'a, Message: Clone + 'a>(
    state: ViewportState<'a>,
    callbacks: ViewportCallbacks<'a, Message>,
) -> Element<'a, Message> {
    let surface = viewport_surface::view(viewport_surface::Surface {
        id: id::VIEWPORT_SURFACE,
        image: state.image,
        status: state.status,
        hint: state.hint,
        empty: state.empty,
        dragging: state.dragging,
        on_left_press: callbacks.surface_left_press,
        on_right_press: callbacks.surface_right_press,
        on_left_release: callbacks.surface_left_release,
        on_right_release: callbacks.surface_right_release,
        on_move: callbacks.surface_move,
        on_scroll: callbacks.surface_scroll,
        on_double_click: callbacks.surface_double_click,
    });

    container(surface)
        .width(iced::Length::Fill)
        .height(iced::Length::Fill)
        .into()
}

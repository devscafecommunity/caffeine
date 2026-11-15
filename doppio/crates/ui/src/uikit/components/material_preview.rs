use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{container, image, mouse_area, text};
use iced::{ContentFit, Element, Length, Point};

/// Rendered material sphere. Dragging orbits the preview.
pub fn view<'a, Message: Clone + 'a>(
    id: UiId,
    handle: Option<&Handle>,
    height: f32,
    on_press: Message,
    on_move: impl Fn(Point) -> Message + 'a,
    on_release: Message,
) -> Element<'a, Message> {
    let body: Element<'a, Message> = if let Some(handle) = handle {
        mouse_area(
            image(handle.clone())
                .width(Length::Fill)
                .height(Length::Fixed(height))
                .content_fit(ContentFit::Contain),
        )
        .on_press(on_press)
        .on_release(on_release)
        .on_move(on_move)
        .into()
    } else {
        container(
            text("Save or open a material to render its preview")
                .size(10)
                .color(theme::TEXT_MUTED),
        )
        .width(Length::Fill)
        .height(Length::Fixed(height))
        .center_x(Length::Fill)
        .center_y(Length::Fixed(height))
        .into()
    };

    container(body)
        .id(id)
        .width(Length::Fill)
        .style(theme::material_preview)
        .into()
}

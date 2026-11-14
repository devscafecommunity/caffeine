use crate::uikit::components::rendering_canvas;
use iced::widget::{column, container, text};
use iced::Element;

pub fn view<'a, Message: 'a>() -> Element<'a, Message> {
    container(
        column![
            text("Viewport").size(16),
            rendering_canvas::placeholder::<Message>(
                "scene-viewport",
                "The caffeine render texture will be embedded here.",
            ),
        ]
        .spacing(8),
    )
    .padding(8)
    .into()
}

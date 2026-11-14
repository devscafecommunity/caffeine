use iced::widget::{container, text};
use iced::{ContentFit, Element, Length};

/// Placeholder for the engine render texture.
///
/// This is the seam where the viewport will receive a wgpu texture (readback
/// first, shared texture later). Keeping it as a component means every panel
/// uses the same host and the same stable id.
pub fn placeholder<'a, Message: 'a>(
    id: impl Into<String>,
    label: impl Into<String>,
) -> Element<'a, Message> {
    container(text(label.into()))
        .id(id.into())
        .padding(8)
        .width(Length::Fill)
        .height(Length::Fill)
        .into()
}

/// Displays a frame produced by the engine readback path.
pub fn preview<'a, Message: 'a>(
    id: impl Into<String>,
    handle: iced::widget::image::Handle,
) -> Element<'a, Message> {
    let image = iced::widget::image(handle)
        .width(Length::Fill)
        .height(Length::Fill)
        .content_fit(ContentFit::Contain);
    container(image)
        .id(id.into())
        .width(Length::Fill)
        .height(Length::Fill)
        .into()
}

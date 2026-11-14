use iced::widget::{container, text};
use iced::{Element, Length};

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

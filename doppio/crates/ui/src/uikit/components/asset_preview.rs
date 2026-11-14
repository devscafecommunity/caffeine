use iced::widget::{column, container, text, Id};
use iced::Element;

/// Lightweight asset thumbnail/card. The real thumbnail texture will be added
/// when the asset preview service is exposed through the FFI.
pub fn card<'a, Message: 'a>(
    id: impl Into<Id>,
    name: impl Into<String>,
    kind: impl Into<String>,
) -> Element<'a, Message> {
    container(column![text(name.into()), text(kind.into()).size(12)])
        .id(id.into())
        .padding(8)
        .width(120.0)
        .height(80.0)
        .into()
}

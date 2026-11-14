use iced::widget::{column, container, text, Id};
use iced::Element;

/// Lightweight asset thumbnail/card. The real thumbnail texture will be added
/// when the asset preview service is exposed through the FFI.
pub fn card<'a, Message: 'a>(
    id: impl Into<Id>,
    name: impl Into<String>,
    kind: impl Into<String>,
) -> Element<'a, Message> {
    let name = name.into();
    let name = if name.chars().count() > 10 {
        let mut short: String = name.chars().take(10).collect();
        short.push('…');
        short
    } else {
        name
    };
    container(column![text(name).size(10), text(kind.into()).size(9)])
        .id(id.into())
        .padding(6)
        .width(96.0)
        .height(54.0)
        .into()
}

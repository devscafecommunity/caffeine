//! Reusable canvas-based code editor.
//!
//! Iced 0.14's `text_editor` renders empty in the current wgpu/tiny-skia
//! environment, so Doppio uses a canvas editor instead. The wrapper keeps the
//! pane on a stable component boundary and gives the editor a queryable id.

use crate::uikit::theme;
use iced::widget::container;
use iced::{Element, Length};

pub fn view<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    editor: &'a iced_code_editor::CodeEditor,
    on_action: impl Fn(iced_code_editor::Message) -> Message + 'a,
) -> Element<'a, Message> {
    container(editor.view().map(on_action))
        .id(id.into())
        .width(Length::Fill)
        .height(Length::Fill)
        .padding(1)
        .style(theme::surface)
        .into()
}

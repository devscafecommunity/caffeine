//! Reusable canvas-based code editor.
//!
//! Iced 0.14's `text_editor` renders empty in the current wgpu/tiny-skia
//! environment, so Doppio uses a canvas editor instead. The wrapper keeps the
//! pane on a stable component boundary and gives the editor a queryable id.

use crate::uikit::theme;
use iced::widget::{container, stack};
use iced::{Element, Length, Theme};
use std::sync::OnceLock;

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

fn overlay_theme() -> &'static Theme {
    static THEME: OnceLock<Theme> = OnceLock::new();
    THEME.get_or_init(|| Theme::Dark)
}

/// The editor with a floating reference card (hover tooltip) above it.
pub fn view_with_reference<'a, Message: Clone + 'a>(
    id: impl Into<iced::widget::Id>,
    editor: &'a iced_code_editor::CodeEditor,
    overlay: &'a iced_code_editor::LspOverlayState,
    on_action: impl Fn(iced_code_editor::Message) -> Message + 'a,
    on_overlay: impl Fn(iced_code_editor::LspOverlayMessage) -> Message + 'a,
) -> Element<'a, Message> {
    let card = iced_code_editor::view_lsp_overlay(
        overlay,
        editor,
        overlay_theme(),
        14.0,
        20.0,
        on_overlay,
    );
    stack![view(id, editor, on_action), card].into()
}

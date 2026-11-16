//! Script editor pane.
//!
//! A compact tab strip over the open `.lua`/`.dyd` documents, a toolbar with
//! save/revert/close and the attach/detach hooks, and a full-height code
//! editor. Attaching a script to the selected entity is emitted as a message;
//! the window owns the engine binding.

use crate::uikit::components::{button, code_editor, status, toggle_chip};
use crate::uikit::id::UiId;
use crate::uikit::script;
use crate::uikit::theme;
use iced::widget::{column, container, row, scrollable, space, text};
use iced::{Element, Length};

/// Stable ids for the pane widgets. `tab`/`tab_close` build per-document ids.
pub struct Ids {
    pub root: UiId,
    pub editor: UiId,
    pub save: UiId,
    pub revert: UiId,
    pub close: UiId,
    pub attach: UiId,
    pub detach: UiId,
    pub tab: fn(usize) -> UiId,
    pub tab_close: fn(usize) -> UiId,
}

impl Default for Ids {
    fn default() -> Self {
        Self {
            root: UiId::fixed("editor.pane.script"),
            editor: UiId::fixed("script.editor.code"),
            save: UiId::fixed("script.toolbar.save"),
            revert: UiId::fixed("script.toolbar.revert"),
            close: UiId::fixed("script.toolbar.close"),
            attach: UiId::fixed("script.toolbar.attach"),
            detach: UiId::fixed("script.toolbar.detach"),
            tab: |index| UiId::dynamic(format!("script.tabs.tab.{index}")),
            tab_close: |index| UiId::dynamic(format!("script.tabs.close.{index}")),
        }
    }
}

pub struct Callbacks<'a, Message> {
    pub ids: Ids,
    pub on_select: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_tab_close: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_action: Box<dyn Fn(iced_code_editor::Message) -> Message + 'a>,
    pub on_save: Message,
    pub on_revert: Message,
    pub on_close: Message,
    pub on_attach: Message,
    pub on_detach: Message,
}

pub fn view<'a, Message: Clone + 'a>(
    state: &'a script::State,
    callbacks: Callbacks<'a, Message>,
) -> Element<'a, Message> {
    let Callbacks {
        ids,
        on_select,
        on_tab_close,
        on_action,
        on_save,
        on_revert,
        on_close,
        on_attach,
        on_detach,
    } = callbacks;

    if state.documents.is_empty() {
        return container(
            column![
                text("Script Editor").size(14).color(theme::TEXT),
                text("Open a .lua or .dyd file from Assets to edit it here.")
                    .size(10)
                    .color(theme::TEXT_MUTED),
                text("Save writes the file to disk; Revert restores the last saved version.")
                    .size(9)
                    .color(theme::TEXT_MUTED),
            ]
            .spacing(4)
            .align_x(iced::Alignment::Center),
        )
        .id(ids.root)
        .width(Length::Fill)
        .height(Length::Fill)
        .center_x(Length::Fill)
        .center_y(Length::Fill)
        .padding(16)
        .into();
    }

    let mut tabs = row![].spacing(3).align_y(iced::Alignment::Center);
    for (index, document) in state.documents.iter().enumerate() {
        let label = if document.is_dirty() {
            format!("{} *", document.title)
        } else {
            document.title.clone()
        };
        tabs = tabs
            .push(toggle_chip::view(
                (ids.tab)(index),
                label,
                state.active == Some(index),
                theme::ACCENT,
                (on_select)(index),
            ))
            .push(button::compact(
                (ids.tab_close)(index),
                "×",
                (on_tab_close)(index),
            ));
    }
    let strip = scrollable(tabs).horizontal().height(Length::Shrink);

    let toolbar = row![
        button::primary(ids.save, "Save", on_save),
        button::toolbar(ids.revert, "Revert", on_revert),
        button::toolbar(ids.close, "Close", on_close),
        space::horizontal(),
        button::toolbar(ids.attach, "Attach Selected", on_attach),
        button::toolbar(ids.detach, "Detach", on_detach),
    ]
    .spacing(4)
    .align_y(iced::Alignment::Center);

    let active = state.current();
    let mut meta = row![].spacing(6).align_y(iced::Alignment::Center);
    if let Some(document) = active {
        let path = if document.relative_path.is_empty() {
            document.path.display().to_string()
        } else {
            document.relative_path.clone()
        };
        meta = meta.push(text(path).size(9).color(theme::TEXT_MUTED));
        if document.is_dirty() {
            meta = meta.push(status::chip("unsaved", theme::ChipTone::Warning));
        }
    }
    if let Some(note) = state.status.as_deref() {
        meta = meta.push(text(note).size(9).color(theme::TEXT_MUTED));
    }

    let body: Element<'a, Message> = match active {
        Some(document) => code_editor::view(ids.editor, &document.editor, on_action),
        None => container(
            text("Select a script tab.")
                .size(10)
                .color(theme::TEXT_MUTED),
        )
        .width(Length::Fill)
        .height(Length::Fill)
        .center_x(Length::Fill)
        .center_y(Length::Fill)
        .into(),
    };

    container(
        column![strip, toolbar, meta, body]
            .spacing(6)
            .height(Length::Fill),
    )
    .id(ids.root)
    .width(Length::Fill)
    .height(Length::Fill)
    .into()
}

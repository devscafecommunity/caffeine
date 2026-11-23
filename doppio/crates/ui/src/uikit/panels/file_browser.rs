//! The file browser modal: places on the left, the folder's entries on the right, filters and the
//! selection at the bottom. A pure view over [`Browser`]; the window owns the state and the messages.

use crate::uikit::asset_kind::{self, AssetKind};
use crate::uikit::components::{
    asset_preview, breadcrumb, button, checkbox, list_row, scroll, text_field,
};
use crate::uikit::file_browser::{format_size, Browser};
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::theme;
use iced::widget::{column, container, row, space, text};
use iced::{Element, Length};
use std::path::PathBuf;
use std::rc::Rc;

#[derive(Debug, Clone, PartialEq)]
pub enum Edit {
    /// Enter a folder (a breadcrumb or a folder row).
    Go(PathBuf),
    Up,
    /// A sidebar place, by index.
    Place(usize),
    /// A row was pressed: toggle it in the selection.
    Press(usize),
    /// A row was double-clicked: enter a folder, or take that file and finish.
    Open(usize),
    Search(String),
    PathInput(String),
    PathGo,
    Hidden(bool),
    Supported(bool),
    SelectSupported,
    Clear,
    Refresh,
    /// Nothing: the release half of a row press.
    Noop,
    Confirm,
    Close,
}

fn entry_icon(entry: &crate::uikit::file_browser::Entry) -> Icon {
    if entry.is_dir {
        Icon::Folder
    } else {
        entry.kind.unwrap_or(AssetKind::Other).icon()
    }
}

pub fn view<'a, Message: Clone + 'a>(
    browser: &'a Browser,
    title: &'a str,
    confirm_label: &'a str,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);

    let header = container(
        row![
            column![
                text(title).size(15).color(theme::TEXT),
                text("Files and folders on this computer")
                    .size(10)
                    .color(theme::TEXT_MUTED),
            ]
            .spacing(1),
            space::horizontal(),
            button::compact(id::FILEBROWSER_CLOSE, "×", emit(Edit::Close)),
        ]
        .align_y(iced::Alignment::Center),
    )
    .padding([8, 12])
    .style(theme::panel_title);

    // Navigation: up, the trail, and a box that takes a typed or pasted path.
    let crumb_emit = emit.clone();
    let crumbs: Vec<breadcrumb::Crumb> = crate::uikit::file_browser::crumbs(&browser.dir)
        .into_iter()
        .enumerate()
        .map(|(index, (label, path))| breadcrumb::Crumb {
            id: id::file_browser_crumb(index),
            label,
            path: path.display().to_string(),
        })
        .collect();
    let trail = scroll::horizontal(breadcrumb::view(crumbs, move |path| {
        crumb_emit(Edit::Go(PathBuf::from(path)))
    }));
    let path_emit = emit.clone();
    let navigation = column![
        row![
            button::compact(id::FILEBROWSER_UP, "↑", emit(Edit::Up)),
            container(trail).width(Length::Fill),
            button::compact(id::FILEBROWSER_REFRESH, "⟳", emit(Edit::Refresh)),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
        text_field::submit(
            id::FILEBROWSER_PATH,
            "Type or paste a path, then press Enter…",
            &browser.path_input,
            move |value| path_emit(Edit::PathInput(value)),
            emit(Edit::PathGo),
        ),
    ]
    .spacing(4);

    // Sidebar.
    let mut places = column![].spacing(2);
    for (index, place) in browser.places.iter().enumerate() {
        places = places.push(list_row::view(
            id::file_browser_place(index),
            place.label.clone(),
            browser.dir == place.path,
            emit(Edit::Place(index)),
            emit(Edit::Place(index)),
            emit(Edit::Place(index)),
        ));
    }
    let sidebar = container(scroll::vertical(places))
        .width(Length::Fixed(150.0))
        .height(Length::Fill)
        .padding(3)
        .style(theme::surface);

    // Entries.
    let mut rows = column![].spacing(1);
    if let Some(error) = &browser.error {
        rows =
            rows.push(container(text(error.clone()).size(11).color(theme::DANGER)).padding([8, 6]));
    } else if browser.entries.is_empty() {
        rows = rows.push(
            container(
                text(if browser.search.is_empty() {
                    "This folder has nothing to show with these filters."
                } else {
                    "Nothing matches this search."
                })
                .size(11)
                .color(theme::TEXT_MUTED),
            )
            .padding([8, 6]),
        );
    }
    for (index, entry) in browser.entries.iter().enumerate() {
        let icon = entry_icon(entry);
        let detail = if entry.is_dir {
            "folder".to_owned()
        } else {
            let kind = entry
                .kind
                .map(|kind| kind.label())
                .unwrap_or("not supported");
            format!("{kind} · {}", format_size(entry.size))
        };
        let preview = asset_preview::Entry {
            name: entry.name.clone(),
            kind: if entry.is_dir {
                "folder".to_owned()
            } else {
                entry
                    .kind
                    .map(|kind| kind.key().to_owned())
                    .unwrap_or_else(|| "file".to_owned())
            },
            detail: Some(detail),
            thumbnail: None,
            tint: None,
            icon: Some(icon),
            selected: browser.is_selected(&entry.path),
            accent: Some(icon.color()),
            tag: asset_kind::extension_tag(&entry.name),
        };
        rows = rows.push(asset_preview::row(
            id::file_browser_row(index),
            preview,
            emit(Edit::Press(index)),
            emit(Edit::Noop),
            Some(emit(Edit::Open(index))),
        ));
    }
    let list = container(scroll::vertical(rows))
        .width(Length::Fill)
        .height(Length::Fill)
        .padding(3)
        .style(theme::surface);

    // Footer: filters, selection, confirm.
    let search_emit = emit.clone();
    let hidden_emit = emit.clone();
    let supported_emit = emit.clone();
    let filters = row![
        container(text_field::search(
            id::FILEBROWSER_SEARCH,
            "Filter this folder…",
            &browser.search,
            move |value| search_emit(Edit::Search(value)),
        ))
        .width(Length::Fill),
        checkbox::property(
            id::FILEBROWSER_SUPPORTED,
            "Only importable types",
            browser.only_supported,
            move |on| supported_emit(Edit::Supported(on)),
        ),
        checkbox::property(
            id::FILEBROWSER_HIDDEN,
            "Hidden files",
            browser.show_hidden,
            move |on| hidden_emit(Edit::Hidden(on)),
        ),
    ]
    .spacing(10)
    .align_y(iced::Alignment::Center);

    let confirm = if browser.selected.is_empty() {
        confirm_label.to_owned()
    } else {
        format!("{confirm_label} ({})", browser.selected.len())
    };
    let actions = row![
        text(format!("Selected: {}", browser.summary()))
            .size(10)
            .color(theme::TEXT_MUTED),
        space::horizontal(),
        button::toolbar(
            id::FILEBROWSER_SELECT_SUPPORTED,
            "Select all here",
            emit(Edit::SelectSupported),
        ),
        button::toolbar(id::FILEBROWSER_CLEAR, "Clear", emit(Edit::Clear)),
        button::primary(id::FILEBROWSER_CONFIRM, confirm, emit(Edit::Confirm)),
        button::toolbar(id::FILEBROWSER_CANCEL, "Cancel", emit(Edit::Close)),
    ]
    .spacing(6)
    .align_y(iced::Alignment::Center);

    let body = column![
        navigation,
        row![sidebar, list].spacing(6).height(Length::Fill),
        filters,
        actions,
    ]
    .spacing(8)
    .padding(12)
    .height(Length::Fill);

    container(column![header, body])
        .id(id::FILEBROWSER)
        .width(Length::Fixed(760.0))
        .height(Length::Fixed(540.0))
        .style(theme::menu)
        .into()
}

//! Asset browser: folders first, breadcrumbs and grid/list presentation.
//!
//! Navigation is derived from the relative paths the engine already exposes,
//! so no extra C ABI call is needed to enter a folder.

use crate::uikit::components::asset_preview::CARD_WIDTH;
use crate::uikit::components::{
    asset_preview, breadcrumb, button, scroll, text_field, toggle_chip, tree_view,
};
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::theme;
use doppio_engine::AssetInfo;
use iced::widget::{column, container, responsive, row, text, Column, Row};
use iced::{Element, Length};
use std::rc::Rc;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ViewMode {
    #[default]
    Grid,
    List,
}

/// The inline editor under the toolbar.
#[derive(Debug, Clone, PartialEq)]
pub enum FileEdit {
    /// A new folder in the browsed directory, with the name typed so far.
    NewFolder(String),
    /// Renaming the asset at this path, with the name typed so far.
    Rename(String, String),
    /// Asking before moving this asset (a display name) to the trash.
    ConfirmDelete(String),
}

/// What the file bar can do.
#[derive(Debug, Clone, PartialEq)]
pub enum FileAction {
    NewFolder,
    Rename,
    Delete,
    Text(String),
    Commit,
    Cancel,
    ConfirmDelete,
}

pub struct View<'a> {
    pub edit: Option<&'a FileEdit>,
    pub assets: &'a [AssetInfo],
    pub thumbnails: &'a [Option<iced::widget::image::Handle>],
    pub tints: &'a [Option<iced::Color>],
    pub search: &'a str,
    pub directory: &'a str,
    pub mode: ViewMode,
    pub selected: Option<usize>,
    /// Folder paths collapsed in the tree.
    pub collapsed: &'a std::collections::HashSet<String>,
    pub tree_open: bool,
}

pub struct Callbacks<'a, Message> {
    pub on_search: Box<dyn Fn(String) -> Message + 'a>,
    pub on_press: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_release: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_double_click: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_folder: Box<dyn Fn(String) -> Message + 'a>,
    pub on_crumb: Box<dyn Fn(String) -> Message + 'a>,
    pub on_up: Message,
    pub on_root: Message,
    pub on_refresh: Message,
    pub on_mode: Box<dyn Fn(ViewMode) -> Message + 'a>,
    pub on_new: Message,
    pub on_import: Message,
    pub on_tree_toggle: Box<dyn Fn(String) -> Message + 'a>,
    pub on_noop: Message,
    pub on_tree_panel: Message,
    pub on_file: Box<dyn Fn(FileAction) -> Message + 'a>,
}

/// Canonical relative directory: no leading/trailing slashes, `/` separators.
pub fn normalize_directory(path: &str) -> String {
    path.replace('\\', "/")
        .split('/')
        .filter(|segment| !segment.is_empty() && *segment != ".")
        .collect::<Vec<_>>()
        .join("/")
}

/// Parent folder of a relative asset path (`""` at the root).
pub fn parent_directory(path: &str) -> &str {
    let path = path.trim_end_matches('/');
    path.rfind('/').map(|index| &path[..index]).unwrap_or("")
}

/// Assets visible at the current level, folders first. A non-empty search
/// looks through the whole asset root so users do not have to navigate first.
pub fn filtered_entries<'a>(
    assets: &'a [AssetInfo],
    directory: &str,
    search: &str,
) -> Vec<(usize, &'a AssetInfo)> {
    let directory = normalize_directory(directory);
    let needle = search.trim().to_lowercase();
    let mut entries: Vec<(usize, &AssetInfo)> = assets
        .iter()
        .enumerate()
        .filter(|(_, asset)| {
            if asset.name.starts_with('.') {
                return false;
            }
            if !needle.is_empty() {
                return asset.name.to_lowercase().contains(&needle)
                    || asset.relative_path.to_lowercase().contains(&needle);
            }
            parent_directory(&asset.relative_path) == directory
        })
        .collect();
    entries.sort_by(|(index_a, a), (index_b, b)| {
        b.is_directory
            .cmp(&a.is_directory)
            .then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase()))
            .then_with(|| index_a.cmp(index_b))
    });
    entries
}

pub fn visible_count(assets: &[AssetInfo], directory: &str, search: &str) -> usize {
    filtered_entries(assets, directory, search).len()
}

fn entry_icon(kind: &str, is_directory: bool) -> Icon {
    crate::uikit::asset_kind::AssetKind::from_key(if is_directory { "folder" } else { kind }).icon()
}

fn format_size(bytes: u64) -> String {
    if bytes >= 1024 * 1024 {
        format!("{:.1} MB", bytes as f64 / (1024.0 * 1024.0))
    } else if bytes >= 1024 {
        format!("{:.1} KB", bytes as f64 / 1024.0)
    } else {
        format!("{bytes} B")
    }
}

fn entry_detail(asset: &AssetInfo, searching: bool) -> Option<String> {
    if searching {
        return Some(asset.relative_path.clone());
    }
    if asset.is_directory {
        return None;
    }
    if asset.size_bytes > 0 {
        return Some(format_size(asset.size_bytes));
    }
    Some(asset.kind.clone())
}

fn preview_entry<'a>(
    asset: &'a AssetInfo,
    index: usize,
    thumbnails: &'a [Option<iced::widget::image::Handle>],
    tints: &'a [Option<iced::Color>],
    selected: bool,
    searching: bool,
) -> asset_preview::Entry<'a> {
    let icon = entry_icon(&asset.kind, asset.is_directory);
    asset_preview::Entry {
        name: asset.name.clone(),
        kind: asset.kind.clone(),
        detail: entry_detail(asset, searching),
        thumbnail: thumbnails.get(index).and_then(Option::as_ref),
        tint: tints.get(index).copied().flatten(),
        icon: Some(icon),
        selected,
        accent: Some(icon.color()),
        tag: if asset.is_directory {
            String::new()
        } else {
            crate::uikit::asset_kind::extension_tag(&asset.name)
        },
    }
}

/// Crumbs for a directory: `Assets / Textures / Rock`.
fn crumbs(directory: &str) -> Vec<breadcrumb::Crumb> {
    let directory = normalize_directory(directory);
    let mut result = vec![breadcrumb::Crumb {
        id: id::asset_crumb(0),
        label: "Assets".to_owned(),
        path: String::new(),
    }];
    let mut path = String::new();
    for (index, segment) in directory
        .split('/')
        .filter(|segment| !segment.is_empty())
        .enumerate()
    {
        if !path.is_empty() {
            path.push('/');
        }
        path.push_str(segment);
        result.push(breadcrumb::Crumb {
            id: id::asset_crumb(index + 1),
            label: segment.to_owned(),
            path: path.clone(),
        });
    }
    result
}

/// One visible folder row of the tree (root first, then depth-first).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FolderRow {
    pub path: String,
    pub name: String,
    pub depth: usize,
    pub has_children: bool,
    pub expanded: bool,
}

/// Folders derived from the asset list: directory entries plus the parents of
/// every file, so the tree works even if the engine omits empty ancestors.
pub fn folder_rows(
    assets: &[AssetInfo],
    collapsed: &std::collections::HashSet<String>,
) -> Vec<FolderRow> {
    let mut folders = std::collections::BTreeSet::new();
    for asset in assets {
        if asset.name.starts_with('.') {
            continue;
        }
        let path = normalize_directory(&asset.relative_path);
        let mut current = if asset.is_directory {
            path.as_str()
        } else {
            parent_directory(&path)
        };
        while !current.is_empty() {
            folders.insert(current.to_owned());
            current = parent_directory(current);
        }
    }
    let has_children = |path: &str| {
        folders
            .iter()
            .any(|other| parent_directory(other) == path && other != path)
    };
    let mut rows = vec![FolderRow {
        path: String::new(),
        name: "Assets".to_owned(),
        depth: 0,
        has_children: !folders.is_empty(),
        expanded: !collapsed.contains(""),
    }];
    // BTreeSet order puts every parent right before its children.
    for path in &folders {
        let mut hidden = false;
        let mut ancestor = parent_directory(path);
        loop {
            if collapsed.contains(ancestor) {
                hidden = true;
                break;
            }
            if ancestor.is_empty() {
                break;
            }
            ancestor = parent_directory(ancestor);
        }
        if hidden {
            continue;
        }
        rows.push(FolderRow {
            name: path.rsplit('/').next().unwrap_or(path).to_owned(),
            depth: path.split('/').count(),
            has_children: has_children(path),
            expanded: !collapsed.contains(path.as_str()),
            path: path.clone(),
        });
    }
    rows
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    callbacks: Callbacks<'a, Message>,
) -> Element<'a, Message> {
    let Callbacks {
        on_search,
        on_press,
        on_release,
        on_double_click,
        on_folder,
        on_crumb,
        on_up,
        on_root,
        on_refresh,
        on_mode,
        on_new,
        on_import,
        on_tree_toggle,
        on_noop,
        on_tree_panel,
        on_file,
    } = callbacks;
    let on_press_box = on_press;
    let on_release_box = on_release;
    let on_double_click_box = on_double_click;
    let on_folder_rc: Rc<dyn Fn(String) -> Message + 'a> = Rc::from(on_folder);
    let on_mode = &on_mode;
    let directory = normalize_directory(view.directory);
    let needle = view.search.trim();
    let searching = !needle.is_empty();
    let all_entries = filtered_entries(view.assets, &directory, needle);
    let count = all_entries.len();
    const MAX_VISIBLE: usize = 160;
    let entries = &all_entries[..count.min(MAX_VISIBLE)];

    let trail = breadcrumb::view(crumbs(&directory), on_crumb);
    let navigation = Row::new()
        .push(button::primary(id::ASSET_NEW, "+ New", on_new))
        .push(button::toolbar(id::ASSET_IMPORT, "Import", on_import))
        .push(toggle_chip::view(
            id::ASSET_TREE_TOGGLE,
            "▤",
            view.tree_open,
            theme::ACCENT,
            on_tree_panel,
        ))
        .push(button::compact(id::ASSET_UP, "↑", on_up.clone()))
        .push(button::compact(id::ASSET_ROOT, "Root", on_root.clone()))
        .push(container(trail).width(Length::Fill))
        .spacing(4)
        .align_y(iced::Alignment::Center);

    let search = text_field::search(
        id::ASSET_SEARCH,
        "Search project assets…",
        view.search,
        on_search,
    );
    let count_label = text(if count == 1 {
        "1 item".to_owned()
    } else if count > MAX_VISIBLE {
        format!("{MAX_VISIBLE} of {count}")
    } else {
        format!("{count} items")
    })
    .size(9)
    .color(theme::TEXT_MUTED);
    let grid = toggle_chip::view(
        id::ASSET_VIEW_GRID,
        "▦",
        view.mode == ViewMode::Grid,
        theme::ACCENT,
        (on_mode)(ViewMode::Grid),
    );
    let list = toggle_chip::view(
        id::ASSET_VIEW_LIST,
        "☰",
        view.mode == ViewMode::List,
        theme::ACCENT,
        (on_mode)(ViewMode::List),
    );
    let actions = Row::new()
        .push(count_label)
        .push(iced::widget::space::horizontal())
        .push(button::compact(id::ASSET_REFRESH, "⟳", on_refresh.clone()))
        .push(grid)
        .push(list)
        .width(Length::Fill)
        .spacing(4)
        .align_y(iced::Alignment::Center);

    let content: Element<'a, Message> = if entries.is_empty() {
        container(
            column![
                text(if searching {
                    "No assets match this search."
                } else {
                    "This folder is empty."
                })
                .size(11)
                .color(theme::TEXT_MUTED),
                button::compact(id::ASSET_REFRESH, "Refresh", on_refresh.clone()),
            ]
            .spacing(6)
            .align_x(iced::Alignment::Center),
        )
        .id(id::ASSET_EMPTY)
        .width(Length::Fill)
        .padding(18)
        .center_x(Length::Fill)
        .into()
    } else {
        let mode = view.mode;
        let assets_len = entries.len();
        let entries: Vec<(usize, &'a AssetInfo)> = entries.to_vec();
        let thumbnails = view.thumbnails;
        let tints = view.tints;
        let selected = view.selected;
        let on_press: Rc<dyn Fn(usize) -> Message + 'a> = Rc::from(on_press_box);
        let on_release: Rc<dyn Fn(usize) -> Message + 'a> = Rc::from(on_release_box);
        let on_double_click: Rc<dyn Fn(usize) -> Message + 'a> = Rc::from(on_double_click_box);
        let on_folder = on_folder_rc.clone();
        let _ = assets_len;
        // The grid reflows with the pane: as many tiles per row as fit.
        responsive(move |size| {
            let (on_press, on_release, on_double_click, on_folder) = (
                on_press.clone(),
                on_release.clone(),
                on_double_click.clone(),
                on_folder.clone(),
            );
            let open = move |asset: &AssetInfo, index: usize| -> Message {
                if asset.is_directory {
                    (on_folder)(asset.relative_path.clone())
                } else {
                    (on_double_click)(index)
                }
            };
            match mode {
                ViewMode::Grid => {
                    let gap = 4.0;
                    let usable = (size.width - 14.0).max(CARD_WIDTH);
                    let columns = (((usable + gap) / (CARD_WIDTH + gap)).floor() as usize).max(1);
                    let mut rows = Column::new().spacing(gap);
                    for chunk in entries.chunks(columns) {
                        let mut row = Row::new().spacing(gap);
                        for (index, asset) in chunk.iter() {
                            let entry = preview_entry(
                                asset,
                                *index,
                                thumbnails,
                                tints,
                                selected == Some(*index),
                                searching,
                            );
                            row = row.push(asset_preview::card(
                                id::asset_card(*index),
                                entry,
                                (on_press)(*index),
                                (on_release)(*index),
                                Some(open(asset, *index)),
                            ));
                        }
                        rows = rows.push(row);
                    }
                    scroll::vertical(rows).width(Length::Fill).into()
                }
                ViewMode::List => {
                    let mut rows = Column::new().spacing(1);
                    for (index, asset) in entries.iter().copied() {
                        let entry = preview_entry(
                            asset,
                            index,
                            thumbnails,
                            tints,
                            selected == Some(index),
                            searching,
                        );
                        rows = rows.push(asset_preview::row(
                            id::asset_row(index),
                            entry,
                            (on_press)(index),
                            (on_release)(index),
                            Some(open(asset, index)),
                        ));
                    }
                    scroll::vertical(rows).width(Length::Fill).into()
                }
            }
        })
        .into()
    };

    let tree = {
        let mut items = Vec::new();
        for folder in folder_rows(view.assets, view.collapsed) {
            let path = folder.path.clone();
            items.push(tree_view::Item {
                id: id::asset_tree_row(&folder.path),
                label: folder.name,
                icon: Icon::Folder,
                depth: folder.depth,
                has_children: folder.has_children,
                expanded: folder.expanded,
                selected: directory == folder.path,
                hovered: false,
                drop_target: false,
                trailing: None,
                extra: None,
                on_toggle: (on_tree_toggle)(path.clone()),
                on_press: (on_folder_rc)(path),
                on_release: on_noop.clone(),
                on_enter: on_noop.clone(),
                on_exit: on_noop.clone(),
                on_context: on_noop.clone(),
            });
        }
        container(scroll::vertical(tree_view::view(items)))
            .id(id::ASSET_TREE)
            .width(Length::Fixed(190.0))
            .height(Length::Fill)
            .padding(3)
            .style(theme::surface)
    };

    let on_file: Rc<dyn Fn(FileAction) -> Message + 'a> = Rc::from(on_file);
    let file_bar: Element<'a, Message> = match view.edit {
        None => {
            let mut bar = Row::new().push(button::toolbar(
                id::ASSET_NEW_FOLDER,
                "New folder",
                (on_file)(FileAction::NewFolder),
            ));
            if view.selected.is_some() {
                bar = bar
                    .push(button::toolbar(
                        id::ASSET_RENAME,
                        "Rename",
                        (on_file)(FileAction::Rename),
                    ))
                    .push(button::toolbar(
                        id::ASSET_DELETE,
                        "Delete",
                        (on_file)(FileAction::Delete),
                    ));
            }
            bar.spacing(4).align_y(iced::Alignment::Center).into()
        }
        Some(FileEdit::ConfirmDelete(label)) => Row::new()
            .push(
                text(format!("Move “{label}” to the trash?"))
                    .size(11)
                    .color(theme::TEXT),
            )
            .push(iced::widget::space::horizontal())
            .push(button::danger(
                id::ASSET_DELETE_CONFIRM,
                "Delete",
                (on_file)(FileAction::ConfirmDelete),
            ))
            .push(button::toolbar(
                id::ASSET_EDIT_CANCEL,
                "Cancel",
                (on_file)(FileAction::Cancel),
            ))
            .spacing(4)
            .align_y(iced::Alignment::Center)
            .into(),
        Some(edit) => {
            let (placeholder, value) = match edit {
                FileEdit::NewFolder(value) => ("New folder name…", value.as_str()),
                FileEdit::Rename(_, value) => ("New name…", value.as_str()),
                FileEdit::ConfirmDelete(_) => ("", ""),
            };
            let typed = on_file.clone();
            let submit = on_file.clone();
            Row::new()
                .push(text_field::submit(
                    id::ASSET_EDIT_NAME,
                    placeholder,
                    value,
                    move |text| (typed)(FileAction::Text(text)),
                    (submit)(FileAction::Commit),
                ))
                .push(button::primary(
                    id::ASSET_EDIT_OK,
                    "OK",
                    (on_file)(FileAction::Commit),
                ))
                .push(button::toolbar(
                    id::ASSET_EDIT_CANCEL,
                    "Cancel",
                    (on_file)(FileAction::Cancel),
                ))
                .spacing(4)
                .align_y(iced::Alignment::Center)
                .into()
        }
    };

    let files = column![navigation, file_bar, search, actions, content]
        .spacing(4)
        .width(Length::Fill)
        .height(Length::Fill);
    if view.tree_open {
        row![tree, files].spacing(6).height(Length::Fill).into()
    } else {
        files.into()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn asset(name: &str, path: &str, kind: &str, directory: bool) -> AssetInfo {
        AssetInfo {
            name: name.to_owned(),
            relative_path: path.to_owned(),
            kind: kind.to_owned(),
            size_bytes: 0,
            is_directory: directory,
        }
    }

    #[test]
    fn folders_are_listed_first_and_navigation_stays_relative() {
        let assets = vec![
            asset("brick.png", "Textures/brick.png", "image", false),
            asset("Textures", "Textures", "folder", true),
            asset("rock.mat", "Materials/rock.mat", "material", false),
            asset("Materials", "Materials", "folder", true),
            asset("root.mat", "root.mat", "material", false),
        ];
        let root = filtered_entries(&assets, "", "");
        assert_eq!(root.len(), 3);
        assert!(root[0].1.is_directory);
        assert!(root[1].1.is_directory);
        assert_eq!(root[2].1.relative_path, "root.mat");

        let textures = filtered_entries(&assets, "Textures", "");
        assert_eq!(textures.len(), 1);
        assert_eq!(textures[0].1.name, "brick.png");
        assert_eq!(parent_directory("Textures/brick.png"), "Textures");
        assert_eq!(normalize_directory("/Textures//"), "Textures");
    }

    #[test]
    fn search_walks_the_whole_tree() {
        let assets = vec![
            asset("brick.png", "Textures/brick.png", "image", false),
            asset("rock.mat", "Materials/rock.mat", "material", false),
        ];
        let found = filtered_entries(&assets, "", "brick");
        assert_eq!(found.len(), 1);
        assert_eq!(found[0].1.relative_path, "Textures/brick.png");
    }
}

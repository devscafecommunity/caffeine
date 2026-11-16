//! Asset browser: folders first, breadcrumbs and grid/list presentation.
//!
//! Navigation is derived from the relative paths the engine already exposes,
//! so no extra C ABI call is needed to enter a folder.

use crate::uikit::components::{asset_preview, breadcrumb, button, text_field, toggle_chip};
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::theme;
use doppio_engine::AssetInfo;
use iced::widget::{column, container, scrollable, text, Column, Row};
use iced::{Element, Length};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ViewMode {
    #[default]
    Grid,
    List,
}

pub struct View<'a> {
    pub assets: &'a [AssetInfo],
    pub thumbnails: &'a [Option<iced::widget::image::Handle>],
    pub tints: &'a [Option<iced::Color>],
    pub search: &'a str,
    pub directory: &'a str,
    pub mode: ViewMode,
    pub selected: Option<usize>,
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
    if is_directory {
        return Icon::Folder;
    }
    match kind {
        "image" => Icon::Texture,
        "material" => Icon::Material,
        "mesh" => Icon::Mesh,
        "audio" => Icon::Audio,
        "script" => Icon::Script,
        "asset" => Icon::Scene,
        "animation" => Icon::Player,
        _ => Icon::File,
    }
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
    asset_preview::Entry {
        name: asset.name.clone(),
        kind: asset.kind.clone(),
        detail: entry_detail(asset, searching),
        thumbnail: thumbnails.get(index).and_then(Option::as_ref),
        tint: tints.get(index).copied().flatten(),
        icon: Some(entry_icon(&asset.kind, asset.is_directory)),
        selected,
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
    } = callbacks;
    let on_press = &on_press;
    let on_release = &on_release;
    let on_double_click = &on_double_click;
    let on_folder = &on_folder;
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
        match view.mode {
            ViewMode::Grid => {
                let mut rows = Column::new().spacing(4);
                for chunk in entries.chunks(2) {
                    let mut row = Row::new().spacing(4);
                    for (index, asset) in chunk.iter() {
                        let entry = preview_entry(
                            asset,
                            *index,
                            view.thumbnails,
                            view.tints,
                            view.selected == Some(*index),
                            searching,
                        );
                        let (on_press, on_release) = if asset.is_directory {
                            let navigate = (on_folder)(asset.relative_path.clone());
                            (navigate.clone(), navigate)
                        } else {
                            ((on_press)(*index), (on_release)(*index))
                        };
                        row = row.push(asset_preview::card(
                            id::asset_card(*index),
                            entry,
                            on_press,
                            on_release,
                            (!asset.is_directory).then(|| (on_double_click)(*index)),
                        ));
                    }
                    rows = rows.push(row);
                }
                scrollable(rows).into()
            }
            ViewMode::List => {
                let mut rows = Column::new().spacing(1);
                for (index, asset) in entries.iter().copied() {
                    let entry = preview_entry(
                        asset,
                        index,
                        view.thumbnails,
                        view.tints,
                        view.selected == Some(index),
                        searching,
                    );
                    let (on_press, on_release) = if asset.is_directory {
                        let navigate = (on_folder)(asset.relative_path.clone());
                        (navigate.clone(), navigate)
                    } else {
                        ((on_press)(index), (on_release)(index))
                    };
                    rows = rows.push(asset_preview::row(
                        id::asset_row(index),
                        entry,
                        on_press,
                        on_release,
                        (!asset.is_directory).then(|| (on_double_click)(index)),
                    ));
                }
                scrollable(rows).into()
            }
        }
    };

    column![navigation, search, actions, content]
        .spacing(4)
        .height(Length::Fill)
        .into()
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

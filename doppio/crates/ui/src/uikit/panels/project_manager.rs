//! Project Manager: create, reopen or browse Caffeine projects.
//!
//! Mirrors the archived startup dialog's three tabs while staying inside the
//! Doppio component kit: fields, chips, list rows and primary buttons.

use crate::uikit::components::{button, list_row, text_field, toggle_chip};
use crate::uikit::id;
use crate::uikit::project::ProjectInfo;
use crate::uikit::theme;
use iced::widget::{column, container, row, scrollable, space, text};
use iced::{Element, Length};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Tab {
    Create,
    Recent,
    Browse,
}

impl Tab {
    pub const ALL: [Tab; 3] = [Tab::Create, Tab::Recent, Tab::Browse];

    pub fn label(self) -> &'static str {
        match self {
            Tab::Create => "Create New",
            Tab::Recent => "Open Recent",
            Tab::Browse => "Browse Projects",
        }
    }

    pub fn id(self) -> id::UiId {
        match self {
            Tab::Create => id::PROJECT_TAB_CREATE,
            Tab::Recent => id::PROJECT_TAB_RECENT,
            Tab::Browse => id::PROJECT_TAB_BROWSE,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Template {
    Empty,
    TwoD,
    ThreeD,
}

impl Template {
    pub const ALL: [Template; 3] = [Template::Empty, Template::TwoD, Template::ThreeD];

    pub fn label(self) -> &'static str {
        match self {
            Template::Empty => "Empty",
            Template::TwoD => "2D",
            Template::ThreeD => "3D",
        }
    }

    pub fn description(self) -> &'static str {
        match self {
            Template::Empty => "Blank project with the standard project folders.",
            Template::TwoD => "Starter folders for sprites, tilesets and 2D scenes.",
            Template::ThreeD => "Starter folders for models, textures and 3D scenes.",
        }
    }

    pub fn manifest_name(self) -> &'static str {
        match self {
            Template::Empty => "Empty",
            Template::TwoD => "2D",
            Template::ThreeD => "3D",
        }
    }

    pub fn index(self) -> usize {
        match self {
            Template::Empty => 0,
            Template::TwoD => 1,
            Template::ThreeD => 2,
        }
    }

    pub fn from_index(index: usize) -> Self {
        match index {
            0 => Template::Empty,
            1 => Template::TwoD,
            _ => Template::ThreeD,
        }
    }

    pub fn from_name(name: &str) -> Option<Self> {
        match name.trim().to_ascii_lowercase().as_str() {
            "empty" => Some(Template::Empty),
            "2d" | "two_d" | "two-dimensional" => Some(Template::TwoD),
            "3d" | "three_d" | "three-dimensional" => Some(Template::ThreeD),
            _ => None,
        }
    }
}

#[derive(Debug, Clone)]
pub struct State {
    pub open: bool,
    pub active: Option<ProjectInfo>,
    pub tab: Tab,
    pub name: String,
    pub location: String,
    pub template: Template,
    pub error: Option<String>,
    pub recent: Vec<ProjectInfo>,
    pub recent_search: String,
    pub selected_recent: Option<usize>,
    pub browse_path: String,
    pub browse_results: Vec<ProjectInfo>,
    pub browse_search: String,
    pub selected_browse: Option<usize>,
}

impl State {
    pub fn new(location: String) -> Self {
        Self {
            open: false,
            active: None,
            tab: Tab::Create,
            name: String::new(),
            browse_path: location.clone(),
            location,
            template: Template::ThreeD,
            error: None,
            recent: Vec::new(),
            recent_search: String::new(),
            selected_recent: None,
            browse_results: Vec::new(),
            browse_search: String::new(),
            selected_browse: None,
        }
    }
}

#[derive(Debug, Clone)]
pub enum Edit {
    Open,
    Close,
    Tab(Tab),
    Name(String),
    Location(String),
    Template(usize),
    Create,
    /// Set the fields and create in one agent-friendly step.
    CreateAt {
        name: String,
        location: String,
        template: String,
    },
    DefaultLocation,
    DismissError,
    OpenPath(String),
    RecentSearch(String),
    RecentSelect(usize),
    RecentOpen(usize),
    BrowsePath(String),
    BrowseScan,
    BrowseDefault,
    BrowseSearch(String),
    BrowseSelect(usize),
    BrowseOpen(usize),
}

pub struct Callbacks<'a, Message> {
    pub on_tab: Box<dyn Fn(Tab) -> Message + 'a>,
    pub on_name: Box<dyn Fn(String) -> Message + 'a>,
    pub on_location: Box<dyn Fn(String) -> Message + 'a>,
    pub on_template: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_create: Message,
    pub on_default_location: Message,
    pub on_recent_search: Box<dyn Fn(String) -> Message + 'a>,
    pub on_recent_select: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_recent_open: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_browse_path: Box<dyn Fn(String) -> Message + 'a>,
    pub on_browse_scan: Message,
    pub on_browse_default: Message,
    pub on_browse_search: Box<dyn Fn(String) -> Message + 'a>,
    pub on_browse_select: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_browse_open: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_close: Message,
    pub on_dismiss_error: Message,
}

fn filtered<'a>(projects: &'a [ProjectInfo], search: &str) -> Vec<(usize, &'a ProjectInfo)> {
    let needle = search.trim().to_lowercase();
    projects
        .iter()
        .enumerate()
        .filter(|(_, project)| {
            needle.is_empty()
                || project.name.to_lowercase().contains(&needle)
                || project.display_path().to_lowercase().contains(&needle)
        })
        .collect()
}

fn selected_path(projects: &[ProjectInfo], selected: Option<usize>) -> String {
    selected
        .and_then(|index| projects.get(index))
        .map(ProjectInfo::display_path)
        .unwrap_or_else(|| "Select a project to open.".to_owned())
}

fn project_rows<'a, Message: Clone + 'a>(
    projects: Vec<(usize, &'a ProjectInfo)>,
    selected: Option<usize>,
    row_id: fn(usize) -> id::UiId,
    on_select: &dyn Fn(usize) -> Message,
    on_open: &dyn Fn(usize) -> Message,
    empty: &'a str,
) -> Element<'a, Message> {
    if projects.is_empty() {
        return container(text(empty).size(10).color(theme::TEXT_MUTED))
            .padding([8, 10])
            .into();
    }
    let mut rows = column![].spacing(1);
    for (index, project) in projects.into_iter().take(200) {
        let label = format!("{}  ·  {}", project.name, project.template);
        rows = rows.push(list_row::view(
            row_id(index),
            label,
            selected == Some(index),
            on_select(index),
            on_select(index),
            on_open(index),
        ));
    }
    scrollable(rows).height(Length::Fill).into()
}

pub fn view<'a, Message: Clone + 'a>(
    state: &'a State,
    callbacks: Callbacks<'a, Message>,
) -> Element<'a, Message> {
    let Callbacks {
        on_tab,
        on_name,
        on_location,
        on_template,
        on_create,
        on_default_location,
        on_recent_search,
        on_recent_select,
        on_recent_open,
        on_browse_path,
        on_browse_scan,
        on_browse_default,
        on_browse_search,
        on_browse_select,
        on_browse_open,
        on_close,
        on_dismiss_error,
    } = callbacks;
    let on_tab = &on_tab;
    let on_recent_select = &on_recent_select;
    let on_recent_open = &on_recent_open;
    let on_browse_select = &on_browse_select;
    let on_browse_open = &on_browse_open;

    let close = button::compact(id::PROJECT_CLOSE, "×", on_close);
    let header = container(
        row![
            column![
                text("Project Manager").size(16).color(theme::TEXT),
                text("Create a project or reopen one from disk.")
                    .size(10)
                    .color(theme::TEXT_MUTED),
            ]
            .spacing(1),
            space::horizontal(),
            close,
        ]
        .align_y(iced::Alignment::Center),
    )
    .padding([6, 10])
    .style(theme::panel_title);

    let mut tabs = row![].spacing(3);
    for tab in Tab::ALL {
        tabs = tabs.push(toggle_chip::view(
            tab.id(),
            tab.label(),
            state.tab == tab,
            theme::ACCENT,
            (on_tab)(tab),
        ));
    }
    let tabs = container(tabs).padding([4, 10]);

    let body: Element<'a, Message> = match state.tab {
        Tab::Create => {
            let location = text_field::stacked(
                id::PROJECT_LOCATION,
                "Location",
                &state.location,
                on_location,
            );
            let default_location = button::compact(
                id::PROJECT_DEFAULT_LOCATION,
                "Default folder",
                on_default_location,
            );
            let mut templates = row![].spacing(4);
            for (index, template) in Template::ALL.into_iter().enumerate() {
                templates = templates.push(toggle_chip::view(
                    id::project_template(index),
                    template.label(),
                    state.template == template,
                    theme::ACCENT,
                    (on_template)(index),
                ));
            }
            column![
                text_field::stacked(
                    id::PROJECT_NAME,
                    "Project name",
                    &state.name,
                    on_name,
                ),
                row![location, default_location]
                    .spacing(4)
                    .align_y(iced::Alignment::End),
                text("Template").size(10).color(theme::TEXT_MUTED),
                templates,
                text(state.template.description())
                    .size(9)
                    .color(theme::TEXT_MUTED),
                space().height(Length::Fixed(8.0)),
                button::primary(id::PROJECT_CREATE, "Create & Open", on_create),
                text(if state.name.trim().is_empty() {
                    "Enter a name to create project.caffeine with assets/raw, scenes, materials and texture_sets.".to_owned()
                } else {
                    format!(
                        "Creates {}/{}/project.caffeine with assets/raw, scenes, materials and texture_sets.",
                        state.location.trim_end_matches(['/', '\\']),
                        state.name.trim()
                    )
                })
                .size(9)
                .color(theme::TEXT_MUTED),
            ]
            .spacing(6)
            .into()
        }
        Tab::Recent => {
            let entries = filtered(&state.recent, &state.recent_search);
            let path = selected_path(&state.recent, state.selected_recent);
            let rows = project_rows(
                entries,
                state.selected_recent,
                id::project_recent_row,
                on_recent_select,
                on_recent_open,
                "No recent projects yet. Create one in the Create New tab.",
            );
            column![
                text_field::search(
                    id::PROJECT_RECENT_SEARCH,
                    "Search recent projects…",
                    &state.recent_search,
                    on_recent_search,
                ),
                container(rows).height(Length::Fixed(270.0)),
                text(path).size(9).color(theme::TEXT_MUTED),
                row![
                    button::primary(
                        id::PROJECT_RECENT_OPEN,
                        "Open Selected",
                        (on_recent_open)(state.selected_recent.unwrap_or(usize::MAX)),
                    ),
                    button::compact(
                        id::PROJECT_BROWSE_DEFAULT,
                        "Browse projects folder",
                        (on_tab)(Tab::Browse),
                    ),
                ]
                .spacing(6)
                .align_y(iced::Alignment::Center),
            ]
            .spacing(6)
            .into()
        }
        Tab::Browse => {
            let entries = filtered(&state.browse_results, &state.browse_search);
            let path = selected_path(&state.browse_results, state.selected_browse);
            let rows = project_rows(
                entries,
                state.selected_browse,
                id::project_browse_row,
                on_browse_select,
                on_browse_open,
                "Choose a folder and press Scan to find project.caffeine files.",
            );
            column![
                text_field::stacked(
                    id::PROJECT_BROWSE_PATH,
                    "Folder",
                    &state.browse_path,
                    on_browse_path,
                ),
                row![
                    button::compact(id::PROJECT_BROWSE_SCAN, "Scan", on_browse_scan),
                    button::compact(
                        id::PROJECT_BROWSE_DEFAULT,
                        "Default folder",
                        on_browse_default,
                    ),
                    text(format!("{} projects", state.browse_results.len()))
                        .size(9)
                        .color(theme::TEXT_MUTED),
                ]
                .spacing(6)
                .align_y(iced::Alignment::Center),
                text_field::search(
                    id::PROJECT_BROWSE_SEARCH,
                    "Search scanned projects…",
                    &state.browse_search,
                    on_browse_search,
                ),
                container(rows).height(Length::Fixed(270.0)),
                text(path).size(9).color(theme::TEXT_MUTED),
                button::primary(
                    id::PROJECT_BROWSE_OPEN,
                    "Open Selected",
                    (on_browse_open)(state.selected_browse.unwrap_or(usize::MAX)),
                ),
            ]
            .spacing(6)
            .into()
        }
    };

    let error = state.error.as_ref().map(|message| {
        container(
            row![
                text(message).size(10).color(theme::DANGER),
                space::horizontal(),
                button::compact(id::PROJECT_ERROR_DISMISS, "Dismiss", on_dismiss_error),
            ]
            .spacing(6)
            .align_y(iced::Alignment::Center),
        )
        .padding([5, 10])
        .style(theme::surface)
    });

    let mut panel = column![header, tabs, container(body).padding([0, 10])].spacing(0);
    if let Some(error) = error {
        panel = panel.push(error);
    }
    container(panel)
        .id(id::PROJECT_MANAGER)
        .width(Length::Fixed(660.0))
        .height(Length::Fixed(520.0))
        .style(theme::menu)
        .into()
}

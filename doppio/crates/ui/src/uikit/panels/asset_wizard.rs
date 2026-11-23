//! Assets wizard: create assets from templates, import files and pack the
//! raw tree with `caf-pack`. A modal dialog like the Project Manager.

use crate::uikit::asset_wizard::Template;
use crate::uikit::components::{button, checkbox, list_row, scroll, text_field, toggle_chip};
use crate::uikit::id;
use crate::uikit::theme;
use iced::widget::{column, container, row, space, text};
use iced::{Element, Length};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Tab {
    Create,
    Import,
    Pack,
}

impl Tab {
    pub const ALL: [Tab; 3] = [Tab::Create, Tab::Import, Tab::Pack];

    pub fn label(self) -> &'static str {
        match self {
            Tab::Create => "Create",
            Tab::Import => "Import",
            Tab::Pack => "Pack (.cap)",
        }
    }

    fn id(self) -> id::UiId {
        match self {
            Tab::Create => id::WIZARD_TAB_CREATE,
            Tab::Import => id::WIZARD_TAB_IMPORT,
            Tab::Pack => id::WIZARD_TAB_PACK,
        }
    }
}

#[derive(Debug, Clone)]
pub struct State {
    pub open: bool,
    pub tab: Tab,
    pub template: Template,
    pub name: String,
    pub import_input: String,
    pub sources: Vec<String>,
    pub generate_ids: bool,
    pub status: Option<Status>,
    pub packing: bool,
}

#[derive(Debug, Clone)]
pub struct Status {
    pub text: String,
    pub ok: bool,
}

impl Default for State {
    fn default() -> Self {
        Self {
            open: false,
            tab: Tab::Create,
            template: Template::Folder,
            name: String::new(),
            import_input: String::new(),
            sources: Vec::new(),
            generate_ids: false,
            status: None,
            packing: false,
        }
    }
}

#[derive(Debug, Clone)]
pub enum Edit {
    Open(Tab),
    Close,
    Tab(Tab),
    Template(usize),
    Name(String),
    Create,
    ImportInput(String),
    ImportAdd,
    /// Opens the file browser to choose what to import.
    ImportBrowse,
    ImportRemove(usize),
    ImportRun,
    GenerateIds(bool),
    PackRun,
    PackDone(Result<String, String>),
}

/// Read-only context the window supplies for rendering.
pub struct Context {
    pub destination: String,
    pub packer: Option<String>,
    pub pack_output: String,
}

pub fn view<'a, Message: Clone + 'a>(
    state: &'a State,
    context: Context,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: std::rc::Rc<dyn Fn(Edit) -> Message + 'a> = std::rc::Rc::new(on_edit);

    let header = container(
        row![
            column![
                text("Assets").size(15).color(theme::TEXT),
                text(format!("Destination: {}", context.destination))
                    .size(10)
                    .color(theme::TEXT_MUTED),
            ]
            .spacing(1),
            space::horizontal(),
            button::compact(id::WIZARD_CLOSE, "×", emit(Edit::Close)),
        ]
        .align_y(iced::Alignment::Center),
    )
    .padding([8, 12])
    .style(theme::panel_title);

    let mut tabs = row![].spacing(4);
    for tab in Tab::ALL {
        tabs = tabs.push(toggle_chip::view(
            tab.id(),
            tab.label(),
            state.tab == tab,
            theme::ACCENT,
            emit(Edit::Tab(tab)),
        ));
    }

    let body: Element<'a, Message> = match state.tab {
        Tab::Create => create_tab(state, &emit),
        Tab::Import => import_tab(state, &emit),
        Tab::Pack => pack_tab(state, &context, &emit),
    };

    let mut content = column![
        header,
        container(tabs).padding(iced::Padding {
            top: 8.0,
            right: 12.0,
            bottom: 4.0,
            left: 12.0,
        })
    ]
    .spacing(0);
    content = content.push(container(body).padding([4, 12]).height(Length::Fill));
    if let Some(status) = &state.status {
        content = content.push(
            container(text(&status.text).size(11).color(if status.ok {
                theme::SUCCESS
            } else {
                theme::DANGER
            }))
            .padding([6, 12])
            .width(Length::Fill)
            .style(theme::surface),
        );
    }
    container(content)
        .id(id::WIZARD)
        .width(Length::Fixed(560.0))
        .height(Length::Fixed(460.0))
        .style(theme::menu)
        .into()
}

fn create_tab<'a, Message: Clone + 'a>(
    state: &'a State,
    emit: &std::rc::Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let mut templates = column![].spacing(2);
    for (index, template) in Template::ALL.iter().enumerate() {
        let selected = state.template == *template;
        let on_pick = emit(Edit::Template(index));
        templates = templates.push(list_row::view(
            id::wizard_template(index),
            format!("{}  ·  {}", template.label(), template.description()),
            selected,
            on_pick.clone(),
            on_pick.clone(),
            on_pick,
        ));
    }
    let name_emit = emit.clone();
    let editor = state.template.opens_editor();
    column![
        scroll::vertical(templates).height(Length::Fixed(176.0)),
        text_field::stacked(id::WIZARD_NAME, "Name", &state.name, move |value| {
            name_emit(Edit::Name(value))
        }),
        if editor {
            text("The editor opens on a new document; save it there.")
                .size(10)
                .color(theme::TEXT_MUTED)
        } else {
            text("Created in the destination folder; existing files are never overwritten.")
                .size(10)
                .color(theme::TEXT_MUTED)
        },
        button::primary(
            id::WIZARD_CREATE,
            if editor {
                "Create & Open Editor"
            } else {
                "Create"
            },
            emit(Edit::Create),
        ),
    ]
    .spacing(8)
    .into()
}

fn import_tab<'a, Message: Clone + 'a>(
    state: &'a State,
    emit: &std::rc::Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let input_emit = emit.clone();
    let mut queued = column![].spacing(2);
    if state.sources.is_empty() {
        queued = queued.push(
            text("Nothing queued. Press Browse…, type a path and press Add, or drop files onto the window.")
                .size(10)
                .color(theme::TEXT_MUTED),
        );
    }
    for (index, source) in state.sources.iter().enumerate() {
        let path = std::path::Path::new(source);
        let (badge, tone) = if path.is_dir() {
            ("DIR", theme::WARNING)
        } else {
            match crate::uikit::asset_wizard::classify(path) {
                Some(kind) => (kind.badge(), kind.icon().color()),
                None => ("SKIP", theme::DANGER),
            }
        };
        queued = queued.push(
            row![
                container(text(badge).size(8).color(tone)).width(Length::Fixed(34.0)),
                container(text(source).size(11)).width(Length::Fill),
                button::compact(
                    id::wizard_source(index),
                    "×",
                    emit(Edit::ImportRemove(index))
                ),
            ]
            .spacing(6)
            .align_y(iced::Alignment::Center),
        );
    }
    let count = state.sources.len();
    column![
        row![
            container(text_field::search(
                id::WIZARD_IMPORT_PATH,
                "Path to a file or folder…",
                &state.import_input,
                move |value| input_emit(Edit::ImportInput(value)),
            ))
            .width(Length::Fill),
            button::toolbar(id::WIZARD_IMPORT_ADD, "Add", emit(Edit::ImportAdd)),
            button::primary(
                id::WIZARD_IMPORT_BROWSE,
                "Browse…",
                emit(Edit::ImportBrowse)
            ),
        ]
        .spacing(6),
        container(scroll::vertical(queued))
            .padding(6)
            .height(Length::Fixed(190.0))
            .width(Length::Fill)
            .style(theme::surface),
        accepted_types(),
        button::primary(
            id::WIZARD_IMPORT_RUN,
            if count == 0 {
                "Import".to_owned()
            } else {
                format!("Import {count} item(s)")
            },
            emit(Edit::ImportRun),
        ),
    ]
    .spacing(8)
    .into()
}

/// The file types the importer takes, one line per kind, straight from the shared table.
fn accepted_types<'a, Message: Clone + 'a>() -> Element<'a, Message> {
    let mut lines = column![].spacing(1);
    for kind in crate::uikit::asset_kind::AssetKind::IMPORTABLE {
        let extensions = kind.extensions();
        if extensions.is_empty() {
            continue;
        }
        let list = extensions
            .iter()
            .map(|extension| format!(".{extension}"))
            .collect::<Vec<_>>()
            .join(" ");
        lines = lines.push(
            row![
                text(kind.badge())
                    .size(8)
                    .color(kind.icon().color())
                    .width(Length::Fixed(34.0)),
                text(list).size(9).color(theme::TEXT_MUTED),
            ]
            .spacing(4),
        );
    }
    column![
        text("Accepted types (anything else is skipped and reported):")
            .size(10)
            .color(theme::TEXT_MUTED),
        container(scroll::vertical(lines))
            .height(Length::Fixed(84.0))
            .padding([2, 4])
            .style(theme::surface),
    ]
    .spacing(3)
    .into()
}

fn pack_tab<'a, Message: Clone + 'a>(
    state: &'a State,
    context: &Context,
    emit: &std::rc::Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let packer: Element<'a, Message> = match &context.packer {
        Some(path) => text(format!("caf-pack: {path}"))
            .size(11)
            .color(theme::SUCCESS)
            .into(),
        None => {
            text("caf-pack not found. Build it (cmake --build caf-pack/build) or set CAF_PACK.")
                .size(11)
                .color(theme::WARNING)
                .into()
        }
    };
    let ids_emit = emit.clone();
    column![
        text("Converts the raw asset tree (textures, meshes, audio, .lang) into a memory-mappable .cap archive.")
            .size(11)
            .color(theme::TEXT_MUTED),
        packer,
        text(format!("Output: {}", context.pack_output))
            .size(10)
            .color(theme::TEXT_MUTED),
        checkbox::property(
            id::WIZARD_PACK_IDS,
            "Also generate the asset id header (.hpp)",
            state.generate_ids,
            move |value| ids_emit(Edit::GenerateIds(value)),
        ),
        space::vertical(),
        button::primary(
            id::WIZARD_PACK_RUN,
            if state.packing { "Packing…" } else { "Build .cap" },
            emit(Edit::PackRun),
        ),
    ]
    .spacing(8)
    .height(Length::Fill)
    .into()
}

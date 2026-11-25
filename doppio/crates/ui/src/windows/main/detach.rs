//! **Detach**: turning a pane's tab into a window of its own, and docking it back.
//!
//! Every tab of the editor is a view over the one `App` (there is a single engine and a single set of
//! documents), so a detached tab is just the same view drawn in another operating-system window: moving it
//! out of the dock layout and into [`DetachState::windows`] is all the state it needs. Opening and closing
//! windows are tasks, which only the windowed run (`iced::daemon`) can run, so the requests are queued here
//! and [`App::update_windowed`] turns them into tasks after each message. Closing a detached window docks
//! its tab back; closing the main window quits.

use super::{theme, App, LogLevel, Message, PaneKind};
use crate::uikit::components::{button, panel};
use crate::uikit::id;
use iced::widget::{column, container, row, space, text};
use iced::window;
use iced::{Element, Length, Task};

#[derive(Default)]
pub(super) struct DetachState {
    /// True only in the windowed run: the dock can open windows. Elsewhere (tests, the agent bridge)
    /// the Detach button is not offered and a request is refused with a message.
    pub supported: bool,
    pub main: Option<window::Id>,
    /// The windows that hold a detached tab.
    pub windows: Vec<(window::Id, PaneKind)>,
    pub pending_open: Vec<PaneKind>,
    pub pending_close: Vec<window::Id>,
    pub pending_focus: Vec<window::Id>,
    pub quit: bool,
}

impl DetachState {
    pub fn kind_of(&self, id: window::Id) -> Option<PaneKind> {
        self.windows
            .iter()
            .find(|(window, _)| *window == id)
            .map(|(_, kind)| *kind)
    }

    pub fn window_of(&self, kind: PaneKind) -> Option<window::Id> {
        self.windows
            .iter()
            .find(|(_, held)| *held == kind)
            .map(|(window, _)| *window)
    }
}

/// The settings of a window that holds one tab.
fn detached_settings(kind: PaneKind) -> window::Settings {
    let (width, height) = match kind {
        PaneKind::Viewport => (1100.0, 720.0),
        PaneKind::Console | PaneKind::Assets | PaneKind::Profiler => (900.0, 520.0),
        _ => (520.0, 760.0),
    };
    window::Settings {
        size: iced::Size::new(width, height),
        min_size: Some(iced::Size::new(320.0, 240.0)),
        ..window::Settings::default()
    }
}

impl PaneKind {
    /// A stable key for ids and agents (`audio`, `material_generator`…).
    pub(super) fn key(self) -> &'static str {
        match self {
            PaneKind::Hierarchy => "hierarchy",
            PaneKind::Assets => "assets",
            PaneKind::Viewport => "viewport",
            PaneKind::Generator => "material_generator",
            PaneKind::TextureGenerator => "texture_generator",
            PaneKind::NatureGenerator => "nature",
            PaneKind::WaterGenerator => "water",
            PaneKind::TerrainGenerator => "terrain",
            PaneKind::Inspector => "inspector",
            PaneKind::Photon => "photon",
            PaneKind::Environment => "environment",
            PaneKind::Audio => "audio",
            PaneKind::Performance => "performance",
            PaneKind::TerrainBrush => "terrain_brush",
            PaneKind::TerrainPaint => "terrain_paint",
            PaneKind::TerrainVegetation => "terrain_vegetation",
            PaneKind::PhaseShift => "phaseshift",
            PaneKind::Rig2d => "rig2d",
            PaneKind::Profiler => "profiler",
            PaneKind::Material => "material",
            PaneKind::TextureSet => "texture_set",
            PaneKind::Script => "script",
            PaneKind::DryadNodes => "dryad_nodes",
            PaneKind::Console => "console",
            PaneKind::Gameplay => "gameplay",
            PaneKind::CameraPreview => "camera",
        }
    }

    /// The kind with this key or title (case-insensitive).
    #[cfg_attr(not(feature = "agent"), allow(dead_code))]
    pub(super) fn from_name(name: &str) -> Option<PaneKind> {
        Self::ALL.into_iter().find(|kind| {
            kind.key().eq_ignore_ascii_case(name) || kind.title().eq_ignore_ascii_case(name)
        })
    }

    /// Every kind of tab.
    pub(super) const ALL: [PaneKind; 26] = [
        PaneKind::Hierarchy,
        PaneKind::Assets,
        PaneKind::Viewport,
        PaneKind::Generator,
        PaneKind::TextureGenerator,
        PaneKind::NatureGenerator,
        PaneKind::WaterGenerator,
        PaneKind::TerrainGenerator,
        PaneKind::Inspector,
        PaneKind::Photon,
        PaneKind::Environment,
        PaneKind::Audio,
        PaneKind::Performance,
        PaneKind::TerrainBrush,
        PaneKind::TerrainPaint,
        PaneKind::TerrainVegetation,
        PaneKind::PhaseShift,
        PaneKind::Rig2d,
        PaneKind::Profiler,
        PaneKind::Material,
        PaneKind::TextureSet,
        PaneKind::Script,
        PaneKind::DryadNodes,
        PaneKind::Console,
        PaneKind::Gameplay,
        PaneKind::CameraPreview,
    ];
}

impl App {
    /// Takes `kind` out of the dock layout. A pane that held only that tab is closed; the last pane of the
    /// editor is never removed. Returns whether the tab left the layout.
    fn take_page_out_of_layout(&mut self, kind: PaneKind) -> bool {
        let Some((pane, group_len)) = self
            .panes
            .iter()
            .find(|(_, group)| group.pages.contains(&kind))
            .map(|(pane, group)| (*pane, group.pages.len()))
        else {
            return false;
        };
        self.panes.restore();
        if group_len > 1 {
            if let Some(group) = self.panes.get_mut(pane) {
                if let Some(index) = group.pages.iter().position(|page| *page == kind) {
                    group.pages.remove(index);
                    group.active = group.active.min(group.pages.len() - 1);
                    if index < group.active {
                        group.active -= 1;
                    }
                }
            }
            true
        } else if self.panes.len() > 1 {
            self.panes.close(pane).is_some()
        } else {
            false
        }
    }

    /// Puts `kind` back into the dock: next to the Inspector when there is one, else in the first pane.
    fn redock(&mut self, kind: PaneKind) {
        if self
            .panes
            .iter()
            .any(|(_, group)| group.pages.contains(&kind))
        {
            return;
        }
        let target = self
            .panes
            .iter()
            .find(|(_, group)| group.pages.contains(&PaneKind::Inspector))
            .or_else(|| self.panes.iter().next())
            .map(|(pane, _)| *pane);
        if let Some(group) = target.and_then(|pane| self.panes.get_mut(pane)) {
            group.pages.push(kind);
            group.active = group.pages.len() - 1;
        }
    }

    /// The Detach button / agent action: the active tab of `pane` leaves the dock for a window of its own.
    pub(super) fn detach_pane(&mut self, pane: iced::widget::pane_grid::Pane) {
        let Some(kind) = self.panes.get(pane).map(|group| group.active_kind()) else {
            return;
        };
        self.detach_kind(kind);
    }

    pub(super) fn detach_kind(&mut self, kind: PaneKind) {
        if !self.detach.supported {
            self.log(
                LogLevel::Error,
                "Detaching needs the windowed editor: this build has a single window",
            );
            return;
        }
        if let Some(window) = self.detach.window_of(kind) {
            self.detach.pending_focus.push(window);
            return;
        }
        if !self.take_page_out_of_layout(kind) {
            self.log(
                LogLevel::Ui,
                format!(
                    "{} is the last tab of the editor: it stays docked",
                    kind.title()
                ),
            );
            return;
        }
        self.detach.pending_open.push(kind);
        self.log(LogLevel::Action, format!("Detached {}", kind.title()));
    }

    /// The Dock button of a detached window: its tab returns to the editor and the window closes.
    pub(super) fn attach_window(&mut self, window: window::Id) {
        let Some(kind) = self.detach.kind_of(window) else {
            return;
        };
        self.detach.windows.retain(|(held, _)| *held != window);
        self.detach.pending_close.push(window);
        self.redock(kind);
        self.log(LogLevel::Action, format!("Docked {}", kind.title()));
    }

    /// A window went away: the main one quits the editor; a detached one docks its tab back.
    pub(super) fn window_closed(&mut self, window: window::Id) {
        if Some(window) == self.detach.main {
            self.detach.quit = true;
            return;
        }
        if let Some(kind) = self.detach.kind_of(window) {
            self.detach.windows.retain(|(held, _)| *held != window);
            self.redock(kind);
        }
    }

    /// A tab that is detached must not also sit in the dock: a "show this tab" command (View menu, an editor
    /// opening itself) would add it back. The window wins and is brought to the front.
    fn enforce_detached(&mut self) {
        let held: Vec<(window::Id, PaneKind)> = self.detach.windows.clone();
        for (window, kind) in held {
            if self
                .panes
                .iter()
                .any(|(_, group)| group.pages.contains(&kind))
                && self.take_page_out_of_layout(kind)
            {
                self.detach.pending_focus.push(window);
            }
        }
    }

    /// The windowed run's update: the normal one, then the window tasks it asked for.
    #[cfg_attr(feature = "agent", allow(dead_code))]
    pub(super) fn update_windowed(&mut self, message: Message) -> Task<Message> {
        self.update(message);
        self.enforce_detached();
        let mut tasks: Vec<Task<Message>> = Vec::new();
        for kind in std::mem::take(&mut self.detach.pending_open) {
            let (id, open) = window::open(detached_settings(kind));
            self.detach.windows.push((id, kind));
            tasks.push(open.map(|_| Message::Noop));
        }
        for id in std::mem::take(&mut self.detach.pending_close) {
            tasks.push(window::close(id));
        }
        for id in std::mem::take(&mut self.detach.pending_focus) {
            tasks.push(window::gain_focus(id));
        }
        if self.detach.quit {
            tasks.push(iced::exit());
        }
        Task::batch(tasks)
    }

    /// The first state of the windowed run: the main window is opened here.
    #[cfg_attr(feature = "agent", allow(dead_code))]
    pub(super) fn boot_windowed() -> (App, Task<Message>) {
        let mut app = App::new();
        let (id, open) = window::open(theme::window());
        app.detach.supported = true;
        app.detach.main = Some(id);
        (app, open.map(|_| Message::Noop))
    }

    #[cfg_attr(feature = "agent", allow(dead_code))]
    pub(super) fn window_title(&self, window: window::Id) -> String {
        match self.detach.kind_of(window) {
            Some(kind) => format!("{} · Doppio", kind.title()),
            None => "Doppio".to_owned(),
        }
    }

    /// What a window shows: the editor for the main one, the tab for a detached one.
    #[cfg_attr(feature = "agent", allow(dead_code))]
    pub(super) fn view_window(&self, window: window::Id) -> Element<'_, Message> {
        match self.detach.kind_of(window) {
            Some(kind) => self.detached_view(window, kind),
            None => self.view(),
        }
    }

    fn detached_view(&self, window: window::Id, kind: PaneKind) -> Element<'_, Message> {
        let (pane_id, title, subtitle, body) = self.pane_parts(kind);
        let _ = title;
        let header = container(
            row![
                text(kind.title()).size(13).color(theme::TEXT),
                text(subtitle.unwrap_or_default())
                    .size(10)
                    .color(theme::TEXT_MUTED),
                space::horizontal(),
                button::toolbar(
                    id::window_dock(kind.key()),
                    "Dock back",
                    Message::Attach(window),
                ),
            ]
            .spacing(8)
            .align_y(iced::Alignment::Center),
        )
        .padding([4, 8])
        .width(Length::Fill)
        .style(theme::panel_title);
        let body = container(panel::body(body))
            .id(pane_id)
            .width(Length::Fill)
            .height(Length::Fill);
        container(column![header, body])
            .id(id::window_body(kind.key()))
            .width(Length::Fill)
            .height(Length::Fill)
            .style(theme::surface)
            .into()
    }
}

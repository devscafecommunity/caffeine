//! Editor state and message handling of the **Profiler** pane.
//!
//! The scopes live in the engine (one process-wide tree). While the tab is showing, the app reads them twice a
//! second; the two controls (GPU timing, Reset) go straight to the engine. Nothing here edits the scene, so
//! nothing records an undo step.

use super::{App, LogLevel, PaneKind};
use crate::uikit::panels::profiler::{Edit, History, SAMPLE_INTERVAL_MS};
use doppio_engine::ProfileScope;

#[derive(Default)]
pub(super) struct ProfilerState {
    pub scopes: Vec<ProfileScope>,
    pub gpu_timing: bool,
    /// The engine library has the profiler (C ABI 18 or newer).
    pub available: bool,
    pub status: Option<String>,
    /// The last minute of frame rate and machine load, for the charts.
    pub history: History,
    /// When the history was last sampled, and the frames finished since (for the frame rate).
    pub last_sample: Option<std::time::Instant>,
    pub frames_since_sample: u32,
    /// The first machine reading only primes the engine's counters (its percentages cover nothing yet).
    pub primed: bool,
}

impl App {
    /// Whether the Profiler tab is the one showing in its pane.
    pub(super) fn profiler_visible(&self) -> bool {
        self.panes
            .iter()
            .any(|(_, group)| group.pages.get(group.active) == Some(&PaneKind::Profiler))
    }

    /// Called for every frame the viewport finishes: every quarter second it takes a history sample (the frame
    /// rate and the machine's load) and, when the tab is showing, reads the scopes again.
    pub(super) fn profiler_tick(&mut self) {
        let now = std::time::Instant::now();
        self.profiler.frames_since_sample += 1;
        let Some(last) = self.profiler.last_sample else {
            self.profiler.last_sample = Some(now);
            self.profiler.frames_since_sample = 0;
            return;
        };
        let elapsed = now.duration_since(last);
        if elapsed.as_millis() < SAMPLE_INTERVAL_MS {
            return;
        }
        let fps = self.profiler.frames_since_sample as f32 / elapsed.as_secs_f32();
        self.profiler.last_sample = Some(now);
        self.profiler.frames_since_sample = 0;
        let metrics = self
            .engine
            .as_ref()
            .and_then(|engine| engine.system_metrics().ok());
        if self.profiler.primed {
            self.profiler.history.push(fps, metrics.as_ref());
        } else {
            self.profiler.primed = true;
        }
        if self.profiler_visible() {
            self.refresh_profiler();
        }
    }

    /// Reads the engine's scopes again.
    pub(super) fn refresh_profiler(&mut self) {
        let Some(engine) = self.engine.as_ref() else {
            self.profiler.available = false;
            self.profiler.scopes.clear();
            return;
        };
        match engine.profiler_scopes() {
            Ok(scopes) => {
                self.profiler.available = true;
                self.profiler.scopes = scopes;
                self.profiler.gpu_timing = engine.profiler_gpu_timing().unwrap_or(false);
            }
            Err(_) => {
                self.profiler.available = false;
                self.profiler.scopes.clear();
            }
        }
    }

    pub(super) fn edit_profiler(&mut self, edit: Edit) {
        self.profiler.status = None;
        let result = match (self.engine.as_ref(), edit) {
            (Some(engine), Edit::Gpu(on)) => engine
                .set_profiler_gpu_timing(on)
                .map_err(|error| format!("{error:?}")),
            (Some(engine), Edit::Reset) => {
                self.profiler.history.clear();
                engine
                    .profiler_reset()
                    .map_err(|error| format!("{error:?}"))
            }
            (None, _) => Err("The engine is not available".to_owned()),
        };
        match result {
            Ok(()) => {
                if let Edit::Gpu(on) = edit {
                    self.log(
                        LogLevel::Action,
                        if on {
                            "Profiler: GPU timing on (frames are slower while it is on)"
                        } else {
                            "Profiler: GPU timing off"
                        },
                    );
                }
                self.refresh_profiler();
            }
            Err(error) => {
                self.profiler.status = Some(format!("Could not change the profiler: {error}"));
            }
        }
    }

    /// Brings the Profiler tab to the front (it joins the Inspector's pane when it is not open).
    pub(super) fn show_profiler_pane(&mut self) {
        self.panes.restore();
        let mut found = false;
        for (_, group) in self.panes.iter_mut() {
            if let Some(index) = group
                .pages
                .iter()
                .position(|page| *page == PaneKind::Profiler)
            {
                group.active = index;
                found = true;
            }
        }
        if !found {
            for (_, group) in self.panes.iter_mut() {
                if group.pages.contains(&PaneKind::Inspector) {
                    group.pages.push(PaneKind::Profiler);
                    group.active = group.pages.len() - 1;
                    break;
                }
            }
        }
        self.refresh_profiler();
    }
}

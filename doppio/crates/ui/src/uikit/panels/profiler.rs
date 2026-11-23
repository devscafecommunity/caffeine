//! Profiler panel: the engine's timed scopes as a tree.
//!
//! The renderer times its stages (CPU) and, with **GPU timing** on, its GPU passes (each waits for the GPU at its
//! edges, so the time is what the pass really costs and the frame is slower while it is on). This panel is a pure
//! view over `Engine::profiler_scopes`: the app (`windows/main/profiler.rs`) reads the scopes a few times a second
//! and applies the two controls through the engine.

use crate::uikit::components::line_chart::{self, Chart, Series};
use crate::uikit::components::{button, checkbox, progress, scroll, status};
use crate::uikit::id;
use crate::uikit::theme::{self, ChipTone};
use doppio_engine::{ProfileScope, SystemMetrics};
use iced::widget::{column, container, row, text};
use iced::{Element, Length};
use std::rc::Rc;

/// The scope whose average is the frame: the renderer's whole `renderWithCamera`.
pub const FRAME_SCOPE: &str = "GpuSceneRenderer::renderWithCamera";

/// How many samples a chart keeps, and how often one is taken: a minute of history at four a second.
pub const HISTORY_SAMPLES: usize = 240;
pub const SAMPLE_INTERVAL_MS: u128 = 250;

/// What the charts show: the last minute of frame rate and machine load, oldest first.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct History {
    pub fps: Vec<f32>,
    /// One series per logical core.
    pub cores: Vec<Vec<f32>>,
    pub cpu_total: Vec<f32>,
    /// This process, in percent of one core.
    pub process_cpu: Vec<f32>,
    pub gpu_busy: Vec<f32>,
    pub vram_used_mib: Vec<f32>,
    /// The same, as a share of the video memory (what the GPU chart draws).
    pub vram_percent: Vec<f32>,
    pub vram_total_mib: Option<f32>,
    pub ram_used_mib: Vec<f32>,
    pub ram_process_mib: Vec<f32>,
    pub ram_total_mib: Option<f32>,
}

fn push_sample(series: &mut Vec<f32>, value: f32) {
    series.push(value);
    if series.len() > HISTORY_SAMPLES {
        series.drain(..series.len() - HISTORY_SAMPLES);
    }
}

impl History {
    /// Adds one sample: the frame rate since the last one and, when the engine could say, the machine's load.
    pub fn push(&mut self, fps: f32, metrics: Option<&SystemMetrics>) {
        push_sample(&mut self.fps, fps);
        let Some(m) = metrics else {
            return;
        };
        if self.cores.len() != m.cpu_cores.len() {
            self.cores = vec![Vec::new(); m.cpu_cores.len()];
        }
        for (series, value) in self.cores.iter_mut().zip(&m.cpu_cores) {
            push_sample(series, *value);
        }
        if !m.cpu_cores.is_empty() {
            push_sample(&mut self.cpu_total, m.cpu_total);
            push_sample(&mut self.process_cpu, m.process_cpu);
        }
        if let Some(busy) = m.gpu_busy {
            push_sample(&mut self.gpu_busy, busy);
        }
        if let Some(used) = m.vram_used_mib {
            push_sample(&mut self.vram_used_mib, used);
            if let Some(total) = m.vram_total_mib.filter(|total| *total > 0.0) {
                push_sample(&mut self.vram_percent, used / total * 100.0);
            }
        }
        self.vram_total_mib = m.vram_total_mib;
        if let Some(used) = m.ram_used_mib {
            push_sample(&mut self.ram_used_mib, used);
        }
        if let Some(process) = m.ram_process_mib {
            push_sample(&mut self.ram_process_mib, process);
        }
        self.ram_total_mib = m.ram_total_mib;
    }

    pub fn clear(&mut self) {
        *self = History::default();
    }
}

/// The last value, the mean and the lowest of a series.
pub fn stats(values: &[f32]) -> Option<(f32, f32, f32)> {
    let last = *values.last()?;
    let mean = values.iter().sum::<f32>() / values.len() as f32;
    let low = values.iter().copied().fold(f32::MAX, f32::min);
    Some((last, mean, low))
}

#[derive(Debug, Clone, Copy)]
pub enum Edit {
    /// GPU timing on or off.
    Gpu(bool),
    /// Forget every sample.
    Reset,
}

pub struct View<'a> {
    pub scopes: &'a [ProfileScope],
    pub history: &'a History,
    pub gpu_timing: bool,
    /// `None` when the engine library predates the profiler.
    pub available: bool,
    pub status: Option<&'a str>,
}

/// The frame time and rate the renderer's top scope implies.
pub fn frame(scopes: &[ProfileScope]) -> Option<(f32, f32)> {
    let scope = scopes.iter().find(|scope| scope.name == FRAME_SCOPE)?;
    (scope.recent_avg_ms > 0.0).then(|| (scope.recent_avg_ms, 1000.0 / scope.recent_avg_ms))
}

/// The one-line summary under the panel title.
pub fn summary(scopes: &[ProfileScope], gpu_timing: bool) -> String {
    let mode = if gpu_timing {
        "GPU timing"
    } else {
        "CPU scopes"
    };
    match frame(scopes) {
        Some((ms, fps)) => format!("{mode} · {ms:.1} ms · {fps:.0} fps"),
        None => format!("{mode} · no frames yet"),
    }
}

/// The scopes worth a bar, from the most to the least expensive, among those at `depth` 0 and 1.
pub fn heaviest(scopes: &[ProfileScope], count: usize) -> Vec<&ProfileScope> {
    let mut list: Vec<&ProfileScope> = scopes.iter().filter(|scope| scope.depth <= 1).collect();
    list.sort_by(|a, b| b.recent_avg_ms.total_cmp(&a.recent_avg_ms));
    list.truncate(count);
    list
}

/// A titled card around a chart and its caption.
fn chart_card<'a, Message: 'a>(
    title: &'a str,
    chart: Element<'a, Message>,
    legend: Element<'a, Message>,
) -> Element<'a, Message> {
    container(column![text(title).size(11).color(theme::TEXT), chart, legend].spacing(4))
        .padding([6, 8])
        .width(Length::Fill)
        .style(theme::component_card)
        .into()
}

fn caption<'a, Message: 'a>(value: String) -> Element<'a, Message> {
    text(value).size(9).color(theme::TEXT_MUTED).into()
}

/// The four charts: frame rate, CPU per core, GPU and memory.
fn charts<'a, Message: 'a>(history: &'a History) -> Element<'a, Message> {
    const HEIGHT: f32 = 78.0;

    // Frame rate: auto-scaled, never below 60 so a steady 60 sits near the top.
    let fps_top = line_chart::nice_max(history.fps.iter().copied().fold(0.0, f32::max), 60.0);
    let fps_caption = match stats(&history.fps) {
        Some((last, mean, low)) => {
            format!("{last:.0} fps now · {mean:.0} average · {low:.0} lowest")
        }
        None => "waiting for frames".to_owned(),
    };
    let fps = chart_card(
        "Frame rate",
        line_chart::view(
            id::PROFILER_CHART_FPS,
            Chart {
                series: vec![Series {
                    label: "fps",
                    color: theme::series_color(0),
                    values: &history.fps,
                }],
                window: HISTORY_SAMPLES,
                min: 0.0,
                max: fps_top,
                unit: "",
                height: HEIGHT,
            },
        ),
        caption(fps_caption),
    );

    // CPU: every logical core is its own line and colour.
    let cpu_series: Vec<Series<'a>> = history
        .cores
        .iter()
        .enumerate()
        .map(|(index, values)| Series {
            label: "core",
            color: theme::series_color(index),
            values,
        })
        .collect();
    let legend = iced::widget::Row::with_children(history.cores.iter().enumerate().map(
        |(index, values)| {
            let now = values.last().copied().unwrap_or(0.0);
            text(format!("c{index} {now:.0}%"))
                .size(9)
                .color(theme::series_color(index))
                .into()
        },
    ))
    .spacing(8)
    .width(Length::Fill)
    .wrap();
    let cpu_caption = match (stats(&history.cpu_total), stats(&history.process_cpu)) {
        (Some((total, _, _)), Some((process, _, _))) => {
            format!("machine {total:.0}% · this process {process:.0}% of one core")
        }
        _ => "per-core CPU use is not available on this platform".to_owned(),
    };
    let cpu = chart_card(
        "CPU per core",
        line_chart::view(
            id::PROFILER_CHART_CPU,
            Chart {
                series: cpu_series,
                window: HISTORY_SAMPLES,
                min: 0.0,
                max: 100.0,
                unit: "%",
                height: HEIGHT,
            },
        ),
        column![legend, caption(cpu_caption)].spacing(2).into(),
    );

    // GPU: the busy share and the video memory as a share of its size, on one 0..100 axis.
    let gpu_caption = match (
        stats(&history.gpu_busy),
        history.vram_used_mib.last(),
        history.vram_total_mib,
    ) {
        (Some((busy, _, _)), Some(used), Some(total)) => {
            format!("busy {busy:.0}% · video memory {used:.0} of {total:.0} MiB")
        }
        (Some((busy, _, _)), _, _) => format!("busy {busy:.0}%"),
        _ => "GPU use is not available here (NVML or an amdgpu sysfs counter is needed)".to_owned(),
    };
    let gpu = chart_card(
        "GPU",
        line_chart::view(
            id::PROFILER_CHART_GPU,
            Chart {
                series: vec![
                    Series {
                        label: "busy",
                        color: theme::series_color(2),
                        values: &history.gpu_busy,
                    },
                    Series {
                        label: "video memory",
                        color: theme::series_color(4),
                        values: &history.vram_percent,
                    },
                ],
                window: HISTORY_SAMPLES,
                min: 0.0,
                max: 100.0,
                unit: "%",
                height: HEIGHT,
            },
        ),
        row![
            text("busy").size(9).color(theme::series_color(2)),
            text("video memory").size(9).color(theme::series_color(4)),
            caption(gpu_caption),
        ]
        .spacing(8)
        .into(),
    );

    // Memory: the machine's and this process's, in MiB, up to the machine's size.
    let ram_top = history
        .ram_total_mib
        .map(|total| line_chart::nice_max(total, 1.0))
        .unwrap_or_else(|| {
            line_chart::nice_max(
                history.ram_used_mib.iter().copied().fold(0.0, f32::max),
                256.0,
            )
        });
    let ram_caption = match (
        history.ram_used_mib.last(),
        history.ram_process_mib.last(),
        history.ram_total_mib,
    ) {
        (Some(used), Some(process), Some(total)) => {
            format!("machine {used:.0} of {total:.0} MiB · this process {process:.0} MiB")
        }
        (None, Some(process), _) => format!("this process {process:.0} MiB"),
        _ => "memory use is not available on this platform".to_owned(),
    };
    let memory = chart_card(
        "Memory",
        line_chart::view(
            id::PROFILER_CHART_MEMORY,
            Chart {
                series: vec![
                    Series {
                        label: "machine",
                        color: theme::series_color(1),
                        values: &history.ram_used_mib,
                    },
                    Series {
                        label: "process",
                        color: theme::series_color(0),
                        values: &history.ram_process_mib,
                    },
                ],
                window: HISTORY_SAMPLES,
                min: 0.0,
                max: ram_top,
                unit: "",
                height: HEIGHT,
            },
        ),
        row![
            text("machine").size(9).color(theme::series_color(1)),
            text("this process").size(9).color(theme::series_color(0)),
            caption(ram_caption),
        ]
        .spacing(8)
        .into(),
    );

    column![fps, cpu, gpu, memory].spacing(6).into()
}

fn cell<'a, Message: 'a>(value: String, width: f32) -> Element<'a, Message> {
    text(value)
        .size(10)
        .color(theme::TEXT)
        .width(Length::Fixed(width))
        .into()
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let mut content = column![].spacing(6).width(Length::Fill);

    if !view.available {
        content = content.push(
            text("This engine library has no profiler (C ABI 18 or newer)")
                .size(11)
                .color(theme::TEXT_MUTED),
        );
        return scroll::vertical(container(content).padding(8)).into();
    }

    let frame_label = match frame(view.scopes) {
        Some((ms, fps)) => format!("{ms:.2} ms  ·  {fps:.0} fps"),
        None => "no frames yet".to_owned(),
    };
    content = content.push(
        row![
            status::chip(
                if view.gpu_timing {
                    "GPU timing"
                } else {
                    "CPU scopes"
                },
                if view.gpu_timing {
                    ChipTone::Warning
                } else {
                    ChipTone::Neutral
                },
            ),
            container(text(frame_label).size(11).color(theme::TEXT)).id(id::PROFILER_FRAME),
        ]
        .spacing(8)
        .align_y(iced::Alignment::Center),
    );

    content = content.push(
        row![
            checkbox::property(
                id::PROFILER_GPU,
                "GPU timing (frames get slower)",
                view.gpu_timing,
                {
                    let emit = emit.clone();
                    move |on| emit(Edit::Gpu(on))
                }
            ),
            button::toolbar(id::PROFILER_RESET, "Reset", emit(Edit::Reset)),
        ]
        .spacing(8)
        .align_y(iced::Alignment::Center),
    );

    content = content.push(charts(view.history));

    // The scale of the bars: the frame, or the heaviest top scope when there is none.
    let scale = frame(view.scopes)
        .map(|(ms, _)| ms)
        .or_else(|| view.scopes.iter().map(|s| s.recent_avg_ms).reduce(f32::max))
        .unwrap_or(1.0)
        .max(1e-3);

    let mut table = column![row![
        text("Scope")
            .size(9)
            .color(theme::TEXT_MUTED)
            .width(Length::Fill),
        text("avg ms")
            .size(9)
            .color(theme::TEXT_MUTED)
            .width(Length::Fixed(44.0)),
        text("max ms")
            .size(9)
            .color(theme::TEXT_MUTED)
            .width(Length::Fixed(44.0)),
        text("calls")
            .size(9)
            .color(theme::TEXT_MUTED)
            .width(Length::Fixed(36.0)),
    ]
    .spacing(4)]
    .spacing(2);
    if view.scopes.is_empty() {
        table = table.push(
            text("Nothing timed yet: the viewport has to render a frame")
                .size(10)
                .color(theme::TEXT_MUTED),
        );
    }
    for (index, scope) in view.scopes.iter().enumerate() {
        let indent = Length::Fixed(scope.depth as f32 * 10.0);
        let shown = scope
            .name
            .strip_prefix("GpuSceneRenderer::")
            .unwrap_or(&scope.name);
        let label = row![
            iced::widget::space().width(indent),
            text(shown.to_owned()).size(10).color(theme::TEXT),
        ];
        table = table.push(
            container(
                column![
                    row![
                        container(label).width(Length::Fill).clip(true),
                        cell(format!("{:.2}", scope.recent_avg_ms), 44.0),
                        cell(format!("{:.2}", scope.recent_max_ms), 44.0),
                        cell(scope.calls.to_string(), 36.0),
                    ]
                    .spacing(4),
                    progress::bar(
                        id::profiler_row(index).key().to_owned(),
                        scope.recent_avg_ms / scale
                    ),
                ]
                .spacing(1),
            )
            .id(id::profiler_row(index)),
        );
    }
    content = content.push(
        container(table)
            .padding([6, 8])
            .width(Length::Fill)
            .style(theme::component_card)
            .id(id::PROFILER_TABLE),
    );

    if let Some(note) = view.status {
        content = content.push(status::note(note));
    }
    scroll::vertical(container(content).padding(8)).into()
}

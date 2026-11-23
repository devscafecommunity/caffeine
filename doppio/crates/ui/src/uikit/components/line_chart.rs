//! A time chart: one or more lines over the last N samples, newest at the right edge.
//!
//! Pure drawing: the caller owns the samples and says the scale. Used by the Profiler for the frame rate, the CPU
//! cores, the GPU and the memory.

use crate::uikit::theme;
use iced::widget::canvas::{self, Frame, Geometry, Path, Program, Stroke, Text};
use iced::widget::{container, Canvas};
use iced::{mouse, Color, Element, Length, Point, Rectangle};

/// One line of the chart.
#[derive(Debug, Clone)]
pub struct Series<'a> {
    pub label: &'a str,
    pub color: Color,
    /// Oldest first.
    pub values: &'a [f32],
}

#[derive(Debug, Clone)]
pub struct Chart<'a> {
    pub series: Vec<Series<'a>>,
    /// Samples across the full width: a series with fewer starts part-way in from the left.
    pub window: usize,
    /// The value at the bottom and at the top of the chart.
    pub min: f32,
    pub max: f32,
    /// Appended to the axis numbers ("%", " MiB", ...).
    pub unit: &'a str,
    pub height: f32,
}

/// The smallest "round" value (1, 2, 5 times a power of ten) that is at least `value`: the top of an auto-scaled
/// axis. Never below `floor`.
pub fn nice_max(value: f32, floor: f32) -> f32 {
    let value = value.max(floor).max(f32::MIN_POSITIVE);
    let exponent = value.log10().floor();
    let base = 10f32.powf(exponent);
    for step in [1.0, 2.0, 5.0, 10.0] {
        if base * step >= value * 0.999 {
            return base * step;
        }
    }
    base * 10.0
}

/// Where a sample lands: x in 0..1 across `window` samples (newest at 1) for the sample `index` of `len`.
pub fn x_fraction(index: usize, len: usize, window: usize) -> f32 {
    if window <= 1 {
        return 1.0;
    }
    let from_right = (len.saturating_sub(1)).saturating_sub(index) as f32;
    (1.0 - from_right / (window - 1) as f32).clamp(0.0, 1.0)
}

/// Where a value lands: y in 0..1 from the bottom of the chart (clamped).
pub fn y_fraction(value: f32, min: f32, max: f32) -> f32 {
    if max <= min {
        return 0.0;
    }
    ((value - min) / (max - min)).clamp(0.0, 1.0)
}

pub fn view<'a, Message: 'a>(
    id: impl Into<iced::widget::Id>,
    chart: Chart<'a>,
) -> Element<'a, Message> {
    let height = chart.height.max(24.0);
    container(
        Canvas::new(ChartProgram { chart })
            .width(Length::Fill)
            .height(Length::Fixed(height)),
    )
    .id(id.into())
    .width(Length::Fill)
    .into()
}

struct ChartProgram<'a> {
    chart: Chart<'a>,
}

/// Room on the left for the axis numbers.
const LEFT: f32 = 34.0;

impl<'a, Message: 'a> Program<Message> for ChartProgram<'a> {
    type State = ();

    fn draw(
        &self,
        _state: &Self::State,
        renderer: &iced::Renderer,
        _theme: &iced::Theme,
        bounds: Rectangle,
        _cursor: mouse::Cursor,
    ) -> Vec<Geometry> {
        let chart = &self.chart;
        let mut frame = Frame::new(renderer, bounds.size());
        frame.fill_rectangle(Point::ORIGIN, bounds.size(), theme::SURFACE);

        let plot_x = LEFT;
        let plot_w = (bounds.width - LEFT - 4.0).max(8.0);
        let plot_y = 4.0;
        let plot_h = (bounds.height - 8.0).max(8.0);

        // Grid: the bottom, the middle and the top, with their numbers.
        let grid = Color {
            a: 0.14,
            ..theme::TEXT
        };
        for step in 0..=2 {
            let fraction = step as f32 / 2.0;
            let y = plot_y + plot_h * (1.0 - fraction);
            frame.stroke(
                &Path::line(Point::new(plot_x, y), Point::new(plot_x + plot_w, y)),
                Stroke::default().with_color(grid).with_width(1.0),
            );
            let value = chart.min + (chart.max - chart.min) * fraction;
            let label = if value.abs() >= 100.0 || value.fract().abs() < 0.05 {
                format!("{value:.0}{}", chart.unit)
            } else {
                format!("{value:.1}{}", chart.unit)
            };
            frame.fill_text(Text {
                content: label,
                position: Point::new(2.0, (y - 5.0).clamp(0.0, bounds.height - 10.0)),
                color: theme::TEXT_MUTED,
                size: 8.0.into(),
                ..Text::default()
            });
        }

        for series in &chart.series {
            if series.values.len() < 2 {
                continue;
            }
            let len = series.values.len();
            let path = Path::new(|builder| {
                for (index, value) in series.values.iter().enumerate() {
                    let x = plot_x + plot_w * x_fraction(index, len, chart.window);
                    let y = plot_y + plot_h * (1.0 - y_fraction(*value, chart.min, chart.max));
                    if index == 0 {
                        builder.move_to(Point::new(x, y));
                    } else {
                        builder.line_to(Point::new(x, y));
                    }
                }
            });
            frame.stroke(
                &path,
                Stroke::default().with_color(series.color).with_width(1.3),
            );
        }

        frame.stroke(
            &Path::rectangle(Point::new(plot_x, plot_y), iced::Size::new(plot_w, plot_h)),
            Stroke::default()
                .with_color(theme::BORDER_STRONG)
                .with_width(1.0),
        );
        vec![frame.into_geometry()]
    }
}

impl<'a> From<ChartProgram<'a>>
    for canvas::Canvas<ChartProgram<'a>, (), iced::Theme, iced::Renderer>
{
    fn from(program: ChartProgram<'a>) -> Self {
        Canvas::new(program)
    }
}

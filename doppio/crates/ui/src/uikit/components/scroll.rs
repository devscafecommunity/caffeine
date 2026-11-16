//! Scrollable wrapper shared by every panel: a thin rail that sits beside the
//! content instead of overlapping it, styled from the workspace tokens.

use crate::uikit::theme;
use iced::widget::scrollable::{Direction, Scrollbar};
use iced::widget::{scrollable, Scrollable};
use iced::Element;

const RAIL: f32 = 5.0;
const GAP: f32 = 2.0;

/// Vertical scroll area. The scrollbar reserves its own gutter so controls on
/// the right edge (swatches, close buttons) are never covered.
pub fn vertical<'a, Message: 'a>(
    content: impl Into<Element<'a, Message>>,
) -> Scrollable<'a, Message> {
    scrollable(content)
        .direction(Direction::Vertical(
            Scrollbar::new()
                .width(RAIL)
                .scroller_width(RAIL)
                .spacing(GAP),
        ))
        .style(theme::scrollbar)
}

/// Horizontal strip (script tabs) with the same thin rail.
pub fn horizontal<'a, Message: 'a>(
    content: impl Into<Element<'a, Message>>,
) -> Scrollable<'a, Message> {
    scrollable(content)
        .direction(Direction::Horizontal(
            Scrollbar::new()
                .width(RAIL)
                .scroller_width(RAIL)
                .spacing(GAP),
        ))
        .style(theme::scrollbar)
}

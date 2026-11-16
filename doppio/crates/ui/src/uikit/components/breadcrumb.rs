use crate::uikit::id::UiId;
use crate::uikit::theme;
use iced::widget::{button, container, row, text};
use iced::Element;

pub struct Crumb {
    pub id: UiId,
    pub label: String,
    pub path: String,
}

/// A clickable path trail. The first crumb is the root; each following crumb
/// carries the full path it navigates to, so callers never re-parse the trail.
pub fn view<'a, Message: Clone + 'a>(
    crumbs: Vec<Crumb>,
    on_pick: impl Fn(String) -> Message + 'a,
) -> Element<'a, Message> {
    let mut trail = row![].spacing(1).align_y(iced::Alignment::Center);
    for (index, crumb) in crumbs.into_iter().enumerate() {
        if index > 0 {
            trail = trail.push(text("›").size(9).color(theme::TEXT_MUTED));
        }
        trail = trail.push(
            container(
                button(text(crumb.label).size(10))
                    .style(theme::button_text)
                    .padding([0, 3])
                    .on_press(on_pick(crumb.path)),
            )
            .id(crumb.id),
        );
    }
    trail.into()
}

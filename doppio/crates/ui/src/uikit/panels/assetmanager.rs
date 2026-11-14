use crate::uikit::components::asset_preview;
use iced::widget::{column, container, row, text};
use iced::Element;

pub fn view<'a, Message: 'a>() -> Element<'a, Message> {
    let cards = row![
        asset_preview::card::<Message>("Rock", "mesh"),
        asset_preview::card::<Message>("Tree", "mesh"),
        asset_preview::card::<Message>("Player", "prefab"),
    ]
    .spacing(8);

    container(column![text("Asset Manager").size(16), cards].spacing(8))
        .padding(8)
        .into()
}

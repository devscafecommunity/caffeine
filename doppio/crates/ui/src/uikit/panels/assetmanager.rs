use crate::uikit::components::asset_preview;
use crate::uikit::id;
use doppio_engine::AssetInfo;
use iced::widget::{container, scrollable, text, Row};
use iced::Element;

pub fn view<'a, Message: 'a>(assets: &'a [AssetInfo]) -> Element<'a, Message> {
    let files: Vec<&AssetInfo> = assets.iter().filter(|asset| !asset.is_directory).collect();
    if files.is_empty() {
        return container(text("No project assets")).into();
    }

    let mut cards = Row::new().spacing(6);
    for (index, asset) in files.iter().take(60).enumerate() {
        cards = cards.push(asset_preview::card::<Message>(
            id::asset_card(index),
            asset.name.clone(),
            asset.kind.clone(),
        ));
    }

    container(scrollable(cards.wrap().vertical_spacing(6))).into()
}

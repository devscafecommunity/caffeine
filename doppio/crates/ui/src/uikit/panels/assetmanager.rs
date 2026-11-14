use crate::uikit::components::asset_preview;
use crate::uikit::id;
use doppio_engine::AssetInfo;
use iced::widget::{container, scrollable, text, Column, Row};
use iced::Element;

pub fn view<'a, Message: 'a>(assets: &'a [AssetInfo]) -> Element<'a, Message> {
    let files: Vec<&AssetInfo> = assets.iter().filter(|asset| !asset.is_directory).collect();
    if files.is_empty() {
        return container(text("No project assets")).into();
    }

    let mut rows = Column::new().spacing(6);
    for (row_index, chunk) in files
        .iter()
        .take(60)
        .collect::<Vec<_>>()
        .chunks(2)
        .enumerate()
    {
        let mut row = Row::new().spacing(6);
        for (offset, asset) in chunk.iter().enumerate() {
            row = row.push(asset_preview::card::<Message>(
                id::asset_card(row_index * 2 + offset),
                asset.name.clone(),
                asset.kind.clone(),
            ));
        }
        rows = rows.push(row);
    }

    container(scrollable(rows)).into()
}

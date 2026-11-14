use crate::uikit::components::asset_preview;
use doppio_engine::AssetInfo;
use iced::widget::{container, scrollable, text, Column, Row};
use iced::Element;

pub fn view<'a, Message: 'a>(assets: &'a [AssetInfo]) -> Element<'a, Message> {
    let files: Vec<&AssetInfo> = assets.iter().filter(|asset| !asset.is_directory).collect();
    let mut header = Column::new()
        .push(text("Asset Manager").size(16))
        .spacing(2);
    header = header.push(text(format!("{} files", files.len())).size(12));

    if files.is_empty() {
        return container(header.push(text("No project assets")))
            .padding(8)
            .into();
    }

    let mut rows = Column::new().spacing(6);
    for chunk in files.iter().take(60).collect::<Vec<_>>().chunks(3) {
        let mut row = Row::new().spacing(8);
        for asset in chunk {
            row = row.push(asset_preview::card::<Message>(
                asset.name.clone(),
                asset.kind.clone(),
            ));
        }
        rows = rows.push(row);
    }

    container(header.push(scrollable(rows))).padding(8).into()
}

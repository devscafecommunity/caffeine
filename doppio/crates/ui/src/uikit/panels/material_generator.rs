use crate::uikit::components::{
    button, checkbox, color_picker, dropdown, node_canvas, progress, slider, status, text_field,
    tree_view,
};
use crate::uikit::generators::{
    BlendMode, ChannelPacking, ImageFilter, MaterialGraph, NodeId, NodeKind, NoiseType,
    NormalBlendMode, PackSource, PackingPreset, Projection, VoronoiMode, WrapMode,
};
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::theme;
use iced::widget::image::Handle;
use iced::widget::{column, container, image, row, scrollable, text, Column};
use iced::{ContentFit, Element, Length, Vector};
use std::collections::HashSet;
use std::rc::Rc;

#[derive(Debug, Clone)]
pub enum Param {
    Color(usize, f32),
    ColorA(usize, f32),
    ColorB(usize, f32),
    MortarColor(usize, f32),
    Scale(f32),
    Octaves(i32),
    Seed(i32),
    Rows(i32),
    Columns(i32),
    Mortar(f32),
    Angle(f32),
    Factor(f32),
    Mode(usize),
    Brightness(f32),
    Contrast(f32),
    Roughness(f32),
    Metallic(f32),
    Height(f32),
    Ao(f32),
    Projection(usize),
    ImageTiling(usize, f32),
    ImageOffset(usize, f32),
    ImageFilter(usize),
    ImageWrap(usize),
    NoiseType(usize),
    Persistence(f32),
    Lacunarity(f32),
    VoronoiJitter(f32),
    VoronoiMode(usize),
    Gamma(f32),
    Invert(bool),
    CurvePoint(usize, f32),
    Hue(f32),
    Saturation(f32),
    Lightness(f32),
    Threshold(f32),
    Softness(f32),
    ThresholdInvert(bool),
    BlurRadius(f32),
    BlurWrap(usize),
    WarpAmount(f32),
    WarpScale(f32),
    WarpSeed(i32),
    Value(f32),
    NormalMode(usize),
    SharpenAmount(f32),
    SharpenRadius(f32),
    EmbossStrength(f32),
    DirectionalRadius(f32),
    MaskStrength(f32),
    MaskRadius(f32),
    MaskInvert(bool),
}

impl Param {
    pub fn label(&self) -> &'static str {
        match self {
            Param::Color(..) => "color",
            Param::ColorA(..) => "color A",
            Param::ColorB(..) => "color B",
            Param::MortarColor(..) => "mortar color",
            Param::Scale(_) => "scale",
            Param::Octaves(_) => "octaves",
            Param::Seed(_) => "seed",
            Param::Rows(_) => "rows",
            Param::Columns(_) => "columns",
            Param::Mortar(_) => "mortar",
            Param::Angle(_) => "angle",
            Param::Factor(_) => "mix",
            Param::Mode(_) => "blend mode",
            Param::Brightness(_) => "brightness",
            Param::Contrast(_) => "contrast",
            Param::Roughness(_) => "roughness",
            Param::Metallic(_) => "metallic",
            Param::Height(_) => "height",
            Param::Ao(_) => "AO",
            Param::Projection(_) => "projection",
            Param::ImageTiling(..) => "tiling",
            Param::ImageOffset(..) => "offset",
            Param::ImageFilter(_) => "filter",
            Param::ImageWrap(_) => "wrap",
            Param::NoiseType(_) => "noise type",
            Param::Persistence(_) => "persistence",
            Param::Lacunarity(_) => "lacunarity",
            Param::VoronoiJitter(_) => "jitter",
            Param::VoronoiMode(_) => "Voronoi mode",
            Param::Gamma(_) => "gamma",
            Param::Invert(_) => "invert",
            Param::CurvePoint(..) => "curve point",
            Param::Hue(_) => "hue",
            Param::Saturation(_) => "saturation",
            Param::Lightness(_) => "lightness",
            Param::Threshold(_) => "threshold",
            Param::Softness(_) => "softness",
            Param::ThresholdInvert(_) => "invert",
            Param::BlurRadius(_) => "blur radius",
            Param::BlurWrap(_) => "blur wrap",
            Param::WarpAmount(_) => "warp amount",
            Param::WarpScale(_) => "warp scale",
            Param::WarpSeed(_) => "warp seed",
            Param::Value(_) => "value",
            Param::NormalMode(_) => "normal blend mode",
            Param::SharpenAmount(_) => "sharpen amount",
            Param::SharpenRadius(_) => "sharpen radius",
            Param::EmbossStrength(_) => "emboss strength",
            Param::DirectionalRadius(_) => "directional blur radius",
            Param::MaskStrength(_) => "mask strength",
            Param::MaskRadius(_) => "mask radius",
            Param::MaskInvert(_) => "mask invert",
        }
    }

    pub fn key(&self) -> String {
        match self {
            Param::Color(axis, _) => format!("color:{axis}"),
            Param::ColorA(axis, _) => format!("color_a:{axis}"),
            Param::ColorB(axis, _) => format!("color_b:{axis}"),
            Param::MortarColor(axis, _) => format!("mortar_color:{axis}"),
            Param::Scale(_) => "scale".to_owned(),
            Param::Octaves(_) => "octaves".to_owned(),
            Param::Seed(_) => "seed".to_owned(),
            Param::Rows(_) => "rows".to_owned(),
            Param::Columns(_) => "columns".to_owned(),
            Param::Mortar(_) => "mortar".to_owned(),
            Param::Angle(_) => "angle".to_owned(),
            Param::Factor(_) => "mix".to_owned(),
            Param::Mode(_) => "mode".to_owned(),
            Param::Brightness(_) => "brightness".to_owned(),
            Param::Contrast(_) => "contrast".to_owned(),
            Param::Roughness(_) => "roughness".to_owned(),
            Param::Metallic(_) => "metallic".to_owned(),
            Param::Height(_) => "height".to_owned(),
            Param::Ao(_) => "ao".to_owned(),
            Param::Projection(_) => "projection".to_owned(),
            Param::ImageTiling(axis, _) => format!("tiling:{axis}"),
            Param::ImageOffset(axis, _) => format!("offset:{axis}"),
            Param::ImageFilter(_) => "filter".to_owned(),
            Param::ImageWrap(_) => "wrap".to_owned(),
            Param::NoiseType(_) => "noise_type".to_owned(),
            Param::Persistence(_) => "persistence".to_owned(),
            Param::Lacunarity(_) => "lacunarity".to_owned(),
            Param::VoronoiJitter(_) => "jitter".to_owned(),
            Param::VoronoiMode(_) => "mode".to_owned(),
            Param::Gamma(_) => "gamma".to_owned(),
            Param::Invert(_) => "invert".to_owned(),
            Param::CurvePoint(index, _) => format!("point:{index}"),
            Param::Hue(_) => "hue".to_owned(),
            Param::Saturation(_) => "saturation".to_owned(),
            Param::Lightness(_) => "lightness".to_owned(),
            Param::Threshold(_) => "threshold".to_owned(),
            Param::Softness(_) => "softness".to_owned(),
            Param::ThresholdInvert(_) => "threshold_invert".to_owned(),
            Param::BlurRadius(_) => "blur_radius".to_owned(),
            Param::BlurWrap(_) => "blur_wrap".to_owned(),
            Param::WarpAmount(_) => "warp_amount".to_owned(),
            Param::WarpScale(_) => "warp_scale".to_owned(),
            Param::WarpSeed(_) => "warp_seed".to_owned(),
            Param::Value(_) => "value".to_owned(),
            Param::NormalMode(_) => "normal_mode".to_owned(),
            Param::SharpenAmount(_) => "sharpen_amount".to_owned(),
            Param::SharpenRadius(_) => "sharpen_radius".to_owned(),
            Param::EmbossStrength(_) => "emboss_strength".to_owned(),
            Param::DirectionalRadius(_) => "directional_radius".to_owned(),
            Param::MaskStrength(_) => "mask_strength".to_owned(),
            Param::MaskRadius(_) => "mask_radius".to_owned(),
            Param::MaskInvert(_) => "mask_invert".to_owned(),
        }
    }
}

#[derive(Debug, Clone)]
pub enum Edit {
    Select(Option<NodeId>),
    SelectLink(Option<(NodeId, usize)>),
    DeleteLink(NodeId, usize),
    DetachLink(NodeId, usize),
    InsertDot(NodeId, usize, iced::Point),
    MoveNode(NodeId, Vector),
    Connect {
        from: NodeId,
        output: usize,
        to: NodeId,
        input: usize,
    },
    Viewport {
        pan: Vector,
        zoom: f32,
    },
    AddNode(NodeKind),
    DeleteNode(NodeId),
    SetParam(NodeId, Param),
    SetResolution(u32),
    Undo,
    Redo,
    Fit,
    SetSnap(bool),
    SetSearch(String),
    TogglePaletteGroup(String),
    Noop,
    SetPackingPreset(usize),
    ApplyPackingUser(usize),
    SetPackingPresetName(String),
    SavePackingPreset,
    DeletePackingPreset,
    SetPackingChannel(usize, usize),
    SetBits(u8),
    OpenTexturePicker(NodeId),
    SetImagePath(NodeId, String),
    CancelBake,
    Copy,
    Paste,
    Bake,
    OpenMaterial,
}

pub struct BakeProgress<'a> {
    pub fraction: f32,
    pub stage: &'a str,
}

pub struct View<'a> {
    pub graph: &'a MaterialGraph,
    pub selected: Option<NodeId>,
    pub selected_link: Option<(NodeId, usize)>,
    pub pan: Vector,
    pub zoom: f32,
    pub preview: Option<&'a Handle>,
    pub preview_3d: Option<&'a Handle>,
    pub status: Option<&'a str>,
    pub material_name: Option<&'a str>,
    pub dirty: bool,
    pub can_undo: bool,
    pub undo_name: Option<&'a str>,
    pub can_redo: bool,
    pub redo_name: Option<&'a str>,
    pub snap: bool,
    pub search: &'a str,
    pub collapsed_groups: &'a HashSet<String>,
    pub clipboard: bool,
    pub packing: &'a ChannelPacking,
    pub packing_user_presets: &'a [crate::uikit::generators::NamedPackingPreset],
    pub packing_preset_name: &'a str,
    pub bits: u8,
    pub baking: Option<BakeProgress<'a>>,
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    on_edit: impl Fn(Edit) -> Message + 'a,
) -> Element<'a, Message> {
    let emit: Rc<dyn Fn(Edit) -> Message + 'a> = Rc::new(on_edit);
    let graph = view.graph;

    let resolution_options = vec![
        "128".to_owned(),
        "256".to_owned(),
        "512".to_owned(),
        "1024".to_owned(),
    ];
    let resolution_selected = graph.resolution.to_string();
    let resolution_lookup = resolution_options.clone();
    let resolution_emit = emit.clone();
    let resolution = dropdown::property(
        id::GENERATOR_RESOLUTION,
        "Size",
        resolution_options,
        Some(resolution_selected),
        move |choice| {
            let value = resolution_lookup
                .iter()
                .position(|label| *label == choice)
                .map(|index| [128u32, 256, 512, 1024][index])
                .unwrap_or(512);
            resolution_emit(Edit::SetResolution(value))
        },
    );

    let material_name = view
        .material_name
        .unwrap_or("No material open — create one in Material");
    let header = container(
        row![
            column![
                text("Material Generator").size(13).color(theme::TEXT),
                text(material_name).size(9).color(theme::TEXT_MUTED),
            ]
            .spacing(0)
            .width(Length::Fill),
            button::compact(id::GENERATOR_UNDO, "↶", emit(Edit::Undo)),
            button::compact(id::GENERATOR_REDO, "↷", emit(Edit::Redo)),
            button::compact(id::GENERATOR_FIT, "Fit", emit(Edit::Fit)),
            container(resolution).width(Length::Fixed(138.0)),
            button::compact(
                id::GENERATOR_OPEN_MATERIAL,
                "Material",
                emit(Edit::OpenMaterial)
            ),
            button::primary(id::GENERATOR_BAKE, "Bake to Material", emit(Edit::Bake)),
        ]
        .spacing(6)
        .align_y(iced::Alignment::Center),
    )
    .padding([4, 7])
    .style(theme::panel_title);

    let mut toolbox = Column::new().spacing(3);
    toolbox = toolbox.push(text_field::search(
        id::GENERATOR_SEARCH,
        "Search nodes…",
        view.search,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetSearch(value))
        },
    ));
    toolbox = toolbox.push(text("ADD NODE").size(8).color(theme::TEXT_MUTED));
    let needle = view.search.trim().to_lowercase();
    let mut grouped: Vec<(&str, Vec<NodeKind>)> = Vec::new();
    for kind in crate::uikit::generators::palette() {
        let title = kind.title().to_owned();
        let category = kind.category();
        if !needle.is_empty()
            && !title.to_lowercase().contains(&needle)
            && !category.to_lowercase().contains(&needle)
        {
            continue;
        }
        if let Some((_, kinds)) = grouped.iter_mut().find(|(name, _)| *name == category) {
            kinds.push(kind);
        } else {
            grouped.push((category, vec![kind]));
        }
    }
    let mut tree_items = Vec::new();
    for (category, kinds) in grouped {
        let collapsed = needle.is_empty() && view.collapsed_groups.contains(category);
        let group_id = id::generator_group(category);
        let toggle = emit.clone();
        let toggle_name = category.to_owned();
        tree_items.push(tree_view::Item {
            id: group_id,
            label: format!("{category} ({})", kinds.len()),
            icon: Icon::Folder,
            depth: 0,
            has_children: true,
            expanded: !collapsed,
            selected: false,
            hovered: false,
            drop_target: false,
            trailing: None,
            extra: None,
            on_toggle: toggle(Edit::TogglePaletteGroup(toggle_name.clone())),
            on_press: emit(Edit::TogglePaletteGroup(toggle_name.clone())),
            on_release: emit(Edit::Noop),
            on_enter: emit(Edit::Noop),
            on_exit: emit(Edit::Noop),
            on_context: emit(Edit::TogglePaletteGroup(toggle_name)),
        });
        if collapsed {
            continue;
        }
        for kind in kinds {
            let title = kind.title().to_owned();
            let slug = title.to_lowercase().replace(' ', "_");
            let icon = match category {
                "Textures" => Icon::Texture,
                "Filters" => Icon::Material,
                "Utilities" => Icon::Script,
                "Output" => Icon::Material,
                _ => Icon::Generic,
            };
            tree_items.push(tree_view::Item {
                id: id::generator_add(&slug),
                label: title,
                icon,
                depth: 1,
                has_children: false,
                expanded: false,
                selected: false,
                hovered: false,
                drop_target: false,
                trailing: None,
                extra: None,
                on_toggle: emit(Edit::Noop),
                on_press: emit(Edit::AddNode(kind)),
                on_release: emit(Edit::Noop),
                on_enter: emit(Edit::Noop),
                on_exit: emit(Edit::Noop),
                on_context: emit(Edit::Noop),
            });
        }
    }
    if !tree_items.is_empty() {
        toolbox = toolbox.push(tree_view::view(tree_items));
    }
    toolbox = toolbox.push(checkbox::property(
        id::GENERATOR_SNAP,
        "Snap 16",
        view.snap,
        {
            let emit = emit.clone();
            move |value| emit(Edit::SetSnap(value))
        },
    ));
    let emit_copy = emit.clone();
    let emit_paste = emit.clone();
    toolbox = toolbox.push(
        iced::widget::row![
            button::compact(id::GENERATOR_COPY, "Copy", emit_copy(Edit::Copy)),
            button::compact(id::GENERATOR_PASTE, "Paste", emit_paste(Edit::Paste)),
        ]
        .spacing(3),
    );
    toolbox = toolbox.push(
        text("Drag colours into ports. Drag empty space to pan, wheel to zoom.")
            .size(8)
            .color(theme::TEXT_MUTED),
    );

    let nodes = graph.canvas_nodes(view.selected);
    let canvas = node_canvas::view(
        id::NODE_CANVAS,
        node_canvas::View {
            nodes,
            links: &graph.links,
            pan: view.pan,
            zoom: view.zoom,
            selected_link: view.selected_link,
        },
        {
            let emit = emit.clone();
            move |event| match event {
                node_canvas::Event::Select(selected) => emit(Edit::Select(selected)),
                node_canvas::Event::SelectLink(link) => emit(Edit::SelectLink(link)),
                node_canvas::Event::Detach(link) => emit(Edit::DetachLink(link.0, link.1)),
                node_canvas::Event::InsertDot(link, position) => {
                    emit(Edit::InsertDot(link.0, link.1, position))
                }
                node_canvas::Event::MoveNode(id, delta) => emit(Edit::MoveNode(id, delta)),
                node_canvas::Event::Connect {
                    from,
                    output,
                    to,
                    input,
                } => emit(Edit::Connect {
                    from,
                    output,
                    to,
                    input,
                }),
                node_canvas::Event::Viewport { pan, zoom } => emit(Edit::Viewport { pan, zoom }),
            }
        },
    );

    let properties = properties_panel(graph, view.selected, view.selected_link, emit.clone());

    let mut right = Column::new().spacing(6);
    right = right.push(properties);
    let preview_handle = view.preview.or(view.preview_3d);
    let preview_body: Element<'a, Message> = if let Some(preview) = preview_handle {
        image(preview.clone())
            .width(Length::Fill)
            .height(Length::Fixed(132.0))
            .content_fit(ContentFit::Contain)
            .into()
    } else {
        container(
            text("Bake to see the texture")
                .size(9)
                .color(theme::TEXT_MUTED),
        )
        .width(Length::Fill)
        .height(Length::Fixed(72.0))
        .center_x(Length::Fill)
        .center_y(Length::Fixed(72.0))
        .into()
    };
    right = right.push(
        container(
            column![
                text("OUTPUT PREVIEW").size(8).color(theme::TEXT_MUTED),
                preview_body,
            ]
            .spacing(3),
        )
        .id(id::GENERATOR_PREVIEW)
        .width(Length::Fill)
        .padding(4)
        .style(theme::surface),
    );
    let mut packing = Column::new().spacing(3);
    packing = packing.push(text("CHANNEL PACKING").size(8).color(theme::TEXT_MUTED));
    let mut preset_options: Vec<String> = PackingPreset::ALL
        .iter()
        .map(|preset| preset.label().to_owned())
        .collect();
    preset_options.extend(
        view.packing_user_presets
            .iter()
            .map(|preset| preset.name.clone()),
    );
    let preset_index = view
        .packing
        .custom_name
        .as_ref()
        .and_then(|name| {
            view.packing_user_presets
                .iter()
                .position(|preset| preset.name == *name)
                .map(|index| index + PackingPreset::ALL.len())
        })
        .or_else(|| {
            PackingPreset::ALL
                .iter()
                .position(|preset| *preset == view.packing.preset)
        })
        .unwrap_or(0);
    let preset_emit = emit.clone();
    let preset_lookup = preset_options.clone();
    packing = packing.push(dropdown::property(
        id::generator_field("packing_preset"),
        "Preset",
        preset_options,
        preset_lookup.get(preset_index).cloned(),
        move |choice| {
            let index = preset_lookup
                .iter()
                .position(|label| *label == choice)
                .unwrap_or(0);
            if index < PackingPreset::ALL.len() {
                preset_emit(Edit::SetPackingPreset(index))
            } else {
                preset_emit(Edit::ApplyPackingUser(index - PackingPreset::ALL.len()))
            }
        },
    ));
    let name_emit = emit.clone();
    packing = packing.push(text_field::property(
        id::GENERATOR_PACKING_NAME,
        "Preset name",
        view.packing_preset_name,
        move |name| name_emit(Edit::SetPackingPresetName(name)),
    ));
    let save_emit = emit.clone();
    let delete_emit = emit.clone();
    packing = packing.push(
        iced::widget::row![
            button::compact(
                id::GENERATOR_PACKING_SAVE,
                "Save",
                save_emit(Edit::SavePackingPreset),
            ),
            button::compact(
                id::GENERATOR_PACKING_DELETE,
                "Delete",
                delete_emit(Edit::DeletePackingPreset),
            ),
        ]
        .spacing(3),
    );
    for channel in 0..4 {
        let source_options: Vec<String> = PackSource::ALL
            .iter()
            .map(|source| source.label().to_owned())
            .collect();
        let selected = view.packing.channels[channel];
        let selected_index = PackSource::ALL
            .iter()
            .position(|source| *source == selected)
            .unwrap_or(0);
        let channel_emit = emit.clone();
        let lookup = source_options.clone();
        packing = packing.push(dropdown::property(
            id::generator_field(["packed_r", "packed_g", "packed_b", "packed_a"][channel]),
            ["R", "G", "B", "A"][channel],
            source_options,
            Some(lookup[selected_index].clone()),
            move |choice| {
                let index = lookup
                    .iter()
                    .position(|label| *label == choice)
                    .unwrap_or(0);
                channel_emit(Edit::SetPackingChannel(channel, index))
            },
        ));
    }
    let bits_options = vec!["8-bit".to_owned(), "16-bit".to_owned()];
    let bits_emit = emit.clone();
    let bits_lookup = bits_options.clone();
    packing = packing.push(dropdown::property(
        id::generator_field("bits"),
        "Bit depth",
        bits_options,
        Some(if view.bits == 16 {
            bits_lookup[1].clone()
        } else {
            bits_lookup[0].clone()
        }),
        move |choice| bits_emit(Edit::SetBits(if choice == bits_lookup[1] { 16 } else { 8 })),
    ));
    right = right.push(
        container(packing)
            .width(Length::Fill)
            .padding(4)
            .style(theme::surface),
    );
    if let Some(baking) = view.baking {
        right = right.push(
            container(
                column![
                    progress::labelled(id::GENERATOR_BAKE_PROGRESS, baking.fraction, baking.stage,),
                    button::compact(
                        id::GENERATOR_CANCEL_BAKE,
                        "Cancel bake",
                        emit(Edit::CancelBake),
                    ),
                ]
                .spacing(4),
            )
            .width(Length::Fill)
            .padding(4)
            .style(theme::surface),
        );
    }
    if let Some(status) = view.status {
        right = right.push(
            container(text(status).size(9).color(theme::WARNING))
                .width(Length::Fill)
                .padding(3),
        );
    }
    if view.dirty {
        right = right.push(status::chip(
            "Baked material is unsaved",
            theme::ChipTone::Warning,
        ));
    }

    let body = row![
        container(scrollable(toolbox)).width(Length::Fixed(112.0)),
        container(canvas)
            .width(Length::Fill)
            .height(Length::Fill)
            .style(theme::surface),
        container(scrollable(right)).width(Length::Fixed(228.0)),
    ]
    .spacing(6)
    .height(Length::Fill);

    column![header, container(body).height(Length::Fill)]
        .spacing(0)
        .into()
}

fn properties_panel<'a, Message: Clone + 'a>(
    graph: &'a MaterialGraph,
    selected: Option<NodeId>,
    selected_link: Option<(NodeId, usize)>,
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
) -> Element<'a, Message> {
    let Some(node) = selected.and_then(|id| graph.node(id)) else {
        let mut body = column![
            text("NODE").size(8).color(theme::TEXT_MUTED),
            text("Select a node to edit its parameters.")
                .size(10)
                .color(theme::TEXT_MUTED),
        ]
        .spacing(4);
        if let Some((to, input)) = selected_link {
            let emit_delete = emit.clone();
            body = body.push(
                text(format!("Link into node {to}, input {input}"))
                    .size(9)
                    .color(theme::TEXT),
            );
            body = body.push(button::danger(
                id::GENERATOR_DELETE_LINK,
                "Delete link",
                emit_delete(Edit::DeleteLink(to, input)),
            ));
        }
        return container(body).padding(5).style(theme::surface).into();
    };
    let node_id = node.id;
    let kind = node.kind.clone();
    let is_normal_map = matches!(kind, NodeKind::NormalMap { .. });
    let delete_emit = emit.clone();
    let mut body = Column::new().spacing(4);
    body = body.push(
        row![
            text(kind.title()).size(11).color(theme::TEXT),
            iced::widget::space::horizontal(),
            button::danger(
                id::GENERATOR_DELETE_NODE,
                "×",
                delete_emit(Edit::DeleteNode(node_id))
            ),
        ]
        .align_y(iced::Alignment::Center),
    );

    match kind {
        NodeKind::Color { color } => {
            let emit_color = emit.clone();
            body = body.push(color_picker::view(
                "Color",
                color,
                |axis| id::generator_axis("color", axis).into(),
                move |axis, value| emit_color(Edit::SetParam(node_id, Param::Color(axis, value))),
            ));
        }
        NodeKind::Image {
            path,
            projection,
            tiling,
            offset,
            filter,
            wrap,
        }
        | NodeKind::NormalMap {
            path,
            projection,
            tiling,
            offset,
            filter,
            wrap,
        } => {
            let emit_pick = emit.clone();
            body = body.push(button::compact(
                id::generator_field("image_texture"),
                if path.is_empty() {
                    if is_normal_map {
                        "Choose normal map…"
                    } else {
                        "Choose texture…"
                    }
                } else if is_normal_map {
                    "Change normal map…"
                } else {
                    "Change texture…"
                },
                emit_pick(Edit::OpenTexturePicker(node_id)),
            ));
            body = body.push(
                text(if path.is_empty() {
                    "No texture selected".to_owned()
                } else {
                    path.clone()
                })
                .size(8)
                .color(theme::TEXT_MUTED),
            );
            let projection_options: Vec<String> = Projection::ALL
                .iter()
                .map(|projection| projection.label().to_owned())
                .collect();
            let projection_index = Projection::ALL
                .iter()
                .position(|item| *item == projection)
                .unwrap_or(0);
            body = body.push(option_dropdown(
                emit.clone(),
                node_id,
                "image_projection",
                "Projection",
                projection_options,
                projection_index,
                Param::Projection,
            ));
            for (axis, value) in tiling.iter().enumerate() {
                body = body.push(parameter_slider(
                    emit.clone(),
                    node_id,
                    if axis == 0 {
                        "image_tiling_x"
                    } else {
                        "image_tiling_y"
                    },
                    if axis == 0 { "Tiling X" } else { "Tiling Y" },
                    0.01..=32.0,
                    0.01,
                    *value,
                    move |value| Param::ImageTiling(axis, value),
                ));
            }
            for (axis, value) in offset.iter().enumerate() {
                body = body.push(parameter_slider(
                    emit.clone(),
                    node_id,
                    if axis == 0 {
                        "image_offset_x"
                    } else {
                        "image_offset_y"
                    },
                    if axis == 0 { "Offset X" } else { "Offset Y" },
                    -4.0..=4.0,
                    0.01,
                    *value,
                    move |value| Param::ImageOffset(axis, value),
                ));
            }
            body = body.push(option_dropdown(
                emit.clone(),
                node_id,
                "image_filter",
                "Filter",
                vec![
                    ImageFilter::Linear.label().to_owned(),
                    ImageFilter::Nearest.label().to_owned(),
                ],
                if filter == ImageFilter::Nearest { 1 } else { 0 },
                Param::ImageFilter,
            ));
            body = body.push(option_dropdown(
                emit.clone(),
                node_id,
                "image_wrap",
                "Wrap",
                vec![
                    WrapMode::Repeat.label().to_owned(),
                    WrapMode::Clamp.label().to_owned(),
                ],
                if wrap == WrapMode::Clamp { 1 } else { 0 },
                Param::ImageWrap,
            ));
        }
        NodeKind::NormalBlend { strength, mode } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "normal_blend_strength",
                "Strength",
                0.0..=2.0,
                0.01,
                strength,
                Param::Factor,
            ));
            body = body.push(option_dropdown(
                emit.clone(),
                node_id,
                "normal_blend_mode",
                "Mode",
                vec![
                    NormalBlendMode::Whiteout.label().to_owned(),
                    NormalBlendMode::Linear.label().to_owned(),
                ],
                if mode == NormalBlendMode::Linear {
                    1
                } else {
                    0
                },
                Param::NormalMode,
            ));
        }
        NodeKind::Noise {
            noise_type,
            scale,
            octaves,
            persistence,
            lacunarity,
            seed,
            color_a,
            color_b,
        } => {
            body = body.push(option_dropdown(
                emit.clone(),
                node_id,
                "noise_type",
                "Type",
                vec![
                    NoiseType::Value.label().to_owned(),
                    NoiseType::Perlin.label().to_owned(),
                ],
                if noise_type == NoiseType::Perlin {
                    1
                } else {
                    0
                },
                Param::NoiseType,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "scale",
                "Scale",
                1.0..=32.0,
                0.5,
                scale,
                Param::Scale,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "octaves",
                "Octaves",
                1.0..=8.0,
                1.0,
                octaves as f32,
                |value| Param::Octaves(value.round() as i32),
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "persistence",
                "Persistence",
                0.05..=1.0,
                0.01,
                persistence,
                Param::Persistence,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "lacunarity",
                "Lacunarity",
                1.0..=4.0,
                0.01,
                lacunarity,
                Param::Lacunarity,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "seed",
                "Seed",
                0.0..=999.0,
                1.0,
                seed as f32,
                |value| Param::Seed(value.round() as i32),
            ));
            body = body.push(color_pair(emit.clone(), node_id, color_a, color_b));
        }
        NodeKind::Voronoi {
            scale,
            jitter,
            seed,
            color_a,
            color_b,
            mode,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "voronoi_scale",
                "Scale",
                1.0..=32.0,
                0.5,
                scale,
                Param::Scale,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "voronoi_jitter",
                "Jitter",
                0.0..=1.0,
                0.01,
                jitter,
                Param::VoronoiJitter,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "voronoi_seed",
                "Seed",
                0.0..=999.0,
                1.0,
                seed as f32,
                |value| Param::Seed(value.round() as i32),
            ));
            body = body.push(option_dropdown(
                emit.clone(),
                node_id,
                "voronoi_mode",
                "Mode",
                vec![
                    VoronoiMode::Cells.label().to_owned(),
                    VoronoiMode::Distance.label().to_owned(),
                ],
                if mode == VoronoiMode::Distance { 1 } else { 0 },
                Param::VoronoiMode,
            ));
            body = body.push(color_pair(emit.clone(), node_id, color_a, color_b));
        }
        NodeKind::Checker {
            scale,
            color_a,
            color_b,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "checker_scale",
                "Scale",
                1.0..=48.0,
                1.0,
                scale,
                Param::Scale,
            ));
            body = body.push(color_pair(emit.clone(), node_id, color_a, color_b));
        }
        NodeKind::Bricks {
            rows,
            columns,
            mortar,
            color_a,
            color_b,
            mortar_color,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "brick_columns",
                "Columns",
                1.0..=12.0,
                1.0,
                columns as f32,
                |value| Param::Columns(value.round() as i32),
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "brick_rows",
                "Rows",
                1.0..=16.0,
                1.0,
                rows as f32,
                |value| Param::Rows(value.round() as i32),
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "brick_mortar",
                "Mortar",
                0.0..=0.35,
                0.005,
                mortar,
                Param::Mortar,
            ));
            body = body.push(text("Brick A").size(9).color(theme::TEXT_MUTED));
            let emit_color = emit.clone();
            body = body.push(color_picker::view(
                "",
                color_a,
                |axis| id::generator_axis("brick_a", axis).into(),
                move |axis, value| emit_color(Edit::SetParam(node_id, Param::ColorA(axis, value))),
            ));
            let emit_color = emit.clone();
            body = body.push(color_picker::view(
                "",
                color_b,
                |axis| id::generator_axis("brick_b", axis).into(),
                move |axis, value| emit_color(Edit::SetParam(node_id, Param::ColorB(axis, value))),
            ));
            let emit_color = emit.clone();
            body = body.push(color_picker::view(
                "Mortar color",
                mortar_color,
                |axis| id::generator_axis("brick_mortar_color", axis).into(),
                move |axis, value| {
                    emit_color(Edit::SetParam(node_id, Param::MortarColor(axis, value)))
                },
            ));
        }
        NodeKind::Gradient {
            angle,
            color_a,
            color_b,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "gradient_angle",
                "Angle",
                0.0..=360.0,
                1.0,
                angle,
                Param::Angle,
            ));
            body = body.push(color_pair(emit.clone(), node_id, color_a, color_b));
        }
        NodeKind::Blend { factor, mode } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "blend_factor",
                "Mix",
                0.0..=1.0,
                0.01,
                factor,
                Param::Factor,
            ));
            let mode_options: Vec<String> = BlendMode::ALL
                .iter()
                .map(|mode| mode.label().to_owned())
                .collect();
            let selected = mode_options[mode_index(mode)].clone();
            let lookup = mode_options.clone();
            let emit_mode = emit.clone();
            body = body.push(dropdown::property(
                id::generator_field("blend_mode"),
                "Mode",
                mode_options,
                Some(selected),
                move |choice| {
                    let index = lookup
                        .iter()
                        .position(|label| *label == choice)
                        .unwrap_or(0);
                    emit_mode(Edit::SetParam(node_id, Param::Mode(index)))
                },
            ));
        }
        NodeKind::Levels {
            brightness,
            contrast,
            gamma,
            invert,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "levels_brightness",
                "Brightness",
                -1.0..=1.0,
                0.01,
                brightness,
                Param::Brightness,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "levels_contrast",
                "Contrast",
                0.0..=4.0,
                0.01,
                contrast,
                Param::Contrast,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "levels_gamma",
                "Gamma",
                0.1..=4.0,
                0.01,
                gamma,
                Param::Gamma,
            ));
            let emit_invert = emit.clone();
            body = body.push(checkbox::property(
                id::generator_field("levels_invert"),
                "Invert",
                invert,
                move |value| emit_invert(Edit::SetParam(node_id, Param::Invert(value))),
            ));
        }
        NodeKind::Curve { points } => {
            for (index, value) in points.iter().enumerate() {
                body = body.push(parameter_slider(
                    emit.clone(),
                    node_id,
                    "curve_point",
                    ["Black", "Shadows", "Mid", "Highlights", "White"][index],
                    0.0..=1.0,
                    0.01,
                    *value,
                    move |value| Param::CurvePoint(index, value),
                ));
            }
        }
        NodeKind::Hsl {
            hue,
            saturation,
            lightness,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "hsl_hue",
                "Hue",
                -180.0..=180.0,
                0.5,
                hue,
                Param::Hue,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "hsl_saturation",
                "Saturation",
                0.0..=2.0,
                0.01,
                saturation,
                Param::Saturation,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "hsl_lightness",
                "Lightness",
                -1.0..=1.0,
                0.01,
                lightness,
                Param::Lightness,
            ));
        }
        NodeKind::Threshold {
            threshold,
            softness,
            invert,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "threshold_threshold",
                "Threshold",
                0.0..=1.0,
                0.01,
                threshold,
                Param::Threshold,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "threshold_softness",
                "Softness",
                0.0..=0.5,
                0.01,
                softness,
                Param::Softness,
            ));
            let emit_invert = emit.clone();
            body = body.push(checkbox::property(
                id::generator_field("threshold_invert"),
                "Invert",
                invert,
                move |value| emit_invert(Edit::SetParam(node_id, Param::ThresholdInvert(value))),
            ));
        }
        NodeKind::Blur { radius, wrap } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "blur_radius",
                "Radius",
                0.0..=64.0,
                1.0,
                radius,
                |value| Param::BlurRadius(value.round()),
            ));
            body = body.push(option_dropdown(
                emit.clone(),
                node_id,
                "blur_wrap",
                "Wrap",
                vec![
                    WrapMode::Repeat.label().to_owned(),
                    WrapMode::Clamp.label().to_owned(),
                ],
                if wrap == WrapMode::Clamp { 1 } else { 0 },
                Param::BlurWrap,
            ));
        }
        NodeKind::Sharpen { amount, radius } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "sharpen_amount",
                "Amount",
                0.0..=4.0,
                0.01,
                amount,
                Param::SharpenAmount,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "sharpen_radius",
                "Radius",
                1.0..=16.0,
                1.0,
                radius,
                |value| Param::SharpenRadius(value.round()),
            ));
        }
        NodeKind::Emboss { strength, angle } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "emboss_strength",
                "Strength",
                0.0..=8.0,
                0.05,
                strength,
                Param::EmbossStrength,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "emboss_angle",
                "Angle",
                -180.0..=180.0,
                1.0,
                angle,
                Param::Angle,
            ));
        }
        NodeKind::DirectionalBlur {
            radius,
            angle,
            wrap,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "directional_blur_radius",
                "Radius",
                1.0..=64.0,
                1.0,
                radius,
                |value| Param::DirectionalRadius(value.round()),
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "directional_blur_angle",
                "Angle",
                -180.0..=180.0,
                1.0,
                angle,
                Param::Angle,
            ));
            body = body.push(option_dropdown(
                emit.clone(),
                node_id,
                "directional_blur_wrap",
                "Wrap",
                vec![
                    WrapMode::Repeat.label().to_owned(),
                    WrapMode::Clamp.label().to_owned(),
                ],
                if wrap == WrapMode::Clamp { 1 } else { 0 },
                Param::BlurWrap,
            ));
        }
        NodeKind::EdgeMask { strength, invert } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "edge_mask_strength",
                "Strength",
                0.0..=8.0,
                0.05,
                strength,
                Param::MaskStrength,
            ));
            let emit_invert = emit.clone();
            body = body.push(checkbox::property(
                id::generator_field("edge_mask_invert"),
                "Invert",
                invert,
                move |value| emit_invert(Edit::SetParam(node_id, Param::MaskInvert(value))),
            ));
        }
        NodeKind::CavityMask {
            radius,
            strength,
            invert,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "cavity_mask_radius",
                "Radius",
                1.0..=64.0,
                1.0,
                radius,
                |value| Param::MaskRadius(value.round()),
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "cavity_mask_strength",
                "Strength",
                0.0..=10.0,
                0.05,
                strength,
                Param::MaskStrength,
            ));
            let emit_invert = emit.clone();
            body = body.push(checkbox::property(
                id::generator_field("cavity_mask_invert"),
                "Invert",
                invert,
                move |value| emit_invert(Edit::SetParam(node_id, Param::MaskInvert(value))),
            ));
        }
        NodeKind::DistanceMask {
            threshold,
            radius,
            invert,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "distance_mask_threshold",
                "Threshold",
                0.0..=1.0,
                0.01,
                threshold,
                Param::Threshold,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "distance_mask_radius",
                "Radius",
                1.0..=256.0,
                1.0,
                radius,
                Param::MaskRadius,
            ));
            let emit_invert = emit.clone();
            body = body.push(checkbox::property(
                id::generator_field("distance_mask_invert"),
                "Invert",
                invert,
                move |value| emit_invert(Edit::SetParam(node_id, Param::MaskInvert(value))),
            ));
        }
        NodeKind::Warp {
            amount,
            scale,
            seed,
        } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "warp_amount",
                "Amount",
                0.0..=0.5,
                0.005,
                amount,
                Param::WarpAmount,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "warp_scale",
                "Scale",
                1.0..=32.0,
                0.5,
                scale,
                Param::WarpScale,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "warp_seed",
                "Seed",
                0.0..=999.0,
                1.0,
                seed as f32,
                |value| Param::WarpSeed(value.round() as i32),
            ));
        }
        NodeKind::Value { value } => {
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "constant_value",
                "Value",
                0.0..=1.0,
                0.01,
                value,
                Param::Value,
            ));
        }
        NodeKind::Luminance => {
            body = body.push(
                text("Converts the input colour to a scalar by luminance.")
                    .size(9)
                    .color(theme::TEXT_MUTED),
            );
        }
        NodeKind::Unknown {
            filter,
            inputs,
            outputs,
            note,
        } => {
            body = body.push(
                text(format!("Imported filter: {filter}"))
                    .size(9)
                    .color(theme::WARNING),
            );
            body = body.push(
                text(format!(
                    "{} input(s) · {} output(s) preserved",
                    inputs.len(),
                    outputs.len()
                ))
                .size(8)
                .color(theme::TEXT_MUTED),
            );
            if !note.is_empty() {
                body = body.push(text(note).size(8).color(theme::TEXT_MUTED));
            }
            body = body.push(
                text("Placeholder: links are kept, evaluation passes the first input through.")
                    .size(8)
                    .color(theme::TEXT_MUTED),
            );
        }
        NodeKind::Dot => {
            body = body.push(
                text("A reroute dot passes its input to its output.")
                    .size(9)
                    .color(theme::TEXT_MUTED),
            );
        }
        NodeKind::Output {
            roughness,
            metallic,
            height,
            ao,
        } => {
            body = body.push(
                text("Connect scalar maps or use constants.")
                    .size(9)
                    .color(theme::TEXT_MUTED),
            );
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "output_roughness",
                "Roughness",
                0.0..=1.0,
                0.01,
                roughness,
                Param::Roughness,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "output_metallic",
                "Metallic",
                0.0..=1.0,
                0.01,
                metallic,
                Param::Metallic,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "output_height",
                "Height",
                0.0..=1.0,
                0.01,
                height,
                Param::Height,
            ));
            body = body.push(parameter_slider(
                emit.clone(),
                node_id,
                "output_ao",
                "AO",
                0.0..=1.0,
                0.01,
                ao,
                Param::Ao,
            ));
        }
    }

    container(scrollable(body))
        .padding(5)
        .style(theme::surface)
        .into()
}

fn mode_index(mode: BlendMode) -> usize {
    BlendMode::ALL
        .iter()
        .position(|candidate| *candidate == mode)
        .unwrap_or(0)
}

fn option_dropdown<'a, Message: Clone + 'a>(
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
    node: NodeId,
    field: &'static str,
    label: &'static str,
    options: Vec<String>,
    selected_index: usize,
    wrap: impl Fn(usize) -> Param + 'a,
) -> Element<'a, Message> {
    let selected = options.get(selected_index).cloned();
    let lookup = options.clone();
    let wrap = Rc::new(wrap);
    dropdown::property(
        id::generator_field(field),
        label,
        options,
        selected,
        move |choice| {
            let index = lookup.iter().position(|item| *item == choice).unwrap_or(0);
            emit(Edit::SetParam(node, wrap(index)))
        },
    )
}

#[allow(clippy::too_many_arguments)]
fn parameter_slider<'a, Message: Clone + 'a>(
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
    node: NodeId,
    field: &'static str,
    label: &'static str,
    range: std::ops::RangeInclusive<f32>,
    step: f32,
    value: f32,
    wrap: impl Fn(f32) -> Param + 'a,
) -> Element<'a, Message> {
    let wrap = Rc::new(wrap);
    slider::property_step(
        id::generator_field(field),
        label,
        range,
        value,
        step,
        move |value| emit(Edit::SetParam(node, wrap(value))),
    )
}

fn color_pair<'a, Message: Clone + 'a>(
    emit: Rc<dyn Fn(Edit) -> Message + 'a>,
    node: NodeId,
    color_a: [f32; 4],
    color_b: [f32; 4],
) -> Element<'a, Message> {
    let emit_a = emit.clone();
    let emit_b = emit.clone();
    column![
        color_picker::view(
            "Color A",
            color_a,
            |axis| id::generator_axis("color_a", axis).into(),
            move |axis, value| emit_a(Edit::SetParam(node, Param::ColorA(axis, value))),
        ),
        color_picker::view(
            "Color B",
            color_b,
            |axis| id::generator_axis("color_b", axis).into(),
            move |axis, value| emit_b(Edit::SetParam(node, Param::ColorB(axis, value))),
        ),
    ]
    .spacing(4)
    .into()
}

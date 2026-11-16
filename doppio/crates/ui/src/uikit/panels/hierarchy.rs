use crate::uikit::components::{button, menu, text_field, tree_view};
use crate::uikit::icons::Icon;
use crate::uikit::id;
use crate::uikit::EntityView;
use iced::widget::{column, container, row};
use iced::{Element, Length};
use std::collections::{HashMap, HashSet};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Action {
    AddEntity,
    Focus,
    Delete,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TreeRow {
    pub index: usize,
    pub depth: usize,
    pub has_children: bool,
    pub expanded: bool,
}

/// Flattens the entity list into visible tree rows using `parent` links.
/// Entities with missing parents are treated as roots.
pub fn tree_rows(entities: &[EntityView], collapsed: &HashSet<u32>) -> Vec<TreeRow> {
    let ids: HashSet<u32> = entities.iter().map(|entity| entity.id).collect();
    let mut children: HashMap<Option<u32>, Vec<usize>> = HashMap::new();
    for (index, entity) in entities.iter().enumerate() {
        let parent = entity
            .parent
            .filter(|parent| ids.contains(parent) && *parent != entity.id);
        children.entry(parent).or_default().push(index);
    }

    fn walk(
        parent: Option<u32>,
        depth: usize,
        entities: &[EntityView],
        children: &HashMap<Option<u32>, Vec<usize>>,
        collapsed: &HashSet<u32>,
        visited: &mut HashSet<usize>,
        rows: &mut Vec<TreeRow>,
    ) {
        let Some(list) = children.get(&parent) else {
            return;
        };
        for &index in list {
            if !visited.insert(index) {
                continue;
            }
            let entity = &entities[index];
            let has_children = children
                .get(&Some(entity.id))
                .is_some_and(|list| !list.is_empty());
            let expanded = has_children && !collapsed.contains(&entity.id);
            rows.push(TreeRow {
                index,
                depth,
                has_children,
                expanded,
            });
            if expanded {
                walk(
                    Some(entity.id),
                    depth + 1,
                    entities,
                    children,
                    collapsed,
                    visited,
                    rows,
                );
            }
        }
    }

    let mut rows = Vec::with_capacity(entities.len());
    let mut visited = HashSet::new();
    walk(
        None,
        0,
        entities,
        &children,
        collapsed,
        &mut visited,
        &mut rows,
    );

    // Roots that were not reached (for example a cycle entry) still need a row.
    // Descendants of a collapsed node are not roots and stay hidden.
    for (index, entity) in entities.iter().enumerate() {
        let has_valid_parent = entity
            .parent
            .is_some_and(|parent| ids.contains(&parent) && parent != entity.id);
        if !has_valid_parent && visited.insert(index) {
            rows.push(TreeRow {
                index,
                depth: 0,
                has_children: children
                    .get(&Some(entity.id))
                    .is_some_and(|list| !list.is_empty()),
                expanded: false,
            });
        }
    }
    rows
}

pub struct View<'a> {
    pub entities: &'a [EntityView],
    pub selected: usize,
    pub collapsed: &'a HashSet<u32>,
    pub menu_for: Option<usize>,
    pub root_label: &'a str,
    pub root_open: bool,
    pub search: &'a str,
    pub dragging: Option<u32>,
    pub hovered: Option<u32>,
    pub root_hovered: bool,
}

pub struct Callbacks<'a, Message> {
    pub on_root_toggle: Message,
    pub on_search: Box<dyn Fn(String) -> Message + 'a>,
    pub on_toggle: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_press: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_release: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_enter: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_exit: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_context: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_root_enter: Message,
    pub on_root_exit: Message,
    pub on_root_release: Message,
    pub on_empty_hover: Message,
    pub on_open_create: Message,
    pub on_action: Box<dyn Fn(Action) -> Message + 'a>,
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    callbacks: Callbacks<'a, Message>,
) -> Element<'a, Message> {
    let add = button::compact(id::HIERARCHY_ADD, "+", callbacks.on_open_create.clone());
    let search = text_field::search(
        id::HIERARCHY_SEARCH,
        "Search entities…",
        view.search,
        move |value| (callbacks.on_search)(value),
    );
    let header = row![add, search]
        .spacing(4)
        .align_y(iced::Alignment::Center);

    let query = view.search.trim().to_lowercase();
    let rows: Vec<TreeRow> = if query.is_empty() {
        tree_rows(view.entities, view.collapsed)
    } else {
        view.entities
            .iter()
            .enumerate()
            .filter(|(_, entity)| entity.name.to_lowercase().contains(&query))
            .map(|(index, _)| TreeRow {
                index,
                depth: 0,
                has_children: false,
                expanded: false,
            })
            .collect()
    };

    let mut items = Vec::with_capacity(rows.len() + 1);
    items.push(tree_view::Item {
        id: id::HIERARCHY_ROOT,
        label: view.root_label.to_owned(),
        icon: Icon::Scene,
        depth: 0,
        has_children: !view.entities.is_empty(),
        expanded: view.root_open,
        selected: false,
        hovered: view.root_hovered,
        drop_target: view.dragging.is_some() && view.root_hovered,
        trailing: None,
        extra: None,
        on_toggle: callbacks.on_root_toggle.clone(),
        on_press: callbacks.on_root_toggle.clone(),
        on_release: callbacks.on_root_release.clone(),
        on_enter: callbacks.on_root_enter.clone(),
        on_exit: callbacks.on_root_exit.clone(),
        on_context: callbacks.on_root_toggle.clone(),
    });

    if view.root_open {
        if rows.is_empty() {
            let message = if query.is_empty() {
                "No entities"
            } else {
                "No matches"
            };
            items.push(tree_view::Item {
                id: id::HIERARCHY_EMPTY,
                label: message.to_owned(),
                icon: Icon::Generic,
                depth: 1,
                has_children: false,
                expanded: false,
                selected: false,
                hovered: false,
                drop_target: false,
                trailing: None,
                extra: None,
                on_toggle: callbacks.on_root_toggle.clone(),
                on_press: callbacks.on_root_toggle.clone(),
                on_release: callbacks.on_empty_hover.clone(),
                on_enter: callbacks.on_empty_hover.clone(),
                on_exit: callbacks.on_empty_hover.clone(),
                on_context: callbacks.on_root_toggle.clone(),
            });
        }
        for row in rows {
            let entity = &view.entities[row.index];
            let context_menu = (view.menu_for == Some(row.index)).then(|| {
                menu::context(vec![
                    menu::Item {
                        id: id::ADD_ENTITY,
                        label: "Add Entity",
                        shortcut: Some("Ctrl+N"),
                        message: (callbacks.on_action)(Action::AddEntity),
                    },
                    menu::Item {
                        id: id::FOCUS_SELECTION,
                        label: "Focus in Viewport",
                        shortcut: Some("Ctrl+F"),
                        message: (callbacks.on_action)(Action::Focus),
                    },
                    menu::Item {
                        id: id::DELETE_ENTITY,
                        label: "Delete Entity",
                        shortcut: None,
                        message: (callbacks.on_action)(Action::Delete),
                    },
                ])
            });
            items.push(tree_view::Item {
                id: id::entity_row(row.index),
                label: entity.name.clone(),
                icon: entity.icon,
                depth: row.depth + 1,
                has_children: row.has_children,
                expanded: row.expanded,
                selected: row.index == view.selected,
                hovered: view.hovered == Some(entity.id) && view.dragging != Some(entity.id),
                drop_target: view.dragging.is_some()
                    && view.dragging != Some(entity.id)
                    && view.hovered == Some(entity.id),
                trailing: None,
                extra: context_menu,
                on_toggle: (callbacks.on_toggle)(row.index),
                on_press: (callbacks.on_press)(row.index),
                on_release: (callbacks.on_release)(row.index),
                on_enter: (callbacks.on_enter)(row.index),
                on_exit: (callbacks.on_exit)(row.index),
                on_context: (callbacks.on_context)(row.index),
            });
        }
    }

    container(
        column![
            header,
            crate::uikit::components::scroll::vertical(tree_view::view(items))
        ]
        .spacing(4),
    )
    .width(Length::Fill)
    .into()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entity(id: u32, parent: Option<u32>) -> EntityView {
        EntityView {
            id,
            name: format!("Entity {id}"),
            position: [0.0; 3],
            rotation: [0.0; 3],
            scale: [1.0; 3],
            parent,
            icon: crate::uikit::icons::Icon::Generic,
        }
    }

    #[test]
    fn flattens_parents_before_children() {
        let entities = vec![
            entity(0, None),
            entity(1, Some(0)),
            entity(2, Some(1)),
            entity(3, None),
        ];
        let rows = tree_rows(&entities, &HashSet::new());
        let shape: Vec<(u32, usize)> = rows
            .iter()
            .map(|row| (entities[row.index].id, row.depth))
            .collect();
        assert_eq!(shape, vec![(0, 0), (1, 1), (2, 2), (3, 0)]);
        assert!(rows[0].has_children);
        assert!(!rows[2].expanded);
    }

    #[test]
    fn collapsing_hides_descendants() {
        let entities = vec![
            entity(0, None),
            entity(1, Some(0)),
            entity(2, Some(1)),
            entity(3, None),
        ];
        let collapsed = HashSet::from([0]);
        let rows = tree_rows(&entities, &collapsed);
        let ids: Vec<u32> = rows.iter().map(|row| entities[row.index].id).collect();
        assert_eq!(ids, vec![0, 3]);
        assert!(!rows[0].expanded);
    }

    #[test]
    fn missing_parent_is_a_root() {
        let entities = vec![entity(0, Some(99)), entity(1, None)];
        let rows = tree_rows(&entities, &HashSet::new());
        assert_eq!(rows.len(), 2);
        assert!(rows.iter().all(|row| row.depth == 0));
    }
}

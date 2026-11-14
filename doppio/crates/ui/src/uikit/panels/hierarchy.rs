use crate::uikit::components::{menu, tree_row};
use crate::uikit::id;
use crate::uikit::EntityView;
use iced::widget::{column, container, scrollable, text};
use iced::Element;
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
}

pub struct Callbacks<'a, Message> {
    pub on_toggle: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_select: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_context: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_double_click: Box<dyn Fn(usize) -> Message + 'a>,
    pub on_action: Box<dyn Fn(Action) -> Message + 'a>,
}

pub fn view<'a, Message: Clone + 'a>(
    view: View<'a>,
    callbacks: Callbacks<'a, Message>,
) -> Element<'a, Message> {
    let mut list = column![].spacing(3);
    if view.entities.is_empty() {
        list = list.push(text("No entities").color(crate::uikit::theme::TEXT_MUTED));
    }
    for row in tree_rows(view.entities, view.collapsed) {
        let entity = &view.entities[row.index];
        list = list.push(tree_row::view(tree_row::Row {
            id: id::entity_row(row.index),
            label: format!("{}  #{}", entity.name, entity.id),
            depth: row.depth,
            has_children: row.has_children,
            expanded: row.expanded,
            selected: row.index == view.selected,
            on_toggle: (callbacks.on_toggle)(row.index),
            on_select: (callbacks.on_select)(row.index),
            on_context: (callbacks.on_context)(row.index),
            on_double_click: (callbacks.on_double_click)(row.index),
        }));
        if view.menu_for == Some(row.index) {
            list = list.push(menu::context(vec![
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
            ]));
        }
    }
    container(scrollable(list)).into()
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

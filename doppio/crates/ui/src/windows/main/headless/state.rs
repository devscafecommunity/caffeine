//! JSON snapshot of the editor state for the `state` action.
//!
//! It is a curated, stable view (not a dump of `App`): enough for an agent to
//! assert on what a human would see or infer, with the same names the UI uses.

use super::super::{App, MenuId};
use serde_json::{json, Value};

fn menu_name(menu: Option<MenuId>) -> Value {
    match menu {
        Some(MenuId::Project) => json!("Project"),
        Some(MenuId::Scene) => json!("Scene"),
        Some(MenuId::Edit) => json!("Edit"),
        Some(MenuId::View) => json!("View"),
        None => Value::Null,
    }
}

pub fn describe(app: &App) -> Value {
    let mut panes: Vec<(String, Value)> = app
        .panes
        .iter()
        .map(|(pane, group)| {
            let id = format!("{pane:?}");
            (
                id.clone(),
                json!({
                    "id": id,
                    "pages": group.pages.iter().map(|page| page.title()).collect::<Vec<_>>(),
                    "active": group.active_kind().title(),
                    "maximized": app.panes.maximized() == Some(*pane),
                }),
            )
        })
        .collect();
    panes.sort_by(|a, b| a.0.cmp(&b.0));

    let entities: Vec<Value> = app
        .entities
        .iter()
        .map(|entity| {
            json!({
                "id": entity.id,
                "name": entity.name,
                "position": entity.position,
                "rotation": entity.rotation,
                "scale": entity.scale,
                "parent": entity.parent,
            })
        })
        .collect();

    let console: Vec<Value> = app
        .log
        .iter()
        .rev()
        .take(10)
        .rev()
        .map(|entry| json!({ "level": format!("{:?}", entry.level), "text": entry.text }))
        .collect();

    let mut collapsed: Vec<&String> = app.asset_tree_collapsed.iter().collect();
    collapsed.sort();

    json!({
        "panes": panes.into_iter().map(|(_, value)| value).collect::<Vec<_>>(),
        "maximized": app.panes.maximized().map(|pane| format!("{pane:?}")),
        "entities": entities,
        "selected": app.entities.get(app.selected).map(|entity| entity.name.clone()),
        "selected_index": if app.entities.is_empty() { Value::Null } else { json!(app.selected) },
        "menu": menu_name(app.open_menu),
        "hierarchy_menu": app.hierarchy_menu,
        "playing": app.playing,
        "dirty": app.engine.as_ref().map(|engine| engine.is_dirty()),
        "tab_drag": app.tab_drag.map(|(pane, page)| json!({
            "pane": format!("{pane:?}"),
            "page": page.title(),
            "dragging": app.tab_dragging,
        })),
        "dock_hover": app.dock_hover.map(|(pane, zone)| json!({
            "pane": format!("{pane:?}"),
            "zone": format!("{zone:?}"),
        })),
        "assets": {
            "count": app.assets.len(),
            "directory": app.asset_directory,
            "search": app.asset_search,
            "selected": app.asset_selected.and_then(|i| app.assets.get(i)).map(|a| a.relative_path.clone()),
            "view_mode": format!("{:?}", app.asset_view_mode),
            "tree_open": app.asset_tree_open,
            "tree_collapsed": collapsed,
        },
        "wizard": {
            "open": app.wizard.open,
            "tab": format!("{:?}", app.wizard.tab),
            "template": app.wizard.template.label(),
            "name": app.wizard.name,
            "sources": app.wizard.sources,
            "packing": app.wizard.packing,
            "status": app.wizard.status.as_ref().map(|s| json!({ "text": s.text, "ok": s.ok })),
        },
        "project": {
            "manager_open": app.project.open,
            "active": app.project.active.as_ref().map(|p| p.name.clone()),
        },
        "material": app.material.as_ref().map(|m| json!({ "name": m.surface.name })),
        "texture_set": app.texture_set.is_some(),
        "script": {
            "documents": app.script.documents.iter().map(|d| d.title.clone()).collect::<Vec<_>>(),
        },
        "viewport": {
            "gizmo": app.gizmo_mode.label(),
            "grid": app.viewport_settings.show_grid,
        },
        "console": { "count": app.log.len(), "last": console },
    })
}

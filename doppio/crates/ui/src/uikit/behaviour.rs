//! Behaviours: everything that can be attached to an entity.
//!
//! A "behaviour" is not only a script. Engine components (cameras, lights,
//! mesh renderers, rigid bodies, colliders…), scripts, meshes and materials
//! from the project are all things you add to an entity, so the inspector's
//! **Add Behaviour** picker lists them together, grouped and searchable.
//! This module is the pure catalogue (no widgets): it turns the engine's
//! component types plus the asset list into entries the picker renders.

use doppio_engine::{AssetInfo, ComponentData, ComponentType};

/// What picking an entry does to the selected entity.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Behaviour {
    /// Add an engine component by type id.
    Component(u32),
    /// Attach a script asset (adds the Script component if needed).
    Script(String),
    /// Add a Mesh Renderer pointing at a mesh asset.
    Mesh(String),
    /// Assign a material asset to the entity's mesh.
    Material(String),
}

/// One row of the picker.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    pub behaviour: Behaviour,
    pub label: String,
    pub detail: String,
    pub group: &'static str,
    /// The entity already has this component (shown, not selectable).
    pub added: bool,
}

/// Group titles in display order.
pub const GROUPS: [&str; 10] = [
    "Rendering",
    "Physics",
    "Scripting",
    "Audio",
    "Animation",
    "Navigation",
    "Other",
    "Scripts",
    "Meshes",
    "Materials",
];

/// Components that every entity already has through the Transform card.
fn is_transform_part(name: &str) -> bool {
    matches!(
        name,
        "Transform" | "Position 3D" | "Rotation 3D" | "Scale 3D"
    )
}

/// Which group an engine component belongs to, from its name.
pub fn component_group(name: &str) -> &'static str {
    let lower = name.to_ascii_lowercase();
    let has = |words: &[&str]| words.iter().any(|word| lower.contains(word));
    if has(&["script"]) {
        "Scripting"
    } else if has(&[
        "rigid",
        "collider",
        "physics",
        "joint",
        "trigger",
        "character controller",
    ]) {
        "Physics"
    } else if has(&["audio", "sound", "listener", "music"]) {
        "Audio"
    } else if has(&["anim", "skeleton", "skin", "bone", "timeline"]) {
        "Animation"
    } else if has(&["nav", "agent", "path", "obstacle"]) {
        "Navigation"
    } else if has(&[
        "camera", "light", "mesh", "sprite", "particle", "terrain", "water", "render", "material",
        "post", "decal", "sky", "fog", "volume", "mask",
    ]) {
        "Rendering"
    } else {
        "Other"
    }
}

fn field_summary(kind: &ComponentType) -> String {
    let names: Vec<&str> = kind
        .fields
        .iter()
        .map(|field| field.name.as_str())
        .collect();
    match names.len() {
        0 => "No fields".to_owned(),
        1..=3 => names.join(", "),
        n => format!("{}, {}, {} +{}", names[0], names[1], names[2], n - 3),
    }
}

/// Every attachable thing, in group order (components first, then assets).
pub fn catalog(
    component_types: &[ComponentType],
    present: &[ComponentData],
    assets: &[AssetInfo],
) -> Vec<Entry> {
    let mut entries: Vec<Entry> = Vec::new();
    for kind in component_types {
        if is_transform_part(&kind.name) {
            continue;
        }
        entries.push(Entry {
            behaviour: Behaviour::Component(kind.id),
            label: kind.name.clone(),
            detail: field_summary(kind),
            group: component_group(&kind.name),
            added: present
                .iter()
                .any(|component| component.info.type_id == kind.id),
        });
    }
    for asset in assets.iter().filter(|asset| !asset.is_directory) {
        let (behaviour, group) = match asset.kind.as_str() {
            "script" => (Behaviour::Script(asset.relative_path.clone()), "Scripts"),
            "mesh" => (Behaviour::Mesh(asset.relative_path.clone()), "Meshes"),
            "material" => (
                Behaviour::Material(asset.relative_path.clone()),
                "Materials",
            ),
            _ => continue,
        };
        entries.push(Entry {
            behaviour,
            label: asset.name.clone(),
            detail: asset.relative_path.clone(),
            group,
            added: false,
        });
    }
    let order = |group: &str| {
        GROUPS
            .iter()
            .position(|name| *name == group)
            .unwrap_or(GROUPS.len())
    };
    entries.sort_by(|a, b| {
        order(a.group)
            .cmp(&order(b.group))
            .then_with(|| a.label.to_lowercase().cmp(&b.label.to_lowercase()))
    });
    entries
}

/// Entries matching `search` (name, detail or group, case-insensitive).
pub fn filter<'a>(entries: &'a [Entry], search: &str) -> Vec<&'a Entry> {
    let needle = search.trim().to_lowercase();
    entries
        .iter()
        .filter(|entry| {
            needle.is_empty()
                || entry.label.to_lowercase().contains(&needle)
                || entry.detail.to_lowercase().contains(&needle)
                || entry.group.to_lowercase().contains(&needle)
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use doppio_engine::{ComponentInfo, FieldInfo, FieldKind};

    fn kind(id: u32, name: &str, fields: &[&str]) -> ComponentType {
        ComponentType {
            id,
            name: name.to_owned(),
            fields: fields
                .iter()
                .map(|field| FieldInfo {
                    name: (*field).to_owned(),
                    kind: FieldKind::F32,
                    min: 0.0,
                    max: 1.0,
                    step: 0.1,
                    options: Vec::new(),
                })
                .collect(),
        }
    }

    fn asset(name: &str, path: &str, kind: &str) -> AssetInfo {
        AssetInfo {
            name: name.to_owned(),
            relative_path: path.to_owned(),
            kind: kind.to_owned(),
            size_bytes: 0,
            is_directory: false,
        }
    }

    #[test]
    fn components_scripts_meshes_and_materials_share_one_catalogue() {
        let types = vec![
            kind(1, "Transform", &["Position"]),
            kind(
                7,
                "Mesh Renderer",
                &["Mesh", "Material", "Cast shadows", "Receive shadows"],
            ),
            kind(8, "Rigid Body 3D", &["Mass"]),
            kind(12, "Script", &["Script"]),
        ];
        let present = vec![ComponentData {
            info: ComponentInfo {
                type_id: 8,
                name: "Rigid Body 3D".to_owned(),
                field_count: 1,
            },
            values: Vec::new(),
        }];
        let assets = vec![
            asset("spin.lua", "Scripts/spin.lua", "script"),
            asset("hero.glb", "Models/hero.glb", "mesh"),
            asset("brick.mat", "Materials/brick.mat", "material"),
            asset("rock.png", "Textures/rock.png", "image"),
        ];
        let entries = catalog(&types, &present, &assets);
        let labels: Vec<_> = entries
            .iter()
            .map(|e| (e.group, e.label.as_str()))
            .collect();
        assert_eq!(
            labels,
            [
                ("Rendering", "Mesh Renderer"),
                ("Physics", "Rigid Body 3D"),
                ("Scripting", "Script"),
                ("Scripts", "spin.lua"),
                ("Meshes", "hero.glb"),
                ("Materials", "brick.mat"),
            ],
            "Transform and non-attachable assets are left out"
        );
        assert!(
            entries
                .iter()
                .find(|e| e.label == "Rigid Body 3D")
                .unwrap()
                .added
        );
        assert!(
            !entries
                .iter()
                .find(|e| e.label == "Mesh Renderer")
                .unwrap()
                .added
        );
        assert_eq!(entries[0].detail, "Mesh, Material, Cast shadows +1");
    }

    #[test]
    fn search_matches_names_details_and_groups() {
        let types = vec![
            kind(8, "Rigid Body 3D", &["Mass"]),
            kind(9, "Collider 3D", &["Shape"]),
        ];
        let entries = catalog(&types, &[], &[asset("hero.glb", "Models/hero.glb", "mesh")]);
        assert_eq!(filter(&entries, "collider").len(), 1);
        assert_eq!(filter(&entries, "PHYSICS").len(), 2);
        assert_eq!(filter(&entries, "models/").len(), 1);
        assert_eq!(filter(&entries, "").len(), 3);
        assert!(filter(&entries, "zzz").is_empty());
    }

    #[test]
    fn component_groups_follow_their_names() {
        assert_eq!(component_group("Camera 3D"), "Rendering");
        assert_eq!(component_group("Point Light"), "Rendering");
        assert_eq!(component_group("Collider 2D"), "Physics");
        assert_eq!(component_group("Audio Source"), "Audio");
        assert_eq!(component_group("Skeletal Animator"), "Animation");
        assert_eq!(component_group("Nav Agent"), "Navigation");
        assert_eq!(component_group("Whatever"), "Other");
    }
}

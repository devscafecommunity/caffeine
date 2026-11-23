//! `*.prefab` files: a saved entity tree that can be placed in any scene again.
//!
//! A prefab is a snapshot of an entity and its children **by what the editor shows**: every component and
//! every field value, by name. Nothing is lost between what the Inspector edits and what the file keeps (material
//! slots, Micromesh switches, wind, light settings, …), and a field added to the engine later is simply carried.
//!
//! ```text
//! Prefabs/Oak.prefab               ← the tree: a group with a trunk and a crown
//! Models/Nature/Oak.generator.nature  ← the recipe the prefab came from (`generator`)
//! ```
//!
//! The file is JSON. The first entity is the root; `parent` is an index into `entities` (always smaller than the
//! entity's own, so a reader can create them in order). Paths inside values are project paths, exactly as the
//! Inspector stores them.

use serde::{Deserialize, Serialize};
use std::path::Path;

pub const FORMAT: &str = "caffeine-prefab";
pub const VERSION: u32 = 1;
/// The file extension, without the dot.
pub const EXTENSION: &str = "prefab";

/// A field value, the same shapes the engine reflects.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "type", content = "value", rename_all = "snake_case")]
pub enum Value {
    F32(f32),
    Bool(bool),
    I32(i32),
    U32(u32),
    Vec2([f32; 2]),
    Vec3([f32; 3]),
    Vec4([f32; 4]),
    String(String),
    Enum(i32),
    Color([f32; 4]),
}

impl From<&doppio_engine::FieldValue> for Value {
    fn from(value: &doppio_engine::FieldValue) -> Self {
        use doppio_engine::FieldValue as F;
        match value {
            F::F32(v) => Value::F32(*v),
            F::Bool(v) => Value::Bool(*v),
            F::I32(v) => Value::I32(*v),
            F::U32(v) => Value::U32(*v),
            F::Vec2(v) => Value::Vec2(*v),
            F::Vec3(v) => Value::Vec3(*v),
            F::Vec4(v) => Value::Vec4(*v),
            F::String(v) => Value::String(v.clone()),
            F::Enum(v) => Value::Enum(*v),
            F::Color(v) => Value::Color(*v),
        }
    }
}

impl Value {
    pub fn to_engine(&self) -> doppio_engine::FieldValue {
        use doppio_engine::FieldValue as F;
        match self {
            Value::F32(v) => F::F32(*v),
            Value::Bool(v) => F::Bool(*v),
            Value::I32(v) => F::I32(*v),
            Value::U32(v) => F::U32(*v),
            Value::Vec2(v) => F::Vec2(*v),
            Value::Vec3(v) => F::Vec3(*v),
            Value::Vec4(v) => F::Vec4(*v),
            Value::String(v) => F::String(v.clone()),
            Value::Enum(v) => F::Enum(*v),
            Value::Color(v) => F::Color(*v),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Field {
    pub name: String,
    #[serde(flatten)]
    pub value: Value,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Component {
    pub name: String,
    pub fields: Vec<Field>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Entity {
    pub name: String,
    /// Index of the parent in [`Prefab::entities`]; `None` for the root.
    pub parent: Option<usize>,
    pub components: Vec<Component>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Prefab {
    format: String,
    version: u32,
    pub name: String,
    /// The recipe this prefab came from, relative to the assets root (empty for a hand-made one).
    #[serde(default)]
    pub generator: String,
    pub entities: Vec<Entity>,
}

impl Prefab {
    pub fn new(name: &str, generator: &str, entities: Vec<Entity>) -> Self {
        Self {
            format: FORMAT.to_owned(),
            version: VERSION,
            name: name.to_owned(),
            generator: generator.to_owned(),
            entities,
        }
    }

    pub fn to_text(&self) -> String {
        serde_json::to_string_pretty(self).unwrap_or_default() + "\n"
    }

    pub fn from_text(text: &str) -> Result<Self, String> {
        let prefab: Prefab =
            serde_json::from_str(text).map_err(|error| format!("not a prefab: {error}"))?;
        if prefab.format != FORMAT {
            return Err(format!("unknown format `{}`", prefab.format));
        }
        if prefab.version > VERSION {
            return Err(format!(
                "written by a newer Doppio (version {} > {VERSION})",
                prefab.version
            ));
        }
        if prefab.entities.is_empty() {
            return Err("the prefab has no entities".to_owned());
        }
        for (index, entity) in prefab.entities.iter().enumerate() {
            match (index, entity.parent) {
                (0, None) => {}
                (0, Some(_)) => return Err("the first entity must be the root".to_owned()),
                (_, Some(parent)) if parent < index => {}
                _ => return Err(format!("entity {index} has no valid parent")),
            }
        }
        Ok(prefab)
    }
}

pub fn is_prefab(path: &Path) -> bool {
    path.extension()
        .and_then(|extension| extension.to_str())
        .is_some_and(|extension| extension.eq_ignore_ascii_case(EXTENSION))
}

pub fn write(path: &Path, prefab: &Prefab) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)
            .map_err(|error| format!("{}: {error}", parent.display()))?;
    }
    std::fs::write(path, prefab.to_text()).map_err(|error| format!("{}: {error}", path.display()))
}

pub fn read(path: &Path) -> Result<Prefab, String> {
    let text =
        std::fs::read_to_string(path).map_err(|error| format!("{}: {error}", path.display()))?;
    Prefab::from_text(&text)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Prefab {
        let part = |name: &str, parent: Option<usize>| Entity {
            name: name.to_owned(),
            parent,
            components: vec![Component {
                name: "Mesh Filter".to_owned(),
                fields: vec![
                    Field {
                        name: "Mesh".to_owned(),
                        value: Value::String("Models/Nature/oak_trunk.obj".to_owned()),
                    },
                    Field {
                        name: "Shininess".to_owned(),
                        value: Value::F32(32.0),
                    },
                ],
            }],
        };
        Prefab::new(
            "Oak",
            "Models/Nature/Oak.generator.nature",
            vec![
                part("Oak", None),
                part("Oak trunk", Some(0)),
                part("Oak leaves", Some(0)),
            ],
        )
    }

    #[test]
    fn a_prefab_round_trips_through_its_text() {
        let prefab = sample();
        let back = Prefab::from_text(&prefab.to_text()).expect("reads back");
        assert_eq!(back, prefab);
        assert!(prefab.to_text().contains("\"type\": \"string\""));
    }

    #[test]
    fn engine_values_survive_the_mirror_enum() {
        use doppio_engine::FieldValue as F;
        for value in [
            F::F32(1.5),
            F::Bool(true),
            F::I32(-3),
            F::U32(7),
            F::Vec2([1.0, 2.0]),
            F::Vec3([1.0, 2.0, 3.0]),
            F::Vec4([1.0, 2.0, 3.0, 4.0]),
            F::String("a;b".to_owned()),
            F::Enum(2),
            F::Color([0.1, 0.2, 0.3, 1.0]),
        ] {
            assert_eq!(Value::from(&value).to_engine(), value);
        }
    }

    #[test]
    fn broken_or_newer_files_are_refused_with_a_reason() {
        assert!(Prefab::from_text("{}").is_err());
        let newer = sample()
            .to_text()
            .replace("\"version\": 1", "\"version\": 99");
        assert!(Prefab::from_text(&newer).unwrap_err().contains("newer"));
        let mut orphan = sample();
        orphan.entities[2].parent = Some(2);
        assert!(Prefab::from_text(&orphan.to_text())
            .unwrap_err()
            .contains("no valid parent"));
        let mut empty = sample();
        empty.entities.clear();
        assert!(Prefab::from_text(&empty.to_text()).is_err());
    }

    #[test]
    fn only_dot_prefab_is_a_prefab() {
        assert!(is_prefab(Path::new("Prefabs/Oak.prefab")));
        assert!(is_prefab(Path::new("Oak.PREFAB")));
        assert!(!is_prefab(Path::new("Oak.generator.nature")));
    }
}

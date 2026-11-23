//! Built-in entity prefabs for the Hierarchy create palette.
//!
//! A prefab is a small data recipe: a transform plus the component set that
//! the C ABI can create today. User-defined prefab files can be added later
//! without changing the palette.

use crate::uikit::icons::Icon;

#[derive(Debug, Clone, Copy)]
pub struct Category {
    pub key: &'static str,
    pub label: &'static str,
}

#[derive(Debug, Clone, Copy)]
pub enum LightKind {
    Directional,
    Point,
    Spot,
}

#[derive(Debug, Clone, Copy)]
pub enum Kind {
    Empty,
    Mesh {
        primitive: &'static str,
        scale: [f32; 3],
    },
    Light {
        kind: LightKind,
    },
    Camera,
    Physics {
        primitive: &'static str,
        body: &'static str,
        collider: &'static str,
        scale: [f32; 3],
    },
    DuplicateSelection,
    /// The scene's weather: clouds, fog and rain (one entity; a scene has one).
    Environment,
}

#[derive(Debug, Clone, Copy)]
pub struct Prefab {
    pub key: &'static str,
    pub label: &'static str,
    pub description: &'static str,
    pub category: &'static str,
    pub icon: Icon,
    pub kind: Kind,
}

pub const CATEGORIES: &[Category] = &[
    Category {
        key: "essentials",
        label: "Essentials",
    },
    Category {
        key: "mesh",
        label: "3D Object",
    },
    Category {
        key: "light",
        label: "Light",
    },
    Category {
        key: "camera",
        label: "Camera",
    },
    Category {
        key: "gameplay",
        label: "Gameplay",
    },
    Category {
        key: "world",
        label: "World",
    },
];

pub const PREFABS: &[Prefab] = &[
    Prefab {
        key: "empty",
        label: "Empty Entity",
        description: "Transform-only entity; good as a folder or root.",
        category: "essentials",
        icon: Icon::Generic,
        kind: Kind::Empty,
    },
    Prefab {
        key: "folder",
        label: "Folder",
        description: "Empty grouping entity.",
        category: "essentials",
        icon: Icon::Folder,
        kind: Kind::Empty,
    },
    Prefab {
        key: "duplicate",
        label: "Duplicate Selection",
        description: "Copy the selected entity and all its components.",
        category: "essentials",
        icon: Icon::File,
        kind: Kind::DuplicateSelection,
    },
    Prefab {
        key: "cube",
        label: "Cube",
        description: "Box mesh with a mesh renderer.",
        category: "mesh",
        icon: Icon::Mesh,
        kind: Kind::Mesh {
            primitive: "Cube",
            scale: [1.0; 3],
        },
    },
    Prefab {
        key: "sphere",
        label: "Sphere",
        description: "Sphere mesh with a mesh renderer.",
        category: "mesh",
        icon: Icon::Mesh,
        kind: Kind::Mesh {
            primitive: "Sphere",
            scale: [1.0; 3],
        },
    },
    Prefab {
        key: "capsule",
        label: "Capsule",
        description: "Capsule mesh with a mesh renderer.",
        category: "mesh",
        icon: Icon::Mesh,
        kind: Kind::Mesh {
            primitive: "Capsule",
            scale: [1.0; 3],
        },
    },
    Prefab {
        key: "cylinder",
        label: "Cylinder",
        description: "Cylinder mesh with a mesh renderer.",
        category: "mesh",
        icon: Icon::Mesh,
        kind: Kind::Mesh {
            primitive: "Cylinder",
            scale: [1.0; 3],
        },
    },
    Prefab {
        key: "plane",
        label: "Plane",
        description: "Flat plane mesh with a mesh renderer.",
        category: "mesh",
        icon: Icon::Mesh,
        kind: Kind::Mesh {
            primitive: "Plane",
            scale: [1.0; 3],
        },
    },
    Prefab {
        key: "quad",
        label: "Quad",
        description: "Unit plane, useful for cards and billboards.",
        category: "mesh",
        icon: Icon::Mesh,
        kind: Kind::Mesh {
            primitive: "Plane",
            scale: [1.0; 3],
        },
    },
    Prefab {
        key: "directional_light",
        label: "Directional Light",
        description: "Sun light with shadow settings.",
        category: "light",
        icon: Icon::Light,
        kind: Kind::Light {
            kind: LightKind::Directional,
        },
    },
    Prefab {
        key: "point_light",
        label: "Point Light",
        description: "Omni light with a radius.",
        category: "light",
        icon: Icon::Light,
        kind: Kind::Light {
            kind: LightKind::Point,
        },
    },
    Prefab {
        key: "spot_light",
        label: "Spot Light",
        description: "Cone light with a radius and angle.",
        category: "light",
        icon: Icon::Light,
        kind: Kind::Light {
            kind: LightKind::Spot,
        },
    },
    Prefab {
        key: "camera",
        label: "Camera",
        description: "Camera 3D component.",
        category: "camera",
        icon: Icon::Camera,
        kind: Kind::Camera,
    },
    Prefab {
        key: "physics_cube",
        label: "Physics Cube",
        description: "Cube with a dynamic rigid body and box collider.",
        category: "gameplay",
        icon: Icon::Physics,
        kind: Kind::Physics {
            primitive: "Cube",
            body: "Dynamic",
            collider: "Box",
            scale: [1.0; 3],
        },
    },
    Prefab {
        key: "player",
        label: "Player",
        description: "Capsule with a dynamic body and capsule collider.",
        category: "gameplay",
        icon: Icon::Player,
        kind: Kind::Physics {
            primitive: "Capsule",
            body: "Dynamic",
            collider: "Capsule",
            scale: [1.0; 3],
        },
    },
    Prefab {
        key: "terrain",
        label: "Terrain",
        description: "Large plane marker for terrain work.",
        category: "world",
        icon: Icon::Terrain,
        kind: Kind::Mesh {
            primitive: "Plane",
            scale: [50.0, 1.0, 50.0],
        },
    },
    Prefab {
        key: "water",
        label: "Water",
        description: "Thin plane marker for water surfaces.",
        category: "world",
        icon: Icon::Water,
        kind: Kind::Mesh {
            primitive: "Plane",
            scale: [50.0, 0.1, 50.0],
        },
    },
    Prefab {
        key: "environment",
        label: "Environment",
        description:
            "Clouds, fog and rain for the scene. Selects the existing one when the scene has it.",
        category: "world",
        icon: Icon::Wind,
        kind: Kind::Environment,
    },
];

pub fn find(key: &str) -> Option<&'static Prefab> {
    PREFABS.iter().find(|prefab| prefab.key == key)
}

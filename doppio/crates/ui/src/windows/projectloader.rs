use crate::uikit::icons::Icon;
use crate::uikit::EntityView;
use crate::windows::log::{LogEntry, LogLevel};
use doppio_engine::{AssetInfo, Engine};

pub struct LoadedProject {
    pub engine: Option<Engine>,
    pub entities: Vec<EntityView>,
    pub assets: Vec<AssetInfo>,
    pub log: Vec<LogEntry>,
}

/// Pick a semantic icon from the entity's components first, then from its
/// name for engine concepts that do not have a C ABI component type yet.
pub fn entity_icon(engine: &Engine, id: u32, name: &str) -> Icon {
    let lower = name.to_lowercase();
    if lower.contains("camera") {
        return Icon::Camera;
    }
    if lower.contains("light") || lower.contains("sun") {
        return Icon::Light;
    }
    if lower.contains("terrain") {
        return Icon::Terrain;
    }
    if lower.contains("water") {
        return Icon::Water;
    }
    if lower.contains("wind") {
        return Icon::Wind;
    }
    if lower.contains("volume")
        || lower.contains("render environment")
        || lower.contains("post process")
    {
        return Icon::Volume;
    }
    if lower.contains("player") {
        return Icon::Player;
    }

    let components = engine.entity_components(id);
    let has = |needle: &str| components.iter().any(|component| component.name == needle);
    if has("Camera 3D") {
        Icon::Camera
    } else if has("Light") || has("Directional Light") || has("Point Light") || has("Spot Light") {
        Icon::Light
    } else if has("Mesh Renderer") || has("Mesh Filter") {
        Icon::Mesh
    } else if has("Script") {
        Icon::Script
    } else if has("Rigid Body 2D")
        || has("Rigid Body 3D")
        || has("Collider 2D")
        || has("Collider 3D")
    {
        Icon::Physics
    } else {
        Icon::Generic
    }
}

/// Mirror the engine hierarchy into plain UI views.
pub fn read_entities(engine: &Engine) -> Vec<EntityView> {
    let mut entities = Vec::new();
    for index in 0..engine.entity_count() {
        if let Ok(info) = engine.entity_at(index) {
            entities.push(EntityView {
                id: info.id,
                name: info.name.clone(),
                position: engine.position(info.id).unwrap_or([0.0; 3]),
                rotation: engine.rotation(info.id).unwrap_or([0.0; 3]),
                scale: engine.scale(info.id).unwrap_or([1.0; 3]),
                parent: engine.entity_parent(info.id).ok().flatten(),
                icon: entity_icon(engine, info.id, &info.name),
            });
        }
    }
    entities
}

/// Whether the process was started with a `.caf` scene argument. The startup
/// Project Manager only appears when no scene was requested explicitly, so
/// automated screenshots and fixtures stay deterministic.
pub fn scene_from_args() -> bool {
    std::env::args()
        .nth(1)
        .is_some_and(|path| path.ends_with(".caf"))
}

/// Load the optional `.caf` scene from the first CLI argument and mirror the
/// engine hierarchy into UI-side views. Falls back to fixtures when the C ABI
/// library has not been built.
pub fn load_from_args() -> LoadedProject {
    let mut log = vec![LogEntry::new(LogLevel::Log, "Doppio UI ready")];
    let mut engine = Engine::load_default();
    let mut entities = Vec::new();

    let Some(engine_ref) = engine.as_mut() else {
        log.push(LogEntry::new(
            LogLevel::Engine,
            "C ABI library not found; using fixtures",
        ));
        for (index, name) in ["Entity 1", "Entity 2"].iter().enumerate() {
            entities.push(EntityView {
                id: index as u32,
                name: (*name).to_owned(),
                position: [0.0; 3],
                rotation: [0.0; 3],
                scale: [1.0; 3],
                parent: None,
                icon: Icon::Generic,
            });
        }
        return LoadedProject {
            engine: None,
            entities,
            assets: Vec::new(),
            log,
        };
    };

    log.push(LogEntry::new(
        LogLevel::Engine,
        "Connected to libcaffeine-capi",
    ));

    let scene_path = std::env::args()
        .nth(1)
        .filter(|path| path.ends_with(".caf"));
    let scene_from_args = scene_path.is_some();
    let scene_loaded = if let Some(path) = scene_path {
        match engine_ref.load_scene(&path) {
            Ok(()) => {
                log.push(LogEntry::new(LogLevel::Engine, format!("Loaded {path}")));
                true
            }
            Err(_) => {
                log.push(LogEntry::new(
                    LogLevel::Error,
                    format!("Could not load {path}"),
                ));
                false
            }
        }
    } else {
        false
    };

    if scene_loaded {
        entities = read_entities(engine_ref);
    } else {
        for name in ["Entity 1", "Entity 2"] {
            if let Ok(id) = engine_ref.create_entity(name) {
                entities.push(EntityView {
                    id,
                    name: name.to_owned(),
                    position: [0.0; 3],
                    rotation: [0.0; 3],
                    scale: [1.0; 3],
                    parent: None,
                    icon: Icon::Generic,
                });
            }
        }
    }

    let mut assets = engine_ref.assets();
    // Dev fallback: benchmarks live under assets/benchmarks, so their derived
    // asset root is empty. Show the repository assets when a scene was passed
    // explicitly; real projects get their own assets/raw root from load_scene.
    let has_asset_files = assets.iter().any(|asset| !asset.is_directory);
    if !has_asset_files && scene_from_args {
        if let Ok(cwd) = std::env::current_dir() {
            let candidate = cwd.join("../assets");
            if candidate.exists()
                && engine_ref
                    .set_asset_root(candidate.to_string_lossy().as_ref())
                    .is_ok()
            {
                assets = engine_ref.assets();
                log.push(LogEntry::new(
                    LogLevel::Engine,
                    "Using ../assets as the development asset root",
                ));
            }
        }
    }

    LoadedProject {
        engine,
        entities,
        assets,
        log,
    }
}

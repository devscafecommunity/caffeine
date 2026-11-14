use crate::uikit::EntityView;
use doppio_engine::{AssetInfo, Engine};

pub struct LoadedProject {
    pub engine: Option<Engine>,
    pub entities: Vec<EntityView>,
    pub assets: Vec<AssetInfo>,
    pub log: Vec<String>,
}

/// Mirror the engine hierarchy into plain UI views.
pub fn read_entities(engine: &Engine) -> Vec<EntityView> {
    let mut entities = Vec::new();
    for index in 0..engine.entity_count() {
        if let Ok(info) = engine.entity_at(index) {
            entities.push(EntityView {
                id: info.id,
                name: info.name,
                position: engine.position(info.id).unwrap_or([0.0; 3]),
                rotation: engine.rotation(info.id).unwrap_or([0.0; 3]),
                scale: engine.scale(info.id).unwrap_or([1.0; 3]),
            });
        }
    }
    entities
}

/// Load the optional `.caf` scene from the first CLI argument and mirror the
/// engine hierarchy into UI-side views. Falls back to fixtures when the C ABI
/// library has not been built.
pub fn load_from_args() -> LoadedProject {
    let mut log = vec!["Doppio UI ready".to_owned()];
    let mut engine = Engine::load_default();
    let mut entities = Vec::new();

    let Some(engine_ref) = engine.as_mut() else {
        log.push("C ABI library not found; using fixtures".to_owned());
        for (index, name) in ["Entity 1", "Entity 2"].iter().enumerate() {
            entities.push(EntityView {
                id: index as u32,
                name: (*name).to_owned(),
                position: [0.0; 3],
                rotation: [0.0; 3],
                scale: [1.0; 3],
            });
        }
        return LoadedProject {
            engine: None,
            entities,
            assets: Vec::new(),
            log,
        };
    };

    log.push("Connected to libcaffeine-capi".to_owned());

    let scene_path = std::env::args()
        .nth(1)
        .filter(|path| path.ends_with(".caf"));
    let scene_from_args = scene_path.is_some();
    let scene_loaded = if let Some(path) = scene_path {
        match engine_ref.load_scene(&path) {
            Ok(()) => {
                log.push(format!("Loaded {path}"));
                true
            }
            Err(_) => {
                log.push(format!("Could not load {path}"));
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
                log.push("Using ../assets as the development asset root".to_owned());
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

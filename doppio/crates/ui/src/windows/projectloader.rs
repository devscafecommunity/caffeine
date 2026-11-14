use crate::uikit::EntityView;
use doppio_engine::Engine;

pub struct LoadedProject {
    pub engine: Option<Engine>,
    pub entities: Vec<EntityView>,
    pub log: Vec<String>,
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
            log,
        };
    };

    log.push("Connected to libcaffeine-capi".to_owned());

    let scene_path = std::env::args()
        .nth(1)
        .filter(|path| path.ends_with(".caf"));
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
        let count = engine_ref.entity_count();
        for index in 0..count {
            if let Ok(info) = engine_ref.entity_at(index) {
                entities.push(EntityView {
                    id: info.id,
                    name: info.name,
                    position: engine_ref.position(info.id).unwrap_or([0.0; 3]),
                    rotation: engine_ref.rotation(info.id).unwrap_or([0.0; 3]),
                    scale: engine_ref.scale(info.id).unwrap_or([1.0; 3]),
                });
            }
        }
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

    LoadedProject {
        engine,
        entities,
        log,
    }
}

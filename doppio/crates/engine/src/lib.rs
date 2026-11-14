//! Safe Rust bindings to the Caffeine C ABI.
//!
//! This crate is the only place that knows about `libcaffeine-capi`. The UI
//! crates consume a typed session and never touch function pointers. When the
//! shared library is not built, callers can fall back to fixtures.

use std::ffi::{c_char, c_void, CStr, CString};
use std::path::{Path, PathBuf};

use libloading::Library;

pub const ENTITY_NONE: u32 = u32::MAX;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EngineError {
    Error,
    InvalidArgument,
    NotFound,
}

pub type EngineResult<T> = Result<T, EngineError>;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EntityInfo {
    pub id: u32,
    pub name: String,
}

#[derive(Debug, Clone, Copy, Default)]
pub struct FrameStats {
    pub delta_time: f32,
    pub frame_index: u64,
    pub entity_count: u32,
}

#[repr(C)]
struct RawEntityInfo {
    id: u32,
    name: [c_char; 64],
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
struct RawVec3 {
    x: f32,
    y: f32,
    z: f32,
}

#[repr(C)]
struct RawFrameStats {
    delta_time: f32,
    frame_index: u64,
    entity_count: u32,
}

type CreateFn = unsafe extern "C" fn() -> *mut c_void;
type DestroyFn = unsafe extern "C" fn(*mut c_void);
type LoadSceneFn = unsafe extern "C" fn(*mut c_void, *const c_char) -> i32;
type SaveSceneFn = unsafe extern "C" fn(*mut c_void, *const c_char) -> i32;
type ScenePathFn = unsafe extern "C" fn(*const c_void) -> *const c_char;
type IsDirtyFn = unsafe extern "C" fn(*const c_void) -> i32;
type EntityCountFn = unsafe extern "C" fn(*mut c_void) -> u32;
type EntityAtFn = unsafe extern "C" fn(*mut c_void, u32, *mut RawEntityInfo) -> i32;
type EntityNameFn = unsafe extern "C" fn(*mut c_void, u32, *mut c_char, usize) -> i32;
type CreateEntityFn = unsafe extern "C" fn(*mut c_void, *const c_char) -> u32;
type DestroyEntityFn = unsafe extern "C" fn(*mut c_void, u32) -> i32;
type SetNameFn = unsafe extern "C" fn(*mut c_void, u32, *const c_char) -> i32;
type SelectFn = unsafe extern "C" fn(*mut c_void, u32) -> i32;
type SelectedFn = unsafe extern "C" fn(*mut c_void) -> u32;
type PositionFn = unsafe extern "C" fn(*mut c_void, u32, *mut RawVec3) -> i32;
type SetPositionFn = unsafe extern "C" fn(*mut c_void, u32, RawVec3) -> i32;
type RotationFn = unsafe extern "C" fn(*mut c_void, u32, *mut RawVec3) -> i32;
type SetRotationFn = unsafe extern "C" fn(*mut c_void, u32, RawVec3) -> i32;
type ScaleFn = unsafe extern "C" fn(*mut c_void, u32, *mut RawVec3) -> i32;
type SetScaleFn = unsafe extern "C" fn(*mut c_void, u32, RawVec3) -> i32;
type TickFn = unsafe extern "C" fn(*mut c_void, f32);
type FrameStatsFn = unsafe extern "C" fn(*mut c_void, *mut RawFrameStats);

struct Functions {
    destroy: DestroyFn,
    load_scene: LoadSceneFn,
    save_scene: SaveSceneFn,
    scene_path: ScenePathFn,
    is_dirty: IsDirtyFn,
    entity_count: EntityCountFn,
    entity_at: EntityAtFn,
    entity_name: EntityNameFn,
    create_entity: CreateEntityFn,
    destroy_entity: DestroyEntityFn,
    set_name: SetNameFn,
    select: SelectFn,
    selected: SelectedFn,
    position: PositionFn,
    set_position: SetPositionFn,
    rotation: RotationFn,
    set_rotation: SetRotationFn,
    scale: ScaleFn,
    set_scale: SetScaleFn,
    tick: TickFn,
    frame_stats: FrameStatsFn,
}

pub struct Engine {
    _library: Library,
    session: *mut c_void,
    functions: Functions,
}

unsafe fn load_symbol<T: Copy>(library: &Library, name: &[u8]) -> Option<T> {
    library.get::<T>(name).ok().map(|symbol| *symbol)
}

fn default_library_path() -> PathBuf {
    #[cfg(target_os = "windows")]
    const FILE: &str = "caffeine-capi.dll";
    #[cfg(target_os = "macos")]
    const FILE: &str = "libcaffeine-capi.dylib";
    #[cfg(not(any(target_os = "windows", target_os = "macos")))]
    const FILE: &str = "libcaffeine-capi.so";

    if let Some(path) = std::env::var_os("CAFFEINE_CAPI_LIB") {
        return PathBuf::from(path);
    }
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
        .join("..")
        .join("build")
        .join(FILE)
}

impl Engine {
    pub fn load_default() -> Option<Self> {
        Self::load(&default_library_path())
    }

    pub fn load(path: &Path) -> Option<Self> {
        let library = unsafe { Library::new(path) }.ok()?;
        let create: CreateFn = unsafe { load_symbol(&library, b"caffeine_session_create\0") }?;
        let functions = Functions {
            destroy: unsafe { load_symbol(&library, b"caffeine_session_destroy\0") }?,
            load_scene: unsafe { load_symbol(&library, b"caffeine_session_load_scene\0") }?,
            save_scene: unsafe { load_symbol(&library, b"caffeine_session_save_scene\0") }?,
            scene_path: unsafe { load_symbol(&library, b"caffeine_session_scene_path\0") }?,
            is_dirty: unsafe { load_symbol(&library, b"caffeine_session_is_dirty\0") }?,
            entity_count: unsafe { load_symbol(&library, b"caffeine_session_entity_count\0") }?,
            entity_at: unsafe { load_symbol(&library, b"caffeine_session_entity_at\0") }?,
            entity_name: unsafe { load_symbol(&library, b"caffeine_session_entity_name\0") }?,
            create_entity: unsafe { load_symbol(&library, b"caffeine_session_create_entity\0") }?,
            destroy_entity: unsafe { load_symbol(&library, b"caffeine_session_destroy_entity\0") }?,
            set_name: unsafe { load_symbol(&library, b"caffeine_session_set_entity_name\0") }?,
            select: unsafe { load_symbol(&library, b"caffeine_session_select_entity\0") }?,
            selected: unsafe { load_symbol(&library, b"caffeine_session_selected_entity\0") }?,
            position: unsafe { load_symbol(&library, b"caffeine_session_entity_position\0") }?,
            set_position: unsafe {
                load_symbol(&library, b"caffeine_session_set_entity_position\0")
            }?,
            rotation: unsafe { load_symbol(&library, b"caffeine_session_entity_rotation\0") }?,
            set_rotation: unsafe {
                load_symbol(&library, b"caffeine_session_set_entity_rotation\0")
            }?,
            scale: unsafe { load_symbol(&library, b"caffeine_session_entity_scale\0") }?,
            set_scale: unsafe { load_symbol(&library, b"caffeine_session_set_entity_scale\0") }?,
            tick: unsafe { load_symbol(&library, b"caffeine_session_tick\0") }?,
            frame_stats: unsafe { load_symbol(&library, b"caffeine_session_frame_stats\0") }?,
        };
        let session = unsafe { create() };
        if session.is_null() {
            return None;
        }
        Some(Self {
            _library: library,
            session,
            functions,
        })
    }

    pub fn load_scene(&mut self, path: &str) -> EngineResult<()> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { (self.functions.load_scene)(self.session, path.as_ptr()) })
    }

    pub fn save_scene(&mut self, path: &str) -> EngineResult<()> {
        let path = CString::new(path).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { (self.functions.save_scene)(self.session, path.as_ptr()) })
    }

    pub fn scene_path(&self) -> Option<String> {
        let path = unsafe { (self.functions.scene_path)(self.session) };
        if path.is_null() {
            return None;
        }
        let path = unsafe { CStr::from_ptr(path) }
            .to_string_lossy()
            .into_owned();
        if path.is_empty() {
            None
        } else {
            Some(path)
        }
    }

    pub fn is_dirty(&self) -> bool {
        unsafe { (self.functions.is_dirty)(self.session) != 0 }
    }

    pub fn entity_count(&self) -> u32 {
        unsafe { (self.functions.entity_count)(self.session) }
    }

    pub fn entity_at(&self, index: u32) -> EngineResult<EntityInfo> {
        let mut info = RawEntityInfo {
            id: ENTITY_NONE,
            name: [0; 64],
        };
        result(unsafe { (self.functions.entity_at)(self.session, index, &mut info) })?;
        let name = unsafe { CStr::from_ptr(info.name.as_ptr()) }
            .to_string_lossy()
            .into_owned();
        Ok(EntityInfo { id: info.id, name })
    }

    pub fn entity_name(&self, entity: u32) -> EngineResult<String> {
        let mut buffer = [0 as c_char; 64];
        result(unsafe {
            (self.functions.entity_name)(self.session, entity, buffer.as_mut_ptr(), buffer.len())
        })?;
        Ok(unsafe { CStr::from_ptr(buffer.as_ptr()) }
            .to_string_lossy()
            .into_owned())
    }

    pub fn create_entity(&mut self, name: &str) -> EngineResult<u32> {
        let name = CString::new(name).map_err(|_| EngineError::InvalidArgument)?;
        let id = unsafe { (self.functions.create_entity)(self.session, name.as_ptr()) };
        if id == ENTITY_NONE {
            Err(EngineError::Error)
        } else {
            Ok(id)
        }
    }

    pub fn destroy_entity(&mut self, entity: u32) -> EngineResult<()> {
        result(unsafe { (self.functions.destroy_entity)(self.session, entity) })
    }

    pub fn set_entity_name(&mut self, entity: u32, name: &str) -> EngineResult<()> {
        let name = CString::new(name).map_err(|_| EngineError::InvalidArgument)?;
        result(unsafe { (self.functions.set_name)(self.session, entity, name.as_ptr()) })
    }

    pub fn select_entity(&mut self, entity: u32) -> EngineResult<()> {
        result(unsafe { (self.functions.select)(self.session, entity) })
    }

    pub fn selected_entity(&self) -> u32 {
        unsafe { (self.functions.selected)(self.session) }
    }

    pub fn position(&self, entity: u32) -> EngineResult<[f32; 3]> {
        read_vec3(|out| unsafe { (self.functions.position)(self.session, entity, out) })
    }

    pub fn set_position(&mut self, entity: u32, position: [f32; 3]) -> EngineResult<()> {
        result(unsafe { (self.functions.set_position)(self.session, entity, to_raw(position)) })
    }

    pub fn rotation(&self, entity: u32) -> EngineResult<[f32; 3]> {
        read_vec3(|out| unsafe { (self.functions.rotation)(self.session, entity, out) })
    }

    pub fn set_rotation(&mut self, entity: u32, euler_degrees: [f32; 3]) -> EngineResult<()> {
        result(unsafe {
            (self.functions.set_rotation)(self.session, entity, to_raw(euler_degrees))
        })
    }

    pub fn scale(&self, entity: u32) -> EngineResult<[f32; 3]> {
        read_vec3(|out| unsafe { (self.functions.scale)(self.session, entity, out) })
    }

    pub fn set_scale(&mut self, entity: u32, scale: [f32; 3]) -> EngineResult<()> {
        result(unsafe { (self.functions.set_scale)(self.session, entity, to_raw(scale)) })
    }

    pub fn tick(&mut self, delta_time: f32) {
        unsafe { (self.functions.tick)(self.session, delta_time) }
    }

    pub fn frame_stats(&self) -> FrameStats {
        let mut stats = RawFrameStats {
            delta_time: 0.0,
            frame_index: 0,
            entity_count: 0,
        };
        unsafe { (self.functions.frame_stats)(self.session, &mut stats) };
        FrameStats {
            delta_time: stats.delta_time,
            frame_index: stats.frame_index,
            entity_count: stats.entity_count,
        }
    }
}

impl Drop for Engine {
    fn drop(&mut self) {
        unsafe { (self.functions.destroy)(self.session) };
    }
}

fn to_raw(value: [f32; 3]) -> RawVec3 {
    RawVec3 {
        x: value[0],
        y: value[1],
        z: value[2],
    }
}

fn read_vec3(call: impl FnOnce(*mut RawVec3) -> i32) -> EngineResult<[f32; 3]> {
    let mut value = RawVec3::default();
    result(call(&mut value))?;
    Ok([value.x, value.y, value.z])
}

fn result(value: i32) -> EngineResult<()> {
    match value {
        0 => Ok(()),
        2 => Err(EngineError::InvalidArgument),
        3 => Err(EngineError::NotFound),
        _ => Err(EngineError::Error),
    }
}

#[cfg(test)]
pub(crate) fn test_lock() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    LOCK.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bridge_roundtrip_when_library_is_built() {
        let _guard = test_lock();
        let Some(mut engine) = Engine::load_default() else {
            eprintln!(
                "skipping C ABI bridge test: {} is not built",
                default_library_path().display()
            );
            return;
        };

        let player = engine.create_entity("Player").expect("create entity");
        let enemy = engine.create_entity("Enemy").expect("create enemy");
        assert_eq!(engine.entity_count(), 2);

        assert_eq!(engine.entity_name(player).unwrap(), "Player");
        engine.set_entity_name(player, "Hero").unwrap();
        assert_eq!(engine.entity_name(player).unwrap(), "Hero");
        engine.select_entity(enemy).unwrap();
        assert_eq!(engine.selected_entity(), enemy);

        assert_eq!(engine.position(player).unwrap_err(), EngineError::NotFound);
        engine.set_position(player, [1.0, 2.0, 3.0]).unwrap();
        assert_eq!(engine.position(player).unwrap(), [1.0, 2.0, 3.0]);

        engine.set_rotation(player, [10.0, 20.0, 30.0]).unwrap();
        let rotation = engine.rotation(player).unwrap();
        assert!((rotation[0] - 10.0).abs() < 0.01);
        assert!((rotation[1] - 20.0).abs() < 0.01);
        assert!((rotation[2] - 30.0).abs() < 0.01);

        engine.set_scale(player, [2.0, 3.0, 4.0]).unwrap();
        assert_eq!(engine.scale(player).unwrap(), [2.0, 3.0, 4.0]);
        assert!(engine.is_dirty());

        let path = std::env::temp_dir().join("caffeine_capi_rust_roundtrip.caf");
        engine.save_scene(path.to_str().unwrap()).unwrap();
        assert!(!engine.is_dirty());
        assert_eq!(engine.scene_path().as_deref(), path.to_str());

        let first = engine.entity_at(0).unwrap();
        assert!(first.id == player || first.id == enemy);
        assert_eq!(engine.entity_at(99).unwrap_err(), EngineError::NotFound);

        engine.destroy_entity(player).unwrap();
        assert_eq!(engine.entity_count(), 1);
        assert_eq!(
            engine.entity_name(player).unwrap_err(),
            EngineError::NotFound
        );
        let _ = std::fs::remove_file(path);
    }
}

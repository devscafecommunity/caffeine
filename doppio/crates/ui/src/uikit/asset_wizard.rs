//! Asset creation / import / packing logic behind the Assets wizard.
//!
//! Everything here is plain file-system work against an asset root, so it can
//! be tested with temp directories and never touches the engine. The wizard
//! panel and `App` only translate messages into these calls.
//!
//! Raw assets live under the project asset root (`assets/raw`). The engine
//! recognises them by extension (see `assetKind` in the C ABI); `caf-pack`
//! turns that raw tree into the `.cap` archive shipped with a game.

use crate::uikit::dryad;
use std::path::{Path, PathBuf};

/// Something the "Create" tab can make.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Template {
    Folder,
    Material,
    TextureSet,
    LuaScript,
    DryadScript,
}

impl Template {
    pub const ALL: [Template; 5] = [
        Template::Folder,
        Template::Material,
        Template::TextureSet,
        Template::LuaScript,
        Template::DryadScript,
    ];

    pub fn label(self) -> &'static str {
        match self {
            Template::Folder => "Folder",
            Template::Material => "Material",
            Template::TextureSet => "Texture Set",
            Template::LuaScript => "Lua Script",
            Template::DryadScript => "Dryad Script",
        }
    }

    pub fn description(self) -> &'static str {
        match self {
            Template::Folder => "Organise assets in the current folder.",
            Template::Material => "PBR material; opens the Material editor.",
            Template::TextureSet => "UDIM/layer painter; opens the Texture Set editor.",
            Template::LuaScript => "Lua behaviour with onCreate / onUpdate.",
            Template::DryadScript => "Visual script (.dyd + .node.json) with an OnUpdate node.",
        }
    }

    /// Extension written to disk, when the template is a plain file.
    pub fn extension(self) -> Option<&'static str> {
        match self {
            Template::Folder => None,
            Template::Material => Some("mat"),
            Template::TextureSet => Some("texset"),
            Template::LuaScript => Some("lua"),
            Template::DryadScript => Some("dyd"),
        }
    }

    /// True when creating it just opens the matching editor on an unsaved
    /// document (the editor owns saving), instead of writing a file now.
    pub fn opens_editor(self) -> bool {
        matches!(self, Template::Material | Template::TextureSet)
    }
}

/// Keeps letters, digits, `-`, `_` and single spaces; rejects empty names.
pub fn sanitize_name(name: &str) -> Option<String> {
    let cleaned: String = name
        .trim()
        .chars()
        .map(|c| {
            if c.is_alphanumeric() || matches!(c, '-' | '_' | ' ' | '.') {
                c
            } else {
                '_'
            }
        })
        .collect();
    let cleaned = cleaned.trim_matches(|c| c == '.' || c == ' ').to_owned();
    (!cleaned.is_empty() && cleaned != "..").then_some(cleaned)
}

/// Joins a relative directory and name with `/` separators.
pub fn join_relative(directory: &str, name: &str) -> String {
    let directory = directory.trim_matches('/');
    if directory.is_empty() {
        name.to_owned()
    } else {
        format!("{directory}/{name}")
    }
}

/// First free `stem.ext`, `stem-1.ext`, … in `directory`.
pub fn unique_path(directory: &Path, stem: &str, extension: Option<&str>) -> PathBuf {
    let file = |suffix: String| match extension {
        Some(ext) => directory.join(format!("{stem}{suffix}.{ext}")),
        None => directory.join(format!("{stem}{suffix}")),
    };
    let mut candidate = file(String::new());
    let mut counter = 1;
    while candidate.exists() {
        candidate = file(format!("-{counter}"));
        counter += 1;
    }
    candidate
}

const LUA_TEMPLATE: &str = "-- {name}\n\
-- Attach this script to an entity; the engine calls these hooks in play mode.\n\n\
function onCreate(entityId)\nend\n\n\
function onUpdate(entityId, dt)\nend\n";

/// Creates a folder or script file under `root/directory`. Returns the new
/// asset's path relative to `root`. Material and texture-set templates are
/// created by their editors, so they are rejected here.
pub fn create(
    root: &Path,
    directory: &str,
    template: Template,
    name: &str,
) -> Result<String, String> {
    let name = sanitize_name(name).ok_or("Enter a name (letters, digits, - and _).")?;
    let parent = root.join(directory.trim_matches('/'));
    std::fs::create_dir_all(&parent).map_err(|error| format!("{}: {error}", parent.display()))?;
    let target = unique_path(&parent, &name, template.extension());
    match template {
        Template::Folder => std::fs::create_dir_all(&target)
            .map_err(|error| format!("{}: {error}", target.display()))?,
        Template::LuaScript => std::fs::write(&target, LUA_TEMPLATE.replace("{name}", &name))
            .map_err(|error| format!("{}: {error}", target.display()))?,
        Template::DryadScript => {
            let file_name = target
                .file_name()
                .map(|name| name.to_string_lossy().into_owned())
                .unwrap_or_default();
            let emitted = dryad::Graph::default_for_script(&file_name).emit()?;
            std::fs::write(&target, emitted.dyd)
                .map_err(|error| format!("{}: {error}", target.display()))?;
            let json = dryad::node_json_path_for_script(&target);
            std::fs::write(&json, emitted.node_json)
                .map_err(|error| format!("{}: {error}", json.display()))?;
        }
        Template::Material | Template::TextureSet => {
            return Err(format!("{} is created from its editor.", template.label()))
        }
    }
    target
        .strip_prefix(root)
        .map(|relative| relative.to_string_lossy().replace('\\', "/"))
        .map_err(|error| error.to_string())
}

/// What the engine / caf-pack will treat an imported file as.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ImportKind {
    Texture,
    Mesh,
    Audio,
    Script,
    Material,
    Packed,
    Animation,
    Localization,
}

impl ImportKind {
    pub fn label(self) -> &'static str {
        match self {
            ImportKind::Texture => "texture",
            ImportKind::Mesh => "mesh",
            ImportKind::Audio => "audio",
            ImportKind::Script => "script",
            ImportKind::Material => "material",
            ImportKind::Packed => "packed asset",
            ImportKind::Animation => "animation",
            ImportKind::Localization => "localization",
        }
    }
}

/// Classifies by extension, mirroring the engine's `assetKind` plus the
/// `.lang` tables caf-pack's Scribe processor turns into string tables.
pub fn classify(path: &Path) -> Option<ImportKind> {
    let extension = path.extension()?.to_string_lossy().to_ascii_lowercase();
    Some(match extension.as_str() {
        "png" | "jpg" | "jpeg" | "tga" | "gif" | "hdr" | "exr" => ImportKind::Texture,
        "obj" | "glb" | "gltf" | "fbx" => ImportKind::Mesh,
        "wav" | "ogg" | "mp3" => ImportKind::Audio,
        "lua" | "dyd" => ImportKind::Script,
        "mat" | "material" => ImportKind::Material,
        "caf" | "cap" => ImportKind::Packed,
        "anim" => ImportKind::Animation,
        "lang" => ImportKind::Localization,
        _ => return None,
    })
}

#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct ImportReport {
    /// Relative paths (inside the asset root) that were copied.
    pub imported: Vec<String>,
    /// `(source, reason)` for everything that was not copied.
    pub skipped: Vec<(String, String)>,
}

impl ImportReport {
    pub fn summary(&self) -> String {
        match (self.imported.len(), self.skipped.len()) {
            (0, 0) => "Nothing to import.".to_owned(),
            (imported, 0) => format!("Imported {imported} file(s)."),
            (0, skipped) => format!("Nothing imported; {skipped} skipped."),
            (imported, skipped) => format!("Imported {imported} file(s); {skipped} skipped."),
        }
    }
}

/// Copies files (or whole folders) into `root/directory`, never overwriting:
/// a clashing name gets a `-1`, `-2`… suffix. Unsupported file types are
/// skipped and reported instead of silently dropped.
pub fn import(root: &Path, directory: &str, sources: &[PathBuf]) -> ImportReport {
    let mut report = ImportReport::default();
    let destination = root.join(directory.trim_matches('/'));
    if let Err(error) = std::fs::create_dir_all(&destination) {
        report
            .skipped
            .push((destination.display().to_string(), error.to_string()));
        return report;
    }
    for source in sources {
        import_one(root, &destination, source, &mut report);
    }
    report
}

fn import_one(root: &Path, destination: &Path, source: &Path, report: &mut ImportReport) {
    let label = source.display().to_string();
    if source.is_dir() {
        let name = source
            .file_name()
            .map(|name| name.to_string_lossy().into_owned())
            .unwrap_or_else(|| "folder".to_owned());
        let target = unique_path(destination, &name, None);
        if let Err(error) = std::fs::create_dir_all(&target) {
            report.skipped.push((label, error.to_string()));
            return;
        }
        match std::fs::read_dir(source) {
            Ok(entries) => {
                for entry in entries.flatten() {
                    import_one(root, &target, &entry.path(), report);
                }
            }
            Err(error) => report.skipped.push((label, error.to_string())),
        }
        return;
    }
    if !source.is_file() {
        report.skipped.push((label, "file not found".to_owned()));
        return;
    }
    if classify(source).is_none() {
        report
            .skipped
            .push((label, "unsupported file type".to_owned()));
        return;
    }
    let stem = source
        .file_stem()
        .map(|stem| stem.to_string_lossy().into_owned())
        .unwrap_or_else(|| "asset".to_owned());
    let extension = source
        .extension()
        .map(|extension| extension.to_string_lossy().into_owned());
    let target = unique_path(destination, &stem, extension.as_deref());
    match std::fs::copy(source, &target) {
        Ok(_) => {
            if let Ok(relative) = target.strip_prefix(root) {
                report
                    .imported
                    .push(relative.to_string_lossy().replace('\\', "/"));
            }
        }
        Err(error) => report.skipped.push((label, error.to_string())),
    }
}

/// Looks for the `caf-pack` CLI: `CAF_PACK`, the engine checkout
/// (`CAFFEINE_ENGINE_ROOT`), a sibling `caf-pack/build/src`, then `PATH`.
pub fn find_packer() -> Option<PathBuf> {
    let name = if cfg!(windows) {
        "caf-pack.exe"
    } else {
        "caf-pack"
    };
    let mut candidates: Vec<PathBuf> = Vec::new();
    if let Ok(path) = std::env::var("CAF_PACK") {
        candidates.push(PathBuf::from(path));
    }
    if let Ok(root) = std::env::var("CAFFEINE_ENGINE_ROOT") {
        let root = PathBuf::from(root);
        candidates.push(root.join("caf-pack/build/src").join(name));
        candidates.push(root.join("caf-pack/build").join(name));
    }
    if let Ok(current) = std::env::current_dir() {
        for ancestor in current.ancestors().take(5) {
            candidates.push(ancestor.join("caf-pack/build/src").join(name));
        }
    }
    if let Some(path) = std::env::var_os("PATH") {
        candidates.extend(std::env::split_paths(&path).map(|dir| dir.join(name)));
    }
    candidates.into_iter().find(|path| path.is_file())
}

/// Arguments for packing `root` into `output` (`caf-pack --input … --output …`).
pub fn pack_arguments(root: &Path, output: &Path, generate_ids: Option<&Path>) -> Vec<String> {
    let mut arguments = vec![
        "--input".to_owned(),
        root.display().to_string(),
        "--output".to_owned(),
        output.display().to_string(),
    ];
    if let Some(header) = generate_ids {
        arguments.push("--gen-ids".to_owned());
        arguments.push(header.display().to_string());
    }
    arguments
}

/// Runs the packer and returns its combined output (or the failure).
pub fn run_packer(packer: &Path, arguments: &[String]) -> Result<String, String> {
    let output = std::process::Command::new(packer)
        .args(arguments)
        .output()
        .map_err(|error| format!("{}: {error}", packer.display()))?;
    let mut text = String::from_utf8_lossy(&output.stdout).into_owned();
    text.push_str(&String::from_utf8_lossy(&output.stderr));
    if output.status.success() {
        Ok(text.trim().to_owned())
    } else {
        Err(format!("caf-pack failed: {}", text.trim()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(label: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!(
            "doppio-wizard-{label}-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&dir).expect("temp dir");
        dir
    }

    #[test]
    fn names_are_sanitised_and_empty_names_rejected() {
        assert_eq!(sanitize_name("  Rock Wall "), Some("Rock Wall".to_owned()));
        assert_eq!(sanitize_name("a/b\\c"), Some("a_b_c".to_owned()));
        assert_eq!(sanitize_name("  .. "), None);
        assert_eq!(sanitize_name(""), None);
    }

    #[test]
    fn creating_assets_never_overwrites_and_writes_templates() {
        let root = temp("create");
        let folder = create(&root, "", Template::Folder, "Props").expect("folder");
        assert_eq!(folder, "Props");
        let first = create(&root, "Props", Template::LuaScript, "spin").expect("lua");
        let second = create(&root, "Props", Template::LuaScript, "spin").expect("lua 2");
        assert_eq!(first, "Props/spin.lua");
        assert_eq!(second, "Props/spin-1.lua");
        let lua = std::fs::read_to_string(root.join(&first)).expect("read");
        assert!(lua.contains("function onUpdate(entityId, dt)"));

        let dryad = create(&root, "Props", Template::DryadScript, "logic").expect("dryad");
        assert_eq!(dryad, "Props/logic.dyd");
        assert!(root.join("Props/logic.dyd").is_file());
        assert!(root.join("Props/logic.node.json").is_file());
        assert!(create(&root, "", Template::Material, "m").is_err());
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn import_copies_supported_files_and_reports_the_rest() {
        let root = temp("import-root");
        let source = temp("import-src");
        std::fs::write(source.join("rock.png"), b"png").expect("write");
        std::fs::write(source.join("hero.glb"), b"glb").expect("write");
        std::fs::write(source.join("notes.txt"), b"x").expect("write");
        let sources = vec![
            source.join("rock.png"),
            source.join("hero.glb"),
            source.join("notes.txt"),
            source.join("missing.wav"),
        ];
        let report = import(&root, "Models", &sources);
        assert_eq!(report.imported, vec!["Models/rock.png", "Models/hero.glb"]);
        assert_eq!(report.skipped.len(), 2);
        // Importing again keeps the originals.
        let again = import(&root, "Models", &[source.join("rock.png")]);
        assert_eq!(again.imported, vec!["Models/rock-1.png"]);

        let folder = import(&root, "", std::slice::from_ref(&source));
        assert!(folder
            .imported
            .iter()
            .any(|path| path.ends_with("rock.png")));
        let _ = std::fs::remove_dir_all(root);
        let _ = std::fs::remove_dir_all(source);
    }

    #[test]
    fn classify_matches_engine_kinds() {
        assert_eq!(classify(Path::new("a.PNG")), Some(ImportKind::Texture));
        assert_eq!(classify(Path::new("a.glb")), Some(ImportKind::Mesh));
        assert_eq!(classify(Path::new("a.wav")), Some(ImportKind::Audio));
        assert_eq!(
            classify(Path::new("a.lang")),
            Some(ImportKind::Localization)
        );
        assert_eq!(classify(Path::new("a.zip")), None);
    }

    #[test]
    fn pack_arguments_follow_the_cli() {
        let args = pack_arguments(
            Path::new("raw"),
            Path::new("out.cap"),
            Some(Path::new("ids.hpp")),
        );
        assert_eq!(
            args,
            [
                "--input",
                "raw",
                "--output",
                "out.cap",
                "--gen-ids",
                "ids.hpp"
            ]
        );
    }
}

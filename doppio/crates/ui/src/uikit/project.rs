//! Project manifest and recent-project storage.
//!
//! The format matches the existing `project.caffeine` convention: a small JSON
//! file at the project root plus `assets/raw`, `assets/processed`, `scripts`,
//! `scenes`, `materials`, `texture_sets` and `build` folders.

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

pub const PROJECT_FILE: &str = "project.caffeine";

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ProjectPaths {
    #[serde(default = "default_assets_raw")]
    pub assets_raw: String,
    #[serde(default = "default_assets_processed")]
    pub assets_processed: String,
    #[serde(default = "default_scripts")]
    pub scripts: String,
}

impl Default for ProjectPaths {
    fn default() -> Self {
        Self {
            assets_raw: default_assets_raw(),
            assets_processed: default_assets_processed(),
            scripts: default_scripts(),
        }
    }
}

fn default_assets_raw() -> String {
    "assets/raw".to_owned()
}

fn default_assets_processed() -> String {
    "assets/processed".to_owned()
}

fn default_scripts() -> String {
    "scripts".to_owned()
}

fn default_engine_version() -> String {
    env!("CARGO_PKG_VERSION").to_owned()
}

fn default_template() -> String {
    "Empty".to_owned()
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProjectManifest {
    #[serde(default)]
    pub project_name: String,
    #[serde(default = "default_engine_version")]
    pub engine_version: String,
    #[serde(default)]
    pub paths: ProjectPaths,
    #[serde(default = "default_template")]
    pub template_type: String,
    #[serde(default)]
    pub last_scene: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProjectInfo {
    pub name: String,
    pub root: PathBuf,
    pub template: String,
    pub last_scene: String,
    pub paths: ProjectPaths,
}

impl ProjectInfo {
    pub fn manifest_path(&self) -> PathBuf {
        self.root.join(PROJECT_FILE)
    }

    pub fn display_path(&self) -> String {
        self.root.display().to_string()
    }

    /// Absolute asset root used by the editor asset browser.
    pub fn asset_root(&self) -> PathBuf {
        self.root.join(&self.paths.assets_raw)
    }

    pub fn scene_path(&self) -> Option<PathBuf> {
        if self.last_scene.is_empty() {
            let default = self.root.join("scenes").join("main.caf");
            return default.exists().then_some(default);
        }
        let relative = Path::new(&self.last_scene);
        let scene = if relative.is_absolute() {
            relative.to_path_buf()
        } else {
            self.root.join(relative)
        };
        scene.exists().then_some(scene)
    }
}

/// `$CAFFEINE_PROJECTS_DIR`, then `~/Documents/CaffeineProjects`.
pub fn default_projects_directory() -> PathBuf {
    if let Some(directory) = std::env::var_os("CAFFEINE_PROJECTS_DIR") {
        return PathBuf::from(directory);
    }
    home_directory().join("Documents").join("CaffeineProjects")
}

fn home_directory() -> PathBuf {
    std::env::var_os("HOME")
        .or_else(|| std::env::var_os("USERPROFILE"))
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."))
}

/// The shared recent-project file. `$CAFFEINE_RECENT_PROJECTS` overrides it
/// for tests, portable installs and smoke flows.
pub fn recent_projects_file() -> PathBuf {
    if let Some(path) = std::env::var_os("CAFFEINE_RECENT_PROJECTS") {
        return PathBuf::from(path);
    }
    #[cfg(target_os = "windows")]
    {
        if let Some(app_data) = std::env::var_os("APPDATA") {
            return PathBuf::from(app_data).join("caffeine").join("recent.txt");
        }
    }
    std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".config")))
        .unwrap_or_else(|| PathBuf::from(".caffeine"))
        .join("caffeine")
        .join("recent.txt")
}

/// Validates a project name before it becomes a directory name.
pub fn validate_project_name(name: &str) -> Result<&str, String> {
    let name = name.trim();
    if name.is_empty() {
        return Err("Project name cannot be empty".to_owned());
    }
    if name == "." || name == ".." {
        return Err("Project name cannot be a path segment".to_owned());
    }
    if name.chars().any(|character| {
        matches!(
            character,
            '/' | '\\' | ':' | '*' | '?' | '"' | '<' | '>' | '|'
        )
    }) {
        return Err("Project name cannot contain path or reserved characters".to_owned());
    }
    Ok(name)
}

pub fn create_project(location: &Path, name: &str, template: &str) -> Result<ProjectInfo, String> {
    let name = validate_project_name(name)?;
    let root = location.join(name);
    if root.join(PROJECT_FILE).exists() {
        return Err(format!("A project already exists at {}", root.display()));
    }
    std::fs::create_dir_all(&root).map_err(|error| error.to_string())?;
    for folder in [
        "assets/raw",
        "assets/raw/Animations",
        "assets/raw/Models",
        "assets/raw/Materials",
        "assets/raw/Textures",
        "assets/raw/Audio",
        "assets/raw/Prefabs",
        "assets/raw/Sky",
        "assets/processed",
        "scripts",
        "scenes",
        "build",
    ] {
        std::fs::create_dir_all(root.join(folder)).map_err(|error| error.to_string())?;
    }
    let paths = ProjectPaths::default();
    let manifest = ProjectManifest {
        project_name: name.to_owned(),
        engine_version: default_engine_version(),
        paths: paths.clone(),
        template_type: template.to_owned(),
        last_scene: String::new(),
    };
    write_manifest(&root, &manifest)?;
    Ok(ProjectInfo {
        name: name.to_owned(),
        root,
        template: template.to_owned(),
        last_scene: String::new(),
        paths,
    })
}

pub fn load_project(path: &Path) -> Result<ProjectInfo, String> {
    let manifest_path = if path.file_name().and_then(|name| name.to_str()) == Some(PROJECT_FILE) {
        path.to_path_buf()
    } else {
        path.join(PROJECT_FILE)
    };
    let text = std::fs::read_to_string(&manifest_path)
        .map_err(|error| format!("{}: {error}", manifest_path.display()))?;
    let manifest: ProjectManifest =
        serde_json::from_str(&text).map_err(|error| format!("project.caffeine: {error}"))?;
    let root = manifest_path
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(|| PathBuf::from("."));
    let name = if manifest.project_name.trim().is_empty() {
        root.file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("Project")
            .to_owned()
    } else {
        manifest.project_name
    };
    Ok(ProjectInfo {
        name,
        root,
        template: manifest.template_type,
        last_scene: manifest.last_scene,
        paths: manifest.paths,
    })
}

pub fn update_last_scene(root: &Path, scene: &str) -> Result<(), String> {
    let info = load_project(root)?;
    let manifest = ProjectManifest {
        project_name: info.name,
        engine_version: default_engine_version(),
        paths: info.paths.clone(),
        template_type: info.template,
        last_scene: scene.to_owned(),
    };
    write_manifest(&info.root, &manifest)
}

fn write_manifest(root: &Path, manifest: &ProjectManifest) -> Result<(), String> {
    let text = serde_json::to_string_pretty(manifest).map_err(|error| error.to_string())?;
    std::fs::write(root.join(PROJECT_FILE), text).map_err(|error| error.to_string())
}

/// Projects directly inside `directory`.
pub fn scan_projects(directory: &Path) -> Vec<ProjectInfo> {
    let mut projects = Vec::new();
    let Ok(entries) = std::fs::read_dir(directory) else {
        return projects;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() || path.file_name().and_then(|name| name.to_str()) == Some(PROJECT_FILE) {
            if let Ok(project) = load_project(&path) {
                projects.push(project);
            }
        }
    }
    sort_projects(&mut projects);
    projects
}

/// Projects anywhere below `directory` (up to `MAX_SCAN_DEPTH`), matching the
/// archived startup dialog's recursive browse behaviour.
pub fn scan_projects_recursive(directory: &Path) -> Vec<ProjectInfo> {
    const MAX_SCAN_DEPTH: usize = 6;
    let mut projects = Vec::new();
    let mut seen = std::collections::HashSet::new();
    if directory.join(PROJECT_FILE).is_file() {
        if let Ok(project) = load_project(directory) {
            seen.insert(project.root.clone());
            projects.push(project);
        }
    }
    scan_recursive(directory, 0, MAX_SCAN_DEPTH, &mut projects, &mut seen);
    sort_projects(&mut projects);
    projects
}

fn scan_recursive(
    directory: &Path,
    depth: usize,
    max_depth: usize,
    projects: &mut Vec<ProjectInfo>,
    seen: &mut std::collections::HashSet<PathBuf>,
) {
    let Ok(entries) = std::fs::read_dir(directory) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().into_owned();
        if name.starts_with('.') {
            continue;
        }
        let Ok(file_type) = entry.file_type() else {
            continue;
        };
        if file_type.is_dir() {
            if depth >= max_depth || matches!(name.as_str(), "target" | "node_modules" | "build") {
                continue;
            }
            if path.join(PROJECT_FILE).is_file() {
                if let Ok(project) = load_project(&path) {
                    if seen.insert(project.root.clone()) {
                        projects.push(project);
                    }
                }
            }
            scan_recursive(&path, depth + 1, max_depth, projects, seen);
        } else if name == PROJECT_FILE {
            if let Ok(project) = load_project(&path) {
                if seen.insert(project.root.clone()) {
                    projects.push(project);
                }
            }
        }
    }
}

fn sort_projects(projects: &mut [ProjectInfo]) {
    projects.sort_by(|a, b| {
        a.name
            .to_lowercase()
            .cmp(&b.name.to_lowercase())
            .then_with(|| a.root.cmp(&b.root))
    });
}

/// Records a project in the default recent file.
pub fn record_recent(root: &Path) {
    record_recent_at(&recent_projects_file(), root);
}

pub fn record_recent_at(store: &Path, root: &Path) {
    let mut roots = load_recent_roots(store);
    let canonical = canonical_root(root);
    roots.retain(|candidate| candidate != &canonical);
    roots.insert(0, canonical);
    roots.truncate(12);
    save_recent_roots(store, &roots);
}

pub fn forget_recent_at(store: &Path, root: &Path) {
    let canonical = canonical_root(root);
    let mut roots = load_recent_roots(store);
    roots.retain(|candidate| candidate != &canonical);
    save_recent_roots(store, &roots);
}

/// Loadable projects from the default recent file.
pub fn recent_projects() -> Vec<ProjectInfo> {
    recent_projects_at(&recent_projects_file())
}

pub fn recent_projects_at(store: &Path) -> Vec<ProjectInfo> {
    let mut seen = std::collections::HashSet::new();
    let mut projects = Vec::new();
    for root in load_recent_roots(store) {
        if !seen.insert(root.clone()) {
            continue;
        }
        if let Ok(project) = load_project(&root) {
            projects.push(project);
        }
    }
    projects
}

fn save_recent_roots(store: &Path, roots: &[PathBuf]) {
    if let Some(parent) = store.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    let text = roots
        .iter()
        .map(|root| root.display().to_string())
        .collect::<Vec<_>>()
        .join("\n");
    let _ = std::fs::write(store, text + "\n");
}

fn load_recent_roots(store: &Path) -> Vec<PathBuf> {
    std::fs::read_to_string(store)
        .map(|text| {
            text.lines()
                .map(str::trim)
                .filter(|line| !line.is_empty())
                .map(PathBuf::from)
                .collect()
        })
        .unwrap_or_default()
}

fn canonical_root(root: &Path) -> PathBuf {
    root.canonicalize().unwrap_or_else(|_| root.to_path_buf())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp_dir(label: &str) -> PathBuf {
        std::env::temp_dir().join(format!(
            "doppio-project-{label}-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|duration| duration.as_nanos())
                .unwrap_or_default()
        ))
    }

    #[test]
    fn project_manifest_create_load_scan_and_recent() {
        let root = temp_dir("manifest");
        let recent = root.join("recent.txt");
        std::fs::create_dir_all(&root).expect("temp dir");
        let project = create_project(&root, "Demo", "3D").expect("create");
        assert!(project.root.join(PROJECT_FILE).exists());
        assert!(project.root.join("assets/raw/Textures").is_dir());
        assert_eq!(project.asset_root(), project.root.join("assets/raw"));

        let loaded = load_project(&project.root).expect("load");
        assert_eq!(loaded.name, "Demo");
        assert_eq!(loaded.template, "3D");
        assert_eq!(loaded.paths.assets_raw, "assets/raw");

        update_last_scene(&project.root, "scenes/main.caf").expect("scene");
        let updated = load_project(&project.root).expect("reload");
        assert_eq!(updated.last_scene, "scenes/main.caf");
        assert_eq!(updated.paths.assets_raw, "assets/raw");

        let scanned = scan_projects(&root);
        assert!(scanned.iter().any(|entry| entry.name == "Demo"));

        record_recent_at(&recent, &project.root);
        record_recent_at(&recent, &project.root);
        let recents = recent_projects_at(&recent);
        assert_eq!(recents.len(), 1);
        assert_eq!(recents[0].name, "Demo");

        forget_recent_at(&recent, &project.root);
        assert!(recent_projects_at(&recent).is_empty());
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn recursive_scan_finds_nested_projects_and_skips_hidden() {
        let root = temp_dir("recursive");
        let nested = root.join("work").join("nested");
        std::fs::create_dir_all(&nested).expect("nested dir");
        create_project(&nested, "Inner", "2D").expect("create");
        let hidden = root.join(".hidden");
        std::fs::create_dir_all(&hidden).expect("hidden dir");
        create_project(&hidden, "Hidden", "3D").expect("create");
        let found = scan_projects_recursive(&root);
        assert!(found.iter().any(|entry| entry.name == "Inner"));
        assert!(!found.iter().any(|entry| entry.name == "Hidden"));
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn loads_the_cpp_project_manifest_shape_and_file_recents() {
        let root = temp_dir("legacy");
        let legacy = root.join("Legacy");
        std::fs::create_dir_all(legacy.join("scenes")).expect("legacy dir");
        let manifest = r#"{
  "project_name": "Legacy",
  "engine_version": "0.1.0",
  "paths": {
    "assets_raw": "content/raw",
    "assets_processed": "content/processed",
    "scripts": "code"
  },
  "template_type": "2D",
  "last_scene": "scenes/start.caf"
}
"#;
        std::fs::write(legacy.join(PROJECT_FILE), manifest).expect("manifest");
        std::fs::write(legacy.join("scenes/start.caf"), b"scene").expect("scene");
        let loaded = load_project(&legacy).expect("load legacy");
        assert_eq!(loaded.name, "Legacy");
        assert_eq!(loaded.template, "2D");
        assert_eq!(loaded.paths.assets_raw, "content/raw");
        assert_eq!(loaded.paths.assets_processed, "content/processed");
        assert_eq!(loaded.paths.scripts, "code");
        assert_eq!(
            loaded.scene_path().expect("scene").file_name().unwrap(),
            "start.caf"
        );

        // The C++ recent file stores project.caffeine paths, not roots.
        let recent = root.join("recent.txt");
        std::fs::write(
            &recent,
            format!("{}\n", legacy.join(PROJECT_FILE).display()),
        )
        .expect("recent");
        let recents = recent_projects_at(&recent);
        assert_eq!(recents.len(), 1);
        assert_eq!(recents[0].name, "Legacy");
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn project_names_cannot_escape_the_location() {
        assert!(validate_project_name("").is_err());
        assert!(validate_project_name("..").is_err());
        assert!(validate_project_name("a/b").is_err());
        assert_eq!(validate_project_name("  Game  ").expect("name"), "Game");
    }
}

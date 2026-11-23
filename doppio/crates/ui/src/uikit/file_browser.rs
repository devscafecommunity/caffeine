//! Doppio's own file browser: the logic behind the picker that chooses files and folders on this
//! computer, the same on Linux, macOS and Windows.
//!
//! The operating system's file dialogs differ per platform, need a toolkit or a portal, and cannot be
//! driven by tests or agents. This browser is plain `std::fs` plus a state struct, so the Import
//! assistant, the headless driver and the agent all use the one thing. It lists a folder (folders
//! first), can hide dotfiles and show only the file types the project accepts, selects several entries
//! and offers the usual places (home, Desktop, Documents, Downloads, every drive on Windows, `/` and the
//! mount points on Unix).

use crate::uikit::asset_kind::{self, AssetKind};
use std::path::{Path, PathBuf};

/// One row of the listing.
#[derive(Debug, Clone, PartialEq)]
pub struct Entry {
    pub name: String,
    pub path: PathBuf,
    pub is_dir: bool,
    pub size: u64,
    /// What the project would make of the file; `None` for folders and unsupported files.
    pub kind: Option<AssetKind>,
}

/// A shortcut in the sidebar.
#[derive(Debug, Clone, PartialEq)]
pub struct Place {
    pub label: String,
    pub path: PathBuf,
}

/// At most this many rows are listed: a folder of a hundred thousand files must not freeze the editor.
pub const MAX_ENTRIES: usize = 5000;

/// The user's home folder (`$HOME`, or `%USERPROFILE%` on Windows).
pub fn home() -> PathBuf {
    std::env::var_os("HOME")
        .or_else(|| std::env::var_os("USERPROFILE"))
        .map(PathBuf::from)
        .filter(|path| path.is_dir())
        .unwrap_or_else(|| PathBuf::from(std::path::MAIN_SEPARATOR_STR))
}

/// Home, its usual folders and every root the machine has.
pub fn places() -> Vec<Place> {
    let home = home();
    let mut places = vec![Place {
        label: "Home".to_owned(),
        path: home.clone(),
    }];
    for name in [
        "Desktop",
        "Documents",
        "Downloads",
        "Pictures",
        "Music",
        "Videos",
    ] {
        let path = home.join(name);
        if path.is_dir() {
            places.push(Place {
                label: name.to_owned(),
                path,
            });
        }
    }
    places.extend(roots());
    places
}

/// Where the file system starts: drives on Windows, `/` and the usual mount folders elsewhere.
pub fn roots() -> Vec<Place> {
    let mut roots = Vec::new();
    if cfg!(windows) {
        for letter in b'A'..=b'Z' {
            let path = PathBuf::from(format!("{}:\\", letter as char));
            if path.exists() {
                roots.push(Place {
                    label: format!("{}:", letter as char),
                    path,
                });
            }
        }
    } else {
        roots.push(Place {
            label: "/".to_owned(),
            path: PathBuf::from("/"),
        });
        for (label, path) in [
            ("Volumes", "/Volumes"),
            ("Media", "/media"),
            ("Mounts", "/mnt"),
        ] {
            let path = PathBuf::from(path);
            if path.is_dir() {
                roots.push(Place {
                    label: label.to_owned(),
                    path,
                });
            }
        }
    }
    roots
}

/// The folder above `dir`, if any.
pub fn parent(dir: &Path) -> Option<PathBuf> {
    dir.parent()
        .map(Path::to_path_buf)
        .filter(|parent| !parent.as_os_str().is_empty())
}

/// `/home/me/Downloads` as `[("/", /), ("home", /home), ("me", /home/me), ("Downloads", …)]`.
pub fn crumbs(dir: &Path) -> Vec<(String, PathBuf)> {
    let mut chain: Vec<PathBuf> = dir.ancestors().map(Path::to_path_buf).collect();
    chain.reverse();
    chain
        .into_iter()
        .filter(|path| !path.as_os_str().is_empty())
        .map(|path| {
            let label = path
                .file_name()
                .map(|name| name.to_string_lossy().into_owned())
                .unwrap_or_else(|| path.display().to_string());
            (label, path)
        })
        .collect()
}

/// What was typed in the path box, as a folder or file: `~` is home, a relative path starts from `current`,
/// `file://` links are accepted, and surrounding quotes (a path pasted from a shell) are dropped.
pub fn resolve_typed(input: &str, current: &Path) -> Option<PathBuf> {
    let trimmed = input.trim().trim_matches(|c| c == '"' || c == '\'').trim();
    let trimmed = trimmed.strip_prefix("file://").unwrap_or(trimmed);
    if trimmed.is_empty() {
        return None;
    }
    let path = if trimmed == "~" {
        home()
    } else if let Some(rest) = trimmed
        .strip_prefix("~/")
        .or_else(|| trimmed.strip_prefix("~\\"))
    {
        home().join(rest)
    } else {
        let path = PathBuf::from(trimmed);
        if path.is_absolute() {
            path
        } else {
            current.join(path)
        }
    };
    path.exists().then_some(path)
}

#[cfg(windows)]
fn is_hidden_file(path: &Path, name: &str) -> bool {
    use std::os::windows::fs::MetadataExt;
    name.starts_with('.')
        || std::fs::metadata(path)
            .map(|metadata| metadata.file_attributes() & 0x2 != 0)
            .unwrap_or(false)
}

#[cfg(not(windows))]
fn is_hidden_file(_path: &Path, name: &str) -> bool {
    name.starts_with('.')
}

/// Lists `dir`: folders first, then files, each sorted by name without regard to case.
pub fn list(
    dir: &Path,
    show_hidden: bool,
    only_supported: bool,
    search: &str,
) -> Result<Vec<Entry>, String> {
    let needle = search.trim().to_lowercase();
    let read = std::fs::read_dir(dir).map_err(|error| format!("{}: {error}", dir.display()))?;
    let mut entries = Vec::new();
    for item in read.flatten() {
        let name = item.file_name().to_string_lossy().into_owned();
        let path = item.path();
        if !show_hidden && is_hidden_file(&path, &name) {
            continue;
        }
        // Follows symlinks; a broken link has no metadata and is skipped.
        let Ok(metadata) = std::fs::metadata(&path) else {
            continue;
        };
        let is_dir = metadata.is_dir();
        let kind = if is_dir {
            None
        } else {
            asset_kind::classify(&path)
        };
        if !is_dir && only_supported && kind.is_none() {
            continue;
        }
        if !needle.is_empty() && !name.to_lowercase().contains(&needle) {
            continue;
        }
        entries.push(Entry {
            name,
            path,
            is_dir,
            size: if is_dir { 0 } else { metadata.len() },
            kind,
        });
        if entries.len() >= MAX_ENTRIES {
            break;
        }
    }
    entries.sort_by(|a, b| {
        b.is_dir
            .cmp(&a.is_dir)
            .then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase()))
    });
    Ok(entries)
}

/// The browser's state: where it is, what it lists, what is selected and how it filters.
#[derive(Debug, Clone)]
pub struct Browser {
    pub dir: PathBuf,
    pub entries: Vec<Entry>,
    pub error: Option<String>,
    pub selected: Vec<PathBuf>,
    pub search: String,
    pub path_input: String,
    pub show_hidden: bool,
    pub only_supported: bool,
    pub places: Vec<Place>,
}

impl Browser {
    /// Opens at `dir`, or at home when it is not a folder.
    pub fn open(dir: &Path) -> Self {
        let dir = if dir.is_dir() {
            dir.to_path_buf()
        } else {
            home()
        };
        let mut browser = Self {
            path_input: dir.display().to_string(),
            dir,
            entries: Vec::new(),
            error: None,
            selected: Vec::new(),
            search: String::new(),
            show_hidden: false,
            only_supported: true,
            places: places(),
        };
        browser.reload();
        browser
    }

    /// Lists the folder again with the current filters.
    pub fn reload(&mut self) {
        match list(
            &self.dir,
            self.show_hidden,
            self.only_supported,
            &self.search,
        ) {
            Ok(entries) => {
                self.entries = entries;
                self.error = None;
            }
            Err(error) => {
                self.entries.clear();
                self.error = Some(error);
            }
        }
    }

    /// Goes into `dir`; the selection is kept, so files from several folders can be gathered.
    pub fn go(&mut self, dir: &Path) {
        if !dir.is_dir() {
            self.error = Some(format!("{} is not a folder", dir.display()));
            return;
        }
        self.dir = dir.to_path_buf();
        self.path_input = self.dir.display().to_string();
        self.search.clear();
        self.reload();
    }

    pub fn up(&mut self) {
        if let Some(parent) = parent(&self.dir) {
            self.go(&parent);
        }
    }

    /// Takes what was typed in the path box: a folder is entered, a file is selected in its folder.
    pub fn go_typed(&mut self) {
        let Some(path) = resolve_typed(&self.path_input, &self.dir) else {
            self.error = Some(format!("{} does not exist", self.path_input.trim()));
            return;
        };
        if path.is_dir() {
            self.go(&path);
        } else {
            if let Some(parent) = path.parent() {
                self.go(parent);
            }
            self.select(&path);
        }
    }

    pub fn is_selected(&self, path: &Path) -> bool {
        self.selected.iter().any(|selected| selected == path)
    }

    pub fn select(&mut self, path: &Path) {
        if !self.is_selected(path) {
            self.selected.push(path.to_path_buf());
        }
    }

    pub fn toggle(&mut self, path: &Path) {
        if self.is_selected(path) {
            self.selected.retain(|selected| selected != path);
        } else {
            self.selected.push(path.to_path_buf());
        }
    }

    /// Selects every supported file listed here.
    pub fn select_supported(&mut self) {
        let supported: Vec<PathBuf> = self
            .entries
            .iter()
            .filter(|entry| entry.kind.is_some())
            .map(|entry| entry.path.clone())
            .collect();
        for path in supported {
            self.select(&path);
        }
    }

    /// How the selection reads: `3 files · 1 folder`.
    pub fn summary(&self) -> String {
        let folders = self.selected.iter().filter(|path| path.is_dir()).count();
        let files = self.selected.len() - folders;
        match (files, folders) {
            (0, 0) => "nothing selected".to_owned(),
            (files, 0) => format!("{files} file{}", if files == 1 { "" } else { "s" }),
            (0, folders) => format!("{folders} folder{}", if folders == 1 { "" } else { "s" }),
            (files, folders) => format!(
                "{files} file{} · {folders} folder{}",
                if files == 1 { "" } else { "s" },
                if folders == 1 { "" } else { "s" }
            ),
        }
    }
}

/// `1.4 MB`, `820 B`.
pub fn format_size(bytes: u64) -> String {
    if bytes >= 1024 * 1024 {
        format!("{:.1} MB", bytes as f64 / (1024.0 * 1024.0))
    } else if bytes >= 1024 {
        format!("{:.1} KB", bytes as f64 / 1024.0)
    } else {
        format!("{bytes} B")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(label: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!(
            "doppio-fb-{label}-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&dir).expect("temp dir");
        dir
    }

    fn fixture() -> PathBuf {
        let root = temp("list");
        std::fs::create_dir_all(root.join("Zebra")).unwrap();
        std::fs::create_dir_all(root.join("alpha")).unwrap();
        std::fs::create_dir_all(root.join(".hidden_dir")).unwrap();
        for name in [
            "rock.png",
            "Hero.glb",
            "notes.zip",
            ".secret.png",
            "boom.wav",
        ] {
            std::fs::write(root.join(name), b"12345").unwrap();
        }
        root
    }

    #[test]
    fn folders_come_first_and_names_sort_without_case() {
        let root = fixture();
        let entries = list(&root, false, false, "").unwrap();
        let names: Vec<&str> = entries.iter().map(|e| e.name.as_str()).collect();
        assert_eq!(
            names,
            [
                "alpha",
                "Zebra",
                "boom.wav",
                "Hero.glb",
                "notes.zip",
                "rock.png"
            ]
        );
        assert!(entries[0].is_dir && entries[1].is_dir && !entries[2].is_dir);
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn hidden_entries_appear_only_when_asked() {
        let root = fixture();
        let shown = list(&root, true, false, "").unwrap();
        assert!(shown.iter().any(|e| e.name == ".secret.png"));
        assert!(shown.iter().any(|e| e.name == ".hidden_dir"));
        let hidden = list(&root, false, false, "").unwrap();
        assert!(!hidden.iter().any(|e| e.name.starts_with('.')));
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn the_supported_filter_keeps_folders_and_drops_unknown_files() {
        let root = fixture();
        let entries = list(&root, false, true, "").unwrap();
        assert!(entries.iter().any(|e| e.name == "alpha"));
        assert!(!entries.iter().any(|e| e.name == "notes.zip"));
        let png = entries.iter().find(|e| e.name == "rock.png").unwrap();
        assert_eq!(png.kind, Some(AssetKind::Image));
        assert_eq!(png.size, 5);
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn search_matches_names_case_insensitively() {
        let root = fixture();
        let entries = list(&root, false, false, "HERO").unwrap();
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].name, "Hero.glb");
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn a_missing_folder_is_an_error_not_a_panic() {
        let missing = std::env::temp_dir().join("doppio-fb-does-not-exist-xyz");
        assert!(list(&missing, false, false, "").is_err());
        let mut browser = Browser::open(&temp("err"));
        browser.go(&missing);
        assert!(browser.error.is_some());
        assert_ne!(browser.dir, missing, "it stays where it was");
    }

    #[test]
    fn crumbs_walk_from_the_root_to_the_folder() {
        let trail = crumbs(Path::new("/home/me/Downloads"));
        let labels: Vec<&str> = trail.iter().map(|(label, _)| label.as_str()).collect();
        assert_eq!(labels, ["/", "home", "me", "Downloads"]);
        assert_eq!(trail[3].1, PathBuf::from("/home/me/Downloads"));
        assert_eq!(parent(Path::new("/home/me")), Some(PathBuf::from("/home")));
        assert_eq!(parent(Path::new("/")), None);
    }

    #[test]
    fn typed_paths_accept_quotes_tilde_relative_and_file_links() {
        let root = fixture();
        assert_eq!(
            resolve_typed(&format!("\"{}\"", root.display()), Path::new("/")),
            Some(root.clone())
        );
        assert_eq!(
            resolve_typed(&format!("file://{}", root.display()), Path::new("/")),
            Some(root.clone())
        );
        assert_eq!(resolve_typed("alpha", &root), Some(root.join("alpha")));
        assert_eq!(resolve_typed("~", Path::new("/")), Some(home()));
        assert_eq!(resolve_typed("nope-nope", &root), None);
        assert_eq!(resolve_typed("   ", &root), None);
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn the_selection_survives_walking_between_folders() {
        let root = fixture();
        let mut browser = Browser::open(&root);
        browser.toggle(&root.join("rock.png"));
        browser.go(&root.join("alpha"));
        std::fs::write(root.join("alpha/inner.obj"), b"o").unwrap();
        browser.reload();
        browser.toggle(&root.join("alpha/inner.obj"));
        assert_eq!(browser.summary(), "2 files");
        browser.up();
        assert_eq!(browser.dir, root);
        browser.toggle(&root.join("rock.png"));
        assert_eq!(browser.summary(), "1 file");
        browser.toggle(&root.join("Zebra"));
        assert_eq!(browser.summary(), "1 file · 1 folder");
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn select_supported_takes_every_listed_file_the_project_accepts() {
        let root = fixture();
        let mut browser = Browser::open(&root);
        browser.select_supported();
        let names: Vec<String> = browser
            .selected
            .iter()
            .filter_map(|path| path.file_name())
            .map(|name| name.to_string_lossy().into_owned())
            .collect();
        assert_eq!(names.len(), 3, "{names:?}");
        assert!(names.contains(&"rock.png".to_owned()));
        assert!(!names.contains(&"notes.zip".to_owned()));
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn typing_a_file_path_opens_its_folder_with_the_file_selected() {
        let root = fixture();
        let mut browser = Browser::open(&root.join("alpha"));
        browser.path_input = root.join("rock.png").display().to_string();
        browser.go_typed();
        assert_eq!(browser.dir, root);
        assert!(browser.is_selected(&root.join("rock.png")));
        browser.path_input = "does/not/exist".to_owned();
        browser.go_typed();
        assert!(browser.error.is_some());
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn places_start_with_home_and_include_a_root() {
        let found = places();
        assert_eq!(found[0].label, "Home");
        assert!(found.iter().any(|place| place.path.parent().is_none()));
        assert_eq!(format_size(820), "820 B");
        assert_eq!(format_size(1536), "1.5 KB");
    }
}

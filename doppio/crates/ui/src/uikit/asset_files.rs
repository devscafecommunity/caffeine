//! File and folder operations of the Assets pane: new folder, rename, move and delete.
//!
//! Paths are always **relative to the assets root** and `/`-separated. Nothing can leave the root
//! (`..`, absolute paths and separators in names are refused), nothing is overwritten, and delete
//! moves into `<root>/.trash` instead of erasing, so a mistake can be undone by hand.
//!
//! Moving or renaming a file does not rewrite the materials and scenes that mention it: the result
//! message says so, and the Assets pane keeps the trash folder hidden.

use std::path::{Component, Path, PathBuf};

/// The hidden folder deleted assets are moved into.
pub const TRASH: &str = ".trash";

/// A name for one file or folder: not empty, no separators, not `.` or `..`.
pub fn validate_name(name: &str) -> Result<&str, String> {
    let name = name.trim();
    if name.is_empty() {
        return Err("The name is empty".to_owned());
    }
    if name == "."
        || name == ".."
        || name.contains('/')
        || name.contains('\\')
        || name.contains('\0')
    {
        return Err(format!("`{name}` is not a valid name"));
    }
    Ok(name)
}

/// `relative` joined onto `root`, refusing anything that would leave it.
fn inside(root: &Path, relative: &str) -> Result<PathBuf, String> {
    let path = Path::new(relative);
    if path.is_absolute()
        || path.components().any(|c| {
            matches!(
                c,
                Component::ParentDir | Component::Prefix(_) | Component::RootDir
            )
        })
    {
        return Err(format!("`{relative}` is outside the project assets"));
    }
    Ok(root.join(path))
}

fn join_relative(directory: &str, name: &str) -> String {
    if directory.is_empty() {
        name.to_owned()
    } else {
        format!("{directory}/{name}")
    }
}

/// Creates `<directory>/<name>` and returns its relative path.
pub fn create_folder(root: &Path, directory: &str, name: &str) -> Result<String, String> {
    let name = validate_name(name)?;
    let parent = inside(root, directory)?;
    let target = parent.join(name);
    if target.exists() {
        return Err(format!("`{name}` already exists"));
    }
    std::fs::create_dir_all(&target).map_err(|e| e.to_string())?;
    Ok(join_relative(directory, name))
}

/// Renames the file or folder at `relative`; returns the new relative path.
pub fn rename(root: &Path, relative: &str, new_name: &str) -> Result<String, String> {
    let new_name = validate_name(new_name)?;
    let from = inside(root, relative)?;
    if !from.exists() {
        return Err(format!("`{relative}` does not exist"));
    }
    let parent = relative.rsplit_once('/').map(|(p, _)| p).unwrap_or("");
    let to = inside(root, &join_relative(parent, new_name))?;
    if to.exists() {
        return Err(format!("`{new_name}` already exists"));
    }
    std::fs::rename(&from, &to).map_err(|e| e.to_string())?;
    Ok(join_relative(parent, new_name))
}

/// Moves `relative` into the folder `destination` (`""` = the root); returns the new path.
pub fn move_into(root: &Path, relative: &str, destination: &str) -> Result<String, String> {
    let from = inside(root, relative)?;
    let folder = inside(root, destination)?;
    if !from.exists() {
        return Err(format!("`{relative}` does not exist"));
    }
    if !folder.is_dir() {
        return Err(format!("`{destination}` is not a folder"));
    }
    let name = from
        .file_name()
        .and_then(|n| n.to_str())
        .ok_or("The file has no name")?
        .to_owned();
    // A folder cannot go into itself or into one of its own subfolders.
    if from.is_dir() && (folder == from || folder.starts_with(&from)) {
        return Err("A folder cannot be moved into itself".to_owned());
    }
    let to = folder.join(&name);
    if to == from {
        return Ok(relative.to_owned());
    }
    if to.exists() {
        return Err(format!("`{name}` already exists in `{destination}`"));
    }
    std::fs::rename(&from, &to).map_err(|e| e.to_string())?;
    Ok(join_relative(destination, &name))
}

/// Moves `relative` into `<root>/.trash` under a unique name; returns where it went.
pub fn trash(root: &Path, relative: &str) -> Result<String, String> {
    let from = inside(root, relative)?;
    if !from.exists() {
        return Err(format!("`{relative}` does not exist"));
    }
    if relative == TRASH || relative.starts_with(&format!("{TRASH}/")) {
        return Err("That is already in the trash".to_owned());
    }
    let name = from
        .file_name()
        .and_then(|n| n.to_str())
        .ok_or("The file has no name")?
        .to_owned();
    let bin = root.join(TRASH);
    std::fs::create_dir_all(&bin).map_err(|e| e.to_string())?;
    let mut candidate = bin.join(&name);
    let mut n = 1;
    while candidate.exists() {
        candidate = bin.join(format!("{n}_{name}"));
        n += 1;
    }
    std::fs::rename(&from, &candidate).map_err(|e| e.to_string())?;
    Ok(format!(
        "{TRASH}/{}",
        candidate
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or(&name)
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(label: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!(
            "doppio-files-{label}-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn folders_are_created_renamed_and_never_overwritten() {
        let root = temp("folders");
        assert_eq!(create_folder(&root, "", "Textures").unwrap(), "Textures");
        assert_eq!(
            create_folder(&root, "Textures", "Bark").unwrap(),
            "Textures/Bark"
        );
        assert!(create_folder(&root, "", "Textures")
            .unwrap_err()
            .contains("already exists"));
        assert_eq!(
            rename(&root, "Textures/Bark", "Moss").unwrap(),
            "Textures/Moss"
        );
        assert!(root.join("Textures/Moss").is_dir() && !root.join("Textures/Bark").exists());
        create_folder(&root, "Textures", "Rock").unwrap();
        assert!(rename(&root, "Textures/Moss", "Rock")
            .unwrap_err()
            .contains("already exists"));
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn names_and_paths_cannot_escape_the_assets_root() {
        let root = temp("escape");
        for bad in ["", "  ", ".", "..", "a/b", "a\\b"] {
            assert!(validate_name(bad).is_err(), "{bad:?}");
        }
        assert!(create_folder(&root, "../outside", "x").is_err());
        assert!(rename(&root, "../etc", "x").is_err());
        assert!(move_into(&root, "/etc/passwd", "").is_err());
        assert!(trash(&root, "a/../../b").is_err());
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn files_move_between_folders_without_overwriting_or_nesting_a_folder_in_itself() {
        let root = temp("move");
        std::fs::create_dir_all(root.join("A/Inner")).unwrap();
        std::fs::create_dir_all(root.join("B")).unwrap();
        std::fs::write(root.join("A/leaf.png"), b"1").unwrap();
        assert_eq!(move_into(&root, "A/leaf.png", "B").unwrap(), "B/leaf.png");
        assert!(root.join("B/leaf.png").is_file() && !root.join("A/leaf.png").exists());
        // Back to the root, and a name clash is refused.
        std::fs::write(root.join("leaf.png"), b"2").unwrap();
        assert!(move_into(&root, "B/leaf.png", "")
            .unwrap_err()
            .contains("already exists"));
        assert_eq!(std::fs::read(root.join("leaf.png")).unwrap(), b"2");
        // A folder into itself or a descendant, and a file as destination, are refused.
        assert!(move_into(&root, "A", "A").is_err());
        assert!(move_into(&root, "A", "A/Inner").is_err());
        assert!(move_into(&root, "B", "leaf.png")
            .unwrap_err()
            .contains("not a folder"));
        assert_eq!(move_into(&root, "A/Inner", "B").unwrap(), "B/Inner");
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn delete_goes_to_the_trash_with_a_unique_name() {
        let root = temp("trash");
        std::fs::write(root.join("a.png"), b"1").unwrap();
        assert_eq!(trash(&root, "a.png").unwrap(), ".trash/a.png");
        assert!(!root.join("a.png").exists() && root.join(".trash/a.png").is_file());
        std::fs::write(root.join("a.png"), b"2").unwrap();
        assert_eq!(trash(&root, "a.png").unwrap(), ".trash/1_a.png");
        assert_eq!(
            std::fs::read(root.join(".trash/a.png")).unwrap(),
            b"1",
            "older copy kept"
        );
        assert!(trash(&root, ".trash/a.png").is_err());
        assert!(trash(&root, "missing.png").is_err());
        std::fs::remove_dir_all(root).ok();
    }
}

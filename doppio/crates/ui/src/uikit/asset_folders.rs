//! The project's asset folder convention, in one place.
//!
//! Projects are created with capitalised folders (`Models`, `Materials`, `Textures`...). Generators
//! used to write their own lowercase twins (`textures`, `materials`, `nature`, `texture_sets`);
//! everything now asks this module, which **reuses an existing folder whatever its case** and only
//! creates the canonical name when none exists.
//!
//! ```text
//! Models/Nature/<model>.obj          generated meshes and the .nature recipes
//! Materials/Nature/<name>.mat        materials written by the Nature tool
//! Textures/Nature/<name>_*.png       baked maps of those materials
//! Textures/<name>.png + .texgraph    Texture Generator exports
//! Textures/Sets/<name>.texset        Texture Set projects
//! Materials/<name>.mat + .mgraph     Material Generator output
//! ```

use std::path::{Path, PathBuf};

/// What a generator writes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AssetFolder {
    Textures,
    TextureSets,
    Materials,
    Models,
    NatureModels,
    NatureMaterials,
    NatureTextures,
}

impl AssetFolder {
    /// The canonical path below the assets root, `/`-separated.
    pub fn canonical(self) -> &'static str {
        match self {
            AssetFolder::Textures => "Textures",
            AssetFolder::TextureSets => "Textures/Sets",
            AssetFolder::Materials => "Materials",
            AssetFolder::Models => "Models",
            AssetFolder::NatureModels => "Models/Nature",
            AssetFolder::NatureMaterials => "Materials/Nature",
            AssetFolder::NatureTextures => "Textures/Nature",
        }
    }
}

/// The folder for `kind` below `root`: each path component reuses an existing directory that matches
/// it ignoring case (the exact canonical spelling wins when several exist), else the canonical name.
/// Nothing is created.
pub fn resolve(root: &Path, kind: AssetFolder) -> PathBuf {
    let mut path = root.to_path_buf();
    for component in kind.canonical().split('/') {
        path.push(existing_or(&path, component));
    }
    path
}

/// [`resolve`], creating the folders.
pub fn ensure(root: &Path, kind: AssetFolder) -> std::io::Result<PathBuf> {
    let path = resolve(root, kind);
    std::fs::create_dir_all(&path)?;
    Ok(path)
}

fn existing_or(parent: &Path, name: &str) -> String {
    let Ok(entries) = std::fs::read_dir(parent) else {
        return name.to_owned();
    };
    let mut found: Option<String> = None;
    for entry in entries.flatten() {
        if !entry.file_type().is_ok_and(|t| t.is_dir()) {
            continue;
        }
        let candidate = entry.file_name().to_string_lossy().into_owned();
        if candidate == name {
            return candidate;
        }
        if candidate.eq_ignore_ascii_case(name)
            && found.as_deref().is_none_or(|f| candidate.as_str() < f)
        {
            found = Some(candidate);
        }
    }
    found.unwrap_or_else(|| name.to_owned())
}

/// `path` relative to `root`, `/`-separated (what `.mat` files and components store).
pub fn relative(root: &Path, path: &Path) -> String {
    path.strip_prefix(root)
        .unwrap_or(path)
        .components()
        .map(|c| c.as_os_str().to_string_lossy().into_owned())
        .collect::<Vec<_>>()
        .join("/")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(label: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!(
            "doppio-folders-{label}-{}-{}",
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
    fn missing_folders_use_the_capitalised_names_and_are_created_on_ensure() {
        let root = temp("new");
        assert_eq!(
            resolve(&root, AssetFolder::NatureMaterials),
            root.join("Materials/Nature")
        );
        assert!(!root.join("Materials").exists(), "resolve creates nothing");
        let made = ensure(&root, AssetFolder::NatureTextures).unwrap();
        assert!(made.is_dir() && made.ends_with("Textures/Nature"));
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn existing_folders_are_reused_whatever_their_case() {
        let root = temp("case");
        std::fs::create_dir_all(root.join("textures/nature")).unwrap();
        std::fs::create_dir_all(root.join("MODELS")).unwrap();
        assert_eq!(
            resolve(&root, AssetFolder::NatureTextures),
            root.join("textures/nature")
        );
        assert_eq!(resolve(&root, AssetFolder::Textures), root.join("textures"));
        assert_eq!(
            resolve(&root, AssetFolder::NatureModels),
            root.join("MODELS/Nature")
        );
        // The exact canonical spelling wins when both exist.
        std::fs::create_dir_all(root.join("Textures")).unwrap();
        assert_eq!(resolve(&root, AssetFolder::Textures), root.join("Textures"));
        assert_eq!(
            ensure(&root, AssetFolder::Textures).unwrap(),
            root.join("Textures")
        );
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn relative_paths_use_forward_slashes() {
        let root = Path::new("/p/assets");
        assert_eq!(
            relative(root, &root.join("Materials").join("Nature").join("a.mat")),
            "Materials/Nature/a.mat"
        );
    }
}

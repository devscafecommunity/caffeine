//! What an asset is, judged by its file name.
//!
//! The engine labels a file with a coarse `kind` string (`image`, `mesh`, …) and calls everything it
//! does not load `file`. The editor needs more: prefabs, terrains, water and nature graphs, texture
//! sets, fonts and plain data deserve their own icon, colour and filter. This table is the one place
//! that knows the extensions; the asset browser, the picker, the importer and the inspector all ask
//! it, so they cannot disagree.
//!
//! The `key()` of the kinds the engine already reports is the engine's own string, so code that
//! compares `asset.kind == "image"` keeps working once the list is refined with [`refine`].

use crate::uikit::icons::Icon;
use std::path::Path;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AssetKind {
    Folder,
    Image,
    Mesh,
    Material,
    Audio,
    Script,
    Prefab,
    Scene,
    Terrain,
    Water,
    Nature,
    TextureSet,
    TextureGraph,
    Animation,
    Font,
    Packed,
    Localization,
    Generator,
    Data,
    Other,
}

impl AssetKind {
    /// Every kind the importer can bring in (everything except folders and unknown files).
    pub const IMPORTABLE: [AssetKind; 18] = [
        AssetKind::Image,
        AssetKind::Mesh,
        AssetKind::Material,
        AssetKind::Audio,
        AssetKind::Script,
        AssetKind::Prefab,
        AssetKind::Scene,
        AssetKind::Terrain,
        AssetKind::Water,
        AssetKind::Nature,
        AssetKind::TextureSet,
        AssetKind::TextureGraph,
        AssetKind::Animation,
        AssetKind::Font,
        AssetKind::Packed,
        AssetKind::Localization,
        AssetKind::Generator,
        AssetKind::Data,
    ];

    /// The string the engine (and every `asset.kind == "…"` test) uses.
    pub fn key(self) -> &'static str {
        match self {
            AssetKind::Folder => "folder",
            AssetKind::Image => "image",
            AssetKind::Mesh => "mesh",
            AssetKind::Material => "material",
            AssetKind::Audio => "audio",
            AssetKind::Script => "script",
            AssetKind::Prefab => "prefab",
            AssetKind::Scene => "scene",
            AssetKind::Terrain => "terrain",
            AssetKind::Water => "water",
            AssetKind::Nature => "nature",
            AssetKind::TextureSet => "texset",
            AssetKind::TextureGraph => "texgraph",
            AssetKind::Animation => "animation",
            AssetKind::Font => "font",
            AssetKind::Packed => "asset",
            AssetKind::Localization => "localization",
            AssetKind::Generator => "generator",
            AssetKind::Data => "data",
            AssetKind::Other => "file",
        }
    }

    pub fn from_key(key: &str) -> AssetKind {
        match key {
            "folder" => AssetKind::Folder,
            "image" => AssetKind::Image,
            "mesh" => AssetKind::Mesh,
            "material" => AssetKind::Material,
            "audio" => AssetKind::Audio,
            "script" => AssetKind::Script,
            "prefab" => AssetKind::Prefab,
            "scene" => AssetKind::Scene,
            "terrain" => AssetKind::Terrain,
            "water" => AssetKind::Water,
            "nature" => AssetKind::Nature,
            "texset" => AssetKind::TextureSet,
            "texgraph" => AssetKind::TextureGraph,
            "animation" => AssetKind::Animation,
            "font" => AssetKind::Font,
            "asset" => AssetKind::Packed,
            "localization" => AssetKind::Localization,
            "generator" => AssetKind::Generator,
            "data" => AssetKind::Data,
            _ => AssetKind::Other,
        }
    }

    /// Singular name for badges and the importer's report.
    pub fn label(self) -> &'static str {
        match self {
            AssetKind::Folder => "folder",
            AssetKind::Image => "texture",
            AssetKind::Mesh => "mesh",
            AssetKind::Material => "material",
            AssetKind::Audio => "audio",
            AssetKind::Script => "script",
            AssetKind::Prefab => "prefab",
            AssetKind::Scene => "scene",
            AssetKind::Terrain => "terrain",
            AssetKind::Water => "water",
            AssetKind::Nature => "nature graph",
            AssetKind::TextureSet => "texture set",
            AssetKind::TextureGraph => "texture graph",
            AssetKind::Animation => "animation",
            AssetKind::Font => "font",
            AssetKind::Packed => "packed asset",
            AssetKind::Localization => "localization",
            AssetKind::Generator => "generator recipe",
            AssetKind::Data => "data file",
            AssetKind::Other => "file",
        }
    }

    /// Short tag drawn where a thumbnail would be (`TEX`, `MESH`…).
    pub fn badge(self) -> &'static str {
        match self {
            AssetKind::Folder => "DIR",
            AssetKind::Image => "TEX",
            AssetKind::Mesh => "MESH",
            AssetKind::Material => "MAT",
            AssetKind::Audio => "SND",
            AssetKind::Script => "SCR",
            AssetKind::Prefab => "PFB",
            AssetKind::Scene => "SCN",
            AssetKind::Terrain => "TRN",
            AssetKind::Water => "WTR",
            AssetKind::Nature => "NAT",
            AssetKind::TextureSet => "SET",
            AssetKind::TextureGraph => "TGR",
            AssetKind::Animation => "ANM",
            AssetKind::Font => "FNT",
            AssetKind::Packed => "CAF",
            AssetKind::Localization => "LANG",
            AssetKind::Generator => "GEN",
            AssetKind::Data => "DATA",
            AssetKind::Other => "FILE",
        }
    }

    /// Placeholder of a picker's search box when only this kind is listed.
    pub fn search_hint(self) -> &'static str {
        match self {
            AssetKind::Image => "Search textures…",
            AssetKind::Mesh => "Search meshes…",
            AssetKind::Material => "Search materials…",
            AssetKind::Audio => "Search sounds…",
            AssetKind::Script => "Search scripts…",
            AssetKind::Prefab => "Search prefabs…",
            AssetKind::Scene => "Search scenes…",
            AssetKind::Terrain => "Search terrains…",
            AssetKind::Water => "Search water graphs…",
            AssetKind::Nature => "Search nature graphs…",
            AssetKind::Font => "Search fonts…",
            _ => "Search project assets…",
        }
    }

    /// What a picker says when the project has no asset of this kind.
    pub fn empty_hint(self) -> &'static str {
        match self {
            AssetKind::Image => "No textures in the project yet: import one from Assets → Import.",
            AssetKind::Mesh => {
                "No meshes in the project yet: import an .obj or .glb from Assets → Import."
            }
            AssetKind::Material => {
                "No materials in the project yet: create one from Assets → + New."
            }
            AssetKind::Audio => "No sounds in the project yet: import a .wav from Assets → Import.",
            AssetKind::Script => "No scripts in the project yet: create one from Assets → + New.",
            AssetKind::Prefab => "No prefabs in the project yet: save one from the Hierarchy.",
            _ => "No matching assets: import some from Assets → Import.",
        }
    }

    pub fn icon(self) -> Icon {
        match self {
            AssetKind::Folder => Icon::Folder,
            AssetKind::Image => Icon::Texture,
            AssetKind::Mesh => Icon::Mesh,
            AssetKind::Material => Icon::Material,
            AssetKind::Audio => Icon::Audio,
            AssetKind::Script => Icon::Script,
            AssetKind::Prefab => Icon::Prefab,
            AssetKind::Scene => Icon::Scene,
            AssetKind::Terrain => Icon::Terrain,
            AssetKind::Water => Icon::Water,
            AssetKind::Nature => Icon::Nature,
            AssetKind::TextureSet | AssetKind::TextureGraph => Icon::Texture,
            AssetKind::Animation => Icon::Player,
            AssetKind::Font => Icon::Font,
            AssetKind::Packed => Icon::Volume,
            AssetKind::Localization | AssetKind::Data => Icon::Data,
            AssetKind::Generator => Icon::Generic,
            AssetKind::Other => Icon::File,
        }
    }

    /// Lower-case file extensions (without the dot) the importer accepts for this kind.
    pub fn extensions(self) -> &'static [&'static str] {
        match self {
            // The engine decodes these with stb_image; `exr` is kept for the HDR bake path.
            AssetKind::Image => &[
                "png", "jpg", "jpeg", "tga", "gif", "hdr", "exr", "bmp", "psd", "pic", "ppm",
                "pgm", "pnm",
            ],
            AssetKind::Mesh => &["obj", "glb", "gltf", "fbx"],
            AssetKind::Material => &["mat", "material"],
            AssetKind::Audio => &["wav", "ogg", "mp3", "flac"],
            AssetKind::Script => &["lua", "dyd"],
            AssetKind::Prefab => &["prefab"],
            AssetKind::Scene => &["scene"],
            AssetKind::Terrain => &["terrain"],
            AssetKind::Water => &["water"],
            AssetKind::Nature => &["nature"],
            AssetKind::TextureSet => &["texset"],
            AssetKind::TextureGraph => &["texgraph"],
            AssetKind::Animation => &["anim"],
            AssetKind::Font => &["ttf", "otf"],
            AssetKind::Packed => &["caf", "cap"],
            AssetKind::Localization => &["lang"],
            // `Rock.generator.material`: decided by the stem, not the extension.
            AssetKind::Generator => &[],
            // `.mtl` and `.bin` are the companions an `.obj` / `.gltf` points at.
            AssetKind::Data => &[
                "json", "csv", "txt", "xml", "toml", "yaml", "yml", "ron", "mtl", "bin",
            ],
            AssetKind::Folder | AssetKind::Other => &[],
        }
    }

    /// The kinds a field of this kind can usually be filled with, e.g. a texture slot takes images.
    pub fn is_pickable(self) -> bool {
        !matches!(self, AssetKind::Folder | AssetKind::Other)
    }
}

/// True for `Rock.generator.material` and friends: the editable recipe of a generator.
fn is_generator_recipe(path: &Path) -> bool {
    path.file_stem()
        .map(|stem| {
            stem.to_string_lossy()
                .to_ascii_lowercase()
                .ends_with(".generator")
        })
        .unwrap_or(false)
}

/// Classifies a path by its name alone.
pub fn classify(path: &Path) -> Option<AssetKind> {
    if is_generator_recipe(path) {
        return Some(AssetKind::Generator);
    }
    let extension = path.extension()?.to_string_lossy().to_ascii_lowercase();
    AssetKind::IMPORTABLE
        .into_iter()
        .find(|kind| kind.extensions().contains(&extension.as_str()))
}

/// The kind of a listed asset: its name wins over the engine's coarse label.
pub fn of(relative_path: &str, engine_kind: &str, is_directory: bool) -> AssetKind {
    if is_directory {
        return AssetKind::Folder;
    }
    classify(Path::new(relative_path)).unwrap_or_else(|| AssetKind::from_key(engine_kind))
}

/// Rewrites the engine's kind labels so that prefabs, terrains, fonts and the other kinds the
/// engine reports as `file` carry their own. Folders and kinds the engine already knows stay.
pub fn refine(assets: &mut [doppio_engine::AssetInfo]) {
    for asset in assets {
        let kind = of(&asset.relative_path, &asset.kind, asset.is_directory);
        if kind != AssetKind::Other {
            asset.kind = kind.key().to_owned();
        }
    }
}

/// The extension shown on a tile (`GLB`, `PNG`): an at-a-glance hint beside the kind colour.
pub fn extension_tag(name: &str) -> String {
    Path::new(name)
        .extension()
        .map(|extension| extension.to_string_lossy().to_ascii_uppercase())
        .filter(|extension| extension.chars().count() <= 5)
        .unwrap_or_default()
}

/// Which kinds a component field or generator slot expects, judged by the field's name
/// (`Mesh Path`, `Clip Path`, `Normal map`…). `None` means it is not an asset reference.
pub fn for_field(name: &str) -> Option<Vec<AssetKind>> {
    let lower = name.to_ascii_lowercase();
    let has = |needle: &str| lower.contains(needle);
    // `Slot Materials` is a list of paths, not one asset: it stays a text field.
    if has("slot") {
        None
    } else if has("material") {
        Some(vec![AssetKind::Material])
    } else if has("mesh") || has("model") {
        Some(vec![AssetKind::Mesh])
    } else if has("texture")
        || has("normal map")
        || has("splat layer")
        || has("sprite")
        || has("skybox")
        || has("cubemap")
        || has("environment map")
        || has("heightmap")
        || has("height map")
        || (has("map") && has("path"))
    {
        Some(vec![AssetKind::Image])
    } else if has("clip") || has("sound") || has("audio") || has("music") {
        Some(vec![AssetKind::Audio])
    } else if has("script") {
        Some(vec![AssetKind::Script])
    } else if has("prefab") {
        Some(vec![AssetKind::Prefab])
    } else if has("terrain data") {
        Some(vec![AssetKind::Terrain])
    } else if has("skeleton") || has("animation") {
        Some(vec![AssetKind::Animation, AssetKind::Mesh])
    } else if has("font") {
        Some(vec![AssetKind::Font])
    } else if has("scene") {
        Some(vec![AssetKind::Scene])
    } else if has("path") || has("file") {
        // Some asset, kind unknown: offer everything the project holds.
        Some(AssetKind::IMPORTABLE.to_vec())
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extensions_map_to_one_kind_each() {
        let mut seen = std::collections::HashMap::new();
        for kind in AssetKind::IMPORTABLE {
            for extension in kind.extensions() {
                if let Some(other) = seen.insert(*extension, kind) {
                    panic!("{extension} belongs to both {other:?} and {kind:?}");
                }
            }
        }
    }

    #[test]
    fn keys_round_trip_and_match_the_engine_labels() {
        for kind in AssetKind::IMPORTABLE {
            assert_eq!(AssetKind::from_key(kind.key()), kind);
        }
        for engine in [
            "image",
            "mesh",
            "material",
            "audio",
            "script",
            "asset",
            "animation",
        ] {
            assert_eq!(AssetKind::from_key(engine).key(), engine);
        }
        assert_eq!(AssetKind::from_key("nonsense"), AssetKind::Other);
    }

    #[test]
    fn names_are_classified_case_insensitively() {
        assert_eq!(classify(Path::new("a/B.PNG")), Some(AssetKind::Image));
        assert_eq!(classify(Path::new("hero.glb")), Some(AssetKind::Mesh));
        assert_eq!(classify(Path::new("level.scene")), Some(AssetKind::Scene));
        assert_eq!(classify(Path::new("tree.prefab")), Some(AssetKind::Prefab));
        assert_eq!(classify(Path::new("mix.nature")), Some(AssetKind::Nature));
        assert_eq!(classify(Path::new("a.TTF")), Some(AssetKind::Font));
        assert_eq!(classify(Path::new("a.flac")), Some(AssetKind::Audio));
        assert_eq!(classify(Path::new("hero.mtl")), Some(AssetKind::Data));
        assert_eq!(classify(Path::new("a.zip")), None);
        assert_eq!(classify(Path::new("noextension")), None);
    }

    #[test]
    fn a_generator_recipe_is_not_a_material() {
        assert_eq!(
            classify(Path::new("Rock.generator.material")),
            Some(AssetKind::Generator)
        );
        assert_eq!(classify(Path::new("Rock.mat")), Some(AssetKind::Material));
    }

    #[test]
    fn the_name_wins_over_the_engines_label() {
        assert_eq!(of("Props/tree.prefab", "file", false), AssetKind::Prefab);
        assert_eq!(of("Props", "folder", true), AssetKind::Folder);
        // A name the table does not know keeps what the engine said.
        assert_eq!(of("x.unknown", "image", false), AssetKind::Image);
        assert_eq!(of("x.unknown", "file", false), AssetKind::Other);
    }

    #[test]
    fn refine_relabels_only_what_the_engine_called_a_file() {
        let info = |path: &str, kind: &str| doppio_engine::AssetInfo {
            name: path.rsplit('/').next().unwrap_or(path).to_owned(),
            relative_path: path.to_owned(),
            kind: kind.to_owned(),
            size_bytes: 1,
            is_directory: false,
        };
        let mut assets = vec![
            info("a.prefab", "file"),
            info("b.png", "image"),
            info("c.bmp", "file"),
            info("d.zip", "file"),
        ];
        refine(&mut assets);
        let kinds: Vec<&str> = assets.iter().map(|asset| asset.kind.as_str()).collect();
        assert_eq!(kinds, ["prefab", "image", "image", "file"]);
    }

    #[test]
    fn field_names_say_which_asset_they_want() {
        assert_eq!(for_field("Mesh Path"), Some(vec![AssetKind::Mesh]));
        assert_eq!(for_field("Material"), Some(vec![AssetKind::Material]));
        assert_eq!(for_field("Normal map"), Some(vec![AssetKind::Image]));
        assert_eq!(
            for_field("Splat Layer Paths 2"),
            Some(vec![AssetKind::Image])
        );
        assert_eq!(for_field("Clip Path"), Some(vec![AssetKind::Audio]));
        assert_eq!(for_field("Script"), Some(vec![AssetKind::Script]));
        assert_eq!(
            for_field("Terrain Data Path"),
            Some(vec![AssetKind::Terrain])
        );
        assert_eq!(for_field("Name"), None);
        assert_eq!(for_field("Slot Materials"), None);
        assert_eq!(for_field("Tag"), None);
        // An unknown "path" accepts anything the project has.
        assert_eq!(
            for_field("Custom Path").map(|kinds| kinds.len()),
            Some(AssetKind::IMPORTABLE.len())
        );
    }

    #[test]
    fn extension_tags_are_short_and_upper_case() {
        assert_eq!(extension_tag("rock.png"), "PNG");
        assert_eq!(extension_tag("noext"), "");
        assert_eq!(extension_tag("a.verylongext"), "");
    }
}

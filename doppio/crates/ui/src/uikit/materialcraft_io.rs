//! Engine-backed MaterialCraft import/export shared by the editor, MCP actions
//! and the headless CLI.

use crate::uikit::generators::{self, MaterialGraph, NodeKind};
use crate::uikit::materialcraft::{self, Manifest, MapEntry, PortableMap, PortableMaterial};
use doppio_engine::{Engine, TexturePackKind};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct ImportedMaterial {
    pub surface: doppio_engine::Material,
    pub graph: MaterialGraph,
    pub material_path: PathBuf,
    pub asset_path: Option<String>,
    pub source_path: Option<PathBuf>,
    pub source_map: Option<String>,
    pub warnings: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct ExportedMaterial {
    pub path: PathBuf,
    pub bytes: usize,
    pub maps: usize,
    pub unknown_nodes: usize,
}

fn project_relative(engine: &Engine, path: &Path) -> String {
    let root = engine.asset_path("").ok().map(PathBuf::from);
    root.and_then(|root| path.strip_prefix(root).ok().map(Path::to_path_buf))
        .unwrap_or_else(|| path.to_path_buf())
        .to_string_lossy()
        .replace('\\', "/")
}

fn safe_usage(usage: &str) -> String {
    usage
        .chars()
        .map(|character| {
            if character.is_ascii_alphanumeric() {
                character.to_ascii_lowercase()
            } else {
                '_'
            }
        })
        .collect()
}

/// Imports a `.mcmat` archive into a `.mat`, cooked maps, `source.sbs` and an
/// `.mgraph` sidecar in `output_directory` (or beside the archive).
pub fn import_portable(
    engine: &Engine,
    input: &Path,
    output_directory: Option<&Path>,
) -> Result<ImportedMaterial, String> {
    let bytes = std::fs::read(input).map_err(|error| format!("{}: {error}", input.display()))?;
    let portable = materialcraft::read_mcmat(&bytes)?;
    let stem = input
        .file_stem()
        .and_then(|stem| stem.to_str())
        .unwrap_or("imported");
    let directory = output_directory
        .map(Path::to_path_buf)
        .or_else(|| input.parent().map(Path::to_path_buf))
        .unwrap_or_default();
    std::fs::create_dir_all(&directory)
        .map_err(|error| format!("{}: {error}", directory.display()))?;

    let mut surface = engine.new_material().unwrap_or_default();
    surface.name = portable.manifest.name.clone();
    if surface.name.trim().is_empty() {
        surface.name = stem.to_owned();
    }
    let mut written: Vec<(String, PathBuf)> = Vec::new();
    for map in &portable.maps {
        let file_name = format!("{stem}_{}.png", safe_usage(&map.entry.usage));
        let path = directory.join(file_name);
        std::fs::write(&path, &map.bytes)
            .map_err(|error| format!("{}: {error}", path.display()))?;
        written.push((map.entry.usage.to_lowercase(), path));
    }
    let find = |usages: &[&str]| {
        written
            .iter()
            .find(|(usage, _)| usages.iter().any(|candidate| usage == candidate))
            .map(|(_, path)| path.clone())
    };
    let map_path = |path: &Path| project_relative(engine, path);
    if let Some(path) = find(&["basecolor", "base_color", "diffuse", "albedo"]) {
        surface.albedo_map = map_path(&path);
    }
    if let Some(path) = find(&["normal", "normalmap"]) {
        surface.normal_map = map_path(&path);
    }
    if let Some(path) = find(&["emissive", "emission"]) {
        surface.emission_map = map_path(&path);
    }
    let mut orm = find(&["orm", "occlusionroughnessmetallic", "mask"]);
    let ao = find(&["ambientocclusion", "ao", "occlusion"]);
    let roughness = find(&["roughness"]);
    let metallic = find(&["metallic"]);
    if orm.is_none() && (ao.is_some() || roughness.is_some() || metallic.is_some()) {
        let orm_path = directory.join(format!("{stem}_orm.png"));
        let ao = ao.as_ref().map(|path| path.to_string_lossy().into_owned());
        let roughness = roughness
            .as_ref()
            .map(|path| path.to_string_lossy().into_owned());
        let metallic = metallic
            .as_ref()
            .map(|path| path.to_string_lossy().into_owned());
        if engine
            .pack_texture_maps(
                ao.as_deref(),
                roughness.as_deref(),
                metallic.as_deref(),
                &orm_path.to_string_lossy(),
                TexturePackKind::Orm,
            )
            .is_ok()
        {
            orm = Some(orm_path);
        }
    }
    if let Some(path) = orm {
        surface.orm_map = map_path(&path);
    }
    let source_map = find(&["height", "displacement"]).map(|path| map_path(&path));
    apply_manifest_parameters(&mut surface, &portable.manifest);

    let material_path = directory.join(format!("{stem}.mat"));
    let material_text = material_path.to_string_lossy().into_owned();
    engine
        .save_material(&material_text, &surface)
        .map_err(|error| format!("{error:?}"))?;

    let mut warnings = Vec::new();
    let mut graph = MaterialGraph::default();
    let source_path = if portable.source.trim().is_empty() {
        None
    } else {
        let source_path = directory.join(format!("{stem}.sbs"));
        std::fs::write(&source_path, &portable.source)
            .map_err(|error| format!("{}: {error}", source_path.display()))?;
        match materialcraft::import_sbs(&portable.source, &portable.manifest.graph) {
            Ok(imported) => {
                graph = imported.graph;
                graph.resolution = portable.manifest.size[0].clamp(64, 1024);
                warnings.extend(imported.warnings);
                if imported.unknown_nodes > 0 {
                    warnings.push(format!(
                        "{} imported node(s) are placeholders",
                        imported.unknown_nodes
                    ));
                }
            }
            Err(error) => warnings.push(format!("source.sbs could not be mapped: {error}")),
        }
        Some(source_path)
    };
    let graph_path = material_path.with_extension("mgraph");
    std::fs::write(&graph_path, graph.to_text())
        .map_err(|error| format!("{}: {error}", graph_path.display()))?;

    let root = engine.asset_path("").ok().map(PathBuf::from);
    let asset_path = root
        .and_then(|root| material_path.strip_prefix(root).ok())
        .map(|path| path.to_string_lossy().replace('\\', "/"));

    Ok(ImportedMaterial {
        surface,
        graph,
        material_path,
        asset_path,
        source_path,
        source_map,
        warnings,
    })
}

fn apply_manifest_parameters(surface: &mut doppio_engine::Material, manifest: &Manifest) {
    for parameter in &manifest.parameters {
        let id = parameter
            .get("id")
            .or_else(|| parameter.get("label"))
            .or_else(|| parameter.get("name"))
            .and_then(|value| value.as_str())
            .unwrap_or_default()
            .to_lowercase();
        let value = json_number(parameter.get("value")).or_else(|| json_number(Some(parameter)));
        let Some(value) = value else {
            continue;
        };
        match id.as_str() {
            "roughness" | "rough" => surface.roughness = value.clamp(0.0, 1.0),
            "metallic" | "metal" => surface.metallic = value.clamp(0.0, 1.0),
            "reflectance" | "specular" => surface.reflectance = value.clamp(0.0, 1.0),
            "ao" | "ao_strength" | "ambientocclusion" => {
                surface.ao_strength = value.clamp(0.0, 1.0)
            }
            "normal_strength" | "normalstrength" => surface.normal_strength = value,
            "emission_strength" | "emissionstrength" => surface.emission_strength = value.max(0.0),
            _ => {}
        }
    }
}

fn json_number(value: Option<&serde_json::Value>) -> Option<f32> {
    let value = value?;
    if let Some(number) = value.as_f64() {
        return Some(number as f32);
    }
    if let Some(number) = value.get("v").and_then(serde_json::Value::as_f64) {
        return Some(number as f32);
    }
    None
}

/// Bakes the graph, writes a portable `.mcmat` with cooked maps and a minimal
/// SBS source, and validates the source by re-importing it.
pub fn export_portable(
    engine: &Engine,
    graph: &MaterialGraph,
    name: &str,
    output: &Path,
) -> Result<ExportedMaterial, String> {
    let mut texture_paths: Vec<(String, String)> = Vec::new();
    for node in &graph.nodes {
        let path = match &node.kind {
            NodeKind::Image { path, .. } | NodeKind::NormalMap { path, .. } => path,
            _ => continue,
        };
        if path.is_empty() || texture_paths.iter().any(|(key, _)| key == path) {
            continue;
        }
        let resolved = engine
            .resolve_path(path)
            .ok()
            .filter(|resolved| Path::new(resolved).exists())
            .or_else(|| engine.asset_path(path).ok())
            .ok_or_else(|| format!("texture not found: {path}"))?;
        texture_paths.push((path.clone(), resolved));
    }
    let mut textures = generators::TextureMap::new();
    for (key, path) in texture_paths {
        textures.insert(key, generators::decode_texture(&path)?);
    }
    let baked = graph.bake_with(&textures, None, None)?;

    let temp = std::env::temp_dir().join(format!(
        "doppio-mcmat-{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|duration| duration.as_nanos())
            .unwrap_or_default()
    ));
    std::fs::create_dir_all(&temp).map_err(|error| error.to_string())?;
    let save = |file: &str, pixels: &[u8]| -> Result<Vec<u8>, String> {
        let path = temp.join(file);
        engine
            .save_texture_rgba(
                &path.to_string_lossy(),
                baked.resolution,
                baked.resolution,
                pixels,
            )
            .map_err(|error| format!("{error:?}"))?;
        std::fs::read(&path).map_err(|error| error.to_string())
    };
    let base = save("baseColor.png", &baked.base)?;
    let roughness = save("roughness.png", &gray_to_rgba(&baked.roughness))?;
    let metallic = save("metallic.png", &gray_to_rgba(&baked.metallic))?;
    let height_pixels = gray_to_rgba(&baked.height);
    let height = save("height.png", &height_pixels)?;
    let ao = save("ambientOcclusion.png", &gray_to_rgba(&baked.ao))?;
    let height_path = temp.join("height.png");
    let normal_path = temp.join("normal.png");
    let normal_options = doppio_engine::TextureMapOptions {
        strength: 1.0,
        directx: false,
        wrap: true,
        min_value: 0.0,
        max_value: 1.0,
        invert: false,
        radius: 8,
    };
    engine
        .generate_texture_map(
            &height_path.to_string_lossy(),
            &normal_path.to_string_lossy(),
            doppio_engine::TextureMapKind::Normal,
            normal_options,
        )
        .map_err(|error| format!("{error:?}"))?;
    let normal = std::fs::read(&normal_path).map_err(|error| error.to_string())?;

    let maps = vec![
        PortableMap {
            entry: MapEntry {
                usage: "baseColor".to_owned(),
                file: "maps/baseColor.png".to_owned(),
                color_space: "srgb".to_owned(),
            },
            bytes: base,
        },
        PortableMap {
            entry: MapEntry {
                usage: "normal".to_owned(),
                file: "maps/normal.png".to_owned(),
                color_space: "linear".to_owned(),
            },
            bytes: normal,
        },
        PortableMap {
            entry: MapEntry {
                usage: "roughness".to_owned(),
                file: "maps/roughness.png".to_owned(),
                color_space: "linear".to_owned(),
            },
            bytes: roughness,
        },
        PortableMap {
            entry: MapEntry {
                usage: "metallic".to_owned(),
                file: "maps/metallic.png".to_owned(),
                color_space: "linear".to_owned(),
            },
            bytes: metallic,
        },
        PortableMap {
            entry: MapEntry {
                usage: "height".to_owned(),
                file: "maps/height.png".to_owned(),
                color_space: "linear".to_owned(),
            },
            bytes: height,
        },
        PortableMap {
            entry: MapEntry {
                usage: "ambientOcclusion".to_owned(),
                file: "maps/ambientOcclusion.png".to_owned(),
                color_space: "linear".to_owned(),
            },
            bytes: ao,
        },
    ];
    let source = materialcraft::export_sbs(graph, name);
    let imported = materialcraft::import_sbs(&source, name)
        .map_err(|error| format!("exported SBS failed validation: {error}"))?;
    if imported.graph.nodes.len() != graph.nodes.len() {
        return Err("exported SBS failed round-trip validation".to_owned());
    }
    if imported.graph.links.len() != graph.links.len() {
        return Err("exported SBS failed link round-trip validation".to_owned());
    }
    let manifest = Manifest::new(name, name, [baked.resolution, baked.resolution], {
        maps.iter().map(|map| map.entry.clone()).collect()
    });
    let bytes = materialcraft::write_mcmat(&PortableMaterial {
        manifest,
        maps: maps.clone(),
        source,
    })?;
    if let Some(parent) = output.parent() {
        std::fs::create_dir_all(parent).map_err(|error| error.to_string())?;
    }
    std::fs::write(output, &bytes).map_err(|error| error.to_string())?;
    let _ = std::fs::remove_dir_all(&temp);
    Ok(ExportedMaterial {
        path: output.to_path_buf(),
        bytes: bytes.len(),
        maps: maps.len(),
        unknown_nodes: graph
            .nodes
            .iter()
            .filter(|node| matches!(node.kind, NodeKind::Unknown { .. }))
            .count(),
    })
}

fn gray_to_rgba(gray: &[u8]) -> Vec<u8> {
    let mut pixels = Vec::with_capacity(gray.len() * 4);
    for value in gray {
        pixels.extend_from_slice(&[*value, *value, *value, 255]);
    }
    pixels
}

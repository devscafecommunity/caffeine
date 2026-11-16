//! Minimal OBJ/MTL and glTF material import. This maps the common PBR fields
//! into Doppio material definitions; unsupported extensions are ignored.

use serde_json::Value;
use std::path::Path;

#[derive(Debug, Clone, PartialEq)]
pub struct ImportedMaterialDef {
    pub name: String,
    pub albedo: [f32; 4],
    pub metallic: f32,
    pub roughness: f32,
    pub emission: [f32; 3],
    pub emission_strength: f32,
    pub albedo_map: String,
    pub normal_map: String,
    pub ao_map: String,
    pub roughness_map: String,
    pub metallic_map: String,
    pub emission_map: String,
    pub uv_tiling: [f32; 2],
    pub uv_offset: [f32; 2],
    pub alpha_mode: u8,
    pub alpha_cutoff: f32,
}

impl Default for ImportedMaterialDef {
    fn default() -> Self {
        Self {
            name: "Imported Material".to_owned(),
            albedo: [0.8, 0.8, 0.8, 1.0],
            metallic: 0.0,
            roughness: 0.5,
            emission: [0.0; 3],
            emission_strength: 0.0,
            albedo_map: String::new(),
            normal_map: String::new(),
            ao_map: String::new(),
            roughness_map: String::new(),
            metallic_map: String::new(),
            emission_map: String::new(),
            uv_tiling: [1.0, 1.0],
            uv_offset: [0.0, 0.0],
            alpha_mode: 0,
            alpha_cutoff: 0.5,
        }
    }
}

pub fn import_mesh_materials(path: &Path) -> Result<Vec<ImportedMaterialDef>, String> {
    let extension = path
        .extension()
        .and_then(|extension| extension.to_str())
        .unwrap_or_default()
        .to_ascii_lowercase();
    match extension.as_str() {
        "obj" => import_obj(path),
        "gltf" => import_gltf(path, false),
        "glb" => import_gltf(path, true),
        _ => Err(format!("unsupported mesh format: {}", path.display())),
    }
}

fn import_obj(path: &Path) -> Result<Vec<ImportedMaterialDef>, String> {
    let text =
        std::fs::read_to_string(path).map_err(|error| format!("{}: {error}", path.display()))?;
    let directory = path.parent().unwrap_or_else(|| Path::new("."));
    let mut materials = Vec::new();
    let mut libraries = Vec::new();
    for line in text.lines() {
        let mut words = line.split_whitespace();
        if words.next() == Some("mtllib") {
            libraries.extend(words.map(|name| directory.join(name)));
        }
    }
    if libraries.is_empty() {
        return Ok(materials);
    }
    for library in libraries {
        let text = std::fs::read_to_string(&library)
            .map_err(|error| format!("{}: {error}", library.display()))?;
        let library_dir = library.parent().unwrap_or_else(|| Path::new("."));
        let mut current: Option<ImportedMaterialDef> = None;
        for line in text.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let mut words = line.split_whitespace();
            let Some(key) = words.next() else {
                continue;
            };
            if key.eq_ignore_ascii_case("newmtl") {
                if let Some(material) = current.take() {
                    materials.push(material);
                }
                current = Some(ImportedMaterialDef {
                    name: words.collect::<Vec<_>>().join(" "),
                    ..ImportedMaterialDef::default()
                });
                continue;
            }
            let Some(material) = current.as_mut() else {
                continue;
            };
            let values: Vec<&str> = words.collect();
            match key.to_ascii_lowercase().as_str() {
                "kd" => material.albedo = parse_color(&values, material.albedo),
                "ke" => {
                    material.emission = parse_vec3(&values, material.emission);
                    material.emission_strength = 1.0;
                }
                "ns" => {
                    if let Some(value) = values.first().and_then(|value| value.parse::<f32>().ok())
                    {
                        material.roughness = (1.0 - value / 1000.0).clamp(0.05, 1.0);
                    }
                }
                "pr" => {
                    if let Some(value) = values.first().and_then(|value| value.parse::<f32>().ok())
                    {
                        material.roughness = value.clamp(0.0, 1.0);
                    }
                }
                "pm" => {
                    if let Some(value) = values.first().and_then(|value| value.parse::<f32>().ok())
                    {
                        material.metallic = value.clamp(0.0, 1.0);
                    }
                }
                "d" => {
                    if let Some(value) = values.first().and_then(|value| value.parse::<f32>().ok())
                    {
                        material.albedo[3] = value.clamp(0.0, 1.0);
                    }
                }
                "map_kd" => material.albedo_map = map_path(library_dir, &values),
                "map_bump" | "bump" | "norm" => {
                    material.normal_map = map_path(library_dir, &values)
                }
                "map_ka" => material.ao_map = map_path(library_dir, &values),
                "map_ks" | "map_ns" => material.roughness_map = map_path(library_dir, &values),
                "map_ke" => material.emission_map = map_path(library_dir, &values),
                _ => {}
            }
        }
        if let Some(material) = current.take() {
            materials.push(material);
        }
    }
    Ok(materials)
}

fn parse_color(values: &[&str], fallback: [f32; 4]) -> [f32; 4] {
    let numbers: Vec<f32> = values
        .iter()
        .take(3)
        .filter_map(|value| value.parse().ok())
        .collect();
    if numbers.len() == 3 {
        [numbers[0], numbers[1], numbers[2], fallback[3]]
    } else {
        fallback
    }
}

fn parse_vec3(values: &[&str], fallback: [f32; 3]) -> [f32; 3] {
    let numbers: Vec<f32> = values
        .iter()
        .take(3)
        .filter_map(|value| value.parse().ok())
        .collect();
    if numbers.len() == 3 {
        [numbers[0], numbers[1], numbers[2]]
    } else {
        fallback
    }
}

fn map_path(directory: &Path, values: &[&str]) -> String {
    let name = values
        .iter()
        .rev()
        .find(|value| !value.starts_with('-'))
        .copied()
        .unwrap_or_default();
    if name.is_empty() {
        return String::new();
    }
    directory.join(name).to_string_lossy().into_owned()
}

fn import_gltf(path: &Path, binary: bool) -> Result<Vec<ImportedMaterialDef>, String> {
    let bytes = std::fs::read(path).map_err(|error| format!("{}: {error}", path.display()))?;
    let json = if binary { glb_json(&bytes)? } else { bytes };
    let root: Value =
        serde_json::from_slice(&json).map_err(|error| format!("glTF JSON: {error}"))?;
    let directory = path.parent().unwrap_or_else(|| Path::new("."));
    let images: Vec<String> = root
        .get("images")
        .and_then(Value::as_array)
        .map(|images| {
            images
                .iter()
                .filter_map(|image| image.get("uri").and_then(Value::as_str))
                .map(|uri| {
                    if uri.starts_with("data:") {
                        String::new()
                    } else {
                        let decoded = uri.replace("%20", " ");
                        directory.join(decoded).to_string_lossy().into_owned()
                    }
                })
                .collect()
        })
        .unwrap_or_default();
    let textures: Vec<usize> = root
        .get("textures")
        .and_then(Value::as_array)
        .map(|textures| {
            textures
                .iter()
                .map(|texture| texture.get("source").and_then(Value::as_u64).unwrap_or(0) as usize)
                .collect()
        })
        .unwrap_or_default();
    let texture_path = |index: Option<u64>| -> String {
        let texture = index.unwrap_or(u64::MAX) as usize;
        textures
            .get(texture)
            .and_then(|source| images.get(*source))
            .cloned()
            .unwrap_or_default()
    };
    let texture_index =
        |value: Option<&Value>| -> Option<u64> { value?.get("index").and_then(Value::as_u64) };
    let materials = root
        .get("materials")
        .and_then(Value::as_array)
        .cloned()
        .unwrap_or_default();
    let mut imported = Vec::new();
    for (index, material) in materials.iter().enumerate() {
        let pbr = material
            .get("pbrMetallicRoughness")
            .cloned()
            .unwrap_or(Value::Null);
        let mut def = ImportedMaterialDef {
            name: material
                .get("name")
                .and_then(Value::as_str)
                .map(str::to_owned)
                .unwrap_or_else(|| format!("Material{}", index + 1)),
            ..ImportedMaterialDef::default()
        };
        if let Some(color) = pbr.get("baseColorFactor").and_then(Value::as_array) {
            for (component, value) in color.iter().take(4).enumerate() {
                if let Some(value) = value.as_f64() {
                    def.albedo[component] = value as f32;
                }
            }
        }
        if let Some(value) = pbr.get("metallicFactor").and_then(Value::as_f64) {
            def.metallic = value as f32;
        }
        if let Some(value) = pbr.get("roughnessFactor").and_then(Value::as_f64) {
            def.roughness = value as f32;
        }
        def.albedo_map = texture_path(texture_index(pbr.get("baseColorTexture")));
        if let Some(transform) = pbr
            .get("baseColorTexture")
            .and_then(|texture| texture.get("extensions"))
            .and_then(|extensions| extensions.get("KHR_texture_transform"))
        {
            if let Some(scale) = transform.get("scale").and_then(Value::as_array) {
                if let (Some(x), Some(y)) = (
                    scale.first().and_then(Value::as_f64),
                    scale.get(1).and_then(Value::as_f64),
                ) {
                    def.uv_tiling = [x as f32, y as f32];
                }
            }
            if let Some(offset) = transform.get("offset").and_then(Value::as_array) {
                if let (Some(x), Some(y)) = (
                    offset.first().and_then(Value::as_f64),
                    offset.get(1).and_then(Value::as_f64),
                ) {
                    def.uv_offset = [x as f32, y as f32];
                }
            }
        }
        def.normal_map = texture_path(texture_index(material.get("normalTexture")));
        def.ao_map = texture_path(texture_index(material.get("occlusionTexture")));
        def.emission_map = texture_path(texture_index(material.get("emissiveTexture")));
        if let Some(color) = material.get("emissiveFactor").and_then(Value::as_array) {
            for (component, value) in color.iter().take(3).enumerate() {
                if let Some(value) = value.as_f64() {
                    def.emission[component] = value as f32;
                }
            }
            def.emission_strength = 1.0;
        }
        if let Some(mode) = material.get("alphaMode").and_then(Value::as_str) {
            def.alpha_mode = match mode {
                "MASK" => 1,
                "BLEND" => 2,
                _ => 0,
            };
        }
        if let Some(cutoff) = material.get("alphaCutoff").and_then(Value::as_f64) {
            def.alpha_cutoff = cutoff as f32;
        }
        imported.push(def);
    }
    Ok(imported)
}

fn glb_json(bytes: &[u8]) -> Result<Vec<u8>, String> {
    if bytes.len() < 20 || &bytes[0..4] != b"glTF" {
        return Err("not a GLB file".to_owned());
    }
    let length = u32::from_le_bytes(bytes[12..16].try_into().unwrap_or([0; 4])) as usize;
    let chunk_type = &bytes[16..20];
    if chunk_type != b"JSON" || length > bytes.len().saturating_sub(20) {
        return Err("GLB has no JSON chunk".to_owned());
    }
    Ok(bytes[20..20 + length].to_vec())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn obj_mtl_materials_map_pbr_fields_and_textures() {
        let root = std::env::temp_dir().join(format!(
            "doppio-mtl-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|duration| duration.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&root).expect("temp dir");
        std::fs::write(
            root.join("model.mtl"),
            "newmtl Bricks\nKd 0.6 0.2 0.1\nNs 500\nPm 0.2\nmap_Kd brick.png\nmap_Bump normal.png\n",
        )
        .expect("mtl");
        std::fs::write(
            root.join("model.obj"),
            "mtllib model.mtl\nv 0 0 0\nusemtl Bricks\n",
        )
        .expect("obj");
        let materials = import_mesh_materials(&root.join("model.obj")).expect("import");
        assert_eq!(materials.len(), 1);
        let material = &materials[0];
        assert_eq!(material.name, "Bricks");
        assert!((material.albedo[0] - 0.6).abs() < 1e-5);
        assert!((material.roughness - 0.5).abs() < 1e-5);
        assert!((material.metallic - 0.2).abs() < 1e-5);
        assert!(material.albedo_map.ends_with("brick.png"));
        assert!(material.normal_map.ends_with("normal.png"));
        std::fs::remove_dir_all(root).ok();
    }

    #[test]
    fn gltf_materials_map_textures_and_alpha() {
        let root = std::env::temp_dir().join(format!(
            "doppio-gltf-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|duration| duration.as_nanos())
                .unwrap_or_default()
        ));
        std::fs::create_dir_all(&root).expect("temp dir");
        let json = serde_json::json!({
            "images": [{"uri": "base.png"}, {"uri": "normal.png"}],
            "textures": [{"source": 0}, {"source": 1}],
            "materials": [{
                "name": "Paint",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.2, 0.4, 0.6, 1.0],
                    "metallicFactor": 0.7,
                    "roughnessFactor": 0.3,
                    "baseColorTexture": {
                        "index": 0,
                        "extensions": {
                            "KHR_texture_transform": {
                                "scale": [2.0, 3.0],
                                "offset": [0.25, 0.5]
                            }
                        }
                    }
                },
                "normalTexture": {"index": 1},
                "alphaMode": "MASK",
                "alphaCutoff": 0.25
            }]
        });
        std::fs::write(root.join("model.gltf"), serde_json::to_vec(&json).unwrap()).expect("gltf");
        let materials = import_mesh_materials(&root.join("model.gltf")).expect("import");
        assert_eq!(materials.len(), 1);
        let material = &materials[0];
        assert_eq!(material.name, "Paint");
        assert_eq!(material.alpha_mode, 1);
        assert!((material.alpha_cutoff - 0.25).abs() < 1e-5);
        assert!(material.albedo_map.ends_with("base.png"));
        assert!(material.normal_map.ends_with("normal.png"));
        assert_eq!(material.uv_tiling, [2.0, 3.0]);
        assert_eq!(material.uv_offset, [0.25, 0.5]);
        std::fs::remove_dir_all(root).ok();
    }
}

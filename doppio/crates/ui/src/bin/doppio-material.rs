//! Headless MaterialCraft bridge: import/export `.mcmat`, inspect manifests and
//! convert `.sbs` XML into a Doppio `.mgraph`.

use doppio_ui::uikit::{
    generators::{self, MaterialGraph},
    materialcraft, materialcraft_io,
};
use std::path::{Path, PathBuf};

fn usage() -> ! {
    eprintln!(
        "usage:\n  doppio-material import <file.mcmat> [--out DIR]\n  \
         doppio-material import-sbs <file.sbs> [--out DIR]\n  \
         doppio-material export <file.mat|file.mgraph> --out <file.mcmat>\n  \
         doppio-material bake-graph <file.mgraph> [--size N] [--out DIR]\n  \
         doppio-material inspect <file.mcmat|file.sbs>\n  \
         doppio-material texset-new --out <file.texset> [--size 512]\n  \
         doppio-material texset-info <file.texset>\n  \
         doppio-material texset-export <file.texset> --out DIR [--preset NAME]\n  \
         doppio-material bake-preset <NAME|--all> --out DIR [--size 512] [--depth 0.04] [--list|--slugs]\n  \
         doppio-material nature <NAME|--all> --out DIR [--quality 0|1|2] [--seed N] [--list]\n  \
         doppio-material variants <FILE.generator.nature> --out DIR [--count 4] [--seed 1] [--quality 0|1|2]\n         doppio-material bake-texture <NAME|FILE.texgraph|--all> --out DIR [--size 512] [--bits 8|16] [--flipbook CxR] [--list|--slugs]"
    );
    std::process::exit(2);
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let Some(command) = args.first().map(String::as_str) else {
        usage();
    };
    let result = match command {
        "import" => import(&args[1..]),
        "import-sbs" => import_sbs(&args[1..]),
        "export" => export(&args[1..]),
        "bake-graph" => bake_graph(&args[1..]),
        "inspect" => inspect(&args[1..]),
        "texset-new" => texset_new(&args[1..]),
        "texset-info" => texset_info(&args[1..]),
        "texset-export" => texset_export(&args[1..]),
        "bake-preset" => bake_preset(&args[1..]),
        "bake-texture" => bake_texture(&args[1..]),
        "nature" => nature_models(&args[1..]),
        "variants" => recipe_variants(&args[1..]),
        _ => usage(),
    };
    if let Err(error) = result {
        eprintln!("doppio-material: {error}");
        std::process::exit(1);
    }
}

fn argument<'a>(args: &'a [String], name: &str) -> Option<&'a str> {
    args.iter()
        .position(|argument| argument == name)
        .and_then(|index| args.get(index + 1))
        .map(String::as_str)
}

fn positional(args: &[String]) -> Option<&str> {
    args.iter()
        .find(|argument| !argument.starts_with("--"))
        .map(String::as_str)
}

fn engine() -> Result<doppio_engine::Engine, String> {
    doppio_engine::Engine::load_default()
        .ok_or_else(|| "could not load libcaffeine-capi (build caffeine-capi first)".to_owned())
}

fn import(args: &[String]) -> Result<(), String> {
    let input = positional(args).ok_or_else(|| "missing .mcmat path".to_owned())?;
    let output = argument(args, "--out").map(PathBuf::from);
    let engine = engine()?;
    let imported = materialcraft_io::import_portable(&engine, Path::new(input), output.as_deref())?;
    for warning in &imported.warnings {
        eprintln!("warning: {warning}");
    }
    println!(
        "imported {} -> {} ({} node(s) mapped)",
        input,
        imported.material_path.display(),
        imported.graph.nodes.len()
    );
    Ok(())
}

fn import_sbs(args: &[String]) -> Result<(), String> {
    let input = positional(args).ok_or_else(|| "missing .sbs path".to_owned())?;
    let text = std::fs::read_to_string(input).map_err(|error| format!("{input}: {error}"))?;
    let stem = Path::new(input)
        .file_stem()
        .and_then(|stem| stem.to_str())
        .unwrap_or("imported");
    let directory = argument(args, "--out")
        .map(PathBuf::from)
        .or_else(|| Path::new(input).parent().map(Path::to_path_buf))
        .unwrap_or_default();
    std::fs::create_dir_all(&directory).map_err(|error| error.to_string())?;
    let imported = materialcraft::import_sbs(&text, stem)?;
    let output = directory.join(format!("{stem}.mgraph"));
    std::fs::write(&output, imported.graph.to_text()).map_err(|error| error.to_string())?;
    for warning in &imported.warnings {
        eprintln!("warning: {warning}");
    }
    println!(
        "{}: {} node(s), {} unknown placeholder(s) -> {}",
        imported.graph_name,
        imported.graph.nodes.len(),
        imported.unknown_nodes,
        output.display()
    );
    Ok(())
}

fn export(args: &[String]) -> Result<(), String> {
    let input = positional(args).ok_or_else(|| "missing material or mgraph path".to_owned())?;
    let output = argument(args, "--out")
        .map(PathBuf::from)
        .ok_or_else(|| "expected --out <file.mcmat>".to_owned())?;
    let input = Path::new(input);
    let graph_path = if input.extension().and_then(|ext| ext.to_str()) == Some("mgraph") {
        input.to_path_buf()
    } else {
        input.with_extension("mgraph")
    };
    let text = std::fs::read_to_string(&graph_path)
        .map_err(|error| format!("{}: {error}", graph_path.display()))?;
    let graph = MaterialGraph::from_text(&text)
        .ok_or_else(|| format!("{} is not a valid .mgraph", graph_path.display()))?;
    let name = input
        .file_stem()
        .and_then(|stem| stem.to_str())
        .unwrap_or("Doppio Material");
    let engine = engine()?;
    let exported = materialcraft_io::export_portable(&engine, &graph, name, &output)?;
    println!(
        "exported {} ({} maps, {} bytes{})",
        exported.path.display(),
        exported.maps,
        exported.bytes,
        if exported.unknown_nodes > 0 {
            format!(", {} placeholder node(s)", exported.unknown_nodes)
        } else {
            String::new()
        }
    );
    Ok(())
}

fn bake_graph(args: &[String]) -> Result<(), String> {
    let input = positional(args).ok_or_else(|| "missing .mgraph path".to_owned())?;
    let text = std::fs::read_to_string(input).map_err(|error| format!("{input}: {error}"))?;
    let mut graph =
        MaterialGraph::from_text(&text).ok_or_else(|| format!("{input} is not a valid .mgraph"))?;
    if let Some(size) = argument(args, "--size") {
        graph.resolution = size
            .parse::<u32>()
            .map_err(|_| format!("invalid --size `{size}`"))?;
    }
    let out = argument(args, "--out").map(PathBuf::from);
    let start = std::time::Instant::now();
    let mut last_percent = -1i32;
    let mut progress = |fraction: f32, stage: &str| {
        let percent = (fraction * 100.0).round() as i32;
        if percent != last_percent {
            last_percent = percent;
            eprintln!(
                "{:8.2}s {:3}% {}",
                start.elapsed().as_secs_f32(),
                percent,
                stage
            );
        }
    };
    let baked = graph.bake_with(&generators::TextureMap::new(), Some(&mut progress), None)?;
    let elapsed = start.elapsed().as_secs_f32();
    println!(
        "baked {input} at {}x{} in {elapsed:.2}s",
        baked.resolution, baked.resolution
    );
    let Some(out) = out else {
        return Ok(());
    };
    std::fs::create_dir_all(&out).map_err(|error| format!("{}: {error}", out.display()))?;
    let engine = engine()?;
    let res = baked.resolution;
    let gray = |values: &[u8]| -> Vec<u8> {
        values
            .iter()
            .flat_map(|value| [*value, *value, *value, 255])
            .collect()
    };
    let maps = [
        ("base", baked.base.clone()),
        ("roughness", gray(&baked.roughness)),
        ("metallic", gray(&baked.metallic)),
        ("height", gray(&baked.height)),
        ("ao", gray(&baked.ao)),
        (
            "packed",
            generators::ChannelPacking::default().packed8(&baked),
        ),
    ];
    for (name, pixels) in maps {
        let path = out.join(format!("{name}.png"));
        engine
            .save_texture_rgba(&path.to_string_lossy(), res, res, &pixels)
            .map_err(|error| format!("{error:?}"))?;
        println!("wrote {}", path.display());
    }
    Ok(())
}

fn texset_new(args: &[String]) -> Result<(), String> {
    let output = argument(args, "--out")
        .map(PathBuf::from)
        .ok_or_else(|| "expected --out <file.texset>".to_owned())?;
    let size = argument(args, "--size")
        .and_then(|size| size.parse::<u32>().ok())
        .unwrap_or(512)
        .clamp(64, 2048);
    let set = doppio_ui::uikit::texture_set::TextureSet {
        tile_size: size,
        name: output
            .file_stem()
            .and_then(|stem| stem.to_str())
            .unwrap_or("texture-set")
            .to_owned(),
        ..Default::default()
    };
    set.save(&output)?;
    println!(
        "created {} ({}×{}, {} channels)",
        output.display(),
        size,
        size,
        set.channels.len()
    );
    Ok(())
}

fn texset_info(args: &[String]) -> Result<(), String> {
    let input = positional(args).ok_or_else(|| "missing .texset path".to_owned())?;
    let set = doppio_ui::uikit::texture_set::TextureSet::load(Path::new(input))?;
    let summary = serde_json::json!({
        "name": set.name,
        "tile_size": set.tile_size,
        "tiles": set.tiles(),
        "active_tile": set.active_tile,
        "channels": set.channels.iter().map(|channel| channel.name.clone()).collect::<Vec<_>>(),
        "layers": set.layers.iter().map(|layer| serde_json::json!({
            "name": layer.name,
            "visible": layer.visible,
            "opacity": layer.opacity,
            "tiles": layer.pixels.keys().collect::<Vec<_>>(),
            "masked": !layer.masks.is_empty(),
        })).collect::<Vec<_>>(),
    });
    println!(
        "{}",
        serde_json::to_string_pretty(&summary).map_err(|error| error.to_string())?
    );
    Ok(())
}

fn texset_export(args: &[String]) -> Result<(), String> {
    let input = positional(args).ok_or_else(|| "missing .texset path".to_owned())?;
    let output = argument(args, "--out")
        .map(PathBuf::from)
        .ok_or_else(|| "expected --out DIR".to_owned())?;
    let set = doppio_ui::uikit::texture_set::TextureSet::load(Path::new(input))?;
    let presets = doppio_ui::uikit::texture_set::export_presets();
    let preset = argument(args, "--preset")
        .and_then(|name| {
            presets
                .iter()
                .find(|preset| preset.name.eq_ignore_ascii_case(name))
                .cloned()
        })
        .or_else(|| presets.first().cloned())
        .ok_or_else(|| "no export presets".to_owned())?;
    let stem = Path::new(input)
        .file_stem()
        .and_then(|stem| stem.to_str())
        .unwrap_or("texture-set");
    let written = set.export(&output, &preset, stem)?;
    println!(
        "exported {} map(s) with {} to {}",
        written.len(),
        preset.name,
        output.display()
    );
    Ok(())
}

fn inspect(args: &[String]) -> Result<(), String> {
    let input = positional(args).ok_or_else(|| "missing input path".to_owned())?;
    let path = Path::new(input);
    let bytes = std::fs::read(path).map_err(|error| format!("{input}: {error}"))?;
    if path.extension().and_then(|ext| ext.to_str()) == Some("mcmat") || bytes.starts_with(b"PK") {
        let material = materialcraft::read_mcmat(&bytes)?;
        let summary = serde_json::json!({
            "name": material.manifest.name,
            "graph": material.manifest.graph,
            "size": material.manifest.size,
            "version": material.manifest.version,
            "source_bytes": material.source.len(),
            "maps": material.maps.iter().map(|map| serde_json::json!({
                "usage": map.entry.usage,
                "file": map.entry.file,
                "color_space": map.entry.color_space,
                "bytes": map.bytes.len(),
            })).collect::<Vec<_>>(),
        });
        println!(
            "{}",
            serde_json::to_string_pretty(&summary).map_err(|error| error.to_string())?
        );
        return Ok(());
    }
    let text = String::from_utf8_lossy(&bytes);
    let imported = materialcraft::import_sbs(&text, input)?;
    let summary = serde_json::json!({
        "graph": imported.graph_name,
        "nodes": imported.graph.nodes.len(),
        "links": imported.graph.links.len(),
        "unknown_nodes": imported.unknown_nodes,
        "warnings": imported.warnings,
        "node_kinds": imported.graph.nodes.iter().map(|node| node.kind.title()).collect::<Vec<_>>(),
    });
    println!(
        "{}",
        serde_json::to_string_pretty(&summary).map_err(|error| error.to_string())?
    );
    Ok(())
}

/// Bakes procedural library presets into a ready-to-render material set:
/// `<name>_base.png`, `<name>_normal.png` (height in alpha), `<name>_orm.png`,
fn preset_slug(name: &str) -> String {
    name.to_lowercase()
        .chars()
        .map(|c| if c.is_alphanumeric() { c } else { '_' })
        .collect()
}

/// `<name>.mat` (parallax on) and `<name>_flat.mat` (same maps, parallax off).
fn bake_preset(args: &[String]) -> Result<(), String> {
    use doppio_ui::uikit::{generators, height_pack, material_library};
    if args.iter().any(|argument| argument == "--slugs") {
        // Machine-readable: slug <TAB> category <TAB> name (used by the gallery script).
        for preset in material_library::presets() {
            println!(
                "{}\t{}\t{}",
                preset_slug(preset.name),
                preset.category,
                preset.name
            );
        }
        return Ok(());
    }
    if args.iter().any(|argument| argument == "--list") {
        for preset in material_library::presets() {
            println!(
                "{:<10} {:<22} {}",
                preset.category, preset.name, preset.description
            );
        }
        return Ok(());
    }
    let out = argument(args, "--out")
        .map(PathBuf::from)
        .ok_or_else(|| "expected --out DIR".to_owned())?;
    let size: u32 = argument(args, "--size")
        .and_then(|value| value.parse().ok())
        .unwrap_or(512);
    let depth: f32 = argument(args, "--depth")
        .and_then(|value| value.parse().ok())
        .unwrap_or(0.04);
    let wanted: Vec<&material_library::Preset> = if args.iter().any(|a| a == "--all") {
        material_library::presets().iter().collect()
    } else {
        let name = positional(args).ok_or_else(|| "missing preset name (or --all)".to_owned())?;
        vec![material_library::presets()
            .iter()
            .find(|preset| preset.name.eq_ignore_ascii_case(name))
            .ok_or_else(|| format!("unknown preset `{name}` (use --list)"))?]
    };
    std::fs::create_dir_all(&out).map_err(|error| format!("{}: {error}", out.display()))?;
    let engine = engine()?;
    for preset in wanted {
        let mut graph = preset.build(&material_library::PresetParams::default());
        graph.resolution = size;
        let baked = graph
            .bake()
            .map_err(|error| format!("{}: {error}", preset.name))?;
        let slug = preset_slug(preset.name);
        let path = |suffix: &str| {
            out.join(format!("{slug}{suffix}"))
                .to_string_lossy()
                .into_owned()
        };
        let res = baked.resolution;
        let (base, normal, orm) = (path("_base.png"), path("_normal.png"), path("_orm.png"));
        let height = path("_height.png");
        engine
            .save_texture_rgba(&base, res, res, &baked.base)
            .map_err(|e| format!("{e:?}"))?;
        let gray: Vec<u8> = baked
            .height
            .iter()
            .flat_map(|v| [*v, *v, *v, 255])
            .collect();
        engine
            .save_texture_rgba(&height, res, res, &gray)
            .map_err(|e| format!("{e:?}"))?;
        let options = doppio_engine::TextureMapOptions {
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
                &height,
                &normal,
                doppio_engine::TextureMapKind::Normal,
                options,
            )
            .map_err(|e| format!("{e:?}"))?;
        let (nw, nh, mut pixels) = engine
            .load_texture_rgba(&normal)
            .map_err(|e| format!("{e:?}"))?;
        height_pack::pack(
            &mut pixels,
            nw as usize,
            nh as usize,
            &gray,
            res as usize,
            res as usize,
        );
        engine
            .save_texture_rgba(&normal, nw, nh, &pixels)
            .map_err(|e| format!("{e:?}"))?;
        engine
            .save_texture_rgba(
                &orm,
                res,
                res,
                &generators::ChannelPacking::default().packed8(&baked),
            )
            .map_err(|e| format!("{e:?}"))?;
        for (suffix, scale) in [(".mat", depth), ("_flat.mat", 0.0)] {
            let mut material = doppio_engine::Material {
                name: preset.name.to_owned(),
                albedo_map: base.clone(),
                normal_map: normal.clone(),
                orm_map: orm.clone(),
                height_scale: scale,
                parallax_steps: 64.0,
                uv_tiling: [8.0, 8.0],
                // The docs floor is a scaled plane tuned with an explicit tiling.
                uv_object_scale: false,
                ..doppio_engine::Material::default()
            };
            material.roughness = 1.0;
            engine
                .save_material(&path(suffix), &material)
                .map_err(|e| format!("{e:?}"))?;
        }
        println!("baked {} -> {}", preset.name, out.display());
    }
    Ok(())
}

/// Slug used for the output file names of a texture preset.
fn texture_slug(name: &str) -> String {
    name.to_lowercase()
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
        .collect::<String>()
        .split('_')
        .filter(|part| !part.is_empty())
        .collect::<Vec<_>>()
        .join("_")
}

/// Bakes Texture Generator presets (or a `.texgraph`) to PNG, optionally as a flipbook sheet.
fn bake_texture(args: &[String]) -> Result<(), String> {
    use doppio_ui::uikit::texture_graph as tg;
    let presets = tg::presets();
    if args.iter().any(|arg| arg == "--list") {
        for preset in presets {
            println!(
                "{} / {}  {}",
                preset.category, preset.name, preset.description
            );
        }
        return Ok(());
    }
    if args.iter().any(|arg| arg == "--slugs") {
        for preset in presets {
            println!(
                "{}\t{}\t{}",
                texture_slug(preset.name),
                preset.category,
                preset.name
            );
        }
        return Ok(());
    }
    let out = argument(args, "--out")
        .map(PathBuf::from)
        .ok_or_else(|| "expected --out DIR".to_owned())?;
    let size: u32 = argument(args, "--size")
        .and_then(|value| value.parse().ok())
        .unwrap_or(512);
    let depth = if argument(args, "--bits") == Some("16") {
        tg::BitDepth::Sixteen
    } else {
        tg::BitDepth::Eight
    };
    let flipbook = argument(args, "--flipbook").and_then(|value| {
        let (c, r) = value.split_once(['x', 'X'])?;
        Some((c.parse::<u32>().ok()?, r.parse::<u32>().ok()?))
    });
    let mut jobs: Vec<(String, generators::MaterialGraph)> = Vec::new();
    if args.iter().any(|arg| arg == "--all") {
        for preset in presets {
            jobs.push((texture_slug(preset.name), preset.build()));
        }
    } else {
        let target =
            positional(args).ok_or_else(|| "name a preset, a .texgraph or --all".to_owned())?;
        if let Some(preset) = presets
            .iter()
            .find(|preset| preset.name.eq_ignore_ascii_case(target))
        {
            jobs.push((texture_slug(preset.name), preset.build()));
        } else if Path::new(target).is_file() {
            let stem = Path::new(target)
                .file_stem()
                .and_then(|stem| stem.to_str())
                .unwrap_or("texture");
            jobs.push((texture_slug(stem), tg::load(Path::new(target))?));
        } else {
            return Err(format!("unknown texture preset `{target}` (try --list)"));
        }
    }
    for (slug, mut graph) in jobs {
        graph.resolution = size;
        let (baked, name) = match flipbook {
            Some((columns, rows)) => (
                tg::bake_flipbook(&graph, columns, rows)?,
                format!("{slug}_flipbook_{columns}x{rows}.png"),
            ),
            None => (
                graph.bake_texture_with(&std::collections::HashMap::new(), None, None)?,
                format!("{slug}.png"),
            ),
        };
        let path = out.join(name);
        tg::export_png(&baked, &path, depth)?;
        println!("wrote {}", path.display());
    }
    Ok(())
}

/// Exports Nature Generator presets as `.obj` files (one per part) plus their `.nature` recipe.
/// `variants <Name.generator.nature> --out DIR [--count 4] [--seed 1] [--quality 0|1|2]`:
/// bakes seeded variants of a nature recipe into a content-addressed cache folder.
fn recipe_variants(args: &[String]) -> Result<(), String> {
    use doppio_ui::uikit::{nature, recipe_ref};
    let recipe = positional(args).ok_or_else(|| "name a .generator.nature file".to_owned())?;
    let out = argument(args, "--out")
        .map(PathBuf::from)
        .ok_or_else(|| "expected --out DIR".to_owned())?;
    let count: u32 = argument(args, "--count")
        .and_then(|value| value.parse().ok())
        .unwrap_or(4);
    let seed: u64 = argument(args, "--seed")
        .and_then(|value| value.parse().ok())
        .unwrap_or(1);
    let quality = argument(args, "--quality")
        .and_then(|value| value.parse::<usize>().ok())
        .map(nature::Quality::from_index);
    for baked in
        recipe_ref::nature_variants(std::path::Path::new(recipe), seed, count, quality, &out)?
    {
        println!(
            "{:016x}  seed {:<5} {} {}",
            baked.key,
            baked.seed,
            if baked.cached { "cached" } else { "baked " },
            baked.directory.display()
        );
    }
    Ok(())
}

fn nature_models(args: &[String]) -> Result<(), String> {
    use doppio_ui::uikit::nature;
    let presets = nature::presets();
    if args.iter().any(|arg| arg == "--list") {
        for preset in &presets {
            println!(
                "{:<7} {:<16} {}",
                preset.category, preset.name, preset.description
            );
        }
        return Ok(());
    }
    let out = argument(args, "--out")
        .map(PathBuf::from)
        .ok_or_else(|| "expected --out DIR".to_owned())?;
    let quality = argument(args, "--quality")
        .and_then(|value| value.parse::<usize>().ok())
        .map(nature::Quality::from_index);
    let seed: Option<u32> = argument(args, "--seed").and_then(|value| value.parse().ok());
    let wanted: Vec<&nature::NaturePreset> = if args.iter().any(|a| a == "--all") {
        presets.iter().collect()
    } else {
        let name = positional(args).ok_or_else(|| "name a preset or use --all".to_owned())?;
        vec![presets
            .iter()
            .find(|preset| preset.name.eq_ignore_ascii_case(name))
            .ok_or_else(|| format!("unknown nature preset `{name}` (try --list)"))?]
    };
    for preset in wanted {
        let mut graph = preset.graph();
        if let Some(seed) = seed {
            // Re-seeds every node that has a seed, so the same preset gives another individual.
            let ids: Vec<nature::NodeId> = graph.nodes.iter().map(|n| n.id).collect();
            for id in ids {
                if graph.get(id, "seed").is_some() {
                    graph.set(id, "seed", seed as f32);
                }
            }
        }
        let quality = quality.unwrap_or(preset.quality);
        let model = graph.build(quality)?;
        let files = nature::write_obj_files(&model, &out, preset.name)?;
        std::fs::write(
            out.join(format!(
                "{}.{}",
                nature::slug(preset.name),
                nature::EXTENSION
            )),
            nature::to_text(preset.name, &graph, quality),
        )
        .map_err(|error| error.to_string())?;
        println!(
            "{}: {} triangles, {} part(s): {}",
            preset.name,
            model.triangles(),
            model.parts.len(),
            files.join(", ")
        );
    }
    Ok(())
}

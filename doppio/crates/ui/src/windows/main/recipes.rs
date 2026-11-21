//! Opening and writing `generator.*` recipe files (see `uikit::recipe`).
//!
//! Every generator writes its recipe next to the asset it bakes. Double-clicking the recipe,
//! or the baked asset when it has one, reopens the graph in the matching generator.

use super::{App, LogLevel};
use crate::uikit::generators::MaterialGraph;
use crate::uikit::nature;
use crate::uikit::recipe::{self, Kind, Recipe};
use crate::uikit::{terrain, texture_graph, water};
use std::path::{Path, PathBuf};

impl App {
    /// Write `<stem>.generator.<kind>` in `directory` and return its path.
    pub(super) fn write_recipe(
        &mut self,
        directory: &Path,
        kind: Kind,
        stem: &str,
        name: &str,
        product: &str,
        graph: String,
    ) -> Result<PathBuf, String> {
        let path = directory.join(recipe::file_name(stem, kind));
        recipe::write(&path, &Recipe::new(kind, name, product, graph))?;
        // The file is new to the Assets pane until the engine rescans the folder.
        self.refresh_assets();
        Ok(path)
    }

    /// The material generator graph saved for a `.mat`, preferring its recipe.
    pub(super) fn material_recipe_graph(product: &Path) -> Option<MaterialGraph> {
        let path = recipe::recipe_for_product(product)?;
        let recipe = recipe::read(&path).ok()?;
        (recipe.kind() == Some(Kind::Material))
            .then(|| MaterialGraph::from_text(&recipe.graph))
            .flatten()
    }

    /// Save the open material's generator graph as `<name>.generator.material`.
    pub(super) fn write_material_recipe(&mut self, product: &Path, graph: &MaterialGraph) {
        let Some(directory) = product.parent() else {
            return;
        };
        let stem = product
            .file_stem()
            .and_then(|stem| stem.to_str())
            .unwrap_or("Material")
            .to_owned();
        let file = product
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or_default()
            .to_owned();
        if let Err(error) = self.write_recipe(
            directory,
            Kind::Material,
            &stem,
            &stem,
            &file,
            graph.to_text(),
        ) {
            self.log(
                LogLevel::Error,
                format!("Could not save generator recipe: {error}"),
            );
        }
    }

    /// Reopen the graph of a recipe in its generator.
    pub(super) fn open_recipe_file(&mut self, path: &Path) {
        let recipe = match recipe::read(path) {
            Ok(recipe) => recipe,
            Err(error) => {
                self.log(LogLevel::Error, format!("Generator file: {error}"));
                return;
            }
        };
        let Some(kind) = recipe.kind() else {
            return;
        };
        let opened = match kind {
            Kind::Texture => match texture_graph::from_text(&recipe.graph) {
                Some(graph) => {
                    self.texgen.name = recipe.name.clone();
                    self.texgen.path = Some(path.to_path_buf());
                    self.texgen.load_graph(graph);
                    self.texgen.dirty = false;
                    self.show_texgen_pane();
                    true
                }
                None => false,
            },
            Kind::Nature => match nature::from_text(&recipe.graph) {
                Some((name, graph, quality)) => {
                    self.nature.name = name;
                    self.nature.quality = quality;
                    self.nature.load(graph);
                    self.nature.dirty = false;
                    self.show_nature_pane();
                    true
                }
                None => false,
            },
            Kind::Terrain => match terrain::from_text(&recipe.graph) {
                Some((name, graph)) => {
                    self.terraingen.name = name;
                    self.terraingen.load(graph);
                    self.terraingen.dirty = false;
                    self.show_terrain_pane();
                    true
                }
                None => false,
            },
            Kind::Water => match water::from_text(&recipe.graph) {
                Some((name, graph)) => {
                    self.watergen.name = name;
                    self.watergen.load(graph);
                    self.watergen.dirty = false;
                    self.show_water_pane();
                    true
                }
                None => false,
            },
            Kind::Material => {
                let product = path
                    .parent()
                    .map(|directory| directory.join(&recipe.product))
                    .filter(|product| !recipe.product.is_empty() && product.is_file());
                match product {
                    Some(product) => {
                        self.open_material_path(&product.to_string_lossy());
                        self.show_generator_pane();
                        true
                    }
                    None => false,
                }
            }
        };
        if opened {
            self.log(
                LogLevel::Action,
                format!("Opened {} recipe {}", kind.label(), recipe.name),
            );
        } else {
            self.log(
                LogLevel::Error,
                format!(
                    "Could not read the {} graph in {}",
                    kind.label(),
                    path.display()
                ),
            );
        }
    }
}

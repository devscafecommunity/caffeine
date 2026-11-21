//! A tiny indexed triangle mesh for the Nature Generator: parts, normals, UVs and OBJ export.
//!
//! Everything here is plain Rust (no engine, no UI), so generators are deterministic and testable.
//! A model is a list of [`Part`]s. Each part has its own vertex arrays and a [`MaterialSpec`], and
//! is exported as its own `.obj`: the engine gives one material to a mesh, so a tree becomes a
//! trunk part (bark) and a leaves part (cutout cards).

pub type V3 = [f32; 3];

pub fn add(a: V3, b: V3) -> V3 {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}
pub fn sub(a: V3, b: V3) -> V3 {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}
pub fn mul(a: V3, k: f32) -> V3 {
    [a[0] * k, a[1] * k, a[2] * k]
}
pub fn dot(a: V3, b: V3) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}
pub fn cross(a: V3, b: V3) -> V3 {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}
pub fn length(a: V3) -> f32 {
    dot(a, a).sqrt()
}
pub fn normalize(a: V3) -> V3 {
    let l = length(a);
    if l < 1e-9 {
        [0.0, 1.0, 0.0]
    } else {
        mul(a, 1.0 / l)
    }
}
pub fn lerp(a: V3, b: V3, t: f32) -> V3 {
    add(mul(a, 1.0 - t), mul(b, t))
}

/// How a part is shaded in the engine. The Nature tool turns these into real `.mat` files.
#[derive(Debug, Clone, PartialEq)]
pub enum MaterialSpec {
    /// A material-library preset baked to maps (bark, rock...).
    Preset(&'static str),
    /// A Texture Generator preset (alpha cut-out cards: leaves, petals, blades).
    Cards(&'static str),
    /// A plain colour and roughness (stylised models, flower centres).
    Solid { rgb: [f32; 3], roughness: f32 },
    /// One of **your own** materials: an asset path to an existing `.mat` (used as it is).
    Project(String, (u32, u32)),
    /// One of your own images with transparency (a PNG leaf, petal, grass blade...): turned into a
    /// cut-out card material.
    ProjectTexture(String, (u32, u32)),
    /// Built by a **Material node** in the graph: colour, your textures and the surface settings.
    Custom(CustomMaterial),
}

/// The settings of a Material node plus the texture layers Texture nodes added.
#[derive(Debug, Clone, PartialEq)]
pub struct CustomMaterial {
    pub rgb: [f32; 3],
    pub roughness: f32,
    pub metallic: f32,
    /// A project `.mat` to start from (its maps and settings); empty = a plain material.
    pub base: String,
    /// Texture layers in the order the nodes were wired; they are summed per slot at placement.
    pub layers: Vec<TextureLayer>,
    pub cutout: bool,
    pub cutoff: f32,
    pub normal_strength: f32,
    pub translucency: f32,
    pub sway: f32,
    pub flutter: f32,
}

impl CustomMaterial {
    /// A plain material of one colour (what a Texture node starts from).
    pub fn plain(rgb: [f32; 3], roughness: f32) -> Self {
        CustomMaterial {
            rgb,
            roughness,
            metallic: 0.0,
            base: String::new(),
            layers: Vec::new(),
            cutout: false,
            cutoff: 0.45,
            normal_strength: 1.0,
            translucency: 0.0,
            sway: 0.0,
            flutter: 0.0,
        }
    }
}

/// Which map of the material a layer writes to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TextureSlot {
    Albedo,
    Normal,
    /// Occlusion (R), roughness (G), metallic (B).
    Orm,
}

impl TextureSlot {
    pub const ALL: [TextureSlot; 3] = [TextureSlot::Albedo, TextureSlot::Normal, TextureSlot::Orm];
}

/// How a layer combines with what is already in its slot.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LayerBlend {
    /// Mix toward the layer by its strength.
    Replace,
    /// Add the layer (scaled by its strength); for normals, add the slopes.
    Add,
    /// Multiply (darkens; for normals, add the slopes).
    Multiply,
    /// Contrast-preserving overlay.
    Overlay,
}

impl LayerBlend {
    pub const ALL: [LayerBlend; 4] = [
        LayerBlend::Replace,
        LayerBlend::Add,
        LayerBlend::Multiply,
        LayerBlend::Overlay,
    ];
}

/// One image of a Texture node: where it goes, how it blends, how often it repeats.
#[derive(Debug, Clone, PartialEq)]
pub struct TextureLayer {
    pub slot: TextureSlot,
    /// Project-relative image path.
    pub path: String,
    pub blend: LayerBlend,
    pub strength: f32,
    pub tiling: [f32; 2],
    /// Multiplies the albedo layer (a tint); ignored for other slots.
    pub tint: [f32; 3],
}

impl MaterialSpec {
    /// Short stable name used for file names.
    pub fn slug(&self) -> String {
        match self {
            MaterialSpec::Preset(name) | MaterialSpec::Cards(name) => name
                .to_lowercase()
                .chars()
                .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
                .collect(),
            MaterialSpec::Project(path, _) | MaterialSpec::ProjectTexture(path, _) => {
                let stem = std::path::Path::new(path)
                    .file_stem()
                    .and_then(|s| s.to_str())
                    .unwrap_or("custom");
                format!(
                    "project_{}",
                    stem.to_lowercase()
                        .chars()
                        .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
                        .collect::<String>()
                )
            }
            MaterialSpec::Custom(custom) => {
                use std::hash::{Hash, Hasher};
                // Stable across runs (the default hasher has fixed keys): one file per setting set.
                let mut hasher = std::collections::hash_map::DefaultHasher::new();
                format!("{custom:?}").hash(&mut hasher);
                format!("custom_{:012x}", hasher.finish() & 0xffff_ffff_ffff)
            }
            MaterialSpec::Solid { rgb, .. } => format!(
                "solid_{:02x}{:02x}{:02x}",
                (rgb[0] * 255.0) as u8,
                (rgb[1] * 255.0) as u8,
                (rgb[2] * 255.0) as u8
            ),
        }
    }
}

#[derive(Debug, Clone)]
pub struct Part {
    pub name: String,
    pub material: MaterialSpec,
    pub positions: Vec<V3>,
    pub normals: Vec<V3>,
    pub uvs: Vec<[f32; 2]>,
    pub indices: Vec<u32>,
    /// Colour used by the editor preview only.
    pub preview: [f32; 3],
    /// A generated texture set (from the Surface node); replaces `material` when placed.
    pub surface: Option<super::surface::SurfaceSpec>,
    /// When the part is many copies of one small mesh (scattered moss, pebbles, clumps), the
    /// copies as transforms. `positions`/`indices` still hold the merged geometry (preview, stats,
    /// export); placing in the scene writes the template once and creates one entity per copy,
    /// which the renderer **GPU-instances** (one draw call, per-copy wind phase).
    pub instances: Option<Instances>,
    /// Optional per-vertex colour, parallel to `positions` (empty or the same length): rgb tints
    /// the albedo (baked occlusion, per-leaf variation), alpha is the **painted flutter weight**
    /// (0 = the stalk, rigid; 1 = the tip, full shimmer). Written to the `.obj` as
    /// `v x y z r g b a`, which the engine reads into a second vertex stream.
    pub colors: Vec<[f32; 4]>,
}

/// One copy of an instanced template, in the engine's transform terms.
#[derive(Debug, Clone, PartialEq)]
pub struct InstanceTransform {
    pub position: V3,
    /// Pitch, yaw, roll in degrees (the engine's ZYX convention).
    pub euler_degrees: [f32; 3],
    pub scale: f32,
}

#[derive(Debug, Clone)]
pub struct Instances {
    pub template: Box<Part>,
    pub transforms: Vec<InstanceTransform>,
}

/// Engine Euler angles (pitch, yaw, roll in degrees, ZYX) of a rotation given by where it sends
/// the X, Y and Z axes (the matrix columns).
pub fn euler_from_columns(c0: V3, c1: V3, c2: V3) -> [f32; 3] {
    // R = [c0 c1 c2]; R_ij is component i of column j.
    let pitch = c1[2].atan2(c2[2]);
    let yaw = (-c0[2]).clamp(-1.0, 1.0).asin();
    let roll = c0[1].atan2(c0[0]);
    [pitch.to_degrees(), yaw.to_degrees(), roll.to_degrees()]
}

impl Part {
    pub fn new(name: &str, material: MaterialSpec, preview: [f32; 3]) -> Self {
        Part {
            name: name.to_owned(),
            material,
            positions: Vec::new(),
            normals: Vec::new(),
            uvs: Vec::new(),
            indices: Vec::new(),
            preview,
            surface: None,
            instances: None,
            colors: Vec::new(),
        }
    }

    pub fn vertex(&mut self, position: V3, normal: V3, uv: [f32; 2]) -> u32 {
        self.positions.push(position);
        self.normals.push(normal);
        self.uvs.push(uv);
        // Once a part is painted, later vertices are white (neutral) so the arrays stay parallel.
        if !self.colors.is_empty() {
            self.colors.push([1.0; 4]);
        }
        (self.positions.len() - 1) as u32
    }

    pub fn triangle(&mut self, a: u32, b: u32, c: u32) {
        self.indices.extend_from_slice(&[a, b, c]);
    }

    pub fn quad(&mut self, a: u32, b: u32, c: u32, d: u32) {
        self.triangle(a, b, c);
        self.triangle(a, c, d);
    }

    pub fn triangle_count(&self) -> usize {
        self.indices.len() / 3
    }

    pub fn is_empty(&self) -> bool {
        self.indices.is_empty()
    }

    /// Axis-aligned bounds `(min, max)`.
    pub fn bounds(&self) -> (V3, V3) {
        let mut lo = [f32::MAX; 3];
        let mut hi = [f32::MIN; 3];
        for p in &self.positions {
            for k in 0..3 {
                lo[k] = lo[k].min(p[k]);
                hi[k] = hi[k].max(p[k]);
            }
        }
        (lo, hi)
    }

    pub fn translate(&mut self, offset: V3) {
        for p in &mut self.positions {
            *p = add(*p, offset);
        }
    }

    /// Smooth normals from the triangles (area weighted), replacing the current ones.
    pub fn recompute_normals(&mut self) {
        let mut sums = vec![[0.0f32; 3]; self.positions.len()];
        for tri in self.indices.chunks_exact(3) {
            let (a, b, c) = (tri[0] as usize, tri[1] as usize, tri[2] as usize);
            let n = cross(
                sub(self.positions[b], self.positions[a]),
                sub(self.positions[c], self.positions[a]),
            );
            for i in [a, b, c] {
                sums[i] = add(sums[i], n);
            }
        }
        self.normals = sums.into_iter().map(normalize).collect();
    }

    /// Gives every triangle its own vertices with the face normal (hard edges, low-poly look).
    pub fn flat_shade(&mut self) {
        let mut out = Part::new(&self.name, self.material.clone(), self.preview);
        for tri in self.indices.chunks_exact(3) {
            let p = [
                self.positions[tri[0] as usize],
                self.positions[tri[1] as usize],
                self.positions[tri[2] as usize],
            ];
            let n = normalize(cross(sub(p[1], p[0]), sub(p[2], p[0])));
            let ids: Vec<u32> = (0..3)
                .map(|k| out.vertex(p[k], n, self.uvs[tri[k] as usize]))
                .collect();
            out.triangle(ids[0], ids[1], ids[2]);
        }
        *self = out;
    }

    /// Appends another part's geometry (materials must match; the caller guarantees it).
    /// Paints a vertex: rgb tint and flutter weight. Vertices created later stay white / weight 1.
    pub fn set_color(&mut self, vertex: u32, rgba: [f32; 4]) {
        if self.colors.len() < self.positions.len() {
            self.colors.resize(self.positions.len(), [1.0; 4]);
        }
        if let Some(slot) = self.colors.get_mut(vertex as usize) {
            *slot = rgba;
        }
    }

    /// Pads the colours with white up to the vertex count (call after the last painted vertex).
    pub fn pad_colors(&mut self) {
        if !self.colors.is_empty() && self.colors.len() < self.positions.len() {
            self.colors.resize(self.positions.len(), [1.0; 4]);
        }
    }

    /// The colours when they line up with the vertices and say anything (not all white).
    pub fn painted_colors(&self) -> Option<&[[f32; 4]]> {
        (self.colors.len() == self.positions.len() && self.colors.iter().any(|c| *c != [1.0; 4]))
            .then_some(self.colors.as_slice())
    }

    pub fn append(&mut self, other: &Part) {
        // Colours stay parallel to the vertices: pad whichever side has none with white.
        if self.painted_colors().is_some() || other.painted_colors().is_some() {
            self.colors.resize(self.positions.len(), [1.0; 4]);
            match other.painted_colors() {
                Some(colors) => self.colors.extend_from_slice(colors),
                None => self
                    .colors
                    .extend(std::iter::repeat_n([1.0; 4], other.positions.len())),
            }
        } else {
            self.colors.clear();
        }
        // Two sets of copies (or copies and plain geometry) are one merged mesh from here on.
        self.instances = None;
        let base = self.positions.len() as u32;
        self.positions.extend_from_slice(&other.positions);
        self.normals.extend_from_slice(&other.normals);
        self.uvs.extend_from_slice(&other.uvs);
        self.indices.extend(other.indices.iter().map(|i| i + base));
    }

    /// Wavefront OBJ text: `v`, `vt`, `vn` and `f v/vt/vn`, one object named after the part.
    pub fn to_obj(&self) -> String {
        let mut text = format!("# Caffeine Nature Generator\no {}\n", self.name);
        let colors = self.painted_colors();
        for (i, p) in self.positions.iter().enumerate() {
            match colors {
                Some(c) => text.push_str(&format!(
                    "v {:.5} {:.5} {:.5} {:.4} {:.4} {:.4} {:.4}\n",
                    p[0], p[1], p[2], c[i][0], c[i][1], c[i][2], c[i][3]
                )),
                None => text.push_str(&format!("v {:.5} {:.5} {:.5}\n", p[0], p[1], p[2])),
            }
        }
        for t in &self.uvs {
            text.push_str(&format!("vt {:.5} {:.5}\n", t[0], t[1]));
        }
        for n in &self.normals {
            text.push_str(&format!("vn {:.4} {:.4} {:.4}\n", n[0], n[1], n[2]));
        }
        for tri in self.indices.chunks_exact(3) {
            let i = |k: usize| tri[k] + 1;
            text.push_str(&format!(
                "f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n",
                a = i(0),
                b = i(1),
                c = i(2)
            ));
        }
        text
    }
}

impl Part {
    /// Midpoint subdivision (each triangle becomes four); normals and UVs are interpolated.
    pub fn subdivide(&self) -> Part {
        use std::collections::HashMap;
        let mut out = Part::new(&self.name, self.material.clone(), self.preview);
        out.positions = self.positions.clone();
        out.normals = self.normals.clone();
        out.uvs = self.uvs.clone();
        let mut cache: HashMap<(u32, u32), u32> = HashMap::new();
        let mut mid = |out: &mut Part, a: u32, b: u32| -> u32 {
            let key = (a.min(b), a.max(b));
            if let Some(i) = cache.get(&key) {
                return *i;
            }
            let (pa, pb) = (out.positions[a as usize], out.positions[b as usize]);
            let (na, nb) = (out.normals[a as usize], out.normals[b as usize]);
            let (ua, ub) = (out.uvs[a as usize], out.uvs[b as usize]);
            let i = out.vertex(
                lerp(pa, pb, 0.5),
                normalize(lerp(na, nb, 0.5)),
                [(ua[0] + ub[0]) * 0.5, (ua[1] + ub[1]) * 0.5],
            );
            cache.insert(key, i);
            i
        };
        for tri in self.indices.chunks_exact(3) {
            let (a, b, c) = (tri[0], tri[1], tri[2]);
            let (ab, bc, ca) = (
                mid(&mut out, a, b),
                mid(&mut out, b, c),
                mid(&mut out, c, a),
            );
            out.triangle(a, ab, ca);
            out.triangle(b, bc, ab);
            out.triangle(c, ca, bc);
            out.triangle(ab, bc, ca);
        }
        out
    }

    /// Welds vertices that share a position (needed before smoothing geometry that was unwelded).
    pub fn weld(&self, tolerance: f32) -> Part {
        use std::collections::HashMap;
        let key = |p: V3| {
            [
                (p[0] / tolerance).round() as i64,
                (p[1] / tolerance).round() as i64,
                (p[2] / tolerance).round() as i64,
            ]
        };
        let mut map: HashMap<[i64; 3], u32> = HashMap::new();
        let mut out = Part::new(&self.name, self.material.clone(), self.preview);
        let mut remap = Vec::with_capacity(self.positions.len());
        for (i, p) in self.positions.iter().enumerate() {
            let id = *map
                .entry(key(*p))
                .or_insert_with(|| out.vertex(*p, self.normals[i], self.uvs[i]));
            remap.push(id);
        }
        for tri in self.indices.chunks_exact(3) {
            let t = [
                remap[tri[0] as usize],
                remap[tri[1] as usize],
                remap[tri[2] as usize],
            ];
            if t[0] != t[1] && t[1] != t[2] && t[0] != t[2] {
                out.triangle(t[0], t[1], t[2]);
            }
        }
        out
    }

    /// Per-corner normals averaged only over neighbouring faces that bend less than `degrees`
    /// (smooth on lumps, crisp along creases). The mesh is unwelded; UVs are kept. Faces are
    /// neighbours when they share a vertex position, so unwelded input works too.
    pub fn crease_normals(&self, degrees: f32) -> Part {
        use std::collections::HashMap;
        let threshold = degrees.to_radians().cos();
        let key = |p: V3| {
            [
                (p[0] * 20000.0).round() as i64,
                (p[1] * 20000.0).round() as i64,
                (p[2] * 20000.0).round() as i64,
            ]
        };
        let faces: Vec<[usize; 3]> = self
            .indices
            .chunks_exact(3)
            .map(|t| [t[0] as usize, t[1] as usize, t[2] as usize])
            .collect();
        let area: Vec<V3> = faces
            .iter()
            .map(|f| {
                cross(
                    sub(self.positions[f[1]], self.positions[f[0]]),
                    sub(self.positions[f[2]], self.positions[f[0]]),
                )
            })
            .collect();
        let mut at: HashMap<[i64; 3], Vec<usize>> = HashMap::new();
        for (i, f) in faces.iter().enumerate() {
            for v in f {
                at.entry(key(self.positions[*v])).or_default().push(i);
            }
        }
        let mut out = Part::new(&self.name, self.material.clone(), self.preview);
        for (i, f) in faces.iter().enumerate() {
            let own = normalize(area[i]);
            let ids: Vec<u32> = f
                .iter()
                .map(|v| {
                    let mut sum = [0.0f32; 3];
                    for other in &at[&key(self.positions[*v])] {
                        if dot(normalize(area[*other]), own) >= threshold {
                            sum = add(sum, area[*other]);
                        }
                    }
                    out.vertex(self.positions[*v], normalize(sum), self.uvs[*v])
                })
                .collect();
            out.triangle(ids[0], ids[1], ids[2]);
        }
        out
    }
}

/// A generated model: one or more parts.
#[derive(Debug, Clone, Default)]
pub struct NatureModel {
    pub parts: Vec<Part>,
}

impl NatureModel {
    pub fn triangles(&self) -> usize {
        self.parts.iter().map(Part::triangle_count).sum()
    }

    pub fn vertices(&self) -> usize {
        self.parts.iter().map(|p| p.positions.len()).sum()
    }

    /// One Wavefront OBJ holding the listed parts, each as its own `usemtl <part name>` group. The engine turns every
    /// group into a **sub-mesh**, so the parts keep their own materials (`Material slots` on the Mesh Filter, in the
    /// order of `parts`) while they place as one entity. Vertex colours are kept: parts without painted ones are white.
    pub fn merged_obj(&self, parts: &[usize], name: &str) -> String {
        let chosen: Vec<&Part> = parts.iter().filter_map(|i| self.parts.get(*i)).collect();
        let painted = chosen.iter().any(|p| p.painted_colors().is_some());
        let mut text = format!("# Caffeine Nature Generator\no {name}\n");
        for part in &chosen {
            let colors = part.painted_colors();
            for (i, p) in part.positions.iter().enumerate() {
                if painted {
                    let c = colors.map_or([1.0; 4], |c| c[i]);
                    text.push_str(&format!(
                        "v {:.5} {:.5} {:.5} {:.4} {:.4} {:.4} {:.4}\n",
                        p[0], p[1], p[2], c[0], c[1], c[2], c[3]
                    ));
                } else {
                    text.push_str(&format!("v {:.5} {:.5} {:.5}\n", p[0], p[1], p[2]));
                }
            }
        }
        for part in &chosen {
            for t in &part.uvs {
                text.push_str(&format!("vt {:.5} {:.5}\n", t[0], t[1]));
            }
        }
        for part in &chosen {
            for n in &part.normals {
                text.push_str(&format!("vn {:.4} {:.4} {:.4}\n", n[0], n[1], n[2]));
            }
        }
        let mut base = 0u32;
        for part in &chosen {
            text.push_str(&format!(
                "usemtl {}\n",
                part.name.replace(char::is_whitespace, "_")
            ));
            for tri in part.indices.chunks_exact(3) {
                let i = |k: usize| tri[k] + base + 1;
                text.push_str(&format!(
                    "f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n",
                    a = i(0),
                    b = i(1),
                    c = i(2)
                ));
            }
            base += part.positions.len() as u32;
        }
        text
    }

    /// Bounds of the whole model.
    pub fn bounds(&self) -> (V3, V3) {
        let mut lo = [f32::MAX; 3];
        let mut hi = [f32::MIN; 3];
        for part in self.parts.iter().filter(|p| !p.is_empty()) {
            let (a, b) = part.bounds();
            for k in 0..3 {
                lo[k] = lo[k].min(a[k]);
                hi[k] = hi[k].max(b[k]);
            }
        }
        (lo, hi)
    }

    /// Drops parts without triangles.
    pub fn retain_non_empty(&mut self) {
        self.parts.retain(|p| !p.is_empty());
    }
}

/// Orthonormal frame `(u, v)` perpendicular to `axis`.
pub fn frame_around(axis: V3) -> (V3, V3) {
    let helper = if axis[1].abs() < 0.9 {
        [0.0, 1.0, 0.0]
    } else {
        [1.0, 0.0, 0.0]
    };
    let u = normalize(cross(axis, helper));
    (u, cross(axis, u))
}

// ----------------------------------------------------------------------- noise

fn hash3(x: i32, y: i32, z: i32, seed: u32) -> f32 {
    let mut h = seed.wrapping_mul(0x9E37_79B1)
        ^ (x as u32).wrapping_mul(0x85EB_CA6B)
        ^ (y as u32).wrapping_mul(0xC2B2_AE35)
        ^ (z as u32).wrapping_mul(0x27D4_EB2F);
    h ^= h >> 15;
    h = h.wrapping_mul(0x2C1B_3C6D);
    h ^= h >> 12;
    h = h.wrapping_mul(0x297A_2D39);
    h ^= h >> 15;
    (h >> 8) as f32 / 16_777_215.0
}

/// Smooth 3D value noise in 0..1.
pub fn noise3(p: V3, seed: u32) -> f32 {
    let (x0, y0, z0) = (p[0].floor(), p[1].floor(), p[2].floor());
    let (fx, fy, fz) = (p[0] - x0, p[1] - y0, p[2] - z0);
    let smooth = |t: f32| t * t * (3.0 - 2.0 * t);
    let (sx, sy, sz) = (smooth(fx), smooth(fy), smooth(fz));
    let (ix, iy, iz) = (x0 as i32, y0 as i32, z0 as i32);
    let corner = |dx: i32, dy: i32, dz: i32| hash3(ix + dx, iy + dy, iz + dz, seed);
    let lerp1 = |a: f32, b: f32, t: f32| a + (b - a) * t;
    let x00 = lerp1(corner(0, 0, 0), corner(1, 0, 0), sx);
    let x10 = lerp1(corner(0, 1, 0), corner(1, 1, 0), sx);
    let x01 = lerp1(corner(0, 0, 1), corner(1, 0, 1), sx);
    let x11 = lerp1(corner(0, 1, 1), corner(1, 1, 1), sx);
    lerp1(lerp1(x00, x10, sy), lerp1(x01, x11, sy), sz)
}

/// Fractal 3D noise in 0..1.
pub fn fbm3(p: V3, seed: u32, octaves: u32) -> f32 {
    let (mut total, mut amplitude, mut norm, mut q) = (0.0, 1.0, 0.0, p);
    for octave in 0..octaves.max(1) {
        total += noise3(q, seed.wrapping_add(octave * 101)) * amplitude;
        norm += amplitude;
        amplitude *= 0.5;
        q = mul(q, 2.03);
    }
    total / norm
}

/// A small deterministic RNG (no globals: the seed alone decides the model).
#[derive(Debug, Clone)]
pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u32) -> Self {
        Rng((seed as u64).wrapping_mul(0x9E37_79B9_7F4A_7C15) ^ 0xA076_1D64_78BD_642F)
    }

    #[allow(clippy::should_implement_trait)]
    pub fn next(&mut self) -> f32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        ((self.0 >> 40) as f32) / 16_777_216.0
    }

    pub fn range(&mut self, lo: f32, hi: f32) -> f32 {
        lo + (hi - lo) * self.next()
    }

    /// A random unit vector.
    pub fn direction(&mut self) -> V3 {
        let z = self.range(-1.0, 1.0);
        let a = self.range(0.0, std::f32::consts::TAU);
        let r = (1.0 - z * z).sqrt();
        [r * a.cos(), z, r * a.sin()]
    }
}

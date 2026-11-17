//! Mesh maps: surface data baked from a [`Part`] into **UV space**.
//!
//! A procedural surface needs to know *where it is* on the model: moss grows in crevices and on top,
//! edges wear and lighten, cracks follow the form. This module rasterises, per texel of the part's UV
//! layout, the **position**, the smooth **normal**, the **curvature** (convex > 0), an ambient-occlusion
//! estimate and the normalised **height**. Surfaces ([`super::surface`]) then evaluate 3D noise at the
//! baked positions, so patterns are continuous across UV seams.
//!
//! Everything is CPU and deterministic. Ambient occlusion uses disk occluders from a spatial hash
//! (no ray casting), which is cheap enough for a few thousand triangles at 1-2K textures.

use super::mesh::*;
use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct MeshMaps {
    pub res: usize,
    /// Texels covered by a triangle (or filled from a neighbour by dilation).
    pub covered: Vec<bool>,
    pub position: Vec<V3>,
    pub normal: Vec<V3>,
    /// Signed curvature scaled by the model size: > 0 convex (edges), < 0 concave (crevices); -1..1.
    pub curvature: Vec<f32>,
    /// 1 = open, 0 = fully occluded.
    pub ao: Vec<f32>,
    /// 0 at the bottom of the model, 1 at the top.
    pub height: Vec<f32>,
    /// Size of the model's bounding box diagonal in metres.
    pub size: f32,
}

#[derive(Clone, Copy, Default)]
struct VertexData {
    curvature: f32,
    ao: f32,
}

/// Welds vertices by position and returns `(welded index per vertex, welded positions)`.
fn weld_index(part: &Part, quantum: f32) -> (Vec<usize>, Vec<V3>) {
    let mut table: HashMap<(i64, i64, i64), usize> = HashMap::new();
    let mut positions = Vec::new();
    let map = part
        .positions
        .iter()
        .map(|p| {
            let key = (
                (p[0] / quantum).round() as i64,
                (p[1] / quantum).round() as i64,
                (p[2] / quantum).round() as i64,
            );
            *table.entry(key).or_insert_with(|| {
                positions.push(*p);
                positions.len() - 1
            })
        })
        .collect();
    (map, positions)
}

fn per_vertex(part: &Part, size: f32) -> Vec<VertexData> {
    let (map, welded) = weld_index(part, (size * 1e-4).max(1e-6));
    let tris: Vec<[usize; 3]> = part
        .indices
        .chunks_exact(3)
        .map(|t| [t[0] as usize, t[1] as usize, t[2] as usize])
        .collect();
    // Face normals, areas and centroids; smooth normals per welded vertex.
    let mut wn = vec![[0.0f32; 3]; welded.len()];
    let mut occluders = Vec::with_capacity(tris.len());
    for t in &tris {
        let (a, b, c) = (
            part.positions[t[0]],
            part.positions[t[1]],
            part.positions[t[2]],
        );
        let n = cross(sub(b, a), sub(c, a));
        let area = 0.5 * length(n);
        if area < 1e-12 {
            continue;
        }
        let fn_ = normalize(n);
        for v in t {
            let w = map[*v];
            wn[w] = add(wn[w], n);
        }
        let centre = mul(add(add(a, b), c), 1.0 / 3.0);
        occluders.push((centre, fn_, area));
    }
    for n in &mut wn {
        *n = normalize(*n);
    }
    // Curvature from the normal change along every edge.
    let mut sum = vec![0.0f32; welded.len()];
    let mut count = vec![0.0f32; welded.len()];
    for t in &tris {
        for k in 0..3 {
            let (wa, wb) = (map[t[k]], map[t[(k + 1) % 3]]);
            if wa == wb {
                continue;
            }
            let d = sub(welded[wb], welded[wa]);
            let l2 = dot(d, d);
            if l2 < 1e-14 {
                continue;
            }
            let k_ = dot(sub(wn[wb], wn[wa]), d) / l2;
            for w in [wa, wb] {
                sum[w] += k_;
                count[w] += 1.0;
            }
        }
    }
    // Neighbours along edges, to smooth the per-vertex noise of a lumpy mesh.
    let mut neighbours: Vec<Vec<usize>> = vec![Vec::new(); welded.len()];
    for t in &tris {
        for k in 0..3 {
            let (wa, wb) = (map[t[k]], map[t[(k + 1) % 3]]);
            if wa != wb {
                neighbours[wa].push(wb);
                neighbours[wb].push(wa);
            }
        }
    }
    let mut curvature: Vec<f32> = (0..welded.len())
        .map(|w| {
            if count[w] > 0.0 {
                sum[w] / count[w]
            } else {
                0.0
            }
        })
        .collect();
    for _ in 0..4 {
        let previous = curvature.clone();
        for w in 0..welded.len() {
            if neighbours[w].is_empty() {
                continue;
            }
            let around: f32 = neighbours[w].iter().map(|n| previous[*n]).sum::<f32>()
                / neighbours[w].len() as f32;
            curvature[w] = previous[w] * 0.4 + around * 0.6;
        }
    }
    // Spatial hash of the occluders.
    let radius = (size * 0.22).max(1e-4);
    let cell = |p: V3| {
        (
            (p[0] / radius).floor() as i64,
            (p[1] / radius).floor() as i64,
            (p[2] / radius).floor() as i64,
        )
    };
    let mut grid: HashMap<(i64, i64, i64), Vec<usize>> = HashMap::new();
    for (i, o) in occluders.iter().enumerate() {
        grid.entry(cell(o.0)).or_default().push(i);
    }
    let welded_data: Vec<VertexData> = welded
        .iter()
        .enumerate()
        .map(|(w, p)| {
            let n = wn[w];
            let mut occlusion = 0.0f32;
            let (cx, cy, cz) = cell(*p);
            for dx in -1..=1 {
                for dy in -1..=1 {
                    for dz in -1..=1 {
                        let Some(list) = grid.get(&(cx + dx, cy + dy, cz + dz)) else {
                            continue;
                        };
                        for &i in list {
                            let (centre, fnorm, area) = occluders[i];
                            let d = sub(centre, *p);
                            let len = length(d);
                            if !(1e-5..=radius).contains(&len) {
                                continue;
                            }
                            let towards = dot(n, d) / len;
                            let facing = dot(fnorm, mul(d, -1.0 / len));
                            if towards > 0.02 && facing > 0.0 {
                                let falloff = 1.0 - len / radius;
                                occlusion += area * towards * facing
                                    / (std::f32::consts::PI * len * len + area)
                                    * falloff;
                            }
                        }
                    }
                }
            }
            let k = curvature[w];
            VertexData {
                curvature: (k * size * 0.07).clamp(-1.0, 1.0),
                ao: (1.0 - occlusion * 4.5).clamp(0.0, 1.0),
            }
        })
        .collect();
    map.iter().map(|w| welded_data[*w]).collect()
}

/// Bakes the maps of `part` at `res` x `res`.
pub fn bake(part: &Part, res: usize) -> MeshMaps {
    let (lo, hi) = part.bounds();
    let size = length(sub(hi, lo)).max(1e-4);
    let data = per_vertex(part, size);
    let n = res * res;
    let mut maps = MeshMaps {
        res,
        covered: vec![false; n],
        position: vec![[0.0; 3]; n],
        normal: vec![[0.0, 1.0, 0.0]; n],
        curvature: vec![0.0; n],
        ao: vec![1.0; n],
        height: vec![0.5; n],
        size,
    };
    let span_y = (hi[1] - lo[1]).max(1e-4);
    // The engine samples row = v * height (V is not flipped), so texel rows follow +v.
    let to_px = |uv: [f32; 2]| [uv[0] * res as f32, uv[1] * res as f32];
    for t in part.indices.chunks_exact(3) {
        let i = [t[0] as usize, t[1] as usize, t[2] as usize];
        let p = [
            to_px(part.uvs[i[0]]),
            to_px(part.uvs[i[1]]),
            to_px(part.uvs[i[2]]),
        ];
        let denom =
            (p[1][1] - p[2][1]) * (p[0][0] - p[2][0]) + (p[2][0] - p[1][0]) * (p[0][1] - p[2][1]);
        if denom.abs() < 1e-9 {
            continue;
        }
        let min_x = p
            .iter()
            .map(|q| q[0])
            .fold(f32::MAX, f32::min)
            .floor()
            .max(0.0) as usize;
        let max_x = (p.iter().map(|q| q[0]).fold(f32::MIN, f32::max).ceil() as usize).min(res - 1);
        let min_y = p
            .iter()
            .map(|q| q[1])
            .fold(f32::MAX, f32::min)
            .floor()
            .max(0.0) as usize;
        let max_y = (p.iter().map(|q| q[1]).fold(f32::MIN, f32::max).ceil() as usize).min(res - 1);
        for y in min_y..=max_y {
            for x in min_x..=max_x {
                let (px, py) = (x as f32 + 0.5, y as f32 + 0.5);
                let w0 = ((p[1][1] - p[2][1]) * (px - p[2][0])
                    + (p[2][0] - p[1][0]) * (py - p[2][1]))
                    / denom;
                let w1 = ((p[2][1] - p[0][1]) * (px - p[2][0])
                    + (p[0][0] - p[2][0]) * (py - p[2][1]))
                    / denom;
                let w2 = 1.0 - w0 - w1;
                if w0 < -0.001 || w1 < -0.001 || w2 < -0.001 {
                    continue;
                }
                let o = y * res + x;
                let mix3 = |a: V3, b: V3, c: V3| {
                    [
                        a[0] * w0 + b[0] * w1 + c[0] * w2,
                        a[1] * w0 + b[1] * w1 + c[1] * w2,
                        a[2] * w0 + b[2] * w1 + c[2] * w2,
                    ]
                };
                let position = mix3(
                    part.positions[i[0]],
                    part.positions[i[1]],
                    part.positions[i[2]],
                );
                maps.covered[o] = true;
                maps.position[o] = position;
                maps.normal[o] = normalize(mix3(
                    part.normals[i[0]],
                    part.normals[i[1]],
                    part.normals[i[2]],
                ));
                maps.curvature[o] = data[i[0]].curvature * w0
                    + data[i[1]].curvature * w1
                    + data[i[2]].curvature * w2;
                maps.ao[o] =
                    (data[i[0]].ao * w0 + data[i[1]].ao * w1 + data[i[2]].ao * w2).clamp(0.0, 1.0);
                maps.height[o] = ((position[1] - lo[1]) / span_y).clamp(0.0, 1.0);
            }
        }
    }
    dilate(&mut maps, 4);
    maps
}

/// Copies covered texels into their uncovered neighbours so filtering and mips do not bleed.
fn dilate(maps: &mut MeshMaps, passes: usize) {
    let res = maps.res;
    for _ in 0..passes {
        let covered = maps.covered.clone();
        for y in 0..res {
            for x in 0..res {
                let o = y * res + x;
                if covered[o] {
                    continue;
                }
                let mut source = None;
                'search: for dy in -1i32..=1 {
                    for dx in -1i32..=1 {
                        let (nx, ny) = (x as i32 + dx, y as i32 + dy);
                        if nx < 0 || ny < 0 || nx >= res as i32 || ny >= res as i32 {
                            continue;
                        }
                        let q = ny as usize * res + nx as usize;
                        if covered[q] {
                            source = Some(q);
                            break 'search;
                        }
                    }
                }
                if let Some(q) = source {
                    maps.covered[o] = true;
                    maps.position[o] = maps.position[q];
                    maps.normal[o] = maps.normal[q];
                    maps.curvature[o] = maps.curvature[q];
                    maps.ao[o] = maps.ao[q];
                    maps.height[o] = maps.height[q];
                }
            }
        }
    }
}

/// A grey-scale PNG-ready plane (0..255) of one map, for export and debugging.
pub fn plane_to_u8(values: &[f32], lo: f32, hi: f32) -> Vec<u8> {
    values
        .iter()
        .map(|v| (((v - lo) / (hi - lo)).clamp(0.0, 1.0) * 255.0) as u8)
        .collect()
}

/// Re-unwraps a part into a **non-overlapping atlas**: triangles are grouped into six charts by
/// their dominant axis, each chart is planar-projected and fitted (aspect kept) into its own cell of
/// a 2 x 3 grid. Baked maps need unique UVs; repeating box-projection UVs would overwrite each other.
pub fn atlas_unwrap(part: &Part) -> Part {
    struct Corner {
        position: V3,
        normal: V3,
        plane: [f32; 2],
    }
    let (lo_b, hi_b) = part.bounds();
    let centre = mul(add(lo_b, hi_b), 0.5);
    let mut charts: Vec<Vec<[Corner; 3]>> = (0..6).map(|_| Vec::new()).collect();
    for t in part.indices.chunks_exact(3) {
        let i = [t[0] as usize, t[1] as usize, t[2] as usize];
        let (a, b, c) = (
            part.positions[i[0]],
            part.positions[i[1]],
            part.positions[i[2]],
        );
        let face = cross(sub(b, a), sub(c, a));
        if length(face) < 1e-12 {
            continue;
        }
        // The chart follows the direction from the model's centre to the triangle (a cube map), so
        // a lumpy, roughly star-shaped rock gets six coherent islands instead of speckles.
        let centroid = mul(add(add(a, b), c), 1.0 / 3.0);
        let n = sub(centroid, centre);
        let _ = face;
        let axis = (0..3)
            .max_by(|x, y| n[*x].abs().total_cmp(&n[*y].abs()))
            .unwrap_or(1);
        let negative = n[axis] < 0.0;
        let flat = |p: V3| -> [f32; 2] {
            let (u, v) = match axis {
                0 => (p[2], p[1]),
                1 => (p[0], p[2]),
                _ => (p[0], p[1]),
            };
            [if negative { -u } else { u }, v]
        };
        let corner = |k: usize| Corner {
            position: part.positions[i[k]],
            normal: part.normals[i[k]],
            plane: flat(part.positions[i[k]]),
        };
        charts[axis * 2 + usize::from(negative)].push([corner(0), corner(1), corner(2)]);
    }
    let mut out = Part::new(&part.name, part.material.clone(), part.preview);
    out.surface = part.surface.clone();
    let pad = 0.015;
    for (index, chart) in charts.iter().enumerate() {
        if chart.is_empty() {
            continue;
        }
        let (mut lo, mut hi) = ([f32::MAX; 2], [f32::MIN; 2]);
        for tri in chart {
            for c in tri {
                for k in 0..2 {
                    lo[k] = lo[k].min(c.plane[k]);
                    hi[k] = hi[k].max(c.plane[k]);
                }
            }
        }
        let (cell_w, cell_h) = (0.5, 1.0 / 3.0);
        let origin = [(index % 2) as f32 * cell_w, (index / 2) as f32 * cell_h];
        let scale = ((cell_w - 2.0 * pad) / (hi[0] - lo[0]).max(1e-6))
            .min((cell_h - 2.0 * pad) / (hi[1] - lo[1]).max(1e-6));
        for tri in chart {
            let mut ids = [0u32; 3];
            for (k, c) in tri.iter().enumerate() {
                let uv = [
                    origin[0] + pad + (c.plane[0] - lo[0]) * scale,
                    origin[1] + pad + (c.plane[1] - lo[1]) * scale,
                ];
                ids[k] = out.vertex(c.position, c.normal, uv);
            }
            out.triangle(ids[0], ids[1], ids[2]);
        }
    }
    out
}

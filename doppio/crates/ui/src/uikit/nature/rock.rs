//! Rock helpers for the node graph: the icosphere base and box-projected UVs. The rock itself is a
//! graph (Icosphere, Noise Displace, Cut Planes, Flat Base, Shade, UV Box, Ground...).

use super::mesh::*;
use std::collections::HashMap;

/// Unit icosphere: positions on the sphere and triangles.
pub fn icosphere(detail: u32) -> (Vec<V3>, Vec<[u32; 3]>) {
    let t = (1.0 + 5.0f32.sqrt()) / 2.0;
    let mut vertices: Vec<V3> = [
        [-1.0, t, 0.0],
        [1.0, t, 0.0],
        [-1.0, -t, 0.0],
        [1.0, -t, 0.0],
        [0.0, -1.0, t],
        [0.0, 1.0, t],
        [0.0, -1.0, -t],
        [0.0, 1.0, -t],
        [t, 0.0, -1.0],
        [t, 0.0, 1.0],
        [-t, 0.0, -1.0],
        [-t, 0.0, 1.0],
    ]
    .iter()
    .map(|v| normalize(*v))
    .collect();
    let mut faces: Vec<[u32; 3]> = vec![
        [0, 11, 5],
        [0, 5, 1],
        [0, 1, 7],
        [0, 7, 10],
        [0, 10, 11],
        [1, 5, 9],
        [5, 11, 4],
        [11, 10, 2],
        [10, 7, 6],
        [7, 1, 8],
        [3, 9, 4],
        [3, 4, 2],
        [3, 2, 6],
        [3, 6, 8],
        [3, 8, 9],
        [4, 9, 5],
        [2, 4, 11],
        [6, 2, 10],
        [8, 6, 7],
        [9, 8, 1],
    ];
    for _ in 0..detail.min(6) {
        let mut cache: HashMap<(u32, u32), u32> = HashMap::new();
        let mut midpoint = |vertices: &mut Vec<V3>, a: u32, b: u32| -> u32 {
            let key = (a.min(b), a.max(b));
            *cache.entry(key).or_insert_with(|| {
                let m = normalize(lerp(vertices[a as usize], vertices[b as usize], 0.5));
                vertices.push(m);
                (vertices.len() - 1) as u32
            })
        };
        let mut next = Vec::with_capacity(faces.len() * 4);
        for f in &faces {
            let ab = midpoint(&mut vertices, f[0], f[1]);
            let bc = midpoint(&mut vertices, f[1], f[2]);
            let ca = midpoint(&mut vertices, f[2], f[0]);
            next.extend_from_slice(&[[f[0], ab, ca], [f[1], bc, ab], [f[2], ca, bc], [ab, bc, ca]]);
        }
        faces = next;
    }
    (vertices, faces)
}

/// Replaces a welded part by one whose triangles each carry a box-projected UV (the projection axis
/// is the dominant axis of the face normal), so textures never smear. Normals are kept.
pub fn box_uv_unweld(part: &Part, texels_per_metre: f32) -> Part {
    let mut out = Part::new(&part.name, part.material.clone(), part.preview);
    for tri in part.indices.chunks_exact(3) {
        let p = [
            part.positions[tri[0] as usize],
            part.positions[tri[1] as usize],
            part.positions[tri[2] as usize],
        ];
        let n = normalize(cross(sub(p[1], p[0]), sub(p[2], p[0])));
        let axis = if n[0].abs() >= n[1].abs() && n[0].abs() >= n[2].abs() {
            0
        } else if n[1].abs() >= n[2].abs() {
            1
        } else {
            2
        };
        let ids: Vec<u32> = (0..3)
            .map(|k| {
                let q = p[k];
                let uv = match axis {
                    0 => [q[2], q[1]],
                    1 => [q[0], q[2]],
                    _ => [q[0], q[1]],
                };
                out.vertex(
                    q,
                    part.normals[tri[k] as usize],
                    [uv[0] * texels_per_metre, uv[1] * texels_per_metre],
                )
            })
            .collect();
        out.triangle(ids[0], ids[1], ids[2]);
    }
    out
}

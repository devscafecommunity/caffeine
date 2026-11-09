#version 450

// Photon P0 — Min-Z pyramid reduction (spec §5.2 / §7.2).
//
// Mode 0 (pyramid level 0): linearise the scene depth buffer into view-space Z.
//   Background pixels are written as the caller-provided sky sentinel so that
//   hierarchical tracing treats them as "no occluder".
// Mode 1 (levels 1..N-1): exact min over the 2×2 footprint of the previous
//   level. The previous level is staged in a single-mip scratch texture, so the
//   shader never reads and writes the same subresource.
//
// The output is a full mip chain of R32F containing the minimum linear depth
// over each footprint. Sampling it with `textureLod(minZ, uv, level)` gives the
// conservative distance used by the near-field cascade tracer.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform MinZUBO {
    vec4 uProj;      // x P00, y P11, z P22, w P23
    vec4 uProjExtra; // x P32: -1 perspective, 0 orthographic
    vec4 uParams;    // x mode (0 = depth, 1 = reduce), y sky sentinel
} minz;

layout(set = 2, binding = 0) uniform sampler2D uSource;

float linearDepth(float d) {
    // Perspective: d = -P22 - P23/z  ->  z = P23 / (d + P22).
    // Orthographic: d = P22*z + P23 ->  -z = (P23 - d) / P22 (positive view distance).
    if (abs(minz.uProjExtra.x) < 0.5) return (minz.uProj.w - d) / minz.uProj.z;
    return minz.uProj.w / (d + minz.uProj.z);
}

void main() {
    if (minz.uParams.x < 0.5) {
        float raw = textureLod(uSource, v_uv, 0.0).r;
        float z = raw >= 0.999999 ? minz.uParams.y : max(linearDepth(raw), 0.0);
        outColor = vec4(z, 0.0, 0.0, 1.0);
        return;
    }

    // Destination texel centre covers a 2×2 block of source texels; sampling at
    // ±half a source texel hits their centres exactly (nearest filtering).
    vec2 texel = 1.0 / vec2(textureSize(uSource, 0));
    vec2 o = texel * 0.5;
    float a = textureLod(uSource, v_uv + vec2(-o.x, -o.y), 0.0).r;
    float b = textureLod(uSource, v_uv + vec2( o.x, -o.y), 0.0).r;
    float c = textureLod(uSource, v_uv + vec2(-o.x,  o.y), 0.0).r;
    float d = textureLod(uSource, v_uv + vec2( o.x,  o.y), 0.0).r;
    outColor = vec4(min(min(a, b), min(c, d)), 0.0, 0.0, 1.0);
}

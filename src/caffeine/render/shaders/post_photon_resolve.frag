#version 450

// Photon P2 — diffuse resolve for cascade 0 (spec §10).
//
// Samples the 4 nearest valid C0 probes, evaluates their SH-L1 irradiance
// along the (world-space) shading normal, and multiplies by the G-buffer
// albedo. Falls back to the sky when no probe is valid.
//
// This is a fragment pass so it can reuse the existing post-process plumbing;
// the heavy tracing lives in photon_c0.comp.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;
// P5: S(t) = indirect irradiance / π (RGB) and validity (A).
layout(location = 1) out vec4 outS;

layout(set = 3, binding = 0) uniform PhotonResolveUBO {
    vec4 uProj;    // x P00, y P11, z P22, w P23
    vec4 uExtra;   // x P32, y s0, z grid width, w grid height
    vec4 uTexel;   // xy 1 / scene size, z frame index, w enabled
    vec4 uParams;  // x intensity, y sky intensity, z tau_p, w gamma_n
    mat4 uView;    // world -> view
    vec4 uSky;     // rgb sky radiance
} pr;

layout(set = 2, binding = 0) uniform sampler2D uScene;
layout(set = 2, binding = 1) uniform sampler2D uDepth;
layout(set = 2, binding = 2) uniform sampler2D uGBufferAlbedo;
layout(set = 2, binding = 3) uniform sampler2D uGBufferNormal;  // rg of normal+material
layout(set = 2, binding = 4) uniform sampler2D uProbeSH;

const float kY00 = 0.282095;
const float kY1 = 0.488603;

bool isOrthographic() { return abs(pr.uExtra.x) < 0.5; }

float linearDepth(float d) {
    if (isOrthographic()) return (pr.uProj.w - d) / pr.uProj.z;
    return pr.uProj.w / (d + pr.uProj.z);
}

float pixelFootprint(float z) {
    float scale = isOrthographic() ? 1.0 : z;
    return 2.0 * scale * pr.uTexel.y / pr.uProj.y;
}

vec3 viewPosition(vec2 uv, float z) {
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float scale = isOrthographic() ? 1.0 : z;
    return vec3(ndc.x / pr.uProj.x * scale, ndc.y / pr.uProj.y * scale, -z);
}

vec3 octDecode(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

vec4 shTexel(ivec2 probeBase, ivec2 offset, vec2 atlasSize) {
    vec2 uv = (vec2(probeBase + offset) + 0.5) / atlasSize;
    return textureLod(uProbeSH, uv, 0.0);
}

void main() {
    vec3 scene = textureLod(uScene, v_uv, 0.0).rgb;
    float rawDepth = textureLod(uDepth, v_uv, 0.0).r;
    vec4 albedoSample = textureLod(uGBufferAlbedo, v_uv, 0.0);
    if (pr.uTexel.w < 0.5 || rawDepth >= 0.999999 || albedoSample.a < 0.5) {
        outColor = vec4(scene, 1.0);
        outS = vec4(0.0);
        return;
    }

    const vec2 size = vec2(1.0 / pr.uTexel.x, 1.0 / pr.uTexel.y);
    const float s0 = pr.uExtra.y;
    const ivec2 grid = ivec2(pr.uExtra.z, pr.uExtra.w);
    if (grid.x <= 0 || grid.y <= 0) {
        outColor = vec4(scene + albedoSample.rgb * pr.uSky.rgb * pr.uParams.x, 1.0);
        outS = vec4(pr.uSky.rgb * pr.uParams.x, 1.0);
        return;
    }

    float z = linearDepth(rawDepth);
    vec3 P = viewPosition(v_uv, z);
    vec3 Nw = octDecode(textureLod(uGBufferNormal, v_uv, 0.0).rg);
    vec3 Nv = normalize(mat3(pr.uView) * Nw);
    if (dot(Nv, -P) < 0.0) Nv = -Nv;

    // Spec §10: probe-grid coordinate of this pixel.
    vec2 g = v_uv * size / s0 - 0.5;
    ivec2 baseProbe = ivec2(floor(g));
    vec2 f = g - vec2(baseProbe);

    const float delta0 = max(s0 * pixelFootprint(z), 1e-4);
    const float tau = max(pr.uParams.z, 1e-3);
    const float gamma = max(pr.uParams.w, 1.0);
    const vec2 atlasSize = vec2(grid * 2);
    const vec3 y1 = kY1 * Nw;

    vec3 accum = vec3(0.0);
    float weightSum = 0.0;

    for (int oy = 0; oy < 2; ++oy) {
        for (int ox = 0; ox < 2; ++ox) {
            ivec2 q = clamp(baseProbe + ivec2(ox, oy), ivec2(0), grid - 1);
            // Spec §6.3 anchor pixel, converted to a texture coordinate.
            vec2 anchor = vec2(q) * s0 + s0 * 0.5;
            if (anchor.x < 0.0 || anchor.y < 0.0 || anchor.x >= size.x ||
                anchor.y >= size.y) {
                continue;
            }
            vec2 auv = (anchor + 0.5) / size;

            float dq = textureLod(uDepth, auv, 0.0).r;
            vec4 aq = textureLod(uGBufferAlbedo, auv, 0.0);
            if (dq >= 0.999999 || aq.a < 0.5) continue;

            vec3 Pq = viewPosition(auv, linearDepth(dq));
            vec3 Nq = octDecode(textureLod(uGBufferNormal, auv, 0.0).rg);

            float bilinear = (ox == 0 ? 1.0 - f.x : f.x) * (oy == 0 ? 1.0 - f.y : f.y);
            float plane = abs(dot(Pq - P, Nv)) / (tau * delta0);
            float wZ = exp(-plane * plane);
            float wN = pow(max(dot(Nw, Nq), 0.0), gamma);
            float w = bilinear * wZ * wN;
            if (w <= 0.0) continue;

            ivec2 probeBase = q * 2;
            vec4 t00 = shTexel(probeBase, ivec2(0, 0), atlasSize);
            vec4 t10 = shTexel(probeBase, ivec2(1, 0), atlasSize);
            vec4 t01 = shTexel(probeBase, ivec2(0, 1), atlasSize);
            vec4 t11 = shTexel(probeBase, ivec2(1, 1), atlasSize);

            // S = c00·Y00 + (2/3)·Σ c1m·Y1m(n)  (irradiance / π)
            vec3 irradiance = t00.rgb * kY00 +
                              (2.0 / 3.0) * (t10.rgb * y1.x + t01.rgb * y1.y +
                                             t11.rgb * y1.z);
            accum += w * max(irradiance, vec3(0.0));
            weightSum += w;
        }
    }

    // Invariant 1: an empty scene under a uniform sky resolves to that sky
    // radiance (S = L_s), independent of the normal.
    vec3 sun = weightSum > 1e-3 ? accum / weightSum : pr.uSky.rgb;
    vec3 indirect = max(albedoSample.rgb, vec3(0.0)) * sun * pr.uParams.x;
    outColor = vec4(scene + max(indirect, vec3(0.0)), 1.0);
    outS = vec4(max(sun, vec3(0.0)) * pr.uParams.x, 1.0);
}

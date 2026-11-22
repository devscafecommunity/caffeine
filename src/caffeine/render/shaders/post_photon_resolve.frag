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
    vec4 uSky;     // rgb sky radiance, a debug view (0 lit, 1 GI only, 2 sky fallback only)
    mat4 uInvView;      // view -> world
    mat4 uPrevViewProj; // world -> previous frame's clip space (unjittered)
    vec4 uHistory;      // x 1 when S(t-1) can be used, y weight of the current frame
} pr;

layout(set = 2, binding = 0) uniform sampler2D uScene;
layout(set = 2, binding = 1) uniform sampler2D uDepth;
layout(set = 2, binding = 2) uniform sampler2D uGBufferAlbedo;
layout(set = 2, binding = 3) uniform sampler2D uGBufferNormal;  // rg of normal+material
layout(set = 2, binding = 4) uniform sampler2D uProbeSH;
layout(set = 2, binding = 5) uniform sampler2D uSPrev;  // S(t-1) = indirect irradiance / pi, validity in alpha

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
    // The range of the probes' own values: the history may not leave it by much (disocclusion, a moved light).
    vec3 lo = vec3(1e9);
    vec3 hi = vec3(0.0);

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
            irradiance = max(irradiance, vec3(0.0));
            accum += w * irradiance;
            weightSum += w;
            lo = min(lo, irradiance);
            hi = max(hi, irradiance);
        }
    }

    // Invariant 1: an empty scene under a uniform sky resolves to that sky
    // radiance (S = L_s), independent of the normal.
    vec3 sun = weightSum > 1e-3 ? accum / weightSum : pr.uSky.rgb;
    sun = max(sun, vec3(0.0)) * pr.uParams.x;

    // Temporal accumulation. The cascades trace a different set of directions every frame, so a single frame's
    // irradiance flickers by tens of percent on a small surface. The previous frame's S, reprojected to this
    // pixel, is blended in (exponential average) and kept within a window around the probes' values, so what
    // the history gets wrong (a surface that was hidden, a light that moved) cannot linger.
    if (pr.uHistory.x > 0.5 && weightSum > 1e-3) {
        vec3 world = (pr.uInvView * vec4(P, 1.0)).xyz;
        vec4 clip = pr.uPrevViewProj * vec4(world, 1.0);
        if (clip.w > 1e-4) {
            vec2 prevUv = vec2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
            if (all(greaterThanEqual(prevUv, vec2(0.0))) && all(lessThanEqual(prevUv, vec2(1.0)))) {
                vec4 history = textureLod(uSPrev, prevUv, 0.0);
                if (history.a > 0.5) {
                    // The probes' values flicker together (every probe traces the same new directions), so the
                    // window must be wider than that flicker: a history within 2.5x of the probes is kept.
                    vec3 low = max(lo * pr.uParams.x * 0.4 - 0.02, vec3(0.0));
                    vec3 high = hi * pr.uParams.x * 2.5 + 0.02;
                    vec3 kept = clamp(history.rgb, low, high);
                    // Noise stays within a few tens of percent of the average; a change larger than that (a light that
                    // moved, a leak that closed, a surface the history had wrong) is followed quickly.
                    float lc = dot(sun, vec3(1.0 / 3.0));
                    float lh = dot(kept, vec3(1.0 / 3.0));
                    float change = abs(lc - lh) / (max(lc, lh) + 0.02);
                    float weight = mix(clamp(pr.uHistory.y, 0.02, 1.0), 0.5, smoothstep(0.35, 0.7, change));
                    sun = mix(kept, sun, max(weight, clamp(pr.uHistory.y, 0.02, 1.0)));
                }
            }
        }
    }
    vec3 indirect = max(albedoSample.rgb, vec3(0.0)) * sun;
    int debugView = int(pr.uSky.a + 0.5);
    if (debugView == 1) {
        outColor = vec4(max(indirect, vec3(0.0)), 1.0);        // the GI term on its own
    } else if (debugView == 2) {
        outColor = vec4(vec3(weightSum > 1e-3 ? 0.0 : 1.0), 1.0);  // white where only the sky stood in
    } else {
        outColor = vec4(scene + max(indirect, vec3(0.0)), 1.0);
    }
    outS = vec4(max(sun, vec3(0.0)), 1.0);
}

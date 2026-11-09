#version 450

// Photon v0.1 — screen-space fallback for GPUs without the compute cascade path.
// Near field: K exponentially growing intervals traced against the depth buffer.
// Albedo and world-space normals come from the G-buffer, so indirect light
// carries the real surface colour. Far field: sky/ambient fallback until the
// voxel clipmap + probe field land (P4).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform PhotonUBO {
    vec4 uProj;      // x P00, y P11, z P22, w P23
    vec4 uProjExtra; // x P32: -1 perspective, 0 orthographic; y: G-buffer valid
    vec4 uTexel;     // xy scene texel, z frame index, w enabled
    vec4 uParams0;   // x intensity, y radius, z thickness, w sky intensity
    vec4 uParams1;   // x ambient intensity, y cascades, z steps per interval, w rays
    vec4 uTint;      // rgb albedo fallback, a debug view
    mat4 uView;      // world -> view; rotates G-buffer normals into tracing space
} photon;

layout(set = 2, binding = 0) uniform sampler2D uScene;
layout(set = 2, binding = 1) uniform sampler2D uDepth;
layout(set = 2, binding = 2) uniform sampler2D uGBufferAlbedo;  // rgb albedo, a surface flag
layout(set = 2, binding = 3) uniform sampler2D uGBufferNormal;  // rg of normal+material  // rg octahedral world normal (normal+material)

vec3 octDecode(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

bool isOrthographic() {
    return abs(photon.uProjExtra.x) < 0.5;
}

float linearDepth(float d) {
    // Perspective: d = -P22 - P23/z, so z = -P23/(d + P22).
    // Orthographic: d = P22*z + P23, so z = (d - P23)/P22.
    if (isOrthographic()) return (photon.uProj.w - d) / photon.uProj.z;
    return photon.uProj.w / (d + photon.uProj.z);
}

float pixelFootprint(float z) {
    // World size of one pixel: 2*z*tan(fovy/2)/height, with tan = 1/P11.
    // Orthographic projection has no z dependence.
    float scale = isOrthographic() ? 1.0 : z;
    return 2.0 * scale * photon.uTexel.y / photon.uProj.y;
}

vec3 viewPosition(vec2 uv) {
    float d = textureLod(uDepth, uv, 0.0).r;
    float z = linearDepth(d);
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float scale = isOrthographic() ? 1.0 : z;
    return vec3(ndc.x / photon.uProj.x * scale, ndc.y / photon.uProj.y * scale, -z);
}

vec3 reconstructNormal(vec3 center) {
    vec2 tx = vec2(photon.uTexel.x, 0.0);
    vec2 ty = vec2(0.0, photon.uTexel.y);
    vec3 l = viewPosition(v_uv - tx), r = viewPosition(v_uv + tx);
    vec3 u = viewPosition(v_uv - ty), d = viewPosition(v_uv + ty);
    vec3 dx = abs(l.z - center.z) < abs(r.z - center.z) ? center - l : r - center;
    vec3 dy = abs(u.z - center.z) < abs(d.z - center.z) ? center - u : d - center;
    return normalize(cross(dy, dx));
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float interleavedGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec2 projectToUv(vec3 viewPos) {
    float z = -viewPos.z;
    if (z <= 0.0001) return vec2(-1.0);
    float invZ = isOrthographic() ? 1.0 : 1.0 / z;
    vec2 ndc = vec2(viewPos.x * photon.uProj.x * invZ,
                    viewPos.y * photon.uProj.y * invZ);
    return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

vec3 sampleSky() {
    return vec3(0.48, 0.60, 0.82) * photon.uParams0.w;
}

void main() {
    vec3 scene = textureLod(uScene, v_uv, 0.0).rgb;
    float rawDepth = textureLod(uDepth, v_uv, 0.0).r;
    if (photon.uTexel.w < 0.5 || rawDepth >= 0.99999) {
        outColor = vec4(scene, 1.0);
        return;
    }

    // Per-pixel albedo/normal from the G-buffer. alpha == 0 marks sky, grid and
    // effect pixels, which must keep the constant tint fallback (or be skipped).
    vec4 albedoSample = textureLod(uGBufferAlbedo, v_uv, 0.0);
    bool hasSurface = photon.uProjExtra.y > 0.5 && albedoSample.a > 0.5;
    vec3 albedo = hasSurface ? max(albedoSample.rgb, vec3(0.0)) : photon.uTint.rgb;

    vec3 P = viewPosition(v_uv);
    vec3 N;
    if (hasSurface) {
        vec3 worldNormal = octDecode(textureLod(uGBufferNormal, v_uv, 0.0).rg);
        N = normalize(mat3(photon.uView) * worldNormal);
    } else {
        N = reconstructNormal(P);
    }
    if (dot(N, -P) < 0.0) N = -N;

    float noise = interleavedGradientNoise(gl_FragCoord.xy + photon.uTexel.z * 5.588238);
    float angleOffset = noise * 6.28318530;

    vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);

    int rays = int(clamp(photon.uParams1.w, 1.0, 16.0));
    int cascades = int(clamp(photon.uParams1.y, 1.0, 6.0));
    int stepsPerInterval = int(clamp(photon.uParams1.z, 1.0, 4.0));
    float radius = max(photon.uParams0.y, 0.1);
    float thickness = max(photon.uParams0.z, 0.005);
    float bias = max(0.02, radius * 0.0015);
    // Offset along the normal, as in Section 7.3 of the spec. Starting the
    // ray at P itself makes grazing rays self-intersect the depth buffer.
    vec3 rayOrigin = P + N * bias;

    // Geometric series: l0 + 4*l0 + ... + 4^(K-1)*l0 = radius.
    float series = (pow(4.0, float(cascades)) - 1.0) / 3.0;
    float l0 = radius / max(series, 1.0);

    vec3 gathered = vec3(0.0);
    int misses = 0;

    for (int ray = 0; ray < 16; ++ray) {
        if (ray >= rays) break;
        float fi = (float(ray) + 0.5) / float(rays);
        float phi = float(ray) * 2.39996323 + angleOffset;
        float cosTheta = sqrt(1.0 - fi);
        float sinTheta = sqrt(fi);
        vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        vec3 dir = normalize(T * h.x + B * h.y + N * h.z);

        float t = 0.0;
        float interval = l0;
        bool hit = false;
        for (int k = 0; k < 6; ++k) {
            if (k >= cascades) break;
            float dt = interval / float(stepsPerInterval);
            for (int s = 0; s < 4; ++s) {
                if (s >= stepsPerInterval) break;
                vec3 S = rayOrigin + dir * t;
                vec2 suv = projectToUv(S);
                if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) {
                    t = radius; // force a miss and stop this interval chain
                    break;
                }
                float sceneZ = linearDepth(textureLod(uDepth, suv, 0.0).r);
                float rayZ = -S.z;
                // Thickness slab from Section 7.6: relative thickness with a
                // floor of two pixel footprints, instead of a fixed 5 cm.
                float theta = max(thickness * sceneZ, 2.0 * pixelFootprint(sceneZ));
                if (sceneZ <= rayZ && (rayZ - sceneZ) <= theta) {
                    // Hit radiance is already outgoing radiance; no ad-hoc
                    // distance attenuation is allowed here (Section 7.6).
                    gathered += textureLod(uScene, suv, 0.0).rgb;
                    hit = true;
                    break;
                }
                t += dt;
            }
            if (hit || t >= radius) break;
            interval *= 4.0;
        }
        if (!hit) {
            ++misses;
            gathered += sampleSky();
        }
    }

    float invRays = 1.0 / float(rays);
    float missFraction = float(misses) * invRays;
    // Per-pixel albedo from the G-buffer; textured surfaces now bleed their own
    // colour instead of a constant tint.
    vec3 gi = gathered * invRays * albedo * photon.uParams0.x;
    // The ambient fallback is incident light too: it must be multiplied by the
    // surface albedo, otherwise coloured surfaces get white GI.
    gi += vec3(photon.uParams1.x) * missFraction * photon.uParams0.x * albedo;

    int debugView = int(photon.uTint.a + 0.5);
    if (debugView == 1) {
        outColor = vec4(gi, 1.0);
    } else if (debugView == 2) {
        outColor = vec4(vec3(float(misses) * invRays), 1.0);
    } else {
        outColor = vec4(scene + max(gi, vec3(0.0)), 1.0);
    }
}

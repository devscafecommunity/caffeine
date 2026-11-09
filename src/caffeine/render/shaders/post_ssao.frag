#version 450

// Screen-space ambient occlusion from the depth buffer only (normals are reconstructed).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform SsaoUBO {
    vec4 uProj;    // x P00, y P11, z P22, w P23
    vec4 uTexel;   // xy depth texel size, z frame index
    vec4 uParams;  // x radius (m), y bias, z intensity, w sample count
} ao;

layout(set = 2, binding = 0) uniform sampler2D uDepth;

float linearDepth(float d) {
    return ao.uProj.w / (d + ao.uProj.z);
}

vec3 viewPosition(vec2 uv) {
    float d = textureLod(uDepth, uv, 0.0).r;
    float z = linearDepth(d);
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    return vec3(ndc.x / ao.uProj.x * z, ndc.y / ao.uProj.y * z, -z);
}

vec3 reconstructNormal(vec3 center) {
    vec2 tx = vec2(ao.uTexel.x, 0.0);
    vec2 ty = vec2(0.0, ao.uTexel.y);
    vec3 l = viewPosition(v_uv - tx), r = viewPosition(v_uv + tx);
    vec3 u = viewPosition(v_uv - ty), d = viewPosition(v_uv + ty);
    // Pick the neighbour closer in depth on each axis so silhouettes don't smear normals.
    vec3 dx = abs(l.z - center.z) < abs(r.z - center.z) ? center - l : r - center;
    vec3 dy = abs(u.z - center.z) < abs(d.z - center.z) ? center - u : d - center;
    return normalize(cross(dy, dx));
}

float interleavedGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    float rawDepth = textureLod(uDepth, v_uv, 0.0).r;
    if (rawDepth >= 0.99999) {
        outColor = vec4(1.0);
        return;
    }
    vec3 P = viewPosition(v_uv);
    vec3 N = reconstructNormal(P);
    if (dot(N, -P) < 0.0) N = -N;

    float radius = max(ao.uParams.x, 0.01);
    float bias = ao.uParams.y;
    int samples = int(clamp(ao.uParams.w, 4.0, 32.0));
    vec2 pixel = v_uv / ao.uTexel.xy;
    float noise = interleavedGradientNoise(pixel + ao.uTexel.z * 5.588238);
    float angleOffset = noise * 6.28318530;

    vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);

    float occlusion = 0.0;
    for (int i = 0; i < 32; ++i) {
        if (i >= samples) break;
        float fi = (float(i) + 0.5) / float(samples);
        // Golden-angle spiral over the hemisphere, denser near the centre.
        float phi = float(i) * 2.39996323 + angleOffset;
        float cosTheta = sqrt(1.0 - fi);
        float sinTheta = sqrt(fi);
        vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        float scale = mix(0.15, 1.0, fract(fi * 7.31 + noise));
        vec3 S = P + (T * h.x + B * h.y + N * h.z) * radius * scale;

        float sz = -S.z;
        if (sz <= 0.0) continue;
        vec2 suv = vec2(S.x * ao.uProj.x / sz, S.y * ao.uProj.y / sz);
        suv = vec2(suv.x * 0.5 + 0.5, 1.0 - (suv.y * 0.5 + 0.5));
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float sceneZ = linearDepth(textureLod(uDepth, suv, 0.0).r);
        float range = smoothstep(0.0, 1.0, radius / max(abs(-P.z - sceneZ), 1e-4));
        occlusion += (sceneZ <= sz - bias ? 1.0 : 0.0) * range;
    }
    float visibility = 1.0 - occlusion / float(samples);
    visibility = pow(clamp(visibility, 0.0, 1.0), max(ao.uParams.z, 0.0));
    outColor = vec4(visibility, P.z, 0.0, 1.0);
}

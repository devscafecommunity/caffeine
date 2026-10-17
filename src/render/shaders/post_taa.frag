#version 450

// Temporal anti-aliasing on the HDR scene: depth reprojection + YCoCg neighbourhood clamp.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform TaaUBO {
    mat4 uInvViewProj;
    mat4 uPrevViewProj;
    vec4 uTexel;  // xy texel size
    vec4 uParams; // x history valid, y feedback (history weight), z jitter x (uv), w jitter y (uv)
} taa;

layout(set = 2, binding = 0) uniform sampler2D uCurrent;
layout(set = 2, binding = 1) uniform sampler2D uHistory;
layout(set = 2, binding = 2) uniform sampler2D uDepth;

vec3 toYCoCg(vec3 c) {
    return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
                0.5 * c.r - 0.5 * c.b,
                -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

vec3 fromYCoCg(vec3 c) {
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Tonemapped weights stop bright pixels from dominating the resolve.
vec3 compress(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }
vec3 uncompress(vec3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4); }

void main() {
    vec2 uv = v_uv;
    vec3 current = compress(texture(uCurrent, uv).rgb);
    if (taa.uParams.x < 0.5) {
        outColor = vec4(uncompress(current), 1.0);
        return;
    }

    // Closest depth in 3x3 keeps edges of foreground objects attached to their history.
    float closest = 1.0;
    vec2 closestUv = uv;
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 o = vec2(x, y) * taa.uTexel.xy;
            float d = textureLod(uDepth, uv + o, 0.0).r;
            if (d < closest) {
                closest = d;
                closestUv = uv + o;
            }
            vec3 s = toYCoCg(compress(texture(uCurrent, uv + o).rgb));
            m1 += s;
            m2 += s * s;
        }
    }

    vec2 ndc = vec2(closestUv.x * 2.0 - 1.0, (1.0 - closestUv.y) * 2.0 - 1.0);
    vec4 world = taa.uInvViewProj * vec4(ndc, closest, 1.0);
    world /= world.w;
    vec4 prevClip = taa.uPrevViewProj * world;
    vec2 velocity = vec2(0.0);
    if (prevClip.w > 1e-4) {
        vec2 prevUv = vec2(prevClip.x / prevClip.w * 0.5 + 0.5,
                           1.0 - (prevClip.y / prevClip.w * 0.5 + 0.5));
        velocity = closestUv - prevUv;
    }
    vec2 historyUv = uv - velocity;
    if (historyUv.x < 0.0 || historyUv.x > 1.0 || historyUv.y < 0.0 || historyUv.y > 1.0) {
        outColor = vec4(uncompress(current), 1.0);
        return;
    }

    vec3 history = toYCoCg(compress(texture(uHistory, historyUv).rgb));
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    vec3 lo = mean - sigma * 1.25;
    vec3 hi = mean + sigma * 1.25;
    history = clamp(history, lo, hi);

    float motionPx = length(velocity / taa.uTexel.xy);
    float feedback = mix(taa.uParams.y, 0.75, clamp(motionPx / 8.0, 0.0, 1.0));
    vec3 resolved = mix(toYCoCg(current), history, feedback);
    outColor = vec4(uncompress(fromYCoCg(resolved)), 1.0);
}

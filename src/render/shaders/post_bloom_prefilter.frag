#version 450

// First bloom level: 13-tap downsample of the HDR scene with a soft-knee threshold.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform BloomUBO {
    vec4 uTexel;   // xy source texel size
    vec4 uParams;  // x threshold, y knee, z clamp
} bloom;

layout(set = 2, binding = 0) uniform sampler2D uSource;

vec3 tap(vec2 offset) {
    return min(texture(uSource, v_uv + offset * bloom.uTexel.xy).rgb, vec3(bloom.uParams.z));
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Karis average on the first downsample keeps single bright pixels from flickering.
vec3 karis(vec3 a, vec3 b, vec3 c, vec3 d) {
    float wa = 1.0 / (1.0 + luma(a));
    float wb = 1.0 / (1.0 + luma(b));
    float wc = 1.0 / (1.0 + luma(c));
    float wd = 1.0 / (1.0 + luma(d));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

void main() {
    vec3 a = tap(vec2(-2, -2)), b = tap(vec2(0, -2)), c = tap(vec2(2, -2));
    vec3 d = tap(vec2(-1, -1)), e = tap(vec2(1, -1));
    vec3 f = tap(vec2(-2, 0)), g = tap(vec2(0, 0)), h = tap(vec2(2, 0));
    vec3 i = tap(vec2(-1, 1)), j = tap(vec2(1, 1));
    vec3 k = tap(vec2(-2, 2)), l = tap(vec2(0, 2)), m = tap(vec2(2, 2));

    vec3 color = karis(d, e, i, j) * 0.5
               + karis(a, b, f, g) * 0.125 + karis(b, c, g, h) * 0.125
               + karis(f, g, k, l) * 0.125 + karis(g, h, l, m) * 0.125;

    float threshold = bloom.uParams.x;
    float knee = max(threshold * bloom.uParams.y, 1e-4);
    float brightness = max(color.r, max(color.g, color.b));
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
    outColor = vec4(color * contribution, 1.0);
}

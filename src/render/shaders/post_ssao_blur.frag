#version 450

// Depth-aware 4x4 blur for the SSAO noise pattern.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform BlurUBO {
    vec4 uTexel; // xy texel size
} blur;

layout(set = 2, binding = 0) uniform sampler2D uAo;

void main() {
    vec2 center = texture(uAo, v_uv).rg;
    float sum = 0.0;
    float weight = 0.0;
    for (int y = -2; y < 2; ++y) {
        for (int x = -2; x < 2; ++x) {
            vec2 s = texture(uAo, v_uv + (vec2(x, y) + 0.5) * blur.uTexel.xy).rg;
            float w = 1.0 / (1e-3 + abs(s.g - center.g) * 8.0);
            sum += s.r * w;
            weight += w;
        }
    }
    outColor = vec4(sum / max(weight, 1e-4), center.g, 0.0, 1.0);
}

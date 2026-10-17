#version 450

// 3x3 tent upsample, added onto the next larger level (additive blend).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform BloomUBO {
    vec4 uTexel;   // xy source texel size
    vec4 uParams;  // x radius scale, y weight
} bloom;

layout(set = 2, binding = 0) uniform sampler2D uSource;

void main() {
    vec2 t = bloom.uTexel.xy * max(bloom.uParams.x, 0.25);
    vec3 c = texture(uSource, v_uv + vec2(-t.x, -t.y)).rgb
           + texture(uSource, v_uv + vec2(0.0, -t.y)).rgb * 2.0
           + texture(uSource, v_uv + vec2(t.x, -t.y)).rgb
           + texture(uSource, v_uv + vec2(-t.x, 0.0)).rgb * 2.0
           + texture(uSource, v_uv).rgb * 4.0
           + texture(uSource, v_uv + vec2(t.x, 0.0)).rgb * 2.0
           + texture(uSource, v_uv + vec2(-t.x, t.y)).rgb
           + texture(uSource, v_uv + vec2(0.0, t.y)).rgb * 2.0
           + texture(uSource, v_uv + vec2(t.x, t.y)).rgb;
    outColor = vec4(c * (1.0 / 16.0) * bloom.uParams.y, 1.0);
}

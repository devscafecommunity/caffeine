#version 450

// 13-tap downsample (Call of Duty: Advanced Warfare bloom).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform BloomUBO {
    vec4 uTexel;   // xy source texel size
    vec4 uParams;
} bloom;

layout(set = 2, binding = 0) uniform sampler2D uSource;

vec3 tap(vec2 offset) {
    return texture(uSource, v_uv + offset * bloom.uTexel.xy).rgb;
}

void main() {
    vec3 a = tap(vec2(-2, -2)), b = tap(vec2(0, -2)), c = tap(vec2(2, -2));
    vec3 d = tap(vec2(-1, -1)), e = tap(vec2(1, -1));
    vec3 f = tap(vec2(-2, 0)), g = tap(vec2(0, 0)), h = tap(vec2(2, 0));
    vec3 i = tap(vec2(-1, 1)), j = tap(vec2(1, 1));
    vec3 k = tap(vec2(-2, 2)), l = tap(vec2(0, 2)), m = tap(vec2(2, 2));

    vec3 color = (d + e + i + j) * 0.125
               + (a + b + f + g) * 0.03125 + (b + c + g + h) * 0.03125
               + (f + g + k + l) * 0.03125 + (g + h + l + m) * 0.03125;
    outColor = vec4(color, 1.0);
}

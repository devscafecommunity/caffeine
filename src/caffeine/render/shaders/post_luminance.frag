#version 450

// Log-luminance of the HDR scene; mip-mapped afterwards to get the scene average.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uScene;

void main() {
    vec3 c = texture(uScene, v_uv).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    outColor = vec4(log2(max(lum, 1e-4)), 0.0, 0.0, 1.0);
}

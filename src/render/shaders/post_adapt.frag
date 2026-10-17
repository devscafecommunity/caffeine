#version 450

// Eye adaptation: blends last frame's adapted log-luminance toward the current average.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform AdaptUBO {
    vec4 uParams; // x blend factor (1 = snap), y average mip, z min log2, w max log2
} adapt;

layout(set = 2, binding = 0) uniform sampler2D uLuminance;
layout(set = 2, binding = 1) uniform sampler2D uPrevious;

void main() {
    float current = textureLod(uLuminance, vec2(0.5), adapt.uParams.y).r;
    current = clamp(current, adapt.uParams.z, adapt.uParams.w);
    float previous = texture(uPrevious, vec2(0.5)).r;
    float value = mix(previous, current, clamp(adapt.uParams.x, 0.0, 1.0));
    outColor = vec4(value, 0.0, 0.0, 1.0);
}

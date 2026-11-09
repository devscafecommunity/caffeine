#version 450

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;

layout(location = 0) out vec4 v_color;

layout(set = 0, binding = 0) uniform OverlayUBO {
    mat4 uViewProj;
} overlay;

void main() {
    v_color = a_color;
    gl_Position = overlay.uViewProj * vec4(a_pos, 0.0, 1.0);
}

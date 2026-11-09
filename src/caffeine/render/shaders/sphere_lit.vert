#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(set = 1, binding = 0) uniform Uniforms {
    mat4 uMVP;
    vec4 uAlbedo;
    float uMetallic;
    float uRoughness;
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;

void main() {
    vNormal = aNormal;
    vUV = aUV;
    gl_Position = ubo.uMVP * vec4(aPosition, 1.0);
}

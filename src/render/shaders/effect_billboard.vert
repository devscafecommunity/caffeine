#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(set = 1, binding = 0) uniform EffectVertexUBO {
    mat4 uMVP;
    mat4 uModel;
} ubo;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main() {
    v_uv = aTexCoord;
    v_color = aTangent;
    gl_Position = ubo.uMVP * vec4(aPosition, 1.0);
}

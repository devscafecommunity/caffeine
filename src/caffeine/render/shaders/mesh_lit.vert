#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 uMVP;
    mat4 uModel;
} ubo;

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 texCoord;

void main() {
    vec4 worldPos = ubo.uModel * vec4(aPosition, 1.0);
    v_position = worldPos.xyz;
    v_normal = mat3(ubo.uModel) * aNormal;
    texCoord = aTexCoord;
    gl_Position = ubo.uMVP * vec4(aPosition, 1.0);
}

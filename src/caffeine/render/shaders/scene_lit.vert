#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 uMVP;
    mat4 uModel;
} ubo;

layout(location = 0) out vec3 v_worldPos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_texCoord;
layout(location = 3) out vec3 v_tangent;
layout(location = 4) out vec3 v_bitangent;

void main() {
    vec4 worldPos = ubo.uModel * vec4(aPosition, 1.0);
    v_worldPos = worldPos.xyz;

    vec3 N = normalize(mat3(ubo.uModel) * aNormal);
    vec3 T = normalize(mat3(ubo.uModel) * aTangent.xyz);
    vec3 B = cross(N, T) * aTangent.w;

    v_normal = N;
    v_tangent = T;
    v_bitangent = B;
    v_texCoord = aTexCoord;
    gl_Position = ubo.uMVP * vec4(aPosition, 1.0);
}

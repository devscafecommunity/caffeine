#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(set = 1, binding = 0) uniform ShadowUBO {
    mat4 uMVP;
    mat4 uModel;
    vec4 uLightPos;
    int uMode; // 0 = linear distance (point), 1 = NDC depth (directional/spot)
} ubo;

layout(location = 0) out float v_depth;

void main() {
    vec4 world = ubo.uModel * vec4(aPosition, 1.0);
    vec4 clip = ubo.uMVP * vec4(aPosition, 1.0);
    if (ubo.uMode == 0) {
        v_depth = length(world.xyz - ubo.uLightPos.xyz);
    } else {
        v_depth = clip.z / clip.w;
    }
    gl_Position = clip;
}

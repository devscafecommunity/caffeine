#version 450

// Directional shadow cascades for Micromeshes: positions only, the matrix from the instance storage buffer
// (gl_InstanceIndex is the slot's firstInstance, the instance). The depth is the NDC depth the directional
// shadow maps store (shadow_depth.vert mode 1).
layout(location = 0) in vec3 aPosition;

layout(std430, set = 0, binding = 0) readonly buffer Instances { vec4 instances[]; };

layout(set = 1, binding = 0) uniform ShadowMicromeshUBO {
    mat4 uViewProj;   // the cascade's light view-projection
} ubo;

layout(location = 0) out float v_depth;

void main() {
    uint instance = uint(gl_InstanceIndex);
    vec4 r0 = instances[instance * 4u + 0u];
    vec4 r1 = instances[instance * 4u + 1u];
    vec4 r2 = instances[instance * 4u + 2u];
    vec3 world = vec3(dot(r0.xyz, aPosition) + r0.w, dot(r1.xyz, aPosition) + r1.w, dot(r2.xyz, aPosition) + r2.w);
    vec4 clip = ubo.uViewProj * vec4(world, 1.0);
    v_depth = clip.z / clip.w;
    gl_Position = clip;
}

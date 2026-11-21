#version 450

// Scene pass vertex shader for Micromeshes: the same outputs as scene_lit.vert (so scene_lit.frag, the
// G-buffer, Photon, SSAO and TAA need no change), but the vertex streams are the cluster buffers
// (positions, normals, UVs, tangents in four slots) and the object matrix comes from the instance
// storage buffer of render/MicromeshGpu. `gl_InstanceIndex` is the slot's `firstInstance`: the instance.
// A non-uniform scale is handled (normals use the inverse transpose).
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in vec4 aColor;

layout(std430, set = 0, binding = 0) readonly buffer Instances { vec4 instances[]; };

layout(set = 1, binding = 0) uniform MicromeshVertexUBO {
    mat4 uViewProj;
} ubo;

layout(location = 0) out vec3 v_worldPos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_texCoord;
layout(location = 3) out vec3 v_tangent;
layout(location = 4) out vec3 v_bitangent;
layout(location = 5) out vec2 v_uvScale;
layout(location = 6) out vec4 v_color;
layout(location = 7) out float v_foam;

void main() {
    uint instance = uint(gl_InstanceIndex);
    vec4 r0 = instances[instance * 4u + 0u];
    vec4 r1 = instances[instance * 4u + 1u];
    vec4 r2 = instances[instance * 4u + 2u];
    mat3 model = mat3(r0.x, r1.x, r2.x, r0.y, r1.y, r2.y, r0.z, r1.z, r2.z);  // rows r0..r2 as a matrix

    vec3 world = model * aPosition + vec3(r0.w, r1.w, r2.w);
    v_worldPos = world;

    mat3 normalMatrix = transpose(inverse(model));
    vec3 N = normalize(normalMatrix * aNormal);
    vec3 T = normalize(model * aTangent.xyz);
    vec3 B = cross(N, T) * aTangent.w;
    v_normal = N;
    v_tangent = T;
    v_bitangent = B;
    v_color = aColor;
    v_foam = 0.0;
    v_texCoord = aTexCoord;

    // World length of the tangent / bitangent per object-space unit (see scene_lit.vert).
    float tl = length(aTangent.xyz);
    vec2 uvScale = vec2(1.0);
    if (tl >= 1e-6) {
        vec3 t = aTangent.xyz / tl;
        vec3 b = cross(normalize(aNormal), t) * aTangent.w;
        uvScale = vec2(length(model * t), length(model * b));
    }
    v_uvScale = uvScale;
    gl_Position = ubo.uViewProj * vec4(world, 1.0);
}

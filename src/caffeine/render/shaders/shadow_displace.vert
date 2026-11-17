#version 450

// shadow_depth.vert with the same height displacement as scene_displace.vert, so geometric
// relief casts (and receives) real shadows. Keep the two formulas identical.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(set = 0, binding = 0) uniform sampler2D uHeightMap;

layout(set = 1, binding = 0) uniform ShadowUBO {
    mat4 uMVP;
    mat4 uModel;
    vec4 uLightPos;
    int uMode; // 0 = linear distance (point), 1 = NDC depth (directional/spot)
    vec4 uDisp; // x depth (metres), y uv follows object scale
    vec4 uUv;   // xy tiling, zw offset
} ubo;

layout(location = 0) out float v_depth;


// World length of the mesh's tangent / bitangent per object-space unit: how much the model
// matrix stretches the U and V directions. Multiplying the UVs by it keeps the texture's
// density when the object is scaled (a quad made twice as big tiles the texture twice).
vec2 objectUvScale(mat3 model, vec3 normal, vec4 tangent) {
    float tl = length(tangent.xyz);
    if (tl < 1e-6) return vec2(1.0);
    vec3 T = tangent.xyz / tl;
    vec3 B = cross(normalize(normal), T) * tangent.w;
    return vec2(length(model * T), length(model * B));
}

void main() {
    vec2 uvScale = ubo.uDisp.y > 0.5 ? objectUvScale(mat3(ubo.uModel), aNormal, aTangent) : vec2(1.0);
    vec2 uv = aTexCoord * ubo.uUv.xy * uvScale + ubo.uUv.zw;
    float h = textureLod(uHeightMap, uv, 0.0).a;
    float scaleAlongNormal = max(length(mat3(ubo.uModel) * aNormal), 1e-5);
    vec3 localPos = aPosition + aNormal * ((h - 1.0) * ubo.uDisp.x / scaleAlongNormal);

    vec4 world = ubo.uModel * vec4(localPos, 1.0);
    vec4 clip = ubo.uMVP * vec4(localPos, 1.0);
    if (ubo.uMode == 0) {
        v_depth = length(world.xyz - ubo.uLightPos.xyz);
    } else {
        v_depth = clip.z / clip.w;
    }
    gl_Position = clip;
}

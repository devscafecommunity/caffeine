#version 450

// Same outputs as scene_lit.vert, but the vertices are pushed along their normal by the height
// stored in the ALPHA channel of the material's normal map (1 = surface, 0 = deepest), so the
// relief is real geometry: it changes the silhouette and, with shadow_displace.vert, casts
// shadows. The normal is rebuilt from finite differences of the same height field.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

// SDL GPU: vertex samplers live in set 0, vertex uniform buffers in set 1.
layout(set = 0, binding = 0) uniform sampler2D uHeightMap;

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 uMVP;
    mat4 uModel;
    vec4 uDisp; // x depth (metres), y finite-difference step (tiled uv), z metres per tiled uv, w uv follows object scale
    vec4 uUv;   // xy tiling, zw offset
} ubo;

layout(location = 0) out vec3 v_worldPos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_texCoord;
layout(location = 3) out vec3 v_tangent;
layout(location = 4) out vec3 v_bitangent;
layout(location = 5) out vec2 v_uvScale;
layout(location = 6) out vec4 v_color;
layout(location = 7) out float v_foam;

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

float heightAt(vec2 uv) {
    return textureLod(uHeightMap, uv, 0.0).a;
}

void main() {
    vec2 uvScale = ubo.uDisp.w > 0.5 ? objectUvScale(mat3(ubo.uModel), aNormal, aTangent) : vec2(1.0);
    vec2 uv = aTexCoord * ubo.uUv.xy * uvScale + ubo.uUv.zw;
    float h = heightAt(uv);

    // Metres of displacement -> object space (the model may scale along the normal).
    float scaleAlongNormal = max(length(mat3(ubo.uModel) * aNormal), 1e-5);
    float depth = (h - 1.0) * ubo.uDisp.x;
    vec3 localPos = aPosition + aNormal * (depth / scaleAlongNormal);

    vec3 N = normalize(mat3(ubo.uModel) * aNormal);
    vec3 T = normalize(mat3(ubo.uModel) * aTangent.xyz);
    vec3 B = cross(N, T) * aTangent.w;

    // Slope of the displaced surface along T (u) and B (v), in metres per metre.
    float eps = max(ubo.uDisp.y, 1e-5);
    float metres = max(ubo.uDisp.z, 1e-5);
    float dhdu = (heightAt(uv + vec2(eps, 0.0)) - heightAt(uv - vec2(eps, 0.0))) * ubo.uDisp.x / (2.0 * eps * metres);
    float dhdv = (heightAt(uv + vec2(0.0, eps)) - heightAt(uv - vec2(0.0, eps))) * ubo.uDisp.x / (2.0 * eps * metres);
    vec3 Nd = normalize(N - T * dhdu - B * dhdv);
    vec3 Td = normalize(T - Nd * dot(Nd, T));
    vec3 Bd = cross(Nd, Td) * aTangent.w;

    v_worldPos = (ubo.uModel * vec4(localPos, 1.0)).xyz;
    v_normal = Nd;
    v_tangent = Td;
    v_bitangent = Bd;
    v_color = vec4(1.0);
    v_foam = 0.0;
    v_texCoord = aTexCoord;
    v_uvScale = uvScale;
    gl_Position = ubo.uMVP * vec4(localPos, 1.0);
}

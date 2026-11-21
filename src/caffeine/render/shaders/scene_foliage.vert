#version 450

// scene_lit.vert for meshes with per-vertex colour (a second vertex stream): rgb tints the albedo,
// alpha is the painted wind weight. Keep in sync with scene_lit.vert.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in vec4 aColor; // second vertex stream: rgb tint, a wind weight

layout(set = 1, binding = 0) uniform VertexUBO {
    mat4 uMVP;
    mat4 uModel;
    vec4 uWind;
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


// Foliage wind (shared by the colour, instanced and shadow vertex shaders; keep in sync).
// `wind` = strength per metre of height above the object's origin, speed, flutter, time.
// `flutterMask` is the painted per-vertex weight of the fast shimmer (1 without a colour stream):
// the lean follows the object so leaves stay on their branches, only the flutter is painted.
// Returns a world-space offset: a slow gust that leans the crown along the wind, phased per
// object so a forest does not move in lockstep, plus fast per-vertex flutter for leaves.
vec3 windOffset(vec3 world, vec3 origin, vec4 wind, float flutterMask) {
    if (wind.x <= 0.0 && wind.z <= 0.0) return vec3(0.0);
    float h = max(world.y - origin.y, 0.0);
    float t = wind.w * wind.y;
    float phase = dot(origin.xz, vec2(0.37, 0.23));
    float gust = 0.55 + 0.45 * sin(t * 0.8 + phase) + 0.30 * sin(t * 2.1 + phase * 1.7)
               + 0.15 * sin(t * 4.7 + phase * 2.9);
    vec2 dir = vec2(0.86, 0.51);
    vec3 off = vec3(dir.x, 0.0, dir.y) * (wind.x * h * h / (h + 1.5) * gust * 1.6);
    float f = wind.z * 0.06 * (0.4 + 0.6 * gust);
    vec3 p = world * 2.3;
    off += flutterMask * f * vec3(sin(t * 7.0 + p.x + p.z * 1.3), 0.5 * sin(t * 9.0 + p.y * 2.0 + p.x),
                    cos(t * 6.0 + p.z + p.y * 1.1));
    return off;
}

void main() {
    vec3 localPos = aPosition;
    vec4 worldPos = ubo.uModel * vec4(localPos, 1.0);
    if (ubo.uWind.x > 0.0 || ubo.uWind.z > 0.0) {
        // Offset in object space so the MVP (and the shadow pass) see the same moved vertex.
        vec3 off = windOffset(worldPos.xyz, ubo.uModel[3].xyz, ubo.uWind, aColor.a);
        localPos += inverse(mat3(ubo.uModel)) * off;
        worldPos = ubo.uModel * vec4(localPos, 1.0);
    }
    v_worldPos = worldPos.xyz;

    vec3 N = normalize(transpose(inverse(mat3(ubo.uModel))) * aNormal);  // inverse transpose: right under a non-uniform scale
    vec3 T = normalize(mat3(ubo.uModel) * aTangent.xyz);
    vec3 B = cross(N, T) * aTangent.w;

    v_normal = N;
    v_tangent = T;
    v_bitangent = B;
    v_foam = 0.0;
    v_texCoord = aTexCoord;
    v_color = vec4(aColor.rgb, 1.0);
    v_uvScale = objectUvScale(mat3(ubo.uModel), aNormal, aTangent);
    gl_Position = ubo.uMVP * vec4(localPos, 1.0);
}

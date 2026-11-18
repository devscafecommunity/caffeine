#version 450

// scene_instanced.vert for meshes with per-vertex colour. Keep in sync with scene_instanced.vert.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in vec4 aModel0;
layout(location = 5) in vec4 aModel1;
layout(location = 6) in vec4 aModel2;
layout(location = 7) in vec4 aModel3;
layout(location = 8) in vec4 aColor; // third stream: rgb tint, a flutter weight

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
    mat4 model = mat4(aModel0, aModel1, aModel2, aModel3);
    vec4 worldPos = model * vec4(aPosition, 1.0);
    if (ubo.uWind.x > 0.0 || ubo.uWind.z > 0.0) {
        worldPos.xyz += windOffset(worldPos.xyz, model[3].xyz, ubo.uWind, aColor.a);
    }
    v_worldPos = worldPos.xyz;

    vec3 N = normalize(mat3(model) * aNormal);
    vec3 T = normalize(mat3(model) * aTangent.xyz);
    vec3 B = cross(N, T) * aTangent.w;

    v_normal = N;
    v_tangent = T;
    v_bitangent = B;
    v_color = vec4(aColor.rgb, 1.0);
    v_foam = 0.0;
    v_texCoord = aTexCoord;
    v_uvScale = vec2(1.0); // instances keep their authored UVs (batched vegetation)
    gl_Position = ubo.uMVP * worldPos + ubo.uModel[3] * 0.0;
}

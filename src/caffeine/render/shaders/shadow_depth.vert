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
    vec4 uWind;
} ubo;

layout(location = 0) out float v_depth;
layout(location = 1) out vec2 v_uv;


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
    vec4 world = ubo.uModel * vec4(localPos, 1.0);
    if (ubo.uWind.x > 0.0 || ubo.uWind.z > 0.0) {
        vec3 off = windOffset(world.xyz, ubo.uModel[3].xyz, ubo.uWind, 1.0);
        localPos += inverse(mat3(ubo.uModel)) * off;
        world = ubo.uModel * vec4(localPos, 1.0);
    }
    vec4 clip = ubo.uMVP * vec4(localPos, 1.0);
    if (ubo.uMode == 0) {
        v_depth = length(world.xyz - ubo.uLightPos.xyz);
    } else {
        v_depth = clip.z / clip.w;
    }
    v_uv = aTexCoord;
    gl_Position = clip;
}

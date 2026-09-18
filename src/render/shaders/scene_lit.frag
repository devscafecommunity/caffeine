#version 450

layout(location = 0) in vec3 v_worldPos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_texCoord;

layout(set = 3, binding = 0) uniform LightingUBO {
    vec4 uCameraPos;
    vec4 uAmbient;
    vec4 uAlbedo;
    float uMetallic;
    float uRoughness;
    int uDirCount;
    int uPointCount;
    vec4 uDirData[4];      // xyz = direction, w = intensity
    vec4 uDirColor[4];
    vec4 uPointData[4];    // xyz = position, w = radius
    vec4 uPointColor[4];
    vec4 uPointShadow[4];  // x = hasShadow, y = shadow slot
    vec4 uDirShadow[4];    // x = hasShadow, y = shadow slot
    mat4 uDirShadowVP[2];
} lights;

layout(set = 2, binding = 0) uniform samplerCube uPointShadow0;
layout(set = 2, binding = 1) uniform samplerCube uPointShadow1;
layout(set = 2, binding = 2) uniform sampler2D uDirShadow0;
layout(set = 2, binding = 3) uniform sampler2D uDirShadow1;

layout(location = 0) out vec4 outColor;

float samplePointShadow(int slot, vec3 worldPos, vec3 lightPos, float radius) {
    vec3 toFrag = worldPos - lightPos;
    float dist = length(toFrag);
    if (dist > radius) return 1.0;

    vec3 dir = normalize(toFrag);
    float stored = 0.0;
    if (slot == 0) stored = texture(uPointShadow0, dir).r;
    else if (slot == 1) stored = texture(uPointShadow1, dir).r;

    const float bias = 0.02;
    return dist - bias > stored ? 0.15 : 1.0;
}

float sampleDirShadow(int slot, vec3 worldPos, mat4 lightVP) {
    vec4 clip = lightVP * vec4(worldPos, 1.0);
    if (clip.w <= 0.0) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0) return 1.0;

    vec2 uv = ndc.xy * 0.5 + 0.5;
    float stored = 0.0;
    if (slot == 0) stored = texture(uDirShadow0, uv).r;
    else if (slot == 1) stored = texture(uDirShadow1, uv).r;

    const float bias = 0.002;
    return ndc.z - bias > stored ? 0.2 : 1.0;
}

void main() {
    vec3 n = normalize(v_normal);
    vec3 ambient = lights.uAmbient.rgb * lights.uAlbedo.rgb;
    vec3 color = ambient;

    for (int i = 0; i < lights.uDirCount; ++i) {
        vec3 dir = normalize(-lights.uDirData[i].xyz);
        float ndotl = max(dot(n, dir), 0.0);
        float shadow = 1.0;
        if (lights.uDirShadow[i].x > 0.5) {
            int slot = int(lights.uDirShadow[i].y);
            shadow = sampleDirShadow(slot, v_worldPos, lights.uDirShadowVP[slot]);
        }
        vec3 diffuse = lights.uDirColor[i].rgb * lights.uDirData[i].w * ndotl * shadow;
        color += diffuse * lights.uAlbedo.rgb;
    }

    for (int i = 0; i < lights.uPointCount; ++i) {
        vec3 toLight = lights.uPointData[i].xyz - v_worldPos;
        float dist = length(toLight);
        float radius = max(lights.uPointData[i].w, 0.001);
        if (dist > radius) continue;

        vec3 ldir = toLight / dist;
        float atten = 1.0 - smoothstep(radius * 0.75, radius, dist);
        float ndotl = max(dot(n, ldir), 0.0);
        float shadow = 1.0;
        if (lights.uPointShadow[i].x > 0.5) {
            int slot = int(lights.uPointShadow[i].y);
            shadow = samplePointShadow(slot, v_worldPos, lights.uPointData[i].xyz, radius);
        }
        vec3 diffuse = lights.uPointColor[i].rgb * lights.uPointData[i].w * ndotl * atten * shadow;
        color += diffuse * lights.uAlbedo.rgb;
    }

    outColor = vec4(color, 1.0);
}

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
    vec4 uDirData[4];
    vec4 uDirColor[4];
    vec4 uPointData[4];
    vec4 uPointColor[4];
    vec4 uPointShadow[4];
    vec4 uDirShadow[4];
    mat4 uDirShadowVP[2];
} lights;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(v_normal);
    vec3 ambient = lights.uAmbient.rgb * lights.uAlbedo.rgb;
    vec3 color = ambient;

    for (int i = 0; i < lights.uDirCount; ++i) {
        vec3 dir = normalize(-lights.uDirData[i].xyz);
        float ndotl = max(dot(n, dir), 0.0);
        vec3 diffuse = lights.uDirColor[i].rgb * lights.uDirData[i].w * ndotl;
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
        vec3 diffuse = lights.uPointColor[i].rgb * lights.uPointData[i].w * ndotl * atten;
        color += diffuse * lights.uAlbedo.rgb;
    }

    outColor = vec4(color, 1.0);
}

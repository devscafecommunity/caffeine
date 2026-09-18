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

layout(set = 3, binding = 1) uniform TerrainMaterialUBO {
    vec4 uWorldSize;
    vec4 uFlags;
    mat4 uModelInv;
} terrainMat;

layout(set = 2, binding = 0) uniform samplerCube uPointShadow0;
layout(set = 2, binding = 1) uniform samplerCube uPointShadow1;
layout(set = 2, binding = 2) uniform sampler2D uDirShadow0;
layout(set = 2, binding = 3) uniform sampler2D uDirShadow1;
layout(set = 2, binding = 4) uniform sampler2D uSplatMap;
layout(set = 2, binding = 5) uniform sampler2D uLayer0;
layout(set = 2, binding = 6) uniform sampler2D uLayer1;
layout(set = 2, binding = 7) uniform sampler2D uLayer2;
layout(set = 2, binding = 8) uniform sampler2D uLayer3;
layout(set = 2, binding = 9) uniform sampler2D uAlbedoTex;

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

vec3 triplanarSample(sampler2D tex, vec3 localPos, vec3 blend, float tileScale) {
    vec3 scaled = localPos / max(tileScale, 0.1);
    vec3 xProj = texture(tex, scaled.yz).rgb;
    vec3 yProj = texture(tex, scaled.xz).rgb;
    vec3 zProj = texture(tex, scaled.xy).rgb;
    return xProj * blend.x + yProj * blend.y + zProj * blend.z;
}

vec3 applyDetailVariation(vec3 albedo, vec3 localPos) {
    float macro = sin(localPos.x * 0.08) * sin(localPos.z * 0.06) * 0.015;
    return clamp(albedo * (1.0 + macro), 0.0, 1.0);
}

vec3 sampleTerrainAlbedo(vec3 n) {
    const float tileScale = max(terrainMat.uWorldSize.z, 0.1);
    vec3 blend = abs(normalize(n));
    blend = max(blend, vec3(0.0001));
    blend /= (blend.x + blend.y + blend.z);

    if (terrainMat.uFlags.x > 0.5) {
        vec3 local = (terrainMat.uModelInv * vec4(v_worldPos, 1.0)).xyz;
        float halfX = terrainMat.uWorldSize.x * 0.5;
        float halfZ = terrainMat.uWorldSize.y * 0.5;
        float su = clamp((local.x + halfX) / max(terrainMat.uWorldSize.x, 0.0001), 0.0, 1.0);
        float sv = clamp((local.z + halfZ) / max(terrainMat.uWorldSize.y, 0.0001), 0.0, 1.0);
        vec4 weights = texture(uSplatMap, vec2(su, sv));
        vec3 albedo = vec3(0.0);
        albedo += triplanarSample(uLayer0, local, blend, tileScale) * weights.r;
        albedo += triplanarSample(uLayer1, local, blend, tileScale) * weights.g;
        albedo += triplanarSample(uLayer2, local, blend, tileScale) * weights.b;
        albedo += triplanarSample(uLayer3, local, blend, tileScale) * weights.a;
        if (dot(albedo, vec3(1.0)) > 1e-4) {
            return applyDetailVariation(albedo, local);
        }
    }
    if (terrainMat.uFlags.y > 0.5) {
        vec3 local = (terrainMat.uModelInv * vec4(v_worldPos, 1.0)).xyz;
        return applyDetailVariation(triplanarSample(uAlbedoTex, local, blend, tileScale), local);
    }
    return lights.uAlbedo.rgb;
}

void main() {
    vec3 n = normalize(v_normal);
    vec3 surfaceAlbedo = sampleTerrainAlbedo(n);
    vec3 ambient = lights.uAmbient.rgb * surfaceAlbedo;
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
        color += diffuse * surfaceAlbedo;
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
        color += diffuse * surfaceAlbedo;
    }

    outColor = vec4(color, 1.0);
}

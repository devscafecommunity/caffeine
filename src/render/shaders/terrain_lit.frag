#version 450

layout(location = 0) in vec3 v_worldPos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_texCoord;
layout(location = 3) in vec3 v_tangent;
layout(location = 4) in vec3 v_bitangent;

layout(set = 3, binding = 0) uniform LightingUBO {
    vec4 uCameraPos;
    vec4 uAmbient;
    vec4 uAlbedo;
    float uMetallic;
    float uRoughness;
    float uShininess;
    vec4 uFlags; // x=receiveShadows, y=useNormalMap
    int uDirCount;
    int uPointCount;
    int uSpotCount;
    int uAlignPad;
    vec4 uDirData[4];
    vec4 uDirColor[4];
    vec4 uPointData[4];
    vec4 uPointColor[4];
    vec4 uSpotData[4];
    vec4 uSpotDir[4];
    vec4 uSpotColor[4];
    vec4 uSpotAngle[4];
} lights;

layout(set = 3, binding = 1) uniform ShadowUBO {
    mat4 uDirShadowVP[8];
    vec4 uDirCascadeSplits[2];
    vec4 uDirShadowValid;
    mat4 uCameraView;
    vec4 uPointShadowPos[2];
    vec4 uPointShadowRadius;
    vec4 uPointShadowValid;
    mat4 uSpotShadowVP[2];
    vec4 uSpotShadowPos[2];
    vec4 uSpotShadowParams[2];
    vec4 uSpotShadowValid;
} shadows;

layout(set = 3, binding = 2) uniform TerrainMaterialUBO {
    vec4 uWorldSize;
    vec4 uFlags;
    mat4 uModelInv;
} terrainMat;

layout(set = 2, binding = 0) uniform sampler2D uSplatMap;
layout(set = 2, binding = 1) uniform sampler2D uLayer0;
layout(set = 2, binding = 2) uniform sampler2D uLayer1;
layout(set = 2, binding = 3) uniform sampler2D uLayer2;
layout(set = 2, binding = 4) uniform sampler2D uLayer3;
layout(set = 2, binding = 5) uniform sampler2D uNormalMap;
layout(set = 2, binding = 6) uniform sampler2D uShadowMap0;
layout(set = 2, binding = 7) uniform sampler2D uShadowMap1;
layout(set = 2, binding = 8) uniform samplerCube uPointShadow0;
layout(set = 2, binding = 9) uniform samplerCube uPointShadow1;
layout(set = 2, binding = 10) uniform sampler2D uSpotShadow0;
layout(set = 2, binding = 11) uniform sampler2D uSpotShadow1;

layout(location = 0) out vec4 outColor;

float sampleShadowMapAtlas(sampler2D shadowMap, mat4 lightVP, vec3 worldPos, int cascade,
                           int cascadeCount) {
    vec4 clip = lightVP * vec4(worldPos, 1.0);
    if (clip.w <= 1e-5) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0 || ndc.z < -1.0 || ndc.z > 1.0) return 1.0;
    vec2 uv = vec2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));
    if (cascadeCount > 1) {
        vec2 offset = vec2(float(cascade % 2) * 0.5, float(cascade / 2) * 0.5);
        uv = uv * 0.5 + offset;
    }
    const float bias = 0.005;
    float depth = ndc.z;
    float shadow = 0.0;
    vec2 atlasSize = vec2(textureSize(shadowMap, 0));
    vec2 texel = vec2(0.5 / atlasSize.x, 0.5 / atlasSize.y);
    if (cascadeCount > 1) {
        texel *= 0.5;
    }
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(float(x), float(y)) * texel;
            float stored = texture(shadowMap, uv + offset).r;
            shadow += (depth - bias > stored) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

int selectDirCascade(float viewDepth, vec4 splitData) {
    int count = int(splitData.w);
    if (count <= 1) return 0;
    if (viewDepth < splitData.x) return 0;
    if (viewDepth < splitData.y) return 1;
    if (viewDepth < splitData.z) return 2;
    return 3;
}

float sampleDirShadow(sampler2D shadowMap, int lightIndex, vec3 worldPos) {
    vec4 splitData = shadows.uDirCascadeSplits[lightIndex];
    float viewDepth = (shadows.uCameraView * vec4(worldPos, 1.0)).z;
    if (viewDepth < 0.0) viewDepth = -viewDepth;
    int cascade = selectDirCascade(viewDepth, splitData);
    mat4 lightVP = shadows.uDirShadowVP[lightIndex * 4 + cascade];
    return sampleShadowMapAtlas(shadowMap, lightVP, worldPos, cascade, int(splitData.w));
}

float sampleSpotShadow(sampler2D shadowMap, mat4 lightVP, vec3 worldPos) {
    return sampleShadowMapAtlas(shadowMap, lightVP, worldPos, 0, 1);
}

float samplePointShadow(samplerCube shadowCube, vec3 worldPos, vec3 lightPos, float radius) {
    vec3 toFrag = worldPos - lightPos;
    float dist = length(toFrag);
    if (dist > radius) return 1.0;
    float stored = texture(shadowCube, toFrag).r;
    return (dist - 0.05 > stored) ? 0.0 : 1.0;
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
    vec3 local = (terrainMat.uModelInv * vec4(v_worldPos, 1.0)).xyz;

    if (terrainMat.uFlags.x > 0.5) {
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
    return applyDetailVariation(triplanarSample(uLayer3, local, blend, tileScale), local);
}

vec3 sampleTerrainNormal(vec3 n) {
    if (lights.uFlags.y < 0.5) return normalize(n);
    const float tileScale = max(terrainMat.uWorldSize.z, 0.1);
    vec3 blend = abs(normalize(n));
    blend = max(blend, vec3(0.0001));
    blend /= (blend.x + blend.y + blend.z);
    vec3 local = (terrainMat.uModelInv * vec4(v_worldPos, 1.0)).xyz;
    vec3 map = triplanarSample(uNormalMap, local, blend, tileScale) * 2.0 - 1.0;
    vec3 T = normalize(v_tangent);
    vec3 B = normalize(v_bitangent);
    mat3 tbn = mat3(T, B, normalize(n));
    return normalize(tbn * map);
}

void main() {
    vec3 n = sampleTerrainNormal(v_normal);
    vec3 surfaceAlbedo = sampleTerrainAlbedo(n) * lights.uAlbedo.rgb;
    vec3 color = lights.uAmbient.rgb * surfaceAlbedo;
    const float kDiffuseScale = 0.65;

    for (int i = 0; i < lights.uDirCount; ++i) {
        vec3 dir = normalize(-lights.uDirData[i].xyz);
        float ndotl = max(dot(n, dir), 0.0);
        vec3 diffuse = lights.uDirColor[i].rgb * lights.uDirData[i].w * ndotl * kDiffuseScale;

        float shadow = 1.0;
        if (lights.uFlags.x > 0.5) {
            if (i == 0 && shadows.uDirShadowValid.x > 0.5) {
                shadow = sampleDirShadow(uShadowMap0, 0, v_worldPos);
            } else if (i == 1 && shadows.uDirShadowValid.y > 0.5) {
                shadow = sampleDirShadow(uShadowMap1, 1, v_worldPos);
            }
        }
        color += diffuse * surfaceAlbedo * shadow;
    }

    for (int i = 0; i < lights.uPointCount; ++i) {
        vec3 toLight = lights.uPointData[i].xyz - v_worldPos;
        float dist = length(toLight);
        float radius = max(lights.uPointData[i].w, 0.001);
        if (dist > radius) continue;
        vec3 ldir = toLight / dist;
        float atten = 1.0 - smoothstep(radius * 0.75, radius, dist);
        float ndotl = max(dot(n, ldir), 0.0);
        vec3 diffuse = lights.uPointColor[i].rgb * lights.uPointColor[i].a * ndotl * atten *
                       kDiffuseScale;

        float shadow = 1.0;
        if (lights.uFlags.x > 0.5) {
            if (i == 0 && shadows.uPointShadowValid.x > 0.5) {
                shadow = samplePointShadow(uPointShadow0, v_worldPos,
                    shadows.uPointShadowPos[0].xyz, shadows.uPointShadowRadius.x);
            } else if (i == 1 && shadows.uPointShadowValid.y > 0.5) {
                shadow = samplePointShadow(uPointShadow1, v_worldPos,
                    shadows.uPointShadowPos[1].xyz, shadows.uPointShadowRadius.y);
            }
        }
        color += diffuse * surfaceAlbedo * shadow;
    }

    for (int i = 0; i < lights.uSpotCount; ++i) {
        vec3 toPoint = v_worldPos - lights.uSpotData[i].xyz;
        float dist = length(toPoint);
        float radius = max(lights.uSpotData[i].w, 0.001);
        if (dist > radius) continue;
        vec3 fromLight = toPoint / dist;
        vec3 spotDir = normalize(lights.uSpotDir[i].xyz);
        float cone = dot(spotDir, fromLight);
        float cosHalfAngle = lights.uSpotAngle[i].x;
        if (cone < cosHalfAngle) continue;
        float spotAtten = (cone - cosHalfAngle) / max(0.0001, 1.0 - cosHalfAngle);
        float rangeAtten = 1.0 - (dist / radius);
        vec3 ldir = -fromLight;
        float ndotl = max(dot(n, ldir), 0.0);
        vec3 diffuse = lights.uSpotColor[i].rgb * lights.uSpotDir[i].w * ndotl * spotAtten *
                       rangeAtten * rangeAtten * kDiffuseScale;
        float shadow = 1.0;
        if (lights.uFlags.x > 0.5) {
            if (i == 0 && shadows.uSpotShadowValid.x > 0.5) {
                shadow = sampleSpotShadow(uSpotShadow0, shadows.uSpotShadowVP[0], v_worldPos);
            } else if (i == 1 && shadows.uSpotShadowValid.y > 0.5) {
                shadow = sampleSpotShadow(uSpotShadow1, shadows.uSpotShadowVP[1], v_worldPos);
            }
        }
        color += diffuse * surfaceAlbedo * shadow;
    }

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}

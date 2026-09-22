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

layout(set = 2, binding = 0) uniform sampler2D uAlbedoMap;
layout(set = 2, binding = 1) uniform sampler2D uNormalMap;
layout(set = 2, binding = 2) uniform sampler2D uShadowMap0;
layout(set = 2, binding = 3) uniform sampler2D uShadowMap1;
layout(set = 2, binding = 4) uniform samplerCube uPointShadow0;
layout(set = 2, binding = 5) uniform samplerCube uPointShadow1;
layout(set = 2, binding = 6) uniform sampler2D uSpotShadow0;
layout(set = 2, binding = 7) uniform sampler2D uSpotShadow1;

layout(location = 0) out vec4 outColor;

float sampleShadowMapAtlas(sampler2D shadowMap, mat4 lightVP, vec3 worldPos, int cascade,
                           int cascadeCount) {
    vec4 clip = lightVP * vec4(worldPos, 1.0);
    if (clip.w <= 1e-5) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0 || ndc.z < -1.0 || ndc.z > 1.0) {
        return 1.0;
    }

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

float samplePointShadow(samplerCube shadowCube, vec3 worldPos, vec3 lightPos, float radius) {
    vec3 toFrag = worldPos - lightPos;
    float dist = length(toFrag);
    if (dist > radius) return 1.0;
    float stored = texture(shadowCube, toFrag).r;
    const float bias = 0.05;
    return (dist - bias > stored) ? 0.0 : 1.0;
}

float sampleSpotShadow(sampler2D shadowMap, mat4 lightVP, vec3 worldPos) {
    return sampleShadowMapAtlas(shadowMap, lightVP, worldPos, 0, 1);
}

vec3 surfaceNormal() {
    vec3 N = normalize(v_normal);
    if (lights.uFlags.y < 0.5) return N;

    vec3 T = normalize(v_tangent);
    vec3 B = normalize(v_bitangent);
    vec3 map = texture(uNormalMap, v_texCoord).xyz * 2.0 - 1.0;
    mat3 tbn = mat3(T, B, N);
    return normalize(tbn * map);
}

void main() {
    vec3 n = surfaceNormal();
    vec3 viewDir = normalize(lights.uCameraPos.xyz - v_worldPos);
    vec3 surfaceAlbedo = texture(uAlbedoMap, v_texCoord).rgb * lights.uAlbedo.rgb;
    vec3 ambient = lights.uAmbient.rgb * surfaceAlbedo;
    vec3 color = ambient;

    for (int i = 0; i < lights.uDirCount; ++i) {
        vec3 dir = normalize(-lights.uDirData[i].xyz);
        float ndotl = max(dot(n, dir), 0.0);

        vec3 diffuse = lights.uDirColor[i].rgb * lights.uDirData[i].w * ndotl;

        vec3 halfVec = normalize(dir + viewDir);
        float ndoth = max(dot(n, halfVec), 0.0);
        float spec = pow(ndoth, max(lights.uShininess, 1.0));
        vec3 specular = lights.uDirColor[i].rgb * spec * (1.0 - lights.uRoughness);

        float shadow = 1.0;
        if (lights.uFlags.x > 0.5 && lights.uDirData[i].w > 0.0) {
            if (i == 0 && shadows.uDirShadowValid.x > 0.5) {
                shadow = sampleDirShadow(uShadowMap0, 0, v_worldPos);
            } else if (i == 1 && shadows.uDirShadowValid.y > 0.5) {
                shadow = sampleDirShadow(uShadowMap1, 1, v_worldPos);
            }
        }

        color += (diffuse * surfaceAlbedo + specular) * shadow;
    }

    for (int i = 0; i < lights.uPointCount; ++i) {
        vec3 toLight = lights.uPointData[i].xyz - v_worldPos;
        float dist = length(toLight);
        float radius = max(lights.uPointData[i].w, 0.001);
        if (dist > radius) continue;

        vec3 ldir = toLight / dist;
        float atten = 1.0 - smoothstep(radius * 0.75, radius, dist);
        float ndotl = max(dot(n, ldir), 0.0);
        vec3 diffuse = lights.uPointColor[i].rgb * lights.uPointColor[i].a * ndotl * atten;

        vec3 halfVec = normalize(ldir + viewDir);
        float ndoth = max(dot(n, halfVec), 0.0);
        float spec = pow(ndoth, max(lights.uShininess, 1.0));
        vec3 specular = lights.uPointColor[i].rgb * spec * (1.0 - lights.uRoughness);

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

        color += (diffuse * surfaceAlbedo + specular) * shadow;
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
                       rangeAtten * rangeAtten;

        vec3 halfVec = normalize(ldir + viewDir);
        float ndoth = max(dot(n, halfVec), 0.0);
        float spec = pow(ndoth, max(lights.uShininess, 1.0));
        vec3 specular = lights.uSpotColor[i].rgb * spec * (1.0 - lights.uRoughness);

        float shadow = 1.0;
        if (lights.uFlags.x > 0.5) {
            if (i == 0 && shadows.uSpotShadowValid.x > 0.5) {
                shadow = sampleSpotShadow(uSpotShadow0, shadows.uSpotShadowVP[0], v_worldPos);
            } else if (i == 1 && shadows.uSpotShadowValid.y > 0.5) {
                shadow = sampleSpotShadow(uSpotShadow1, shadows.uSpotShadowVP[1], v_worldPos);
            }
        }

        color += (diffuse * surfaceAlbedo + specular) * shadow;
    }

    outColor = vec4(color, 1.0);
}

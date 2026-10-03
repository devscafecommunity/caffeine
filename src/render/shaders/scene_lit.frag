#version 450

layout(location = 0) in vec3 v_worldPos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_texCoord;
layout(location = 3) in vec3 v_tangent;
layout(location = 4) in vec3 v_bitangent;

layout(set = 3, binding = 0) uniform LightingUBO {
    vec4 uCameraPos;
    vec4 uAmbient;
    vec4 uAlbedo;       // linear rgb, a = opacity
    float uMetallic;
    float uRoughness;
    float uReflectance; // dielectric F0, from MaterialSurface::reflectance
    vec4 uFlags; // x=receiveShadows, y=useNormalMap, z=planar receiver, w=orm map
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
    vec4 uIblColor;     // rgb irradiance, a = specular strength
    vec4 uIblParams;    // x diffuse, y enabled, z probe, w anisotropy
    vec4 uVolParams;    // x enabled, y density, z height, w steps
    vec4 uReflectParams; // x intensity, y planeY, z clip below, w steps
    vec4 uExtra;        // x volumetric shadow samples, y planar steps, z env exposure (0 = no env), w env max mip
    mat4 uReflectVP;
    vec4 uEmission;     // rgb already scaled by strength (linear)
    vec4 uUvTransform;  // xy tiling, zw offset
    vec4 uMaterialParams; // x normal strength, y AO strength, z unused, w alpha cutoff
    vec4 uCoat;         // x clearcoat, y clearcoat roughness, z ior, w transmission
    vec4 uSheen;        // rgb sheen colour (linear), a sheen roughness
    vec4 uIridescence;  // x strength, y thickness (nm), z film ior
    vec4 uMaterialFlags; // x alpha mode (0 opaque, 1 cutout, 2 blend), y emission map, z opaque scene copy bound, w SSR on
    vec4 uSsrParams;    // x intensity, y max roughness, z steps, w max distance
    vec4 uProjParams;   // x prev P22, y prev P23, z probe max mip, w unused
    mat4 uPrevViewProj;
    mat4 uPrevView;
    vec4 uScreen;       // xy 1 / target size, z frame index
    mat4 uViewProj;
    vec4 uReflectPerf;    // x enabled, y resolution scale, z max steps, w temporal frames
    vec4 uReflectQuality; // x enabled, y samples, z denoise, w distance fade
    vec4 uReflectExtra;   // x planar, y probe blend, z bounces
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
layout(set = 2, binding = 8) uniform sampler2D uReflection;
layout(set = 2, binding = 9) uniform samplerCube uProbe;
layout(set = 2, binding = 10) uniform sampler2D uOrm;
layout(set = 2, binding = 11) uniform sampler2D uEnvironment;
layout(set = 2, binding = 12) uniform sampler2D uPrevColor;
layout(set = 2, binding = 13) uniform sampler2D uPrevDepth;
layout(set = 2, binding = 14) uniform sampler2D uEmissionMap;
layout(set = 2, binding = 15) uniform sampler2D uOpaqueScene;

layout(location = 0) out vec4 outColor;

const vec2 kShadowPoisson[12] = vec2[](
    vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
    vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893),
    vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

// Normal-offset, texel-scaled bias and a rotated Poisson PCF (TAA averages the rotation).
float sampleShadowMapAtlas(sampler2D shadowMap, mat4 lightVP, vec3 worldPos, vec3 normal,
                           int cascade, int cascadeCount) {
    vec2 atlasSize = vec2(textureSize(shadowMap, 0));
    float res = cascadeCount > 1 ? atlasSize.x * 0.5 : atlasSize.x;
    float scaleX = length(vec3(lightVP[0][0], lightVP[1][0], lightVP[2][0]));
    float depthPerMeter = length(vec3(lightVP[0][2], lightVP[1][2], lightVP[2][2]));
    vec4 probe = lightVP * vec4(worldPos, 1.0);
    if (probe.w <= 1e-5) return 1.0;
    float texelWorld = 2.0 * probe.w / max(scaleX * res, 1e-5);

    vec3 lightAxis = normalize(vec3(lightVP[0][2], lightVP[1][2], lightVP[2][2]));
    float NoL = abs(dot(normal, lightAxis));
    vec3 offsetPos = worldPos + normal * texelWorld * (0.6 + 1.6 * (1.0 - NoL));

    vec4 clip = lightVP * vec4(offsetPos, 1.0);
    if (clip.w <= 1e-5) return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;

    vec2 uv = vec2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));
    vec2 tileMin = vec2(0.0);
    vec2 tileMax = vec2(1.0);
    if (cascadeCount > 1) {
        vec2 offset = vec2(float(cascade % 2) * 0.5, float(cascade / 2) * 0.5);
        uv = uv * 0.5 + offset;
        tileMin = offset;
        tileMax = offset + 0.5;
    }
    vec2 texel = 1.0 / atlasSize;
    tileMin += texel;
    tileMax -= texel;

    float bias = depthPerMeter * texelWorld * 0.75 / max(probe.w, 1e-5) + 0.0004;
    float depth = ndc.z - bias;
    float angle = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    mat2 rot = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    float lit = 0.0;
    for (int i = 0; i < 12; ++i) {
        vec2 o = rot * kShadowPoisson[i] * texel * 1.75;
        float stored = textureLod(shadowMap, clamp(uv + o, tileMin, tileMax), 0.0).r;
        lit += depth > stored ? 0.0 : 1.0;
    }
    return lit / 12.0;
}

int selectDirCascade(float viewDepth, vec4 splitData) {
    int count = int(splitData.w);
    if (count <= 1) return 0;
    if (viewDepth < splitData.x) return 0;
    if (viewDepth < splitData.y) return 1;
    if (viewDepth < splitData.z) return 2;
    return 3;
}

float sampleDirShadow(sampler2D shadowMap, int lightIndex, vec3 worldPos, vec3 normal) {
    vec4 splitData = shadows.uDirCascadeSplits[lightIndex];
    float viewDepth = (shadows.uCameraView * vec4(worldPos, 1.0)).z;
    if (viewDepth < 0.0) viewDepth = -viewDepth;
    int cascade = selectDirCascade(viewDepth, splitData);
    mat4 lightVP = shadows.uDirShadowVP[lightIndex * 4 + cascade];
    return sampleShadowMapAtlas(shadowMap, lightVP, worldPos, normal, cascade, int(splitData.w));
}

float samplePointShadow(samplerCube shadowCube, vec3 worldPos, vec3 lightPos, float radius) {
    vec3 toFrag = worldPos - lightPos;
    float dist = length(toFrag);
    if (dist > radius) return 1.0;
    float stored = texture(shadowCube, toFrag).r;
    const float bias = 0.05;
    return (dist - bias > stored) ? 0.0 : 1.0;
}

float sampleSpotShadow(sampler2D shadowMap, mat4 lightVP, vec3 worldPos, vec3 normal) {
    return sampleShadowMapAtlas(shadowMap, lightVP, worldPos, normal, 0, 1);
}

const float PI = 3.14159265;

struct Surface {
    vec3 n;
    vec3 v;
    vec3 albedo;
    vec3 F0;
    float metallic;
    float roughness;
    float ao;
    float clearcoat;
    float clearcoatRoughness;
    vec3 sheen;
    float sheenRoughness;
    float transmission;
    vec3 film;          // thin-film reflectance per channel
    float iridescence;
};

vec2 materialUv() {
    return v_texCoord * lights.uUvTransform.xy + lights.uUvTransform.zw;
}

vec3 surfaceNormal(vec2 uv) {
    vec3 N = normalize(v_normal);
    if (lights.uFlags.y < 0.5) return N;

    vec3 T = normalize(v_tangent);
    vec3 B = normalize(v_bitangent);
    vec3 map = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
    map.xy *= lights.uMaterialParams.x;
    return normalize(mat3(T, B, N) * map);
}

float maxComponent(vec3 v) { return max(v.r, max(v.g, v.b)); }

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 F_Schlick(vec3 F0, float VoH) {
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}

float D_Charlie(float roughness, float NoH) {
    float a = max(roughness * roughness, 0.01);
    float invA = 1.0 / a;
    float sin2h = max(1.0 - NoH * NoH, 0.0078125);
    return (2.0 + invA) * pow(sin2h, invA * 0.5) / (2.0 * PI);
}

float V_Neubelt(float NoV, float NoL) {
    return clamp(1.0 / (4.0 * (NoL + NoV - NoL * NoV)), 0.0, 1.0);
}

// Split-sum DFG approximation (Karis, mobile): scale/bias on F0 for image-based specular.
vec3 envBrdfApprox(vec3 F0, float roughness, float NoV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

// Thin-film interference: reflectance of a film of `thickness` nm at three wavelengths.
vec3 thinFilm(float cosTheta, float thickness, float filmIor) {
    float sinT2 = (1.0 - cosTheta * cosTheta) / max(filmIor * filmIor, 1.0);
    float cosT = sqrt(max(1.0 - sinT2, 0.0));
    float opd = 2.0 * filmIor * thickness * cosT;
    vec3 phase = 2.0 * PI * opd / vec3(650.0, 532.0, 450.0);
    return 0.5 + 0.5 * cos(phase);
}

vec3 iridescentWeight(Surface s, vec3 weight) {
    if (s.iridescence <= 0.0) return weight;
    vec3 film = clamp(s.film * max(maxComponent(weight), 0.15) * 1.6, 0.0, 1.0);
    return mix(weight, film, s.iridescence);
}

// Direct light, returning diffuse and specular separately so blended surfaces can keep their
// highlights at full strength.
void shadeLight(Surface s, vec3 l, vec3 radiance, inout vec3 diffuseOut, inout vec3 specularOut) {
    float NoL = max(dot(s.n, l), 0.0);
    if (NoL <= 0.0) return;
    vec3 h = normalize(s.v + l);
    float NoV = max(dot(s.n, s.v), 1e-4);
    float NoH = max(dot(s.n, h), 0.0);
    float VoH = max(dot(s.v, h), 0.0);

    float a = max(s.roughness * s.roughness, 0.002);
    vec3 F = iridescentWeight(s, F_Schlick(s.F0, VoH));
    vec3 spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F * PI;
    vec3 kd = (1.0 - F) * (1.0 - s.metallic) * (1.0 - s.transmission);
    vec3 diffuse = kd * s.albedo * s.ao;

    if (maxComponent(s.sheen) > 0.0) {
        diffuse += s.sheen * D_Charlie(s.sheenRoughness, NoH) * V_Neubelt(NoV, NoL) * PI;
    }
    if (s.clearcoat > 0.0) {
        float ac = max(s.clearcoatRoughness * s.clearcoatRoughness, 0.002);
        float Fc = (0.04 + 0.96 * pow(1.0 - VoH, 5.0)) * s.clearcoat;
        float coat = D_GGX(NoH, ac) * V_SmithGGXCorrelated(NoV, NoL, ac) * Fc * PI;
        diffuse *= 1.0 - Fc;
        spec = spec * (1.0 - Fc) + vec3(coat);
    }
    diffuseOut += diffuse * radiance * NoL;
    specularOut += spec * radiance * NoL;
}

bool environmentAvailable() {
    return lights.uExtra.z > 0.0;
}

// Same mapping as SkyboxRenderer::directionToEquirectUV.
vec2 directionToEquirect(vec3 d) {
    d = normalize(d);
    float u = atan(d.x, d.z) / 6.28318530 + 0.5;
    float v = 0.5 - asin(clamp(d.y, -1.0, 1.0)) / 3.14159265;
    return vec2(u, v);
}

// Sky radiance along `dir`; `blur` 0 = mirror, 1 = fully diffuse (smallest mip).
vec3 sampleEnvironment(vec3 dir, float blur) {
    if (!environmentAvailable()) return lights.uIblColor.rgb;
    float lod = clamp(blur, 0.0, 1.0) * lights.uExtra.w;
    return textureLod(uEnvironment, directionToEquirect(dir), lod).rgb * lights.uExtra.z;
}

float interleavedGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

float prevLinearDepth(float d) {
    return lights.uProjParams.y / (d + lights.uProjParams.x);
}

bool prevScreenUv(vec3 p, out vec2 uv, out float rayDist) {
    vec4 clip = lights.uPrevViewProj * vec4(p, 1.0);
    if (clip.w <= 0.01) return false;
    uv = vec2(clip.x / clip.w * 0.5 + 0.5, 1.0 - (clip.y / clip.w * 0.5 + 0.5));
    rayDist = -(lights.uPrevView * vec4(p, 1.0)).z;
    return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
}

int reflectionStepCount(float roughness) {
    float steps = lights.uSsrParams.z;
    if (lights.uReflectPerf.x > 0.5) {
        float scale = clamp(lights.uReflectPerf.y, 0.25, 1.0);
        float cap = clamp(lights.uReflectPerf.z, 4.0, 64.0);
        float roughScale = mix(1.0, 0.35, clamp(roughness, 0.0, 1.0));
        steps = min(steps, cap) * scale * roughScale;
    }
    if (lights.uReflectQuality.x > 0.5) {
        steps = max(steps, clamp(lights.uReflectQuality.y, 4.0, 64.0));
    }
    return int(clamp(steps, 4.0, 64.0));
}

vec3 denoiseReflection(vec2 uv, vec3 hit) {
    float amount = lights.uReflectQuality.x > 0.5 ? lights.uReflectQuality.z : 0.0;
    if (amount <= 0.001) return hit;
    vec2 texel = max(lights.uScreen.xy, vec2(1.0 / 1920.0)) * 2.0;
    vec3 sum = hit * 2.0;
    sum += textureLod(uPrevColor, uv + vec2(texel.x, 0.0), 0.0).rgb;
    sum += textureLod(uPrevColor, uv - vec2(texel.x, 0.0), 0.0).rgb;
    sum += textureLod(uPrevColor, uv + vec2(0.0, texel.y), 0.0).rgb;
    sum += textureLod(uPrevColor, uv - vec2(0.0, texel.y), 0.0).rgb;
    return mix(hit, sum / 6.0, clamp(amount, 0.0, 1.0));
}

// Screen-space reflection: marches the reflected ray in world space against last frame's depth
// and returns last frame's colour at the hit (rgb) with a confidence (a).
vec4 traceScreenSpace(vec3 origin, vec3 dir, float roughness) {
    if (lights.uMaterialFlags.w < 0.5 || roughness > lights.uSsrParams.y) return vec4(0.0);
    int steps = reflectionStepCount(roughness);
    float maxDist = max(lights.uSsrParams.w, 1.0);
    if (lights.uReflectPerf.x > 0.5) {
        maxDist = min(maxDist, max(lights.uReflectQuality.w, 1.0));
        float stride = max(lights.uReflectPerf.w, 1.0);
        if (stride > 1.5 && mod(lights.uScreen.z, stride) >= 0.5) steps = min(steps, 4);
    }
    float jitter = interleavedGradientNoise(gl_FragCoord.xy + lights.uScreen.z * 5.588238);
    float prevT = 0.0;
    for (int i = 1; i <= 64; ++i) {
        if (i > steps) break;
        float s = (float(i) - 1.0 + jitter) / float(steps);
        float t = maxDist * s * s + 0.02;
        vec2 uv;
        float rayDist;
        if (!prevScreenUv(origin + dir * t, uv, rayDist)) break;
        float sceneDist = prevLinearDepth(textureLod(uPrevDepth, uv, 0.0).r);
        float diff = rayDist - sceneDist;
        float thickness = max(0.25, (t - prevT) * 1.5 + t * 0.03);
        if (diff > 0.0 && diff < thickness) {
            float lo = prevT;
            float hi = t;
            int refine = lights.uReflectQuality.x > 0.5 ? 8 : 5;
            for (int k = 0; k < 8; ++k) {
                if (k >= refine) break;
                float mid = 0.5 * (lo + hi);
                vec2 muv;
                float mDist;
                if (!prevScreenUv(origin + dir * mid, muv, mDist)) break;
                float mScene = prevLinearDepth(textureLod(uPrevDepth, muv, 0.0).r);
                if (mDist > mScene) hi = mid; else lo = mid;
            }
            vec2 hitUv;
            float hitDist;
            if (!prevScreenUv(origin + dir * hi, hitUv, hitDist)) break;
            vec3 hit = denoiseReflection(hitUv, textureLod(uPrevColor, hitUv, 0.0).rgb);
            vec2 edge = min(hitUv, 1.0 - hitUv);
            float fade = smoothstep(0.0, 0.08, min(edge.x, edge.y));
            fade *= 1.0 - smoothstep(0.7, 1.0, hi / maxDist);
            float maxRough = max(lights.uSsrParams.y, 0.01);
            fade *= 1.0 - smoothstep(maxRough * 0.6, maxRough, roughness);
            if (lights.uReflectPerf.x > 0.5) {
                float dither = interleavedGradientNoise(gl_FragCoord.xy + vec2(17.0, 3.0));
                fade = clamp(fade + (dither - 0.5) * 0.12, 0.0, 1.0);
            }
            return vec4(hit, clamp(fade * lights.uSsrParams.x, 0.0, 1.0));
        }
        prevT = t;
    }
    return vec4(0.0);
}

vec3 sampleProbeOrSky(vec3 dir, float roughness) {
    float blur = sqrt(clamp(roughness, 0.0, 1.0));
    if (lights.uIblParams.z > 0.5) {
        float lod = blur * lights.uProjParams.z;
        vec3 probe = textureLod(uProbe, dir, lod).rgb;
        if (lights.uReflectQuality.x > 0.5 && lights.uReflectExtra.y > 1.0) {
            float lod2 = min(lod + lights.uReflectExtra.y * 0.15, lights.uProjParams.z);
            probe = mix(probe, textureLod(uProbe, dir, lod2).rgb, 0.35);
        }
        return probe;
    }
    return sampleEnvironment(dir, blur);
}

vec3 specularRadiance(vec3 R, float roughness) {
    vec3 radiance = sampleProbeOrSky(R, roughness);
    vec4 ssr = traceScreenSpace(v_worldPos + normalize(v_normal) * 0.02, R, roughness);
    radiance = mix(radiance, ssr.rgb, ssr.a);
    if (lights.uReflectQuality.x > 0.5 && lights.uReflectExtra.z >= 0.5) {
        vec3 bounceDir = normalize(reflect(R, vec3(0.0, 1.0, 0.0)));
        float weight = 0.1 * min(lights.uReflectExtra.z, 2.0) * (1.0 - clamp(roughness, 0.0, 1.0));
        radiance += sampleProbeOrSky(bounceDir, max(roughness, 0.25)) * weight;
    }
    return radiance;
}

void applyIbl(Surface s, inout vec3 diffuseOut, inout vec3 specularOut) {
    // Diffuse IBL (sky as fill light) is gated by uIblParams.y.
    // Specular IBL (metals reflecting the sky / probes) stays on whenever an
    // environment or probe is bound — the skybox is a reflection source, not a lamp.
    bool diffuseIbl = lights.uIblParams.y > 0.5;
    float specAmt = max(lights.uIblColor.a, 0.0);
    bool envBound = lights.uExtra.z > 0.0 || lights.uIblParams.z > 0.5;
    if (!diffuseIbl && specAmt <= 0.0) return;
    if (!diffuseIbl && !envBound) return;

    float NoV = max(dot(s.n, s.v), 1e-4);
    vec3 R = reflect(-s.v, s.n);
    vec3 specWeight = iridescentWeight(s, envBrdfApprox(s.F0, s.roughness, NoV));
    vec3 kd = (1.0 - specWeight) * (1.0 - s.metallic) * (1.0 - s.transmission);

    vec3 diffuse = vec3(0.0);
    if (diffuseIbl) {
        vec3 irradiance = sampleEnvironment(s.n, 1.0);
        diffuse = kd * irradiance * s.albedo * max(lights.uIblParams.x, 0.0) * s.ao;
        if (maxComponent(s.sheen) > 0.0) {
            diffuse += s.sheen * irradiance * s.ao * mix(0.08, 0.5, pow(1.0 - NoV, 3.0));
        }
    }

    float specOcclusion =
        clamp(pow(NoV + s.ao, exp2(-16.0 * s.roughness - 1.0)) - 1.0 + s.ao, 0.0, 1.0);
    vec3 spec = vec3(0.0);
    if (specAmt > 0.0 && (diffuseIbl || envBound)) {
        spec = specWeight * specularRadiance(R, s.roughness) * specAmt * specOcclusion;
        if (s.clearcoat > 0.0) {
            float Fc = (0.04 + 0.96 * pow(1.0 - NoV, 5.0)) * s.clearcoat;
            vec3 Rc = reflect(-s.v, normalize(v_normal));
            diffuse *= 1.0 - Fc;
            spec = spec * (1.0 - Fc) + Fc * specularRadiance(Rc, s.clearcoatRoughness) * specAmt *
                                           specOcclusion;
        }
    }
    diffuseOut += diffuse;
    specularOut += spec;
}

// What shows through a transmissive surface: the opaque scene behind it, bent by refraction.
vec3 transmittedLight(Surface s) {
    float ior = max(lights.uCoat.z, 1.0);
    vec3 T = refract(-s.v, s.n, 1.0 / ior);
    if (dot(T, T) < 1e-6) T = -s.v;
    if (lights.uMaterialFlags.z > 0.5) {
        float thickness = max(lights.uMaterialParams.z, 0.01);
        vec3 bent = v_worldPos + T * thickness;
        vec4 clip = lights.uViewProj * vec4(bent, 1.0);
        if (clip.w > 0.01) {
            vec2 uv = vec2(clip.x / clip.w * 0.5 + 0.5, 1.0 - (clip.y / clip.w * 0.5 + 0.5));
            uv = clamp(uv, vec2(0.001), vec2(0.999));
            float r = s.roughness * s.roughness * 0.04;
            if (r < 0.0005) return textureLod(uOpaqueScene, uv, 0.0).rgb;
            vec3 sum = vec3(0.0);
            for (int i = 0; i < 8; ++i) {
                float a = float(i) * 2.39996323;
                vec2 o = vec2(cos(a), sin(a)) * r * sqrt((float(i) + 0.5) / 8.0);
                sum += textureLod(uOpaqueScene, clamp(uv + o, vec2(0.001), vec2(0.999)), 0.0).rgb;
            }
            return sum / 8.0;
        }
    }
    return sampleProbeOrSky(T, s.roughness);
}

float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (12.5663706144 * pow(denom, 1.5));
}

vec3 applyVolumetrics(vec3 color) {
    if (lights.uVolParams.x < 0.5 || lights.uVolParams.y <= 0.0001) return color;
    vec3 ray = v_worldPos - lights.uCameraPos.xyz;
    float rayLen = length(ray);
    if (rayLen < 0.05) return color;
    vec3 rd = ray / rayLen;
    float steps = clamp(lights.uVolParams.w, 1.0, 24.0);
    float stepLen = rayLen / steps;
    vec3 lightDir = vec3(0.0, 1.0, 0.0);
    if (lights.uDirCount > 0) lightDir = normalize(-lights.uDirData[0].xyz);
    float g = clamp(lights.uIblParams.w, 0.0, 0.92);
    float hg = henyeyGreenstein(dot(rd, lightDir), g);
    float phase = 0.28 + hg * 2.4;
    bool shadowMaps = lights.uExtra.x > 0.5 && shadows.uDirShadowValid.x > 0.5;
    vec3 accum = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < 24; ++i) {
        if (float(i) >= steps) break;
        vec3 p = lights.uCameraPos.xyz + rd * (stepLen * (float(i) + 0.5));
        float heightFog = exp(-max(p.y, 0.0) / max(lights.uVolParams.z, 8.0));
        float density = lights.uVolParams.y * mix(1.0, heightFog, 0.22);
        float shaft = 0.22;
        if (shadowMaps) {
            shaft = pow(sampleDirShadow(uShadowMap0, 0, p, vec3(0.0)), 1.25);
        }
        float absorb = density * stepLen;
        vec3 fogColor = lights.uAmbient.rgb * 0.10;
        if (lights.uDirCount > 0) {
            fogColor += lights.uDirColor[0].rgb * lights.uDirData[0].w * phase * shaft;
        }
        for (int L = 0; L < 4; ++L) {
            if (L >= lights.uPointCount) break;
            vec3 toL = lights.uPointData[L].xyz - p;
            float dist = length(toL);
            float radius = max(lights.uPointData[L].w, 0.05);
            if (dist > radius) continue;
            float att = 1.0 - dist / radius;
            att *= att;
            float pntHg = henyeyGreenstein(dot(rd, toL / max(dist, 0.001)), g * 0.85);
            fogColor += lights.uPointColor[L].rgb * lights.uPointColor[L].a * att * (0.35 + pntHg * 2.0);
        }
        for (int L = 0; L < 4; ++L) {
            if (L >= lights.uSpotCount) break;
            vec3 toL = lights.uSpotData[L].xyz - p;
            float dist = length(toL);
            float radius = max(lights.uSpotData[L].w, 0.05);
            if (dist > radius) continue;
            vec3 ldir = toL / max(dist, 0.001);
            float cone = dot(-ldir, normalize(lights.uSpotDir[L].xyz));
            if (cone < lights.uSpotAngle[L].x) continue;
            float att = 1.0 - dist / radius;
            float spotHg = henyeyGreenstein(dot(rd, ldir), g);
            fogColor += lights.uSpotColor[L].rgb * lights.uSpotDir[L].w * att * att * (0.40 + spotHg * 2.2);
        }
        accum += transmittance * fogColor * absorb;
        transmittance *= exp(-absorb);
    }
    return color * transmittance + accum;
}

vec3 applyReflection(vec3 n, vec3 viewDir, vec3 color, float roughness) {
    if (lights.uFlags.z < 0.5 || lights.uReflectParams.x <= 0.01) return color;
    vec3 R = reflect(-viewDir, n);
    float dist = max(lights.uReflectParams.w, 0.5);
    vec4 startC = lights.uReflectVP * vec4(v_worldPos, 1.0);
    vec4 endC = lights.uReflectVP * vec4(v_worldPos + R * dist, 1.0);
    if (startC.w <= 0.05) return color;
    vec2 startUv = vec2(startC.x / startC.w * 0.5 + 0.5, 1.0 - (startC.y / startC.w * 0.5 + 0.5));
    vec2 endUv = startUv;
    if (endC.w > 0.05) {
        endUv = vec2(endC.x / endC.w * 0.5 + 0.5, 1.0 - (endC.y / endC.w * 0.5 + 0.5));
    }
    float steps = clamp(dist > 0.6 ? lights.uExtra.y : 1.0, 1.0, 12.0);
    vec2 duv = (endUv - startUv) / steps;
    vec3 accum = vec3(0.0);
    float wsum = 0.0;
    for (int s = 0; s < 12; ++s) {
        if (float(s) >= steps) break;
        vec2 uv = startUv + duv * float(s);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        float w = 1.0 - float(s) / steps;
        vec4 hit = texture(uReflection, uv);
        accum += mix(sampleEnvironment(R, roughness), hit.rgb, hit.a) * w;
        wsum += w;
    }
    if (wsum <= 0.001) return color;
    float rough = clamp(roughness, 0.0, 1.0);
    float amount = lights.uReflectParams.x * (1.0 - rough * 0.65);
    return mix(color, accum / wsum, clamp(amount, 0.0, 1.0));
}

void main() {
    if (lights.uReflectParams.z > 0.5 && v_worldPos.y < lights.uReflectParams.y - 0.02) {
        discard;
    }

    vec2 uv = materialUv();
    vec4 albedoSample = texture(uAlbedoMap, uv);
    float opacity = clamp(albedoSample.a * lights.uAlbedo.a, 0.0, 1.0);
    int alphaMode = int(lights.uMaterialFlags.x + 0.5);
    if (alphaMode == 1 && opacity < lights.uMaterialParams.w) discard;

    Surface s;
    s.n = surfaceNormal(uv);
    s.v = normalize(lights.uCameraPos.xyz - v_worldPos);
    s.metallic = clamp(lights.uMetallic, 0.0, 1.0);
    s.roughness = clamp(lights.uRoughness, 0.0, 1.0);
    s.ao = 1.0;
    if (lights.uFlags.w > 0.5) {
        vec3 orm = texture(uOrm, uv).rgb;
        s.ao = mix(1.0, clamp(orm.r, 0.0, 1.0), clamp(lights.uMaterialParams.y, 0.0, 1.0));
        s.roughness = clamp(orm.g, 0.0, 1.0);
        s.metallic = clamp(orm.b, 0.0, 1.0);
    }
    s.albedo = albedoSample.rgb * lights.uAlbedo.rgb;
    float reflectance = clamp(lights.uReflectance, 0.0, 1.0);
    s.F0 = mix(vec3(reflectance), s.albedo, s.metallic);
    s.clearcoat = clamp(lights.uCoat.x, 0.0, 1.0);
    s.clearcoatRoughness = clamp(lights.uCoat.y, 0.0, 1.0);
    s.transmission = clamp(lights.uCoat.w, 0.0, 1.0) * (1.0 - s.metallic);
    s.sheen = max(lights.uSheen.rgb, vec3(0.0));
    s.sheenRoughness = clamp(lights.uSheen.a, 0.0, 1.0);
    s.iridescence = clamp(lights.uIridescence.x, 0.0, 1.0);
    s.film = vec3(1.0);
    if (s.iridescence > 0.0) {
        s.film = thinFilm(max(dot(s.n, s.v), 0.0), lights.uIridescence.y, lights.uIridescence.z);
    }
    if (s.transmission > 0.0) {
        // Dielectric F0 from the index of refraction.
        float ior = max(lights.uCoat.z, 1.0);
        float f0 = (ior - 1.0) / (ior + 1.0);
        s.F0 = mix(s.F0, vec3(f0 * f0), s.transmission);
    }

    vec3 diffuse = vec3(0.0);
    vec3 specular = vec3(0.0);
    vec3 shadowNormal = normalize(v_normal);
    if (lights.uIblParams.y < 0.5) {
        diffuse += lights.uAmbient.rgb * s.albedo * s.ao * (1.0 - s.transmission);
    }

    for (int i = 0; i < lights.uDirCount; ++i) {
        vec3 dir = normalize(-lights.uDirData[i].xyz);
        float shadow = 1.0;
        if (lights.uFlags.x > 0.5 && lights.uDirData[i].w > 0.0) {
            if (i == 0 && shadows.uDirShadowValid.x > 0.5) {
                shadow = sampleDirShadow(uShadowMap0, 0, v_worldPos, shadowNormal);
            } else if (i == 1 && shadows.uDirShadowValid.y > 0.5) {
                shadow = sampleDirShadow(uShadowMap1, 1, v_worldPos, shadowNormal);
            }
        }
        shadeLight(s, dir, lights.uDirColor[i].rgb * lights.uDirData[i].w * shadow, diffuse, specular);
    }

    for (int i = 0; i < lights.uPointCount; ++i) {
        vec3 toLight = lights.uPointData[i].xyz - v_worldPos;
        float dist = length(toLight);
        float radius = max(lights.uPointData[i].w, 0.001);
        if (dist > radius) continue;

        vec3 ldir = toLight / dist;
        float atten = 1.0 - smoothstep(radius * 0.75, radius, dist);
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
        shadeLight(s, ldir, lights.uPointColor[i].rgb * lights.uPointColor[i].a * atten * shadow,
                   diffuse, specular);
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
        float shadow = 1.0;
        if (lights.uFlags.x > 0.5) {
            if (i == 0 && shadows.uSpotShadowValid.x > 0.5) {
                shadow = sampleSpotShadow(uSpotShadow0, shadows.uSpotShadowVP[0], v_worldPos, shadowNormal);
            } else if (i == 1 && shadows.uSpotShadowValid.y > 0.5) {
                shadow = sampleSpotShadow(uSpotShadow1, shadows.uSpotShadowVP[1], v_worldPos, shadowNormal);
            }
        }
        shadeLight(s, -fromLight,
                   lights.uSpotColor[i].rgb * lights.uSpotDir[i].w * spotAtten * rangeAtten *
                       rangeAtten * shadow,
                   diffuse, specular);
    }

    applyIbl(s, diffuse, specular);

    if (s.transmission > 0.0) {
        float NoV = max(dot(s.n, s.v), 1e-4);
        vec3 through = 1.0 - envBrdfApprox(s.F0, s.roughness, NoV);
        diffuse += transmittedLight(s) * s.albedo * through * s.transmission;
    }

    vec3 emission = lights.uEmission.rgb;
    if (lights.uMaterialFlags.y > 0.5) emission *= texture(uEmissionMap, uv).rgb;

    if (alphaMode == 2 && s.transmission <= 0.0) {
        // Premultiplied: highlights and reflections stay visible on faint surfaces.
        vec3 color = diffuse * opacity + specular + emission;
        color = applyVolumetrics(color);
        outColor = vec4(color, opacity);
        return;
    }

    vec3 color = diffuse + specular;
    color = applyReflection(s.n, s.v, color, s.roughness);
    color += emission;
    color = applyVolumetrics(color);
    outColor = vec4(color, 1.0);
}

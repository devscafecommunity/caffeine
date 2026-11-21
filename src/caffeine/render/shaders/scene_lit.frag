#version 450

layout(location = 0) in vec3 v_worldPos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_texCoord;
layout(location = 3) in vec3 v_tangent;
layout(location = 4) in vec3 v_bitangent;
layout(location = 5) in vec2 v_uvScale;
layout(location = 7) in float v_foam;   // whitecap amount from the water vertex shader (0 elsewhere)
layout(location = 6) in vec4 v_color; // per-vertex tint (rgb); neutral white without a colour stream // object scale along U / V (see scene_lit.vert)

// Photon G-buffer (attachment order is fixed in GpuSceneRenderer.hpp):
//   1 = albedo          rgb linear, a = 1 on GI-relevant surfaces (RGBA8)
//   2 = normal+material rg octahedral world normal, b roughness, a metallic
//   3 = direct          D_t = direct diffuse + emissive (RGBA16F)
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 gAlbedo;
layout(location = 2) out vec4 gNormalMaterial;
layout(location = 3) out vec4 gDirect;

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
    int uUvObjectScale; // 1 = UVs follow the object scale (used to be std140 padding)
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
    vec4 uFoliage;        // x translucency (light through thin leaves)
    vec4 uModifiers;      // run-time modifiers: x wetness, y snow, z age
    vec4 uModTint;        // rgb albedo tint
    vec4 uWater;          // x water surface, y foam scale, z ripple detail, w time
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
    float translucency;
    vec3 film;          // thin-film reflectance per channel
    float iridescence;
};

vec2 materialUv() {
    vec2 follow = lights.uUvObjectScale != 0 ? v_uvScale : vec2(1.0);
    return v_texCoord * lights.uUvTransform.xy * follow + lights.uUvTransform.zw;
}

vec3 surfaceNormal(vec2 uv) {
    vec3 N = normalize(v_normal);
    if (lights.uFlags.y < 0.5) return N;

    vec3 T = normalize(v_tangent);
    vec3 B = normalize(v_bitangent);
    vec3 map = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
    if (lights.uProjParams.w > 0.5) map.y = -map.y;
    map.xy *= lights.uMaterialParams.x;
    return normalize(mat3(T, B, N) * map);
}

// Parallax occlusion mapping. The height lives in the ALPHA channel of the normal map
// (1 = surface, 0 = deepest), so it needs no extra sampler. The view ray is marched through
// the height field and the UV where it first dips below the surface is returned, refined
// between the last two samples. uIridescence.w carries the depth (UV units, 0 = off) and
// uReflectExtra.w the maximum number of steps.
//
// The ray is expressed in the *UV* frame (dP/du, dP/dv) rebuilt from screen-space
// derivatives, not from the mesh tangents: those only agree with the UVs on well-authored
// meshes, and a tangent that is not along `u` shears the whole effect into an oblique plane.
bool g_pomActive = false;
float g_pomHitDepth = 0.0;
vec2 g_pomHitUv = vec2(0.0);
vec3 g_pomT = vec3(1.0, 0.0, 0.0);
vec3 g_pomB = vec3(0.0, 0.0, 1.0);
float g_pomScale = 0.0;

vec2 parallaxUv(vec2 uv) {
    float depthScale = lights.uIridescence.w;
    if (depthScale <= 0.0 || lights.uFlags.y < 0.5) return uv;
    vec3 N = normalize(v_normal);
    vec3 dp1 = dFdx(v_worldPos);
    vec3 dp2 = dFdy(v_worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x; // direction of increasing u
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y; // direction of increasing v
    if (dot(T, T) < 1e-20 || dot(B, B) < 1e-20) return uv;
    T = normalize(T);
    B = normalize(B);
    vec3 V = normalize(lights.uCameraPos.xyz - v_worldPos);
    vec3 Vt = vec3(dot(V, T), dot(V, B), dot(V, N));
    if (Vt.z <= 0.02) return uv; // back-facing: keep the flat lookup
    float maxSteps = clamp(lights.uReflectExtra.w, 4.0, 96.0);
    // Fewer layers when looking straight on, more at grazing angles.
    float stepsF = mix(maxSteps, max(8.0, maxSteps * 0.4), clamp(Vt.z, 0.0, 1.0));
    int steps = int(stepsF);
    float layer = 1.0 / stepsF;
    // Offset limiting: the divisor is floored so grazing angles do not smear the surface.
    vec2 delta = (Vt.xy / max(Vt.z, 0.3)) * depthScale / stepsF;

    vec2 cur = uv;
    float curLayer = 0.0;
    float h = 1.0 - textureGrad(uNormalMap, cur, duv1, duv2).a;
    vec2 prev = cur;
    float prevLayer = 0.0;
    float prevH = h;
    for (int i = 0; i < 96; ++i) {
        if (i >= steps || curLayer >= h) break;
        prev = cur;
        prevLayer = curLayer;
        prevH = h;
        // The alpha channel is height (1 = surface, 0 = deepest). The view ray
        // travels from the surface toward the camera, so a recessed texel is
        // reached by moving in the camera-projected UV direction.
        cur += delta;
        h = 1.0 - textureGrad(uNormalMap, cur, duv1, duv2).a;
        curLayer += layer;
    }
    float after = h - curLayer;
    float before = prevH - prevLayer;
    float w = clamp(after / (after - before - 1e-5), 0.0, 1.0);
    g_pomActive = true;
    g_pomHitUv = mix(cur, prev, w);
    g_pomHitDepth = mix(curLayer, prevLayer, w);
    g_pomT = T;
    g_pomB = B;
    g_pomScale = depthScale;
    return g_pomHitUv;
}

// Self-shadowing: march from the point the view ray hit towards the light through the same
// height field; the crevices of the relief shade themselves. Returns 1 when lit.
float parallaxSelfShadow(vec3 L) {
    if (!g_pomActive) return 1.0;
    vec3 N = normalize(v_normal);
    vec3 Lt = vec3(dot(L, g_pomT), dot(L, g_pomB), dot(L, N));
    if (Lt.z <= 0.05 || g_pomHitDepth <= 0.01) return 1.0;
    const int kSteps = 12;
    vec2 stepUv = (Lt.xy / max(Lt.z, 0.3)) * g_pomScale * (g_pomHitDepth / float(kSteps));
    float stepDepth = g_pomHitDepth / float(kSteps);
    vec2 uv = g_pomHitUv;
    float rayDepth = g_pomHitDepth;
    float occlusion = 0.0;
    for (int i = 1; i <= kSteps; ++i) {
        uv += stepUv;
        rayDepth -= stepDepth;
        float surfaceDepth = 1.0 - textureLod(uNormalMap, uv, 0.0).a;
        // The surface is above the ray: something blocks the light, more so deeper in.
        occlusion = max(occlusion, (rayDepth - surfaceDepth) * (1.0 - float(i) / float(kSteps + 1)));
    }
    return 1.0 - clamp(occlusion * 6.0, 0.0, 1.0);
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
void shadeLight(Surface s, vec3 l, vec3 radiance, float occlusion, inout vec3 diffuseOut,
                inout vec3 specularOut) {
    float NoLRaw = dot(s.n, l);
    if (s.translucency > 0.0) {
        // Thin leaves pass light: the face turned away from the sun glows in the leaf colour
        // (more so when the viewer looks toward the sun through it) instead of going black.
        float through = max(-NoLRaw, 0.0);
        float glow = pow(max(dot(s.v, -l), 0.0), 3.0);
        vec3 tint = sqrt(max(s.albedo, vec3(0.0)));
        diffuseOut += tint * s.albedo * s.translucency * (through * 0.55 + glow * 0.6) * radiance * occlusion;
    }
    float NoL = max(NoLRaw, 0.0);
    if (NoL <= 0.0) return;
    vec3 h = normalize(s.v + l);
    float NoV = max(dot(s.n, s.v), 1e-4);
    float NoH = max(dot(s.n, h), 0.0);
    float VoH = max(dot(s.v, h), 0.0);

    float a = max(s.roughness * s.roughness, 0.002);
    vec3 F = iridescentWeight(s, F_Schlick(s.F0, VoH));
    // D*V already contains the BRDF denominator; multiplying by PI made direct
    // highlights ~3x too bright and blew out the normal/height detail on faces
    // pointing at the sun.
    vec3 spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
    vec3 kd = (1.0 - F) * (1.0 - s.metallic) * (1.0 - s.transmission);
    vec3 diffuse = kd * s.albedo * s.ao;

    if (maxComponent(s.sheen) > 0.0) {
        diffuse += s.sheen * D_Charlie(s.sheenRoughness, NoH) * V_Neubelt(NoV, NoL) * PI;
    }
    if (s.clearcoat > 0.0) {
        float ac = max(s.clearcoatRoughness * s.clearcoatRoughness, 0.002);
        float Fc = (0.04 + 0.96 * pow(1.0 - VoH, 5.0)) * s.clearcoat;
        float coat = D_GGX(NoH, ac) * V_SmithGGXCorrelated(NoV, NoL, ac) * Fc;
        diffuse *= 1.0 - Fc;
        spec = spec * (1.0 - Fc) + vec3(coat);
    }
    // The height cavity also darkens direct light, so a face pointing at the
    // sun keeps its grooves instead of turning into a flat white/plastic plate.
    diffuseOut += diffuse * radiance * NoL * occlusion;
    specularOut += spec * radiance * NoL * occlusion;
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
    // Water takes no screen-space reflection: it writes no depth, so the trace (last frame's depth
    // and colour) hit stale or black pixels and left large dark patches with hard edges.
    vec4 ssr = lights.uWater.x > 0.5
                   ? vec4(0.0)
                   : traceScreenSpace(v_worldPos + normalize(v_normal) * 0.02, R, roughness);
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
    // A rippled water normal can tilt the mirror direction below the horizon, into the dark ground
    // half of the environment: smooth black patches with hard edges. The ray would really hit the
    // next wave, so reflect it back up instead.
    const bool onWater = lights.uWater.x > 0.5;
    if (onWater) R.y = abs(R.y);
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
            if (onWater) Rc.y = abs(Rc.y);
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

vec3 applyReflection(vec3 n, vec3 viewDir, vec3 color, float roughness, float occlusion) {
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
    float amount = lights.uReflectParams.x * (1.0 - rough * 0.65) * clamp(occlusion, 0.0, 1.0);
    return mix(color, accum / wsum, clamp(amount, 0.0, 1.0));
}

// Octahedral normal packing used by the Photon G-buffer (RG8, world space).
vec2 octEncode(vec3 n) {
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6);
    vec2 e = n.xy;
    if (n.z < 0.0) {
        e = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return e * 0.5 + 0.5;
}

void writePhotonGBuffer(vec3 albedo, vec3 normal, float roughness, float metallic,
                        vec3 direct) {
    gAlbedo = vec4(max(albedo, vec3(0.0)), 1.0);
    gNormalMaterial = vec4(octEncode(normal), clamp(roughness, 0.0, 1.0),
                           clamp(metallic, 0.0, 1.0));
    gDirect = vec4(max(direct, vec3(0.0)), 1.0);
}

// ---- water look: procedural ripple detail and foam breakup (no textures)
float waterHash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float waterNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(waterHash(i), waterHash(i + vec2(1.0, 0.0)), f.x),
               mix(waterHash(i + vec2(0.0, 1.0)), waterHash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float waterFbm(vec2 p) {
    return waterNoise(p) * 0.55 + waterNoise(p * 2.07 + 17.0) * 0.3 + waterNoise(p * 4.3 + 41.0) * 0.15;
}

// Ripple noise: gradient (Perlin) noise with a PCG integer hash. Value noise with a smooth blend has a
// zero slope along every lattice line, which showed as thin flat, mirror-bright lines in a regular grid
// across the water; gradient noise has a slope everywhere, and every octave is rotated so no two
// lattices line up.
uint waterPcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

vec2 waterGrad(ivec2 cell) {
    uint h = waterPcg(uint(cell.x) + waterPcg(uint(cell.y) + 0x9e3779b9u));
    float a = float(h & 0xffffu) * (6.2831853 / 65536.0);
    return vec2(cos(a), sin(a));
}

float waterPerlin(vec2 p) {
    ivec2 i = ivec2(floor(p));
    vec2 f = p - vec2(i);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(waterGrad(i), f);
    float b = dot(waterGrad(i + ivec2(1, 0)), f - vec2(1.0, 0.0));
    float c = dot(waterGrad(i + ivec2(0, 1)), f - vec2(0.0, 1.0));
    float d = dot(waterGrad(i + ivec2(1, 1)), f - vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float waterRippleHeight(vec2 p) {
    const mat2 rot = mat2(0.8, 0.6, -0.6, 0.8);
    float h = waterPerlin(p) * 0.55;
    p = rot * p * 2.07 + 17.0;
    h += waterPerlin(p) * 0.3;
    p = rot * p * 2.07 + 41.0;
    h += waterPerlin(p) * 0.15;
    return h;
}

// Slope of two ripple layers drifting in different directions.
vec2 waterRipples(vec2 p, float t) {
    vec2 a = p * 0.4 + vec2(t * 0.04, t * 0.025);
    mat2 turn = mat2(0.6, -0.8, 0.8, 0.6);
    vec2 b = turn * p * 1.05 + vec2(-t * 0.03, t * 0.05);
    const float e = 0.02;
    float ca = waterRippleHeight(a);
    float cb = waterRippleHeight(b);
    vec2 ga = vec2(waterRippleHeight(a + vec2(e, 0.0)) - ca, waterRippleHeight(a + vec2(0.0, e)) - ca) / e;
    vec2 gb = vec2(waterRippleHeight(b + vec2(e, 0.0)) - cb, waterRippleHeight(b + vec2(0.0, e)) - cb) / e;
    // The second layer's slope is in its own rotated frame: bring it back to world axes.
    gb = transpose(turn) * gb;
    return ga * 0.4 + gb * 0.25;
}

void main() {
    if (lights.uReflectParams.z > 0.5 && v_worldPos.y < lights.uReflectParams.y - 0.02) {
        discard;
    }
    vec2 uv = parallaxUv(materialUv());
    // Height-field micro-occlusion for the non-directional lighting. The direct
    // lights get parallaxSelfShadow(); ambient, IBL and reflections need this
    // cavity term or the relief disappears when the sun is not grazing.
    float microAo = 1.0;
    if (g_pomActive) {
        float surfaceHeight = textureLod(uNormalMap, uv, 0.0).a;
        microAo = mix(0.35, 1.0, clamp(surfaceHeight, 0.0, 1.0));
    }
    vec4 albedoSample = texture(uAlbedoMap, uv);
    float opacity = clamp(albedoSample.a * lights.uAlbedo.a, 0.0, 1.0);
    int alphaMode = int(lights.uMaterialFlags.x + 0.5);
    if (alphaMode == 1 && opacity < lights.uMaterialParams.w) discard;

    Surface s;
    s.n = surfaceNormal(uv);
    float waterFoam = 0.0;
    bool isWater = lights.uWater.x > 0.5;
    if (isWater) {
        // Fine ripples the grid cannot show, fading with distance so the far sea stays calm.
        float camDist = length(lights.uCameraPos.xyz - v_worldPos);
        float fade = 1.0 - smoothstep(25.0, 140.0, camDist);
        vec2 g = waterRipples(v_worldPos.xz, lights.uWater.w);
        s.n = normalize(s.n + vec3(g.x, 0.0, g.y) * lights.uWater.z * fade);
    }
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
    s.albedo = albedoSample.rgb * lights.uAlbedo.rgb * v_color.rgb;
    // Run-time modifiers: the same material reacts to the world without re-baking its maps.
    s.albedo *= lights.uModTint.rgb;
    float modAge = clamp(lights.uModifiers.z, 0.0, 1.0);
    if (modAge > 0.0) {
        // Old: washed-out, darker, rougher.
        float luma = dot(s.albedo, vec3(0.2126, 0.7152, 0.0722));
        s.albedo = mix(s.albedo, vec3(luma) * vec3(1.0, 0.92, 0.78), modAge * 0.6) * (1.0 - 0.25 * modAge);
        s.roughness = mix(s.roughness, 1.0, modAge * 0.5);
    }
    float modWet = clamp(lights.uModifiers.x, 0.0, 1.0);
    if (modWet > 0.0) {
        // Wet: darker albedo and a glossy film.
        s.albedo *= mix(1.0, 0.55, modWet);
        s.roughness = mix(s.roughness, 0.08, modWet * 0.85);
    }
    float modSnow = clamp(lights.uModifiers.y, 0.0, 1.0);
    if (modSnow > 0.0) {
        // Snow settles on what faces up: the more snow, the further down the slopes it reaches.
        float up = clamp(normalize(v_normal).y, 0.0, 1.0);
        float cover = smoothstep(1.0 - modSnow, 1.0 - modSnow + 0.2, up);
        s.albedo = mix(s.albedo, vec3(0.92, 0.94, 0.98), cover);
        s.roughness = mix(s.roughness, 0.65, cover);
        s.metallic *= 1.0 - cover;
    }
    if (isWater) {
        // Depth colour: looking down shows the lit, turquoise shallows, grazing views the deep
        // blue; crests facing the sun let light through.
        float facing = clamp(dot(s.n, s.v), 0.0, 1.0);
        vec3 deepColor = s.albedo * vec3(0.30, 0.45, 0.65);
        vec3 shallowColor = s.albedo * vec3(0.9, 1.9, 1.7);
        s.albedo = mix(deepColor, shallowColor, pow(facing, 1.6));
        // Whitecaps where crests compress, broken up by noise.
        float breakup = smoothstep(0.52, 0.8, waterFbm(v_worldPos.xz * 1.3 + lights.uWater.w * 0.1) + 0.22);
        waterFoam = clamp(v_foam * lights.uWater.y, 0.0, 1.0) * breakup;
        s.albedo = mix(s.albedo, vec3(0.92, 0.96, 1.0), waterFoam);
        s.roughness = mix(s.roughness, 0.9, waterFoam);
    }
    float reflectance = clamp(lights.uReflectance, 0.0, 1.0);
    s.F0 = mix(vec3(reflectance), s.albedo, s.metallic);
    s.clearcoat = clamp(lights.uCoat.x, 0.0, 1.0);
    s.clearcoatRoughness = clamp(lights.uCoat.y, 0.0, 1.0);
    s.transmission = clamp(lights.uCoat.w, 0.0, 1.0) * (1.0 - s.metallic);
    s.translucency = clamp(lights.uFoliage.x, 0.0, 1.0);
    s.transmission *= 1.0 - waterFoam;
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

    // Ambient is tracked separately: D_t (the Photon G-buffer) is direct
    // diffuse + emissive only, so IBL/ambient/SSR are not double counted.
    vec3 ambient = vec3(0.0);
    if (lights.uIblParams.y < 0.5) {
        ambient = lights.uAmbient.rgb * s.albedo * s.ao * (1.0 - s.transmission) * microAo;
    }
    vec3 diffuse = ambient;
    vec3 specular = vec3(0.0);
    vec3 shadowNormal = normalize(v_normal);

    for (int i = 0; i < lights.uDirCount; ++i) {
        vec3 dir = normalize(-lights.uDirData[i].xyz);
        float pomShadow = parallaxSelfShadow(dir);
        float shadow = pomShadow;
        if (lights.uFlags.x > 0.5 && lights.uDirData[i].w > 0.0) {
            if (i == 0 && shadows.uDirShadowValid.x > 0.5) {
                shadow = sampleDirShadow(uShadowMap0, 0, v_worldPos, shadowNormal) * pomShadow;
            } else if (i == 1 && shadows.uDirShadowValid.y > 0.5) {
                shadow = sampleDirShadow(uShadowMap1, 1, v_worldPos, shadowNormal) * pomShadow;
            }
        }
        shadeLight(s, dir, lights.uDirColor[i].rgb * lights.uDirData[i].w * shadow, microAo,
                   diffuse, specular);
    }

    for (int i = 0; i < lights.uPointCount; ++i) {
        vec3 toLight = lights.uPointData[i].xyz - v_worldPos;
        float dist = length(toLight);
        float radius = max(lights.uPointData[i].w, 0.001);
        if (dist > radius) continue;

        vec3 ldir = toLight / dist;
        float atten = 1.0 - smoothstep(radius * 0.75, radius, dist);
        float shadow = parallaxSelfShadow(ldir);
        if (lights.uFlags.x > 0.5) {
            if (i == 0 && shadows.uPointShadowValid.x > 0.5) {
                shadow *= samplePointShadow(uPointShadow0, v_worldPos,
                    shadows.uPointShadowPos[0].xyz, shadows.uPointShadowRadius.x);
            } else if (i == 1 && shadows.uPointShadowValid.y > 0.5) {
                shadow *= samplePointShadow(uPointShadow1, v_worldPos,
                    shadows.uPointShadowPos[1].xyz, shadows.uPointShadowRadius.y);
            }
        }
        shadeLight(s, ldir, lights.uPointColor[i].rgb * lights.uPointColor[i].a * atten * shadow,
                   microAo, diffuse, specular);
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
        float shadow = parallaxSelfShadow(-fromLight);
        if (lights.uFlags.x > 0.5) {
            if (i == 0 && shadows.uSpotShadowValid.x > 0.5) {
                shadow *= sampleSpotShadow(uSpotShadow0, shadows.uSpotShadowVP[0], v_worldPos, shadowNormal);
            } else if (i == 1 && shadows.uSpotShadowValid.y > 0.5) {
                shadow *= sampleSpotShadow(uSpotShadow1, shadows.uSpotShadowVP[1], v_worldPos, shadowNormal);
            }
        }
        shadeLight(s, -fromLight,
                   lights.uSpotColor[i].rgb * lights.uSpotDir[i].w * spotAtten * rangeAtten *
                       rangeAtten * shadow,
                   microAo, diffuse, specular);
    }

    // Emission and direct diffuse are captured before IBL/SSR/transmission.
    vec3 emission = lights.uEmission.rgb;
    if (lights.uMaterialFlags.y > 0.5) emission *= texture(uEmissionMap, uv).rgb;
    vec3 directDiffuse = max(diffuse - ambient, vec3(0.0)) + emission;

    vec3 diffuseBeforeIbl = diffuse;
    vec3 specularBeforeIbl = specular;
    applyIbl(s, diffuse, specular);
    // IBL has no direction to ray-march; scale only its contribution by the
    // height cavity so crevices stay dark even under pure sky light.
    diffuse = diffuseBeforeIbl + (diffuse - diffuseBeforeIbl) * microAo;
    specular = specularBeforeIbl + (specular - specularBeforeIbl) * microAo;

    if (s.transmission > 0.0) {
        float NoV = max(dot(s.n, s.v), 1e-4);
        vec3 through = 1.0 - envBrdfApprox(s.F0, s.roughness, NoV);
        diffuse += transmittedLight(s) * s.albedo * through * s.transmission;
    }

    if (alphaMode == 2 && s.transmission <= 0.0) {
        // Premultiplied: highlights and reflections stay visible on faint surfaces.
        vec3 color = diffuse * opacity + specular + emission;
        color = applyVolumetrics(color);
        writePhotonGBuffer(s.albedo, s.n, s.roughness, s.metallic, directDiffuse);
        outColor = vec4(color, opacity);
        return;
    }

    vec3 color = diffuse + specular;
    color = applyReflection(s.n, s.v, color, s.roughness, microAo);
    color += emission;
    color = applyVolumetrics(color);
    writePhotonGBuffer(s.albedo, s.n, s.roughness, s.metallic, directDiffuse);
    outColor = vec4(color, 1.0);
}

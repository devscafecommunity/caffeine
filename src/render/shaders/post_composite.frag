#version 450

// Final HDR -> display pass: lens effects, AO, fog, bloom, exposure, grading, ACES, sRGB, film.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform CompositeUBO {
    mat4 uInvViewProj;
    vec4 uCamera;     // xyz position, w time (s)
    vec4 uProj;       // z P22, w P23
    vec4 uTexel;      // xy source texel, zw output texel
    vec4 uExposure;   // x manual multiplier, y auto on, z compensation (EV), w frame index
    vec4 uExposureRange; // x min, y max
    vec4 uGrading;    // x contrast, y saturation, z temperature, w tint
    vec4 uBloom;      // x intensity (0 = off)
    vec4 uFog;        // x density, y start, z end, w enabled
    vec4 uFogColor;   // rgb linear
    vec4 uAo;         // x enabled
    vec4 uLens;       // x chromatic aberration, y distortion, z vignette intensity, w vignette smoothness
    vec4 uGrain;      // x intensity, y size
    vec4 uFlags;      // x luma in alpha, y grading on, z sharpen amount
} post;

layout(set = 2, binding = 0) uniform sampler2D uScene;
layout(set = 2, binding = 1) uniform sampler2D uBloomTex;
layout(set = 2, binding = 2) uniform sampler2D uDepth;
layout(set = 2, binding = 3) uniform sampler2D uAoTex;
layout(set = 2, binding = 4) uniform sampler2D uAdapted;

// ACES fitted (Stephen Hill): sRGB -> AP1, RRT + ODT fit, AP1 -> sRGB.
const mat3 kAcesInput = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);
const mat3 kAcesOutput = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602);

vec3 rrtAndOdtFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 acesFitted(vec3 color) {
    color = kAcesInput * color;
    color = rrtAndOdtFit(color);
    color = kAcesOutput * color;
    return clamp(color, 0.0, 1.0);
}

vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 distort(vec2 uv, float k) {
    vec2 d = uv - 0.5;
    float r2 = dot(d, d);
    return 0.5 + d * (1.0 + k * r2) / (1.0 + k * 0.25);
}

vec3 sampleScene(vec2 uv) {
    float ca = post.uLens.x;
    if (ca <= 0.0001) return texture(uScene, uv).rgb;
    vec2 d = uv - 0.5;
    float amount = ca * 0.015 * dot(d, d) * 4.0;
    return vec3(texture(uScene, uv - d * amount).r,
                texture(uScene, uv).g,
                texture(uScene, uv + d * amount).b);
}

void main() {
    vec2 uv = v_uv;
    if (abs(post.uLens.y) > 0.0001) {
        uv = distort(uv, post.uLens.y * 0.6);
    }
    vec3 color = sampleScene(uv);

    if (post.uFlags.z > 0.0) {
        vec3 n = texture(uScene, uv + vec2(0.0, -post.uTexel.y)).rgb;
        vec3 s = texture(uScene, uv + vec2(0.0, post.uTexel.y)).rgb;
        vec3 e = texture(uScene, uv + vec2(post.uTexel.x, 0.0)).rgb;
        vec3 w = texture(uScene, uv + vec2(-post.uTexel.x, 0.0)).rgb;
        vec3 sharpened = color + (color * 4.0 - n - s - e - w) * post.uFlags.z * 0.25;
        color = max(sharpened, vec3(0.0));
    }

    float depth = textureLod(uDepth, uv, 0.0).r;
    if (post.uAo.x > 0.5) {
        color *= texture(uAoTex, uv).r;
    }

    if (post.uFog.w > 0.5) {
        vec2 ndc = vec2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
        vec4 world = post.uInvViewProj * vec4(ndc, depth, 1.0);
        world /= world.w;
        vec3 toPixel = world.xyz - post.uCamera.xyz;
        float dist = length(toPixel);
        float fog;
        if (depth >= 0.99999) {
            // Sky: only the band near the horizon takes fog, so the ground fades into it.
            vec3 dir = toPixel / max(dist, 1e-4);
            fog = pow(1.0 - clamp(abs(dir.y), 0.0, 1.0), 6.0);
        } else {
            float d = max(dist - post.uFog.y, 0.0);
            fog = 1.0 - exp(-post.uFog.x * d);
            float endT = clamp((dist - post.uFog.y) / max(post.uFog.z - post.uFog.y, 1e-3), 0.0, 1.0);
            fog = max(fog, endT * endT);
        }
        color = mix(color, post.uFogColor.rgb, clamp(fog, 0.0, 1.0));
    }

    if (post.uBloom.x > 0.0) {
        color += texture(uBloomTex, uv).rgb * post.uBloom.x;
    }

    float exposure = max(post.uExposure.x, 0.0);
    if (post.uExposure.y > 0.5) {
        float avgLog = texture(uAdapted, vec2(0.5)).r;
        float autoExposure = 0.18 / max(exp2(avgLog), 1e-4);
        autoExposure = clamp(autoExposure, post.uExposureRange.x, post.uExposureRange.y);
        exposure *= autoExposure;
    }
    color *= exposure * exp2(post.uExposure.z);

    if (post.uFlags.y > 0.5) {
        float temp = post.uGrading.z;
        float tint = post.uGrading.w;
        color *= vec3(1.0 + temp * 0.18, 1.0 - tint * 0.12, 1.0 - temp * 0.18);
        // Contrast pivots on mid grey in log space so it never clips shadows to black.
        vec3 logC = log2(max(color, vec3(1e-5)) / 0.18);
        color = 0.18 * exp2(logC * post.uGrading.x);
        color = max(mix(vec3(luma(color)), color, post.uGrading.y), vec3(0.0));
    }

    vec3 display = linearToSrgb(acesFitted(color));

    if (post.uLens.z > 0.0) {
        vec2 d = (v_uv - 0.5) * vec2(post.uTexel.w / post.uTexel.z, 1.0);
        float radius = length(d);
        float softness = max(post.uLens.w, 0.01);
        float inner = mix(0.55, 0.15, softness);
        float outer = inner + mix(0.15, 0.65, softness);
        float vignette = smoothstep(inner, outer, radius);
        display *= 1.0 - post.uLens.z * vignette;
    }

    if (post.uGrain.x > 0.0) {
        vec2 cell = floor(gl_FragCoord.xy / max(post.uGrain.y, 0.5));
        float n = hash12(cell + fract(post.uCamera.w * 7.13) * 917.0) - 0.5;
        float response = 1.0 - luma(display) * 0.6;
        display += n * post.uGrain.x * 0.18 * response;
    }

    display = clamp(display, 0.0, 1.0);
    float alpha = post.uFlags.x > 0.5 ? dot(display, vec3(0.299, 0.587, 0.114)) : 1.0;
    outColor = vec4(display, alpha);
}

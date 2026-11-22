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
    // Weather (scene fog + rain). All zero = the pass is unchanged.
    vec4 uWFogA;      // x density, y height falloff, z base height, w start distance
    vec4 uWFogB;      // x max opacity, y sky blend, z sun scatter, w sun exponent
    vec4 uWFogColor;  // rgb linear colour, w enabled
    vec4 uWSunDir;    // xyz toward the sun
    vec4 uWSunColor;  // rgb linear
    vec4 uWSky;       // x environment exposure (0 = none), y environment lod
    vec4 uWHorizon;   // rgb sky colour at the horizon when there is no environment
    vec4 uRainA;      // x intensity (0 = off), y drop length (0..1), z fall speed multiplier, w splash strength
    vec4 uRainB;      // x brightness
    vec4 uRainColor;  // rgb
    vec4 uRainWind;   // xyz wind velocity in world space (m/s, gusts included)
    vec4 uFlash;      // rgb colour of a lightning flash, a its strength (0 = none)
    vec4 uShafts;     // xy sun position on screen (uv), z strength (0 = off), w sun in front of the camera
} post;

layout(set = 2, binding = 0) uniform sampler2D uScene;
layout(set = 2, binding = 1) uniform sampler2D uBloomTex;
layout(set = 2, binding = 2) uniform sampler2D uDepth;
layout(set = 2, binding = 3) uniform sampler2D uAoTex;
layout(set = 2, binding = 4) uniform sampler2D uAdapted;
layout(set = 2, binding = 5) uniform sampler2D uEnvironment;

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


vec2 directionToEquirect(vec3 d) {
    float u = atan(d.x, d.z) / 6.28318530 + 0.5;
    float v = 0.5 - asin(clamp(d.y, -1.0, 1.0)) / 3.14159265;
    return vec2(u, v);
}

// Optical depth of exponential height fog along a ray of length `dist` in direction `dir`.
float weatherOpticalDepth(float dist, vec3 dir) {
    float f = post.uWFogA.y;
    float rho = post.uWFogA.x * exp(-f * clamp(post.uCamera.y - post.uWFogA.z, -400.0, 400.0));
    float s0 = min(post.uWFogA.w, dist);
    float len = dist - s0;
    float k = f * dir.y;
    float shape;
    if (abs(k) < 1e-4) {
        shape = len;
    } else {
        shape = (exp(min(-k * s0, 40.0)) - exp(min(-k * dist, 40.0))) / k;
    }
    return max(rho * shape, 0.0);
}

vec3 weatherFog(vec3 color, vec3 dir, float dist, bool isSky) {
    float od = weatherOpticalDepth(dist, isSky ? vec3(dir.x, max(dir.y, 0.0), dir.z) : dir);
    if (isSky) od *= 0.6;
    float amount = (1.0 - exp(-od)) * post.uWFogB.x;
    vec3 horizon = post.uWHorizon.rgb;
    if (post.uWSky.x > 0.0) {
        vec3 flatDir = normalize(vec3(dir.x, 0.02, dir.z) + vec3(1e-5, 0.0, 0.0));
        horizon = textureLod(uEnvironment, directionToEquirect(flatDir), post.uWSky.y).rgb * post.uWSky.x;
    }
    vec3 fogColor = mix(post.uWFogColor.rgb, horizon, post.uWFogB.y);
    float phase = pow(max(dot(dir, post.uWSunDir.xyz), 0.0), post.uWFogB.w);
    fogColor += post.uWSunColor.rgb * post.uWFogB.z * phase;
    // Lightning inside the fog: the lit mist is what you see of a strike behind the rain.
    fogColor += post.uFlash.rgb * (post.uFlash.a * 0.35);
    return mix(color, fogColor, clamp(amount, 0.0, 1.0));
}

uint rainHash(uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v.x ^ v.y ^ v.z;
}

vec4 rainRandom(ivec2 cell, int layer) {
    uint h0 = rainHash(uvec3(uint(cell.x + 32768), uint(cell.y + 32768), uint(layer)));
    uint h1 = rainHash(uvec3(h0, uint(layer) + 7u, 3u));
    uint h2 = rainHash(uvec3(h1, h0, 11u));
    uint h3 = rainHash(uvec3(h2, h1, 19u));
    return vec4(float(h0 & 0xFFFFFFu), float(h1 & 0xFFFFFFu), float(h2 & 0xFFFFFFu), float(h3 & 0xFFFFFFu)) / 16777216.0;
}

// Rain as drops in the world, seen through six shells round the camera (2.5 m to 61 m). Every shell is a lattice of
// cells fixed in the world and sheared along the fall, each cell holding one slanted streak: moving the camera
// moves the near streaks across the picture faster than the far ones, a long streak is long because it is near,
// and the wind tilts the whole lattice. A streak shows only where nothing nearer than its shell is in the way.
float rainStreaks(vec3 camera, vec3 dir, float sceneDist, float time) {
    float intensity = clamp(post.uRainA.x, 0.0, 1.0);
    vec3 velocity = vec3(post.uRainWind.x, -9.0 * max(post.uRainA.z, 0.05), post.uRainWind.z);
    vec3 axis = normalize(velocity);
    float cosTilt = abs(axis.y);
    vec2 shear = -axis.xz / axis.y;
    float length_m = mix(0.35, 1.6, clamp(post.uRainA.y, 0.0, 1.0));
    const float period = 1.8;                              // vertical repeat of a cell, metres
    float tanPixel = 2.0 * post.uTexel.w / max(post.uProj.y, 1e-3);
    float sum = 0.0;
    for (int i = 0; i < 6; ++i) {
        float shell = 2.5 * pow(1.9, float(i));
        if (shell > sceneDist) break;
        vec3 q = camera + dir * shell - velocity * mod(time, 64.0);   // the point in the frame that falls with the drops
        vec2 sheared = q.xz + shear * q.y;
        float cell = 0.16 + 0.03 * shell;
        ivec2 id = ivec2(floor(sheared / cell));
        vec4 h = rainRandom(id, i);
        if (h.w > intensity * 0.85) continue;              // fewer cells hold a drop in light rain
        vec2 centre = (vec2(id) + 0.2 + 0.6 * h.xy) * cell;
        float along = mod(q.y + h.z * period, period) - 0.5 * period;  // 0 at the head, positive up the tail
        float tail = along / (length_m * cosTilt);
        if (tail < 0.0 || tail > 1.0) continue;
        float radius = max(shell * tanPixel * 0.9, 0.006);
        float d = length(sheared - centre) * cosTilt;
        float core = 1.0 - smoothstep(0.0, radius, d);
        float fade = exp(-shell / 70.0) * smoothstep(1.2, 3.2, shell);
        sum += core * (1.0 - tail) * (1.0 - tail) * fade * (0.7 + 0.6 * h.x);
    }
    return sum;
}

// Rings where a drop meets a surface that faces up, near the camera: a ring grows and fades in each cell, at a
// phase of its own. The surface's slope comes from the depth next to the pixel.
float rainSplashes(vec3 world, vec3 normal, float dist, float time) {
    float strength = clamp(post.uRainA.w, 0.0, 1.0);
    if (strength <= 0.0 || normal.y < 0.75 || dist > 35.0) return 0.0;
    float intensity = clamp(post.uRainA.x, 0.0, 1.0);
    const float cell = 0.5;
    ivec2 id = ivec2(floor(world.xz / cell));
    vec4 h = rainRandom(id, 40);
    if (h.z > intensity * 0.75) return 0.0;
    float periodS = 0.5 + 0.6 * h.w;
    float age = fract(time / periodS + h.x);
    vec2 centre = (vec2(id) + 0.2 + 0.6 * h.xy) * cell;
    float d = length(world.xz - centre);
    float radius = age * cell * 0.5;
    float ring = exp(-pow((d - radius) / 0.014, 2.0)) * pow(1.0 - age, 1.6);
    float spark = (1.0 - smoothstep(0.0, 0.03, d)) * (1.0 - smoothstep(0.0, 0.14, age));
    return (ring + spark) * strength * (1.0 - smoothstep(18.0, 35.0, dist)) * (0.6 + 0.8 * h.y);
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


    if (post.uWFogColor.w > 0.5 || post.uRainA.x > 0.0) {
        vec2 wndc = vec2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
        vec4 wworld = post.uInvViewProj * vec4(wndc, depth, 1.0);
        wworld /= wworld.w;
        vec3 wToPixel = wworld.xyz - post.uCamera.xyz;
        float wDist = length(wToPixel);
        vec3 wDir = wToPixel / max(wDist, 1e-4);
        bool wSky = depth >= 0.99999;
        if (wSky) wDist = 8000.0;

        if (post.uWFogColor.w > 0.5) {
            color = weatherFog(color, wDir, wDist, wSky);
        }
        if (post.uRainA.x > 0.0) {
            float time = post.uCamera.w;
            float rain = rainStreaks(post.uCamera.xyz, wDir, wSky ? 1.0e9 : wDist, time);
            if (!wSky && wDist < 35.0 && post.uRainA.w > 0.0) {
                vec2 texel = post.uTexel.xy;
                float depthX = textureLod(uDepth, uv + vec2(texel.x, 0.0), 0.0).r;
                float depthY = textureLod(uDepth, uv + vec2(0.0, texel.y), 0.0).r;
                vec4 wx = post.uInvViewProj * vec4(wndc + vec2(2.0 * texel.x, 0.0), depthX, 1.0);
                vec4 wy = post.uInvViewProj * vec4(wndc + vec2(0.0, -2.0 * texel.y), depthY, 1.0);
                vec3 dx = wx.xyz / wx.w - wworld.xyz;
                vec3 dy = wy.xyz / wy.w - wworld.xyz;
                vec3 normal = normalize(cross(dx, dy));
                if (normal.y < 0.0) normal = -normal;
                rain += rainSplashes(wworld.xyz, normal, wDist, time);
            }
            // A flash lights the rain from within the cloud: the streaks flare for its duration.
            float flashBoost = 1.0 + post.uFlash.a * 6.0;
            color += post.uRainColor.rgb * (rain * post.uRainB.x * flashBoost);
            // A wet haze: the scene loses a little contrast in heavy rain.
            color = mix(color, color * vec3(0.92, 0.95, 1.0), clamp(post.uRainA.x * 0.35, 0.0, 0.35));
        }
    }

    if (post.uShafts.z > 0.0 && post.uShafts.w > 0.5) {
        // Light shafts: march towards the sun on the screen and gather the bright sky that nothing blocks. Clouds
        // and geometry in front of the sun cut the light into rays; looking away from the sun costs nothing.
        vec2 toSun = post.uShafts.xy - uv;
        float onScreen = 1.0 - smoothstep(0.6, 1.4, length(toSun));
        if (onScreen > 0.0) {
            const int kShaftSteps = 14;
            vec2 stepUv = toSun * (0.92 / float(kShaftSteps));
            float jitter = hash12(gl_FragCoord.xy + fract(post.uCamera.w) * 31.0);
            vec2 p = uv + stepUv * jitter;
            float gather = 0.0;
            float weight = 1.0;
            for (int i = 0; i < kShaftSteps; ++i) {
                float d = textureLod(uDepth, p, 0.0).r;
                if (d >= 0.99999) {
                    vec3 c = textureLod(uScene, p, 0.0).rgb;
                    gather += clamp(luma(c) - 0.8, 0.0, 6.0) * weight;
                }
                weight *= 0.93;
                p += stepUv;
            }
            color += vec3(1.0, 0.86, 0.62) * (gather / float(kShaftSteps)) * post.uShafts.z * onScreen * 0.9;
        }
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

#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;
// Sky is not a GI source/surface: albedo alpha 0 makes Photon ignore it.
layout(location = 1) out vec4 gAlbedo;
layout(location = 2) out vec4 gNormalMaterial;
layout(location = 3) out vec4 gDirect;

layout(set = 3, binding = 0) uniform SkyUBO {
    mat4 uInvViewProj;
    vec4 uParams;   // x exposure (0 = no environment), y lod
    vec4 uZenith;   // fallback gradient (linear)
    vec4 uHorizon;
    vec4 uGround;
    vec4 uCamera;   // xyz world camera position, w time
    vec4 uCloudA;   // x coverage, y density, z base altitude, w top altitude
    vec4 uCloudB;   // x wind speed, y wind direction (deg), z detail, w seed
    vec4 uCloudC;   // x march steps, y mist density, z mist height, w falloff
    vec4 uSunDir;   // xyz direction toward the sun, w intensity
    vec4 uSunColor; // rgb linear colour
    vec4 uCloudD;   // rgb tint of the sun-lit side, w darkness (storm)
    vec4 uCloudE;   // x sky light inside the clouds
    vec4 uBoltA;    // x bolt azimuth, y elevation of its top, z how visible the bolt is, w flash strength
    vec4 uBoltB;    // x strike seed (shapes the path), y silver lining
} sky;

layout(set = 2, binding = 0) uniform sampler2D uEnvironment;

vec2 directionToEquirect(vec3 d) {
    float u = atan(d.x, d.z) / 6.28318530 + 0.5;
    float v = 0.5 - asin(clamp(d.y, -1.0, 1.0)) / 3.14159265;
    return vec2(u, v);
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(
        mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
        mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y),
        f.z);
}

float fbm(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; ++i) {
        if (i >= octaves) break;
        value += amplitude * valueNoise(p);
        p = p * 2.03 + vec3(17.1, 9.2, 13.7);
        amplitude *= 0.5;
    }
    return value;
}

vec3 windOffset() {
    float angle = radians(sky.uCloudB.y);
    vec3 dir = vec3(cos(angle), 0.0, sin(angle));
    return dir * (sky.uCloudB.x * sky.uCamera.w) + vec3(sky.uCloudB.w);
}

// `withDetail` false is the cheap shape (4 octaves of noise instead of 7): the sun march reads the cloud many
// times per pixel and only needs its broad form to know how much light gets through.
float cloudDensity(vec3 p, vec3 wind, float dist, bool withDetail) {
    float span = max(sky.uCloudA.w - sky.uCloudA.z, 1.0);
    float h = (p.y - sky.uCloudA.z) / span;
    if (h <= 0.0 || h >= 1.0) return 0.0;

    float profile = smoothstep(0.0, 0.16, h) * smoothstep(1.0, 0.72, h);
    vec3 q = (p + wind) * 0.00125;
    float base = fbm(q, 4);
    float detailFade = 1.0 - smoothstep(3500.0, 14000.0, dist);
    float detail = base;
    if (withDetail) {
        detail = fbm(q * 4.0 + vec3(19.0), 3) * (0.35 + 0.65 * detailFade);
    }
    float shape = base * 0.76 + detail * 0.24 * sky.uCloudB.z;
    float cover = smoothstep(1.00 - sky.uCloudA.x, 1.12 - sky.uCloudA.x, shape);
    return cover * profile * sky.uCloudA.y;
}

float cloudSunTransmittance(vec3 p, vec3 sunDir, vec3 wind) {
    float span = max(sky.uCloudA.w - sky.uCloudA.z, 1.0);
    float stepLen = span * 0.1;
    float optical = 0.0;
    for (int i = 1; i <= 3; ++i) {
        vec3 sp = p + sunDir * (stepLen * float(i));
        optical += cloudDensity(sp, wind, length(sp - sky.uCamera.xyz), false) * stepLen;
    }
    return exp(-optical * 0.008);
}

vec3 flashGlow(vec3 dir);

vec3 applyVolumetricClouds(vec3 skyColor, vec3 dir) {
    if (sky.uCloudC.x < 0.5 || sky.uCloudA.y <= 0.0) return skyColor;
    if (dir.y < 0.012) return skyColor;
    float horizonFade = smoothstep(0.02, 0.42, dir.y);

    float base = sky.uCloudA.z;
    float top = sky.uCloudA.w;
    float t0 = (base - sky.uCamera.y) / dir.y;
    float t1 = (top - sky.uCamera.y) / dir.y;
    if (t0 > t1) {
        float tmp = t0;
        t0 = t1;
        t1 = tmp;
    }
    t0 = max(t0, 0.0);
    t1 = min(t1, 18000.0);
    if (t1 <= t0) return skyColor;

    int steps = int(clamp(sky.uCloudC.x, 4.0, 32.0));
    float dt = (t1 - t0) / float(steps);
    vec3 wind = windOffset();
    vec3 acc = vec3(0.0);
    float transmittance = 1.0;
    float jitter = hash13(vec3(gl_FragCoord.xy, sky.uCamera.w));

    for (int i = 0; i < 32; ++i) {
        if (i >= steps) break;
        vec3 p = sky.uCamera.xyz + dir * (t0 + dt * (float(i) + jitter));
        float density = cloudDensity(p, wind, length(p - sky.uCamera.xyz), true);
        if (density > 0.0001) {
            float sun = cloudSunTransmittance(p, sky.uSunDir.xyz, wind);
            float storm = clamp(sky.uCloudD.w, 0.0, 1.0);
            vec3 shadowColor = (vec3(0.16, 0.18, 0.22) + sky.uZenith.rgb * 0.10) * (1.0 - 0.7 * storm);
            vec3 sunLit = sky.uSunColor.rgb * sky.uCloudD.rgb * min(sky.uSunDir.w, 1.6) * 0.85 *
                          (1.0 - 0.6 * storm);
            vec3 lit = mix(shadowColor, sunLit, clamp(sun, 0.0, 1.0));
            if (sky.uBoltB.y > 0.0) {
                // Silver lining: toward the sun, thin edges of cloud glow (forward scattering, strongest where the
                // light has little cloud to cross and the line of sight is close to the sun).
                float cosSun = clamp(dot(dir, sky.uSunDir.xyz), 0.0, 1.0);
                float forward = pow(cosSun, 18.0) * (0.35 + 0.65 * smoothstep(0.05, 0.9, sun));
                float edge = 1.0 - smoothstep(0.0, 0.5, density);   // thin cloud
                lit += sunLit * forward * edge * sky.uBoltB.y * 2.5;
            }
            lit += sky.uZenith.rgb * 0.12 * sky.uCloudE.x;
            lit += flashGlow(dir) * 2.2;   // the flash lights the cloud from inside
            float sigma = density * 0.008;
            float alpha = (1.0 - exp(-sigma * dt)) * horizonFade;
            acc += transmittance * alpha * lit;
            transmittance *= 1.0 - alpha;
            if (transmittance < 0.02) break;
        }
    }

    return skyColor * transmittance + acc;
}

// ---- Lightning: a jagged bolt from the cloud base to the horizon, and the glow of the flash round it.
const vec3 kFlashColor = vec3(0.78, 0.84, 1.0);

float wrapAngle(float a) {
    return a - 6.28318530 * floor((a + 3.14159265) / 6.28318530);
}

// Distance of a pixel's direction from the bolt's path, in radians, and how far down the path it is (0 ground,
// 1 cloud). The path wanders from side to side by a few sines of the strike's seed, so no two bolts match.
vec2 boltPath(vec3 dir, float shift, float seed) {
    float el = asin(clamp(dir.y, -1.0, 1.0));
    float top = max(sky.uBoltA.y, 0.05);
    float t = clamp(el / top, 0.0, 1.0);
    float wander = 0.05 * (0.25 + t) * (0.5 * sin(t * 19.0 + seed * 13.0 + shift) +
                                        0.3 * sin(t * 47.0 + seed * 7.0 + shift * 2.0) +
                                        0.2 * sin(t * 113.0 + seed * 3.0));
    float az = atan(dir.x, dir.z);
    float away = wrapAngle(az - (sky.uBoltA.x + wander + shift * 0.02 * t)) * cos(el);
    return vec2(abs(away), t);
}

vec3 boltColor(vec3 dir) {
    float visible = sky.uBoltA.z;
    float el = asin(clamp(dir.y, -1.0, 1.0));
    if (visible <= 0.01 || el <= 0.0 || el > sky.uBoltA.y) return vec3(0.0);
    float seed = sky.uBoltB.x;
    vec2 main = boltPath(dir, 0.0, seed);
    vec2 branch = boltPath(dir, 2.1, seed + 1.7);
    // The main stroke, a thinner branch that leaves it a little way up, and a soft halo round both.
    float core = exp(-pow(main.x / mix(0.0016, 0.0009, main.y), 2.0));
    float fork = main.y > 0.45 ? exp(-pow(branch.x / 0.0009, 2.0)) * smoothstep(0.45, 0.6, main.y) * 0.6 : 0.0;
    float halo = exp(-pow(main.x / 0.014, 2.0)) * 0.22;
    float fadeBase = smoothstep(0.0, 0.05, main.y);  // the foot of the bolt eases out at the horizon
    return kFlashColor * (core + fork + halo) * fadeBase * visible * 24.0;
}

// The light of the flash seen in the sky: a glow round the bolt and a lift of the whole sky.
vec3 flashGlow(vec3 dir) {
    float flash = sky.uBoltA.w;
    if (flash <= 0.0) return vec3(0.0);
    float away = abs(wrapAngle(atan(dir.x, dir.z) - sky.uBoltA.x));
    float near = exp(-pow(away / 0.55, 2.0));
    return kFlashColor * flash * (0.07 + 0.55 * near) * smoothstep(-0.05, 0.25, dir.y);
}

vec3 applyMist(vec3 color, vec3 dir) {
    float density = clamp(sky.uCloudC.y, 0.0, 1.0);
    if (density <= 0.0) return color;
    float height = max(sky.uCloudC.z, 1.0);
    float ground = exp(-max(sky.uCamera.y, 0.0) / height);
    float horizon = pow(1.0 - clamp(abs(dir.y), 0.0, 1.0), 5.0);
    float mist = clamp(density * ground * horizon * 0.5, 0.0, 0.85);
    vec3 mistColor = mix(sky.uHorizon.rgb, sky.uSunColor.rgb * 0.6, 0.25);
    return mix(color, mistColor, mist);
}

void main() {
    vec2 ndc = vec2(v_uv.x * 2.0 - 1.0, (1.0 - v_uv.y) * 2.0 - 1.0);
    vec4 nearP = sky.uInvViewProj * vec4(ndc, 0.0, 1.0);
    vec4 farP = sky.uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);

    vec3 color;
    if (sky.uParams.x > 0.0) {
        // Explicit LOD: implicit derivatives jump at the u = 0/1 seam and pick the smallest mip.
        color = textureLod(uEnvironment, directionToEquirect(dir), sky.uParams.y).rgb * sky.uParams.x;
    } else {
        float up = clamp(dir.y, -1.0, 1.0);
        color = up >= 0.0
            ? mix(sky.uHorizon.rgb, sky.uZenith.rgb, pow(up, 0.6))
            : mix(sky.uHorizon.rgb, sky.uGround.rgb, pow(-up, 0.4));
    }

    if (dir.y > 0.012) {
        color = applyVolumetricClouds(color, dir);
        color = applyMist(color, dir);
    }
    color += flashGlow(dir) + boltColor(dir);
    gAlbedo = vec4(0.0);
    gNormalMaterial = vec4(0.5, 0.5, 0.0, 0.0);  // +Z; ignored while albedo.a == 0
    gDirect = vec4(0.0);
    outColor = vec4(color, 1.0);
}

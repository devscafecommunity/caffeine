#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

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

float cloudDensity(vec3 p, vec3 wind, float dist) {
    float span = max(sky.uCloudA.w - sky.uCloudA.z, 1.0);
    float h = (p.y - sky.uCloudA.z) / span;
    if (h <= 0.0 || h >= 1.0) return 0.0;

    float profile = smoothstep(0.0, 0.16, h) * smoothstep(1.0, 0.72, h);
    vec3 q = (p + wind) * 0.00125;
    float base = fbm(q, 4);
    float detailFade = 1.0 - smoothstep(3500.0, 14000.0, dist);
    float detail = fbm(q * 4.0 + vec3(19.0), 3) * (0.35 + 0.65 * detailFade);
    float shape = base * 0.76 + detail * 0.24 * sky.uCloudB.z;
    float cover = smoothstep(1.00 - sky.uCloudA.x, 1.12 - sky.uCloudA.x, shape);
    return cover * profile * sky.uCloudA.y;
}

float cloudSunTransmittance(vec3 p, vec3 sunDir, vec3 wind) {
    float span = max(sky.uCloudA.w - sky.uCloudA.z, 1.0);
    float stepLen = span * 0.075;
    float optical = 0.0;
    for (int i = 1; i <= 4; ++i) {
        vec3 sp = p + sunDir * (stepLen * float(i));
        optical += cloudDensity(sp, wind, length(sp - sky.uCamera.xyz)) * stepLen;
    }
    return exp(-optical * 0.008);
}

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
        float density = cloudDensity(p, wind, length(p - sky.uCamera.xyz));
        if (density > 0.0001) {
            float sun = cloudSunTransmittance(p, sky.uSunDir.xyz, wind);
            vec3 shadowColor = vec3(0.16, 0.18, 0.22) + sky.uZenith.rgb * 0.10;
            vec3 sunLit = sky.uSunColor.rgb * min(sky.uSunDir.w, 1.6) * 0.85;
            vec3 lit = mix(shadowColor, sunLit, clamp(sun, 0.0, 1.0));
            lit += sky.uZenith.rgb * 0.12;
            float sigma = density * 0.008;
            float alpha = (1.0 - exp(-sigma * dt)) * horizonFade;
            acc += transmittance * alpha * lit;
            transmittance *= 1.0 - alpha;
            if (transmittance < 0.02) break;
        }
    }

    return skyColor * transmittance + acc;
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
    outColor = vec4(color, 1.0);
}

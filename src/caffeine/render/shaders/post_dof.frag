#version 450

// Gather depth of field: thin-lens circle of confusion from depth, golden-spiral bokeh.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform DofUBO {
    vec4 uProj;    // z P22, w P23
    vec4 uTexel;   // xy texel size, z aspect
    vec4 uParams;  // x focus distance (m), y aperture (0..1 lens opening), z focal length (mm), w max blur (px)
} dof;

layout(set = 2, binding = 0) uniform sampler2D uScene;
layout(set = 2, binding = 1) uniform sampler2D uDepth;

float linearDepth(float d) {
    return dof.uProj.w / (d + dof.uProj.z);
}

// Circle of confusion in pixels (signed: < 0 in front of the focus plane).
float cocPixels(vec2 uv) {
    float z = linearDepth(textureLod(uDepth, uv, 0.0).r);
    float focus = max(dof.uParams.x, 0.05);
    // Thin lens: CoC grows with (z - focus) / z, scaled by opening and focal length.
    float lens = max(dof.uParams.y, 0.0) * (max(dof.uParams.z, 1.0) / 50.0);
    float pixels = lens * 120.0 * (z - focus) / max(z, 1e-3) * (1.0 / dof.uTexel.y) / 1080.0;
    return clamp(pixels, -dof.uParams.w, dof.uParams.w);
}

void main() {
    float centerCoc = cocPixels(v_uv);
    vec3 centerColor = texture(uScene, v_uv).rgb;
    float radius = abs(centerCoc);
    const int kSamples = 40;
    vec3 sum = centerColor;
    float weight = 1.0;
    float maxRadius = dof.uParams.w;
    for (int i = 1; i < kSamples; ++i) {
        float fi = float(i) / float(kSamples);
        float r = sqrt(fi) * maxRadius;
        float a = float(i) * 2.39996323;
        vec2 offset = vec2(cos(a), sin(a)) * r * dof.uTexel.xy;
        vec2 uv = v_uv + offset;
        float sampleCoc = cocPixels(uv);
        // A sample contributes if its own blur reaches us; background can't bleed over
        // a sharper foreground pixel.
        float reach = abs(sampleCoc);
        if (sampleCoc > centerCoc) reach = min(reach, radius * 2.0);
        float w = smoothstep(r - 1.0, r + 1.0, reach);
        sum += texture(uScene, uv).rgb * w;
        weight += w;
    }
    vec3 blurred = sum / weight;
    outColor = vec4(mix(centerColor, blurred, smoothstep(0.5, 2.0, radius)), 1.0);
}

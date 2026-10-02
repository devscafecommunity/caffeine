#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

layout(set = 3, binding = 0) uniform SkyUBO {
    mat4 uInvViewProj;
    vec4 uParams;   // x exposure (0 = no environment), y lod
    vec4 uZenith;   // fallback gradient (linear)
    vec4 uHorizon;
    vec4 uGround;
} sky;

layout(set = 2, binding = 0) uniform sampler2D uEnvironment;

vec2 directionToEquirect(vec3 d) {
    float u = atan(d.x, d.z) / 6.28318530 + 0.5;
    float v = 0.5 - asin(clamp(d.y, -1.0, 1.0)) / 3.14159265;
    return vec2(u, v);
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
    outColor = vec4(color, 1.0);
}
